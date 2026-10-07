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


def add_tri(m: dict, pts: list[tuple[float, float, float]], uvs: list[tuple[float, float]],
            inside: tuple[float, float, float]) -> None:
    """Triângulo com a face da frente (CCW, a que o jogo desenha) virada para longe de `inside`."""
    a, b, c = pts
    ab = [b[i] - a[i] for i in range(3)]
    ac = [c[i] - a[i] for i in range(3)]
    n = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
    out = [(a[i] + b[i] + c[i]) / 3 - inside[i] for i in range(3)]
    if sum(n[i] * out[i] for i in range(3)) < 0:
        pts, uvs = [a, c, b], [uvs[0], uvs[2], uvs[1]]
    base = len(m["positions"])
    m["positions"].extend(pts)
    m["uvs"].extend(uvs)
    m["indices"].extend([base, base + 1, base + 2])


def add_frustum(m: dict, r0: float, r1: float, y0: float, y1: float, sides: int,
                u_rep: float = 1.0, v_rep: float = 1.0, lean: tuple[float, float] = (0.0, 0.0)) -> None:
    """Tronco de cone aberto de raio `r0` (em y0) a `r1` (em y1); o topo pode pender `lean` (dx, dz)."""
    for k in range(sides):
        a0, a1 = 2 * math.pi * k / sides, 2 * math.pi * (k + 1) / sides
        p = [(r0 * math.cos(a0), y0, r0 * math.sin(a0)), (r0 * math.cos(a1), y0, r0 * math.sin(a1)),
             (lean[0] + r1 * math.cos(a1), y1, lean[1] + r1 * math.sin(a1)),
             (lean[0] + r1 * math.cos(a0), y1, lean[1] + r1 * math.sin(a0))]
        u0, u1 = u_rep * k / sides, u_rep * (k + 1) / sides
        uv = [(u0, v_rep), (u1, v_rep), (u1, 0.0), (u0, 0.0)]
        mid = (lean[0] / 2, (y0 + y1) / 2, lean[1] / 2)
        add_tri(m, [p[0], p[1], p[2]], [uv[0], uv[1], uv[2]], mid)
        add_tri(m, [p[0], p[2], p[3]], [uv[0], uv[2], uv[3]], mid)


def pine(rng, crown_mat: str, trunk_mat: str, h: float = 12.0, w: float = 6.0) -> list[dict]:
    """Pinheiro low-poly: tronco cônico e andares de "saias" de agulhas, cada uma com a borda irregular
    e o fundo fechado (sobe até o tronco), como uma copa de abeto vista de baixo."""
    trunk = mesh("pine_trunk", trunk_mat)
    add_frustum(trunk, 0.28, 0.1, 0.0, h * 0.62, 6, 1.0, 3.0)
    crown = mesh("pine_crown", crown_mat)
    tiers, sides = 5, 9
    for t in range(tiers):
        f = t / (tiers - 1)
        y_base = h * (0.16 + 0.15 * t)
        y_top = h if t == tiers - 1 else y_base + h * 0.30
        r = w / 2 * (1.0 - 0.72 * f)
        twist = rng.uniform(0, 2 * math.pi)
        rim = []
        for k in range(sides):
            a = twist + 2 * math.pi * k / sides
            rr = r * rng.uniform(0.82, 1.12)
            # bordas pendem para baixo e alternam altura: lembra galhos
            dy = -rng.uniform(0.0, 0.35) - (0.25 if k % 2 else 0.0)
            rim.append((rr * math.cos(a), y_base + dy, rr * math.sin(a)))
        apex = (rng.uniform(-0.1, 0.1), y_top, rng.uniform(-0.1, 0.1))
        under = (0.0, y_base + h * 0.06, 0.0)
        inside = (0.0, (y_base + y_top) / 2, 0.0)
        for k in range(sides):
            p0, p1 = rim[k], rim[(k + 1) % sides]
            u0, u1 = 2.0 * k / sides, 2.0 * (k + 1) / sides
            add_tri(crown, [apex, p0, p1], [((u0 + u1) / 2, 0.0), (u0, 1.0), (u1, 1.0)], inside)
            # fundo: um cone raso para dentro, visto de baixo
            add_tri(crown, [under, p1, p0], [((u0 + u1) / 2, 0.55), (u1, 1.0), (u0, 1.0)], (0.0, y_top + 10.0, 0.0))
    return [trunk, crown]


def _icosphere() -> tuple[list[tuple[float, float, float]], list[tuple[int, int, int]]]:
    """Icosaedro subdividido uma vez (42 vértices, 80 faces), raio 1."""
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
         (8, 6, 7), (9, 8, 1)]
    verts = [tuple(c / math.sqrt(sum(x * x for x in p)) for c in p) for p in v]
    mids: dict[tuple[int, int], int] = {}

    def mid(i: int, j: int) -> int:
        key = (min(i, j), max(i, j))
        if key not in mids:
            p = [(verts[i][k] + verts[j][k]) / 2 for k in range(3)]
            n = math.sqrt(sum(x * x for x in p))
            verts.append(tuple(x / n for x in p))
            mids[key] = len(verts) - 1
        return mids[key]

    faces = []
    for a, b, c in f:
        ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
        faces += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
    return verts, faces


def add_blob(m: dict, rng, center: tuple[float, float, float], radius: tuple[float, float, float],
             bump: float = 0.22, uv_m: float = 2.0) -> None:
    """Bola irregular (icosfera com o raio de cada vértice sorteado): um tufo de folhas. UV planar em x+z, y."""
    verts, faces = _icosphere()
    pts = []
    for v in verts:
        k = 1.0 + rng.uniform(-bump, bump)
        pts.append(tuple(center[i] + v[i] * radius[i] * k for i in range(3)))
    for a, b, c in faces:
        tri = [pts[a], pts[b], pts[c]]
        add_tri(m, tri, [((p[0] + p[2]) / uv_m, -p[1] / uv_m) for p in tri], center)


def birch(rng, crown_mat: str, trunk_mat: str, h: float = 8.0, w: float = 5.0) -> list[dict]:
    """Bétula low-poly: tronco fino e branco, um pouco torto, e copa em tufos irregulares."""
    lean = (rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3))
    trunk = mesh("birch_trunk", trunk_mat)
    add_frustum(trunk, 0.2, 0.09, 0.0, h * 0.82, 6, 1.0, 3.0, lean)
    crown = mesh("birch_crown", crown_mat)
    # tufo central alto e outros em volta, mais baixos e para fora: copa oval e recortada
    lumps = [((0.0, 0.72, 0.0), (0.22, 0.2, 0.22))]
    for k in range(6):
        a = 2 * math.pi * k / 6 + rng.uniform(-0.4, 0.4)
        d = rng.uniform(0.18, 0.28)
        r = rng.uniform(0.15, 0.2)
        lumps.append(((d * math.cos(a), rng.uniform(0.5, 0.78), d * math.sin(a)), (r, r * 1.25 * w / h, r)))
    lumps.append(((rng.uniform(-0.06, 0.06), 0.9, rng.uniform(-0.06, 0.06)), (0.13, 0.1, 0.13)))
    for (cx, cy, cz), (rx, ry, rz) in lumps:
        c = (lean[0] * cy + cx * w, cy * h, lean[1] * cy + cz * w)
        add_blob(crown, rng, c, (rx * w, ry * h, rz * w), bump=0.15)
    return [trunk, crown]


def add_face(m: dict, center: tuple[float, float, float], right: tuple[float, float, float],
             up: tuple[float, float, float], w: float, h: float, uv: tuple[float, float, float, float] = (0, 0, 1, 1)) -> None:
    """Retângulo w×h visto de frente por quem tem `right` à direita e `up` para cima (a frente é
    right × up, ordem igual à de `add_box`). `uv` = (u0, v0, u1, v1), v0 no topo."""
    u0, v0, u1, v1 = uv
    c = [(center[i] + sr * right[i] * w / 2 + su * up[i] * h / 2) for sr, su in ((-1, -1), (1, -1), (1, 1), (-1, 1))
         for i in range(3)]
    corners = [tuple(c[3 * k:3 * k + 3]) for k in range(4)]
    add_quad(m, corners, [(u0, v1), (u1, v1), (u1, v0), (u0, v0)])


def band_uv(band: int, bands: int = 16) -> tuple[float, float, float, float]:
    """Retângulo de UV dentro da faixa `band` (de cima para baixo) de uma textura de faixas, com folga."""
    return (0.05, (band + 0.2) / bands, 0.95, (band + 0.8) / bands)


def add_painted_box(m: dict, lo: tuple[float, float, float], hi: tuple[float, float, float],
                    uv: tuple[float, float, float, float]) -> None:
    """Caixa como `add_box`, mas todas as faces com o mesmo retângulo de UV (uma cor da paleta)."""
    start = len(m["uvs"])
    add_box(m, lo, hi)
    u0, v0, u1, v1 = uv
    m["uvs"][start:] = [(u0 + (u1 - u0) * u, v0 + (v1 - v0) * v) for u, v in m["uvs"][start:]]


def add_cylinder(m: dict, r: float, length: float, sides: int, uv_side: tuple[float, float, float, float],
                 uv_cap: tuple[float, float, float, float], place) -> None:
    """Cilindro fechado de eixo y (0 a `length`) e raio `r`; `place(x, y, z)` leva cada ponto ao lugar
    (deitar, mover). As faces ficam para fora depois de `place` se ele for uma rotação + translação."""
    tmp = mesh("tmp", "")
    add_frustum(tmp, r, r, 0.0, length, sides)
    su0, sv0, su1, sv1 = uv_side
    tmp["uvs"] = [(su0 + (su1 - su0) * u, sv0 + (sv1 - sv0) * v) for u, v in tmp["uvs"]]
    cu0, cv0, cu1, cv1 = uv_cap
    for y, inside in ((0.0, (0.0, 1.0, 0.0)), (length, (0.0, length - 1.0, 0.0))):
        for k in range(sides):
            a0, a1 = 2 * math.pi * k / sides, 2 * math.pi * (k + 1) / sides
            pts = [(0.0, y, 0.0), (r * math.cos(a0), y, r * math.sin(a0)), (r * math.cos(a1), y, r * math.sin(a1))]
            uvs = [((cu0 + cu1) / 2 + (cu1 - cu0) / 2 * p[0] / r, (cv0 + cv1) / 2 + (cv1 - cv0) / 2 * p[2] / r) for p in pts]
            add_tri(tmp, pts, uvs, inside)
    base = len(m["positions"])
    m["positions"] += [place(*p) for p in tmp["positions"]]
    m["uvs"] += tmp["uvs"]
    m["indices"] += [base + i for i in tmp["indices"]]


def lying_x(cx: float, cy: float, cz: float, length: float):
    """`place` de `add_cylinder` que deita o cilindro ao longo de x, centrado em (cx, cy, cz)."""
    return lambda x, y, z: (cx + y - length / 2, cy - x, cz + z)


def lying_z(cx: float, cy: float, cz: float, length: float):
    """`place` de `add_cylinder` que deita o cilindro ao longo de z, centrado em (cx, cy, cz)."""
    return lambda x, y, z: (cx + x, cy - z, cz + y - length / 2)


def standing(cx: float, cy: float, cz: float):
    return lambda x, y, z: (cx + x, cy + y, cz + z)


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


def footprint(parts: list[dict]) -> tuple[float, float, float, float]:
    """Caixa local (x0, x1, z0, z1) das peças de um tipo, vista de cima."""
    ps = [p for m in parts for p in m["positions"]]
    return min(p[0] for p in ps), max(p[0] for p in ps), min(p[2] for p in ps), max(p[2] for p in ps)


def ground_fit(theta: float, x: float, z: float, ground: Callable[[float, float], float],
               foot: tuple[float, float, float, float], mode: str = "tilt", scale: float = 1.0,
               lift: float = 0.0, max_tilt: float = math.radians(25)) -> list[float]:
    """Matriz de instância assentada no terreno, olhando o chão em 3×3 pontos da base `foot` (de `footprint`).

    - "tilt": acompanha a inclinação (plano por mínimos quadrados, no máximo `max_tilt`), mantém o rumo
      `theta` e desce o que for preciso para nenhum ponto da base ficar no ar;
    - "sink": fica em pé (prédio, tenda, placa, árvore) e desce até o ponto mais baixo do chão sob a base.
    `lift` sobe ao longo do "para cima" da peça (contêiner empilhado)."""
    c, s = math.cos(theta), math.sin(theta)
    x0, x1, z0, z1 = (v * scale for v in foot)
    pts = []
    for lx in (x0, (x0 + x1) / 2, x1):
        for lz in (z0, (z0 + z1) / 2, z1):
            pts.append((lx, lz, ground(x + lx * c + lz * s, z - lx * s + lz * c)))
    if mode == "sink":
        return yaw_matrix(theta, (x, min(p[2] for p in pts) + lift, z), scale)
    # y = a + b·lx + d·lz nas coordenadas locais (equações normais 3×3, Cramer)
    n = len(pts)
    sx = sum(p[0] for p in pts); sz = sum(p[1] for p in pts); sy = sum(p[2] for p in pts)
    sxx = sum(p[0] * p[0] for p in pts); szz = sum(p[1] * p[1] for p in pts); sxz = sum(p[0] * p[1] for p in pts)
    sxy = sum(p[0] * p[2] for p in pts); szy = sum(p[1] * p[2] for p in pts)

    def det3(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))

    A = [[n, sx, sz], [sx, sxx, sxz], [sz, sxz, szz]]
    r = [sy, sxy, szy]
    D = det3(A)
    a, b, d = (det3([[r[i] if k == col else A[i][k] for k in range(3)] for i in range(3)]) / D for col in range(3))
    slope = math.hypot(b, d)
    if slope > math.tan(max_tilt):
        b, d = b * math.tan(max_tilt) / slope, d * math.tan(max_tilt) / slope
    a -= max(0.0, max(a + b * lx + d * lz - g for lx, lz, g in pts))  # nada no ar
    tx = (c, b, -s)                     # X local + inclinação ao longo dele
    tz = (s, d, c)                      # Z local + inclinação ao longo dele

    def unit(v):
        k = math.sqrt(sum(e * e for e in v))
        return tuple(e / k for e in v)

    def cross(u, v):
        return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])

    ex = unit(tx)
    ey = unit(cross(tz, tx))            # Z × X = Y
    ez = cross(ex, ey)                  # X × Y = Z
    pos = (x + ey[0] * lift, a + ey[1] * lift, z + ey[2] * lift)
    return [ex[0] * scale, ex[1] * scale, ex[2] * scale, 0.0, ey[0] * scale, ey[1] * scale, ey[2] * scale, 0.0,
            ez[0] * scale, ez[1] * scale, ez[2] * scale, 0.0, pos[0], pos[1], pos[2], 1.0]


def heading(tx: float, tz: float) -> float:
    """θ de `yaw_matrix` que leva o X local para a direção (tx, tz)."""
    return math.atan2(-tz, tx)
