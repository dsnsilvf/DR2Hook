#!/usr/bin/env python3
"""Grava quem aponta para o StatePauseScreen e as cópias vivas dos rótulos.

O filho em +0x38 é um nó de cena (backbuffer, rigidbody). A lista de itens
não está nele, e as chaves lng_* do bloco de localização não são apontadas
por ponteiro. Este gravador segue os ponteiros que caem na tela e as cópias
das chaves e dos rótulos fora desse bloco. Uma linha neste terminal vira
marcador. Ctrl+C encerra.

Não escreve no processo do jogo.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import select
import struct
import sys
import threading
import time

VTABLE = 0x141250B50
COMBO_VTABLE = 0x1413D8F88
SCREEN_SIZE = 0x130
CHILD_OFF = 0x38
HEAP_FLOOR = 0x10000000
CODE_LO, CODE_HI = 0x140001000, 0x141100000
VT_LO, VT_HI = 0x141000000, 0x142800000
CHUNK = 8 * 1024 * 1024

KEY_SEED = b"lng_pause_menu_button_continue"
TEXT_NEEDLES = (
    b"lng_pause_menu_button_continue",
    b"lng_pause_menu_button_restart",
    b"lng_pause_menu_button_options",
    b"lng_return_to_service_area",
    b"lng_pause_quit_button",
    b"\x00Continuar\x00",
    b"\x00Reiniciar\x00",
    "\x00Opções\x00".encode(),
    "\x00Voltar à área de serviço\x00".encode(),
    "\x00Sair para o menu principal\x00".encode(),
)


class IOV(ctypes.Structure):
    _fields_ = [("b", ctypes.c_uint64), ("n", ctypes.c_uint64)]


LIBC = ctypes.CDLL(None, use_errno=True)
LIBC.process_vm_readv.restype = ctypes.c_ssize_t


def game_pid() -> int:
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        pid = int(name)
        try:
            text = open(f"/proc/{pid}/maps", encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        if "/dirtrally2.exe" in text and "140000000-140001000" in text:
            return pid
    raise SystemExit("dirtrally2.exe não está mapeado em 0x140000000")


def read_mem(pid: int, addr: int, size: int) -> bytes:
    if addr < 0x10000 or size <= 0:
        return b""
    buf = ctypes.create_string_buffer(size)
    local = IOV(ctypes.addressof(buf), size)
    remote = IOV(addr, size)
    n = LIBC.process_vm_readv(pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0)
    if n < 0:
        return b""
    return buf.raw[:n]


def u64(blob: bytes, off: int) -> int:
    if off < 0 or off + 8 > len(blob):
        return 0
    return struct.unpack_from("<Q", blob, off)[0]


def maps_of(pid: int, *, ceiling: int) -> list[tuple[int, int]]:
    spans = []
    for line in open(f"/proc/{pid}/maps", encoding="utf-8", errors="replace"):
        parts = line.split()
        if "rw" not in parts[1] or len(parts) >= 6:
            continue
        start_s, end_s = parts[0].split("-")
        start, end = int(start_s, 16), int(end_s, 16)
        size = end - start
        if size <= 0 or size > ceiling or start < HEAP_FLOOR or start >= 0x800000000:
            continue
        spans.append((start, end))
    return spans


def find_screens(pid: int, spans: list[tuple[int, int]]) -> list[int]:
    sig = VTABLE.to_bytes(8, "little")
    hits = []
    for start, end in spans:
        data = read_mem(pid, start, end - start)
        off = 0
        while True:
            found = data.find(sig, off)
            if found < 0:
                break
            if found % 8 == 0:
                hits.append(start + found)
            off = found + 1
    return hits


def loc_window(pid: int, spans: list[tuple[int, int]]) -> tuple[int, int]:
    for start, end in sorted(spans, key=lambda span: span[1] - span[0], reverse=True):
        pos = start
        while pos < end:
            size = min(CHUNK, end - pos)
            data = read_mem(pid, pos, size)
            found = data.find(KEY_SEED)
            if found >= 0:
                anchor = pos + found
                return anchor - 0x200000, anchor + 0x800000
            pos += size if size <= len(KEY_SEED) else size - len(KEY_SEED) + 1
    return 0, 0


def cstring(pid: int, addr: int) -> str:
    if not (HEAP_FLOOR <= addr < 0x800000000):
        return ""
    blob = read_mem(pid, addr, 96)
    if not blob or blob[0] < 32 or blob[0] >= 127:
        return ""
    raw = blob.split(b"\x00", 1)[0]
    if not raw or not all(32 <= byte < 127 for byte in raw):
        return ""
    return raw.decode()


def label_of(needle: bytes, at_nul: bool) -> str:
    text = needle[1:-1] if at_nul else needle
    try:
        name = text.decode()
    except UnicodeDecodeError:
        name = text.decode("utf-8", "replace")
    return name


def survey(
    pid: int,
    spans: list[tuple[int, int]],
    screens: list[int],
    loc_lo: int,
    loc_hi: int,
) -> tuple[list[dict], list[dict]]:
    owners: list[dict] = []
    copies: list[dict] = []
    seen_refs: set[int] = set()
    seen_text: set[tuple[str, int]] = set()
    copy_count: dict[bytes, int] = {}
    buf = ctypes.create_string_buffer(CHUNK)
    sigs = {screen.to_bytes(8, "little"): screen for screen in screens}

    for start, end in spans:
        pos = start
        while pos < end:
            size = min(CHUNK, end - pos)
            local = IOV(ctypes.addressof(buf), size)
            remote = IOV(pos, size)
            got = LIBC.process_vm_readv(pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0)
            data = buf.raw[: max(got, 0)]
            for sig, screen in sigs.items():
                off = 0
                while True:
                    found = data.find(sig, off)
                    if found < 0:
                        break
                    ref = pos + found
                    off = found + 1
                    if ref % 8 or ref in seen_refs:
                        continue
                    if screen <= ref < screen + SCREEN_SIZE:
                        continue
                    nxt = u64(read_mem(pid, ref + 8, 8), 0)
                    prv = u64(read_mem(pid, ref - 8, 8), 0)
                    if CODE_LO <= nxt < CODE_HI or CODE_LO <= prv < CODE_HI:
                        continue
                    seen_refs.add(ref)
                    name = cstring(pid, nxt) or cstring(pid, prv)
                    window = read_mem(pid, ref - 0x20, 0x48)
                    owners.append(
                        {
                            "ref": ref,
                            "screen": screen,
                            "name": name,
                            "hex": window.hex(),
                        }
                    )
            for needle in TEXT_NEEDLES:
                if copy_count.get(needle, 0) >= 6:
                    continue
                at_nul = needle.startswith(b"\x00")
                off = 0
                while copy_count.get(needle, 0) < 6:
                    found = data.find(needle, off)
                    if found < 0:
                        break
                    addr = pos + found + (1 if at_nul else 0)
                    off = found + 1
                    if loc_lo <= addr < loc_hi:
                        continue
                    name = label_of(needle, at_nul)
                    if (name, addr) in seen_text:
                        continue
                    seen_text.add((name, addr))
                    copy_count[needle] = copy_count.get(needle, 0) + 1
                    copies.append(
                        {
                            "addr": addr,
                            "name": name,
                            "hex": read_mem(pid, addr - 0x20, 0x40).hex(),
                        }
                    )
            pos += size if size <= 16 else size - 15
    return owners, copies


def refresh(pid: int, owners: list[dict], copies: list[dict]) -> None:
    for owner in owners:
        owner["hex"] = read_mem(pid, owner["ref"] - 0x20, 0x48).hex()
        nxt = u64(bytes.fromhex(owner["hex"]), 0x28) if len(owner["hex"]) >= 0x60 else 0
        name = cstring(pid, nxt)
        if name:
            owner["name"] = name
    for copy in copies:
        copy["hex"] = read_mem(pid, copy["addr"] - 0x20, 0x40).hex()


def graphics_widgets(pid: int, spans: list[tuple[int, int]]) -> list[dict]:
    sig = COMBO_VTABLE.to_bytes(8, "little")
    found = []
    seen: set[int] = set()
    for start, end in spans:
        data = read_mem(pid, start, end - start)
        off = 0
        while len(found) < 48:
            at = data.find(sig, off)
            if at < 0:
                break
            off = at + 1
            if at % 8:
                continue
            addr = start + at
            if addr in seen:
                continue
            seen.add(addr)
            header = u64(read_mem(pid, addr - 0x18, 8), 0)
            name = cstring(pid, header + 0x10)
            if "graphics" not in name:
                continue
            raw = read_mem(pid, addr + 0x28, 4)
            value = struct.unpack("<i", raw)[0] if len(raw) == 4 else None
            found.append({"addr": addr, "name": name, "value": value})
    return found


def refresh_graphics(pid: int, widgets: list[dict]) -> None:
    for widget in widgets:
        raw = read_mem(pid, widget["addr"] + 0x28, 4)
        if len(raw) == 4:
            widget["value"] = struct.unpack("<i", raw)[0]


def screen_view(pid: int, screens: list[int]) -> list[dict]:
    rows = []
    for addr in screens:
        blob = read_mem(pid, addr, SCREEN_SIZE)
        if len(blob) < SCREEN_SIZE or u64(blob, 0) != VTABLE:
            continue
        child = u64(blob, CHILD_OFF)
        plus50 = struct.unpack_from("<I", blob, 0x50)[0]
        rows.append({"addr": addr, "child": child, "plus50": plus50, "hex": blob.hex()})
    return rows


def short_combo(name: str) -> str:
    return name.removeprefix("ui.").replace(".list[%u]", "")


def interesting(before: bytes, after: bytes) -> list[str]:
    notes = []
    limit = min(len(before), len(after))
    off = 0
    while off + 4 <= limit and len(notes) < 6:
        if before[off : off + 4] != after[off : off + 4]:
            old = struct.unpack_from("<i", before, off)[0]
            new = struct.unpack_from("<i", after, off)[0]
            if -1 <= old <= 16 or -1 <= new <= 16:
                notes.append(f"+{off:#x} {old}->{new}")
        off += 4
    return notes


def summarize(prev: dict | None, sample: dict) -> str:
    if prev is None:
        named = [o["name"] for o in sample["owners"] if o["name"]]
        label = ", ".join(named[:3]) or "sem nome ao lado"
        kinds: dict[str, int] = {}
        for copy in sample["copies"]:
            kinds[copy["name"]] = kinds.get(copy["name"], 0) + 1
        extra = ", ".join(f"{name} x{count}" for name, count in list(kinds.items())[:4])
        mounted = [s for s in sample["screens"] if s["child"]]
        child = hex(mounted[0]["child"]) if mounted else "nenhum"
        combos = ", ".join(
            f"{short_combo(row['name'])}={row['value']}" for row in sample.get("graphics", [])[:4]
        )
        gfx = f"; gráficos {combos}" if combos else ""
        return f"cena {child}; vizinho {label}; cópias {extra or 'nenhuma'}{gfx}"
    notes = []
    old_screens = {row["addr"]: row for row in prev["screens"]}
    for row in sample["screens"]:
        old = old_screens.get(row["addr"])
        if old is None:
            notes.append(f"tela nova {row['addr']:#x}")
            continue
        if old["child"] != row["child"] or old["plus50"] != row["plus50"]:
            notes.append(f"cena {old['child']:#x}->{row['child']:#x} +50 {old['plus50']}->{row['plus50']}")
        notes.extend(f"tela {note}" for note in interesting(bytes.fromhex(old["hex"]), bytes.fromhex(row["hex"])))
    old_owners = {row["ref"]: row for row in prev["owners"]}
    for row in sample["owners"]:
        old = old_owners.get(row["ref"])
        if old is None:
            notes.append(f"dono novo {row['ref']:#x} {row['name']}")
            continue
        if old["name"] != row["name"] and row["name"]:
            notes.append(f"nome {old['name']}->{row['name']}")
        diffs = interesting(bytes.fromhex(old["hex"]), bytes.fromhex(row["hex"]))
        if diffs:
            notes.append(f"dono {row['ref']:#x} {row['name']} " + " ".join(diffs))
    old_copies = {row["addr"]: row for row in prev["copies"]}
    for row in sample["copies"]:
        old = old_copies.get(row["addr"])
        if old is None:
            notes.append(f"cópia nova {row['name']} {row['addr']:#x}")
            continue
        diffs = interesting(bytes.fromhex(old["hex"]), bytes.fromhex(row["hex"]))
        if diffs:
            notes.append(f"cópia {row['name']} " + " ".join(diffs))
    old_gfx = {row["addr"]: row for row in prev.get("graphics", [])}
    for row in sample.get("graphics", []):
        old = old_gfx.get(row["addr"])
        short = short_combo(row["name"])
        if old is None:
            notes.append(f"combo {short}={row['value']}")
            continue
        if old["value"] != row["value"]:
            notes.append(f"{short} {old['value']}->{row['value']}")
    return "; ".join(notes[:8])


def signature(sample: dict) -> bytes:
    parts = [bytes.fromhex(row["hex"]) for row in sample["screens"]]
    parts.extend(bytes.fromhex(row["hex"]) for row in sample["owners"])
    parts.extend(bytes.fromhex(row["hex"]) for row in sample["copies"])
    parts.append(",".join(f"{row['child']:x}:{row['plus50']}" for row in sample["screens"]).encode())
    parts.append(",".join(f"{row['addr']:x}:{row['value']}" for row in sample.get("graphics", [])).encode())
    return b"\0".join(parts)


def marker_thread(box: list[str], stop: threading.Event) -> None:
    while not stop.is_set():
        ready, _, _ = select.select([sys.stdin], [], [], 0.2)
        if not ready:
            continue
        line = sys.stdin.readline()
        if line == "":
            if sys.stdin.isatty():
                stop.set()
            return
        text = line.strip()
        if text:
            box.append(text)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="/tmp/dr2_pause/session.jsonl")
    parser.add_argument("--interval", type=float, default=0.1)
    args = parser.parse_args()
    pid = game_pid()
    print(f"pid {pid}. Procurando a tela...", flush=True)
    small = maps_of(pid, ceiling=32 * 1024 * 1024)
    wide = maps_of(pid, ceiling=512 * 1024 * 1024)
    screens = find_screens(pid, small)
    if not screens:
        raise SystemExit("nenhum StatePauseScreen. Abra o menu de pausa e rode de novo.")
    print("lendo quem aponta para a tela e as cópias dos rótulos...", flush=True)
    loc_lo, loc_hi = loc_window(pid, wide)
    owners, copies = survey(pid, wide, screens, loc_lo, loc_hi)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    print(f"telas: {', '.join(hex(addr) for addr in screens)}", flush=True)
    for owner in owners:
        name = owner["name"] or "(sem texto no vizinho)"
        print(f"  dono {owner['ref']:#x} da tela {owner['screen']:#x}  {name}", flush=True)
    print(f"cópias fora do bloco de localização: {len(copies)}", flush=True)
    for copy in copies[:8]:
        print(f"  {copy['addr']:#x} {copy['name']}", flush=True)
    print("lendo os combos de gráficos...", flush=True)
    widgets = graphics_widgets(pid, small)
    print(f"combos: {len(widgets)}", flush=True)
    for widget in widgets:
        if "list" in widget["name"] or "title" in widget["name"]:
            print(f"  {widget['addr']:#x} +28={widget['value']}  {widget['name']}", flush=True)
    print(f"gravando em {args.out}", flush=True)
    print("Mude um controle. Uma linha aqui marca o instante. Ctrl+C encerra.", flush=True)

    box: list[str] = []
    stop = threading.Event()
    threading.Thread(target=marker_thread, args=(box, stop), daemon=True).start()
    prev = None
    prev_sig = b""
    written = 0
    t0 = time.time()
    last_beat = t0
    last_mount = None
    with open(args.out, "a", encoding="utf-8") as out:
        out.write(json.dumps({"kind": "start", "pid": pid, "t": t0, "screens": screens}) + "\n")
        out.flush()
        try:
            while not stop.is_set():
                now = time.time()
                rows = screen_view(pid, screens)
                if not rows:
                    screens = find_screens(pid, maps_of(pid, ceiling=32 * 1024 * 1024)) or screens
                    rows = screen_view(pid, screens)
                mount = tuple((row["addr"], row["child"], row["plus50"]) for row in rows)
                if last_mount is not None and mount != last_mount:
                    print("  a cena mudou, procurando de novo...", flush=True)
                    owners, copies = survey(pid, maps_of(pid, ceiling=512 * 1024 * 1024), screens, loc_lo, loc_hi)
                    widgets = graphics_widgets(pid, maps_of(pid, ceiling=32 * 1024 * 1024))
                    print(f"  donos {len(owners)}  cópias {len(copies)}  combos {len(widgets)}", flush=True)
                last_mount = mount
                refresh(pid, owners, copies)
                refresh_graphics(pid, widgets)
                sample = {"screens": rows, "owners": owners, "copies": copies, "graphics": widgets}
                marker = box.pop(0) if box else None
                sig = signature(sample)
                text = summarize(prev, sample) if (sig != prev_sig or marker or prev is None) else ""
                if text or marker:
                    record = {
                        "kind": "sample",
                        "t": now,
                        "dt": round(now - t0, 3),
                        "marker": marker,
                        "screens": rows,
                        "owners": owners,
                        "copies": copies,
                        "graphics": widgets,
                    }
                    out.write(json.dumps(record, ensure_ascii=False) + "\n")
                    out.flush()
                    written += 1
                    tag = f" [{marker}]" if marker else ""
                    print(f"{record['dt']:7.2f}s #{written}{tag}  {text}", flush=True)
                    prev_sig = sig
                    prev = json.loads(json.dumps(sample))
                elif now - last_beat > 2.0:
                    print(f"{now - t0:7.2f}s ouvindo", flush=True)
                    last_beat = now
                time.sleep(args.interval)
        except KeyboardInterrupt:
            pass
    print(f"encerrou. {written} amostras em {args.out}", flush=True)


if __name__ == "__main__":
    main()
