#!/usr/bin/env python3
"""Compara os quatro escalares da memória com suspension_position do UDP.

Não inventa escala fora de {1, 1000, 0.001}. Um único b vale para as quatro
rodas. Um único deslocamento de amostra vale para a série inteira.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
import sys

SCALES = (1.0, 1000.0, 0.001)
BLOCKS = ("A", "B", "C", "D")
WHEELS = ("RL", "RR", "FL", "FR")
MEM_COL = {"A": "mem_a", "B": "mem_b", "C": "mem_c", "D": "mem_d"}
UDP_COL = {"RL": "udp_rl", "RR": "udp_rr", "FL": "udp_fl", "FR": "udp_fr"}
DECISION_PHASES = ("T1", "T2", "T3", "T4", "T5", "T6")
MAX_LAG = 8
MIN_PHASE_SAMPLES = 30
CORR_IDENTITY = 0.98
ERR_FRAC = 0.02
B_FRAC = 0.01
SEP_CORR = 0.05
PLATEAU_SAMPLES = 8
PLATEAU_MEM = 1e-5
PLATEAU_UDP_FRAC = 0.10

STATUS_DIRECT = "IDENTIDADE DIRETA"
STATUS_LINEAR = "TRANSFORMAÇÃO LINEAR CONHECIDA"
STATUS_ORIGIN = "MESMA GRANDEZA COM ORIGEM DIFERENTE"
STATUS_CORR = "CORRELAÇÃO SEM IDENTIDADE"
STATUS_NONE = "SEM CORRELAÇÃO"
STATUS_OPEN = "INCONCLUSIVO"


def pearson(xs: list[float], ys: list[float]) -> float | None:
    n = len(xs)
    if n < 3 or n != len(ys):
        return None
    mx = sum(xs) / n
    my = sum(ys) / n
    vx = sum((x - mx) ** 2 for x in xs)
    vy = sum((y - my) ** 2 for y in ys)
    if vx <= 0.0 or vy <= 0.0:
        return None
    cov = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    return cov / math.sqrt(vx * vy)


def ptp(xs: list[float]) -> float:
    if not xs:
        return 0.0
    return max(xs) - min(xs)


def finite(row: dict, key: str) -> bool:
    try:
        value = float(row[key])
    except (KeyError, TypeError, ValueError):
        return False
    return math.isfinite(value)


def load_rows(path: str) -> list[dict]:
    with open(path, newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    usable = []
    for row in rows:
        if row.get("rig_note") != "ok":
            continue
        if not all(finite(row, MEM_COL[b]) for b in BLOCKS):
            continue
        if not all(finite(row, UDP_COL[w]) for w in WHEELS):
            continue
        usable.append(row)
    return usable


def split_series(rows: list[dict]) -> list[list[dict]]:
    groups: list[list[dict]] = []
    current: list[dict] = []
    key = None
    for row in rows:
        ident = (row.get("series_id", "0"), row.get("rig_ptr", ""))
        if ident != key:
            if current:
                groups.append(current)
            current = [row]
            key = ident
        else:
            current.append(row)
    if current:
        groups.append(current)
    return groups


def motion_ok(rows: list[dict]) -> tuple[bool, str, float]:
    parked = [r for r in rows if r.get("phase") == "T0"]
    moving = [r for r in rows if r.get("phase") in DECISION_PHASES]
    if len(parked) < 20:
        return False, "T0 com menos de 20 amostras", 0.0
    if not moving:
        return False, "nenhuma amostra em T1–T6", 0.0
    ratios = []
    for wheel in WHEELS:
        base = [float(r[UDP_COL[wheel]]) for r in parked]
        live = [float(r[UDP_COL[wheel]]) for r in moving]
        span0 = ptp(base)
        span = ptp(live)
        if span <= 0.0:
            ratios.append(0.0)
            continue
        if span0 <= 0.0:
            ratios.append(math.inf)
        else:
            ratios.append(span / span0)
    best = max(ratios) if ratios else 0.0
    if best <= 10.0:
        return False, "UDP não passou de 10× a amplitude parada", best
    return True, "amplitude acima de 10× o T0", best


def shift(rows: list[dict], lag: int) -> list[tuple[dict, dict]]:
    """lag > 0 compara memória[i] com UDP[i+lag]. Um lag para as quatro rodas."""
    n = len(rows)
    pairs = []
    if lag >= 0:
        for i in range(n - lag):
            pairs.append((rows[i], rows[i + lag]))
    else:
        k = -lag
        for i in range(n - k):
            pairs.append((rows[i + k], rows[i]))
    return pairs


def column(pairs, mem_row_key: str, udp_row_key: str, phase_set: set[str] | None):
    mem, udp = [], []
    for mem_row, udp_row in pairs:
        if phase_set is not None and udp_row.get("phase") not in phase_set:
            continue
        mem.append(float(mem_row[mem_row_key]))
        udp.append(float(udp_row[udp_row_key]))
    return mem, udp


def fit_b(pairs, perm: dict[str, str], scale: float, phase_set: set[str]) -> float | None:
    acc_m = []
    acc_u = []
    for wheel in WHEELS:
        mem, udp = column(pairs, MEM_COL[perm[wheel]], UDP_COL[wheel], phase_set)
        acc_m.extend(mem)
        acc_u.extend(udp)
    if len(acc_u) < 3:
        return None
    return sum(u - scale * m for u, m in zip(acc_u, acc_m)) / len(acc_u)


def score_perm(pairs, perm, scale, b, phase_set: set[str]) -> dict | None:
    per = {}
    all_err = []
    all_udp = []
    for wheel in WHEELS:
        mem, udp = column(pairs, MEM_COL[perm[wheel]], UDP_COL[wheel], phase_set)
        if len(udp) < 3:
            return None
        pred = [scale * m + b for m in mem]
        err = [p - u for p, u in zip(pred, udp)]
        r = pearson(mem, udp)
        if r is None:
            return None
        rmse = math.sqrt(sum(e * e for e in err) / len(err))
        per[wheel] = {
            "corr": r,
            "rmse": rmse,
            "max_abs": max(abs(e) for e in err),
            "udp_ptp": ptp(udp),
            "err_ptp": ptp(err),
            "n": len(udp),
        }
        all_err.extend(err)
        all_udp.extend(udp)
    rmse = math.sqrt(sum(e * e for e in all_err) / len(all_err))
    return {
        "per_wheel": per,
        "min_corr": min(item["corr"] for item in per.values()),
        "rmse": rmse,
        "max_abs": max(abs(e) for e in all_err),
        "udp_ptp": ptp(all_udp),
        "err_ptp": ptp(all_err),
    }


def plateaus(pairs, perm, phase_set: set[str]) -> int:
    count = 0
    for wheel in WHEELS:
        mem, udp = column(pairs, MEM_COL[perm[wheel]], UDP_COL[wheel], phase_set)
        span = ptp(udp)
        if span <= 0.0 or len(mem) < PLATEAU_SAMPLES:
            continue
        i = 0
        while i + PLATEAU_SAMPLES <= len(mem):
            window_m = mem[i : i + PLATEAU_SAMPLES]
            window_u = udp[i : i + PLATEAU_SAMPLES]
            if ptp(window_m) < PLATEAU_MEM and ptp(window_u) > PLATEAU_UDP_FRAC * span:
                count += 1
                i += PLATEAU_SAMPLES
            else:
                i += 1
    return count


def sign_agreement(pairs, perm, phase: str) -> dict | None:
    mem, udp = [], []
    for wheel in WHEELS:
        m, u = column(pairs, MEM_COL[perm[wheel]], UDP_COL[wheel], {phase})
        if len(m) < 2:
            return None
        mem.append(m)
        udp.append(u)
    same = 0
    total = 0
    for m, u in zip(mem, udp):
        for i in range(1, len(m)):
            dm = m[i] - m[i - 1]
            du = u[i] - u[i - 1]
            if dm == 0.0 or du == 0.0:
                continue
            total += 1
            if (dm > 0) == (du > 0):
                same += 1
    if total == 0:
        return {"same_frac": None, "n": 0}
    return {"same_frac": same / total, "n": total}


def classify(scale: float, b: float, scored: dict, udp_span: float, compressed: dict | None, extended: dict | None, n_plateau: int) -> tuple[str, str]:
    notes = []
    if udp_span <= 0.0:
        return STATUS_OPEN, "amplitude UDP nula"
    min_corr = scored["min_corr"]
    small_b = abs(b) < B_FRAC * udp_span
    small_err = scored["max_abs"] < ERR_FRAC * udp_span
    phases_ok = True
    for label, block in (("compressão", compressed), ("extensão", extended)):
        count = 0 if block is None else min(item["n"] for item in block["per_wheel"].values())
        if block is None or count < MIN_PHASE_SAMPLES:
            phases_ok = False
            notes.append(f"{label} sem {MIN_PHASE_SAMPLES} amostras")
            continue
        if block["min_corr"] <= CORR_IDENTITY or block["max_abs"] >= ERR_FRAC * udp_span:
            phases_ok = False
            notes.append(f"{label} fora do critério")
    if n_plateau:
        notes.append(f"{n_plateau} platô(s) da memória com UDP ainda em movimento")
    inverted = min_corr < -CORR_IDENTITY
    if inverted:
        notes.append("sinal invertido nas quatro rodas; escala -1 não entra no teste")
        return STATUS_CORR, "; ".join(notes)

    fitted = min_corr > CORR_IDENTITY and small_err and phases_ok and n_plateau == 0
    if fitted and scale == 1.0 and small_b:
        return STATUS_DIRECT, "ok"
    if fitted and scale in (1000.0, 0.001) and small_b:
        return STATUS_LINEAR, "ok"
    if fitted and not small_b:
        return STATUS_ORIGIN, "b comum acima de 1% da amplitude"
    if min_corr < 0.5:
        return STATUS_NONE, "correlação mínima abaixo de 0,5"
    return STATUS_CORR, "; ".join(notes) if notes else "correlação sem os critérios de identidade"


def evaluate_series(rows: list[dict]) -> dict:
    ok, reason, ratio = motion_ok(rows)
    result = {
        "rig_ptr": rows[0].get("rig_ptr"),
        "n": len(rows),
        "motion_ok": ok,
        "motion_note": reason,
        "motion_ratio": None if math.isinf(ratio) else ratio,
        "lag": None,
        "lag_at_edge": False,
        "rows": [],
        "best": None,
        "second": None,
        "mapping": STATUS_OPEN,
    }
    if not ok:
        result["mapping_note"] = reason
        return result

    decision = set(DECISION_PHASES)
    best_lag = 0
    best_lag_score = -2.0
    for lag in range(-MAX_LAG, MAX_LAG + 1):
        pairs = shift(rows, lag)
        local = -2.0
        for perm_blocks in itertools.permutations(BLOCKS):
            perm = dict(zip(WHEELS, perm_blocks))
            for scale in SCALES:
                b = fit_b(pairs, perm, scale, decision)
                if b is None:
                    continue
                scored = score_perm(pairs, perm, scale, b, decision)
                if scored is None:
                    continue
                local = max(local, scored["min_corr"])
        if local > best_lag_score:
            best_lag_score = local
            best_lag = lag
    result["lag"] = best_lag
    result["lag_at_edge"] = abs(best_lag) == MAX_LAG
    pairs = shift(rows, best_lag)

    ranked = []
    for perm_blocks in itertools.permutations(BLOCKS):
        perm = dict(zip(WHEELS, perm_blocks))
        for scale in SCALES:
            b = fit_b(pairs, perm, scale, decision)
            if b is None:
                continue
            scored = score_perm(pairs, perm, scale, b, decision)
            if scored is None:
                continue
            n_plateau = plateaus(pairs, perm, decision)
            comp = score_perm(pairs, perm, scale, b, {"T4"})
            ext = score_perm(pairs, perm, scale, b, {"T5"})
            comp_s = sign_agreement(pairs, perm, "T4")
            ext_s = sign_agreement(pairs, perm, "T5")
            full = score_perm(pairs, perm, scale, b, None)
            span = scored["udp_ptp"]
            status, note = classify(scale, b, scored, span, comp, ext, n_plateau)
            if result["lag_at_edge"] and status in (STATUS_DIRECT, STATUS_LINEAR, STATUS_ORIGIN):
                status = STATUS_OPEN
                note = "Δ no limite de ±8 amostras; sincronização não fechada"
            ranked.append(
                {
                    "perm": perm,
                    "scale": scale,
                    "b": b,
                    "decision": scored,
                    "full": full,
                    "compression": comp,
                    "extension": ext,
                    "compression_sign": comp_s,
                    "extension_sign": ext_s,
                    "plateaus": n_plateau,
                    "status": status,
                    "note": note,
                }
            )
    ranked.sort(key=lambda item: (-item["decision"]["min_corr"], item["decision"]["rmse"]))
    result["rows"] = ranked
    if not ranked:
        result["mapping_note"] = "nenhuma permutação com variância nas quatro rodas"
        return result
    by_perm: dict[tuple, dict] = {}
    for item in ranked:
        key = tuple(item["perm"][wheel] for wheel in WHEELS)
        current = by_perm.get(key)
        if current is None or item["decision"]["rmse"] < current["decision"]["rmse"]:
            by_perm[key] = item
    perm_rank = sorted(
        by_perm.values(),
        key=lambda item: (-item["decision"]["min_corr"], item["decision"]["rmse"]),
    )
    result["best"] = perm_rank[0]
    result["second"] = perm_rank[1] if len(perm_rank) > 1 else None
    gap = None
    if result["second"] is not None:
        gap = (
            perm_rank[0]["decision"]["min_corr"]
            - perm_rank[1]["decision"]["min_corr"]
        )
    result["corr_gap"] = gap
    result["rmse_gap"] = None
    if result["second"] is not None:
        result["rmse_gap"] = (
            perm_rank[1]["decision"]["rmse"] - perm_rank[0]["decision"]["rmse"]
        )
    if gap is None or gap <= SEP_CORR:
        result["mapping"] = STATUS_OPEN
        result["mapping_note"] = "as duas melhores ficam a 0,05 ou menos de correlação"
    elif result["lag_at_edge"]:
        result["mapping"] = STATUS_OPEN
        result["mapping_note"] = "Δ encostou no limite"
    else:
        result["mapping"] = result["best"]["status"]
        result["mapping_note"] = result["best"]["note"]
    return result


def perm_label(perm: dict[str, str]) -> str:
    return " ".join(f"{wheel}->{perm[wheel]}" for wheel in WHEELS)


def fmt(value, digits=4) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        return f"{value:.{digits}g}"
    return str(value)


def render(result: dict) -> str:
    lines = [
        "| Permutação | Escala | b | Corr mínima | RMSE | Erro máx. | Status |",
        "| :--- | ---: | ---: | ---: | ---: | ---: | :--- |",
    ]
    shown = result["rows"]
    if not shown:
        lines.append("| | | | | | | INCONCLUSIVO |")
    for item in shown:
        dec = item["decision"]
        lines.append(
            "| {perm} | {scale} | {b} | {corr} | {rmse} | {err} | {status} |".format(
                perm=perm_label(item["perm"]),
                scale=fmt(item["scale"]),
                b=fmt(item["b"]),
                corr=fmt(dec["min_corr"]),
                rmse=fmt(dec["rmse"]),
                err=fmt(dec["max_abs"]),
                status=item["status"],
            )
        )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="?", help="CSV produzido pela captura")
    parser.add_argument("--json", help="grava o resultado completo")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    if args.self_check:
        self_check()
        print("self-check ok")
        return
    if not args.csv:
        print(render({"rows": []}))
        print("sem captura", file=sys.stderr)
        raise SystemExit(2)
    rows = load_rows(args.csv)
    series = split_series(rows)
    if not series:
        print(render({"rows": []}))
        print("nenhuma amostra com rig_note=ok", file=sys.stderr)
        raise SystemExit(2)
    reports = [evaluate_series(group) for group in series]
    for index, report in enumerate(reports):
        print(f"série {index} rig {report['rig_ptr']} n {report['n']}")
        print(f"movimento: {report['motion_note']}")
        if report["lag"] is not None:
            edge = " no limite" if report["lag_at_edge"] else ""
            print(f"Δ = {report['lag']} amostra(s){edge}")
        print(render(report))
        print(f"mapeamento: {report['mapping']}")
        print(report.get("mapping_note", ""))
        print()
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(reports, fh, indent=2)


def _shapes(i: int) -> tuple[float, float, float, float]:
    return (
        float(i),
        math.sin(i / 2.0) * 5.0,
        ((i - 20) ** 2) / 10.0,
        math.cos(i / 4.0) * 8.0,
    )


def synth(scale: float, noise: float = 0.0) -> list[dict]:
    rows = []

    def add(phase, values):
        row = {
            "series_id": "0",
            "rig_ptr": "0x1",
            "rig_note": "ok",
            "phase": phase,
        }
        for wheel, value in zip(WHEELS, values):
            row[UDP_COL[wheel]] = f"{value}"
            block = {"RL": "A", "RR": "B", "FL": "C", "FR": "D"}[wheel]
            row[MEM_COL[block]] = f"{value / scale + noise}"
        rows.append(row)

    for _ in range(25):
        add("T0", (0.0, 0.0, 0.0, 0.0))
    for i in range(1, 41):
        add("T4", _shapes(i))
    for i in range(40, 0, -1):
        add("T5", _shapes(i))
    return rows


def self_check() -> None:
    assert len(list(itertools.permutations(BLOCKS))) == 24
    direct = evaluate_series(synth(1.0))
    assert direct["mapping"] == STATUS_DIRECT, direct["mapping"]
    assert direct["best"]["perm"] == {"RL": "A", "RR": "B", "FL": "C", "FR": "D"}
    assert direct["best"]["scale"] == 1.0
    assert abs(direct["best"]["b"]) < 1e-6
    assert direct["lag"] == 0
    scaled = evaluate_series(synth(1000.0))
    assert scaled["mapping"] == STATUS_LINEAR, scaled["mapping"]
    assert scaled["best"]["scale"] == 1000.0
    flat = synth(1.0)
    for row in flat:
        for wheel in WHEELS:
            row[UDP_COL[wheel]] = "0"
            row[MEM_COL[{"RL": "A", "RR": "B", "FL": "C", "FR": "D"}[wheel]]] = "0"
    parked = evaluate_series(flat)
    assert parked["mapping"] == STATUS_OPEN
    assert parked["motion_ok"] is False


if __name__ == "__main__":
    main()
