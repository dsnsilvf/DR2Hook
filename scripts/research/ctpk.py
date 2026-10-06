"""Leitor do catálogo do jogo (`catalogues/base.ctpk`, magia CTPK versão 2).

Formato (little-endian):
    0x00  "CTPK", u32 versão (2), u32 hash/checksum, u32 0x18 (início das strings), u32 fim das
          strings (offset absoluto), u32 0
    0x18  u32 n_strings, depois n × (u32 tamanho, bytes)          (sem terminador)
    ...   u32 n_tabelas, n × (u32 hash da seção, u32 offset absoluto)
    tabela: (offset 0xffffffff = seção vazia) u32 n_linhas, n × (u32 id, u32 tamanho, mensagem protobuf)

No exe, um objeto do catálogo é o id de 64 bits (hash da seção << 32) | id da linha, buscado por
0x1400e89a0(catálogo, id, 0). As mensagens não têm descritores no exe (só os enums têm), então
os campos aqui são decodificados de forma genérica (número do campo → valores).
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass, field


def varint(b: bytes, p: int) -> tuple[int, int]:
    v = s = 0
    while True:
        c = b[p]
        p += 1
        v |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return v, p


def decode(msg: bytes) -> list[tuple[int, int, object]]:
    """Lista de (campo, wire type, valor). Tipo 2 devolve bytes; 5 devolve (u32, f32)."""
    out = []
    p = 0
    while p < len(msg):
        key, p = varint(msg, p)
        fn, wt = key >> 3, key & 7
        if wt == 0:
            v, p = varint(msg, p)
        elif wt == 1:
            v = struct.unpack_from("<Q", msg, p)[0]
            p += 8
        elif wt == 2:
            n, p = varint(msg, p)
            v = msg[p : p + n]
            p += n
        elif wt == 5:
            u = struct.unpack_from("<I", msg, p)[0]
            v = (u, struct.unpack_from("<f", msg, p)[0])
            p += 4
        else:
            raise ValueError(f"wire type {wt} em {p}")
        out.append((fn, wt, v))
    return out


@dataclass
class Row:
    id: int
    raw: bytes

    @property
    def fields(self) -> list[tuple[int, int, object]]:
        return decode(self.raw)


@dataclass
class Table:
    hash: int
    offset: int
    rows: list[Row] = field(default_factory=list)


class Ctpk:
    def __init__(self, data: bytes):
        assert data[:4] == b"CTPK", data[:4]
        self.data = data
        self.version, self.checksum, str_off, str_size, _z = struct.unpack_from("<5I", data, 4)
        n = struct.unpack_from("<I", data, str_off)[0]
        p = str_off + 4
        self.strings: list[bytes] = []
        for _ in range(n):
            ln = struct.unpack_from("<I", data, p)[0]
            self.strings.append(data[p + 4 : p + 4 + ln])
            p += 4 + ln
        assert p == str_size, (hex(p), hex(str_size))
        nt = struct.unpack_from("<I", data, p)[0]
        self.tables: dict[int, Table] = {}
        for i in range(nt):
            h, off = struct.unpack_from("<2I", data, p + 4 + 8 * i)
            t = Table(h, off)
            self.tables[h] = t
            if off == 0xFFFFFFFF:  # seção sem linhas neste pacote
                t.end = off
                continue
            nr = struct.unpack_from("<I", data, off)[0]
            q = off + 4
            for _ in range(nr):
                rid, ln = struct.unpack_from("<2I", data, q)
                t.rows.append(Row(rid, data[q + 8 : q + 8 + ln]))
                q += 8 + ln
            t.end = q

    @classmethod
    def open(cls, path: str) -> "Ctpk":
        with open(path, "rb") as fh:
            return cls(fh.read())

    def string(self, i: int) -> str:
        return self.strings[i].decode("utf-8", "replace")


if __name__ == "__main__":
    c = Ctpk.open(sys.argv[1] if len(sys.argv) > 1 else "build/re/ctpk/base.ctpk")
    print(f"versão {c.version}, {len(c.strings)} strings, {len(c.tables)} tabelas, "
          f"{sum(len(t.rows) for t in c.tables.values())} linhas")
