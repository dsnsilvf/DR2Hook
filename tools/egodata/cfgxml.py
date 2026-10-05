"""XML binário das configs do frontend (magia `\\0BXML` ou `\\1BXML`).

Formato diferente do `system/*.bin` (ver `bxml.py`).

Versão 0 (ex.: `text_styles.xml`). Elemento: `u8 0`, `u32 tamanho`, `u8 nº de
atributos`, nome, pares `chave\\0valor\\0`; depois os filhos (cada um começa
com `0`) e, se houver, o texto. O fim é `0` (texto vazio ou terminador do
texto) seguido de `u8 4` e `u32 tamanho`; esse registro ocupa `tamanho` bytes
a partir do `4`.

Versão 1 (ex.: `message_dialogs/*.xml`). Elemento: `u32 tamanho`, `u8 nº de
atributos`, um byte, nome e atributos; depois os filhos (cada um começa com o
seu `u32`); o fim é `u8 4`, o texto terminado em zero e `u32 tamanho`, que
conta a partir do `4` menos um byte.
"""

from __future__ import annotations

import struct

from tools.egodata.bxml import Node

MAGIC = b"\0BXML"
MAGIC_V1 = b"\1BXML"


def _cstr(data: bytes, pos: int) -> tuple[str, int]:
    end = data.index(b"\0", pos)
    return data[pos:end].decode("utf-8", "replace"), end + 1


def decode(data: bytes) -> Node:
    if data[:5] == MAGIC_V1:
        return _decode_v1(data)
    if data[:5] != MAGIC:
        raise ValueError("magia \\0BXML ausente")

    def element(pos: int) -> tuple[Node, int]:
        if data[pos] != 0:
            raise ValueError(f"elemento esperado em {pos:#x}")
        count = data[pos + 5]
        name, pos = _cstr(data, pos + 6)
        attrs = []
        for _ in range(count):
            key, pos = _cstr(data, pos)
            value, pos = _cstr(data, pos)
            attrs.append((key, value))
        children: list[Node] = []
        text = None
        while True:
            if data[pos] == 0 and data[pos + 1] == 4:
                pos += 1
                break
            if data[pos] == 0:
                child, pos = element(pos)
                children.append(child)
                continue
            text, pos = _cstr(data, pos)
            pos -= 1  # o terminador do texto é o 0 antes do 4
            if data[pos + 1] != 4:
                raise ValueError(f"fim de elemento esperado em {pos + 1:#x}")
            pos += 1
            break
        (size,) = struct.unpack_from("<I", data, pos + 1)
        return [name, text, attrs, children], pos + size

    root, _ = element(5)
    return root


def _decode_v1(data: bytes) -> Node:
    def element(pos: int) -> tuple[Node, int]:
        count = data[pos + 4]
        name, pos = _cstr(data, pos + 6)
        attrs = []
        for _ in range(count):
            key, pos = _cstr(data, pos)
            value, pos = _cstr(data, pos)
            attrs.append((key, value))
        children: list[Node] = []
        while data[pos] != 4:
            child, pos = element(pos)
            children.append(child)
        text, pos = _cstr(data, pos + 1)
        (size,) = struct.unpack_from("<I", data, pos)
        return [name, text or None, attrs, children], pos + size - 1

    root, _ = element(5)
    return root
