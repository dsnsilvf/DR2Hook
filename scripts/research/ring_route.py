#!/usr/bin/env python3
"""Traçado da rota do Ring (progresso, IA e ritmo) a partir da linha central do editor, no lugar dos da Montalegre.

Entra a linha central (`source/layout.json`, amostras a cada 2 m) com o mesmo giro e deslocamento do terreno
(`ring_tracksplit.start_transform`). Sai, em XML binário (`bxml.py`), com os da Montalegre de molde:

- `progress_track.xml`: portões (esquerda, direita, cruzamento, distância), chegada no portão 0 e três parciais.
  A rota 1 (joker) ganha portões 300 m abaixo do chão: o Ring não tem volta joker e ninguém passa por eles;
- `ai_track.xml`: portões da IA (posição = limite esquerdo da pista, `normal` = da esquerda para a direita,
  comprimentos ao longo dela: linha de corrida, limites de corrida e de pista), um laço só (fork 0), uma linha
  de freada por curva (no início dela) e uma de retomada (na saída). Sem fork set de joker;
- `ai_vehicle_track.xml`: as mesmas linhas de freada para cada tipo de carro, com a velocidade da curva
  (aceleração lateral fixa) escalada pelo ritmo do tipo na Montalegre;
- `resetlines.cqtc`: sem paredes de reset (ver `resetlines_stub`);
- `vehicle_track_progress_data.xml`: curvas de progresso da Montalegre com o tempo esticado pela razão dos
  comprimentos (são frações da volta).

    python3 scripts/research/ring_route.py build/re/ring_route
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from ring_tracksplit import LAYOUT, ROAD_ABOVE_CENTRELINE, rotate_y, start_transform  # noqa: E402
from tools.egodata import bxml  # noqa: E402

HOST = "build/re/montalegre_route0_orig"
HOST_LENGTH = 1026.759  # total_distance do progress_track da Montalegre
FIRST_DISTANCE = 5.15  # distância do portão 0 na Montalegre (a largada fica antes dele)
PROGRESS_HALF = 9.0  # meia largura dos portões de progresso (asfalto 5 m + zebra + escape)
TRACK_HALF = 6.5  # limites da pista para a IA
RACING_HALF = 4.5  # limites da linha de corrida
CORNER_RADIUS = 150.0  # abaixo disso a curva ganha linha de freada
LATERAL_ACCEL = 12.0  # m/s² na linha de corrida (raio efetivo = 1,5 × o da linha central)
TOP_SPEED = 45.0


def centreline() -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, float]:
    """(s, posição no jogo, tangente xz, curvatura para a esquerda, comprimento) das amostras do layout."""
    lay = json.load(open(LAYOUT))
    smp = lay["samples"]
    yaw, offset = start_transform()
    s = np.array([x["s"] for x in smp])
    pos = rotate_y(np.array([x["p"] for x in smp], dtype=np.float64), yaw) + offset
    pos[:, 1] += ROAD_ABOVE_CENTRELINE
    t3 = rotate_y(np.array([[x["t"][0], 0.0, x["t"][1]] for x in smp]), yaw)
    tan = t3[:, [0, 2]] / np.linalg.norm(t3[:, [0, 2]], axis=1, keepdims=True)
    # no jogo a esquerda de quem anda em t é (tz, -tx) (conferido nos portões da Montalegre): virar à esquerda
    # diminui o ângulo atan2(tz, tx)
    ang = np.unwrap(np.arctan2(tan[:, 1], tan[:, 0]))
    ds = np.gradient(s)
    curv = -np.gradient(ang) / ds
    length = s[-1] + float(np.linalg.norm(pos[0, [0, 2]] - pos[-1, [0, 2]]))
    return s, pos, tan, curv, length


def smooth(a: np.ndarray, n: int) -> np.ndarray:
    k = np.ones(2 * n + 1) / (2 * n + 1)
    return np.convolve(np.concatenate([a[-n:], a, a[:n]]), k, "valid")


def pick(s: np.ndarray, curv: np.ndarray, straight: float, corner: float) -> list[int]:
    """Índices das amostras com espaçamento `straight` nas retas e `corner` em curva fechada."""
    out, last = [0], 0.0
    for i in range(1, len(s)):
        r = 1 / max(abs(curv[i]), 1e-6)
        step = straight if r > 300 else corner if r < 80 else (straight + corner) / 2
        if s[i] - last >= step and s[-1] - s[i] >= step * 0.5:
            out.append(i)
            last = s[i]
    return out


def f3(v) -> str:
    return " ".join(f"{x:.2f}" for x in v)


def node(name, text=None, attrs=(), children=()):
    return [name, text, list(attrs), list(children)]


def left_of(t: np.ndarray) -> np.ndarray:
    return np.array([t[1], 0.0, -t[0]])


def progress_track(host, s, pos, tan, curv, length):
    gates = pick(s, curv, 16.0, 5.0)
    nodes = []
    for k, i in enumerate(gates):
        left = left_of(tan[i])
        nodes.append(node("gate", None, [("id", str(k)), ("distance", f"{s[i] + FIRST_DISTANCE:.2f}")], [
            node("left", f3(pos[i] + PROGRESS_HALF * left), [("format", "float3")]),
            node("right", f3(pos[i] - PROGRESS_HALF * left), [("format", "float3")]),
            node("crossing", f3(pos[i]), [("format", "float3")])]))
    splits = [0] + [min(range(len(gates)), key=lambda k: abs(s[gates[k]] - length * q)) for q in (0.25, 0.5, 0.75)]
    root = copy.deepcopy(host)
    for ch in root[3]:
        if ch[0] == "routes":
            r0 = ch[3][0]
            r0[3] = [node("split", None, [("id", str(j)), ("type", "finish" if j == 0 else "time"), ("gate", str(g))])
                     for j, g in enumerate(splits)]
            r0[2] = [(k, str(len(splits)) if k == "num_splits" else v) for k, v in r0[2]]
        elif ch[0] == "gates":
            ch[3] = nodes
            ch[2] = [("num_gates", str(len(nodes)))]
        elif ch[0] == "jokerGates":
            for g in ch[3]:  # 300 m abaixo do chão: inalcançáveis
                for p in g[3]:
                    v = [float(x) for x in p[1].split()]
                    p[1] = f3((v[0], v[1] - 300.0, v[2]))
        elif ch[0] == "lines":
            ch[3] = [node("line"), node("line")]
        elif ch[0] == "track":
            ch[2] = [(k, f"{length:.3f}" if k == "total_distance" else v) for k, v in ch[2]]
            lo = np.minimum(pos.min(0), 0.0)  # a Montalegre inclui a origem na caixa
            hi = np.maximum(pos.max(0), 0.0)
            ch[3][0][3] = [node("min", f3(lo), [("format", "float3")]), node("max", f3(hi), [("format", "float3")])]
    return root, len(gates)


def corners(s, curv, gate_s):
    """(índice do portão de freada, de retomada, raio mínimo) de cada curva com raio < CORNER_RADIUS."""
    k = np.abs(smooth(curv, 5))
    inside = k > 1 / CORNER_RADIUS
    out = []
    n = len(s)
    start = 0 if inside.all() else int(np.argmin(inside))  # começa fora de curva
    i = 0
    while i < n:
        j = (start + i) % n
        if inside[j]:
            a = j
            while i < n and inside[(start + i) % n]:
                i += 1
            b = (start + i - 1) % n
            seg = [(a + m) % n for m in range((b - a) % n + 1)]
            r = 1 / k[seg].max()
            g0 = int(np.searchsorted(gate_s, s[a])) % len(gate_s)
            g1 = int(np.searchsorted(gate_s, s[b])) % len(gate_s)
            out.append((g0, g1, r))
        i += 1
    return out


def corner_speed(r: float) -> float:
    return min(TOP_SPEED, math.sqrt(LATERAL_ACCEL * 1.5 * r))


def ai_track(host, s, pos, tan, curv, length):
    gates = pick(s, curv, 10.0, 4.0)
    gate_s = s[gates]
    off = np.clip(smooth(curv, 10) * 120.0, -(RACING_HALF - 0.5), RACING_HALF - 0.5)  # ápice por dentro
    nodes = []
    for k, i in enumerate(gates):
        left = left_of(tan[i])
        right_dir = -left
        wp = [("racing_line", TRACK_HALF - off[i]), ("left_racing_limit", TRACK_HALF - RACING_HALF),
              ("right_racing_limit", TRACK_HALF + RACING_HALF), ("left_track_limit", 0.0),
              ("right_track_limit", 2 * TRACK_HALF)]
        nodes.append(node("gate", None, [("id", str(k))], [
            node("position", f3(pos[i] + TRACK_HALF * left), [("format", "float3")]),
            node("normal", f"{right_dir[0]:.6f} 0.0 {right_dir[2]:.6f}", [("format", "float3")]),
            node("waypoints", None, [("num_waypoints", "5")], [
                node("waypoint", None, [("id", str(j)), ("type", t), ("length", f"{v:.2f}")],
                     [node("racing_line", None, [("type", "optimal")])] if j == 0 else [])
                for j, (t, v) in enumerate(wp)])]))
    n = len(nodes)
    links = [node("link", None, [("id", str(k)), ("fork_id", "0"), ("from_gate", str((k - 1) % n)), ("to_gate", str(k))])
             for k in range(n)]
    cs = corners(s, curv, gate_s)
    root = copy.deepcopy(host)
    track = root[3][0]
    tmpl_brake = next(c for c in track[3] if c[0] == "brake_lines")[3][0][3][0]
    brakes, holds = [], []
    for j, (g0, g1, r) in enumerate(cs):
        bd = copy.deepcopy(tmpl_brake)
        v = corner_speed(r) * 0.6  # a linha padrão da Montalegre é mais lenta que a dos tipos de carro
        bd[2] = [(k, f"{v:.2f}" if k in ("max_speed", "max_speed_left", "max_speed_right") else
                  f"{v * 0.6:.2f}" if k == "min_speed" else str(j) if k == "hold_line_id" else val) for k, val in bd[2]]
        brakes.append(node("brake_line", None, [("id", str(10 * j)), ("gate_id", str(g0))], [bd]))
        holds.append(node("hold_line", None, [("id", str(j)), ("gate_id", str(g1)), ("brakeline_id", str(10 * j))]))
    for ch in track[3]:
        if ch[0] == "gates":
            ch[3], ch[2] = nodes, [("num_gates", str(n))]
        elif ch[0] == "links":
            ch[3], ch[2] = links, [("num_links", str(n))]
        elif ch[0] == "brake_lines":
            ch[3], ch[2] = brakes, [("num_brake_lines", str(len(brakes)))]
        elif ch[0] == "hold_lines":
            ch[3], ch[2] = holds, [("num_hold_lines", str(len(holds)))]
        elif ch[0] == "fork_sets":
            ch[3], ch[2] = [], [("num_fork_sets", "0")]
    return root, n, cs


def vehicle_track(host, cs):
    root = copy.deepcopy(host)
    types = root[3][0][3][0][3]
    speeds = {vt[2][0][1]: [float(dict(bl[3][0][2])["max_speed"]) for bl in vt[3][0][3]] for vt in types}
    mean_all = np.mean([v for vs in speeds.values() for v in vs])
    for vt in types:
        scale = np.mean(speeds[vt[2][0][1]]) / mean_all
        bls = vt[3][0]
        tmpl = bls[3][0]
        new = []
        for j, (_, _, r) in enumerate(cs):
            bl = copy.deepcopy(tmpl)
            bl[2] = [(k, str(10 * j) if k == "id" else v) for k, v in bl[2]]
            bd = bl[3][0]
            v = corner_speed(r) * scale
            bd[2] = [(k, f"{v:.1f}" if k == "max_speed" else f"{v * 0.6:.1f}" if k == "min_speed" else
                      str(j) if k == "hold_line_id" else val) for k, val in bd[2]]
            new.append(bl)
        bls[3], bls[2] = new, [("num_brake_lines", str(len(new)))]
    return root


def progress_data(host, length):
    root = copy.deepcopy(host)
    ratio = length / HOST_LENGTH
    stack = [root]
    while stack:
        n = stack.pop()
        stack.extend(n[3])
        if n[0] == "progress_point":
            n[2] = [(k, f"{float(v) * ratio:.6f}" if k == "time" else v) for k, v in n[2]]
    return root


# Folha de um .cqtc "vazio" (uma parede só, nas bordas da caixa), copiada do DiRTbench (MIT,
# github.com/ItsNotPaths/DiRTbench, src/d3/stub_files.odin). O cabeçalho das seções do resetlines do DR2 é o mesmo
# do cqtc do DiRT 3: caixa (6 floats), nº de linhas, de pontos, 1, offsets 56/88/91/105 e a etiqueta.
CQTC_EMPTY_LEAF = bytes.fromhex("0000000104000000000000fefb000000000000fefbffffff0000000104ffffff"
                                "80000000000000030100000001000201000000008001")
RESET_SECTIONS = (b"REST", b"ACRS", b"SCRS", b"ACIP", b"RSTT")


def cqtc_stub(tag: bytes, lo, hi) -> bytes:
    return (struct.pack("<6f", *lo, *hi) + struct.pack("<7I", 2, 4, 1, 56, 88, 91, 105) + tag + CQTC_EMPTY_LEAF)


def resetlines_stub(pos: np.ndarray, reach: float = 4000.0) -> bytes:
    """`resetlines.cqtc` do DR2 sem linhas: contêiner (u32 3, u32 n; n × (u64 offset, u64 tamanho, etiqueta, 8 zeros))
    com as cinco seções da Montalegre, cada uma um cqtc vazio numa caixa de ±`reach` m. REST são as paredes de "fora
    da pista": as da Montalegre resetam o carro no meio do Ring (o reset cai no portão de progresso mais perto, que
    fica do lado de fora das paredes antigas, e o carro entra em laço)."""
    c = pos.mean(0)
    lo, hi = (c[0] - reach, c[1] - 500.0, c[2] - reach), (c[0] + reach, c[1] + 500.0, c[2] + reach)
    secs = [cqtc_stub(t, lo, hi) for t in RESET_SECTIONS]
    head = struct.pack("<II", 3, len(secs))
    off = 8 + 28 * len(secs)
    for t, sec in zip(RESET_SECTIONS, secs):
        head += struct.pack("<QQ", off, len(sec)) + t + b"\0" * 8
        off += len(sec)
    return head + b"".join(secs)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    a = ap.parse_args()
    load = lambda f: bxml.decode(open(os.path.join(HOST, f), "rb").read())  # noqa: E731
    s, pos, tan, curv, length = centreline()
    os.makedirs(a.out, exist_ok=True)
    prog, n_prog = progress_track(load("progress_track.xml"), s, pos, tan, curv, length)
    ai, n_ai, cs = ai_track(load("ai_track.xml"), s, pos, tan, curv, length)
    out = {"progress_track.xml": prog, "ai_track.xml": ai,
           "ai_vehicle_track.xml": vehicle_track(load("ai_vehicle_track.xml"), cs),
           "vehicle_track_progress_data.xml": progress_data(load("vehicle_track_progress_data.xml"), length)}
    for name, root in out.items():
        with open(os.path.join(a.out, name), "wb") as fh:
            fh.write(bxml.encode(root))
    with open(os.path.join(a.out, "resetlines.cqtc"), "wb") as fh:
        fh.write(resetlines_stub(pos))
    print(f"{a.out}: {length:.1f} m, {n_prog} portões de progresso, {n_ai} da IA, {len(cs)} curvas "
          f"(raios {', '.join(f'{r:.0f}' for _, _, r in cs)})")


if __name__ == "__main__":
    main()
