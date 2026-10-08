#!/usr/bin/env python3
"""Vagas de largada do Ring a partir do editor, no lugar das da Montalegre (`grids.pssg`).

Entra `routes[0].grids` do `track.json` (gerado por `tools/synthtrack/grids.py`) com o mesmo giro e deslocamento
do terreno (`ring_tracksplit.start_transform`). O `grids.pssg` da Montalegre serve de molde: a árvore, os nomes e
as caixas (`BOUNDINGBOX`, no espaço local de cada nó) ficam; só os `TRANSFORM` mudam.

Formato (PSSG big-endian): `ROOTNODE` "Scene Root" → um `NODE` por grade → um `NODE` por vaga ou nó de apoio
(`car_grid_spline_*`, `car_near_reset_spline_*`). Cada nó tem `TRANSFORM` (16 f32, linha a linha, translação na
linha 3) e `BOUNDINGBOX` (mín xyz, máx xyz). Mundo = local @ mundo do pai. Linhas: 0 = frente × cima (esquerda do
carro), 1 = cima, 2 = trás (o carro aponta para −linha 2).

Junto sai o `triggers_game_object.xml` (XML binário v0, `cfgxml.py`): o da Montalegre só tem os estepes do parque
(`SpareWheelData`, `transform` = matriz 4×4 vetor-linha em texto), que vão com as vagas `grid_compound_5#5` para o
parque do Ring (mesmo giro e deslocamento da grade).

    python3 scripts/research/ring_grids.py build/re/ring_grids
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from ring_tracksplit import HOST_AHEAD, HOST_START, rotate_y, start_transform  # noqa: E402
from tools.egodata import cfgxml  # noqa: E402
from tools.egodata.pssg import PSSGFile  # noqa: E402

HOST = "build/re/montalegre_route0_orig/grids.pssg"
HOST_TRIGGERS = "build/re/montalegre_route0_orig/triggers_game_object.xml"
COMPOUND = "grid_compound_5#5"
TRACK = "examples/tracks/synthetic__dr2hook_ring/track.json"


def child(node, type_name):
    for c in node.children:
        if c.type_name == type_name:
            return c
    return None


def get_matrix(node) -> np.ndarray:
    t = child(node, "TRANSFORM")
    return np.array(struct.unpack(">16f", t.data), dtype=np.float64).reshape(4, 4) if t else np.eye(4)


def set_matrix(node, m: np.ndarray) -> None:
    child(node, "TRANSFORM").data = struct.pack(">16f", *m.reshape(16))


def pose_matrix(pos, fwd) -> np.ndarray:
    """Pose do jogo: linha 2 = −frente, linha 0 = frente × cima (horizontal), linha 1 = linha 2 × linha 0."""
    f = np.asarray(fwd, float) / np.linalg.norm(fwd)
    left = np.cross(f, (0.0, 1.0, 0.0))
    left /= np.linalg.norm(left)
    back = -f
    m = np.eye(4)
    m[0, :3], m[1, :3], m[2, :3], m[3, :3] = left, np.cross(back, left), back, pos
    return m


class Game:
    """Do editor (layout do synthtrack) para o mundo do jogo: o mesmo giro e deslocamento do terreno."""

    def __init__(self):
        self.yaw, self.offset = start_transform()

    def pos(self, p) -> np.ndarray:
        return rotate_y(np.array([p], dtype=np.float64), self.yaw)[0] + self.offset

    def dir(self, d) -> np.ndarray:
        return rotate_y(np.array([d], dtype=np.float64), self.yaw)[0]


def start_frame(w: np.ndarray) -> tuple[float, float, float, float]:
    """(ao longo, lateral +esquerda, altura, giro em graus) relativos à largada da Montalegre."""
    a = np.subtract(HOST_AHEAD, HOST_START)[[0, 2]]
    a /= np.linalg.norm(a)
    n = np.array((-a[1], a[0]))
    d = w[3, :3] - HOST_START
    f = -w[2, :3]
    return float(d[[0, 2]] @ a), float(d[[0, 2]] @ n), float(d[1]), math.degrees(math.atan2(f[[0, 2]] @ n, f[[0, 2]] @ a))


def build(host: PSSGFile, grids: list[dict], game: Game) -> list[tuple[str, np.ndarray, np.ndarray]]:
    """Troca os TRANSFORM das grades e vagas; devolve (nome, mundo antes, mundo depois) de cada nó mexido."""
    root = host.find_by_type("ROOTNODE")[0]
    root_w = get_matrix(root)
    nodes = {c.nickname: c for c in root.children if c.type_name == "NODE"}
    changed = []
    for g in grids:
        gnode = nodes.get(g["name"])
        if gnode is None:
            raise SystemExit(f"grade {g['name']} não existe no molde")
        old_gw = get_matrix(gnode) @ root_w
        gw = pose_matrix(game.pos(g["pos"]), game.dir(g["fwd"]))
        set_matrix(gnode, gw @ np.linalg.inv(root_w))
        changed.append((g["name"], old_gw, gw))
        kids = {c.nickname: c for c in gnode.children if c.type_name == "NODE"}
        wanted = g["slots"] + g["markers"]
        missing = sorted(set(kids) - {s["name"] for s in wanted})
        if missing:
            raise SystemExit(f"{g['name']}: o editor não tem {', '.join(missing)}")
        for s in wanted:
            node = kids.get(s["name"])
            if node is None:
                raise SystemExit(f"{g['name']}/{s['name']} não existe no molde")
            old = get_matrix(node) @ old_gw
            w = pose_matrix(game.pos(s["pos"]), game.dir(s["fwd"]))
            set_matrix(node, w @ np.linalg.inv(gw))
            changed.append((f"{g['name']}/{s['name']}", old, w))
    if set(nodes) - {g["name"] for g in grids}:
        raise SystemExit(f"o editor não tem as grades {sorted(set(nodes) - {g['name'] for g in grids})}")
    return changed


def move_spare_wheels(tree, old_w: np.ndarray, new_w: np.ndarray) -> int:
    """Leva cada `transform` de `SpareWheelData` de `old_w` (mundo da grade do parque na Montalegre) para `new_w`.
    Devolve quantos mudaram."""
    moved = 0

    def walk(node):
        nonlocal moved
        name, _, attrs, kids = node
        a = dict(attrs)
        if name == "value" and a.get("type") == "matrix4" and a.get("name") == "transform":
            m = np.array([float(x) for x in a["value"].split(",")]).reshape(4, 4) @ np.linalg.inv(old_w) @ new_w
            node[2] = [(k, ", ".join(f"{x:.7g}" for x in m.reshape(16)) if k == "value" else v) for k, v in attrs]
            moved += 1
        for k in kids:
            walk(k)

    walk(tree)
    return moved


def check(data: bytes, grids: list[dict], game: Game) -> float:
    """Relê o arquivo gravado e devolve o maior erro de posição (m) contra o plano do editor."""
    f = PSSGFile(data)
    root = f.find_by_type("ROOTNODE")[0]
    worst = 0.0
    for g in grids:
        gnode = next(c for c in root.children if c.nickname == g["name"])
        gw = get_matrix(gnode) @ get_matrix(root)
        for s in g["slots"] + g["markers"]:
            node = next(c for c in gnode.children if c.nickname == s["name"])
            w = get_matrix(node) @ gw
            worst = max(worst, float(np.linalg.norm(w[3, :3] - game.pos(s["pos"]))),
                        float(np.linalg.norm(-w[2, :3] - game.dir(s["fwd"]) / np.linalg.norm(s["fwd"]))))
    return worst


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--track", default=TRACK)
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--host-triggers", default=HOST_TRIGGERS)
    a = ap.parse_args()
    grids = json.load(open(a.track))["routes"][0]["grids"]
    game = Game()
    host = PSSGFile(a.host)
    changed = build(host, grids, game)
    data = host.serialize()
    worst = check(data, grids, game)
    if worst > 2e-3:
        raise SystemExit(f"erro de {worst:.4f} ao reler o arquivo")
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "grids.pssg"), "wb") as fh:
        fh.write(data)
    print(f"{'nó':48s} {'antes (ao longo, lado, altura, giro)':>40s}   depois")
    for name, old, new in changed:
        o, n = start_frame(old), start_frame(new)
        print(f"{name:48s} {o[0]:8.2f} {o[1]:7.2f} {o[2]:6.2f} {o[3]:7.1f}   {n[0]:8.2f} {n[1]:7.2f} {n[2]:6.2f} {n[3]:7.1f}")
    print(f"{a.out}/grids.pssg: {len(changed)} nós, {len(data)} bytes, erro ao reler {worst * 1000:.2f} mm")
    _, old_w, new_w = next(c for c in changed if c[0] == COMPOUND)
    trig = cfgxml.decode(open(a.host_triggers, "rb").read())
    moved = move_spare_wheels(trig, old_w, new_w)
    with open(os.path.join(a.out, "triggers_game_object.xml"), "wb") as fh:
        fh.write(cfgxml.encode(trig))
    print(f"{a.out}/triggers_game_object.xml: {moved} estepes levados para o parque do Ring")


if __name__ == "__main__":
    main()
