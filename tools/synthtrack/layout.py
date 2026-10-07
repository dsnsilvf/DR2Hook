"""Traçado da pista sintética: um circuito fechado em Catmull-Rom, amostrado a cada `STEP` metros.

Unidades em metros, Y para cima (o sistema do jogo). A altura da pista sobe e desce devagar ao
longo do percurso; o relevo em volta é `hills`, achatado perto da pista.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

STEP = 2.0          # metros entre amostras do traçado
ROAD_HALF = 5.0     # meia largura do asfalto
BASE_Y = 100.0      # altura média da pista

# Pontos de controle (x, z), em sentido anti-horário visto de cima: reta dos boxes, grampo,
# uma chicane e uma curva longa. Perímetro ~1,6 km.
CONTROL = [
    (0, -260), (180, -260), (300, -210), (330, -100), (260, -20), (200, 40), (250, 130),
    (190, 230), (40, 250), (-60, 170), (-40, 80), (-160, 40), (-300, 90), (-360, -20),
    (-300, -170), (-170, -250),
]


@dataclass
class Sample:
    s: float                      # distância ao longo da pista
    p: tuple[float, float, float]  # centro da pista
    t: tuple[float, float]        # tangente (x, z), unitária
    n: tuple[float, float]        # normal à esquerda (x, z), unitária
    curv: float                   # curvatura com sinal (1/m), positiva para a esquerda


def _catmull(p0, p1, p2, p3, u):
    u2, u3 = u * u, u * u * u
    return tuple(0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * u + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * u2
                        + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * u3) for k in range(2))


def road_height(s: float, length: float) -> float:
    a = 2 * math.pi * s / length
    return BASE_Y + 4.0 * math.sin(a) + 1.5 * math.sin(3 * a + 0.7)


def hills(x: float, z: float) -> float:
    return (BASE_Y + 9.0 * math.sin(x * 0.011 + 0.4) * math.cos(z * 0.009)
            + 4.0 * math.sin(x * 0.031) * math.sin(z * 0.027 + 1.1) + 0.00004 * (x * x + z * z))


def samples() -> list[Sample]:
    """Amostras uniformes do circuito fechado (a última não repete a primeira)."""
    n = len(CONTROL)
    dense = []
    for i in range(n):
        p0, p1, p2, p3 = (CONTROL[(i + k - 1) % n] for k in range(4))
        for j in range(40):
            dense.append(_catmull(p0, p1, p2, p3, j / 40))
    # reparametriza por comprimento de arco
    acc = [0.0]
    for i in range(1, len(dense) + 1):
        a, b = dense[i - 1], dense[i % len(dense)]
        acc.append(acc[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    length = acc[-1]
    count = int(length // STEP)
    step = length / count
    pts = []
    j = 0
    for k in range(count):
        s = k * step
        while acc[j + 1] < s:
            j += 1
        a, b = dense[j], dense[(j + 1) % len(dense)]
        f = (s - acc[j]) / max(1e-9, acc[j + 1] - acc[j])
        pts.append((a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, s))
    out = []
    for k, (x, z, s) in enumerate(pts):
        px, pz, _ = pts[k - 1]
        nx_, nz_, _ = pts[(k + 1) % count]
        tx, tz = nx_ - px, nz_ - pz
        tl = math.hypot(tx, tz) or 1.0
        tx, tz = tx / tl, tz / tl
        # curvatura pela variação da direção
        h0 = math.atan2(z - pz, x - px)
        h1 = math.atan2(nz_ - z, nx_ - x)
        dh = (h1 - h0 + math.pi) % (2 * math.pi) - math.pi
        out.append(Sample(s, (x, road_height(s, length), z), (tx, tz), (-tz, tx), dh / (2 * step)))
    return out


class RoadIndex:
    """Busca do ponto da pista mais perto de (x, z), por grade de 25 m."""

    CELL = 25.0

    def __init__(self, pts: list[Sample]):
        self.pts = pts
        self.grid: dict[tuple[int, int], list[int]] = {}
        for i, p in enumerate(pts):
            self.grid.setdefault(self._key(p.p[0], p.p[2]), []).append(i)

    def _key(self, x: float, z: float) -> tuple[int, int]:
        return int(math.floor(x / self.CELL)), int(math.floor(z / self.CELL))

    def nearest(self, x: float, z: float, reach: int = 2) -> tuple[float, int]:
        """(distância, índice) da amostra mais perto, procurando até `reach` células; (inf, -1) se nada."""
        kx, kz = self._key(x, z)
        best, bi = math.inf, -1
        for dx in range(-reach, reach + 1):
            for dz in range(-reach, reach + 1):
                for i in self.grid.get((kx + dx, kz + dz), ()):
                    p = self.pts[i].p
                    d = (p[0] - x) ** 2 + (p[2] - z) ** 2
                    if d < best:
                        best, bi = d, i
        return math.sqrt(best), bi


@dataclass(frozen=True)
class Pad:
    """Retângulo aplainado (paddock, base de prédio): plano y = y0 + gx·x + gz·z dentro de (x0, z0)–(x1, z1),
    voltando ao relevo numa rampa de `edge` metros em volta. Não mexe na faixa achatada ao lado da pista."""

    x0: float
    z0: float
    x1: float
    z1: float
    y0: float
    gx: float = 0.0
    gz: float = 0.0
    edge: float = 15.0

    def height(self, x: float, z: float) -> float:
        return self.y0 + self.gx * x + self.gz * z

    def weight(self, x: float, z: float) -> float:
        """0 dentro do retângulo, 1 a partir de `edge` metros fora dele (suave no meio)."""
        out = math.hypot(max(self.x0 - x, 0.0, x - self.x1), max(self.z0 - z, 0.0, z - self.z1))
        if out >= self.edge:
            return 1.0
        f = out / self.edge
        return f * f * (3 - 2 * f)


@dataclass(frozen=True)
class Berm:
    """Barranco de público por fora de uma curva: um morro de terra ao longo das amostras `pts` ((x, z, nx, nz)
    da pista), do lado `side` (+1 = esquerda, +n), entre `d0` e `d0 + width` metros do centro. Perfil sen²
    na largura, pontas suavizadas no primeiro e no último quarto das amostras."""

    pts: tuple[tuple[float, float, float, float], ...]
    side: float
    d0: float = 26.0
    width: float = 16.0
    height: float = 3.0

    def box(self) -> tuple[float, float, float, float]:
        r = self.d0 + self.width + 2.0
        return (min(p[0] for p in self.pts) - r, min(p[1] for p in self.pts) - r,
                max(p[0] for p in self.pts) + r, max(p[1] for p in self.pts) + r)

    def rise(self, x: float, z: float) -> float:
        bx0, bz0, bx1, bz1 = self.box()
        if not (bx0 <= x <= bx1 and bz0 <= z <= bz1):
            return 0.0
        k = min(range(len(self.pts)), key=lambda i: (self.pts[i][0] - x) ** 2 + (self.pts[i][1] - z) ** 2)
        px, pz, nx, nz = self.pts[k]
        u = (((x - px) * nx + (z - pz) * nz) * self.side - self.d0) / self.width
        if not 0.0 < u < 1.0:
            return 0.0
        t = min(k, len(self.pts) - 1 - k) / (0.25 * (len(self.pts) - 1))
        t = min(1.0, t)
        return self.height * math.sin(math.pi * u) ** 2 * t * t * (3 - 2 * t)


def terrain_height(x: float, z: float, road: RoadIndex, pads: tuple[Pad, ...] = (),
                   berms: tuple[Berm, ...] = ()) -> float:
    """Relevo achatado até a altura da pista perto dela (abaixo do asfalto 5 cm), misturando até 35 m;
    os `pads` aplainam áreas mais longe (paddock, torre); os `berms` sobem barrancos por fora das curvas
    (somem a menos de 6 m da faixa achatada de outro trecho)."""
    d, i = road.nearest(x, z)
    flat, blend = ROAD_HALF + 9.0, 35.0
    if i >= 0 and d <= flat:
        return road.pts[i].p[1] - 0.05
    y = hills(x, z)
    if i >= 0 and d < flat + blend:
        ry = road.pts[i].p[1] - 0.05
        f = (d - flat) / blend
        y = ry + (y - ry) * f * f * (3 - 2 * f)
    for pad in pads:
        w = pad.weight(x, z)
        if w < 1.0:
            py = pad.height(x, z)
            y = py + (y - py) * w
    if berms and i >= 0:
        f = min(1.0, max(0.0, (d - flat) / 6.0))
        if f > 0.0:
            y += f * sum(b.rise(x, z) for b in berms)
    return y
