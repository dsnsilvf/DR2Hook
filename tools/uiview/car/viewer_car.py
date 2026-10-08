"""Carro do jogo para o viewer3d: a malha do highLOD em DR2M (`car.bin`) e as texturas de cor.

O viewer3d desenha o carro do jogo ao vivo (LiveLink) com este modelo no lugar da caixa:

    python3 -m tools.uiview.car.viewer_car fr5            # → build/viewer3d/cars/fr5/
    python3 -m tools.uiview.car.viewer_car 037 --surface gravel

Saída em `<saída>/<id>/`:

- `car.bin`: DR2M (`mesh.pack_geom`), uma malha por material, já no espaço do carro (metros, y para
  cima, z para a frente, rodas em y = 0, que é também a origem física do jogo). Ficam só as rodas do
  piso pedido; estepes e o disco borrado saem.
- `car.json`: `{"id", "surface", "materials": {material: "img/…webp"}}`, no formato do `materials`
  do track.json. Os materiais levam o prefixo `c|`: o shader da pista não recorta o alfa de material
  que começa com `g` (chão), e `glass_exterior`/`grilles`/`gravel_*` começariam.

O PSSG do carro não liga textura a material (o jogo faz isso em tempo de execução), então a escolha é
a mesma heurística do Car Model Explorer (`guessTexture` em web/js/content.js).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from typing import Any

from tools.egodata.nefs import NefsArchive
from tools.egodata.pssg import PSSGFile
from tools.uiview.car.models import HARD_CAP, _files, _texture_paths, surface_suffix
from tools.uiview.content import export_pssg_images
from tools.uiview.mesh import extract_meshes, pack_geom

DEFAULT_GAME = "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"
SURFACES = {"tarmac": "tm", "gravel": "gr", "snow": "sn"}
PREFIX = "c|"
# palavra no material → palavras no nome da textura (a ordem importa: "light" antes de "body")
TABLE = [
    ("glass", ("glass",)), ("light", ("light", "lamp")), ("cabin", ("cabin", "interior", "cockpit")),
    ("carbon", ("carbon",)), ("body", ("body", "paint", "livery")), ("caliper", ("caliper",)),
    ("disc", ("disc", "brake")), ("grille", ("grill",)), ("suspension", ("susp",)),
]


def keep_material(material: str, surface: str) -> bool:
    """Fora: estepes, o disco borrado (só aparece em velocidade) e as rodas dos outros pisos."""
    n = material.lower()
    if n.startswith("spare_") or "disc_blur" in n:
        return False
    for name in SURFACES:
        if n.startswith(name + "_") and name != surface:
            return False
    return True


def pick_texture(material: str, assets: list[dict[str, Any]], surface: str) -> dict[str, Any] | None:
    n = material.lower()
    diffs = [a for a in assets if re.search(r"_d(\.|_|$)", a["n"], re.I)]
    if "tread" in n or "wheel" in n:
        wheels = [a for a in diffs if "wheel_d" in a["n"].lower()]
        token = SURFACES[surface]
        hit = next((a for a in wheels if re.search(f"_{token}(\\.|_|$)", a["n"], re.I)), None)
        hit = hit or next((a for a in wheels if not re.search(r"_(gr|sn|wt|tm)(\.|_|$)", a["n"], re.I)), None)
        if hit or wheels:
            return hit or wheels[0]
    pool = diffs or assets
    for key, words in TABLE:
        if key in n:
            hit = next((a for a in pool if any(w in a["n"].lower() for w in words)), None)
            if hit:
                return hit
    return next((a for a in pool if re.search("body|paint|main", a["n"], re.I)), None)


def export_car(game: str, car_id: str, out: str, surface: str, log=print) -> str:
    arc = NefsArchive.open_path(os.path.join(game, "cars", f"{car_id}.nefs"))
    files = dict(_files(arc))
    model = f"cars/models/{car_id}/{car_id}_highLOD.pssg"
    if model not in files:
        raise SystemExit(f"{model} não está em cars/{car_id}.nefs")
    root = PSSGFile(arc.read(model)).root
    meshes = [m for m in extract_meshes(root) if keep_material(str(m["material"]), surface)]
    del root
    dest = os.path.join(out, car_id)
    os.makedirs(dest, exist_ok=True)

    token = SURFACES[surface]
    assets: list[dict[str, Any]] = []
    for path in _texture_paths(model, files, HARD_CAP):
        suffix = surface_suffix(path)
        if suffix and suffix != token:
            continue  # só a roda do piso pedido
        assets += export_pssg_images(arc.read(path), f"car_{car_id}", dest, False, log, suffix)

    materials: dict[str, str] = {}
    for m in meshes:
        tex = pick_texture(str(m["material"]), assets, surface)
        m["material"] = PREFIX + str(m["material"])
        if tex:
            materials[m["material"]] = tex["p"]
    with open(os.path.join(dest, "car.bin"), "wb") as fh:
        fh.write(pack_geom(meshes))
    with open(os.path.join(dest, "car.json"), "w", encoding="utf-8") as fh:
        json.dump({"id": car_id, "surface": surface, "materials": materials}, fh, indent=1)
    verts = sum(len(m["positions"]) for m in meshes)
    log(f"{car_id}: {len(meshes)} malhas, {verts} vértices, {len(materials)} com textura → {dest}")
    return dest


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("car", help="id do carro (nome do .nefs em cars/, ex.: fr5)")
    ap.add_argument("-o", "--output", default="build/viewer3d/cars", help="pasta de saída")
    ap.add_argument("--surface", choices=sorted(SURFACES), default="tarmac", help="rodas de qual piso")
    ap.add_argument("--game", default=DEFAULT_GAME, help="pasta de instalação do jogo")
    a = ap.parse_args()
    export_car(a.game, a.car, a.output, a.surface)


if __name__ == "__main__":
    sys.exit(main())
