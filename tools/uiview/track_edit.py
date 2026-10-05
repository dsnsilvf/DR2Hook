"""Aplica as edições do Track Explorer (`<pista>.edits.json`) num pacote `locations/*.nefs`.

O viewer só guarda, para cada objeto mexido, a nova matriz (3×3 + posição) ou a marca de apagado. Aqui
cada edição vira uma mudança no arquivo de origem do objeto:

    objects.ens     texto XML; a `TEMPLATETRANSFORM` da instância muda, e uma instância apagada sai do texto
                    (o arquivo é completado com espaços, para não mudar de tamanho)
    ornaments.bin   registro de 212 bytes: matriz em +16, posição em +52
    trees.bin       registro de 96 bytes: matriz em +8, posição em +44

Um objeto apagado em `.bin` não pode sair do arquivo (a contagem e os offsets são fixos): a matriz fica
zerada e a posição vai para `HIDDEN_Y`, o que o deixa invisível.

O pacote novo é gravado em outro caminho; a pasta do jogo nunca é alterada aqui.
"""

from __future__ import annotations

import json
import re
import struct
import sys
from typing import Any

from tools.egodata.nefs import BLOCK_SIZE
from tools.egodata.nefs_write import replace_files
from tools.uiview import track

HIDDEN_Y = -10000.0
_INSTANCE = re.compile(r"<TEMPLATEENTITYINSTANCE\b[^>]*>.*?</TEMPLATEENTITYINSTANCE>", re.DOTALL)
_TRANSFORM = re.compile(r"(<TEMPLATETRANSFORM>)([^<]*)(</TEMPLATETRANSFORM>)")
_URI = re.compile(r'\buri="([^"]*)"')

# (início dos registros em +, tamanho, matriz em +, posição em +) de cada formato binário
BIN_LAYOUT = {
    "o": ("ornaments.bin", 80, 88, 212, 16, 52),
    "t": ("trees.bin", 60, 48, 96, 8, 44),
}


def _fmt(v: float) -> str:
    return "%.9g" % v


def ens_spans(text: str) -> list[tuple[int, int]]:
    """Faixa de texto de cada instância que `track.instances` lista, na mesma ordem."""
    out = []
    for m in _INSTANCE.finditer(text):
        chunk = m.group(0)
        uri = _URI.search(chunk)
        tr = _TRANSFORM.search(chunk)
        if not uri or "#" not in uri.group(1) or not tr or len(tr.group(2).split()) != 16:
            continue
        out.append((m.start(), m.end()))
    return out


def edit_ens(data: bytes, edits: list[dict[str, Any]]) -> bytes:
    text = data.decode("utf-8")
    spans = ens_spans(text)
    by_index = {e["index"]: e for e in edits}
    bad = [i for i in by_index if not 0 <= i < len(spans)]
    if bad:
        raise ValueError(f"objects.ens tem {len(spans)} instâncias; índice fora do arquivo: {bad[:5]}")
    out: list[str] = []
    pos = 0
    for n, (a, b) in enumerate(spans):
        e = by_index.get(n)
        if e is None:
            continue
        out.append(text[pos:a])
        pos = b
        if e["deleted"]:
            continue
        chunk = text[a:b]
        tr = _TRANSFORM.search(chunk)
        old = [float(v) for v in tr.group(2).split()]
        m = e["m"]
        new = [m[0], m[1], m[2], old[3], m[3], m[4], m[5], old[7], m[6], m[7], m[8], old[11], m[9], m[10], m[11], old[15]]
        out.append(chunk[: tr.start(2)] + " ".join(_fmt(v) for v in new) + " " + chunk[tr.end(2):])
    out.append(text[pos:])
    result = "".join(out).encode("utf-8")
    # completa com espaços: o arquivo não pode encolher para menos blocos de 64 KiB
    if len(result) < len(data):
        result += b" " * (len(data) - len(result))
    return result


def edit_bin(data: bytes, edits: list[dict[str, Any]], layout: tuple) -> bytes:
    _name, start_at, count_at, stride, rot_at, pos_at = layout
    start = struct.unpack_from("<I", data, start_at)[0]
    count = struct.unpack_from("<I", data, count_at)[0]
    buf = bytearray(data)
    for e in edits:
        i = e["index"]
        if not 0 <= i < count:
            raise ValueError(f"registro {i} fora do arquivo ({count} registros)")
        o = start + i * stride
        if e["deleted"]:
            struct.pack_into("<9f", buf, o + rot_at, *([0.0] * 9))
            struct.pack_into("<3f", buf, o + pos_at, *e["m"][9:12][:1], HIDDEN_Y, e["m"][11])
        else:
            struct.pack_into("<9f", buf, o + rot_at, *e["m"][:9])
            struct.pack_into("<3f", buf, o + pos_at, *e["m"][9:12])
    return bytes(buf)


def build_changes(arc, base: str, doc: dict[str, Any]) -> dict[str, bytes]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for e in doc["edits"]:
        grouped.setdefault((e["route"], e["kind"]), []).append(e)
    changes: dict[str, bytes] = {}
    for (route, kind), edits in sorted(grouped.items()):
        if kind == "e":
            path = f"{base}{route}/objects.ens"
            changes[path] = edit_ens(arc.read(path), edits)
        elif kind in BIN_LAYOUT:
            layout = BIN_LAYOUT[kind]
            path = f"{base}{route}/{layout[0]}"
            changes[path] = edit_bin(arc.read(path), edits, layout)
        else:
            raise ValueError(f"tipo de edição desconhecido: {kind!r}")
    return changes


def check_blocks(arc, changes: dict[str, bytes]) -> list[str]:
    """Arquivos cujo novo tamanho muda o número de blocos de 64 KiB (o `nefs_write` recusa)."""
    problems = []
    for path, data in changes.items():
        old = (arc._by_path[path].size + BLOCK_SIZE - 1) // BLOCK_SIZE
        new = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
        if old != new:
            problems.append(f"{path}: {new} blocos contra {old}")
    return problems


def apply(game: str, doc: dict[str, Any], out_path: str, log=print) -> dict[str, Any]:
    from tools.uiview.models import open_package

    if doc.get("format") != "dr2-track-edits":
        raise ValueError("não é um arquivo de edições do Track Explorer")
    rel = doc["src"]
    arc = open_package(game, rel)
    base = track.track_base(arc)
    changes = build_changes(arc, base, doc)
    problems = check_blocks(arc, changes)
    if problems:
        raise ValueError("; ".join(problems))
    log(f"{rel}: {len(doc['edits'])} edições em {len(changes)} arquivos")
    for path in changes:
        log(f"  {path}")
    info = replace_files(arc, changes, out_path)
    log(f"gravado em {out_path}")
    return {"files": list(changes), "write": info}


def main(argv: list[str] | None = None) -> int:
    import argparse

    from tools.egodata.cli import DEFAULT_GAME

    parser = argparse.ArgumentParser(prog="python -m tools.uiview.track_edit", description="Aplica edições do Track Explorer num pacote .nefs novo")
    parser.add_argument("edits", help="arquivo .edits.json exportado pelo viewer")
    parser.add_argument("-o", "--output", required=True, help="caminho do .nefs novo (nunca a pasta do jogo)")
    parser.add_argument("--game", default=DEFAULT_GAME)
    args = parser.parse_args(argv)
    if args.game and args.output.startswith(args.game.rstrip("/") + "/"):
        parser.error("a saída não pode ficar dentro da pasta do jogo")
    with open(args.edits, encoding="utf-8") as fh:
        doc = json.load(fh)
    apply(args.game, doc, args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
