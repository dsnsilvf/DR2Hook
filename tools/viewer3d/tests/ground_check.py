#!/usr/bin/env python3
"""Confere o "Pôr no chão" e a oclusão do picking do viewer3d (`--ground-check`) contra Möller–Trumbore.

uso: ground_check.py <pasta da pista exportada> [--n 200] [--seed 1] [--viewer build/viewer3d/viewer3d]

Para instâncias sorteadas, o viewer assenta (y_depois) e pergunta o que o picking acha num raio vertical
de 40 m acima e noutro de 30 m abaixo do centro. Aqui se calcula o mesmo com todos os triângulos:
  - y_depois = altura do terreno sob (x, z), olhando de 1 m acima da origem para baixo; se não há nada
    abaixo (objeto enterrado), a superfície vista de cima (3000 m acima, para baixo); se nem isso, não muda;
  - de baixo, se o terreno está na frente do centro, o picking não pode devolver a própria instância;
  - de cima, idem quando o terreno passa de 20 m sobre o centro (o centro é a base do objeto, e um objeto
    enterrado até 20 m ainda pode ter o topo para fora: abaixo disso a seleção é legítima).
Sai com 1 se algo diverge (altura: 2 cm de tolerância).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from probe_check import load_triangles, nearest_hit  # noqa: E402

DOWN = np.array([0.0, -1.0, 0.0])
UP = np.array([0.0, 1.0, 0.0])
SLACK = 2.0  # folga do viewer (0,25 m) mais o que a caixa do objeto desce abaixo do centro
BURIED = 20.0  # de cima: terreno mais que isso sobre o centro tapa a instância inteira


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
    out = subprocess.run([a.viewer, "--track", a.track, "--fresh", "--ground-check", path], capture_output=True, text=True)
    os.unlink(path)
    if out.returncode != 0:
        sys.exit(out.stderr[-2000:] or f"viewer saiu com {out.returncode}")

    none_above = bad = settled = moved = above_ok = occluded_below = occ_above = checked = 0
    worst = 0.0
    for line in out.stdout.splitlines():
        p = line.split()
        i, x, z, y0, y1 = int(p[0]), *map(float, p[1:5])
        above, below = int(p[5]), int(p[6])
        checked += 1
        # altura do chão: de 1 m acima da origem para baixo
        t = nearest_hit(tris, np.array([x, y0 + 1.0, z]), DOWN)
        if t is not None:
            want = y0 + 1.0 - t
            settled += 1
        else:
            t = nearest_hit(tris, np.array([x, y0 + 3000.0, z]), DOWN)
            want = y0 if t is None else y0 + 3000.0 - t
        err = abs(want - y1)
        worst = max(worst, err)
        moved += abs(y1 - y0) > 1e-4
        if err > 0.02:
            bad += 1
            print(f"DIVERGE altura i={i}: python={want:.4f} viewer={y1:.4f} (antes {y0:.4f})")
        c = np.array([x, y0, z])
        # de baixo: terreno na frente do centro => nunca a própria instância
        tb = nearest_hit(tris, c - [0, 30, 0], UP)
        if tb is not None and tb < 30.0 - SLACK:
            occluded_below += 1
            if below == i:
                bad += 1
                print(f"DIVERGE oclusão de baixo i={i}: terreno a {tb:.2f} m na frente e o picking devolveu a instância")
        # de cima: terreno na frente do centro (instância dentro do morro)
        ta = nearest_hit(tris, c + [0, 40, 0], DOWN)
        if ta is not None and ta < 40.0 - BURIED:
            occ_above += 1
            if above == i:
                bad += 1
                print(f"DIVERGE oclusão de cima i={i}: terreno a {ta:.2f} m na frente e o picking devolveu a instância")
        above_ok += above == i
        none_above += above < 0 and (ta is None or ta > 40.0 - SLACK)
    print(f"{checked} instâncias: {settled} com terreno embaixo, {moved} se moveram, pior erro de altura {worst * 100:.2f} cm; "
          f"oclusão de baixo testada em {occluded_below}, de cima em {occ_above}; "
          f"o picking de cima achou a própria instância em {above_ok} ({100.0 * above_ok / max(1, checked):.0f} %); "
          f"sem nada de cima e sem terreno na frente: {none_above}; {bad} divergências")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
