"""python -m tools.synthtrack [-o build/uiview] [--seed 7]

Gera a pista sintética em `<saída>/tracks/synthetic__dr2hook_ring/` e atualiza `<saída>/data/tracks.js`.
Não lê nem grava a pasta do jogo.
"""

from __future__ import annotations

import argparse
import sys


def main(argv: list[str] | None = None) -> int:
    from tools.synthtrack.build import build

    parser = argparse.ArgumentParser(prog="python -m tools.synthtrack", description="Gera a pista sintética do DR2Hook")
    parser.add_argument("-o", "--output", default="build/uiview", help="pasta de saída (padrão: build/uiview)")
    parser.add_argument("--seed", type=int, default=7, help="semente das texturas e das árvores (padrão: 7)")
    args = parser.parse_args(argv)
    build(args.output, args.seed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
