#!/usr/bin/env python3
"""Confere o "Alinhar ao terreno" do viewer3d (`--align-check`) contra Möller–Trumbore.

uso: align_check.py <pasta da pista exportada> [--n 200] [--seed 1] [--viewer build/viewer3d/viewer3d]

Para instâncias sorteadas, o viewer sobe cada uma 3 m, alinha (Shift+T) e devolve a base usada e a matriz.
Aqui se mede a base (grade 3×3) contra o terreno, raio vertical para baixo:
  - nenhum ponto da base fica no ar (folga de 2 cm);
  - algum ponto toca o chão (2 cm): o objeto voltou dos 3 m e não afundou além do necessário;
  - em pé (árvores): sem inclinação; inclinado: até 25°.
Rumo, escala e espelho são do teste de unidade (edit_test, test_ground_fit).
Sai com 1 se algo diverge.
"""
import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from probe_check import load_triangles, nearest_hit  # noqa: E402

DOWN = np.array([0.0, -1.0, 0.0])
TOL = 0.02


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track")
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--viewer", default="build/viewer3d/viewer3d")
    a = ap.parse_args()

    total = json.load(open(os.path.join(a.track, "track.json")))["routes"][0]["instances"]
    tris = load_triangles(a.track)
    idx = np.random.default_rng(a.seed).choice(total, size=min(a.n, total), replace=False)
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(str(int(i)) for i in idx) + "\n")
        path = f.name
    out = subprocess.run([a.viewer, "--track", a.track, "--fresh", "--align-check", path], capture_output=True, text=True)
    os.unlink(path)
    if out.returncode != 0:
        sys.exit(out.stderr[-2000:] or f"viewer saiu com {out.returncode}")

    bad = aligned = skipped = tilted = 0
    worst_float = worst_gap = worst_tilt = 0.0
    for line in out.stdout.splitlines():
        p = line.split()
        if len(p) >= 2 and p[1] == "skip":
            skipped += 1
            continue
        i, upright = int(p[0]), p[1] == "1"
        x0, x1, z0, z1 = map(float, p[2:6])
        m = np.array(list(map(float, p[6:18])))
        name = p[18] if len(p) > 18 else "?"
        ex, ey, ez, pos = m[0:3], m[3:6], m[6:9], m[9:12]
        aligned += 1
        diffs = []
        for lx in (x0, (x0 + x1) / 2, x1):
            for lz in (z0, (z0 + z1) / 2, z1):
                w = pos + lx * ex + lz * ez
                t = nearest_hit(tris, w + [0, 50, 0], DOWN)
                if t is not None:
                    diffs.append(w[1] - (w[1] + 50 - t))
        if not diffs:
            continue
        top, gap = max(diffs), min(abs(d) for d in diffs)
        worst_float, worst_gap = max(worst_float, top), max(worst_gap, gap)
        tilt = math.degrees(math.acos(max(-1.0, min(1.0, ey[1] / np.linalg.norm(ey)))))
        worst_tilt = max(worst_tilt, tilt)
        tilted += tilt > 0.5
        why = []
        if top > TOL:
            why.append(f"ponto no ar {top * 100:.1f} cm")
        if gap > TOL and tilt < 24.9:
            why.append(f"não toca o chão (mais perto {gap * 100:.1f} cm)")
        if upright and tilt > 0.01:
            why.append(f"em pé inclinou {tilt:.2f}°")
        if tilt > 25.01:
            why.append(f"inclinou {tilt:.2f}° (máx. 25)")
        if why:
            bad += 1
            print(f"DIVERGE i={i} {name}: " + "; ".join(why))
    print(f"{aligned} alinhadas, {skipped} puladas; {tilted} inclinadas (pior {worst_tilt:.1f}°); "
          f"pior ponto no ar {worst_float * 100:.2f} cm; pior distância do ponto mais perto {worst_gap * 100:.2f} cm; "
          f"{bad} divergências")
    sys.exit(1 if bad or not aligned else 0)


if __name__ == "__main__":
    main()
