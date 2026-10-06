#!/usr/bin/env python3
"""Ida e volta de um edits.json do viewer nativo com o lado Python, sem o jogo.

    python3 tools/viewer3d/tests/edit_roundtrip.py build/uiview/saves/synthetic__dr2hook_ring.edits.json \
            build/uiview/tracks/synthetic__dr2hook_ring

Aplica o arquivo aos fontes da pista sintética (`source/<rota>/objects.ens`, `ornaments.bin`, `trees.bin`,
de `python -m tools.synthtrack`) com as mesmas funções que `python -m tools.uiview.track.edit` usa
(`edit_ens`, `edit_bin`), confere que nenhum arquivo muda de número de blocos de 64 KiB (o que
`check_blocks` recusaria), relê com os leitores do exportador e confere cada matriz (1e-3) e cada
apagado. Na Montalegre, a prova equivalente é o script da etapa 7 (docs/plans/viewer3d/etapa_7_selecao_mover.md),
que grava um .nefs com o track.edit.
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))

from tools.uiview.track import edit as track_edit  # noqa: E402
from tools.uiview.track import export as track  # noqa: E402

BLOCK = 65536


def main(edits_path: str, track_dir: str) -> int:
    with open(edits_path, encoding="utf-8") as fh:
        doc = json.load(fh)
    assert doc["format"] == "dr2-track-edits" and doc["version"] == 1, "não é um edits.json"
    grouped: dict[tuple[str, str], list[dict]] = {}
    for e in doc["edits"]:
        for key in ("route", "kind", "type", "index", "deleted", "m", "m0"):
            assert key in e, f"falta {key} em {e}"
        assert len(e["m"]) == 12 and len(e["m0"]) == 12
        grouped.setdefault((e["route"], e["kind"]), []).append(e)
    checked = 0
    for (route, kind), edits in sorted(grouped.items()):
        src = os.path.join(track_dir, "source", route)
        if kind == "e":
            path = os.path.join(src, "objects.ens")
            old = open(path, "rb").read()
            new = track_edit.edit_ens(old, edits)
            # um apagado sai do texto: os índices seguintes descem; confere pelos que restam
            after = track.instances(new)
            before = track.instances(old)
            removed = sorted(e["index"] for e in edits if e["deleted"] and not e.get("added"))
            copies = [e for e in edits if e.get("added")]
            for e in copies:
                assert e["index"] == -1 and isinstance(e["src"], int), e
                flat = lambda m: [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14]]  # noqa: E731
                assert any(all(abs(a - b) < 1e-3 for a, b in zip(flat(x["m"]), e["m"])) for x in after), ("cópia sumiu", e)
                checked += 1
            for e in edits:
                if e["deleted"] or e.get("added"):
                    continue
                # edit_ens põe cada cópia logo depois da origem: as cópias de origens anteriores empurram o índice
                pos = e["index"] - sum(1 for r in removed if r < e["index"]) + sum(1 for c in copies if c["src"] < e["index"])
                m = after[pos]["m"]
                got = [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14]]
                assert all(abs(a - b) < 1e-3 for a, b in zip(got, e["m"])), (e["index"], got, e["m"])
                checked += 1
            assert len(after) == len(before) - len(removed) + len(copies), "contagem de instâncias depois de apagar e copiar"
            checked += len(removed)
        else:
            layout = track_edit.BIN_LAYOUT[kind]
            path = os.path.join(src, layout[0])
            old = open(path, "rb").read()
            new = track_edit.edit_bin(old, edits, layout)
            recs = (track.ornaments_bin if kind == "o" else track.trees_bin)(new)
            for e in edits:
                m = recs[e["index"]]["m"]
                if e["deleted"]:
                    assert m[13] == track_edit.HIDDEN_Y, (e["index"], m)
                else:
                    got = [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14]]
                    assert all(abs(a - b) < 1e-3 for a, b in zip(got, e["m"])), (e["index"], got, e["m"])
                checked += 1
        assert (len(new) + BLOCK - 1) // BLOCK == (len(old) + BLOCK - 1) // BLOCK, f"{path}: mudou de número de blocos"
    print(f"OK {len(doc['edits'])} edições conferidas ({checked} verificações) em {len(grouped)} arquivo(s)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2]))
