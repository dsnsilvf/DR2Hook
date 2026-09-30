"""Leitura do processo do jogo. Não há escrita.

`process_vm_readv` copia bytes para este processo. O jogo não recebe patch,
hook nem pacote.
"""

from __future__ import annotations

import ctypes
import os
from typing import Protocol

IMAGE_BASE_PREFERRED = 0x140000000
CAR_SLOT = 0x1681CE8
CAR_FALLBACKS = (0x15A4B00, 0x15A9760)
CONTAINER_OFF = 0x30
RIG_OFF = 0x08
SELF_OFF = 0x12C0
COUNT_OFF = 0x12D0


class IOVec(ctypes.Structure):
    _fields_ = [("iov_base", ctypes.c_uint64), ("iov_len", ctypes.c_uint64)]


def _libc():
    lib = ctypes.CDLL(None, use_errno=True)
    lib.process_vm_readv.argtypes = [
        ctypes.c_int,
        ctypes.POINTER(IOVec),
        ctypes.c_ulong,
        ctypes.POINTER(IOVec),
        ctypes.c_ulong,
        ctypes.c_ulong,
    ]
    lib.process_vm_readv.restype = ctypes.c_ssize_t
    return lib


LIBC = _libc()


class MemorySource(Protocol):
    def read_many(self, ranges: list[tuple[int, int]]) -> list[bytes | None]:
        """Cada par é (endereço absoluto, tamanho). None quando a leitura falha."""


def read_many(pid: int, ranges: list[tuple[int, int]]) -> list[bytes | None]:
    """Uma syscall para várias faixas. A primeira falha encerra o resto."""
    if not ranges:
        return []
    bufs = []
    for _addr, size in ranges:
        bufs.append(ctypes.create_string_buffer(size))
    local = (IOVec * len(ranges))()
    remote = (IOVec * len(ranges))()
    for index, ((addr, size), buf) in enumerate(zip(ranges, bufs)):
        local[index].iov_base = ctypes.addressof(buf)
        local[index].iov_len = size
        remote[index].iov_base = addr
        remote[index].iov_len = size
    copied = LIBC.process_vm_readv(pid, local, len(ranges), remote, len(ranges), 0)
    if copied < 0:
        return [None] * len(ranges)
    out: list[bytes | None] = []
    consumed = 0
    for (_addr, size), buf in zip(ranges, bufs):
        if consumed + size <= copied:
            out.append(bytes(buf.raw))
            consumed += size
        else:
            out.append(None)
    return out


def merge_ranges(spans: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Une intervalos [início, fim) sobrepostos ou adjacentes."""
    ordered = sorted((start, end) for start, end in spans if end > start)
    if not ordered:
        return []
    merged = [list(ordered[0])]
    for start, end in ordered[1:]:
        if start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return [(start, end) for start, end in merged]


class ProcessSource:
    def __init__(self, pid: int):
        self.pid = pid

    def read_many(self, ranges: list[tuple[int, int]]) -> list[bytes | None]:
        clean = []
        reject = []
        for addr, size in ranges:
            bad = addr <= 0 or size <= 0 or size > 0x100000
            reject.append(bad)
            if not bad:
                clean.append((addr, size))
        got = read_many(self.pid, clean)
        out: list[bytes | None] = []
        cursor = 0
        for bad in reject:
            if bad:
                out.append(None)
            else:
                out.append(got[cursor])
                cursor += 1
        return out


class ArraySource:
    """Memória falsa para teste. `base` é o endereço do PhysicsRig."""

    def __init__(self, base: int, blob: bytearray | bytes):
        self.base = base
        self.blob = bytearray(blob)

    def read_many(self, ranges: list[tuple[int, int]]) -> list[bytes | None]:
        out: list[bytes | None] = []
        for addr, size in ranges:
            off = addr - self.base
            if off < 0 or size < 0 or off + size > len(self.blob):
                out.append(None)
            else:
                out.append(bytes(self.blob[off : off + size]))
        return out


def _proc_text(pid: int, name: str) -> str:
    try:
        with open(f"/proc/{pid}/{name}", "r", encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


def process_matches(pid: int, token: str) -> bool:
    folded = token.strip().lower()
    aliases = {folded, "dirtrally2.exe"}
    if "dirt" in folded and "rally" in folded:
        aliases.add("dirtrally2.exe")
    comm = _proc_text(pid, "comm").strip().lower()
    if comm and (comm in aliases or folded in comm):
        return True
    maps = _proc_text(pid, "maps").lower()
    cmdline = _proc_text(pid, "cmdline").lower()
    haystack = maps + "\n" + cmdline
    return any(alias and alias in haystack for alias in aliases)


def find_pids(token: str) -> list[int]:
    found = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        pid = int(name)
        if process_matches(pid, token):
            found.append(pid)
    return found


def module_base(source: MemorySource, pid: int) -> int | None:
    preferred = None
    first = None
    text = _proc_text(pid, "maps")
    for line in text.splitlines():
        if "dirtrally2.exe" not in line:
            continue
        start = int(line.split("-", 1)[0], 16)
        if first is None:
            first = start
        if start == IMAGE_BASE_PREFERRED:
            preferred = start
            break
    base = preferred if preferred is not None else first
    if base is None:
        return None
    header = source.read_many([(base, 2)])[0]
    if header == b"MZ":
        return base
    return None


def maps_snapshot(pid: int, limit: int = 150_000) -> str:
    text = _proc_text(pid, "maps")
    if len(text) <= limit:
        return text
    return text[:limit] + "\n... truncado ...\n"


def resolve_rig(source: MemorySource, module: int) -> tuple[int, int, int, str]:
    """Devolve car, container, rig e um motivo. rig 0 quando a cadeia falha."""
    car = 0
    for slot in (CAR_SLOT,) + CAR_FALLBACKS:
        raw = source.read_many([(module + slot, 8)])[0]
        if raw is None:
            return 0, 0, 0, "read_fail"
        car = int.from_bytes(raw, "little")
        if car:
            break
    if not car:
        return 0, 0, 0, "null_car"
    raw = source.read_many([(car + CONTAINER_OFF, 8)])[0]
    if raw is None:
        return car, 0, 0, "read_fail"
    container = int.from_bytes(raw, "little")
    if not container:
        return car, 0, 0, "null_container"
    raw = source.read_many([(container + RIG_OFF, 8)])[0]
    if raw is None:
        return car, container, 0, "read_fail"
    rig = int.from_bytes(raw, "little")
    if not rig:
        return car, container, 0, "null_rig"
    check = source.read_many([(rig + SELF_OFF, 4 + COUNT_OFF - SELF_OFF)])[0]
    if check is None:
        return car, container, rig, "read_fail"
    self_ptr = int.from_bytes(check[:8], "little")
    count = int.from_bytes(check[COUNT_OFF - SELF_OFF : COUNT_OFF - SELF_OFF + 4], "little")
    if self_ptr != rig:
        return car, container, rig, "self_mismatch"
    if count != 4:
        return car, container, rig, "count_mismatch"
    return car, container, rig, "ok"


def slice_from_cover(cover: bytes, cover_off: int, offset: int, size: int) -> bytes | None:
    local = offset - cover_off
    if local < 0 or local + size > len(cover):
        return None
    return cover[local : local + size]
