#!/usr/bin/env python3
"""Extrai o mapa-múndi da tela de locations do jogo (`world_map`, 448×272, BC7, em
`frontend/bundles/b_persistent.pssg`) para `build/re/world_map.webp`: o fundo do diálogo "Informações da pista" do
viewer3d, onde um clique põe o alfinete. A imagem é do jogo; fica no build, fora do repositório.

A imagem ocupa o retângulo x 0..4,4, y -2,64..0 do nó `image!2` (BOUNDINGBOX), o mesmo espaço dos marcadores
`map_marker_<nome>`. Com o ajuste dos marcadores (docs/reverse_engineering/location_select.md), em frações da imagem:

    u = (1,96478 + 0,0129152 · lon) / 4,4
    v = (1,74648 − 0,0157565 · lat) / 2,64

    python3 scripts/research/world_map_image.py [--game <pasta>] [--out build/re/world_map.webp]
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.egodata.pssg import PSSGFile  # noqa: E402
from tools.uiview.content import decode_texture  # noqa: E402
from tools.uiview.export import Reader, _attr, _index  # noqa: E402

GAME = os.environ.get("DR2_GAME_DIR", "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0")
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "build", "re", "world_map.webp")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", default=GAME)
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()

    pssg = PSSGFile(Reader(a.game).read_any("frontend/bundles/b_persistent.pssg"))
    for node in _index(pssg.root).values():
        if node.type_name != "TEXTURE" or _attr(node, "id") != "world_map":
            continue
        w, h = int(_attr(node, "width")), int(_attr(node, "height"))
        block = next(c for c in node.children if c.type_name == "TEXTUREIMAGEBLOCK")
        blob = next(c.data for c in block.children if c.data)
        img = decode_texture(str(_attr(node, "texelFormat")), w, h, bytes(blob)).convert("RGBA")
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        img.save(a.out, lossless=True)
        print(f"{a.out}: {w}x{h}")
        return 0
    print("world_map não encontrado no b_persistent.pssg", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
