"""Sonda do terreno: grava um .nefs da Montalegre com grupos de malhas do tracksplit.pssg escondidos ou movidos.

uso: terrain_probe.py <saida.nefs|saida.pssg> --hide <material>[,<material>...] [--lift <material>=<dy>] [--keep-only <material>,...]
Esconder = jogar y para -5000 (só as posições mudam; tamanho e índices ficam).
Com --by-shader, os nomes são grupos de shader (`terrain_road.fx`) em vez de materiais.
Saída `.pssg`: grava só o tracksplit.pssg, para servir pela overlay da LoadProbe sem gerar `.nefs`.
"""
from __future__ import annotations

import argparse
import sys

sys.path.insert(0, ".")
from tools.egodata.nefs import NefsArchive  # noqa: E402
from tools.egodata.nefs_write import replace_files  # noqa: E402
from tools.egodata.pssg import PSSGFile  # noqa: E402
from tools.uiview.track.terrain_patch import TerrainBuffer, locate_meshes  # noqa: E402

GAME = "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"
PACK = GAME + "/locations/portugal__montalegre_rallycross.nefs"
PATH = "tracks/locations/portugal/montalegre_rallycross/tracksplit.pssg"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--hide", default="")
    ap.add_argument("--keep-only", default="")
    ap.add_argument("--lift", action="append", default=[])
    ap.add_argument("--by-shader", action="store_true", help="nomes são grupos de shader, não materiais")
    ap.add_argument("--with-batch", action="store_true",
                    help="move junto os vértices de `batchmaterial` (cópia de profundidade) com a mesma posição exata")
    a = ap.parse_args()
    arc = NefsArchive.open_path(PACK)
    data = arc.read(PATH)
    split = PSSGFile(data)
    meshes = locate_meshes(split)
    buf = TerrainBuffer(data)
    group = {n.id: (n.get("shaderGroup") or "").lstrip("#") for n in split.find_by_type("SHADERINSTANCE")}
    hide = {m for m in a.hide.split(",") if m}
    keep = {m for m in a.keep_only.split(",") if m}
    lift = {k: float(v) for k, v in (x.split("=") for x in a.lift)}
    changed = 0
    moved: dict[tuple[float, float, float], tuple[float, float, float]] = {}
    for m in meshes:
        name = group.get(m.material, "") if a.by_shader else m.material
        dy = lift.get(name)
        gone = name in hide or (keep and name not in keep)
        if dy is None and not gone:
            continue
        pos = buf.read_positions(m)
        if not pos:
            continue
        new = [(x, -5000.0 if gone else y + dy, z) for x, y, z in pos]
        moved.update(zip(pos, new))
        buf.write_positions(m, new)
        changed += 1
    if a.with_batch:
        hits = 0
        for m in meshes:
            if m.material != "batchmaterial" and not m.material.startswith("batchmaterial!"):
                continue
            pos = buf.read_positions(m)
            new = [moved.get(q, q) for q in pos]
            n = sum(1 for q, r in zip(pos, new) if q != r)
            if n:
                buf.write_positions(m, new)
                hits += n
        print(f"batchmaterial: {hits} vértices movidos junto")
    print(f"{changed} malhas alteradas de {len(meshes)}; blocos tocados: {len(buf.touched)}")
    if a.out.endswith(".pssg"):
        with open(a.out, "wb") as f:
            f.write(buf.buf)
    else:
        replace_files(arc, {PATH: buf.patch()}, a.out)
    print("gravado", a.out)


if __name__ == "__main__":
    main()
