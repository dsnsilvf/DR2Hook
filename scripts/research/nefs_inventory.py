#!/usr/bin/env python3
"""Lista os maiores arquivos de um pacote `locations/*.nefs` e a folga dos que a edição de pista mexe.

Só lê o pacote (a pasta do jogo nunca é alterada). A folga é o que sobra no último bloco de 64 KiB:
é o quanto o arquivo pode crescer sem mudar o número de blocos, que `replace_files` exige.

    python3 scripts/research/nefs_inventory.py "<jogo>/locations/portugal__montalegre_rallycross.nefs" [--top 10]
"""

from __future__ import annotations

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.egodata.nefs import BLOCK_SIZE, NefsArchive  # noqa: E402

EDITED = re.compile(
    r"(objects\.ens|ornaments\.bin|trees\.bin|ai_track\.xml|progress_track\.xml|route_objecttypes\.pssg"
    r"|/objects\.pssg|/trees\.pssg|track\.jpk|tracksplit\.pssg)$"
)


def blocks(size: int) -> int:
    return (size + BLOCK_SIZE - 1) // BLOCK_SIZE


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("nefs")
    ap.add_argument("--top", type=int, default=10)
    args = ap.parse_args()
    arc = NefsArchive.open_path(args.nefs)
    files = [e for e in arc.entries() if e.is_file]
    print(f"{os.path.basename(args.nefs)}: {len(files)} arquivos, cabeçalho de {len(arc.header)} bytes")
    print("\nMaiores:")
    for e in sorted(files, key=lambda e: -e.size)[: args.top]:
        print(f"  {e.size:>13,d} {blocks(e.size):>6d} blocos  {e.path}")
    print("\nArquivos que a edição de pista toca (folga = bytes livres no último bloco):")
    for e in sorted(files, key=lambda e: e.path):
        if EDITED.search(e.path):
            print(f"  {e.size:>13,d} {blocks(e.size):>6d} blocos  folga {-e.size % BLOCK_SIZE:>6d} B  {e.path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
