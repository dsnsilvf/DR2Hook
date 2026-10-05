"""Arquivos NeFS v2.0 da EGO Engine.

`game.nefs` traz o cabeçalho no início. Os `game*.dat` não têm cabeçalho: ele fica
embutido no `dirtrally2.exe` e os blocos são AES-256-ECB (chave em hex no próprio
cabeçalho) seguidos de deflate cru.

`cars/<id>.nefs` e `locations/*.nefs` também são NeFS v2, mas o intro de 128 bytes
não está em claro: é um inteiro little-endian elevado a 65537 módulo a chave pública
RSA-1024 do DiRT Rally 2.0. O resto do cabeçalho é AES-256-ECB com a chave hex que
aparece em `+0x24` depois dessa conta. Os blocos de dados seguem a mesma regra dos
`.dat` (deflate cru, ou AES e depois deflate).
"""

from __future__ import annotations

import os
import struct
import zlib
from dataclasses import dataclass
from typing import BinaryIO, Iterator

from tools.egodata.aes import decrypt_ecb

MAGIC = b"NeFS"
VERSION_2 = 0x20000
BLOCK_SIZE = 0x10000
INTRO_SIZE = 0x80
RSA_EXPONENT = 0x10001
# Módulo público RSA-1024 do DiRT Rally 2.0 (little-endian, como o executável guarda).
DR2_RSA_MODULUS = bytes((
    0xCF, 0x19, 0x63, 0x94, 0x1E, 0x0F, 0x42, 0x16, 0x35, 0xDE, 0x51, 0xD0, 0xB3, 0x3A, 0xB7, 0x67,
    0xC7, 0x1C, 0x8D, 0x3B, 0x27, 0x49, 0x40, 0x9E, 0x58, 0x43, 0xDD, 0x6D, 0xD9, 0xAA, 0xF5, 0x1B,
    0x94, 0x94, 0xC4, 0x30, 0x49, 0xBA, 0xE7, 0x72, 0x3D, 0xFA, 0xDF, 0x80, 0x17, 0x55, 0xF3, 0xAB,
    0xF8, 0x97, 0x42, 0xE6, 0xB2, 0xDF, 0x11, 0xE4, 0x93, 0x0E, 0x92, 0x1D, 0xC5, 0x4E, 0x0F, 0x87,
    0xCD, 0x46, 0x83, 0x06, 0x6B, 0x97, 0xA7, 0x00, 0x42, 0x35, 0xB0, 0x33, 0xEA, 0xEF, 0x68, 0x54,
    0xA0, 0xF9, 0x03, 0x41, 0xF7, 0x5C, 0xFF, 0xC3, 0x75, 0xE1, 0x1B, 0x00, 0x73, 0x5A, 0x7A, 0x81,
    0x68, 0xAF, 0xB4, 0x9F, 0x86, 0x3C, 0xD6, 0x09, 0x3A, 0xC0, 0x94, 0x6F, 0x18, 0xE2, 0x03, 0x38,
    0x14, 0xF7, 0xC5, 0x13, 0x91, 0x4E, 0xD0, 0x4F, 0xAC, 0x46, 0x6C, 0x70, 0x27, 0xED, 0x69, 0x99,
))


def decrypt_nefs_intro(block: bytes) -> bytes:
    """Devolve os 128 bytes do intro. Se já começa com NeFS, devolve como está.

    No DiRT Rally 2.0 o bloco cifrado é um inteiro little-endian. A conta
    `pow(bloco, 65537, módulo)` recupera o intro (magia NeFS, chave AES, tamanho).
    """
    if len(block) < INTRO_SIZE:
        raise ValueError("intro NeFS curto demais")
    raw = block[:INTRO_SIZE]
    if raw[:4] == MAGIC:
        return raw
    value = pow(int.from_bytes(raw, "little"), RSA_EXPONENT, int.from_bytes(DR2_RSA_MODULUS, "little"))
    out = value.to_bytes(INTRO_SIZE, "little")
    if out[:4] != MAGIC:
        raise ValueError("intro sem a magia NeFS")
    return out


@dataclass(frozen=True)
class Entry:
    id: int
    path: str
    size: int
    is_file: bool


class NefsArchive:
    def __init__(self, header: bytes, data_path: str):
        if header[:4] != MAGIC:
            raise ValueError("cabeçalho sem a magia NeFS")
        self.header = header
        self.data_path = data_path
        self.key = bytes.fromhex(header[0x24:0x64].decode("ascii"))
        p1, _p6, p2, _p7, p3, p4, p5, _p8 = struct.unpack_from("<8I", header, 0x84)
        self._names = header[p3:p4]
        self._block_ends = header[p4:p5]
        self._items: dict[int, tuple[int, int]] = {}
        for i in range((p2 - p1) // 20):
            offset, _p2i, p4i, item_id = struct.unpack_from("<QIII", header, p1 + i * 20)
            self._items[item_id] = (offset, p4i)
        self._dirs: dict[int, tuple[int, int, str, int]] = {}
        for i in range((p3 - p2) // 20):
            parent, first_child, name_offset, size, item_id = struct.unpack_from("<5I", header, p2 + i * 20)
            self._dirs[item_id] = (parent, first_child, self._name(name_offset), size)
        self._by_path = {e.path: e for e in self.entries()}

    @classmethod
    def open_nefs(cls, path: str) -> "NefsArchive":
        with open(path, "rb") as fh:
            intro = fh.read(0x6C)
            size = struct.unpack_from("<I", intro, 0x64)[0]
            fh.seek(0)
            return cls(fh.read(size), path)

    @classmethod
    def open_encrypted(cls, path: str) -> "NefsArchive":
        """Abre um `.nefs` cujo intro de 128 bytes está embaralhado (carros e pistas)."""
        with open(path, "rb") as fh:
            intro = decrypt_nefs_intro(fh.read(INTRO_SIZE))
            size = struct.unpack_from("<I", intro, 0x64)[0]
            if size < INTRO_SIZE or size > 64 * 1024 * 1024:
                raise ValueError(f"tamanho de cabeçalho implausível em {path}")
            rest = fh.read(size - INTRO_SIZE)
        key = bytes.fromhex(intro[0x24:0x64].decode("ascii"))
        pad = (-len(rest)) % 16
        plain = decrypt_ecb(rest + b"\0" * pad, key)[: len(rest)] if rest else b""
        return cls(intro + plain, path)

    @classmethod
    def open_path(cls, path: str) -> "NefsArchive":
        """`open_nefs` se o arquivo começa com NeFS; senão tenta o intro embaralhado."""
        with open(path, "rb") as fh:
            magic = fh.read(4)
        if magic == MAGIC:
            return cls.open_nefs(path)
        return cls.open_encrypted(path)

    @classmethod
    def open_headless(cls, data_path: str, exe_path: str) -> "NefsArchive":
        exe = open(exe_path, "rb").read()
        data_size = os.path.getsize(data_path)
        best = None
        for header in embedded_headers(exe):
            archive = cls(header, data_path)
            end = archive.data_end()
            if end <= data_size and (best is None or end > best[0]):
                best = (end, archive)
        if best is None or data_size - best[0] > BLOCK_SIZE:
            raise ValueError(f"nenhum cabeçalho embutido corresponde a {data_path}")
        return best[1]

    def _name(self, offset: int) -> str:
        return self._names[offset : self._names.index(b"\0", offset)].decode("latin1")

    def _path(self, item_id: int) -> str:
        parts = []
        for _ in range(64):
            parent, _fc, name, _size = self._dirs[item_id]
            parts.append(name)
            if parent == item_id:
                break
            item_id = parent
        return "/".join(reversed(parts))

    def entries(self) -> Iterator[Entry]:
        for item_id, (_parent, first_child, _name, size) in self._dirs.items():
            yield Entry(item_id, self._path(item_id), size, first_child == item_id)

    def _blocks(self, item_id: int) -> tuple[int, tuple[int, ...]]:
        offset, p4i = self._items[item_id]
        count = (self._dirs[item_id][3] + BLOCK_SIZE - 1) // BLOCK_SIZE
        return offset, struct.unpack_from(f"<{count}I", self._block_ends, p4i * 4)

    def data_end(self) -> int:
        end = 0
        for entry in self._by_path.values():
            if entry.is_file and entry.size and entry.id in self._items:
                offset, ends = self._blocks(entry.id)
                end = max(end, offset + ends[-1])
        return end

    def read(self, path: str, fh: BinaryIO | None = None) -> bytes:
        entry = self._by_path[path]
        if not entry.is_file:
            raise IsADirectoryError(path)
        offset, ends = self._blocks(entry.id)
        own = fh is None
        fh = fh or open(self.data_path, "rb")
        try:
            out, prev = [], 0
            for index, end in enumerate(ends):
                fh.seek(offset + prev)
                expected = min(BLOCK_SIZE, entry.size - index * BLOCK_SIZE)
                out.append(self._decode_block(fh.read(end - prev), expected))
                prev = end
        finally:
            if own:
                fh.close()
        return b"".join(out)

    def _decode_block(self, block: bytes, expected: int) -> bytes:
        # Um bloco cifrado pode passar por deflate válido por acaso e render
        # poucos bytes de lixo; só vale a saída do tamanho esperado.
        out = _inflate(block, expected)
        if out is not None:
            return out
        if len(block) % 16 == 0:
            plain = decrypt_ecb(block, self.key)
            out = _inflate(plain, expected)
            return plain if out is None else out
        return block


def _inflate(block: bytes, expected: int) -> bytes | None:
    try:
        out = zlib.decompress(block, -15)
    except zlib.error:
        return None
    return out if len(out) == expected else None


def embedded_headers(exe: bytes) -> Iterator[bytes]:
    pos = exe.find(MAGIC)
    while pos != -1:
        size, version = struct.unpack_from("<II", exe, pos + 0x64)
        key = exe[pos + 0x24 : pos + 0x64]
        if version == VERSION_2 and 0x100 <= size <= len(exe) - pos and all(c in b"0123456789ABCDEFabcdef" for c in key):
            yield exe[pos : pos + size]
        pos = exe.find(MAGIC, pos + 1)
