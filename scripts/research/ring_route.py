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
- `resetlines.cqtc`: paredes de reset (cortinas de 100 m) por trás das barreiras do Ring, dos dois lados: REST
  (fora da pista) e SCRS (atalho) iguais, ACRS (corte) por dentro das curvas; ACIP e RSTT vazias (ver
  `reset_walls`);
- `vehicle_track_progress_data.xml`: curvas de progresso da Montalegre com o tempo esticado pela razão dos
  comprimentos (são frações da volta);
- `ai_track_markers.xml`: curvas, pontos de acidente e reta da largada em fração da volta (ver `track_markers`).

    python3 scripts/research/ring_route.py build/re/ring_route [--edits <edits.json>]
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
ACCIDENT_RADIUS = 60.0  # curvas mais fechadas que isso são pontos de acidente (6 de 12 no Ring, 6 na Montalegre)
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


def corner_spans(s, curv) -> list[tuple[int, int, float]]:
    """(amostra de entrada, de saída, raio mínimo) de cada curva com raio < CORNER_RADIUS."""
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
            out.append((a, b, 1 / k[seg].max()))
        i += 1
    return out


def corners(s, curv, gate_s):
    """(índice do portão de freada, de retomada, raio mínimo) de cada curva com raio < CORNER_RADIUS."""
    return [(int(np.searchsorted(gate_s, s[a])) % len(gate_s), int(np.searchsorted(gate_s, s[b])) % len(gate_s), r)
            for a, b, r in corner_spans(s, curv)]


def track_markers(host, s, curv, length):
    """ai_track_markers.xml: zonas em fração da volta (0 = chegada, no portão 0 a FIRST_DISTANCE m). Na Montalegre:
    `critical_corner` e `critical_corner_1` = as curvas (entrada um pouco antes), `accident_black_spots` = as
    mesmas curvas, `critical_straight_1..3` = da largada ao fim da 1ª curva. No Ring: as curvas com raio <
    CORNER_RADIUS, entrada 15 m antes; pontos de acidente = as de raio < ACCIDENT_RADIUS."""
    def frac(d):
        return ((d + FIRST_DISTANCE) % length) / length

    spans = sorted(corner_spans(s, curv), key=lambda c: frac(s[c[0]] - 15.0))
    zones = []
    for a, b, r in spans:
        f0, f1 = frac(s[a] - 15.0), frac(s[b])
        zones.append((f0, f1, r))
    first_end = zones[0][1]

    def zone_nodes(items):
        out = []
        for f0, f1 in items:
            parts = [(f0, f1)] if f0 < f1 else [(f0, 1.0), (0.0, f1)]  # passa pela chegada: em dois
            out += [node("zone", None, [("start", f"{x0:.3f}"), ("end", f"{x1:.3f}")]) for x0, x1 in parts]
        return out

    root = copy.deepcopy(host)
    for group in root[3]:
        for kind in group[3]:
            if kind[0] == "accident_black_spots":
                kind[3] = zone_nodes([(f0, f1) for f0, f1, r in zones if r < ACCIDENT_RADIUS])
            elif kind[0].startswith("critical_straight"):
                kind[3] = zone_nodes([(0.0, first_end)])
            elif kind[0].startswith("critical_corner"):
                kind[3] = zone_nodes([(f0, f1) for f0, f1, _ in zones])
    return root, zones


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


# --- paredes de reset --------------------------------------------------------------------------------------------
# Lido da Montalegre (build/re/montalegre_route0_orig/resetlines.cqtc): cada seção é uma "cortina" vertical de 100 m
# ao longo de polilinhas (pontos a ~2 m) num quadtree em xz. Cabeçalho: caixa, nº de triângulos, de vértices, 1,
# offsets dos vértices (56), dos nós, dos triângulos e das refs, etiqueta. Vértice: 8 bytes big-endian (x 24 bits,
# y 16, z 24, frações da caixa). Nó: 3 bytes; bit 23 = folha (resto = offset nas refs), senão o índice do 1º de 4
# filhos seguidos (0 = x baixo/z alto, 1 = x alto/z alto, 2 = x baixo/z baixo, 3 = x alto/z baixo); ffffff = vazio.
# Triângulo: 7 bytes (vértice a de 24 bits, o menor; nibbles altos de b-a e c-a; bytes baixos; material 0). Folha:
# o menor triângulo em 24 bits e os outros em u16 relativos a ele, o último com o bit 15; até 16 por folha, o
# triângulo entra em toda folha que a caixa dele toca. Normais para o lado da pista (75% na Montalegre).
# REST na Montalegre fica a 8,6 m (p10) do centro, com portões de 7,2 m de meia largura: logo depois do asfalto.
WALL_TYPES = ("e:synth_barrier", "e:synth_tyrewall", "o:synth_adboard", "e:synth_fence", "o:synth_tyrestack",
              "o:synth_flag")  # o que marca a beira da área de corrida no Ring
WALL_REACH = 21.0  # além disso é plateia (alambrado atrás dos pneus das curvas e das arquibancadas, a 25 m)
WALL_MARGIN = 1.5  # parede atrás do objeto
WALL_MIN = 10.5  # asfalto 5 m + zebra 1,2 m + 4,3 m de grama
ACRS_HALF = 9.5  # anti-corte: por dentro das curvas, 3,3 m de grama depois da zebra
WALL_BELOW, WALL_ABOVE = 50.0, 50.0
LEAF_MAX, DEPTH_MAX = 16, 12


def edge_offsets(s, pos, tan, objects) -> np.ndarray:
    """Distância da parede ao centro, (2, n): linha 0 à esquerda, 1 à direita. No mínimo WALL_MIN; atrás de cada
    barreira/pneu/placa/alambrado a até WALL_REACH m, com folga, alargada 8 m para os lados e suavizada."""
    n = len(s)
    xz = pos[:, [0, 2]]
    left = np.stack([tan[:, 1], -tan[:, 0]], axis=1)
    off = np.full((2, n), WALL_MIN)
    for kind, p in objects:
        if not kind.startswith(WALL_TYPES):
            continue
        i = int(np.argmin(np.linalg.norm(xz - p, axis=1)))
        lat = float(np.dot(p - xz[i], left[i]))
        if abs(lat) > WALL_REACH:
            continue
        side = 0 if lat > 0 else 1
        for j in range(i - 3, i + 4):
            off[side, j % n] = max(off[side, j % n], abs(lat) + WALL_MARGIN)
    for side in range(2):
        wide = np.max([np.roll(off[side], k) for k in range(-4, 5)], axis=0)
        off[side] = np.maximum(smooth(wide, 3), off[side])
    return off


def curtain(xz: np.ndarray, y: np.ndarray, toward: np.ndarray, base: int = 0):
    """Cortina vertical ao longo da polilinha `xz` (aberta): vértices (cima, baixo) por ponto e dois triângulos por
    trecho, com a normal virada para `toward` (ponto da pista ao lado de cada ponto)."""
    verts, tris = [], []
    for (x, z), h in zip(xz, y):
        verts += [(x, h + WALL_ABOVE, z), (x, h - WALL_BELOW, z)]
    v = np.array(verts)
    for k in range(len(xz) - 1):
        a_top, a_bot, b_top, b_bot = 2 * k, 2 * k + 1, 2 * k + 2, 2 * k + 3
        for t in ((a_top, a_bot, b_top), (a_bot, b_bot, b_top)):
            p = v[list(t)]
            nrm = np.cross(p[1] - p[0], p[2] - p[0])[[0, 2]]
            if np.dot(nrm, toward[k] - xz[k]) < 0:
                t = (t[0], t[2], t[1])
            r = int(np.argmin(t))
            tris.append(tuple(base + i for i in t[r:] + t[:r]))
    return verts, tris


def encode_cqtc_tree(verts, tris, tag: bytes) -> bytes:
    """Cortinas -> cqtc com quadtree em xz (folhas de até LEAF_MAX triângulos), no formato da Montalegre."""
    v = np.asarray(verts, np.float64)
    lo, hi = v.min(0) - 0.1, v.max(0) + 0.1
    p = v[np.array(tris)]
    bmin, bmax = p.min(1), p.max(1)
    nodes: list[int] = [0]
    refs = bytearray()

    def build(slot, ids, x0, z0, x1, z1, depth):
        if not ids:
            nodes[slot] = 0xFFFFFF
            return
        if len(ids) <= LEAF_MAX or depth == DEPTH_MAX:
            ids = sorted(ids)
            assert len(ids) >= 2 and ids[-1] - ids[0] < 0x8000, (tag, ids)
            nodes[slot] = 0x800000 | len(refs)
            refs.extend(ids[0].to_bytes(3, "big"))
            for k, t in enumerate(ids[1:]):
                refs.extend(((t - ids[0]) | (0x8000 if k == len(ids) - 2 else 0)).to_bytes(2, "big"))
            return
        base = len(nodes)
        nodes.extend([0] * 4)
        nodes[slot] = base
        xm, zm = (x0 + x1) / 2, (z0 + z1) / 2
        for c, (a0, b0, a1, b1) in enumerate(((x0, zm, xm, z1), (xm, zm, x1, z1), (x0, z0, xm, zm), (xm, z0, x1, zm))):
            sub = [t for t in ids if bmin[t, 0] <= a1 and bmax[t, 0] >= a0 and bmin[t, 2] <= b1 and bmax[t, 2] >= b0]
            build(base + c, sub, a0, b0, a1, b1, depth + 1)

    build(0, list(range(len(tris))), lo[0], lo[2], hi[0], hi[2], 0)
    nt, nv = len(tris), len(v)
    vo = 56
    no = vo + 8 * nv
    to = no + 3 * len(nodes)
    ro = to + 7 * nt
    out = bytearray(struct.pack("<6f3i4I", *lo, *hi, nt, nv, 1, vo, no, to, ro) + tag)
    q = (v - lo) / (hi - lo)
    for x, y, z in q:
        out += (min(int(round(x * (1 << 24))), (1 << 24) - 1).to_bytes(3, "big")
                + min(int(round(y * (1 << 16))), (1 << 16) - 1).to_bytes(2, "big")
                + min(int(round(z * (1 << 24))), (1 << 24) - 1).to_bytes(3, "big"))
    for n in nodes:
        out += n.to_bytes(3, "big")
    for a, b, c in tris:
        d1, d2 = b - a, c - a
        assert 0 < d1 < 4096 and 0 < d2 < 4096
        out += a.to_bytes(3, "big") + bytes([(d1 >> 8) << 4 | (d2 >> 8), d1 & 255, d2 & 255, 0])
    return bytes(out + refs)


def resetlines(sections: dict[bytes, bytes]) -> bytes:
    """Contêiner do resetlines.cqtc: u32 3, u32 n; n × (u64 offset, u64 tamanho, etiqueta, 8 zeros); as seções."""
    head = struct.pack("<II", 3, len(sections))
    off = 8 + 28 * len(sections)
    for tag, sec in sections.items():
        head += struct.pack("<QQ", off, len(sec)) + tag + b"\0" * 8
        off += len(sec)
    return head + b"".join(sections.values())


def reset_walls(s, pos, tan, curv, objects) -> tuple[bytes, dict]:
    """resetlines.cqtc do Ring: REST e SCRS = laços por fora e por dentro (`edge_offsets`), ACRS = trechos por
    dentro das curvas a ACRS_HALF m, ACIP e RSTT vazias. Devolve o arquivo e um resumo para conferir."""
    xz = pos[:, [0, 2]]
    left = np.stack([tan[:, 1], -tan[:, 0]], axis=1)
    off = edge_offsets(s, pos, tan, objects)
    loop = lambda a: np.concatenate([a, a[:1]])  # noqa: E731  (fecha o laço repetindo o 1º ponto)
    walls = {}
    for side, sign in ((0, 1.0), (1, -1.0)):
        line = xz + sign * left * off[side][:, None]
        seg = np.diff(loop(line), axis=0)
        back = int((np.einsum("ij,ij->i", seg, tan) <= 0).sum())
        if back:
            raise SystemExit(f"parede {'esquerda' if side == 0 else 'direita'} dobra sobre si em {back} trechos")
        walls[side] = loop(line)
    y = loop(pos[:, 1])
    rest_v, rest_t = [], []
    for side in (0, 1):
        v, t = curtain(walls[side], y, loop(xz), len(rest_v))
        rest_v += v
        rest_t += t
    # anti-corte: por dentro de cada curva (lado para onde ela vira), 5 amostras antes e depois
    corner = np.abs(curv) > 1 / 140.0
    acrs_v, acrs_t, pieces = [], [], 0
    n = len(s)
    k = 0
    while k < n:
        if not corner[k] or (k == 0 and corner[-1]):
            k += 1
            continue
        j = k
        while corner[(j + 1) % n] and j + 1 < k + n:
            j += 1
        side = 0 if curv[k:j + 1].sum() > 0 else 1
        idx = [(i % n) for i in range(k - 5, j + 6)]
        half = np.minimum(ACRS_HALF, off[side][idx])
        line = xz[idx] + (1.0 if side == 0 else -1.0) * left[idx] * half[:, None]
        v, t = curtain(line, pos[idx, 1], xz[idx], len(acrs_v))
        acrs_v += v
        acrs_t += t
        pieces += 1
        k = j + 1
    c = pos.mean(0)
    lo, hi = (c[0] - 4000.0, c[1] - 500.0, c[2] - 4000.0), (c[0] + 4000.0, c[1] + 500.0, c[2] + 4000.0)
    secs = {b"REST": encode_cqtc_tree(rest_v, rest_t, b"REST"),
            b"ACRS": encode_cqtc_tree(acrs_v, acrs_t, b"ACRS") if acrs_t else cqtc_stub(b"ACRS", lo, hi),
            b"SCRS": encode_cqtc_tree(rest_v, rest_t, b"SCRS"),
            b"ACIP": cqtc_stub(b"ACIP", lo, hi),
            b"RSTT": cqtc_stub(b"RSTT", lo, hi)}
    info = {"off": off, "walls": walls, "acrs_pieces": pieces, "tris": {t.decode(): struct.unpack_from("<i", b, 24)[0]
                                                                        for t, b in secs.items()}}
    return resetlines(secs), info


def ring_objects(edits: str | None) -> list[tuple[str, np.ndarray]]:
    """(tipo, posição xz no jogo) das instâncias do Ring, com as edições do viewer3d se houver."""
    from ring_objects import RING, apply_edits, read_instances, to_game
    track = json.load(open(os.path.join(RING, "track.json")))
    insts = read_instances(os.path.join(RING, "inst_route_0.bin"), track["type_order"], with_ids=True)
    if edits:
        pairs, _ = apply_edits(insts, json.load(open(edits, encoding="utf-8")), "route_0")
    else:
        pairs = [(t, m) for t, m, _ in insts]
    yaw, offset = start_transform()
    return [(t, to_game(m, yaw, offset)[3, [0, 2]]) for t, m in pairs]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--edits", help="edits.json do viewer3d: barreiras movidas mudam as paredes de reset")
    a = ap.parse_args()
    load = lambda f: bxml.decode(open(os.path.join(HOST, f), "rb").read())  # noqa: E731
    s, pos, tan, curv, length = centreline()
    os.makedirs(a.out, exist_ok=True)
    prog, n_prog = progress_track(load("progress_track.xml"), s, pos, tan, curv, length)
    ai, n_ai, cs = ai_track(load("ai_track.xml"), s, pos, tan, curv, length)
    out = {"progress_track.xml": prog, "ai_track.xml": ai,
           "ai_vehicle_track.xml": vehicle_track(load("ai_vehicle_track.xml"), cs),
           "vehicle_track_progress_data.xml": progress_data(load("vehicle_track_progress_data.xml"), length)}
    out["ai_track_markers.xml"], zones = track_markers(load("ai_track_markers.xml"), s, curv, length)
    for name, root in out.items():
        with open(os.path.join(a.out, name), "wb") as fh:
            fh.write(bxml.encode(root))
    with open(os.path.join(a.out, "resetlines.cqtc"), "wb") as fh:
        data, info = reset_walls(s, pos, tan, curv, ring_objects(a.edits))
        fh.write(data)
    off = info["off"]
    spots = sum(1 for *_, r in zones if r < ACCIDENT_RADIUS)
    print(f"{a.out}: {length:.1f} m, {n_prog} portões de progresso, {n_ai} da IA, {len(cs)} curvas "
          f"(raios {', '.join(f'{r:.0f}' for _, _, r in cs)})")
    print(f"  paredes de reset a {off.min():.1f}–{off.max():.1f} m do centro (esquerda p50 {np.median(off[0]):.1f}, "
          f"direita p50 {np.median(off[1]):.1f}); triângulos {info['tris']}, {info['acrs_pieces']} trechos anti-corte")
    print(f"  zonas da IA: {len(zones)} curvas, {spots} pontos de acidente, reta da largada até {zones[0][1]:.3f}")


if __name__ == "__main__":
    main()
