"""Decoração do Ring (ornamentos `o:` e `t:`, sem colisão): placas de publicidade, tendas, caminhões, contêineres,
banheiros químicos, torre de controle, fardos de feno, pilhas de pneus, pedras, arbustos, público, guarda-sóis,
eólicas, casas, celeiro, caixa d'água, bandeiras, postes de luz e carros.

Malhas fechadas e opacas, faces para fora (CCW), como as peças que já vão ao jogo com `Object_Norm.fx`.
No espaço local o X é a direção da pista e o +Z aponta para a esquerda dela (veja `meshes.heading`).
As cores sólidas vêm da paleta `synth_paint_d` (uma faixa por cor).
"""

from __future__ import annotations

import math
import random

from tools.synthtrack import meshes
from tools.synthtrack import textures as tx

ADS = "o|synth_ads"
PAINT = "o|synth_paint"
CONTAINER = "o|synth_container"
BUILDING = "o|synth_building"
HAY = "o|synth_hay"
BOULDER = "o|synth_boulder"
BUSH = "t|synth_bush"
PEOPLE = "o|synth_people"
ROOF = "o|synth_roof"

TENT_COLORS = {"red": tx.PAINT_RED, "blue": tx.PAINT_BLUE, "yellow": tx.PAINT_YELLOW, "green": tx.PAINT_GREEN}
CONTAINERS = ("red", "blue", "green", "orange")  # mesma ordem de textures.CONTAINER_COLORS


def _ad_uv(row: int) -> tuple[float, float, float, float]:
    n = len(tx.ADS)
    return (0.0, row / n, 1.0, (row + 1) / n)


def _paint(band: int) -> tuple[float, float, float, float]:
    return meshes.band_uv(band, len(tx.PAINT))


def _sign(m: dict, center: tuple[float, float, float], w: float, h: float, row: int) -> None:
    """Placa com a mesma marca nas duas faces (±z), legível dos dois lados."""
    x, y, z = center
    meshes.add_face(m, (x, y, z - 0.051), (-1, 0, 0), (0, 1, 0), w, h, _ad_uv(row))
    meshes.add_face(m, (x, y, z + 0.051), (1, 0, 0), (0, 1, 0), w, h, _ad_uv(row))


def adboard(row: int) -> list[dict]:
    """Placa de 3,2 × 0,8 m sobre dois pés, a borda de baixo a 0,95 m (aparece por cima da barreira)."""
    frame = meshes.mesh("adboard_frame", PAINT)
    meshes.add_painted_box(frame, (-1.65, 0.9, -0.05), (1.65, 1.8, 0.05), _paint(tx.PAINT_STEEL))
    for x in (-1.4, 1.4):
        meshes.add_painted_box(frame, (x - 0.04, -0.2, -0.04), (x + 0.04, 0.9, 0.04), _paint(tx.PAINT_DARK))
    face = meshes.mesh("adboard_face", ADS)
    _sign(face, (0.0, 1.35, 0.0), 3.2, 0.8, row)
    return [frame, face]


def tent(color: str) -> list[dict]:
    """Tenda de equipe 4,5 × 4,5 m: pés, sanefa, telhado em pirâmide e parede de fundo (−z, longe da pista)."""
    band = TENT_COLORS[color]
    m = meshes.mesh("tent", PAINT)
    s, eave, apex = 2.25, 2.3, 3.3
    for x in (-s, s):
        for z in (-s, s):
            meshes.add_painted_box(m, (x - 0.04, 0.0, z - 0.04), (x + 0.04, eave, z + 0.04), _paint(tx.PAINT_STEEL))
    meshes.add_painted_box(m, (-s, eave - 0.3, -s), (s, eave, -s + 0.03), _paint(band))
    meshes.add_painted_box(m, (-s, eave - 0.3, s - 0.03), (s, eave, s), _paint(band))
    meshes.add_painted_box(m, (-s, eave - 0.3, -s), (-s + 0.03, eave, s), _paint(band))
    meshes.add_painted_box(m, (s - 0.03, eave - 0.3, -s), (s, eave, s), _paint(band))
    meshes.add_painted_box(m, (-s, 0.0, -s), (s, eave - 0.3, -s + 0.03), _paint(tx.PAINT_CANVAS))
    corners = [(-s, eave, -s), (s, eave, -s), (s, eave, s), (-s, eave, s)]
    uv = _paint(band)
    for k in range(4):
        tri = [(0.0, apex, 0.0), corners[k], corners[(k + 1) % 4]]
        meshes.add_tri(m, tri, [((uv[0] + uv[2]) / 2, uv[1]), (uv[0], uv[3]), (uv[2], uv[3])], (0.0, eave - 1.0, 0.0))
    # forro, visto de baixo
    meshes.add_tri(m, corners[:3], [uv[:2], uv[2:], uv[2:]], (0.0, apex + 5.0, 0.0))
    meshes.add_tri(m, [corners[0], corners[2], corners[3]], [uv[:2], uv[2:], uv[2:]], (0.0, apex + 5.0, 0.0))
    return [m]


def truck(cab_band: int, row: int) -> list[dict]:
    """Caminhão baú de 8 m ao longo de x (cabine em +x), propaganda nos dois lados do baú."""
    body = meshes.mesh("truck", PAINT)
    meshes.add_painted_box(body, (-4.0, 0.5, -1.1), (4.0, 0.95, 1.1), _paint(tx.PAINT_DARK))       # chassi
    meshes.add_painted_box(body, (-4.0, 0.95, -1.25), (2.5, 3.8, 1.25), _paint(tx.PAINT_WHITE))    # baú
    meshes.add_painted_box(body, (2.6, 0.95, -1.2), (4.2, 3.1, 1.2), _paint(cab_band))            # cabine
    meshes.add_face(body, (4.21, 2.45, 0.0), (0, 0, -1), (0, 1, 0), 2.1, 0.9, _paint(tx.PAINT_GLASS))  # para-brisa
    for z in (-1.21, 1.21):  # janelas laterais da cabine
        right = (1, 0, 0) if z > 0 else (-1, 0, 0)
        meshes.add_face(body, (3.55, 2.45, z + (0.001 if z > 0 else -0.001)), right, (0, 1, 0), 0.9, 0.8, _paint(tx.PAINT_GLASS))
    for x in (3.4, -1.6, -2.9):
        for z in (-0.95, 0.95):
            meshes.add_cylinder(body, 0.5, 0.35, 10, _paint(tx.PAINT_BLACK), _paint(tx.PAINT_DARK),
                                meshes.lying_z(x, 0.5, z, 0.35))
    face = meshes.mesh("truck_ads", ADS)
    meshes.add_face(face, (-0.75, 2.4, 1.26), (1, 0, 0), (0, 1, 0), 6.0, 1.5, _ad_uv(row))
    meshes.add_face(face, (-0.75, 2.4, -1.26), (-1, 0, 0), (0, 1, 0), 6.0, 1.5, _ad_uv(row))
    return [body, face]


def container(color: str) -> list[dict]:
    """Contêiner de 20 pés (6,06 × 2,59 × 2,44 m) ao longo de x; a base desce 0,4 m para o chão em declive."""
    k = CONTAINERS.index(color)
    m = meshes.mesh("container", CONTAINER)
    meshes.add_painted_box(m, (-3.03, -0.4, -1.22), (3.03, 2.59, 1.22), (0.0, k / 4 + 0.01, 1.0, (k + 1) / 4 - 0.01))
    return [m]


def portaloo() -> list[dict]:
    m = meshes.mesh("portaloo", PAINT)
    meshes.add_painted_box(m, (-0.55, -0.2, -0.55), (0.55, 2.2, 0.55), _paint(tx.PAINT_BLUE))
    meshes.add_painted_box(m, (-0.62, 2.2, -0.62), (0.62, 2.35, 0.62), _paint(tx.PAINT_WHITE))
    meshes.add_face(m, (0.0, 1.0, 0.56), (1, 0, 0), (0, 1, 0), 0.8, 1.9, _paint(tx.PAINT_TEAL))  # porta (+z)
    return [m]


def tower() -> list[dict]:
    """Torre de controle: base de dois andares com janelas, cabine envidraçada, laje e placa no telhado."""
    walls = meshes.mesh("tower_walls", BUILDING)
    meshes.add_box(walls, (-4.0, 0.3, -4.0), (4.0, 6.3, 4.0))
    trim = meshes.mesh("tower_trim", PAINT)
    meshes.add_painted_box(trim, (-4.3, -2.0, -4.3), (4.3, 0.3, 4.3), _paint(tx.PAINT_CONCRETE))    # base
    meshes.add_painted_box(trim, (-4.6, 6.3, -4.6), (4.6, 8.8, 4.6), _paint(tx.PAINT_GLASS))        # cabine
    for x in (-4.6, 4.6):
        for z in (-4.6, 4.6):
            meshes.add_painted_box(trim, (x - 0.12, 6.3, z - 0.12), (x + 0.12, 8.8, z + 0.12), _paint(tx.PAINT_WHITE))
    meshes.add_painted_box(trim, (-5.2, 8.8, -5.2), (5.2, 9.2, 5.2), _paint(tx.PAINT_WHITE))        # laje
    meshes.add_painted_box(trim, (-0.06, 9.2, -3.0), (0.06, 13.0, -2.88), _paint(tx.PAINT_STEEL))   # antena
    meshes.add_painted_box(trim, (-2.9, 9.2, -0.05), (2.9, 10.9, 0.05), _paint(tx.PAINT_STEEL))     # placa
    sign = meshes.mesh("tower_sign", ADS)
    _sign(sign, (0.0, 10.05, 0.0), 5.6, 1.4, 0)
    return [walls, trim, sign]


def haybale() -> list[dict]:
    """Fardo redondo de 1,5 m de diâmetro e 1,2 m de largura, deitado ao longo de z."""
    m = meshes.mesh("haybale", HAY)
    meshes.add_cylinder(m, 0.75, 1.2, 12, (0.0, 0.0, 1.0, 0.5), (0.0, 0.5, 1.0, 1.0), meshes.lying_z(0.0, 0.72, 0.0, 1.2))
    return [m]


def tyrestack() -> list[dict]:
    """Pilha de 4 pneus pintados (preto, branco, preto, vermelho)."""
    m = meshes.mesh("tyrestack", PAINT)
    for k, band in enumerate((tx.PAINT_BLACK, tx.PAINT_WHITE, tx.PAINT_BLACK, tx.PAINT_RED)):
        meshes.add_cylinder(m, 0.36, 0.24, 10, _paint(band), _paint(tx.PAINT_BLACK), meshes.standing(0.0, 0.24 * k, 0.0))
    return [m]


def boulder(seed: int, r: float, flat: float) -> list[dict]:
    """Pedra irregular de raio `r` (achatada em y por `flat`), meio enterrada: o centro fica abaixo do chão."""
    rng = random.Random(seed)
    m = meshes.mesh("boulder", BOULDER)
    meshes.add_blob(m, rng, (0.0, r * flat * 0.35, 0.0), (r * rng.uniform(0.9, 1.2), r * flat, r * rng.uniform(0.75, 1.0)),
                    bump=0.3, uv_m=2.5)
    return [m]


def bush(seed: int, w: float, h: float) -> list[dict]:
    """Arbusto baixo de `w` m de largura e `h` m de altura: um tufo central e 3 a 5 em volta, a base enterrada."""
    rng = random.Random(seed)
    m = meshes.mesh("bush", BUSH)
    meshes.add_blob(m, rng, (0.0, h * 0.5, 0.0), (w * 0.32, h * 0.5, w * 0.32), bump=0.2)
    for k in range(rng.randint(3, 5)):
        a = 2 * math.pi * k / 5 + rng.uniform(-0.5, 0.5)
        d, r = rng.uniform(0.18, 0.3) * w, rng.uniform(0.2, 0.28) * w
        meshes.add_blob(m, rng, (d * math.cos(a), r * 0.6, d * math.sin(a)), (r, r * 0.9, r), bump=0.2)
    return [m]


def _person(m: dict, rng, x: float, z: float) -> None:
    """Pessoa de caixas (1,6 a 1,85 m) de frente para +z; às vezes com os braços para cima."""
    band = lambda k: meshes.band_uv(k, len(tx.PEOPLE))  # noqa: E731
    skin, hair = band(rng.choice(tx.SKINS)), band(rng.choice(tx.HAIRS))
    shirt, pants = band(rng.choice(tx.SHIRTS)), band(rng.choice(tx.PANTS))
    k = rng.uniform(0.94, 1.08)
    hip, sh, top = 0.86 * k, 1.42 * k, 1.68 * k
    for dx in (-0.1, 0.1):
        meshes.add_painted_box(m, (x + dx - 0.08, 0.0, z - 0.09), (x + dx + 0.08, hip, z + 0.09), pants)
    meshes.add_painted_box(m, (x - 0.21, hip, z - 0.12), (x + 0.21, sh, z + 0.12), shirt)
    meshes.add_painted_box(m, (x - 0.1, sh, z - 0.1), (x + 0.1, top, z + 0.11), skin)
    meshes.add_painted_box(m, (x - 0.11, top - 0.08, z - 0.12), (x + 0.11, top + 0.03, z + 0.06), hair)
    up = rng.random() < 0.15
    for dx in (-0.27, 0.27):
        if up:
            meshes.add_painted_box(m, (x + dx - 0.06, sh - 0.05, z - 0.06), (x + dx + 0.06, top + 0.5, z + 0.06), skin)
        else:
            meshes.add_painted_box(m, (x + dx - 0.06, hip + 0.05, z - 0.07), (x + dx + 0.06, sh - 0.02, z + 0.07), shirt)


def crowd(seed: int) -> list[dict]:
    """Grupo de 4 a 7 pessoas numa faixa de 3 × 1,2 m, viradas para +z (a pista, como o tipo é posto)."""
    rng = random.Random(seed)
    m = meshes.mesh("crowd", PEOPLE)
    spots = [(-1.2 + 0.6 * i + rng.uniform(-0.12, 0.12), (0.3 if row == 0 else -0.35) + rng.uniform(-0.08, 0.08))
             for row in range(2) for i in range(5)]
    rng.shuffle(spots)
    for x, z in sorted(spots[:rng.randint(4, 7)]):
        _person(m, rng, x, z)
    return [m]


def umbrella(band: int) -> list[dict]:
    """Guarda-sol de 2,4 m de diâmetro: haste e copa em gomos de duas cores (`band` e branco), visível de baixo."""
    m = meshes.mesh("umbrella", PAINT)
    meshes.add_painted_box(m, (-0.025, 0.0, -0.025), (0.025, 2.3, 0.025), _paint(tx.PAINT_STEEL))
    sides, r, rim, apex = 8, 1.2, 1.85, 2.35
    for k in range(sides):
        a0, a1 = 2 * math.pi * k / sides, 2 * math.pi * (k + 1) / sides
        p0, p1 = (r * math.cos(a0), rim, r * math.sin(a0)), (r * math.cos(a1), rim, r * math.sin(a1))
        u0, v0, u1, v1 = _paint(band if k % 2 == 0 else tx.PAINT_WHITE)
        uv = [((u0 + u1) / 2, v0), (u0, v1), (u1, v1)]
        meshes.add_tri(m, [(0.0, apex, 0.0), p0, p1], uv, (0.0, rim - 1.0, 0.0))
        meshes.add_tri(m, [(0.0, apex - 0.02, 0.0), p0, p1], uv, (0.0, apex + 1.0, 0.0))
    return [m]


def _shaped(m: dict, tmp: dict, uv: tuple[float, float, float, float], place=meshes.standing(0.0, 0.0, 0.0)) -> None:
    """Junta `tmp` (UV de 0 a 1) a `m`, levando os UVs para o retângulo `uv` e cada ponto por `place`."""
    u0, v0, u1, v1 = uv
    base = len(m["positions"])
    m["positions"] += [place(*p) for p in tmp["positions"]]
    m["uvs"] += [(u0 + (u1 - u0) * min(1.0, u), v0 + (v1 - v0) * min(1.0, v)) for u, v in tmp["uvs"]]
    m["indices"] += [base + i for i in tmp["indices"]]


def _pole(m: dict, r0: float, r1: float, h: float, band: int, sides: int = 8) -> None:
    tmp = meshes.mesh("tmp", "")
    meshes.add_frustum(tmp, r0, r1, 0.0, h, sides)
    _shaped(m, tmp, _paint(band))


def turbine(phase: float) -> list[dict]:
    """Eólica de 100 m: torre cônica de 66 m, nacele e três pás de 34 m num rotor virado para +z (`phase` em graus
    gira as pás; dois tipos com fases diferentes para o parque não parecer clonado)."""
    m = meshes.mesh("turbine", PAINT)
    white = _paint(tx.PAINT_WHITE)
    meshes.add_painted_box(m, (-3.0, -2.0, -3.0), (3.0, 0.4, 3.0), _paint(tx.PAINT_CONCRETE))
    _pole(m, 1.9, 1.1, 66.0, tx.PAINT_WHITE, 12)
    hub_y, hub_z = 67.4, 3.0
    meshes.add_painted_box(m, (-1.4, 65.8, -5.0), (1.4, 68.8, 1.7), white)
    meshes.add_cylinder(m, 1.0, 2.0, 10, white, white, meshes.lying_z(0.0, hub_y, 2.6, 2.0))
    stations = ((1.0, 1.4), (5.0, 2.6), (12.0, 1.9), (22.0, 1.2), (34.0, 0.45))
    for k in range(3):
        a = math.radians(phase + 120.0 * k)
        d, e = (math.cos(a), math.sin(a)), (-math.sin(a), math.cos(a))

        def at(r: float, w: float, z: float) -> tuple[float, float, float]:
            return (d[0] * r + e[0] * w, hub_y + d[1] * r + e[1] * w, hub_z + z)

        lead = [at(r, 0.35 * c, 0.0) for r, c in stations]
        trail = [at(r, -0.65 * c, 0.0) for r, c in stations]
        axis = [at(r, 0.0, 0.0) for r, _ in stations]
        for i in range(len(stations) - 1):
            quad = (lead[i], lead[i + 1], trail[i + 1], trail[i])
            for z in (-0.18, 0.18):  # faces de frente e de trás
                q = [(p[0], p[1], p[2] + z) for p in quad]
                meshes.add_tri(m, [q[0], q[1], q[2]], [white[:2], white[2:], white[2:]], axis[i])
                meshes.add_tri(m, [q[0], q[2], q[3]], [white[:2], white[2:], white[2:]], axis[i])
            for edge in ((lead[i], lead[i + 1]), (trail[i + 1], trail[i])):  # bordas
                p0, p1 = edge
                q = [(p0[0], p0[1], p0[2] - 0.18), (p1[0], p1[1], p1[2] - 0.18),
                     (p1[0], p1[1], p1[2] + 0.18), (p0[0], p0[1], p0[2] + 0.18)]
                mid = tuple((axis[i][j] + axis[i + 1][j]) / 2 for j in range(3))
                meshes.add_tri(m, [q[0], q[1], q[2]], [white[:2], white[2:], white[2:]], mid)
                meshes.add_tri(m, [q[0], q[2], q[3]], [white[:2], white[2:], white[2:]], mid)
        tip = stations[-1][0]
        q = [at(tip, 0.35 * stations[-1][1], z) for z in (-0.18, 0.18)] + [at(tip, -0.65 * stations[-1][1], z) for z in (0.18, -0.18)]
        meshes.add_tri(m, [q[0], q[1], q[2]], [white[:2], white[2:], white[2:]], axis[-2])
        meshes.add_tri(m, [q[0], q[2], q[3]], [white[:2], white[2:], white[2:]], axis[-2])
    return [m]


def _windows(m: dict, w: float, d: float, floors: int, trim: int, door: bool) -> None:
    """Janelas (caixilho `trim` e vidro) nas quatro paredes de uma caixa w × d, andares de 2,9 m a partir de 0,3 m;
    porta no meio da parede +z, no térreo."""
    glass, frame = _paint(tx.PAINT_GLASS), _paint(trim)
    walls = (((0, 0, 1), (1, 0, 0), w, d / 2), ((0, 0, -1), (-1, 0, 0), w, d / 2),
             ((1, 0, 0), (0, 0, -1), d, w / 2), ((-1, 0, 0), (0, 0, 1), d, w / 2))
    for normal, right, span, dist in walls:
        n = max(1, int(span / 2.6))
        for f in range(floors):
            y = 0.3 + 2.9 * f + 1.55
            for k in range(n):
                u = (k + 0.5) * span / n - span / 2
                if door and f == 0 and normal == (0, 0, 1) and k == n // 2:
                    continue
                c = tuple(normal[i] * dist + right[i] * u + (y if i == 1 else 0.0) for i in range(3))
                meshes.add_face(m, tuple(c[i] + normal[i] * 0.01 for i in range(3)), right, (0, 1, 0), 1.2, 1.4, frame)
                meshes.add_face(m, tuple(c[i] + normal[i] * 0.02 for i in range(3)), right, (0, 1, 0), 1.0, 1.2, glass)
    if door:
        n = max(1, int(w / 2.6))
        u = (n // 2 + 0.5) * w / n - w / 2
        meshes.add_face(m, (u, 1.4, d / 2 + 0.01), (1, 0, 0), (0, 1, 0), 1.3, 2.3, frame)
        meshes.add_face(m, (u, 1.35, d / 2 + 0.02), (1, 0, 0), (0, 1, 0), 1.0, 2.1, _paint(tx.PAINT_WOOD))


def _gable(walls: dict, roof: dict | None, w: float, d: float, top: float, pitch: float, wall_uv, under_uv,
           roof_uv=None) -> float:
    """Telhado de duas águas com a cumeeira ao longo de x, beiral de 0,4 m; devolve a altura da cumeeira.
    `roof` com a textura de telha (u, v em metros) ou, sem ele, telhado pintado `roof_uv` em `walls`."""
    ridge = top + d / 2 * pitch
    for sx in (-1.0, 1.0):  # oitões
        x = sx * w / 2
        meshes.add_tri(walls, [(x, top, -d / 2), (x, top, d / 2), (x, ridge, 0.0)], [wall_uv[:2], wall_uv[2:], wall_uv[2:]],
                       (0.0, top, 0.0))
    ow, od = w / 2 + 0.4, d / 2 + 0.4
    eave = top - 0.4 * pitch
    slope = math.hypot(od, ridge - eave)
    for sz in (-1.0, 1.0):
        q = [(-ow, eave, sz * od), (ow, eave, sz * od), (ow, ridge, 0.0), (-ow, ridge, 0.0)]
        if roof is not None:
            uv = [(-ow / 2, slope / 2), (ow / 2, slope / 2), (ow / 2, 0.0), (-ow / 2, 0.0)]
            target = roof
        else:
            uv = [roof_uv[:2], roof_uv[2:], roof_uv[2:], roof_uv[:2]]
            target = walls
        meshes.add_tri(target, [q[0], q[1], q[2]], [uv[0], uv[1], uv[2]], (0.0, eave - 2.0, 0.0))
        meshes.add_tri(target, [q[0], q[2], q[3]], [uv[0], uv[2], uv[3]], (0.0, eave - 2.0, 0.0))
        # forro do beiral, visto de baixo
        lo = [(p[0], p[1] - 0.06, p[2]) for p in q]
        meshes.add_tri(walls, [lo[0], lo[1], lo[2]], [under_uv[:2], under_uv[2:], under_uv[2:]], (0.0, ridge + 5.0, 0.0))
        meshes.add_tri(walls, [lo[0], lo[2], lo[3]], [under_uv[:2], under_uv[2:], under_uv[2:]], (0.0, ridge + 5.0, 0.0))
    return ridge


def house(w: float, d: float, floors: int, wall: int, trim: int) -> list[dict]:
    """Casa de campo: alicerce, paredes pintadas, janelas com caixilho colorido, porta em +z, telhado de telha
    e chaminé."""
    walls = meshes.mesh("house", PAINT)
    wall_uv = _paint(wall)
    meshes.add_painted_box(walls, (-w / 2 - 0.15, -2.0, -d / 2 - 0.15), (w / 2 + 0.15, 0.3, d / 2 + 0.15),
                           _paint(tx.PAINT_CONCRETE))
    top = 0.3 + 2.9 * floors
    meshes.add_painted_box(walls, (-w / 2, 0.3, -d / 2), (w / 2, top, d / 2), wall_uv)
    _windows(walls, w, d, floors, trim, True)
    roof = meshes.mesh("house_roof", ROOF)
    ridge = _gable(walls, roof, w, d, top, 0.7, wall_uv, _paint(tx.PAINT_WOOD))
    meshes.add_painted_box(walls, (w * 0.22, ridge - 1.6, -0.9), (w * 0.22 + 0.7, ridge + 0.9, -0.2), wall_uv)
    return [walls, roof]


def barn() -> list[dict]:
    """Celeiro vermelho de 14 × 10 m com telhado de chapa cinza e portão branco no oitão +x."""
    w, d, top = 14.0, 10.0, 5.3
    walls = meshes.mesh("barn", PAINT)
    red = _paint(tx.PAINT_RED)
    meshes.add_painted_box(walls, (-w / 2 - 0.1, -2.0, -d / 2 - 0.1), (w / 2 + 0.1, 0.3, d / 2 + 0.1), _paint(tx.PAINT_CONCRETE))
    meshes.add_painted_box(walls, (-w / 2, 0.3, -d / 2), (w / 2, top, d / 2), red)
    white = _paint(tx.PAINT_WHITE)
    meshes.add_face(walls, (w / 2 + 0.01, 2.3, 0.0), (0, 0, -1), (0, 1, 0), 4.4, 4.0, white)
    meshes.add_face(walls, (w / 2 + 0.02, 2.2, 0.0), (0, 0, -1), (0, 1, 0), 3.8, 3.6, _paint(tx.PAINT_RED))
    meshes.add_face(walls, (w / 2 + 0.03, 2.2, 0.0), (0, 0, -1), (0, 1, 0), 0.15, 3.6, white)
    meshes.add_face(walls, (w / 2 + 0.01, top + 1.2, 0.0), (0, 0, -1), (0, 1, 0), 1.2, 1.0, white)  # porta do feno
    for x in (-4.0, 0.0, 4.0):
        meshes.add_face(walls, (x, 2.8, d / 2 + 0.01), (1, 0, 0), (0, 1, 0), 1.1, 1.1, white)
        meshes.add_face(walls, (x, 2.8, d / 2 + 0.02), (1, 0, 0), (0, 1, 0), 0.9, 0.9, _paint(tx.PAINT_GLASS))
    _gable(walls, None, w, d, top, 0.6, red, _paint(tx.PAINT_DARK), _paint(tx.PAINT_GREY))
    return [walls]


def watertower() -> list[dict]:
    """Caixa d'água de 23 m: quatro pés de aço com travessas, tanque branco com faixa vermelha e cobertura cônica."""
    m = meshes.mesh("watertower", PAINT)
    steel, white, red = _paint(tx.PAINT_STEEL), _paint(tx.PAINT_WHITE), _paint(tx.PAINT_RED)
    for x in (-2.2, 2.2):
        for z in (-2.2, 2.2):
            meshes.add_painted_box(m, (x - 0.5, -1.5, z - 0.5), (x + 0.5, 0.3, z + 0.5), _paint(tx.PAINT_CONCRETE))
            meshes.add_painted_box(m, (x - 0.14, 0.3, z - 0.14), (x + 0.14, 15.2, z + 0.14), steel)
    for y in (5.0, 10.0):
        for z in (-2.2, 2.2):
            meshes.add_painted_box(m, (-2.2, y, z - 0.07), (2.2, y + 0.14, z + 0.07), steel)
        for x in (-2.2, 2.2):
            meshes.add_painted_box(m, (x - 0.07, y, -2.2), (x + 0.07, y + 0.14, 2.2), steel)
    meshes.add_cylinder(m, 3.3, 5.5, 16, white, white, meshes.standing(0.0, 15.0, 0.0))
    meshes.add_cylinder(m, 3.34, 0.7, 16, red, red, meshes.standing(0.0, 18.4, 0.0))
    rim, apex, r = 20.5, 22.8, 3.6
    for k in range(16):
        a0, a1 = 2 * math.pi * k / 16, 2 * math.pi * (k + 1) / 16
        p0, p1 = (r * math.cos(a0), rim, r * math.sin(a0)), (r * math.cos(a1), rim, r * math.sin(a1))
        meshes.add_tri(m, [(0.0, apex, 0.0), p0, p1], [red[:2], red[2:], red[2:]], (0.0, rim - 1.0, 0.0))
        meshes.add_tri(m, [(0.0, rim, 0.0), p0, p1], [steel[:2], steel[2:], steel[2:]], (0.0, rim + 1.0, 0.0))
    return [m]


FLAGS = {"tricolor": (tx.PAINT_BLUE, tx.PAINT_WHITE, tx.PAINT_RED), "green": (tx.PAINT_GREEN, tx.PAINT_YELLOW, tx.PAINT_GREEN),
         "orange": (tx.PAINT_ORANGE, tx.PAINT_WHITE, tx.PAINT_ORANGE), "checker": ()}


def flag(name: str) -> list[dict]:
    """Mastro de 9 m com bandeira de 1,8 × 1,2 m em +x (listras verticais, ou xadrez de chegada)."""
    m = meshes.mesh("flag", PAINT)
    meshes.add_painted_box(m, (-0.3, -0.6, -0.3), (0.3, 0.15, 0.3), _paint(tx.PAINT_CONCRETE))
    _pole(m, 0.06, 0.04, 9.0, tx.PAINT_STEEL, 6)
    meshes.add_painted_box(m, (-0.09, 9.0, -0.09), (0.09, 9.18, 0.09), _paint(tx.PAINT_WHITE))
    x0, y0, y1 = 0.07, 7.4, 8.6
    stripes = FLAGS[name]
    if stripes:
        for k, band in enumerate(stripes):
            meshes.add_painted_box(m, (x0 + 0.6 * k, y0, -0.015), (x0 + 0.6 * (k + 1), y1, 0.015), _paint(band))
    else:
        for i in range(6):
            for j in range(4):
                band = tx.PAINT_BLACK if (i + j) % 2 else tx.PAINT_WHITE
                meshes.add_painted_box(m, (x0 + 0.3 * i, y0 + 0.3 * j, -0.015), (x0 + 0.3 * (i + 1), y0 + 0.3 * (j + 1), 0.015),
                                       _paint(band))
    return [m]


def lightpole() -> list[dict]:
    """Poste de 12 m com braço e luminária para +z (a lente branca embaixo)."""
    m = meshes.mesh("lightpole", PAINT)
    steel = _paint(tx.PAINT_STEEL)
    meshes.add_painted_box(m, (-0.35, -0.8, -0.35), (0.35, 0.3, 0.35), _paint(tx.PAINT_CONCRETE))
    _pole(m, 0.16, 0.09, 12.0, tx.PAINT_STEEL)
    meshes.add_painted_box(m, (-0.05, 11.7, -0.05), (0.05, 11.82, 1.8), steel)
    meshes.add_painted_box(m, (-0.28, 11.48, 1.5), (0.28, 11.78, 2.5), _paint(tx.PAINT_DARK))
    meshes.add_face(m, (0.0, 11.47, 2.0), (1, 0, 0), (0, 0, 1), 0.48, 0.9, _paint(tx.PAINT_CANVAS))
    return [m]


CAR_COLORS = {"red": tx.PAINT_RED, "blue": tx.PAINT_BLUE, "white": tx.PAINT_WHITE, "grey": tx.PAINT_GREY,
              "black": tx.PAINT_BLACK, "yellow": tx.PAINT_YELLOW}


def car(band: int) -> list[dict]:
    """Carro de passeio de 4,2 m ao longo de x (frente em +x): carroceria, cabine de vidro, teto, faróis e rodas."""
    m = meshes.mesh("car", PAINT)
    c = _paint(band)
    meshes.add_painted_box(m, (-2.1, 0.32, -0.86), (2.1, 0.9, 0.86), c)
    meshes.add_painted_box(m, (-1.3, 0.9, -0.8), (0.75, 1.4, 0.8), _paint(tx.PAINT_GLASS))
    meshes.add_painted_box(m, (-1.2, 1.4, -0.76), (0.62, 1.47, 0.76), c)
    meshes.add_painted_box(m, (-2.14, 0.3, -0.84), (2.14, 0.45, 0.84), _paint(tx.PAINT_DARK))  # para-choques
    for z in (-0.6, 0.6):
        meshes.add_face(m, (2.141, 0.72, z), (0, 0, -1), (0, 1, 0), 0.32, 0.14, _paint(tx.PAINT_CANVAS))
        meshes.add_face(m, (-2.141, 0.72, z), (0, 0, 1), (0, 1, 0), 0.32, 0.14, _paint(tx.PAINT_RED))
    for x in (-1.35, 1.35):
        for z in (-0.78, 0.78):
            meshes.add_cylinder(m, 0.33, 0.24, 10, _paint(tx.PAINT_BLACK), _paint(tx.PAINT_GREY), meshes.lying_z(x, 0.33, z, 0.24))
    return [m]


def types() -> dict[str, list[dict]]:
    """Tipo -> peças, na ordem em que entram na biblioteca."""
    out: dict[str, list[dict]] = {}
    for row in range(len(tx.ADS)):
        out[f"o:synth_adboard_{row + 1}"] = adboard(row)
    for color in TENT_COLORS:
        out[f"o:synth_tent_{color}"] = tent(color)
    out["o:synth_truck_a"] = truck(tx.PAINT_RED, 0)
    out["o:synth_truck_b"] = truck(tx.PAINT_BLUE, 2)
    for color in CONTAINERS:
        out[f"o:synth_container_{color}"] = container(color)
    out["o:synth_portaloo"] = portaloo()
    out["o:synth_tower"] = tower()
    out["o:synth_haybale"] = haybale()
    out["o:synth_tyrestack"] = tyrestack()
    for k, (r, flat) in enumerate(((0.9, 0.7), (1.4, 0.55), (2.2, 0.6))):
        out[f"o:synth_boulder_{'abc'[k]}"] = boulder(301 + k, r, flat)
    out["t:synth_bush_a"] = bush(401, 2.2, 1.3)
    out["t:synth_bush_b"] = bush(402, 1.5, 0.9)
    for k in range(4):
        out[f"o:synth_crowd_{'abcd'[k]}"] = crowd(501 + k)
    for color in ("red", "blue", "yellow"):
        out[f"o:synth_umbrella_{color}"] = umbrella(TENT_COLORS[color])
    out["o:synth_turbine_a"] = turbine(0.0)
    out["o:synth_turbine_b"] = turbine(47.0)
    out["o:synth_house_a"] = house(9.0, 7.0, 1, tx.PAINT_CANVAS, tx.PAINT_BLUE)
    out["o:synth_house_b"] = house(11.0, 8.0, 2, tx.PAINT_CANVAS, tx.PAINT_GREEN)
    out["o:synth_house_c"] = house(8.0, 6.5, 1, tx.PAINT_YELLOW, tx.PAINT_WHITE)
    out["o:synth_barn"] = barn()
    out["o:synth_watertower"] = watertower()
    for name in FLAGS:
        out[f"o:synth_flag_{name}"] = flag(name)
    out["o:synth_lightpole"] = lightpole()
    for name, band in CAR_COLORS.items():
        out[f"o:synth_car_{name}"] = car(band)
    return out


MATERIALS = {ADS: "synth_ads_d", PAINT: "synth_paint_d", CONTAINER: "synth_container_d",
             BUILDING: "synth_building_d", HAY: "synth_hay_d", BOULDER: "synth_boulder_d", BUSH: "synth_bush_d",
             PEOPLE: "synth_people_d", ROOF: "synth_roof_d"}
