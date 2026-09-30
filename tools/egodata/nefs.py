"""Arquivos NeFS v2.0 da EGO Engine.

`game.nefs` traz o cabeçalho no início. Os `game*.dat` não têm cabeçalho: ele fica
embutido no `dirtrally2.exe` e os blocos são AES-256-ECB (chave em hex no próprio
cabeçalho) seguidos de deflate cru.
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
            for end in ends:
                fh.seek(offset + prev)
                out.append(self._decode_block(fh.read(end - prev)))
                prev = end
        finally:
            if own:
                fh.close()
        return b"".join(out)

    def _decode_block(self, block: bytes) -> bytes:
        try:
            return zlib.decompress(block, -15)
        except zlib.error:
            pass
        if len(block) % 16 == 0:
            plain = decrypt_ecb(block, self.key)
            try:
                return zlib.decompress(plain, -15)
            except zlib.error:
                return plain
        return block


def embedded_headers(exe: bytes) -> Iterator[bytes]:
    pos = exe.find(MAGIC)
    while pos != -1:
        size, version = struct.unpack_from("<II", exe, pos + 0x64)
        key = exe[pos + 0x24 : pos + 0x64]
        if version == VERSION_2 and 0x100 <= size <= len(exe) - pos and all(c in b"0123456789ABCDEFabcdef" for c in key):
            yield exe[pos : pos + size]
        pos = exe.find(MAGIC, pos + 1)
