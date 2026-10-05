"""Tabela de textos traduzidos (`language/language_*.lng`, magia `LNGT`).

Big-endian. `SIDA` tem os pares (offset da chave, offset do valor); as chaves
ficam em `SIDB` e os valores em `LNGB`, terminados em zero. A tabela de hash
(`HSHS`/`HSHT`) não é necessária para ler.
"""

from __future__ import annotations

import struct

MAGIC = b"LNGT"


def _block(data: bytes, tag: bytes, start: int = 0) -> tuple[int, int]:
    pos = data.find(tag, start)
    if pos < 0:
        raise ValueError(f"bloco {tag!r} ausente")
    (size,) = struct.unpack_from(">I", data, pos + 4)
    return pos + 8, size


def _cstr(data: bytes, pos: int) -> str:
    return data[pos : data.index(b"\0", pos)].decode("utf-8", "replace")


def decode(data: bytes) -> dict[str, str]:
    if data[:4] != MAGIC:
        raise ValueError("magia LNGT ausente")
    pairs, _ = _block(data, b"SIDA")
    (count,) = struct.unpack_from(">I", data, pairs)
    keys, keys_size = _block(data, b"SIDB", pairs)
    values, _ = _block(data, b"LNGB", keys + keys_size)
    out: dict[str, str] = {}
    for i in range(count):
        key, value = struct.unpack_from(">II", data, pairs + 4 + 8 * i)
        out[_cstr(data, keys + key)] = _cstr(data, values + value)
    return out
