#!/usr/bin/env python3
"""Monta um edits.json mínimo para provar que o jogo aceita um .nefs gravado por nós.

Sobe `--dy` metros todas as instâncias de um tipo (por padrão o pórtico de largada do Montalegre,
`mnt_startgantry_a`), o que se vê de longe e deixa o resto da pista igual. Só lê a pista exportada
(`build/uiview/tracks/<id>`); depois use `python -m tools.uiview.track.edit <edits.json> -o <saida.nefs>`.

    python3 scripts/research/make_edit_test.py --track build/uiview/tracks/portugal__montalegre_rallycross \\
        --type startgantry --dy 40 -o build/redirect/montalegre_edit.edits.json
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys


def read_instances(path: str, types: list[str]) -> list[dict]:
    data = open(path, "rb").read()
    if data[:4] != b"DR2I":
        raise ValueError(f"{path}: sem a magia DR2I")
    n = struct.unpack_from("<I", data, 4)[0]
    off = 8
    tidx = struct.unpack_from(f"<{n}H", data, off)
    off += n * 2
    off += -off % 4
    ids = struct.unpack_from(f"<{n}I", data, off)
    off += n * 4
    floats = struct.unpack_from(f"<{n * 12}f", data, off)
    return [{"type": types[tidx[i]], "idnum": ids[i], "m": list(floats[i * 12 : i * 12 + 12])} for i in range(n)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--track", required=True, help="pasta exportada (com track.json)")
    ap.add_argument("--route", default="route_0")
    ap.add_argument("--type", default="startgantry", help="trecho do nome do tipo")
    ap.add_argument("--dy", type=float, default=40.0, help="metros para cima")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    meta = json.load(open(os.path.join(args.track, "track.json"), encoding="utf-8"))
    inst = read_instances(os.path.join(args.track, f"inst_{args.route}.bin"), meta["type_order"])
    edits = []
    for it in inst:
        if args.type not in it["type"]:
            continue
        m = list(it["m"])
        m[10] += args.dy  # posição = índices 9..11 (x, y, z) da matriz de 12 floats; y é o 10
        edits.append(
            {
                "route": args.route,
                "kind": it["type"][0],
                "type": it["type"].split(":", 1)[1],
                "index": it["idnum"],
                "deleted": False,
                "m": m,
                "m0": it["m"],
            }
        )
    if not edits:
        print(f"nenhuma instância com '{args.type}' em {args.route}", file=sys.stderr)
        return 1
    doc = {"format": "dr2-track-edits", "version": 1, "track": meta["id"], "src": meta["src"], "edits": edits}
    with open(args.output, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=1)
    print(f"{len(edits)} edição(ões) em {args.output}: " + ", ".join(sorted({e['type'] for e in edits})))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
