#!/usr/bin/env python3
"""Acompanha o foco do menu de pausa a partir do binding ui.pause_menu.

Reencontra tudo a cada execução: a string internada ui.pause_menu, a tela
smart_hub que aponta para ela (+0x60), o vetor de itens (+0xa0..+0xa8,
pares {nome, item}) e o objeto de navegação (6º filho do vetor em
+0x78..+0x80). Imprime uma linha quando o foco ou o estado dos itens muda.
Uma linha digitada aqui vira marcador. Ctrl+C encerra.

Não escreve no processo do jogo.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import select
import struct
import sys
import time

SCREEN_VT = 0x1413D62D0
ITEM_VT = 0x1413D8058
NAV_VT = 0x1413D6858
ACTION_STRIDE = 0xE0
HEAP_FLOOR = 0x10000000
CHUNK = 8 * 1024 * 1024
PATH = b"ui.pause_menu\x00"


class IOV(ctypes.Structure):
    _fields_ = [("b", ctypes.c_uint64), ("n", ctypes.c_uint64)]


LIBC = ctypes.CDLL(None, use_errno=True)
LIBC.process_vm_readv.restype = ctypes.c_ssize_t


def game_pid() -> int:
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            text = open(f"/proc/{name}/maps", encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        if "/dirtrally2.exe" in text and "140000000-140001000" in text:
            return int(name)
    raise SystemExit("dirtrally2.exe não está mapeado em 0x140000000")


def read_mem(pid: int, addr: int, size: int) -> bytes:
    if addr < 0x10000 or size <= 0:
        return b""
    buf = ctypes.create_string_buffer(size)
    n = LIBC.process_vm_readv(
        pid, ctypes.byref(IOV(ctypes.addressof(buf), size)), 1, ctypes.byref(IOV(addr, size)), 1, 0
    )
    return buf.raw[: max(n, 0)]


def u64(pid: int, addr: int) -> int:
    blob = read_mem(pid, addr, 8)
    return struct.unpack("<Q", blob)[0] if len(blob) == 8 else 0


def u32(pid: int, addr: int) -> int:
    blob = read_mem(pid, addr, 4)
    return struct.unpack("<I", blob)[0] if len(blob) == 4 else 0


def text_at(pid: int, addr: int) -> str:
    raw = read_mem(pid, addr, 64).split(b"\x00", 1)[0]
    if raw and all(32 <= byte < 127 for byte in raw):
        return raw.decode()
    return ""


def interned(pid: int, header: int) -> str:
    return text_at(pid, header + 0x10) if header else ""


def heap_spans(pid: int) -> list[tuple[int, int]]:
    spans = []
    for line in open(f"/proc/{pid}/maps", encoding="utf-8", errors="replace"):
        parts = line.split()
        if "rw" not in parts[1] or len(parts) >= 6:
            continue
        start, end = (int(x, 16) for x in parts[0].split("-"))
        if HEAP_FLOOR <= start < 0x800000000 and end - start <= 512 * 1024 * 1024:
            spans.append((start, end))
    return spans


def scan(pid: int, spans: list[tuple[int, int]], needle: bytes, aligned: bool) -> list[int]:
    hits = []
    for start, end in spans:
        pos = start
        while pos < end:
            size = min(CHUNK, end - pos)
            data = read_mem(pid, pos, size)
            off = 0
            while True:
                found = data.find(needle, off)
                if found < 0:
                    break
                if not aligned or (pos + found) % 8 == 0:
                    hits.append(pos + found)
                off = found + 1
            pos += size if size <= len(needle) else size - len(needle) + 1
    return hits


def discover(pid: int) -> dict:
    spans = heap_spans(pid)
    for text in scan(pid, spans, PATH, aligned=False):
        header = text - 0x10
        for ref in scan(pid, spans, header.to_bytes(8, "little"), aligned=True):
            screen = ref - 0x60
            if u64(pid, screen) != SCREEN_VT:
                continue
            items = []
            begin, end = u64(pid, screen + 0xA0), u64(pid, screen + 0xA8)
            for slot in range(begin, end, 0x10):
                name = interned(pid, u64(pid, slot))
                item = u64(pid, slot + 8)
                if u64(pid, item) == ITEM_VT:
                    items.append({"name": name, "addr": item})
            children = u64(pid, screen + 0x78), u64(pid, screen + 0x80)
            nav = 0
            for slot in range(children[0], children[1], 8):
                child = u64(pid, slot)
                if u64(pid, child) == NAV_VT:
                    nav = child
            for index, item in enumerate(items):
                action = screen + ACTION_STRIDE + ACTION_STRIDE * index
                ptr = u64(pid, action + 0xA0)
                item["action"] = interned(pid, ptr) or text_at(pid, ptr)
            return {"screen": screen, "items": items, "nav": nav}
    raise SystemExit("ui.pause_menu não está montado. Abra o menu de pausa e rode de novo.")


def snapshot(pid: int, found: dict) -> dict:
    by_addr = {item["addr"]: item for item in found["items"]}
    nav = found["nav"]

    def label(ptr: int) -> str:
        item = by_addr.get(ptr)
        return f"{item['name']}({item['action']})" if item else hex(ptr)

    return {
        "focus": label(u64(pid, nav + 0x50)),
        "default": label(u64(pid, nav + 0x60)),
        "prev": label(u64(pid, nav + 0x68)),
        "scr_c0": u32(pid, found["screen"] + 0xC0),
        "states": [u32(pid, item["addr"] + 0x230) for item in found["items"]],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interval", type=float, default=0.02)
    args = parser.parse_args()
    pid = game_pid()
    print(f"pid {pid}. Procurando ui.pause_menu...", flush=True)
    found = discover(pid)
    print(f"tela {found['screen']:#x}  navegação {found['nav']:#x}", flush=True)
    for item in found["items"]:
        visible = "visível" if u32(pid, item["addr"] + 0x230) != 2 else "oculto"
        print(f"  {item['addr']:#x} {item['name']:<8} {item['action']:<20} {visible}", flush=True)
    print("Mova o destaque. Uma linha aqui marca o instante. Ctrl+C encerra.", flush=True)
    t0 = time.time()
    last = None
    try:
        while True:
            if select.select([sys.stdin], [], [], 0)[0]:
                line = sys.stdin.readline()
                if line.strip():
                    print(f"{time.time() - t0:7.2f}s [{line.strip()}]", flush=True)
            snap = snapshot(pid, found)
            if snap != last:
                states = " ".join(str(value) for value in snap["states"])
                print(
                    f"{time.time() - t0:7.2f}s foco {snap['focus']}  padrão {snap['default']}  "
                    f"anterior {snap['prev']}  tela+c0 {snap['scr_c0']:#x}  +230 [{states}]",
                    flush=True,
                )
                last = snap
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
