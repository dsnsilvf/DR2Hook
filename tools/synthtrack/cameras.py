"""Câmeras de replay da pista sintética, no sistema do layout (metros, Y para cima).

O replay do jogo (`replay_camera_config.xml` da rota) tem três peças, e o plano gerado aqui tem uma lista para
cada uma, que o `track.json` guarda em `routes[i]["replay"]`:

- `cameras`: câmeras com nome, posição (`pos`) e ponto para onde olham (`aim`). `kind` diz o tipo:
  - "trackside": câmera de beira de pista que segue o carro com zoom (as `camera_r0_spectator_NNN`);
  - "static": parada, sem zoom (a "gopro" no ápice);
  - "dolly": anda num caminho (`path`) olhando para outro (`target`, opcional). Os caminhos são curvas de Bézier
    cúbicas em grupos de 4 pontos, como no jogo (o último de um grupo repete no primeiro do seguinte).
  As tomadas especiais (`role`) usam os nomes que o jogo procura: largada, pré-corrida, chegada, 1ª curva.
- `zones`: portões que trocam a câmera quando o carro passa (`l`/`r` são as pontas, a 0,5 m do asfalto). `switch`
  é a lista de (câmera, probabilidade); `lap` > 0 só vale naquela volta. Nomes fora do plano (`onboard_front`,
  `onboard_rear`, `external_front_R`) são câmeras do próprio carro.
- `bounds`: prismas verticais (4 cantos xz, `y0`–`y1`) em volta das peças altas perto da pista, como os da
  Montalegre em volta das cabines dos fiscais (`cameralines.cqtc`, material CBND).

As câmeras de pódio e de serviço não estão aqui: elas olham para o parque de serviço da Montalegre (definido em
outro arquivo) e o porte as deixa como estão.
"""

from __future__ import annotations

import math
from typing import Callable

from . import layout

SPACING = 150.0     # metros entre câmeras de beira de pista
LEAD = 70.0         # da zona até a câmera que ela liga (o carro vem na direção da câmera)
GRID_BACK = 64.0    # a grade de largada do jogo (grids.pssg da Montalegre) fica 64 m antes da linha de chegada
CAM_HEIGHT = 3.5    # acima do chão (as da Montalegre ficam 2–4 m acima da pista)
CLEAR = 3.0         # raio livre de objetos em volta de uma câmera
BOUND_TYPES = {"o:synth_tower": 4.8, "o:synth_lightpole": 1.0, "o:synth_portaloo": 1.0}  # meia largura do prisma
BOUND_REACH = 40.0  # só as peças até esta distância da linha central ganham prisma
ONBOARD = (("onboard_front", 0.15), ("onboard_rear", 0.15))
SWITCH_MIX = (ONBOARD, (("external_front_R", 0.2),), ())


class Frame:
    """Pontos no referencial da pista: distância `s` (dá a volta), lado `lat` (+ = esquerda, a normal `n` do
    layout) e altura `up`, acima do chão (`ground`) ou da linha central (`on_road`)."""

    def __init__(self, pts: list[layout.Sample], ground: Callable[[float, float], float]):
        self.pts = pts
        self.ground = ground
        self.step = pts[1].s - pts[0].s   # um pouco acima de STEP: o circuito é dividido em partes iguais
        self.length = len(pts) * self.step

    def sample(self, s: float) -> tuple[tuple[float, float, float], tuple[float, float], tuple[float, float]]:
        n = len(self.pts)
        u = (s % self.length) / self.step
        i = int(u)
        f = u - i
        a, b = self.pts[i % n], self.pts[(i + 1) % n]
        p = tuple(a.p[k] + (b.p[k] - a.p[k]) * f for k in range(3))
        t = (a.t[0] + (b.t[0] - a.t[0]) * f, a.t[1] + (b.t[1] - a.t[1]) * f)
        tl = math.hypot(*t) or 1.0
        t = (t[0] / tl, t[1] / tl)
        return p, t, (-t[1], t[0])

    def at(self, s: float, lat: float = 0.0, up: float = 0.0, on_road: bool = False) -> list[float]:
        p, _, nrm = self.sample(s)
        x, z = p[0] + nrm[0] * lat, p[2] + nrm[1] * lat
        y = (p[1] if on_road else self.ground(x, z)) + up
        return [round(x, 3), round(y, 3), round(z, 3)]

    def index(self, s: float) -> int:
        return int(round((s % self.length) / self.step)) % len(self.pts)

    def curv(self, s: float, half: float = 20.0) -> float:
        """Curvatura média num trecho de ±`half` metros."""
        ks = range(int((s - half) / self.step), int((s + half) / self.step) + 1)
        return sum(self.pts[k % len(self.pts)].curv for k in ks) / len(ks)


def bezier(a: list[float], b: list[float]) -> list[list[float]]:
    """Um trecho reto de `a` a `b` como Bézier (controles a 1/3 e 2/3, como os da Montalegre)."""
    return [[round(a[k] + (b[k] - a[k]) * f, 3) for k in range(3)] for f in (0.0, 1 / 3, 2 / 3, 1.0)]


def bezier_chain(pts: list[list[float]]) -> list[list[float]]:
    out: list[list[float]] = []
    for a, b in zip(pts, pts[1:]):
        out.extend(bezier(a, b))
    return out


class _Obstacles:
    """Posições (x, z) das instâncias em grade de 10 m, para achar lugar livre."""

    CELL = 10.0

    def __init__(self, items: list[dict]):
        self.grid: dict[tuple[int, int], list[tuple[float, float]]] = {}
        for i in items:
            if "_dist_" in i["type"]:
                continue
            x, z = i["m"][12], i["m"][14]
            self.grid.setdefault((int(x // self.CELL), int(z // self.CELL)), []).append((x, z))

    def near(self, x: float, z: float, r: float) -> bool:
        kx, kz = int(x // self.CELL), int(z // self.CELL)
        return any((px - x) ** 2 + (pz - z) ** 2 < r * r
                   for dx in (-1, 0, 1) for dz in (-1, 0, 1) for px, pz in self.grid.get((kx + dx, kz + dz), ()))


def _spot(fr: Frame, road: layout.RoadIndex, obs: _Obstacles, s: float, sides: tuple[float, ...],
          lat0: float) -> tuple[float, float] | None:
    """(lado, distância) do primeiro lugar livre a partir de `lat0` metros: longe de objetos e mais perto
    deste trecho da pista do que de qualquer outro."""
    k = fr.index(s)
    n = len(fr.pts)
    for side in sides:
        for extra in range(0, 14, 2):
            lat = lat0 + extra
            x, _, z = fr.at(s, side * lat, on_road=True)
            d, i = road.nearest(x, z)
            other = min((i - k) % n, (k - i) % n) > 15
            if not other and d >= lat - 1.0 and not obs.near(x, z, CLEAR):
                return side, lat
    return None


def _gate(fr: Frame, road: layout.RoadIndex, s: float, half: float) -> tuple[list[float], list[float]]:
    """Pontas de uma zona: até `half` m de cada lado, sem entrar em outro trecho da pista."""
    k = fr.index(s)
    n = len(fr.pts)
    ends = []
    for side in (1.0, -1.0):
        w = layout.ROAD_HALF
        while w < half:
            x, _, z = fr.at(s, side * (w + 1.0), on_road=True)
            i = road.nearest(x, z)[1]
            if min((i - k) % n, (k - i) % n) > 10:
                break
            w += 1.0
        ends.append(fr.at(s, side * w, 0.5, on_road=True))
    return ends[0], ends[1]


def plan(pts: list[layout.Sample], ground: Callable[[float, float], float], items: list[dict],
         corner: float) -> dict:
    fr = Frame(pts, ground)
    road = layout.RoadIndex(pts)
    obs = _Obstacles(items)
    h = layout.ROAD_HALF
    length = fr.length
    cams: list[dict] = []
    zones: list[dict] = []

    # ── beira de pista: uma zona a cada SPACING metros, a câmera LEAD metros adiante ───────────────────────
    count = max(4, round(length / SPACING))
    for j in range(count):
        zs = j * length / count - 30.0
        cs = zs + LEAD
        c = fr.curv(cs)
        if abs(c) > corner / 2:
            sides, lat0 = (-math.copysign(1.0, c),), h + 19.0   # por fora da curva, atrás dos pneus
        else:
            sides, lat0 = (1.0, -1.0), h + 10.0                  # atrás da barreira da reta
        spot = _spot(fr, road, obs, cs, sides, lat0) or _spot(fr, road, obs, cs, (-sides[0], sides[0]), lat0)
        if spot is None:
            continue
        side, lat = spot
        name = f"camera_r0_spectator_{len(cams) + 1:03d}"
        cams.append({"name": name, "kind": "trackside", "s": round(cs % length, 2),
                     "pos": fr.at(cs, side * lat, CAM_HEIGHT), "aim": fr.at(zs + LEAD / 2, 0.0, 0.5, on_road=True)})
        mix = SWITCH_MIX[j % len(SWITCH_MIX)]
        l, r = _gate(fr, road, zs, h + (12.0 if abs(fr.curv(zs)) > corner / 2 else 7.0))
        zones.append({"name": f"Box{len(zones) + 1:03d}", "s": round(zs % length, 2), "l": l, "r": r,
                      "switch": [{"camera": name, "p": round(1.0 - sum(p for _, p in mix), 2)}]
                      + [{"camera": c, "p": p} for c, p in mix]})

    # ── tomadas especiais ───────────────────────────────────────────────────────────────────────────────────
    g = -GRID_BACK
    at = fr.at

    def dolly(name, role, src, tgt=None, duration=10.0, aim=None):
        cam = {"name": name, "kind": "dolly", "role": role, "pos": src[0], "path": bezier_chain(src),
               "duration": duration}
        if tgt is not None:
            cam["target"] = bezier_chain(tgt)
        cam["aim"] = aim or (tgt[0] if tgt is not None else at(0.0, 0.0, 1.0, on_road=True))
        cams.append(cam)

    cams.append({"name": "initial_camera_r0", "kind": "trackside", "role": "inicial", "s": round(-11.0 % length, 2),
                 "pos": at(-11.0, 24.0, 8.0), "aim": at(g, 0.0, 1.0, on_road=True)})
    dolly("Grid_Start_Cam_00", "largada", [at(g - 30, 1.5, 5.6, True), at(g - 28, 9.6, 4.8, True)],
          [at(g, -2.5, 2.3, True), at(g, -2.5, 2.3, True)], 13.333)
    dolly("pre_race_intro_001", "pré-corrida", [at(g - 90, 60.0, 45.0, True), at(g - 30, 25.0, 15.0, True)],
          [at(g - 20, 0.0, 0.5, True), at(g, 0.0, 1.0, True)], 8.333)
    dolly("pre_race_001", "pré-corrida", [at(g + 14, 11.0, 4.5, True), at(g + 16, 13.8, 7.7, True)],
          [at(g + 44, 3.1, 3.7, True), at(g + 46, 4.9, 7.2, True)], 12.833)
    dolly("pre_race_002", "pré-corrida (pórtico)", [at(55, 12.0, 1.5), at(50, 10.0, 4.0)],
          [at(40, 0.0, 6.0, True), at(40, 0.0, 5.0, True)], 11.267)
    dolly("pre_race_003", "pré-corrida (arquibancada)", [at(75, 8.0, 2.0), at(95, 8.0, 3.0)],
          [at(60, -(h + 22), 4.0), at(90, -(h + 22), 4.0)], 11.033)
    dolly("pre_race_004", "pré-corrida (grade de frente)", [at(g + 25, -4.0, 0.8, True), at(g + 25, 4.0, 0.8, True)],
          [at(g, 0.0, 1.0, True), at(g - 10, 0.0, 1.0, True)], 8.733)
    dolly("pre_race_005", "pré-corrida (paddock)", [at(20, -12.0, 6.0), at(80, -12.0, 6.0)],
          [at(20, -(h + 40), 1.5), at(80, -(h + 40), 1.5)], 10.833)
    dolly("pre_race_006", "pré-corrida (torre)", [at(150, 6.0, 1.0), at(170, 6.0, 1.5)],
          [at(160, -(h + 18), 10.0), at(160, -(h + 18), 6.0)], 10.667)
    dolly("pre_race_007", "pré-corrida (grade do alto)", [at(g - 10, 20.0, 12.0, True), at(g + 10, 20.0, 10.0, True)],
          [at(g - 10, 0.0, 0.5, True), at(g + 10, 0.0, 0.5, True)], 10.7)
    dolly("finish_line_camera_001", "chegada", [at(0, -4.6, 3.0, True), at(0, -7.5, 3.4, True), at(0, -10.3, 3.8, True)],
          None, 5.0, aim=at(-20, 0.0, 1.0, True))

    # primeira curva depois da largada: câmera por fora, olhando a entrada
    k0 = next(k for k in range(len(pts)) if abs(pts[k].curv) > corner)
    k1 = next(k for k in range(k0, len(pts)) if abs(pts[k].curv) <= corner)
    apex = max(range(k0, k1), key=lambda k: abs(pts[k].curv))
    side = -math.copysign(1.0, pts[apex].curv)
    sa, s0 = pts[apex].s, pts[k0].s
    dolly("first_corner_001", "1ª curva", [at(sa, side * (h + 20), 4.0), at(sa - 10, side * (h + 18), 3.0)],
          [at(s0 - 30, 0.0, 0.5, True), at(sa, 0.0, 0.5, True)], 5.167)

    # volta 1: a zona logo depois da linha liga uma câmera no meio da reta, com o pelotão ainda junto
    fs = 110.0
    spot = _spot(fr, road, obs, fs, (1.0, -1.0), h + 10.0)
    if spot:
        cams.append({"name": "camera_r0_firstlap_001", "kind": "trackside", "role": "1ª volta", "s": fs,
                     "pos": at(fs, spot[0] * spot[1], 1.5), "aim": at(fs - 60, 0.0, 0.5, True)})
        l, r = _gate(fr, road, 5.0, h + 7.0)
        zones.append({"name": "Box_firstlap", "s": 5.0, "l": l, "r": r, "lap": 1,
                      "switch": [{"camera": "camera_r0_firstlap_001", "p": 1.0}]})

    # curva mais fechada: câmera parada baixinha no ápice, por dentro, e a do joker (o Ring não tem joker)
    tight = max(range(len(pts)), key=lambda k: abs(pts[k].curv))
    st = pts[tight].s
    inside = math.copysign(1.0, pts[tight].curv)
    cams.append({"name": "camera_r0_gopro_001", "kind": "static", "role": "ápice", "s": round(st, 2),
                 "pos": at(st, inside * (h + 1.5), 0.6), "aim": at(st - 25, 0.0, 0.8, True)})
    dolly("joker_r0", "joker (não usada)", [at(st - 40, -inside * (h + 22), 5.0), at(st - 10, -inside * (h + 22), 4.0)],
          [at(st - 40, 0.0, 0.5, True), at(st, 0.0, 0.5, True)], 8.333)
    # a zona da curva fechada sorteia entre a câmera de beira de pista e a do ápice
    near = min(zones, key=lambda zn: min((zn["s"] - (st - 50)) % length, (st - 50 - zn["s"]) % length))
    if not near.get("lap"):
        first = near["switch"][0]
        first["p"] = round(first["p"] / 2, 2)
        near["switch"].insert(1, {"camera": "camera_r0_gopro_001", "p": first["p"]})

    # ── prismas em volta das peças altas perto da pista ──────────────────────────────────────────────────
    bounds = []
    for it in items:
        half = BOUND_TYPES.get(it["type"])
        if half is None:
            continue
        m = it["m"]
        x, y, z = m[12], m[13], m[14]
        if road.nearest(x, z)[0] > BOUND_REACH:
            continue
        ax = (m[0], m[2])
        az = (m[8], m[10])
        corners = [[round(x + ax[0] * u * half + az[0] * v * half, 3), round(z + ax[1] * u * half + az[1] * v * half, 3)]
                   for u, v in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        bounds.append({"name": f"{it['type'][2:]}_{it['idnum']:04d}", "corners": corners,
                       "y0": round(y - 2.0, 3), "y1": round(y + 100.0, 3)})

    return {"cameras": cams, "zones": zones, "bounds": bounds, "grid_s": round(g % length, 2)}
