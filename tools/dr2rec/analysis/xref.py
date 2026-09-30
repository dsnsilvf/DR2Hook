"""Procura um deslocamento imediato no executável local.

Isto não executa o jogo e não distingue sozinho um writer de um falso positivo.
A confiança fica em POSSIBLE.
"""

from __future__ import annotations

import struct
from pathlib import Path

from tools.dr2rec.confidence import POSSIBLE


def load_code(path: str) -> tuple[bytes, int]:
    data = Path(path).read_bytes()
    if data[:2] != b"MZ":
        return data, 0
    return extract_text(data)


def extract_text(data: bytes) -> tuple[bytes, int]:
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError("executável sem cabeçalho MZ")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 24 > len(data) or data[e_lfanew : e_lfanew + 4] != b"PE\x00\x00":
        raise ValueError("executável sem assinatura PE")
    coff = e_lfanew + 4
    _machine, sections, _t1, _t2, _t3, opt_size, _chars = struct.unpack_from("<HHIIIHH", data, coff)
    section_off = coff + 20 + opt_size
    for index in range(sections):
        off = section_off + index * 40
        if off + 40 > len(data):
            break
        name = data[off : off + 8].split(b"\x00", 1)[0]
        virtual_size, virtual_addr, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        if name != b".text":
            continue
        blob = data[raw_ptr : raw_ptr + raw_size]
        return blob, virtual_addr
    raise ValueError("seção .text não encontrada")


def classify_site(code: bytes, disp_at: int) -> tuple[str, str] | None:
    """`disp_at` aponta para o deslocamento de 32 bits, depois do ModRM."""
    if disp_at < 3 or disp_at >= len(code):
        return None
    modrm = code[disp_at - 1]
    if (modrm & 0xC0) != 0x80:
        return None
    before = code[max(0, disp_at - 5) : disp_at - 1]
    if before.endswith(b"\xF3\x0F\x11"):
        return "writer", "movss"
    if before.endswith(b"\xF3\x0F\x10"):
        return "reader", "movss"
    if before.endswith(b"\x0F\x11"):
        return "writer", "movups"
    if before.endswith(b"\x0F\x10"):
        return "reader", "movups"
    if before.endswith(b"\x89"):
        return "writer", "mov"
    if before.endswith(b"\x8B"):
        return "reader", "mov"
    if before.endswith(b"\x8D"):
        return "reader", "lea"
    return None


def scan_code(code: bytes, base_rva: int, offsets: list[int], limit: int = 8) -> list[dict]:
    wanted = {}
    for offset in offsets:
        if 0 < offset < 2**32:
            wanted.setdefault(offset.to_bytes(4, "little"), offset)
    hits = []
    for needle, offset in wanted.items():
        start = 0
        count = 0
        while count < limit:
            found = code.find(needle, start)
            if found < 0:
                break
            classified = classify_site(code, found)
            start = found + 1
            if classified is None:
                continue
            role, kind = classified
            hits.append(
                {
                    "offset": offset,
                    "rva": base_rva + found - 3 if kind in {"movss", "movups"} else base_rva + found,
                    "disp_rva": base_rva + found,
                    "role": role,
                    "kind": kind,
                    "confidence": POSSIBLE,
                }
            )
            count += 1
    return hits


def dataflow(hits: list[dict]) -> list[dict]:
    by_offset: dict[int, list[dict]] = {}
    for hit in hits:
        by_offset.setdefault(hit["offset"], []).append(hit)
    rows = []
    for offset, group in by_offset.items():
        writers = [hit for hit in group if hit["role"] == "writer"]
        readers = [hit for hit in group if hit["role"] == "reader"]
        rows.append(
            {
                "offset": offset,
                "writers": writers,
                "readers": readers,
                "confidence": POSSIBLE,
            }
        )
    return rows
