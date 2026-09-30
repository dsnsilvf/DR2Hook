#!/usr/bin/env python3
"""Captura pareada, somente leitura, de UDP e dos quatro escalares +0x1504.

Não escreve na memória do jogo. Cada datagrama dispara uma releitura da
cadeia do PhysicsRig. Amostra com rig diferente começa outra série.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import os
import select
import socket
import struct
import sys
import threading
import time

IMAGE_BASE_PREFERRED = 0x140000000
CAR_SLOT = 0x1681CE8
CAR_FALLBACKS = (0x15A4B00, 0x15A9760)
CONTAINER_OFF = 0x30
RIG_OFF = 0x08
SELF_OFF = 0x12C0
COUNT_OFF = 0x12D0
SCALAR_BASE = 0x1504
SCALAR_STRIDE = 0x420
SCALAR_SPAN = 0xC60 + 4  # até o fim do float D

UDP_TIME = 0
UDP_SPEED = 28
UDP_VEL = 32
UDP_SUSP = (68, 72, 76, 80)  # RL, RR, FL, FR no pacote
UDP_WHEEL_VEL = (100, 104, 108, 112)
MIN_PACKET = 256

PHASES = ("T0", "T1", "T2", "T3", "T4", "T5", "T6")

COLUMNS = [
    "series_id",
    "host_ns",
    "udp_time",
    "phase",
    "car_ptr",
    "container_ptr",
    "rig_ptr",
    "rig_note",
    "speed",
    "vel_x",
    "vel_y",
    "vel_z",
    "udp_rl",
    "udp_rr",
    "udp_fl",
    "udp_fr",
    "wv_rl",
    "wv_rr",
    "wv_fl",
    "wv_fr",
    "mem_a",
    "mem_b",
    "mem_c",
    "mem_d",
]


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


def read_mem(pid: int, addr: int, size: int) -> bytes | None:
    if addr <= 0 or size <= 0:
        return None
    buf = ctypes.create_string_buffer(size)
    local = IOVec(ctypes.addressof(buf), size)
    remote = IOVec(addr, size)
    n = LIBC.process_vm_readv(pid, ctypes.byref(local), 1, ctypes.byref(remote), 1, 0)
    if n != size:
        return None
    return buf.raw


def u64(buf: bytes, off: int = 0) -> int:
    return struct.unpack_from("<Q", buf, off)[0]


def f32(buf: bytes, off: int) -> float:
    return struct.unpack_from("<f", buf, off)[0]


def candidate_pids() -> list[int]:
    found = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        pid = int(name)
        try:
            mapped = "dirtrally2.exe" in open(
                f"/proc/{pid}/maps", "r", encoding="utf-8", errors="replace"
            ).read()
        except OSError:
            continue
        if mapped:
            found.append(pid)
    return found


def module_base(pid: int) -> int | None:
    preferred = None
    first = None
    try:
        lines = open(f"/proc/{pid}/maps", "r", encoding="utf-8", errors="replace")
    except OSError:
        return None
    with lines:
        for line in lines:
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
    header = read_mem(pid, base, 2)
    if header == b"MZ":
        return base
    return None


def attach() -> tuple[int, int]:
    errors = []
    for pid in candidate_pids():
        base = module_base(pid)
        if base is None:
            errors.append(f"pid {pid}: leitura recusada ou cabeçalho MZ ausente")
            continue
        return pid, base
    raise SystemExit(
        "Nenhum dirtrally2.exe legível.\n" + "\n".join(errors)
    )


def resolve_rig(pid: int, base: int) -> tuple[int, int, int, str]:
    """Devolve car, container, rig e um motivo. rig==0 quando a cadeia falha."""
    slots = (CAR_SLOT,) + CAR_FALLBACKS
    car = 0
    for slot in slots:
        raw = read_mem(pid, base + slot, 8)
        if raw is None:
            return 0, 0, 0, "read_fail"
        car = u64(raw)
        if car:
            break
    if not car:
        return 0, 0, 0, "null_car"
    raw = read_mem(pid, car + CONTAINER_OFF, 8)
    if raw is None:
        return car, 0, 0, "read_fail"
    container = u64(raw)
    if not container:
        return car, 0, 0, "null_container"
    raw = read_mem(pid, container + RIG_OFF, 8)
    if raw is None:
        return car, container, 0, "read_fail"
    rig = u64(raw)
    if not rig:
        return car, container, 0, "null_rig"
    check = read_mem(pid, rig + SELF_OFF, COUNT_OFF - SELF_OFF + 4)
    if check is None:
        return car, container, 0, "read_fail"
    self_ptr = u64(check, 0)
    count = struct.unpack_from("<I", check, COUNT_OFF - SELF_OFF)[0]
    if self_ptr != rig:
        return car, container, rig, "self_mismatch"
    if count != 4:
        return car, container, rig, "count_mismatch"
    return car, container, rig, "ok"


def read_scalars(pid: int, rig: int) -> tuple[float, float, float, float] | None:
    blob = read_mem(pid, rig + SCALAR_BASE, SCALAR_SPAN)
    if blob is None:
        return None
    return tuple(f32(blob, i * SCALAR_STRIDE) for i in range(4))  # type: ignore[return-value]


def parse_packet(data: bytes) -> dict[str, float] | None:
    if len(data) < MIN_PACKET:
        return None
    out = {
        "udp_time": f32(data, UDP_TIME),
        "speed": f32(data, UDP_SPEED),
        "vel_x": f32(data, UDP_VEL),
        "vel_y": f32(data, UDP_VEL + 4),
        "vel_z": f32(data, UDP_VEL + 8),
    }
    for name, off in zip(("udp_rl", "udp_rr", "udp_fl", "udp_fr"), UDP_SUSP):
        out[name] = f32(data, off)
    for name, off in zip(("wv_rl", "wv_rr", "wv_fl", "wv_fr"), UDP_WHEEL_VEL):
        out[name] = f32(data, off)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="CSV de saída")
    parser.add_argument("--port", type=int, default=20777)
    parser.add_argument("--bind", default="0.0.0.0")
    args = parser.parse_args()

    pid, base = attach()
    print(f"pid {pid}  base 0x{base:x}", file=sys.stderr)
    print(
        "Fases no stdin, uma por linha: "
        + " ".join(PHASES)
        + "  (T0 parado, T1 arrancada, T2 frenagem, T3 aceleração, "
        "T4 compressão, T5 extensão, T6 repouso)",
        file=sys.stderr,
    )

    phase = {"name": ""}
    stop = threading.Event()

    def stdin_loop() -> None:
        while not stop.is_set():
            ready, _, _ = select.select([sys.stdin], [], [], 0.2)
            if not ready:
                continue
            line = sys.stdin.readline()
            if line == "":
                break
            token = line.strip().upper()
            if token in PHASES:
                phase["name"] = token
                print(f"fase {token}", file=sys.stderr)

    threading.Thread(target=stdin_loop, daemon=True).start()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    sock.settimeout(0.5)

    series_id = 0
    last_rig = None
    series_open = False
    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
    with open(args.out, "w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=COLUMNS)
        writer.writeheader()
        print(f"ouvindo udp {args.bind}:{args.port} -> {args.out}", file=sys.stderr)
        try:
            while True:
                try:
                    data, _addr = sock.recvfrom(2048)
                except socket.timeout:
                    continue
                pkt = parse_packet(data)
                if pkt is None:
                    continue
                host_ns = time.monotonic_ns()
                car, container, rig, note = resolve_rig(pid, base)
                if note == "ok":
                    if not series_open or rig != last_rig:
                        if series_open or last_rig is not None:
                            series_id += 1
                        print(f"série {series_id} rig 0x{rig:x}", file=sys.stderr)
                        last_rig = rig
                    series_open = True
                else:
                    if series_open:
                        print(f"rig inválido ({note}); série {series_id} encerrada", file=sys.stderr)
                    series_open = False
                scalars = read_scalars(pid, rig) if note == "ok" else None
                if scalars is None:
                    scalars = (float("nan"),) * 4
                    if note == "ok":
                        note = "read_fail"
                row = {
                    "series_id": series_id,
                    "host_ns": host_ns,
                    "phase": phase["name"],
                    "car_ptr": f"0x{car:x}",
                    "container_ptr": f"0x{container:x}",
                    "rig_ptr": f"0x{rig:x}",
                    "rig_note": note,
                    "mem_a": scalars[0],
                    "mem_b": scalars[1],
                    "mem_c": scalars[2],
                    "mem_d": scalars[3],
                }
                row.update(pkt)
                writer.writerow(row)
                fh.flush()
        except KeyboardInterrupt:
            print("encerrado", file=sys.stderr)
        finally:
            stop.set()


if __name__ == "__main__":
    main()
