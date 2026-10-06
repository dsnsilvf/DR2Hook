"""Monta a pista sintética e grava no formato que `python -m tools.uiview.track` exporta.

Saída em `<out>/tracks/<TRACK_ID>/`:

    track.json, terrain_0.bin, objects.bin, inst_route_0.bin, tex/*.webp    formato do viewer (web e nativo)
    expected.json                                                           contagens lidas pelo unpack_geom (oráculo dos testes)
    source/textures/*.png                                                   texturas sem perda, lado em potência de dois
    source/route_0/objects.ens, ornaments.bin, trees.bin                     instâncias no formato de origem do jogo
    source/layout.json                                                      traçado amostrado (centro, tangente, altura)

O formato de origem segue o que `tools/uiview/track/export.py` lê e `track/edit.py` escreve; serve para
conferir edições e é o ponto de partida para levar a pista a um `.nefs` (veja docs/demands/synthetic_track.md).
"""

from __future__ import annotations

import json
import math
import os
import random
import struct

from tools.synthtrack import layout, meshes, textures
from tools.uiview.mesh import pack_geom, unpack_geom
from tools.uiview.track.edit import BIN_LAYOUT
from tools.uiview.track.export import pack_instances, write_index

TRACK_ID = "synthetic__dr2hook_ring"
SRC = "locations/synthetic__dr2hook_ring.nefs"
BASE = "tracks/locations/synthetic/dr2hook_ring/"
ROUTE = "route_0"
ROUTE_ALT = "route_1"   # variante: sem pórtico, arquibancadas e alambrado; chicane de cones na reta

TERRAIN_STEP = 4.0      # metros entre vértices do terreno detalhado
BLOCK = 100.0           # lado de um bloco de terreno
MARGIN = 220.0          # terreno detalhado além da caixa da pista
BACKGROUND = 2600.0     # lado do fundo grosseiro (índices de 32 bits: > 65 535 vértices)


def _texture_files(out_dir: str, seed: int) -> dict[str, str]:
    """Grava PNG (fonte) e WebP (viewer); devolve nome -> "tex/<nome>.webp"."""
    images = textures.make_all(seed)
    os.makedirs(os.path.join(out_dir, "tex"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "source", "textures"), exist_ok=True)
    files = {}
    for name, img in images.items():
        img.save(os.path.join(out_dir, "source", "textures", name + ".png"))
        img.save(os.path.join(out_dir, "tex", name + ".webp"), "WEBP", lossless=True)
        files[name] = f"tex/{name}.webp"
    return files


def _ribbon(name: str, material: str, pts: list[layout.Sample], a: float, b: float, lift: float,
            v_per_m: float, height=None, u_world: float = 0.0) -> dict:
    """Faixa entre as distâncias laterais `a` e `b` (positivo = esquerda) ao longo de `pts`.

    UV: u de 0 (lado `a`) a 1 (lado `b`), v = s * `v_per_m`; com `u_world`, as duas vêm de x/z."""
    m = meshes.mesh(name, material)
    for k, p in enumerate(pts):
        for off in (a, b):
            x, z = p.p[0] + p.n[0] * off, p.p[2] + p.n[1] * off
            y = (height(x, z) if height else p.p[1]) + lift
            m["positions"].append((x, y, z))
            if u_world:
                m["uvs"].append((x / u_world, z / u_world))
            else:
                m["uvs"].append((0.0 if off == a else 1.0, p.s * v_per_m))
        if k:
            i = 2 * k
            m["indices"] += [i - 2, i - 1, i, i - 1, i + 1, i]
    return m


def _segments(pts: list[layout.Sample], pred) -> list[list[layout.Sample]]:
    """Trechos contíguos de amostras em que `pred` vale (o circuito é fechado)."""
    n = len(pts)
    flags = [pred(p) for p in pts]
    if all(flags):
        return [pts + pts[:1]]
    start = next(i for i in range(n) if not flags[i])
    out, cur = [], []
    for k in range(1, n + 1):
        i = (start + k) % n
        if flags[i]:
            cur.append(pts[i])
        elif cur:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return [c for c in out if len(c) >= 2]


def build(out: str, seed: int = 7, log=print) -> dict:
    dest = os.path.join(out, "tracks", TRACK_ID)
    os.makedirs(dest, exist_ok=True)
    rng = random.Random(seed)
    tex = _texture_files(dest, seed)
    pts = layout.samples()
    road = layout.RoadIndex(pts)
    length = pts[-1].s + layout.STEP
    log(f"{TRACK_ID}: traçado de {length:.0f} m, {len(pts)} amostras")

    def ground(x: float, z: float) -> float:
        return layout.terrain_height(x, z, road)

    # ── terreno ────────────────────────────────────────────────────────────
    xs = [p.p[0] for p in pts]
    zs = [p.p[2] for p in pts]
    x0 = math.floor((min(xs) - MARGIN) / BLOCK) * BLOCK
    z0 = math.floor((min(zs) - MARGIN) / BLOCK) * BLOCK
    x1 = math.ceil((max(xs) + MARGIN) / BLOCK) * BLOCK
    z1 = math.ceil((max(zs) + MARGIN) / BLOCK) * BLOCK
    paddock = _paddock(pts)
    terrain = []
    cells = int(BLOCK / TERRAIN_STEP)
    for bz in range(int((z1 - z0) / BLOCK)):
        for bx in range(int((x1 - x0) / BLOCK)):
            bx0, bz0 = x0 + bx * BLOCK, z0 + bz * BLOCK
            cx, cz = bx0 + BLOCK / 2, bz0 + BLOCK / 2
            mat = "g|synth_dirt" if _inside(paddock, cx, cz) else "g|synth_grass"
            terrain.append(meshes.grid(f"terrain_{bx}_{bz}", mat, bx0, bz0, BLOCK, BLOCK, cells, cells, ground, 8.0))
    half = BACKGROUND / 2
    cxm, czm = (x0 + x1) / 2, (z0 + z1) / 2

    def far_ground(x: float, z: float) -> float:
        inside = x0 <= x <= x1 and z0 <= z <= z1
        return ground(x, z) - (0.5 if inside else 0.0)

    terrain.append(meshes.grid("background", "g|synth_grass", cxm - half, czm - half, BACKGROUND, BACKGROUND, 260, 260,
                               far_ground, 16.0, skip=lambda x, z: x0 < x < x1 and z0 < z < z1))
    # pista: asfalto em trechos de 100 m, zebras e brita nas curvas, todos colados ao terreno achatado
    chunk = int(100 / layout.STEP)
    for k in range(0, len(pts), chunk):
        part = pts[k:k + chunk + 1] if k + chunk < len(pts) else pts[k:] + pts[:1]
        terrain.append(_ribbon(f"road_{k // chunk}", "g|synth_asphalt", part, layout.ROAD_HALF, -layout.ROAD_HALF, 0.02, 1 / 10))
    corner = 1 / 140.0
    for n, seg in enumerate(_segments(pts, lambda p: abs(p.curv) > corner)):
        side = 1.0 if sum(p.curv for p in seg) > 0 else -1.0     # dentro da curva
        h = layout.ROAD_HALF
        terrain.append(_ribbon(f"curb_in_{n}", "g|synth_curb", seg, side * h, side * (h + 1.2), 0.06, 1 / 8))
        terrain.append(_ribbon(f"curb_out_{n}", "g|synth_curb", seg, -side * h, -side * (h + 1.2), 0.06, 1 / 8))
        terrain.append(_ribbon(f"gravel_{n}", "g|synth_gravel", seg, -side * (h + 1.2), -side * (h + 12.0), 0.03, 0,
                               height=ground, u_world=6.0))
    with open(os.path.join(dest, "terrain_0.bin"), "wb") as fh:
        fh.write(pack_geom(terrain))

    # ── biblioteca de tipos ────────────────────────────────────────────────
    lib: list[dict] = []
    types: dict[str, dict] = {}

    def add_type(name: str, parts: list[dict]) -> None:
        types[name] = {"node": name.split(":", 1)[1] if parts else None, "first": len(lib), "count": len(parts)}
        lib.extend(parts)

    add_type("e:synth_barrier~a", [meshes.box("barrier", "o|synth_barrier", 4.0, 0.9, 0.6, (1.0, 1.0))])
    add_type("e:synth_tyrewall", [meshes.box("tyres", "o|synth_tyre", 2.0, 1.0, 1.0, (2.0, 1.0))])
    fence = meshes.mesh("fence", "o|synth_fence")
    meshes.add_quad(fence, [(-3, 0, 0), (3, 0, 0), (3, 3, 0), (-3, 3, 0)], [(0, 1), (3, 1), (3, 0), (0, 0)])
    add_type("e:synth_fence", [fence, meshes.box("posts", "o|synth_bark", 0.1, 3.0, 0.1)])
    arch_banner = meshes.mesh("banner", "o|synth_banner")
    # cada face lida de frente: u cresce para a direita de quem olha
    meshes.add_quad(arch_banner, [(8, 5.0, -0.31), (-8, 5.0, -0.31), (-8, 7.0, -0.31), (8, 7.0, -0.31)], [(0, 1), (1, 1), (1, 0), (0, 0)])
    meshes.add_quad(arch_banner, [(-8, 5.0, 0.31), (8, 5.0, 0.31), (8, 7.0, 0.31), (-8, 7.0, 0.31)], [(0, 1), (1, 1), (1, 0), (0, 0)])
    arch = meshes.mesh("arch", "o|synth_barrier")
    meshes.add_box(arch, (-8.5, 0, -0.3), (-7.5, 7.5, 0.3))
    meshes.add_box(arch, (7.5, 0, -0.3), (8.5, 7.5, 0.3))
    meshes.add_box(arch, (-8.5, 7.0, -0.3), (8.5, 7.5, 0.3))
    add_type("e:synth_start_arch", [arch, arch_banner])
    stand = meshes.mesh("stand", "o|synth_seats")
    for k in range(6):
        meshes.add_box(stand, (-15, 0, k * 1.2), (15, 0.6 + k * 0.6, k * 1.2 + 1.2), (6.0, 1.0))
    roof = meshes.box("roof", "o|synth_barrier", 30.5, 0.3, 8.0)
    roof["positions"] = [(x, y + 6.5, z + 3.5) for x, y, z in roof["positions"]]
    add_type("e:synth_grandstand", [stand, roof])
    add_type("e:synth_spawn_marker", [])  # tipo sem malha, como os marcadores do jogo
    add_type("o:synth_cone", [meshes.cone("cone", "o|synth_barrier", 0.25, 0.7)])
    for label in ("100", "50"):
        b = meshes.mesh("board", f"o|synth_board_{label}")
        meshes.add_quad(b, [(-0.8, 1.0, 0), (0.8, 1.0, 0), (0.8, 1.8, 0), (-0.8, 1.8, 0)], [(0, 1), (1, 1), (1, 0), (0, 0)])
        add_type(f"o:synth_board_{label}", [b, meshes.box("post", "o|synth_bark", 0.08, 1.0, 0.08)])
    add_type("t:synth_pine", [meshes.cross_quads("pine", "t|synth_pine", 6.0, 12.0, 3)])
    add_type("t:synth_birch", [meshes.cross_quads("birch", "t|synth_birch", 5.0, 8.0, 2)])
    add_type("t:mnt_dist_synth_ridge", [meshes.cone("ridge", "t|synth_rock", 220.0, 160.0, 9)])
    with open(os.path.join(dest, "objects.bin"), "wb") as fh:
        fh.write(pack_geom(lib))
    type_order = list(types)
    type_index = {k: n for n, k in enumerate(type_order)}

    # ── instâncias ─────────────────────────────────────────────────────────
    items: list[dict] = []
    counters = {"e": 0, "o": 0, "t": 0}
    ens_ids: list[str] = []

    def place(type_name: str, x: float, z: float, theta: float = 0.0, scale: float = 1.0, y: float | None = None) -> None:
        kind = type_name[0]
        pos = (x, ground(x, z) if y is None else y, z)
        items.append({"type": type_name, "idnum": counters[kind], "m": meshes.yaw_matrix(theta, pos, scale)})
        if kind == "e":
            ens_ids.append(f"{type_name[2:].split('~')[0]}_{counters[kind]:04d}")
        counters[kind] += 1

    h = layout.ROAD_HALF
    for k, p in enumerate(pts):
        th = meshes.heading(*p.t)
        if abs(p.curv) > corner:
            side = -1.0 if p.curv > 0 else 1.0          # fora da curva
            if k % 2 == 0:
                off = side * (h + 13.0)
                place("e:synth_barrier~a", p.p[0] + p.n[0] * off, p.p[2] + p.n[1] * off, th)
            if abs(p.curv) > 1 / 60.0 and k % 2 == 1:
                off = side * (h + 14.5)
                place("e:synth_tyrewall", p.p[0] + p.n[0] * off, p.p[2] + p.n[1] * off, th)
        elif k % 4 == 0:
            for side in (1.0, -1.0):
                off = side * (h + 4.0)
                place("e:synth_barrier~a", p.p[0] + p.n[0] * off, p.p[2] + p.n[1] * off, th)
    # largada: pórtico, arquibancada e alambrado na reta dos boxes (início do traçado)
    p = pts[20]
    # o pórtico é largo em X: gira 90° para atravessar a pista; o Z local fica na direção da corrida
    # e a face do banner em −z fica de frente para quem chega
    place("e:synth_start_arch", p.p[0], p.p[2], meshes.heading(*p.t) + math.pi / 2, y=p.p[1])
    for k in (30, 45):
        q = pts[k]
        off = -(h + 22.0)
        place("e:synth_grandstand", q.p[0] + q.n[0] * off, q.p[2] + q.n[1] * off, meshes.heading(*q.t) + math.pi)
    for k in range(8, 70, 3):
        q = pts[k]
        off = -(h + 6.0)
        place("e:synth_fence", q.p[0] + q.n[0] * off, q.p[2] + q.n[1] * off, meshes.heading(*q.t))
    place("e:synth_spawn_marker", pts[5].p[0], pts[5].p[2])
    place("e:synth_spawn_marker", pts[8].p[0], pts[8].p[2])
    # placas de frenagem antes da curva mais fechada e cones na entrada dela
    tight = max(range(len(pts)), key=lambda i: abs(pts[i].curv))
    for dist, label in ((100, "100"), (50, "50")):
        q = pts[(tight - int(dist / layout.STEP)) % len(pts)]
        off = -math.copysign(h + 2.0, pts[tight].curv)
        place(f"o:synth_board_{label}", q.p[0] + q.n[0] * off, q.p[2] + q.n[1] * off, meshes.heading(*q.t))
    for k in range(-6, 7, 2):
        q = pts[(tight + k) % len(pts)]
        off = math.copysign(h + 0.6, q.curv)
        place("o:synth_cone", q.p[0] + q.n[0] * off, q.p[2] + q.n[1] * off)
    # árvores longe da pista (dentro do terreno detalhado) e serra distante
    tries = 0
    while counters["t"] < 420 and tries < 20000:
        tries += 1
        x, z = rng.uniform(x0 + 10, x1 - 10), rng.uniform(z0 + 10, z1 - 10)
        if road.nearest(x, z, 3)[0] < 30 or _inside(paddock, x, z, 15):
            continue
        place(rng.choice(("t:synth_pine", "t:synth_pine", "t:synth_birch")), x, z, rng.uniform(0, 2 * math.pi), rng.uniform(0.8, 1.3))
    for k in range(10):
        a = 2 * math.pi * k / 10 + rng.uniform(-0.2, 0.2)
        r = rng.uniform(1500, 1900)
        x, z = cxm + r * math.cos(a), czm + r * math.sin(a)
        place("t:mnt_dist_synth_ridge", x, z, rng.uniform(0, 6.28), rng.uniform(0.8, 1.4), y=layout.BASE_Y - 20)
    with open(os.path.join(dest, f"inst_{ROUTE}.bin"), "wb") as fh:
        fh.write(pack_instances(items, type_index))

    # rota 1: mesmo terreno (mesmo terrain_0.bin), objetos próprios, idnum próprio por arquivo de origem
    drop = {"e:synth_start_arch", "e:synth_grandstand", "e:synth_fence"}
    alt = [dict(i) for i in items if i["type"] not in drop]
    for k in range(30, 62, 4):
        q = pts[k]
        off = (2.5 if (k // 4) % 2 else -2.5)
        x, z = q.p[0] + q.n[0] * off, q.p[2] + q.n[1] * off
        alt.append({"type": "o:synth_cone", "m": meshes.yaw_matrix(0.0, (x, ground(x, z), z))})
    alt_counters = {"e": 0, "o": 0, "t": 0}
    alt_ids: list[str] = []
    for i in alt:
        kind = i["type"][0]
        i["idnum"] = alt_counters[kind]
        if kind == "e":
            alt_ids.append(f"{i['type'][2:].split('~')[0]}_{alt_counters[kind]:04d}")
        alt_counters[kind] += 1
    with open(os.path.join(dest, f"inst_{ROUTE_ALT}.bin"), "wb") as fh:
        fh.write(pack_instances(alt, type_index))

    # ── track.json ─────────────────────────────────────────────────────────
    gates = []
    for k in range(0, len(pts), 10):
        p = pts[k]
        l = (p.p[0] + p.n[0] * h, p.p[1] + 0.3, p.p[2] + p.n[1] * h)
        r = (p.p[0] - p.n[0] * h, p.p[1] + 0.3, p.p[2] - p.n[1] * h)
        gates.append({"d": round(p.s, 3), "l": [round(v, 4) for v in l], "r": [round(v, 4) for v in r]})
    ai_main = [[round(p.p[0], 4), round(p.p[1] + 0.5, 4), round(p.p[2], 4)] for p in pts[::5]]
    ai_wide = []
    for p in pts[::10]:
        off = -math.copysign(min(3.0, abs(p.curv) * 300), p.curv) if p.curv else 0.0
        ai_wide.append([round(p.p[0] + p.n[0] * off, 4), round(p.p[1] + 0.5, 4), round(p.p[2] + p.n[1] * off, 4)])
    materials = {
        "g|synth_asphalt": tex["synth_asphalt_d"], "g|synth_grass": tex["synth_grass_d"],
        "g|synth_gravel": tex["synth_gravel_d"], "g|synth_dirt": tex["synth_dirt_d"], "g|synth_curb": tex["synth_curb_d"],
        "o|synth_barrier": tex["synth_barrier_d"], "o|synth_tyre": tex["synth_tyre_d"], "o|synth_fence": tex["synth_fence_d"],
        "o|synth_banner": tex["synth_banner_d"], "o|synth_seats": tex["synth_seats_d"],
        "o|synth_board_100": tex["synth_board_100_d"], "o|synth_board_50": tex["synth_board_50_d"],
        "t|synth_pine": tex["synth_pine_d"], "t|synth_birch": tex["synth_birch_d"], "t|synth_rock": tex["synth_rock_d"],
        # o|synth_bark fica sem textura de propósito (cor fixa no viewer), como um material que a exportação não acha
    }
    route = {
        "name": ROUTE,
        "progress": {"routes": [{"id": 0, "direction": "forward", "splits": [{"type": "split", "gate": len(gates) // 2}]}],
                     "gates": gates},
        "ai": [{"name": "default", "pts": ai_main}, {"name": "wide", "pts": ai_wide}],
        "ens_ids": ens_ids,
        "terrain": {"file": "terrain_0.bin", "meshes": len(terrain), "verts": sum(len(m["positions"]) for m in terrain)},
        "instances": len(items),
    }
    route_alt = dict(route, name=ROUTE_ALT, ens_ids=alt_ids, instances=len(alt), ai=[route["ai"][0]])
    route_alt["progress"] = {"routes": [{"id": 1, "direction": "forward", "splits": [{"type": "joker", "gate": 3}]}],
                             "gates": gates}
    doc = {
        "id": TRACK_ID, "src": SRC, "base": BASE,
        "terrain": {"meshes": len(terrain), "verts": sum(len(m["positions"]) for m in terrain)},
        "routes": [route, route_alt], "types": types, "type_order": type_order, "materials": materials,
        "textures": {"wanted": len(set(materials.values())), "found": len(set(materials.values()))},
        "synthetic": {"seed": seed, "length_m": round(length, 1), "generator": "tools.synthtrack"},
    }
    with open(os.path.join(dest, "track.json"), "w", encoding="utf-8") as fh:
        json.dump(doc, fh, separators=(",", ":"))

    _write_sources(dest, items, ROUTE)
    _write_sources(dest, alt, ROUTE_ALT)
    _write_layout(dest, pts)
    expected = _expected(dest, items, type_order, materials, {ROUTE: len(items), ROUTE_ALT: len(alt)})
    write_index(out)
    log(f"{TRACK_ID}: {expected['terrain']['meshes']} malhas de terreno ({expected['terrain']['verts']} vértices), "
        f"{len(type_order)} tipos, {len(items)} + {len(alt)} instâncias (route_0 + route_1), {len(tex)} texturas -> {dest}")
    return expected


def _paddock(pts: list[layout.Sample]) -> tuple[float, float, float, float]:
    """Retângulo (x0, z0, x1, z1) de terra ao lado da reta dos boxes, por fora."""
    a, b = pts[10], pts[60]
    off = -(layout.ROAD_HALF + 30.0)
    xs = [a.p[0] + a.n[0] * off, b.p[0] + b.n[0] * off, a.p[0] + a.n[0] * (off - 60), b.p[0] + b.n[0] * (off - 60)]
    zs = [a.p[2] + a.n[1] * off, b.p[2] + b.n[1] * off, a.p[2] + a.n[1] * (off - 60), b.p[2] + b.n[1] * (off - 60)]
    return min(xs), min(zs), max(xs), max(zs)


def _inside(box, x: float, z: float, pad: float = 0.0) -> bool:
    return box[0] - pad <= x <= box[2] + pad and box[1] - pad <= z <= box[3] + pad


def _write_sources(dest: str, items: list[dict], route: str) -> None:
    """Arquivos de origem da rota: um registro por instância, na ordem do idnum, no layout que export.py lê."""
    src = os.path.join(dest, "source", route)
    os.makedirs(src, exist_ok=True)
    by_kind: dict[str, list[dict]] = {"e": [], "o": [], "t": []}
    for i in items:
        by_kind[i["type"][0]].append(i)
    lines = ['<?xml version="1.0" encoding="utf-8"?>', "<TEMPLATEENTITYS>"]
    for n, i in enumerate(sorted(by_kind["e"], key=lambda x: x["idnum"])):
        name = i["type"][2:]
        m = " ".join("%.9g" % v for v in i["m"])
        lines.append(f'\t\t<TEMPLATEENTITYINSTANCE id="{name.split("~")[0]}_{n:04d}" uri="route_objecttypes.pssg#{name}" '
                     f'instanceID="{1000 + n}" instance_tag="{5000 + n}" staticVis="1">'
                     f"<TEMPLATETRANSFORM>{m}</TEMPLATETRANSFORM></TEMPLATEENTITYINSTANCE>")
    lines.append("</TEMPLATEENTITYS>")
    data = ("\n".join(lines) + "\n").encode("utf-8")
    # sem completar: o que sobra do último bloco de 64 KiB é o espaço para cópias (o edit.py não pode mudar
    # o número de blocos), como num objects.ens do jogo
    with open(os.path.join(src, "objects.ens"), "wb") as fh:
        fh.write(data)
    for kind, (fname, start_at, count_at, stride, rot_at, pos_at) in BIN_LAYOUT.items():
        recs = sorted(by_kind[kind], key=lambda x: x["idnum"])
        start = 128
        buf = bytearray(start + stride * len(recs))
        struct.pack_into("<I", buf, start_at, start)
        struct.pack_into("<I", buf, count_at, len(recs))
        for n, i in enumerate(recs):
            m, o = i["m"], start + n * stride
            struct.pack_into("<I", buf, o, _hash(i["type"]))
            struct.pack_into("<I", buf, o + 4, n)
            struct.pack_into("<9f", buf, o + rot_at, m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10])
            struct.pack_into("<3f", buf, o + pos_at, m[12], m[13], m[14])
        with open(os.path.join(src, fname), "wb") as fh:
            fh.write(bytes(buf))


def _write_layout(dest: str, pts: list[layout.Sample]) -> None:
    with open(os.path.join(dest, "source", "layout.json"), "w", encoding="utf-8") as fh:
        json.dump({"step_m": layout.STEP, "road_half_m": layout.ROAD_HALF, "control_xz": layout.CONTROL,
                   "samples": [{"s": round(p.s, 3), "p": [round(v, 4) for v in p.p], "t": [round(v, 6) for v in p.t],
                                "curv": round(p.curv, 6)} for p in pts]}, fh, separators=(",", ":"))


def _hash(name: str) -> int:
    """FNV-1a de 32 bits do nome do tipo: só um identificador estável (o jogo usa o reference_id do XML)."""
    h = 2166136261
    for ch in name.encode("utf-8"):
        h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return h


def _expected(dest: str, items: list[dict], type_order: list[str], materials: dict, by_route: dict[str, int]) -> dict:
    with open(os.path.join(dest, "terrain_0.bin"), "rb") as fh:
        back = unpack_geom(fh.read())
    with open(os.path.join(dest, "objects.bin"), "rb") as fh:
        objs = unpack_geom(fh.read())
    expected = {
        "terrain": {"meshes": len(back), "verts": sum(len(m["positions"]) for m in back),
                    "tris": sum(len(m["indices"]) // 3 for m in back),
                    "wide": sum(1 for m in back if any(i > 65535 for i in m["indices"])),
                    "colored": sum(1 for m in back if "colors" in m)},
        "objects": {"meshes": len(objs), "verts": sum(len(m["positions"]) for m in objs),
                    "tris": sum(len(m["indices"]) // 3 for m in objs)},
        "instances": len(items),
        "instances_by_route": by_route,
        "types": len(type_order),
        "materials": len(materials),
        "first_mesh": {"name": back[0]["name"], "material": back[0]["material"], "verts": len(back[0]["positions"]),
                       "indices": len(back[0]["indices"])},
    }
    with open(os.path.join(dest, "expected.json"), "w", encoding="utf-8") as fh:
        json.dump(expected, fh, indent=1)
    return expected
