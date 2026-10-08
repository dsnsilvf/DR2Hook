#!/usr/bin/env python3
"""Lê os dumps da sonda `sonda_cb=<pasta>` da LoadView (`src/core/load_view.cpp`).

A cada ~0,5 s da carga (e 3 s depois da largada) o core grava `cb_NNNN.bin` com os constant buffers do vertex
shader do 11º e do 151º draw do alvo grande da cena (1920x1080, sem profundidade), mais as duas câmeras da cena.

Formato (little-endian):
    u32 magia 'dcb1', u32 carga (1) / corrida (0), u32 GetTickCount64, u32 tiros (2)
    0x200 bytes de [cena+0x17c0] (câmera da cena) + 0x200 bytes de [cena+0x38] (câmera de render)
    por tiro: u32 índice do draw (0xffffffff = não houve), u32 0, u64 vertex shader,
              8 slots × (u32 bytes copiados, 4096 bytes)

Slot 3 (272 bytes) = constantes da vista: +0x00 projeção, +0x80 olho, +0x90 vista×projeção relativa ao olho,
+0xd0 eixos do mundo da câmera (direita, cima, trás). Ver docs/reverse_engineering/track_render.md §5.

    python3 scripts/research/load_cb_dump.py <pasta>                 # câmera da GPU em cada dump
    python3 scripts/research/load_cb_dump.py <pasta>/cb_0008.bin 1 3 # tiro 1, slot 3 em linhas de float4
"""
from __future__ import annotations

import glob
import os
import struct
import sys

import numpy as np

SLOTS = 8
SLOT_BYTES = 4096


def load(path: str):
    """(carga?, tick, câmeras 0x400 bytes, [(draw, shader, [bytes por slot])])."""
    b = open(path, "rb").read()
    _magic, loading, tick, shots_n = struct.unpack_from("<4I", b, 0)
    o = 16
    cams = b[o : o + 0x400]
    o += 0x400
    shots = []
    for _ in range(shots_n):
        draw, _pad = struct.unpack_from("<2I", b, o)
        (shader,) = struct.unpack_from("<Q", b, o + 8)
        o += 16
        slots = []
        for _ in range(SLOTS):
            (n,) = struct.unpack_from("<I", b, o)
            slots.append(b[o + 4 : o + 4 + n])
            o += 4 + SLOT_BYTES
        shots.append((draw, shader, slots))
    return bool(loading), tick, cams, shots


def scene_camera(cams: bytes) -> dict[str, np.ndarray]:
    """Matrizes de [cena+0x17c0]: mundo (+0x60), projeção (+0x150), vista (+0x190), vista×proj (+0x1d0)."""
    def m(off: int) -> np.ndarray:
        return np.frombuffer(cams[off : off + 64], dtype="<f4").reshape(4, 4)

    return {"world": m(0x60), "proj": m(0x150), "view": m(0x190), "vp": m(0x1D0)}


def summary(folder: str) -> None:
    t0 = None
    for path in sorted(glob.glob(os.path.join(folder, "cb_*.bin"))):
        loading, tick, cams, shots = load(path)
        t0 = t0 if t0 is not None else tick
        eye = scene_camera(cams)["world"][3, :3]
        parts = []
        for draw, _shader, slots in shots:
            s = slots[3]
            if draw == 0xFFFFFFFF or len(s) < 0x110:
                continue
            a = np.frombuffer(s, dtype="<f4").reshape(-1, 4)
            persp = abs(a[2, 3] + 1) < 1e-3
            fov = np.degrees(2 * np.arctan(1 / a[1, 1])) if a[1, 1] else 0.0
            parts.append(
                f"d{draw} {'persp' if persp else 'orto '} fov={fov:.0f} olho={np.round(a[8, :3], 2)} "
                f"z={np.round(a[15, :3], 3)}"
            )
        phase = "carga  " if loading else "corrida"
        print(f"{os.path.basename(path)} {phase} t+{(tick - t0) / 1000:4.1f}s cena olho={np.round(eye, 2)} | "
              + " | ".join(parts))


def dump_slot(path: str, shot: int, slot: int) -> None:
    _loading, _tick, _cams, shots = load(path)
    draw, shader, slots = shots[shot]
    a = np.frombuffer(slots[slot], dtype="<f4").reshape(-1, 4)
    print(f"tiro {shot} draw {draw} vs {shader:#x} slot {slot} ({len(slots[slot])} bytes)")
    for r, row in enumerate(a):
        print(f"  +{r * 16:#05x}", np.round(row, 4))


def main() -> int:
    np.set_printoptions(suppress=True, linewidth=200)
    if len(sys.argv) == 2 and os.path.isdir(sys.argv[1]):
        summary(sys.argv[1])
    elif len(sys.argv) == 4:
        dump_slot(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
