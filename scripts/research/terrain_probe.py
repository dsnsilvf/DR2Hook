"""Sonda do terreno: grava um .nefs da Montalegre com grupos de malhas do tracksplit.pssg escondidos ou movidos.

uso: terrain_probe.py <saida.nefs> --hide <material>[,<material>...] [--lift <material>=<dy>] [--keep-only <material>,...]
Esconder = jogar y para -5000 (só as posições mudam; tamanho e índices ficam).
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
    a = ap.parse_args()
    arc = NefsArchive.open_path(PACK)
    data = arc.read(PATH)
    split = PSSGFile(data)
    meshes = locate_meshes(split)
    buf = TerrainBuffer(data)
    hide = {m for m in a.hide.split(",") if m}
    keep = {m for m in a.keep_only.split(",") if m}
    lift = {k: float(v) for k, v in (x.split("=") for x in a.lift)}
    changed = 0
    for m in meshes:
        dy = lift.get(m.material)
        gone = m.material in hide or (keep and m.material not in keep)
        if dy is None and not gone:
            continue
        pos = buf.read_positions(m)
        if not pos:
            continue
        pos = [(x, -5000.0 if gone else y + dy, z) for x, y, z in pos]
        buf.write_positions(m, pos)
        changed += 1
    print(f"{changed} malhas alteradas de {len(meshes)}; blocos tocados: {len(buf.touched)}")
    replace_files(arc, {PATH: buf.patch()}, a.out)
    print("gravado", a.out)


if __name__ == "__main__":
    main()
