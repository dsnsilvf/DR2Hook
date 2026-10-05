"""Substituição de um arquivo dentro de um `.nefs` já existente.

O cabeçalho do pacote não muda de tamanho: o arquivo novo vai para o fim do volume, e só
mudam o offset do item (P1), os fins de bloco (P4) e o tamanho no diretório (P2). O intro
de 128 bytes (assinado, não dá para refazer) fica como está, e o resto do cabeçalho é
cifrado de novo com a mesma chave. Por isso o arquivo novo precisa caber no mesmo número
de blocos de 64 KiB do original.
"""

from __future__ import annotations

import shutil
import struct
import zlib

from tools.egodata.aes import encrypt_ecb
from tools.egodata.nefs import BLOCK_SIZE, INTRO_SIZE, MAGIC, NefsArchive, _inflate


def _deflate(chunk: bytes) -> bytes:
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    return comp.compress(chunk) + comp.flush()


def _is_encrypted(arc: NefsArchive, item_id: int) -> bool:
    """Um bloco que não passa por deflate cru está cifrado (AES e depois deflate)."""
    offset, ends = arc._blocks(item_id)
    with open(arc.data_path, "rb") as fh:
        fh.seek(offset)
        first = fh.read(ends[0])
    size = arc._dirs[item_id][3]
    return _inflate(first, min(BLOCK_SIZE, size)) is None


def encode_blocks(data: bytes, key: bytes, encrypted: bool) -> list[bytes]:
    blocks = []
    for pos in range(0, len(data), BLOCK_SIZE):
        block = _deflate(data[pos : pos + BLOCK_SIZE])
        if encrypted:
            block += b"\0" * (-len(block) % 16)
            block = encrypt_ecb(block, key)
        blocks.append(block)
    return blocks


def replace_file(arc: NefsArchive, path: str, data: bytes, out_path: str) -> dict[str, int]:
    """Grava em `out_path` uma cópia do pacote com `path` trocado por `data`."""
    return replace_files(arc, {path: data}, out_path)[path]


def replace_files(arc: NefsArchive, changes: dict[str, bytes], out_path: str) -> dict[str, dict[str, int]]:
    """Grava em `out_path` uma cópia do pacote com vários arquivos trocados, copiando o volume uma vez só."""
    entries = {}
    for path, data in changes.items():
        entry = arc._by_path[path]
        if not entry.is_file or entry.id not in arc._items:
            raise ValueError(f"{path} não é um arquivo com dados")
        old_blocks = (entry.size + BLOCK_SIZE - 1) // BLOCK_SIZE
        new_blocks = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
        if new_blocks != old_blocks:
            raise ValueError(f"{path}: {new_blocks} blocos contra {old_blocks} originais; o cabeçalho cresceria")
        entries[path] = entry

    header = bytearray(arc.header)
    p1, _p6, p2, _p7, p3, p4, _p5, _p8 = struct.unpack_from("<8I", header, 0x84)
    results: dict[str, dict[str, int]] = {}
    shutil.copyfile(arc.data_path, out_path)
    with open(out_path, "r+b") as fh:
        fh.seek(0, 2)
        start = fh.tell()
        start += -start % 16
        fh.truncate(start)
        fh.seek(start)
        for path, data in changes.items():
            entry = entries[path]
            encrypted = _is_encrypted(arc, entry.id)
            blocks = encode_blocks(data, arc.key, encrypted)
            fh.write(b"\0" * (-fh.tell() % 16))
            start = fh.tell()
            ends, total = [], 0
            for block in blocks:
                fh.write(block)
                total += len(block)
                ends.append(total)

            for i in range((p2 - p1) // 20):
                if struct.unpack_from("<QIII", header, p1 + i * 20)[3] == entry.id:
                    _o, p2i, p4i, item_id = struct.unpack_from("<QIII", header, p1 + i * 20)
                    struct.pack_into("<QIII", header, p1 + i * 20, start, p2i, p4i, item_id)
                    break
            else:
                raise ValueError("item sem linha em P1")
            struct.pack_into(f"<{len(ends)}I", header, p4 + p4i * 4, *ends)
            for i in range((p3 - p2) // 20):
                if struct.unpack_from("<5I", header, p2 + i * 20)[4] == entry.id:
                    struct.pack_into("<I", header, p2 + i * 20 + 12, len(data))
                    break
            results[path] = {"offset": start, "blocks": len(blocks), "bytes": total, "encrypted": int(encrypted)}

        plain_header = bytes(header)
        if arc.header[:4] == MAGIC and _intro_is_plain(arc.data_path):
            stored = plain_header
        else:
            rest = plain_header[INTRO_SIZE:]
            pad = -len(rest) % 16
            stored = fh_intro(arc.data_path) + encrypt_ecb(rest + b"\0" * pad, arc.key)[: len(rest)]
        fh.seek(0)
        fh.write(stored)
    return results


def _intro_is_plain(path: str) -> bool:
    with open(path, "rb") as fh:
        return fh.read(4) == MAGIC


def fh_intro(path: str) -> bytes:
    with open(path, "rb") as fh:
        return fh.read(INTRO_SIZE)
