#!/usr/bin/env python3
"""Inventário de uma pista nova: o que é próprio e o que ainda vem da hospedeira (Montalegre).

Uma pista do DR2Hook só traz os arquivos que diferem da hospedeira; o jogo lê o resto da hospedeira pelo
alias (`track_package.py`). Um emprestado que guarda posições no mundo fica no lugar da Montalegre, e numa pista
com outro traçado isso aparece no lugar errado. Cada item da lista junta os arquivos de uma coisa que o jogador
vê ou que o jogo usa, com a gravidade de ficar emprestado:

- `obrigatorio`: a pista não funciona direito sem a versão própria (carro fora da pista, IA perdida, crash).
- `posicional`: funciona, mas o emprestado fica no lugar da Montalegre (enfeite, som ou câmera no lugar errado).
- `generico`: serve para qualquer pista (luz do dia, clima, materiais, listas de modelos).

Também confere a área de serviço: o carro para na grade `grid_compound_5#5`, e as câmeras `service_*` do
`replay_camera_config.xml` têm de olhar para lá. Detalhes no track_loading.md §13.

    python3 scripts/research/track_inventory.py [--track-dir <pasta>] [--json] [--out arquivo.json]

Com `--out`, grava o JSON no arquivo e termina com `@done` (o editor, janela Configurações da pista, lê de lá).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from ring_grids import get_matrix  # noqa: E402
from track_package import host_files  # noqa: E402
from ui_lang import t  # noqa: E402
from tools.egodata import bxml  # noqa: E402
from tools.egodata.pssg import PSSGFile  # noqa: E402

GAME = os.environ.get("DR2_GAME_DIR", "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0")
R = "route_0/"

# (id, rótulo, gravidade, arquivos, o que acontece se ficar emprestado)
ITEMS = [
    ("terrain", "Chão e colisão", "obrigatorio", ["tracksplit.pssg", R + "track.jpk"],
     "o carro anda sobre o chão da Montalegre"),
    ("vis", "Visibilidade por trecho", "obrigatorio", [R + "track.vis"],
     "partes da pista somem ou piscam"),
    ("objects", "Objetos (objects.ens)", "obrigatorio", ["objects.pssg", "objectstextures.pssg", R + "objects.ens"],
     "aparecem os objetos da Montalegre"),
    ("ornaments", "Ornamentos", "posicional", ["ornaments_references.xml", R + "ornaments.bin"],
     "aparecem os ornamentos da Montalegre"),
    ("route", "Traçado da corrida (IA e progresso)", "obrigatorio",
     [R + "ai_track.xml", R + "ai_vehicle_track.xml", R + "ai_track_markers.xml", R + "progress_track.xml",
      R + "vehicle_track_progress_data.xml"],
     "a IA e a contagem de voltas seguem o traçado da Montalegre"),
    ("grids", "Vagas de largada e da oficina", "obrigatorio", [R + "grids.pssg"],
     "o carro nasce fora da pista (o jogo pode travar com NaN)"),
    ("reset", "Linhas de volta à pista", "obrigatorio", [R + "resetlines.cqtc"],
     "o reset põe o carro no lugar errado"),
    ("replay", "Câmeras do replay", "posicional", [R + "replay_camera_config.xml", R + "cameralines.cqtc"],
     "o replay mostra lugares vazios"),
    ("spare", "Estepes da oficina", "posicional", [R + "triggers_game_object.xml"],
     "os estepes ficam no parque da Montalegre"),
    ("pfx", "Poeira e efeitos do chão", "posicional", [R + "pfx.xml"],
     "a poeira usa as regiões da Montalegre"),
    ("trees", "Árvores", "posicional",
     [R + "trees.bin", "trees_references.xml", "trees.pssg", "treestextures.pssg"],
     "aparecem as árvores da Montalegre"),
    ("crowd", "Público e fiscais", "posicional", [R + "crowd_standing2.bin", "crowds_references.xml"],
     "o público fica onde era a Montalegre"),
    ("lights", "Luzes da oficina e do pódio", "posicional", [R + "light_placement.xml"],
     "a oficina fica sem os holofotes e as luzes ficam no parque da Montalegre"),
    ("sound", "Eco e som do lugar", "posicional",
     ["audio_markup.xml", "audio_reflections.xml", R + "audio_triggers.xml", "ambience_triggers.xml"],
     "ecos de prédios que não existem"),
    ("fx", "Fogos e efeitos de fim de corrida", "posicional",
     [R + "triggers_effects.xml", R + "triggers_animated_objects.xml", R + "ornament_path_anims.pssg"],
     "fogos no lugar da Montalegre"),
    ("podium", "Pódio", "posicional", [R + "drivable_entities.jpk"],
     "plataforma do pódio no lugar da Montalegre"),
    ("grass", "Grama e vegetação rasteira", "posicional",
     ["ground_cover.pssg", "ground_cover.xml", "ground_cover_config.xml", "grass.grs"],
     "grama da Montalegre sobre o chão novo"),
    ("water", "Água", "posicional", ["niwater.pssg", "niwater.xml"], "lago da Montalegre"),
    ("height", "Mapa de altura", "posicional", ["landscape.heightfield"], "altura do terreno da Montalegre"),
    ("patchup", "Remendos do cenário", "posicional", ["patchup_ot.pssg", R + "route_patchup_ot.pssg"],
     "remendos de geometria da Montalegre"),
    ("generic", "Clima, luz do dia, materiais e listas", "generico",
     ["weather.xml", "waterdefs.xml", "basewind2.xml", "mud.xml", "mud_materials.xml", "lod_overrides.xml",
      "lightmap_list.xml", "pfx_material_override.xml", "audio_dataset.xml", "audio_props.xml",
      R + "start_lights.xml", R + "organism_anim_dataset.xml", R + "pfx_atmospheric.xml",
      R + "tree_attributes.xml", R + "ornament_attributes.xml", R + "route_objecttypes.pssg",
      R + "route_objectstextures.pssg", R + "light_textures.pssg"]
     + [f"lighting_{t}_{w}_00.xml" for t in ("midday", "sunset") for w in ("dry", "wet", "rain")],
     "nada: serve para qualquer pista"),
]

# o mesmo em inglês (o JSON leva os dois: o editor escolhe pelo idioma dele)
ENGLISH = {
    "terrain": ("Ground and collision", "the car drives on the Montalegre ground"),
    "vis": ("Visibility by section", "parts of the track vanish or flicker"),
    "objects": ("Objects (objects.ens)", "the Montalegre objects show up"),
    "ornaments": ("Ornaments", "the Montalegre ornaments show up"),
    "route": ("Racing line (AI and progress)", "the AI and the lap count follow the Montalegre line"),
    "grids": ("Start and service area slots", "the car spawns off the track (the game may hang on NaN)"),
    "reset": ("Lines back to the track", "the reset puts the car in the wrong place"),
    "replay": ("Replay cameras", "the replay shows empty places"),
    "spare": ("Service area spare tyres", "the spare tyres stay in the Montalegre paddock"),
    "pfx": ("Dust and ground effects", "the dust uses the Montalegre regions"),
    "trees": ("Trees", "the Montalegre trees show up"),
    "crowd": ("Crowd and marshals", "the crowd stands where Montalegre was"),
    "lights": ("Service area and podium lights", "the service area has no floodlights and the lights stay in the Montalegre paddock"),
    "sound": ("Echo and sound of the place", "echoes from buildings that do not exist"),
    "fx": ("Fireworks and end of race effects", "fireworks where Montalegre was"),
    "podium": ("Podium", "podium platform where Montalegre was"),
    "grass": ("Grass and low vegetation", "Montalegre grass over the new ground"),
    "water": ("Water", "Montalegre lake"),
    "height": ("Height map", "Montalegre terrain height"),
    "patchup": ("Scenery patches", "Montalegre geometry patches"),
    "generic": ("Weather, daylight, materials and lists", "nothing: works for any track"),
}
assert set(ENGLISH) == {it[0] for it in ITEMS}

SERVICE_SLOT = "grid_compound_5#5/slot_00_compound"
SERVICE_RADIUS = 40.0  # metros entre o carro na oficina e as câmeras e luzes dela


def slot_positions(data: bytes) -> dict[str, np.ndarray]:
    """Posição no mundo de cada vaga do grids.pssg, por `grade/vaga`."""
    f = PSSGFile(data)
    root = f.find_by_type("ROOTNODE")[0]
    rw = get_matrix(root)
    out = {}
    for g in root.children:
        if g.type_name != "NODE":
            continue
        gw = get_matrix(g) @ rw
        for s in g.children:
            if s.type_name == "NODE":
                out[f"{g.nickname}/{s.nickname}"] = (get_matrix(s) @ gw)[3, :3]
    return out


def light_positions(text: str) -> list[np.ndarray]:
    return [np.array([float(x) for x in w.split()])[12:15] for w in re.findall(r'world="([^"]*)"', text)]


def service_cameras(data: bytes) -> dict[str, np.ndarray]:
    """Posição das câmeras `service_*` do replay_camera_config.xml."""
    xml = bxml.to_xml(bxml.decode(data))
    out = {}
    for m in re.finditer(r'<Camera [^>]*ident="(service[^"]*)"', xml):
        p = re.search(r'name="Position" type="vector3" x="([^"]+)" y="([^"]+)" z="([^"]+)"', xml[m.end():])
        if p:
            out[m.group(1)] = np.array([float(v) for v in p.groups()])
    return out


def inventory(track_dir: str, host: dict[str, bytes]) -> dict:
    def read(rel: str) -> bytes | None:
        path = os.path.join(track_dir, rel)
        if os.path.isfile(path):
            return open(path, "rb").read()
        return host.get(rel)

    def own(rel: str) -> bool:
        path = os.path.join(track_dir, rel)
        return os.path.isfile(path) and open(path, "rb").read() != host.get(rel)

    items = []
    listed = set()
    for ident, label, level, files, effect in ITEMS:
        listed |= set(files)
        mine = [f for f in files if own(f)]
        state = "proprio" if len(mine) == len(files) else "parcial" if mine else "emprestado"
        label_en, effect_en = ENGLISH[ident]
        items.append({"id": ident, "label": label, "label_en": label_en, "level": level, "state": state,
                      "effect": effect, "effect_en": effect_en, "own": mine, "borrowed": [f for f in files if f not in mine]})
    extra = sorted(set(host) - listed - {""})

    service = {"ok": None}
    grids, cams, lights = read(R + "grids.pssg"), read(R + "replay_camera_config.xml"), read(R + "light_placement.xml")
    if grids and cams:
        car = slot_positions(grids)[SERVICE_SLOT]
        far = {name: float(np.linalg.norm(p - car)) for name, p in service_cameras(cams).items()}
        light = max((float(np.linalg.norm(p - car)) for p in light_positions(lights.decode())), default=0.0) \
            if lights else 0.0
        service = {"ok": bool(far) and max(far.values()) <= SERVICE_RADIUS and light <= SERVICE_RADIUS,
                   "car": car.round(1).tolist(), "cameras": {k: round(v, 1) for k, v in far.items()},
                   "lights": round(light, 1)}
    return {"items": items, "unlisted": extra, "service": service}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--track-dir", default="captures/overlay/tracks/locations/portugal/dr2hook_ring")
    ap.add_argument("--host-location", default="portugal")
    ap.add_argument("--host-track", default="montalegre_rallycross")
    ap.add_argument("--game", default=GAME)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--out", help="grava o JSON aqui (para o editor)")
    a = ap.parse_args()

    inv = inventory(a.track_dir, host_files(a.game, a.host_location, a.host_track))
    inv["track_dir"], inv["host"] = a.track_dir, f"{a.host_location}/{a.host_track}"
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, "w") as fh:
            json.dump(inv, fh, ensure_ascii=False, indent=1)
        done = sum(it["state"] == "proprio" for it in inv["items"])
        print("@done " + t(f"{done} de {len(inv['items'])} itens próprios", f"{done} of {len(inv['items'])} items are the track's own"))
        return
    if a.json:
        print(json.dumps(inv, ensure_ascii=False, indent=1))
        return
    mark = {"proprio": "OK  ", "parcial": "PARC", "emprestado": "EMPR"}
    for it in inv["items"]:
        print(f"{mark[it['state']]} {it['level']:11s} {it['label']}")
        if it["state"] != "proprio" and it["level"] != "generico":
            print(f"     emprestado: {', '.join(it['borrowed'])} → {it['effect']}")
    s = inv["service"]
    if s["ok"] is not None:
        print(f"{'OK  ' if s['ok'] else 'ERRO'} oficina: carro em {s['car']}; câmeras a "
              + ", ".join(f"{k} {v} m" for k, v in s["cameras"].items()) + f"; luz mais longe a {s['lights']} m")
    if inv["unlisted"]:
        print("fora da lista:", ", ".join(inv["unlisted"]))


if __name__ == "__main__":
    main()
