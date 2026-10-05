"""python -m tools.uiview [--game PASTA] [-o SAIDA] [--models 037] [--all-models] [--open]"""

from __future__ import annotations

import argparse
import webbrowser

from tools.egodata.cli import DEFAULT_GAME
from tools.uiview.export import run


def main() -> int:
    parser = argparse.ArgumentParser(prog="python -m tools.uiview",
                                     description="Exporta as telas do jogo e gera um visualizador HTML.")
    parser.add_argument("--game", default=DEFAULT_GAME, help="pasta de instalação do jogo")
    parser.add_argument("-o", "--output", default="build/uiview", help="pasta de saída (padrão: build/uiview)")
    parser.add_argument("--force-textures", action="store_true", help="regrava as texturas já exportadas")
    parser.add_argument("--scene-images-only", action="store_true",
                        help="só as imagens usadas pelas cenas (mais rápido, sem a galeria completa)")
    parser.add_argument("--models", default="",
                        help="só estes modelos (trechos do pacote ou do arquivo, separados por vírgula). "
                             "Sem isto, exporta a malha de todos os modelos principais de até 40 MB")
    parser.add_argument("--all-models", action="store_true",
                        help="malha de todos os modelos até 120 MB (carro, personagem, local, prop, "
                             "interior, LOD), um pacote por vez. Acima de 120 MB fica só no índice. "
                             "Reaproveita imagem e malha já geradas")
    parser.add_argument("--open", action="store_true", help="abre o visualizador no navegador ao terminar")
    args = parser.parse_args()
    if args.all_models and args.models.strip():
        parser.error("--all-models não combina com --models")
    tokens = [part.strip() for part in args.models.split(",") if part.strip()] or None
    index = run(args.game, args.output, args.force_textures, args.scene_images_only, tokens, args.all_models)
    if args.open:
        webbrowser.open("file://" + __import__("os").path.abspath(index))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
