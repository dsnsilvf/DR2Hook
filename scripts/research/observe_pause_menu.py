#!/usr/bin/env python3
"""Observa o objeto StatePauseScreen. Somente leitura.

A tela de pausa ao vivo usa a vtable 0x141250b50 e a chave pause_menu.
O script localiza essas instâncias e grava o bloco de 0x130 bytes.
Não escreve no processo.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import struct
import sys
import time

VTABLE = 0x141250B50
OBJECT_SIZE = 0x130
MAX_REGION = 256 * 1024 * 1024


class IOV(ctypes.Structure):
    _fields_ = [("b", ctypes.c_uint64), ("n", ctypes.c_uint64)]


LIBC = ctypes.CDLL(None, use_errno=True)
LIBC.process_vm_readv.restype = ctypes.c_ssize_t


def game_pid() -> int:
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        pid = int(name)
        path = f"/proc/{pid}/maps"
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        if "/dirtrally2.exe" in text and "140000000-140001000" in text:
            return pid
    raise SystemExit("dirtrally2.exe não está mapeado em 0x140000000")


def read_mem(pid: int, addr: int, size: int) -> bytes:
    buf = ctypes.create_string_buffer(size)
    local = IOV(ctypes.addressof(buf), size)
    remote = IOV(addr, size)
    n = LIBC.process_vm_readv(pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0)
    if n < 0:
        return b""
    return buf.raw[:n]


def cstr(pid: int, addr: int, limit: int = 64) -> str:
    if addr < 0x10000:
        return ""
    raw = read_mem(pid, addr, limit)
    text = raw.split(b"\x00", 1)[0]
    if not text or any(c < 32 or c > 126 for c in text):
        return ""
    return text.decode()


def find_objects(pid: int) -> list[int]:
    sig = VTABLE.to_bytes(8, "little")
    hits: list[int] = []
    for line in open(f"/proc/{pid}/maps", encoding="utf-8", errors="replace"):
        parts = line.split()
        if "rw" not in parts[1]:
            continue
        start_s, end_s = parts[0].split("-")
        start, end = int(start_s, 16), int(end_s, 16)
        size = end - start
        if size < 8 or size > MAX_REGION:
            continue
        data = read_mem(pid, start, size)
        off = 0
        while True:
            j = data.find(sig, off)
            if j < 0:
                break
            if j % 8 == 0:
                hits.append(start + j)
            off = j + 1
    return hits


def dump_object(pid: int, addr: int) -> dict:
    raw = read_mem(pid, addr, OBJECT_SIZE)
    if len(raw) < OBJECT_SIZE:
        raise OSError(f"leitura curta em {addr:#x}")
    fields = {}
    for off in (0x38, 0x50, 0x58, 0x88, 0x90, 0xC8, 0xD0, 0xF8, 0x100, 0x118, 0x11C, 0x120, 0x124, 0x128):
        if off in (0x50, 0x11C, 0x124):
            fields[f"+{off:03x}"] = struct.unpack_from("<i", raw, off)[0]
        elif off in (0x118,):
            fields[f"+{off:03x}"] = struct.unpack_from("<H", raw, off)[0]
        elif off == 0x128:
            fields[f"+{off:03x}"] = raw[off]
        else:
            fields[f"+{off:03x}"] = struct.unpack_from("<Q", raw, off)[0]
    key_ptr = fields["+100"]
    return {
        "addr": addr,
        "key": cstr(pid, key_ptr),
        "fields": fields,
        "raw_hex": raw.hex(),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="snapshot")
    parser.add_argument("--out", default="")
    args = parser.parse_args()
    pid = game_pid()
    objs = [dump_object(pid, addr) for addr in find_objects(pid)]
    payload = {"pid": pid, "label": args.label, "time": time.time(), "objects": objs}
    text = json.dumps(payload, indent=2)
    if args.out:
        os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
        open(args.out, "w", encoding="utf-8").write(text + "\n")
        print(f"gravou {args.out}")
    print(f"pid {pid} instâncias {len(objs)} label {args.label}")
    for obj in objs:
        print(f"  {obj['addr']:#x} key {obj['key']!r}")
        for name in ("+038", "+050", "+118", "+11c", "+120", "+124", "+128"):
            print(f"    {name} {obj['fields'][name]}")
    if not objs:
        sys.exit(1)


if __name__ == "__main__":
    main()
