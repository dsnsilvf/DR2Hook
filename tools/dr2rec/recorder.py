"""Recorder. Durante a sessão só copia bytes e carimbo de tempo.

A interpretação fica no analyzer. O laço evita trabalho por amostra: uma
leitura, uma cópia para o buffer e uma fila para a thread que escreve.
"""

from __future__ import annotations

import queue
import threading
import time
from dataclasses import dataclass

from tools.dr2rec.anchors import Anchor, confirmed_anchors
from tools.dr2rec.format import ST_KNOWN, ST_LATE, ST_REGION, ST_UDP, CaptureWriter, SampleRecord
from tools.dr2rec.process_mem import ArraySource, MemorySource, merge_ranges, slice_from_cover


@dataclass(frozen=True)
class ReadPlan:
    covers: tuple[tuple[int, int], ...]
    region_slices: tuple[tuple[int, int, int], ...]
    anchor_slices: tuple[tuple[int, int, int], ...]
    region_bytes: int
    known_bytes: int


def plan_reads(regions: list[dict], anchors: list[Anchor]) -> ReadPlan:
    spans = [(int(region["offset"]), int(region["offset"]) + int(region["size"])) for region in regions]
    spans.extend((item.offset, item.offset + item.size) for item in anchors)
    covers = tuple(merge_ranges(spans))
    region_slices = []
    cursor = 0
    for region in regions:
        region_slices.append((int(region["offset"]), int(region["size"]), cursor))
        cursor += int(region["size"])
    anchor_slices = []
    cursor = 0
    for item in anchors:
        anchor_slices.append((item.offset, item.size, cursor))
        cursor += item.size
    return ReadPlan(
        covers=covers,
        region_slices=tuple(region_slices),
        anchor_slices=tuple(anchor_slices),
        region_bytes=sum(int(region["size"]) for region in regions),
        known_bytes=cursor,
    )


def _fill(source: MemorySource, rig: int, plan: ReadPlan) -> tuple[bytes, bytes, int]:
    ranges = [(rig + start, end - start) for start, end in plan.covers]
    pieces = source.read_many(ranges) if ranges else []
    status = 0
    region = bytearray(plan.region_bytes)
    known = bytearray(plan.known_bytes)
    region_ok = True
    known_ok = True

    def take(offset: int, size: int) -> bytes | None:
        for (start, end), blob in zip(plan.covers, pieces):
            if blob is None:
                continue
            if start <= offset and offset + size <= end:
                return blob[offset - start : offset - start + size]
        return None

    for offset, size, dest in plan.region_slices:
        blob = take(offset, size)
        if blob is None:
            region_ok = False
            continue
        region[dest : dest + size] = blob
    for offset, size, dest in plan.anchor_slices:
        blob = take(offset, size)
        if blob is None:
            known_ok = False
            continue
        known[dest : dest + size] = blob
    if region_ok and plan.region_bytes:
        status |= ST_REGION
    elif plan.region_bytes == 0:
        status |= ST_REGION
    if known_ok and plan.known_bytes:
        status |= ST_KNOWN
    return bytes(region), bytes(known), status


@dataclass
class RecordResult:
    samples: int
    markers: int
    dropped: int
    late: int
    rig: int


def record_loop(
    path: str,
    meta: dict,
    source: MemorySource,
    *,
    rig: int,
    regions: list[dict],
    anchors: list[Anchor] | None = None,
    interval_ns: int,
    stop: threading.Event | None = None,
    marker_queue: "queue.Queue[tuple[int, str]] | None" = None,
    udp_latest: list | None = None,
    max_samples: int | None = None,
    resolve_rig=None,
    clock_ns=None,
    sleep=None,
) -> RecordResult:
    """Grava até `stop`, `max_samples` ou interrupção.

    `udp_latest`, quando existe, é uma lista `[bytes, timestamp_ns]` atualizada
    por outra thread. O laço só lê essa referência.
    """
    anchors = list(anchors if anchors is not None else confirmed_anchors())
    plan = plan_reads(regions, anchors)
    clock_ns = clock_ns or time.monotonic_ns
    sleep = sleep or time.sleep
    stop = stop or threading.Event()
    writer = CaptureWriter(path, meta, interval_ns)
    write_q: queue.Queue = queue.Queue(maxsize=512)
    dropped = 0
    late = 0

    def writer_thread() -> None:
        pending = 0
        while True:
            item = write_q.get()
            if item is None:
                writer.close(dropped=dropped, timestamp_ns=clock_ns())
                return
            kind, payload = item
            if kind == "sample":
                writer.add_sample(payload)
            else:
                writer.add_marker(payload)
            pending += 1
            if pending >= 32:
                pending = 0

    thread = threading.Thread(target=writer_thread, name="dr2rec-writer", daemon=True)
    thread.start()
    sequence = 0
    current_rig = rig
    next_deadline = clock_ns()
    try:
        while not stop.is_set():
            now = clock_ns()
            if interval_ns > 0 and now > next_deadline + interval_ns:
                late += 1
            if resolve_rig is not None and rig == 0:
                current_rig = int(resolve_rig() or 0)
            if marker_queue is not None:
                while True:
                    try:
                        mark_t, label = marker_queue.get_nowait()
                    except queue.Empty:
                        break
                    _enqueue(write_q, ("marker", (mark_t, sequence - 1 if sequence else 0, label)))
            status = 0
            region = bytes(plan.region_bytes)
            known = bytes(plan.known_bytes)
            if current_rig:
                region, known, status = _fill(source, current_rig, plan)
            udp = b""
            udp_t = 0
            if udp_latest is not None and udp_latest[0]:
                udp = bytes(udp_latest[0])
                udp_t = int(udp_latest[1] or 0)
                status |= ST_UDP
            if interval_ns > 0 and now > next_deadline + interval_ns:
                status |= ST_LATE
            sample = SampleRecord(now, sequence, current_rig, udp_t, status, region, known, udp)
            if not _enqueue(write_q, ("sample", sample)):
                dropped += 1
            sequence += 1
            if max_samples is not None and sequence >= max_samples:
                break
            if interval_ns <= 0:
                continue
            next_deadline += interval_ns
            delay = next_deadline - clock_ns()
            if delay < 0:
                next_deadline = clock_ns()
            elif delay > 0:
                sleep(delay / 1_000_000_000)
    except KeyboardInterrupt:
        stop.set()
    finally:
        write_q.put(None)
        thread.join()
    return RecordResult(writer.samples, writer.markers, dropped, late, current_rig)


def _enqueue(write_q: queue.Queue, item: tuple) -> bool:
    kind, payload = item
    if kind == "marker":
        mark_t, after, label = payload
        from tools.dr2rec.format import MarkerRecord

        payload = MarkerRecord(mark_t, after, label)
        item = (kind, payload)
    try:
        write_q.put_nowait(item)
        return True
    except queue.Full:
        return False


def build_meta(
    *,
    executable: str,
    pid: int,
    module_base: int,
    physics_rig: int,
    interval_ns: int,
    regions: list[dict],
    anchors: list[Anchor],
    udp_port: int | None,
    note: str,
    maps: str,
) -> dict:
    from tools.dr2rec import RECORDER_VERSION

    return {
        "recorder_version": RECORDER_VERSION,
        "executable": executable,
        "pid": pid,
        "module_base": module_base,
        "physics_rig": physics_rig,
        "started_monotonic_ns": time.monotonic_ns(),
        "started_wall_iso": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "interval_ns": interval_ns,
        "regions": regions,
        "anchors": [item.to_meta() for item in anchors],
        "udp_port": udp_port,
        "note": note,
        "maps": maps,
        "read_only": True,
    }


def copy_anchors_from_image(image: bytes, image_offset: int, anchors: list[Anchor]) -> bytes:
    """Recorte usado pelos testes. A imagem já é o bloco do rig."""
    out = bytearray()
    for item in anchors:
        piece = slice_from_cover(image, image_offset, item.offset, item.size)
        out += piece if piece is not None else bytes(item.size)
    return bytes(out)


def demo_source(rig: int, image: bytes) -> ArraySource:
    return ArraySource(rig, image)
