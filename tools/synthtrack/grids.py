"""Vagas de largada da pista sintética: onde o carro nasce, no sistema do layout (metros, Y para cima).

O jogo põe os carros nas vagas do `grids.pssg` da rota: um nó por grade, com um nó filho por vaga (pose 4×4).
O plano gerado aqui fica em `routes[i]["grids"]` do `track.json`, com as mesmas grades e os mesmos nomes de vaga
da Montalegre, que é a pista hospedeira:

- `grid_time_trial_0`: a vaga `slot_0` do treino e do contra-relógio, 5 m antes da linha;
- `grid_near_reset_01`: dez vagas em fila atrás da linha (`slot_00_nr`…`slot_09_nr`);
- `grid_start_standing_01`: largada parada do rallycross, dez vagas;
- `grid_start_staggered_01`: largada escalonada, doze vagas;
- `grid_compound_5#5`: quatro vagas paradas no paddock (`slot_00_compound`…).

A Montalegre tem 12 m de asfalto na largada e põe cinco carros lado a lado; o Ring tem 10 m, então as fileiras
aqui são de três (parada) ou de dois em zigue-zague (escalonada). A ordem dos nomes segue a da Montalegre, da
frente para trás e da esquerda para a direita.

Cada vaga tem `pos` (centro do carro, `CLEAR` m acima do asfalto), `fwd` (unitário, para onde o carro aponta),
`s` e `lat` no referencial da pista e `size` (largura e comprimento da caixa da vaga, do `grids.pssg`). As
`markers` são nós de apoio sem carro (`car_grid_spline_*`, `car_near_reset_spline_*`).
"""

from __future__ import annotations

import math
from typing import Callable

from . import layout
from .cameras import Frame

CLEAR = 0.5         # centro da vaga acima do asfalto (na Montalegre: 0,40 a 0,52 m)
WIDE = (2.8, 5.5)   # caixa das vagas do contra-relógio e do reset (largura, comprimento)
NARROW = (2.5, 4.5)  # caixa das vagas das largadas de rallycross
# ordem das vagas da Montalegre, da frente para trás e da esquerda para a direita
STANDING = ("slot_05", "slot_06", "slot_07", "slot_08", "slot_09", "slot_00", "slot_01", "slot_02", "slot_03", "slot_04")
STAGGERED = ("slot_006", "slot_007", "slot_000", "slot_008", "slot_001", "slot_009",
             "slot_002", "slot_010", "slot_003", "slot_011", "slot_004", "slot_005")
STANDING_FRONT = -55.5   # 1ª fileira da largada parada (como na Montalegre)
STANDING_ROW = 6.5
STANDING_LAT = (3.3, 0.0, -3.3)
STAGGERED_FRONT = -57.3
STAGGERED_STEP = 3.0     # cada vaga 3 m atrás da anterior, alternando de lado
STAGGERED_LAT = 2.3
PADDOCK_S = (39.0, 61.0, 83.0, 105.0)  # entre as tendas das equipes
PADDOCK_LAT = -(layout.ROAD_HALF + 42.0)

ROLES = {
    "grid_time_trial_0": "contra-relógio / treino",
    "grid_near_reset_01": "reset perto da largada",
    "grid_start_standing_01": "largada parada",
    "grid_start_staggered_01": "largada escalonada",
    "grid_compound_5#5": "paddock",
}


def _r(v: list[float]) -> list[float]:
    return [round(x, 4) for x in v]


def plan(pts: list[layout.Sample], ground: Callable[[float, float], float]) -> list[dict]:
    fr = Frame(pts, ground)
    length = fr.length

    def pose(name: str, s: float, lat: float, size=WIDE, on_road: bool = True, heading: float = 0.0) -> dict:
        """Vaga em (s, lat), virada para a frente da pista girada de `heading` graus para a esquerda."""
        a = fr.at(s - 1.0, lat, on_road=on_road)
        b = fr.at(s + 1.0, lat, on_road=on_road)
        f = [b[k] - a[k] for k in range(3)]
        if heading:
            c, sn = math.cos(math.radians(heading)), math.sin(math.radians(heading))
            _, t, nrm = fr.sample(s)
            fx, fz = t[0] * c + nrm[0] * sn, t[1] * c + nrm[1] * sn
            f = [fx, 0.0, fz]
        fl = math.sqrt(sum(x * x for x in f))
        pos = fr.at(s, lat, CLEAR, on_road=on_road)
        return {"name": name, "pos": pos, "fwd": _r([x / fl for x in f]), "s": round(s % length, 2),
                "lat": round(lat, 2), "size": list(size)}

    def group(name: str, s: float, slots: list[dict], markers: list[dict] = ()) -> dict:
        g = pose(name, s, 0.0)
        return {"name": name, "role": ROLES[name], "pos": g["pos"], "fwd": g["fwd"], "s": g["s"],
                "slots": slots, "markers": [{k: m[k] for k in ("name", "pos", "fwd")} for m in markers]}

    grids = [
        group("grid_time_trial_0", 0.0, [pose("slot_0", -5.0, 0.0)],
              [pose("car_grid_spline_time_trial_0", 0.0, 0.0)]),
        group("grid_near_reset_01", -1.0, [pose(f"slot_{k:02d}_nr", -6.0 - 8.0 * k, 0.0) for k in range(10)],
              [pose("car_near_reset_spline_01", -42.0, 0.0)]),
        group("grid_start_standing_01", STANDING_FRONT + 4.0,
              [pose(n, STANDING_FRONT - STANDING_ROW * (k // 3), STANDING_LAT[k % 3], NARROW)
               for k, n in enumerate(STANDING)]),
        group("grid_start_staggered_01", STAGGERED_FRONT - 7.0,
              [pose(n, STAGGERED_FRONT - STAGGERED_STEP * k, STAGGERED_LAT if k % 2 == 0 else -STAGGERED_LAT, NARROW)
               for k, n in enumerate(STAGGERED)]),
    ]
    # paddock: carros parados entre as tendas, de frente para a pista (girados 90° para a esquerda)
    paddock = [pose(f"slot_{k:02d}_compound", s, PADDOCK_LAT, (1.0, 2.0), on_road=False, heading=90.0)
               for k, s in enumerate(PADDOCK_S)]
    g = paddock[0]
    grids.append({"name": "grid_compound_5#5", "role": ROLES["grid_compound_5#5"], "pos": g["pos"], "fwd": g["fwd"],
                  "s": g["s"], "slots": paddock, "markers": []})
    return grids
