"""Malhas no formato que `tools.uiview.mesh.pack_geom` recebe: nome, material, posições, UVs, índices.

Os objetos ficam no espaço local com a base em y = 0, como os objetos de pista do jogo.
"""

from __future__ import annotations

import math
from typing import Callable


def mesh(name: str, material: str) -> dict:
    return {"name": name, "material": material, "positions": [], "uvs": [], "indices": []}


def add_quad(m: dict, corners: list[tuple[float, float, float]], uvs: list[tuple[float, float]]) -> None:
    """Quadrilátero (4 cantos em ordem) como dois triângulos."""
    base = len(m["positions"])
    m["positions"].extend(corners)
    m["uvs"].extend(uvs)
    m["indices"].extend([base, base + 1, base + 2, base, base + 2, base + 3])


def add_box(m: dict, lo: tuple[float, float, float], hi: tuple[float, float, float], uv_scale: tuple[float, float] = (1.0, 1.0)) -> None:
    """Caixa alinhada aos eixos, de `lo` a `hi`. A textura cobre cada face inteira (v = 0 no topo)."""
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    su, sv = uv_scale
    uv = [(0, sv), (su, sv), (su, 0), (0, 0)]
    add_quad(m, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], uv)  # +z
    add_quad(m, [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], uv)  # -z
    add_quad(m, [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], uv)  # +x
    add_quad(m, [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], uv)  # -x
    add_quad(m, [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], uv)  # topo
    add_quad(m, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], uv)  # base


def box(name: str, material: str, sx: float, sy: float, sz: float, uv_scale=(1.0, 1.0)) -> dict:
    m = mesh(name, material)
    add_box(m, (-sx / 2, 0.0, -sz / 2), (sx / 2, sy, sz / 2), uv_scale)
    return m


def cone(name: str, material: str, r: float, h: float, sides: int = 12) -> dict:
    """Cone com a base em y = 0; v = 0 no topo."""
    m = mesh(name, material)
    for k in range(sides):
        a0, a1 = 2 * math.pi * k / sides, 2 * math.pi * (k + 1) / sides
        base = len(m["positions"])
        m["positions"] += [(0.0, h, 0.0), (r * math.cos(a0), 0.0, r * math.sin(a0)), (r * math.cos(a1), 0.0, r * math.sin(a1))]
        m["uvs"] += [((k + 0.5) / sides, 0.0), (k / sides, 1.0), ((k + 1) / sides, 1.0)]
        m["indices"] += [base, base + 1, base + 2]
    return m


def cross_quads(name: str, material: str, w: float, h: float, planes: int = 2) -> dict:
    """Árvore de cartão: `planes` planos verticais cruzados, textura inteira (v = 0 no topo)."""
    m = mesh(name, material)
    for k in range(planes):
        a = math.pi * k / planes
        cx, cz = math.cos(a) * w / 2, math.sin(a) * w / 2
        add_quad(m, [(-cx, 0.0, -cz), (cx, 0.0, cz), (cx, h, cz), (-cx, h, -cz)], [(0, 1), (1, 1), (1, 0), (0, 0)])
    return m


def grid(name: str, material: str, x0: float, z0: float, size_x: float, size_z: float, nx: int, nz: int,
         height: Callable[[float, float], float], uv_m: float, skip: Callable[[float, float], bool] | None = None) -> dict:
    """Grade (nx+1)×(nz+1) sobre `height`, UV = posição / `uv_m`. `skip(x, z)` tira o quadrado de centro (x, z)."""
    m = mesh(name, material)
    row = nx + 1
    for j in range(nz + 1):
        for i in range(nx + 1):
            x, z = x0 + size_x * i / nx, z0 + size_z * j / nz
            m["positions"].append((x, height(x, z), z))
            m["uvs"].append((x / uv_m, z / uv_m))
    for j in range(nz):
        for i in range(nx):
            if skip and skip(x0 + size_x * (i + 0.5) / nx, z0 + size_z * (j + 0.5) / nz):
                continue
            a = j * row + i
            m["indices"] += [a, a + row, a + 1, a + 1, a + row, a + row + 1]
    return m


def yaw_matrix(theta: float, pos: tuple[float, float, float], scale: float = 1.0) -> list[float]:
    """16 floats linha-maior, translação em 12..14 (vetor-linha, como `track.instances` devolve).

    O eixo X local vai para (cos θ, 0, −sin θ) no mundo."""
    c, s = math.cos(theta) * scale, math.sin(theta) * scale
    return [c, 0.0, -s, 0.0, 0.0, scale, 0.0, 0.0, s, 0.0, c, 0.0, pos[0], pos[1], pos[2], 1.0]


def heading(tx: float, tz: float) -> float:
    """θ de `yaw_matrix` que leva o X local para a direção (tx, tz)."""
    return math.atan2(-tz, tx)
