"""python -m tools.uiview.car --models 037 -o build/uiview

Exporta só a malha e as texturas dos modelos pedidos (sem as telas do jogo).
"""

from __future__ import annotations

import argparse
import os


def main(argv: list[str] | None = None) -> int:
    from tools.egodata.cli import DEFAULT_GAME
    from tools.uiview.car.models import export_models

    parser = argparse.ArgumentParser(prog="python -m tools.uiview.car", description="Exporta modelos de carro para o Car Model Explorer")
    parser.add_argument("--game", default=DEFAULT_GAME, help="pasta de instalação do jogo")
    parser.add_argument("-o", "--output", default="build/uiview", help="pasta de saída (padrão: build/uiview)")
    parser.add_argument("--models", required=True, help="trechos do pacote ou do arquivo, separados por vírgula (ex.: 037)")
    parser.add_argument("--force", action="store_true", help="regrava o que já foi exportado")
    args = parser.parse_args(argv)
    tokens = [t.strip() for t in args.models.split(",") if t.strip()]
    os.makedirs(os.path.join(args.output, "data"), exist_ok=True)
    _, data = export_models(args.game, args.output, args.force, print, tokens)
    print(f"{len(data['models'])} modelos no índice")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
