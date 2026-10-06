#!/usr/bin/env python3
"""Confere a sonda de raio do viewer3d contra uma interseção exata (Möller–Trumbore em numpy).

uso: probe_check.py <pasta da pista exportada> [--n 300] [--seed 1] [--viewer build/viewer3d/viewer3d]

Sorteia raios sobre a caixa do terreno (verticais para baixo e inclinados, de cima e de dentro),
pede a distância ao viewer (`--probe-rays`) e compara com a menor distância sobre todos os triângulos
de terrain_0.bin. Imprime os erros e sai com 1 se algum raio diverge (tolerância 2 cm, ou 0,01 % da
distância) ou se um acha terreno e o outro não.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", ".."))
from tools.uiview.mesh import unpack_geom  # noqa: E402


def load_triangles(track_dir):
    tj = json.load(open(os.path.join(track_dir, "track.json")))
    fname = tj["routes"][0]["terrain"]["file"]
    tris = []
    for m in unpack_geom(open(os.path.join(track_dir, fname), "rb").read()):
        p = np.asarray(m["positions"], dtype=np.float64).reshape(-1, 3)
        i = np.asarray(m["indices"], dtype=np.int64).reshape(-1, 3)
        tris.append(p[i])
    return np.concatenate(tris)  # (n, 3, 3)


def nearest_hit(tris, o, d, eps=1e-12):
    v0, v1, v2 = tris[:, 0], tris[:, 1], tris[:, 2]
    e1, e2 = v1 - v0, v2 - v0
    h = np.cross(d, e2)
    a = np.einsum("ij,ij->i", e1, h)
    ok = np.abs(a) > eps
    f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
    s = o - v0
    u = f * np.einsum("ij,ij->i", s, h)
    q = np.cross(s, e1)
    v = f * (q @ d)
    t = f * np.einsum("ij,ij->i", e2, q)
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0)
    return float(t[hit].min()) if hit.any() else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track")
    ap.add_argument("--n", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--viewer", default="build/viewer3d/viewer3d")
    a = ap.parse_args()

    tris = load_triangles(a.track)
    lo, hi = tris.reshape(-1, 3).min(0), tris.reshape(-1, 3).max(0)
    rng = np.random.default_rng(a.seed)
    rays = []
    for k in range(a.n):
        x = rng.uniform(lo[0], hi[0])
        z = rng.uniform(lo[2], hi[2])
        if k % 3 == 0:  # vertical, de cima
            o = np.array([x, hi[1] + rng.uniform(1, 200), z])
            d = np.array([0.0, -1.0, 0.0])
        elif k % 3 == 1:  # inclinado, de cima (como o mouse)
            o = np.array([x, hi[1] + rng.uniform(1, 300), z])
            d = np.array([rng.uniform(-1, 1), -rng.uniform(0.1, 1), rng.uniform(-1, 1)])
        else:  # de dentro da caixa, qualquer direção
            o = np.array([x, rng.uniform(lo[1], hi[1]), z])
            d = rng.normal(size=3)
        rays.append((o, d / np.linalg.norm(d)))

    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        for o, d in rays:
            f.write(" ".join(f"{c:.9g}" for c in (*o, *d)) + "\n")
        path = f.name
    env = dict(os.environ)
    out = subprocess.run([a.viewer, "--track", a.track, "--fresh", "--probe-rays", path], capture_output=True, text=True, env=env)
    os.unlink(path)
    lines = out.stdout.split()
    if out.returncode != 0 or len(lines) != len(rays):
        print(out.stderr[-2000:])
        sys.exit(f"viewer devolveu {len(lines)} resultados para {len(rays)} raios (exit {out.returncode})")

    bad = hits = 0
    worst = 0.0
    for (o, d), got in zip(rays, lines):
        want = nearest_hit(tris, o, d)
        got = None if got == "none" else float(got)
        if want is not None:
            hits += 1
        if (want is None) != (got is None):
            bad += 1
            print(f"DIVERGE achou/não achou: python={want} viewer={got} o={o} d={d}")
            continue
        if want is not None:
            err = abs(want - got)
            worst = max(worst, err)
            if err > max(0.02, 1e-4 * want):
                bad += 1
                print(f"DIVERGE distância: python={want:.4f} viewer={got:.4f} erro={err:.4f}")
    print(f"{len(rays)} raios, {hits} com terreno, pior erro {worst * 100:.2f} cm, {bad} divergências")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
