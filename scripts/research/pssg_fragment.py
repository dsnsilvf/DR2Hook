"""Fragmento de PSSG para enxerto (`graft/` dos pacotes .dr2track e o selo embutido na dxgi).

Um fragmento é um PSSG com o esquema do arquivo de origem e só o caminho da raiz até os nós escolhidos. O
carregador (src/trackpkg/pssg.cpp, Pssg::Graft) desce pelos nós que já existem no arquivo do jogo,
casando pela identidade (tipo + id/nickname; LIBRARY pelo `type`), e acrescenta o resto.
"""

from __future__ import annotations

import copy
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.egodata.pssg import PSSGFile, PSSGNode  # noqa: E402


def identity(n: PSSGNode) -> str | None:
    if n.id is not None or n.nickname is not None:
        return f"{n.type_name}|{n.id or ''}|{n.nickname or ''}"
    if n.type_name == "LIBRARY":
        return f"LIBRARY||{n.get('type')}"
    if n.type_name == "PSSGDATABASE":
        return n.type_name
    return None


def fragment(p: PSSGFile, nodes: list[PSSGNode]) -> bytes:
    """Bytes do fragmento com `nodes` (inteiros) e os ancestrais deles (só como caminho)."""
    parent: dict[int, PSSGNode] = {}

    def walk(n: PSSGNode) -> None:
        for c in n.children:
            parent[id(c)] = n
            walk(c)

    walk(p.root)
    copies: dict[int, PSSGNode] = {}

    def path_copy(n: PSSGNode) -> PSSGNode:
        if id(n) in copies:
            return copies[id(n)]
        if n is not p.root and identity(n) is None:
            raise ValueError(f"ancestral sem identidade ({n.type_name}): o enxerto não acharia o caminho")
        c = copy.copy(n)
        c.children = []
        copies[id(n)] = c
        if n is not p.root:
            path_copy(parent[id(n)]).children.append(c)
        return c

    for n in nodes:
        if n is p.root or id(n) not in parent:
            raise ValueError("nó fora da árvore")
        path_copy(parent[id(n)]).children.append(n)
    out = copy.copy(p)
    out.root = path_copy(p.root)
    out._tail = b""
    out.file_size = 0
    return out.serialize()


def write_inc(path: str, name: str, data: bytes, source: str) -> None:
    """Array C++ com os bytes (para embutir na dxgi)."""
    lines = [f"// Gerado por {source}; não editar.", f"const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    lines += ["};", f"const size_t {name}Size = sizeof({name});", ""]
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as fh:
        fh.write("\n".join(lines))


def added_nodes(orig: PSSGFile, new: PSSGFile) -> list[PSSGNode]:
    """Nós de `new` que não existem em `orig` (casando os filhos pela identidade, a partir da raiz).

    Filhos sem identidade de um nó que existe nos dois são tidos como iguais (não entram)."""
    out: list[PSSGNode] = []

    def walk(a: PSSGNode, b: PSSGNode) -> None:
        known = {identity(c): c for c in a.children if identity(c) is not None}
        for c in b.children:
            key = identity(c)
            if key is None:
                continue
            if key in known:
                walk(known[key], c)
            else:
                out.append(c)

    walk(orig.root, new.root)
    return out
