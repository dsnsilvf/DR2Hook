"""Ordem: observar, medir, só então interpretar. Nome físico só se já era âncora."""

from __future__ import annotations

from pathlib import Path

import numpy as np

from tools.dr2rec.analysis.compare import compare_sessions
from tools.dr2rec.analysis.load import RigView, build_slots, hex_offset, load_session
from tools.dr2rec.analysis.observe import bitfields, byte_entropy, describe_slots, discrete_states, sparkline
from tools.dr2rec.analysis.relate import (
    correlations,
    detect_events,
    marker_windows,
    math_relations,
    pointer_candidates,
    structures,
    symmetry,
    udp_relations,
)
from tools.dr2rec.analysis.report import render_compare_md, render_md, write_compare, write_report
from tools.dr2rec.analysis.verify import verify_anchors
from tools.dr2rec.analysis.xref import dataflow, load_code, scan_code
from tools.dr2rec.anchors import anchor_at, documented_at
from tools.dr2rec.confidence import POSSIBLE, PROBABLE, STRONG


def analyze_capture(path: str, *, exe: str | None = None, max_series: int = 36, max_lag: int = 5) -> dict:
    session = load_session(path)
    view = RigView(session)
    slots = build_slots(session)
    anchors = session.anchors
    activity = describe_slots(slots, anchors)
    activity.sort(key=lambda row: (-row["change_count"], row["offset"]))
    discrete = discrete_states(activity)
    bits = bitfields(slots, anchors)
    events = detect_events(slots, session)
    _attach_bit_events(bits, slots, events)
    checks = verify_anchors(session, view)
    corrs = correlations(slots, anchors, max_lag=max_lag, limit=max_series)
    relations = math_relations(slots, anchors, view)
    sym = symmetry(view, slots, anchors)
    udp = udp_relations(session, slots, anchors)
    pointers = pointer_candidates(view, anchors)
    structs = structures(view, slots, anchors, sym, bits)
    flow = []
    if exe:
        code, base = load_code(exe)
        offsets = _xref_offsets(activity, discrete, bits, sym)
        flow = dataflow(scan_code(code, base, offsets))
    interesting, unknown = _rank(activity, slots, discrete, bits, events, sym, relations, udp, pointers)
    for row in unknown:
        index = slots.index_of(row["offset"])
        if index is None:
            continue
        raw = np.ascontiguousarray(slots.i32[:, index]).view(np.uint8)
        row["entropy"] = byte_entropy(raw)
    experiments = _experiments(unknown, events, session)
    n = slots.samples
    if n >= 2:
        duration = float(session.timestamp[-1] - session.timestamp[0]) / 1e9
    else:
        duration = 0.0
    rigs = sorted({int(value) for value in session.rig.tolist() if int(value)})
    result = {
        "summary": {
            "duration_s": duration,
            "samples": n,
            "interval_ns": session.interval_ns,
            "physics_rig": ", ".join(f"0x{value:X}" for value in rigs) or "—",
            "executable": session.meta.get("executable", ""),
            "recorder": session.meta.get("recorder_version", ""),
            "regions": ", ".join(
                f"physicsrig:{hex_offset(int(region['offset']))}:{hex_offset(int(region['offset']) + int(region['size']))}"
                for region in session.meta.get("regions") or []
            )
            or "só âncoras",
            "markers": len(session.markers),
            "closed": session.closed,
            "dropped": session.dropped,
        },
        "anchor_checks": checks,
        "activity": activity,
        "structures": structs,
        "events": events,
        "marker_windows": marker_windows(session, slots),
        "correlations": corrs,
        "relations": relations,
        "symmetry": sym,
        "udp": udp,
        "discrete": discrete,
        "bitfields": bits,
        "pointers": pointers,
        "dataflow": flow,
        "interesting": interesting,
        "unknown": unknown,
        "experiments": experiments,
    }
    result["markdown"] = render_md(result)
    return result


def analyze_to_dir(path: str, output: str | None, **kwargs) -> dict:
    result = analyze_capture(path, **kwargs)
    if output:
        directory = Path(output)
    else:
        directory = Path(path).resolve().parent / f"{Path(path).stem}_report"
    write_report(directory, result)
    result["report_dir"] = str(directory)
    return result


def compare_to_dir(path_a: str, path_b: str, output: str | None) -> dict:
    result = compare_sessions(load_session(path_a), load_session(path_b))
    markdown = render_compare_md(result)
    if output:
        directory = Path(output)
    else:
        directory = Path(path_a).resolve().parent / f"{Path(path_a).stem}_vs_{Path(path_b).stem}"
    write_compare(directory, result, markdown)
    result["markdown"] = markdown
    result["report_dir"] = str(directory)
    return result


def inspect_offset(path: str, offset: int) -> str:
    session = load_session(path)
    slots = build_slots(session)
    index = slots.index_of(offset)
    anchor = anchor_at(offset, 4, session.anchors)
    documented = documented_at(offset, 4)
    lines = [f"offset {hex_offset(offset)}", f"amostras {slots.samples}"]
    if anchor:
        lines.append(f"âncora {anchor.name}: {anchor.note} Confiança: {anchor.confidence}")
    elif documented:
        lines.append(f"já documentado: {documented} Confiança: CONFIRMED")
    else:
        lines.append("sem âncora. Não há nome físico.")
    if index is None:
        lines.append("este offset não está na captura.")
        return "\n".join(lines)
    column = slots.f32[:, index]
    finite = column[np.isfinite(column)]
    rate = float(slots.change_count[index] / max(slots.samples - 1, 1))
    unique = int(np.unique(slots.i32[:, index]).size)
    lines.append(f"change_rate {rate:.4f}  unique_i32 {unique}")
    if finite.size:
        lines.append(
            f"f32 min {float(finite.min()):.6g} max {float(finite.max()):.6g} "
            f"mean {float(finite.mean()):.6g} std {float(finite.std()):.6g}"
        )
    if slots.changed.size:
        lines.append(sparkline(slots.changed[:, index]))
    raw = session
    view_row = None
    for region in raw.regions:
        if region.offset <= offset and offset + 4 <= region.offset + region.size:
            local = offset - region.offset
            view_row = region.data[:, local : local + 4]
            break
    if view_row is not None:
        show = [0, view_row.shape[0] // 2, view_row.shape[0] - 1]
        for sample in show:
            lines.append(f"sample {sample} raw {view_row[sample].tobytes().hex()}")
    return "\n".join(lines)


def _attach_bit_events(bits: list[dict], slots, events: list[dict]) -> None:
    for row in bits:
        index = slots.index_of(row["offset"])
        mapping = {}
        if index is None:
            row["event_bits"] = mapping
            continue
        values = slots.i32[:, index].astype(np.int64)
        for bit in row["bits"]:
            column = (values >> bit["bit"]) & 1
            arrivals = (np.flatnonzero(np.diff(column)) + 1).tolist()
            for event in events:
                if any(abs(sample - event["sample"]) <= 2 for sample in arrivals):
                    mapping[bit["bit"]] = event["id"]
                    break
        row["event_bits"] = mapping


def _xref_offsets(activity, discrete, bits, sym) -> list[int]:
    offsets = []
    for row in discrete[:4]:
        offsets.append(row["offset"])
    for row in bits[:4]:
        offsets.append(row["offset"])
    for row in sym:
        if row["anchor"]:
            continue
        offsets.append(row["base"] + row["relative"])
        if len(offsets) >= 8:
            break
    if len(offsets) < 4:
        for row in activity:
            if row["anchor"] or row["documented"]:
                continue
            offsets.append(row["offset"])
            if len(offsets) >= 8:
                break
    return list(dict.fromkeys(offsets))[:8]


def _symmetry_offsets(sym: list[dict]) -> set[int]:
    found = set()
    for row in sym:
        if row["anchor"]:
            continue
        for index in range(4):
            for extra in (0, 4, 8):
                found.add(row["base"] + index * row["stride"] + row["relative"] + extra)
    return found


def _rank(activity, slots, discrete, bits, events, sym, relations, udp, pointers) -> tuple[list[dict], list[dict]]:
    discrete_at = {row["offset"] for row in discrete}
    bit_at = {row["offset"] for row in bits}
    event_at = {item["offset"] for event in events for item in event["affected"]}
    sym_at = _symmetry_offsets(sym)
    udp_at = {row["memory"] for row in udp}
    rel_at = set()
    for row in relations:
        rel_at.add(row["a"])
        rel_at.add(row["b"])
    pointer_at = {row["offset"] for row in pointers}
    by_offset = {row["offset"]: row for row in activity}
    for offset in sorted(sym_at | pointer_at | udp_at):
        if offset in by_offset:
            continue
        index = slots.index_of(offset)
        if index is None:
            continue
        rate = float(slots.change_count[index] / max(slots.samples - 1, 1))
        by_offset[offset] = {
            "offset": offset,
            "type": "observado",
            "change_rate": rate,
            "unique": int(np.unique(slots.i32[:, index]).size),
            "confidence": POSSIBLE,
            "anchor": None,
            "documented": None,
            "note": "",
            "change_count": int(slots.change_count[index]),
            "spark": sparkline(slots.changed[:, index]) if slots.changed.size else "",
        }
    ranked = []
    for row in by_offset.values():
        if row.get("anchor") or row.get("documented"):
            continue
        score = 0.0
        why = []
        offset = row["offset"]
        if offset in discrete_at:
            score += 40
            why.append("compatível com estado discreto")
        if offset in bit_at:
            score += 25
            why.append("bits individuais mudam")
        if offset in event_at:
            score += 20
            why.append("entra numa memory transition")
        if offset in sym_at:
            score += 30
            why.append("mostra simetria com o bloco do stride 0x420")
        if offset in udp_at:
            score += 50
            why.append("igualdade exata com um canal UDP")
        if offset in rel_at:
            score += 15
            why.append("entra numa relação matemática simples")
        if offset in pointer_at:
            score += 12
            why.append("valor estável com cara de ponteiro, não seguido")
        if 0.01 < row["change_rate"] < 0.5 and row["unique"] > 6:
            score += 8
            why.append("muda ao longo da sessão sem ocupar todas as amostras")
        if score < 15:
            continue
        confidence = STRONG if offset in udp_at or (offset in sym_at and row["change_rate"] == 0) else PROBABLE if offset in discrete_at else POSSIBLE
        if offset in udp_at:
            confidence = STRONG
        ranked.append(
            {
                "offset": offset,
                "interest": score,
                "why": "; ".join(why) + ".",
                "confidence": confidence,
                "change_rate": row["change_rate"],
                "unique": row["unique"],
            }
        )
    ranked.sort(key=lambda item: item["interest"], reverse=True)
    interesting = ranked[:20]
    unknown = [
        item
        for item in interesting
        if "igualdade exata com um canal UDP" not in item["why"]
    ][:12]
    return interesting, unknown


def _experiments(unknown: list[dict], events: list[dict], session) -> list[str]:
    lines = []
    for row in unknown[:4]:
        if row["unique"] <= 6:
            lines.append(
                f"Repita a sessão e marque cada troca de {hex_offset(row['offset'])}. "
                "Poucos valores distintos ainda não dizem o que o campo representa."
            )
        elif row["change_rate"] == 0:
            lines.append(
                f"Grave a mesma pista com setup diferente e compare {hex_offset(row['offset'])}. "
                "Se o valor parado mudar, ele acompanha configuração; se não mudar, pode ser do veículo ou da pista."
            )
        else:
            lines.append(
                f"Isole {hex_offset(row['offset'])} numa sessão curta com uma manobra só, marcada no instante da manobra."
            )
    if events and not session.markers:
        lines.append("A captura teve memory transition sem marcador manual. Repita a manobra e marque o instante.")
    lines.append(
        "Duas sessões do mesmo carro com setup diferente separam configuração de estado dinâmico."
    )
    lines.append(
        "O mesmo setup em dois carros separa o que é do veículo do que é da sessão."
    )
    return lines
