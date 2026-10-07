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

Strings dentro das linhas ficam como djb2 (h = h*33 + byte, semente 5381) da string da tabela de
strings. `Ctpk.build()` regrava o arquivo; sem mudanças a saída é idêntica à entrada.
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


def djb2(s: str | bytes) -> int:
    h = 5381
    for c in s.encode() if isinstance(s, str) else s:
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def enc_varint(v: int) -> bytes:
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def encode(fields: list[tuple[int, int, object]]) -> bytes:
    """Inverso de `decode`. Tipo 5 aceita (u32, f32) ou float."""
    out = bytearray()
    for fn, wt, v in fields:
        out += enc_varint(fn << 3 | wt)
        if wt == 0:
            out += enc_varint(v)
        elif wt == 1:
            out += struct.pack("<Q", v)
        elif wt == 2:
            out += enc_varint(len(v)) + v
        elif wt == 5:
            out += struct.pack("<I", v[0]) if isinstance(v, tuple) else struct.pack("<f", v)
        else:
            raise ValueError(f"wire type {wt}")
    return bytes(out)


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

    def add_string(self, s: str) -> int:
        """Põe a string na tabela (se faltar) e devolve o djb2 que as linhas usam."""
        b = s.encode()
        if b not in self.strings:
            self.strings.append(b)
        return djb2(b)

    def put_row(self, table_hash: int, row: Row) -> None:
        """Insere ou troca a linha; a busca do jogo é binária, então as linhas ficam por id."""
        rows = [r for r in self.tables[table_hash].rows if r.id != row.id] + [row]
        self.tables[table_hash].rows = sorted(rows, key=lambda r: r.id)

    def build(self) -> bytes:
        body = bytearray(struct.pack("<I", len(self.strings)))
        for s in self.strings:
            body += struct.pack("<I", len(s)) + s
        str_end = 0x18 + len(body)
        dir_end = str_end + 4 + 8 * len(self.tables)
        tables = bytearray()
        directory = bytearray(struct.pack("<I", len(self.tables)))
        for t in self.tables.values():
            if not t.rows and t.offset == 0xFFFFFFFF:
                directory += struct.pack("<2I", t.hash, 0xFFFFFFFF)
                continue
            directory += struct.pack("<2I", t.hash, dir_end + len(tables))
            tables += struct.pack("<I", len(t.rows))
            for r in t.rows:
                tables += struct.pack("<2I", r.id, len(r.raw)) + r.raw
        head = b"CTPK" + struct.pack("<5I", self.version, self.checksum, 0x18, str_end, 0)
        return head + bytes(body) + bytes(directory) + bytes(tables)


if __name__ == "__main__":
    c = Ctpk.open(sys.argv[1] if len(sys.argv) > 1 else "build/re/ctpk/base.ctpk")
    print(f"versão {c.version}, {len(c.strings)} strings, {len(c.tables)} tabelas, "
          f"{sum(len(t.rows) for t in c.tables.values())} linhas")
    same = c.build() == c.data and all(encode(r.fields) == r.raw for t in c.tables.values() for r in t.rows)
    print("regravação idêntica:", same)
