#!/usr/bin/env python3
"""Exporta uma pista e um carro para uma pasta e imprime o hash de cada arquivo.

Prova de que uma refatoração não mudou a saída dos exportadores:

    python3 scripts/dev/golden_export.py build/golden_antes  > build/antes.txt
    # ... refatora ...
    python3 scripts/dev/golden_export.py build/golden_depois > build/depois.txt
    diff build/antes.txt build/depois.txt && echo IGUAL

Usa só as linhas de comando estáveis (`python -m tools.uiview.track` e
`python -m tools.uiview.car`), então continua valendo depois de mover módulos.
Os `.json` entram com as chaves ordenadas (a ordem varia entre execuções). Só lê a pasta do jogo e só grava na pasta de saída.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def manifest(root: str) -> list[str]:
    rows = []
    for folder, _, names in os.walk(root):
        for name in names:
            if name == "model_cache.json":  # a assinatura inclui o mtime dos pacotes
                continue
            path = os.path.join(folder, name)
            with open(path, "rb") as fh:
                data = fh.read()
            if name.endswith(".json"):  # a ordem das chaves varia entre execuções (iteração de set)
                data = json.dumps(json.loads(data), sort_keys=True, separators=(",", ":")).encode()
            digest = hashlib.sha256(data).hexdigest()
            rows.append((os.path.relpath(path, root).replace(os.sep, "/"), digest))
    return [f"{digest}  {rel}" for rel, digest in sorted(rows)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", help="pasta de saída (use dentro de build/)")
    parser.add_argument("--game", default=None, help="pasta do jogo (padrão: a do tools.egodata.cli)")
    parser.add_argument("--track", default="montalegre")
    parser.add_argument("--car", default="037")
    args = parser.parse_args()
    out = os.path.abspath(args.out)
    if args.game and out.startswith(os.path.abspath(args.game)):
        parser.error("a saída não pode ficar dentro da pasta do jogo")
    extra = ["--game", args.game] if args.game else []
    env = dict(os.environ, PYTHONPATH=ROOT)
    for cmd in (["-m", "tools.uiview.track", "--tracks", args.track], ["-m", "tools.uiview.car", "--models", args.car, "--force"]):
        done = subprocess.run([sys.executable, *cmd, "-o", out, *extra], cwd=ROOT, env=env, stdout=subprocess.DEVNULL)
        if done.returncode:
            return done.returncode
    print("\n".join(manifest(out)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
