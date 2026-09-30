"""Linha de comando: listar e extrair dos arquivos do jogo, e converter `.bin` em XML.

Nada é escrito na pasta do jogo; a saída vai para o caminho passado em `-o`.
"""

from __future__ import annotations

import argparse
import os
import sys

from tools.egodata import bxml
from tools.egodata.nefs import NefsArchive

DEFAULT_GAME = "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"


def _open(game: str, archive: str) -> NefsArchive:
    path = os.path.join(game, "game", archive)
    if archive.endswith(".nefs"):
        return NefsArchive.open_nefs(path)
    return NefsArchive.open_headless(path, os.path.join(game, "dirtrally2.exe"))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python -m tools.egodata")
    parser.add_argument("--game", default=DEFAULT_GAME, help="pasta de instalação do jogo")
    sub = parser.add_subparsers(dest="command", required=True)

    ls = sub.add_parser("list", help="lista os arquivos de um pacote (ex.: game_1.dat)")
    ls.add_argument("archive")
    ls.add_argument("prefix", nargs="?", default="")

    ex = sub.add_parser("extract", help="extrai um arquivo de um pacote")
    ex.add_argument("archive")
    ex.add_argument("path", help="caminho dentro do pacote, ex.: system/screens.bin")
    ex.add_argument("-o", "--output", required=True)

    xml = sub.add_parser("xml", help="converte um .bin extraído em XML legível")
    xml.add_argument("input")
    xml.add_argument("-o", "--output")

    rt = sub.add_parser("roundtrip", help="confere se decodificar e recodificar reproduz o .bin")
    rt.add_argument("inputs", nargs="+")

    args = parser.parse_args(argv)

    if args.command == "list":
        for entry in sorted(_open(args.game, args.archive).entries(), key=lambda e: e.path):
            if entry.is_file and entry.path.startswith(args.prefix):
                print(f"{entry.size:>12}  {entry.path}")
        return 0

    if args.command == "extract":
        data = _open(args.game, args.archive).read(args.path)
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        with open(args.output, "wb") as fh:
            fh.write(data)
        print(f"{len(data)} bytes -> {args.output}")
        return 0

    if args.command == "xml":
        text = bxml.to_xml(bxml.decode(open(args.input, "rb").read()))
        if args.output:
            with open(args.output, "w", encoding="utf-8") as fh:
                fh.write(text)
        else:
            sys.stdout.write(text)
        return 0

    failed = 0
    for path in args.inputs:
        data = open(path, "rb").read()
        same = bxml.encode(bxml.decode(data)) == data
        failed += not same
        print(f"{'IDENTICO ' if same else 'DIFERENTE'}  {path}")
    return 1 if failed else 0
