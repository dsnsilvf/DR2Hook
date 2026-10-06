"""Transplante do terreno do Ring (examples/tracks/synthetic__dr2hook_ring) para as malhas `batchmaterial`
do tracksplit.pssg da Montalegre, gravando um .nefs novo. Experimento do mapa no jogo (E5).

As malhas `batchmaterial` do jogo (169 no Montalegre) são o terreno visível. Cada uma tem capacidade fixa de
vértices e índices; aqui os triângulos do Ring são empacotados nelas e o que sobra vira triângulo degenerado.
Todas as outras malhas do tracksplit são escondidas (y = -5000) e as caixas de culling são abertas.
"""
from __future__ import annotations

import argparse
import struct
import sys

sys.path.insert(0, ".")
from tools.egodata.nefs import NefsArchive  # noqa: E402
from tools.egodata.nefs_write import replace_files  # noqa: E402
from tools.egodata.pssg import PSSGFile  # noqa: E402
from tools.uiview import mesh  # noqa: E402
from tools.uiview.track.terrain_patch import (  # noqa: E402
    TerrainBuffer, locate_bounding_boxes, locate_meshes, widen_bounding_boxes)

GAME = "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"
PACK = GAME + "/locations/portugal__montalegre_rallycross.nefs"
PATH = "tracks/locations/portugal/montalegre_rallycross/tracksplit.pssg"
RING = "examples/tracks/synthetic__dr2hook_ring/terrain_0.bin"
HIDE_Y = -5000.0
# bytes 12..24 de um vértice batchmaterial: cor ARGB e ST half4 (0,0,0,1)
VERTEX_TAIL = bytes.fromhex("000000c7" "00000000" "00003c00")


def ring_triangles(dx: float, dy: float, dz: float, bg_stride: int) -> list[tuple[tuple[float, float, float], ...]]:
    meshes = mesh.unpack_geom(open(RING, "rb").read())
    fg = [m for m in meshes if len(m["positions"]) < 60000]
    xs = [p[0] for m in fg for p in m["positions"]]
    zs = [p[2] for m in fg for p in m["positions"]]
    box = (min(xs), max(xs), min(zs), max(zs))
    tris: list[tuple[tuple[float, float, float], ...]] = []

    def add(positions, a, b, c):
        tris.append(tuple((positions[i][0] + dx, positions[i][1] + dy, positions[i][2] + dz) for i in (a, b, c)))

    for m in fg:
        ix = m["indices"]
        for i in range(0, len(ix) - 2, 3):
            add(m["positions"], ix[i], ix[i + 1], ix[i + 2])
    bg = next(m for m in meshes if len(m["positions"]) >= 60000)
    n = 261
    for r in range(0, n - bg_stride, bg_stride):
        for c in range(0, n - bg_stride, bg_stride):
            p00, p10 = bg["positions"][r * n + c], bg["positions"][r * n + c + bg_stride]
            p01, p11 = bg["positions"][(r + bg_stride) * n + c], bg["positions"][(r + bg_stride) * n + c + bg_stride]
            cx, cz = (p00[0] + p11[0]) / 2, (p00[2] + p11[2]) / 2
            if box[0] <= cx <= box[1] and box[2] <= cz <= box[3]:
                continue
            pts = [p00, p10, p01, p11]
            add(pts, 0, 2, 1)
            add(pts, 1, 2, 3)
    return tris


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--offset", default="8,1334,-290", help="dx,dy,dz somados às coordenadas do Ring")
    ap.add_argument("--bg-stride", type=int, default=2)
    ap.add_argument("--keep", default="", help="materiais do hospedeiro que NÃO são escondidos (separados por vírgula)")
    ap.add_argument("--no-widen", action="store_true", help="não abre as caixas de culling")
    a = ap.parse_args()
    dx, dy, dz = (float(v) for v in a.offset.split(","))
    tris = ring_triangles(dx, dy, dz, a.bg_stride)
    print(f"{len(tris)} triângulos do Ring")

    arc = NefsArchive.open_path(PACK)
    data = arc.read(PATH)
    split = PSSGFile(data)
    meshes = locate_meshes(split)
    buf = TerrainBuffer(data)
    keep = {m for m in a.keep.split(",") if m}
    slots = sorted((m for m in meshes if m.material == "batchmaterial"), key=lambda m: -m.index_count)

    cursor = 0
    used = 0
    for m in slots:
        vcap, icap = m.vertex_count, m.index_count
        vmap: dict[tuple[float, float, float], int] = {}
        verts: list[tuple[float, float, float]] = []
        idx: list[int] = []
        while cursor < len(tris):
            tri = tris[cursor]
            new = [p for p in set(tri) if p not in vmap]
            if len(idx) + 3 > icap or len(verts) + len(new) > vcap:
                break
            for p in tri:
                if p not in vmap:
                    vmap[p] = len(verts)
                    verts.append(p)
            idx += [vmap[p] for p in tri]
            cursor += 1
        if not idx:
            buf.write_positions(m, [(0.0, HIDE_Y, 0.0)] * vcap)
            buf.write_indices(m, [0] * icap)
            continue
        used += 1
        pad = verts + [(verts[0][0], HIDE_Y, verts[0][2])] * (vcap - len(verts))
        buf.write_positions(m, pad)
        buf.write_indices(m, idx + [0] * (icap - len(idx)))
        sv = m.stream("Vertex")
        for i in range(vcap):
            buf.buf[sv.start + i * sv.stride + 12 : sv.start + i * sv.stride + 24] = VERTEX_TAIL
        buf._mark(sv.start, (vcap - 1) * sv.stride + 24)
    print(f"{cursor} de {len(tris)} triângulos colocados em {used} de {len(slots)} malhas")

    hidden = 0
    for m in meshes:
        if m.material == "batchmaterial" or m.material in keep:
            continue
        pos = buf.read_positions(m)
        if pos:
            buf.write_positions(m, [(x, HIDE_Y, z) for x, _y, z in pos])
            hidden += 1
    print(f"{hidden} malhas escondidas")
    if not a.no_widen:
        widen_bounding_boxes(buf, locate_bounding_boxes(split))
    print(f"blocos tocados: {len(buf.touched)}")
    replace_files(arc, {PATH: buf.patch()}, a.out)
    print("gravado", a.out)


if __name__ == "__main__":
    main()
