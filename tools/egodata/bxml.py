"""XML binário em blocos de `system/*.bin` (screens, states, flow, links).

Cada bloco é `u32 tipo, u32 tamanho, dados`. Um nó tem seis `u32`:
nome, texto, nº de atributos, 1º atributo, nº de filhos, 1º filho. Os filhos de
um nó ficam contíguos e são reservados quando o pai é visitado. As strings são
internadas em pré-ordem, com o texto de um nó depois dos filhos. Esse é o layout que o jogo usa: a ida e volta
reproduz os arquivos originais byte a byte.
"""

from __future__ import annotations

import struct
import sys
from typing import Iterator
from xml.sax.saxutils import quoteattr

ROOT = 0x7252221A
NODES = 0x7252221B
ATTRS = 0x7252221C
STRINGS = 0x7252221D
STRING_OFFSETS = 0x7252221E
STRING_TABLE = 0x72522217

# [nome, texto ou None, [(chave, valor)], [filhos]]
Node = list


def _chunks(data: bytes, start: int, end: int) -> Iterator[tuple[int, int, int]]:
    while start < end:
        kind, size = struct.unpack_from("<II", data, start)
        yield kind, start + 8, size
        start += 8 + size


def _tables(data: bytes):
    kind, size = struct.unpack_from("<II", data, 0)
    if kind != ROOT:
        raise ValueError("não é XML binário da EGO Engine")
    found = {}

    def walk(start: int, end: int) -> None:
        for k, body, length in _chunks(data, start, end):
            if k == STRING_TABLE:
                walk(body, body + length)
            else:
                found[k] = (body, length)

    walk(8, 8 + size)
    sb, _ = found[STRINGS]
    ob, olen = found[STRING_OFFSETS]
    offsets = struct.unpack_from(f"<{olen // 4}I", data, ob)
    strings = [data[sb + o : data.index(b"\0", sb + o)].decode("latin1") for o in offsets]
    nb, nlen = found[NODES]
    nodes = [struct.unpack_from("<6I", data, nb + i * 24) for i in range(nlen // 24)]
    ab, alen = found[ATTRS]
    attrs = [struct.unpack_from("<2I", data, ab + i * 8) for i in range(alen // 8)]
    return strings, nodes, attrs


def decode(data: bytes) -> Node:
    strings, nodes, attrs = _tables(data)

    def build(i: int) -> Node:
        name, text, n_attrs, first_attr, n_children, first_child = nodes[i]
        return [
            strings[name],
            strings[text] if text else None,
            [(strings[attrs[j][0]], strings[attrs[j][1]]) for j in range(first_attr, first_attr + n_attrs)],
            [build(c) for c in range(first_child, first_child + n_children)],
        ]

    sys.setrecursionlimit(max(sys.getrecursionlimit(), 10000))
    return build(0)


def encode(root: Node) -> bytes:
    strings: list[str] = []
    index: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in index:
            index[s] = len(strings)
            strings.append(s)
        return index[s]

    def intern_tree(node: Node) -> None:
        intern(node[0])
        for key, value in node[2]:
            intern(key)
            intern(value)
        for child in node[3]:
            intern_tree(child)
        if node[1] is not None:
            intern(node[1])

    intern_tree(root)
    nodes: list = [None]
    attrs: list[tuple[int, int]] = []
    total = [1]

    def place(i: int, node: Node) -> None:
        first_attr = len(attrs)
        attrs.extend((index[k], index[v]) for k, v in node[2])
        first_child = total[0]
        total[0] += len(node[3])
        nodes.extend([None] * len(node[3]))
        nodes[i] = (
            index[node[0]],
            index[node[1]] if node[1] is not None else 0,
            len(node[2]),
            first_attr if node[2] else 0,
            len(node[3]),
            first_child,
        )
        for j, child in enumerate(node[3]):
            place(first_child + j, child)

    place(0, root)
    blob, offsets = b"", []
    for s in strings:
        offsets.append(len(blob))
        blob += s.encode("latin1") + b"\0"
    blob += b"\0" * (-len(blob) % 16)

    def chunk(kind: int, payload: bytes) -> bytes:
        return struct.pack("<II", kind, len(payload)) + payload

    table = chunk(STRINGS, blob) + chunk(STRING_OFFSETS, struct.pack(f"<{len(offsets)}I", *offsets))
    body = (
        chunk(STRING_TABLE, table)
        + chunk(NODES, b"".join(struct.pack("<6I", *n) for n in nodes))
        + chunk(ATTRS, b"".join(struct.pack("<2I", *a) for a in attrs))
    )
    return chunk(ROOT, body)


def to_xml(root: Node) -> str:
    out: list[str] = []

    def emit(node: Node, depth: int) -> None:
        name, text, attributes, children = node
        attr_text = "".join(f" {k}={quoteattr(v)}" for k, v in attributes)
        indent = "  " * depth
        if not children and text is None:
            out.append(f"{indent}<{name}{attr_text}/>")
            return
        out.append(f"{indent}<{name}{attr_text}>")
        if text is not None and text.strip():
            out.append(f"{indent}  {' '.join(text.split())}")
        for child in children:
            emit(child, depth + 1)
        out.append(f"{indent}</{name}>")

    emit(root, 0)
    return "\n".join(out) + "\n"
