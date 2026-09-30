"""CLI da caixa-preta. Captura primeiro, analisa depois.

Somente leitura de uma sessão local. Não escreve no processo, não altera
RaceNet, placar ou sessão competitiva.
"""

from __future__ import annotations

import argparse
import json
import queue
import socket
import struct
import sys
import threading
import time
from pathlib import Path

from tools.dr2rec.anchors import confirmed_anchors
from tools.dr2rec.analysis.load import load_session
from tools.dr2rec.analysis.pipeline import analyze_to_dir, compare_to_dir, inspect_offset
from tools.dr2rec.format import parse_interval, parse_region
from tools.dr2rec.process_mem import (
    ProcessSource,
    find_pids,
    maps_snapshot,
    module_base,
    resolve_rig,
)
from tools.dr2rec.recorder import build_meta, record_loop

MIN_INTERVAL_NS = 1_000_000
KEY_CODES = {
    "F5": 63,
    "F6": 64,
    "F7": 65,
    "F8": 66,
    "F9": 67,
    "F10": 68,
    "F11": 87,
    "F12": 88,
}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="dr2rec", description="Caixa-preta de sessão do veículo do DiRT Rally 2.0.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    rec = sub.add_parser("record", help="grava uma sessão em .dr2cap")
    rec.add_argument("--process", default="dirtrally2.exe")
    rec.add_argument("--pid", type=int, default=0)
    rec.add_argument("--interval", default="10ms")
    rec.add_argument("--rig", default=None, help="endereço fixo do PhysicsRig; senão a cadeia é relida")
    rec.add_argument("--region", action="append", default=[], help="physicsrig:INÍCIO:FIM, fim exclusivo")
    rec.add_argument("--udp", default=None, help="porta, ou arquivo .bin que também recebe os datagramas crus")
    rec.add_argument("--udp-port", type=int, default=None)
    rec.add_argument("--udp-bind", default="0.0.0.0")
    rec.add_argument("--output", required=True)
    rec.add_argument("--hotkey", default="F8", help="tecla que grava EVENT_MARKER; stdin também aceita um rótulo")
    rec.add_argument("--no-hotkey", action="store_true")
    rec.add_argument("--seconds", type=float, default=None)
    rec.add_argument("--note", default="")

    ana = sub.add_parser("analyze", help="analisa um .dr2cap e escreve o relatório")
    ana.add_argument("session")
    ana.add_argument("--output", default=None)
    ana.add_argument("--exe", default=None, help="dirtrally2.exe local, para o passo de data-flow")
    ana.add_argument("--max-series", type=int, default=36)
    ana.add_argument("--max-lag", type=int, default=5)

    cmp = sub.add_parser("compare", help="compara duas sessões")
    cmp.add_argument("session_a")
    cmp.add_argument("session_b")
    cmp.add_argument("--output", default=None)

    ins = sub.add_parser("inspect", help="mostra um offset ao longo da sessão")
    ins.add_argument("session")
    ins.add_argument("--offset", required=True)

    exp = sub.add_parser("export", help="exporta âncoras em JSON, sem substituir o .dr2cap")
    exp.add_argument("session")
    exp.add_argument("--output", required=True)

    args = parser.parse_args(argv)
    if args.cmd == "record":
        return _record(args)
    if args.cmd == "analyze":
        result = analyze_to_dir(args.session, args.output, exe=args.exe, max_series=args.max_series, max_lag=args.max_lag)
        print(result["report_dir"])
        return 0
    if args.cmd == "compare":
        result = compare_to_dir(args.session_a, args.session_b, args.output)
        print(result["report_dir"])
        return 0
    if args.cmd == "inspect":
        print(inspect_offset(args.session, int(args.offset, 0)))
        return 0
    if args.cmd == "export":
        _export(args.session, args.output)
        return 0
    return 2


def _record(args) -> int:
    interval = parse_interval(args.interval)
    if interval < MIN_INTERVAL_NS:
        raise SystemExit("intervalo mínimo é 1ms")
    regions = [parse_region(item) for item in args.region]
    udp_port, udp_path = _udp_args(args.udp, args.udp_port)
    pid = _select_pid(args.process, args.pid)
    source = ProcessSource(pid)
    base = module_base(source, pid)
    if base is None:
        raise SystemExit(f"pid {pid} sem dirtrally2.exe legível")
    fixed = int(args.rig, 0) if args.rig else 0
    if fixed:
        rig = fixed
        note = "fixed"
    else:
        _car, _container, rig, note = resolve_rig(source, base)
        if note != "ok":
            raise SystemExit(f"PhysicsRig não resolvido ({note}). Passe --rig se a cadeia ainda não está viva.")
    anchors = confirmed_anchors()
    meta = build_meta(
        executable="dirtrally2.exe",
        pid=pid,
        module_base=base,
        physics_rig=rig,
        interval_ns=interval,
        regions=regions,
        anchors=anchors,
        udp_port=udp_port,
        note=args.note,
        maps=maps_snapshot(pid),
    )
    print(
        f"pid {pid}  base 0x{base:X}  rig 0x{rig:X} ({note})  "
        f"intervalo {interval / 1e6:.3f} ms  -> {args.output}",
        file=sys.stderr,
    )
    print(
        "Marcador: digite um rótulo e Enter. Linha vazia grava EVENT_MARKER. "
        f"Hotkey {args.hotkey} também grava EVENT_MARKER.",
        file=sys.stderr,
    )
    if args.hotkey.upper() == "F8":
        print(
            "F8 no mod loader recarrega dr2hook_core.dll. Use --hotkey F9 se a dxgi.dll estiver no processo.",
            file=sys.stderr,
        )
    stop = threading.Event()
    markers: queue.Queue = queue.Queue()
    if not args.no_hotkey:
        _start_hotkey(args.hotkey, markers, stop)
    _start_stdin(markers, stop)
    latest = [b"", 0]
    if udp_port:
        _start_udp(udp_port, args.udp_bind, udp_path, latest, stop)
        print(f"UDP em {args.udp_bind}:{udp_port}", file=sys.stderr)

    def resolve():
        _car, _container, found, found_note = resolve_rig(source, base)
        return found if found_note == "ok" else 0

    if args.seconds:
        threading.Timer(args.seconds, stop.set).start()
    result = record_loop(
        args.output,
        meta,
        source,
        rig=fixed,
        regions=regions,
        anchors=anchors,
        interval_ns=interval,
        stop=stop,
        marker_queue=markers,
        udp_latest=latest if udp_port else None,
        resolve_rig=None if fixed else resolve,
    )
    print(
        f"amostras {result.samples}  marcadores {result.markers}  atrasos {result.late}  descartes {result.dropped}",
        file=sys.stderr,
    )
    return 0


def _select_pid(token: str, pid: int) -> int:
    if pid:
        return pid
    found = find_pids(token)
    if not found:
        raise SystemExit(f"nenhum processo compatível com {token!r}")
    if len(found) > 1:
        raise SystemExit("vários processos: " + ", ".join(str(item) for item in found) + ". Passe --pid.")
    return found[0]


def _udp_args(udp: str | None, port: int | None) -> tuple[int | None, str | None]:
    if udp is None and port is None:
        return None, None
    path = None
    chosen = port
    if udp:
        if udp.isdigit():
            chosen = int(udp)
        else:
            path = udp
            chosen = port or 20777
    if chosen is None or not (1 <= chosen <= 65535):
        raise SystemExit("porta UDP inválida")
    return chosen, path


def _start_stdin(markers: queue.Queue, stop: threading.Event) -> None:
    def run() -> None:
        while not stop.is_set():
            line = sys.stdin.readline()
            if line == "":
                return
            label = line.strip() or "EVENT_MARKER"
            markers.put((time.monotonic_ns(), label))

    threading.Thread(target=run, name="dr2rec-stdin", daemon=True).start()


def _start_hotkey(name: str, markers: queue.Queue, stop: threading.Event) -> None:
    code = KEY_CODES.get(name.upper())
    if code is None:
        print(f"hotkey {name} não mapeada; use o stdin", file=sys.stderr)
        return

    def run() -> None:
        import glob
        import select

        devices = []
        for path in sorted(glob.glob("/dev/input/event*")):
            try:
                devices.append(open(path, "rb", buffering=0))
            except OSError:
                continue
        if not devices:
            print("hotkey indisponível sem leitura de /dev/input. Use o stdin.", file=sys.stderr)
            return
        try:
            while not stop.is_set():
                ready, _, _ = select.select(devices, [], [], 0.2)
                for handle in ready:
                    data = handle.read(24)
                    if len(data) < 24:
                        continue
                    _sec, _usec, kind, key, value = struct.unpack("llHHi", data)
                    if kind == 1 and key == code and value == 1:
                        markers.put((time.monotonic_ns(), "EVENT_MARKER"))
        finally:
            for handle in devices:
                handle.close()

    threading.Thread(target=run, name="dr2rec-hotkey", daemon=True).start()


def _start_udp(port: int, bind: str, path: str | None, latest: list, stop: threading.Event) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind, port))
    sock.settimeout(0.3)
    sidecar = open(path, "wb") if path else None

    def run() -> None:
        try:
            while not stop.is_set():
                try:
                    data, _addr = sock.recvfrom(4096)
                except socket.timeout:
                    continue
                stamp = time.monotonic_ns()
                latest[0] = data
                latest[1] = stamp
                if sidecar is not None:
                    sidecar.write(struct.pack("<QI", stamp, len(data)))
                    sidecar.write(data)
        finally:
            sock.close()
            if sidecar is not None:
                sidecar.close()

    threading.Thread(target=run, name="dr2rec-udp", daemon=True).start()


def _export(path: str, output: str) -> None:
    session = load_session(path)
    anchors = []
    cursor = 0
    for anchor in session.anchors:
        blob = session.known[:, cursor : cursor + anchor.size]
        cursor += anchor.size
        hex_rows = [row.tobytes().hex() for row in blob]
        anchors.append(
            {
                "name": anchor.name,
                "offset": anchor.offset,
                "size": anchor.size,
                "kind": anchor.kind,
                "note": anchor.note,
                "hex": hex_rows,
            }
        )
    payload = {
        "note": "O hex é o byte cru. Qualquer float derivado daqui pode ser recalculado.",
        "meta": {key: value for key, value in session.meta.items() if key != "maps"},
        "markers": [
            {"timestamp_ns": marker.timestamp_ns, "after_sequence": marker.after_sequence, "label": marker.label}
            for marker in session.markers
        ],
        "anchors": anchors,
    }
    Path(output).write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
