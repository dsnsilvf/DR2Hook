"""Pista de estresse para o viewer3d: a pista sintética repetida numa grade, no tamanho das maiores do jogo.

    python3 tools/viewer3d/tests/make_stress.py [--grid 8] [--copies 5] \
        build/uiview/tracks/synthetic__dr2hook_ring build/stress/tracks/synthetic__stress

Com --grid 8 e --copies 5: o terreno (164 malhas, 152 mil vértices) vira 64 blocos, ~9,7 milhões de
vértices e ~17 milhões de triângulos (a Polônia tem ~9 milhões de vértices), e as 1011 instâncias da
route_0 viram 1011 × 64 × 5 = ~323 mil. Uma rota só (route_0); portões e linha da IA ficam os do bloco
de origem. Os bytes do DR2M são copiados e só as posições mudam (numpy), então leva segundos.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct

import numpy as np


def read_dr2m(data: bytes) -> list[dict]:
    """Cada malha como pedaços de bytes: cabeçalho+nomes, posições (float32 n×3) e o resto (uv, cores, índices)."""
    if data[:4] != b"DR2M":
        raise ValueError("não é DR2M")
    (count,) = struct.unpack_from("<I", data, 4)
    o = 8
    meshes = []
    for _ in range(count):
        nlen, mlen, nv, ni, flags = struct.unpack_from("<HHIII", data, o)
        head_end = o + 16 + nlen + mlen
        head_end += (-head_end) % 4
        pos = np.frombuffer(data, dtype="<f4", count=nv * 3, offset=head_end).reshape(nv, 3)
        rest = head_end + nv * 12
        end = rest + nv * 8 + (nv * 4 if flags & 2 else 0) + ni * (4 if flags & 1 else 2)
        end += (-end) % 4
        meshes.append({"head": data[o:head_end], "pos": pos, "rest": data[rest:end]})
        o = end
    return meshes


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("src")
    ap.add_argument("dest")
    ap.add_argument("--grid", type=int, default=8, help="blocos por lado (padrão 8)")
    ap.add_argument("--copies", type=int, default=5, help="cópias de cada instância por bloco (padrão 5)")
    args = ap.parse_args()

    doc = json.load(open(os.path.join(args.src, "track.json"), encoding="utf-8"))
    route = doc["routes"][0]
    terrain_file = route["terrain"]["file"]
    meshes = read_dr2m(open(os.path.join(args.src, terrain_file), "rb").read())
    allpos = np.concatenate([m["pos"] for m in meshes])
    span_x = float(allpos[:, 0].max() - allpos[:, 0].min())
    span_z = float(allpos[:, 2].max() - allpos[:, 2].min())

    if os.path.isdir(args.dest):
        shutil.rmtree(args.dest)
    os.makedirs(args.dest)
    shutil.copytree(os.path.join(args.src, "tex"), os.path.join(args.dest, "tex"))
    shutil.copy(os.path.join(args.src, "objects.bin"), args.dest)

    g = args.grid
    offsets = [((tx - g // 2) * span_x, (tz - g // 2) * span_z) for tx in range(g) for tz in range(g)]
    verts = tris = 0
    with open(os.path.join(args.dest, terrain_file), "wb") as fh:
        fh.write(b"DR2M" + struct.pack("<I", len(meshes) * len(offsets)))
        for dx, dz in offsets:
            for m in meshes:
                pos = m["pos"].copy()
                pos[:, 0] += dx
                pos[:, 2] += dz
                fh.write(m["head"])
                fh.write(pos.astype("<f4").tobytes())
                fh.write(m["rest"])
                verts += len(pos)
                tris += struct.unpack_from("<I", m["head"], 8)[0] // 3

    raw = open(os.path.join(args.src, f"inst_{route['name']}.bin"), "rb").read()
    (n,) = struct.unpack_from("<I", raw, 4)
    types = np.frombuffer(raw, "<u2", n, 8)
    o = 8 + ((2 * n + 3) & ~3)
    idnum = np.frombuffer(raw, "<u4", n, o)
    mats = np.frombuffer(raw, "<f4", n * 12, o + 4 * n).reshape(n, 12)
    rng = np.random.default_rng(7)
    all_t, all_id, all_m = [], [], []
    for dx, dz in offsets:
        for c in range(args.copies):
            mm = mats.copy()
            mm[:, 9] += dx + (rng.uniform(-40, 40, n) if c else 0)
            mm[:, 11] += dz + (rng.uniform(-40, 40, n) if c else 0)
            all_t.append(types)
            all_id.append(idnum)
            all_m.append(mm)
    t = np.concatenate(all_t)
    total = len(t)
    with open(os.path.join(args.dest, f"inst_{route['name']}.bin"), "wb") as fh:
        fh.write(b"DR2I" + struct.pack("<I", total))
        tb = t.astype("<u2").tobytes()
        fh.write(tb + b"\0" * ((-len(tb)) % 4))
        fh.write(np.concatenate(all_id).astype("<u4").tobytes())
        fh.write(np.concatenate(all_m).astype("<f4").tobytes())

    doc["id"] = os.path.basename(os.path.normpath(args.dest))
    route["instances"] = total
    route["terrain"]["meshes"] = len(meshes) * len(offsets)
    route["terrain"]["verts"] = verts
    doc["routes"] = [route]
    with open(os.path.join(args.dest, "track.json"), "w", encoding="utf-8") as fh:
        json.dump(doc, fh)
    print(f"{args.dest}: {len(meshes) * len(offsets)} malhas, {verts} vértices, {tris} triângulos, {total} instâncias")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
