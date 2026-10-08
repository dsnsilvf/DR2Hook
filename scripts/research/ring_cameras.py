#!/usr/bin/env python3
"""Câmeras do replay do Ring a partir do editor, no lugar das da Montalegre.

Entra `routes[0].replay` do `track.json` (gerado por `tools/synthtrack/cameras.py`) com o mesmo giro e deslocamento
do terreno (`ring_tracksplit.start_transform`). Saem:

- `replay_camera_config.xml` (XML binário, `bxml.py`). Cada câmera do editor clona o bloco da Montalegre de mesmo
  nome (as `camera_r0_spectator_NNN` além da 7ª repetem as da Montalegre em ciclo; a `camera_r0_gopro_001` usa a
  `camera_r01_gopro_001`) e troca posição, orientação e caminhos. A orientação olha do ponto da câmera para o
  `aim` (o jogo olha em +z do quatérnio; +x local = cima × frente). Os caminhos são Bézier cúbicos de 4 pontos
  encadeados, com a curva de porcentagem da Montalegre. As zonas (`TriggerZone`) são caixas de 10 m de altura e
  0,5 m de espessura atravessando a pista de `l` a `r`, com um `ZoneEvent` por troca (câmera do editor = `replay`,
  `onboard_*`/`external_*` = `target`) e `lapNumber` quando a zona só vale numa volta.
  Ficam da Montalegre: `dynamic_camera_rig` (relativa ao carro) e as do pódio e do serviço, com seus caminhos.
- `cameralines.cqtc`: prismas `CBND` (4 paredes, 8 triângulos) em volta das peças altas (`replay.bounds`), numa
  folha só. Formato medido na Montalegre (16 prismas em volta das guaritas): caixa (6 f32), nº de triângulos, de
  vértices, de materiais, offsets dos vértices, nós, triângulos e refs, etiquetas de 4 bytes; vértice = x 24 bits,
  y 16, z 24 (big-endian, normalizados na caixa); nó = 3 bytes BE (bit 23 = folha, resto = offset nas refs);
  triângulo = v0 24 bits, nibbles altos dos deltas de v1 e v2, bytes baixos, material; refs de uma folha = 1º triângulo
  em 24 bits BE, os outros como u16 BE relativos a ele, o último com o bit 15. Normais para fora. (Até 2026-10-07 o
  gerador punha um byte 0 a mais antes do 1º triângulo: o jogo lia as refs deslocadas e caía com a câmera livre.)

    python3 scripts/research/ring_cameras.py build/re/ring_cameras
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

from ring_tracksplit import rotate_y, start_transform  # noqa: E402
from tools.egodata import bxml  # noqa: E402

HOST = "build/re/montalegre_route0_orig"
TRACK = "examples/tracks/synthetic__dr2hook_ring/track.json"
KEEP = ("dynamic_camera_rig", "podium_", "service_")  # câmeras da Montalegre que ficam
GOPRO_HOST = "camera_r01_gopro_001"
HOST_SPECTATORS = 7
ZONE_HEIGHT = 10.0
ZONE_DEPTH = 0.5


# --- XML binário -----------------------------------------------------------------------------------------------

def attr(node, key, value=None):
    for i, (k, v) in enumerate(node[2]):
        if k == key:
            if value is not None:
                node[2][i] = (k, value)
            return v
    if value is not None:
        node[2].append((key, value))
    return None


def drop_attr(node, key):
    node[2] = [(k, v) for k, v in node[2] if k != key]


def param(node, name):
    for c in node[3]:
        if c[0] == "Parameter" and attr(c, "name") == name:
            return c
    raise KeyError(name)


def set_vec(node, name, v):
    p = param(node, name)
    for k, x in zip("xyz", v):
        attr(p, k, f"{x:.6f}")


def set_quat(node, name, q):
    p = param(node, name)
    for k, x in zip("xyzw", q):
        attr(p, k, f"{x:.6f}")


def set_value(node, name, value):
    attr(param(node, name), "value", value)


def child(node, name):
    return next(c for c in node[3] if c[0] == name)


# --- geometria --------------------------------------------------------------------------------------------------

class Game:
    """Do editor (layout do synthtrack) para o mundo do jogo: o mesmo giro e deslocamento do terreno."""

    def __init__(self):
        self.yaw, self.offset = start_transform()

    def __call__(self, pts) -> np.ndarray:
        a = np.atleast_2d(np.asarray(pts, dtype=np.float64))
        return rotate_y(a, self.yaw) + self.offset


def look_quat(eye, aim) -> tuple[float, float, float, float]:
    """(x, y, z, w) que leva +z para a direção eye→aim e +x para cima × frente."""
    f = np.asarray(aim, float) - np.asarray(eye, float)
    f /= np.linalg.norm(f)
    x = np.cross((0.0, 1.0, 0.0), f)
    x /= np.linalg.norm(x)
    y = np.cross(f, x)
    m = np.stack([x, y, f], 1)  # colunas = eixos locais
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = 2.0 * math.sqrt(tr + 1.0)
        q = ((m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s)
    else:
        i = int(np.argmax(np.diag(m)))
        j, k = (i + 1) % 3, (i + 2) % 3
        s = 2.0 * math.sqrt(1.0 + m[i, i] - m[j, j] - m[k, k])
        v = [0.0, 0.0, 0.0]
        v[i] = 0.25 * s
        v[j] = (m[j, i] + m[i, j]) / s
        v[k] = (m[k, i] + m[i, k]) / s
        q = (*v, (m[k, j] - m[j, k]) / s)
    return q


def rotate(q, v) -> np.ndarray:
    qv = np.array(q[:3])
    v = np.asarray(v, float)
    return v + 2.0 * np.cross(qv, np.cross(qv, v) + q[3] * v)


def yaw_quat(across) -> tuple[float, float, float, float]:
    """Giro em Y que leva +x local para a direção (x, z) de `across`."""
    th = math.atan2(-across[2], across[0])
    return (0.0, math.sin(th / 2), 0.0, math.cos(th / 2))


# --- replay_camera_config.xml ------------------------------------------------------------------------------------

def host_template(hosts: dict, cam: dict, k_spectator: int):
    name = cam["name"]
    if name in hosts:
        return hosts[name]
    if name.startswith("camera_r0_spectator_"):
        return hosts[f"camera_r0_spectator_{(k_spectator - 1) % HOST_SPECTATORS + 1:03d}"]
    if cam["kind"] == "static":
        return hosts[GOPRO_HOST]
    raise KeyError(f"{name}: sem bloco de molde na Montalegre")


def spline_path(ident, pts, duration, curve):
    return ["Path", None, [("ident", ident), ("type", "spline"), ("local", "false"), ("duration", f"{duration:.6f}"),
                           ("loop", "false"), ("percentageCurve", curve)],
            [["Point", None, [("x", f"{p[0]:.3f}"), ("y", f"{p[1]:.3f}"), ("z", f"{p[2]:.3f}")], []] for p in pts]]


def linear_curve(ident):
    return ["Path", None, [("ident", ident), ("type", "linearPercentage")],
            [["Value", None, [("time", "0.000"), ("value", "0.000")], []],
             ["Value", None, [("time", "1.000"), ("value", "1.000")], []]]]


def build_config(host_root, replay: dict, game: Game):
    root = copy.deepcopy(host_root)
    kids = root[3]
    hosts = {attr(c, "ident"): c for c in kids if c[0] == "Camera"}
    host_paths = {attr(c, "ident"): c for c in kids if c[0] == "Path"}
    host_zones = {attr(c, "identifier"): c for c in kids if c[0] == "TriggerZone"}
    ours = {c["name"] for c in replay["cameras"]}

    keep = [c for c in kids if c[0] == "Camera" and attr(c, "ident").startswith(KEEP) and attr(c, "ident") not in ours]
    kept_paths = {attr(c, k) for c in keep for k in ("sourcePath", "targetPath")} - {None, ""}

    cameras, splines, curves = [], [], []
    k_spec = 0
    for cam in replay["cameras"]:
        if cam["name"].startswith("camera_r0_spectator_"):
            k_spec += 1
        node = copy.deepcopy(host_template(hosts, cam, k_spec))
        name = cam["name"]
        attr(node, "ident", name)
        pos = game(cam["pos"])[0]
        aim = game(cam["aim"])[0]
        set_vec(node, "Position", pos)
        set_quat(node, "orientation", look_quat(pos, aim))
        drop_attr(node, "sourcePath")
        drop_attr(node, "targetPath")
        for key, suffix, xml_key in (("path", "_path", "sourcePath"), ("target", ".Target_path", "targetPath")):
            pts = cam.get(key) or []
            if not pts:
                continue
            ident = name + suffix
            old = host_paths.get(ident)
            curve_id = attr(old, "percentageCurve") if old is not None else ident + "_linearControllerPath"
            curve = host_paths.get(curve_id)
            splines.append(spline_path(ident, game(pts), cam["duration"], curve_id))
            curves.append(copy.deepcopy(curve) if curve is not None else linear_curve(curve_id))
            attr(node, xml_key, ident)
        # a ordem dos atributos da Montalegre: type, ident, sourcePath, targetPath, assigned...
        node[2].sort(key=lambda kv: ("type", "ident", "sourcePath", "targetPath").index(kv[0])
                     if kv[0] in ("type", "ident", "sourcePath", "targetPath") else 9)
        cameras.append(node)

    ev_replay, ev_target = copy.deepcopy(child(host_zones["Box002"], "ZoneEvent")), None
    for ev in host_zones["Box002"][3]:
        if ev[0] == "ZoneEvent" and attr(param(child(ev, "CameraSwitch"), "switchType"), "value") == "target":
            ev_target = ev
            break
    ev_lap = child(host_zones["Box012"], "ZoneEvent")
    zones = []
    for z in replay["zones"]:
        node = copy.deepcopy(host_zones["Box002"])
        attr(node, "identifier", z["name"])
        node[3] = [c for c in node[3] if c[0] != "ZoneEvent"]
        lr = game([z["l"], z["r"]])
        across = lr[1] - lr[0]
        set_vec(node, "size", (float(np.linalg.norm(across[[0, 2]])), ZONE_HEIGHT, ZONE_DEPTH))
        set_vec(node, "position", lr.mean(0))
        set_quat(node, "orientation", yaw_quat(across))
        for sw in z["switch"]:
            ours_cam = sw["camera"] in ours
            tmpl = ev_lap if z.get("lap") else (ev_replay if ours_cam else ev_target)
            ev = copy.deepcopy(tmpl)
            set_value(ev, "probability", f"{sw['p']:.3f}")
            cs = child(ev, "CameraSwitch")
            set_value(cs, "switchType", "replay" if ours_cam else "target")
            set_value(cs, "cameraName", sw["camera"])
            ts = child(ev, "TriggerStatement")
            set_value(ts, "statementType", "lapNumber" if z.get("lap") else "alwaysTrue")
            set_value(ts, "lapNumber", str(int(z.get("lap", 0))))
            node[3].append(ev)
        zones.append(node)

    rig = [c for c in keep if attr(c, "ident") == "dynamic_camera_rig"]
    others = [c for c in keep if attr(c, "ident") != "dynamic_camera_rig"]
    kept_splines = [c for c in kids if c[0] == "Path" and attr(c, "type") == "spline" and attr(c, "ident") in kept_paths]
    kept_curves = [host_paths[attr(c, "percentageCurve")] for c in kept_splines]
    root[3] = rig + cameras + others + zones + splines + kept_splines + curves + kept_curves
    return root


# --- cameralines.cqtc --------------------------------------------------------------------------------------------

def encode_cqtc(prisms: list[tuple[np.ndarray, float, float]], tag: bytes = b"CBND") -> bytes:
    """Prismas (4 cantos xz em ordem, y0, y1) -> cqtc de uma folha só."""
    verts, tris = [], []
    for corners, y0, y1 in prisms:
        c = np.asarray(corners, float)
        base = len(verts)
        for x, z in c:
            verts += [(x, y1, z), (x, y0, z)]
        for k in range(4):
            a_top, a_bot = base + 2 * k, base + 2 * k + 1
            b_top, b_bot = base + 2 * ((k + 1) % 4), base + 2 * ((k + 1) % 4) + 1
            tris += [(a_top, a_bot, b_top), (a_bot, b_bot, b_top)]
    v = np.array(verts)
    # normal para fora: confere e vira o que estiver para dentro
    out = []
    for t in tris:
        p = v[list(t)]
        n = np.cross(p[1] - p[0], p[2] - p[0])
        cen = v[(t[0] // 8) * 8:(t[0] // 8) * 8 + 8].mean(0)
        if np.dot(n, p.mean(0) - cen) < 0:
            t = (t[0], t[2], t[1])
        r = int(np.argmin(t))
        out.append(t[r:] + t[:r])
    lo = v.min(0) - 0.1
    hi = v.max(0) + 0.1
    nt, nv = len(out), len(v)
    vo = 52 + 4
    no = vo + 8 * nv
    to = no + 3
    ro = to + 7 * nt
    head = struct.pack("<6f3i4I", *lo, *hi, nt, nv, 1, vo, no, to, ro) + tag
    q = (v - lo) / (hi - lo)
    vb = bytearray()
    for x, y, z in q:
        xi = min(int(round(x * (1 << 24))), (1 << 24) - 1)
        yi = min(int(round(y * (1 << 16))), (1 << 16) - 1)
        zi = min(int(round(z * (1 << 24))), (1 << 24) - 1)
        vb += xi.to_bytes(3, "big") + yi.to_bytes(2, "big") + zi.to_bytes(3, "big")
    tb = bytearray()
    for a, b, c in out:
        d1, d2 = b - a, c - a
        assert 0 < d1 < 4096 and 0 < d2 < 4096
        tb += a.to_bytes(3, "big") + bytes([(d1 >> 8) << 4 | (d2 >> 8), d1 & 255, d2 & 255, 0])
    refs = (0).to_bytes(3, "big")
    for k in range(1, nt):
        refs += (k | (0x8000 if k == nt - 1 else 0)).to_bytes(2, "big")
    return head + bytes(vb) + b"\x80\x00\x00" + bytes(tb) + refs


def decode_cqtc(data: bytes) -> dict:
    """Lê um cqtc (para conferir o que o encode_cqtc grava): caixa, vértices em metros, triângulos e as folhas
    (lista de índices de triângulo). Para com ValueError se um nó, uma ref ou um vértice sai da tabela."""
    lo, hi = np.array(struct.unpack_from("<3f", data, 0)), np.array(struct.unpack_from("<3f", data, 12))
    nt, nv, _nm, vo, no, to, ro = struct.unpack_from("<3i4I", data, 24)
    q = []
    for k in range(nv):
        b = data[vo + 8 * k:vo + 8 * k + 8]
        q.append((int.from_bytes(b[0:3], "big") / (1 << 24), int.from_bytes(b[3:5], "big") / (1 << 16),
                  int.from_bytes(b[5:8], "big") / (1 << 24)))
    verts = lo + np.array(q).reshape(-1, 3) * (hi - lo)
    tris = []
    for k in range(nt):
        b = data[to + 7 * k:to + 7 * k + 7]
        a = int.from_bytes(b[0:3], "big")
        t = (a, a + ((b[3] >> 4) << 8 | b[4]), a + ((b[3] & 15) << 8 | b[5]))
        if max(t) >= nv:
            raise ValueError(f"triângulo {k} usa o vértice {max(t)} de {nv}")
        tris.append(t)
    leaves, stack = [], [0]
    while stack:
        n = int.from_bytes(data[no + 3 * stack[-1]:no + 3 * stack.pop() + 3], "big")
        if n == 0xFFFFFF:
            continue
        if not n & 0x800000:
            if no + 3 * (n + 4) > to:
                raise ValueError(f"nó com filhos em {n}, fora da tabela")
            stack += [n + 3, n + 2, n + 1, n]
            continue
        at = ro + (n & 0x7FFFFF)
        first = int.from_bytes(data[at:at + 3], "big")
        leaf, at, last = [first], at + 3, False
        while not last:
            if at + 2 > len(data):
                raise ValueError("folha sem o bit 15 no fim")
            r = int.from_bytes(data[at:at + 2], "big")
            last, at = bool(r & 0x8000), at + 2
            leaf.append(first + (r & 0x7FFF))
        if max(leaf) >= nt:
            raise ValueError(f"folha usa o triângulo {max(leaf)} de {nt}")
        leaves.append(leaf)
    return {"lo": lo, "hi": hi, "verts": verts, "tris": tris, "leaves": leaves}


def prisms(bounds: list[dict], game: Game):
    out = []
    for b in bounds:
        c3 = game([[x, b["y0"], z] for x, z in b["corners"]])
        y1 = game([[b["corners"][0][0], b["y1"], b["corners"][0][1]]])[0][1]
        out.append((c3[:, [0, 2]], float(c3[0, 1]), float(y1)))
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--track", default=TRACK)
    a = ap.parse_args()
    replay = json.load(open(a.track))["routes"][0]["replay"]
    game = Game()
    host = bxml.decode(open(os.path.join(HOST, "replay_camera_config.xml"), "rb").read())
    root = build_config(host, replay, game)
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "replay_camera_config.xml"), "wb") as fh:
        fh.write(bxml.encode(root))
    with open(os.path.join(a.out, "replay_camera_config.txt.xml"), "w") as fh:
        fh.write(bxml.to_xml(root))
    pr = prisms(replay["bounds"], game)
    with open(os.path.join(a.out, "cameralines.cqtc"), "wb") as fh:
        fh.write(encode_cqtc(pr))
    n_cam = sum(c[0] == "Camera" for c in root[3])
    n_zone = sum(c[0] == "TriggerZone" for c in root[3])
    print(f"{a.out}: {n_cam} câmeras ({len(replay['cameras'])} do editor), {n_zone} zonas, {len(pr)} prismas")


if __name__ == "__main__":
    main()
