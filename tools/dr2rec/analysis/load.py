"""Carrega a sessão e oferece recortes por offset, sem converter a fonte."""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from tools.dr2rec.anchors import Anchor
from tools.dr2rec.format import EndRecord, MarkerRecord, SampleRecord, load_capture


@dataclass
class RegionArray:
    offset: int
    size: int
    data: np.ndarray


@dataclass
class SessionData:
    meta: dict
    closed: bool
    interval_ns: int
    timestamp: np.ndarray
    sequence: np.ndarray
    rig: np.ndarray
    status: np.ndarray
    udp_timestamp: np.ndarray
    regions: list[RegionArray]
    known: np.ndarray
    anchors: list[Anchor]
    udp: np.ndarray
    udp_len: np.ndarray
    markers: list[MarkerRecord]
    dropped: int
    path: str


@dataclass
class SlotTable:
    offset: np.ndarray
    f32: np.ndarray
    i32: np.ndarray
    changed: np.ndarray
    change_count: np.ndarray
    samples: int = field(default=0)

    def index_of(self, offset: int) -> int | None:
        found = np.flatnonzero(self.offset == offset)
        if found.size == 0:
            return None
        return int(found[0])


def _anchors_from_meta(meta: dict) -> list[Anchor]:
    rows = meta.get("anchors") or []
    anchors = []
    for row in rows:
        anchors.append(
            Anchor(
                name=row["name"],
                offset=int(row["offset"]),
                size=int(row["size"]),
                kind=row.get("kind", "bytes"),
                confidence=row.get("confidence", "CONFIRMED"),
                note=row.get("note", ""),
                wheel=row.get("wheel"),
            )
        )
    return anchors


def load_session(path: str) -> SessionData:
    capture = load_capture(path)
    samples = [row for row in capture.records if isinstance(row, SampleRecord)]
    markers = [row for row in capture.records if isinstance(row, MarkerRecord)]
    dropped = 0
    for row in capture.records:
        if isinstance(row, EndRecord):
            dropped = int(row.dropped)
    n = len(samples)
    regions_meta = capture.meta.get("regions") or []
    region_arrays: list[RegionArray] = []
    cursor_width = sum(int(region["size"]) for region in regions_meta)
    if n and samples[0].region and len(samples[0].region) != cursor_width:
        raise ValueError("tamanho da região não bate com o metadado")
    split_at = []
    cursor = 0
    for region in regions_meta:
        size = int(region["size"])
        split_at.append((int(region["offset"]), size, cursor))
        cursor += size
    if n and cursor_width:
        packed = np.zeros((n, cursor_width), dtype=np.uint8)
        for index, sample in enumerate(samples):
            packed[index, : len(sample.region)] = np.frombuffer(sample.region, dtype=np.uint8)
        for offset, size, start in split_at:
            region_arrays.append(RegionArray(offset, size, packed[:, start : start + size]))
    known_size = sum(int(row["size"]) for row in capture.meta.get("anchors") or [])
    known = np.zeros((n, known_size), dtype=np.uint8)
    for index, sample in enumerate(samples):
        if sample.known:
            known[index, : len(sample.known)] = np.frombuffer(sample.known, dtype=np.uint8)
    udp_width = max((len(sample.udp) for sample in samples), default=0)
    udp = np.zeros((n, udp_width), dtype=np.uint8)
    udp_len = np.zeros(n, dtype=np.int32)
    for index, sample in enumerate(samples):
        if sample.udp:
            udp[index, : len(sample.udp)] = np.frombuffer(sample.udp, dtype=np.uint8)
            udp_len[index] = len(sample.udp)
    return SessionData(
        meta=capture.meta,
        closed=capture.closed,
        interval_ns=int(capture.interval_ns),
        timestamp=np.array([sample.timestamp_ns for sample in samples], dtype=np.uint64),
        sequence=np.array([sample.sequence for sample in samples], dtype=np.uint64),
        rig=np.array([sample.physics_rig for sample in samples], dtype=np.uint64),
        status=np.array([sample.status for sample in samples], dtype=np.uint32),
        udp_timestamp=np.array([sample.udp_timestamp_ns for sample in samples], dtype=np.uint64),
        regions=region_arrays,
        known=known,
        anchors=_anchors_from_meta(capture.meta),
        udp=udp,
        udp_len=udp_len,
        markers=markers,
        dropped=dropped,
        path=path,
    )


class RigView:
    def __init__(self, session: SessionData):
        self.session = session
        self._anchor_cursor = []
        cursor = 0
        for anchor in session.anchors:
            self._anchor_cursor.append((anchor, cursor))
            cursor += anchor.size

    def cover(self, offset: int, size: int) -> np.ndarray | None:
        for region in self.session.regions:
            if region.offset <= offset and offset + size <= region.offset + region.size:
                local = offset - region.offset
                return region.data[:, local : local + size]
        for anchor, start in self._anchor_cursor:
            if anchor.offset <= offset and offset + size <= anchor.offset + anchor.size:
                local = offset - anchor.offset
                return self.session.known[:, start + local : start + local + size]
        return None

    def f32(self, offset: int) -> np.ndarray | None:
        raw = self.cover(offset, 4)
        if raw is None or raw.size == 0:
            return None
        return np.ascontiguousarray(raw).view("<f4").reshape(-1)

    def vec(self, offset: int) -> np.ndarray | None:
        raw = self.cover(offset, 16)
        if raw is None or raw.size == 0:
            return None
        floats = np.ascontiguousarray(raw).view("<f4").reshape(-1, 4)
        return floats


def build_slots(session: SessionData) -> SlotTable:
    n = len(session.timestamp)
    columns_f: list[np.ndarray] = []
    columns_i: list[np.ndarray] = []
    raws: list[np.ndarray] = []
    offsets: list[int] = []
    seen: set[int] = set()

    def add_word(offset: int, raw4: np.ndarray) -> None:
        if offset in seen or raw4.shape != (n, 4):
            return
        seen.add(offset)
        blob = np.ascontiguousarray(raw4)
        offsets.append(offset)
        columns_f.append(blob.view("<f4").reshape(n))
        columns_i.append(blob.view("<i4").reshape(n))
        raws.append(blob)

    for region in session.regions:
        nslots = region.size // 4
        if n == 0 or nslots == 0:
            continue
        blob = np.ascontiguousarray(region.data[:, : nslots * 4])
        for index in range(nslots):
            add_word(region.offset + index * 4, blob[:, index * 4 : (index + 1) * 4])

    view = RigView(session)
    for anchor in session.anchors:
        for step in range(0, anchor.size - 3, 4):
            offset = anchor.offset + step
            if offset in seen:
                continue
            raw = view.cover(offset, 4)
            if raw is not None:
                add_word(offset, raw)

    if not offsets:
        return SlotTable(
            offset=np.zeros(0, dtype=np.int64),
            f32=np.zeros((n, 0), dtype=np.float32),
            i32=np.zeros((n, 0), dtype=np.int32),
            changed=np.zeros((max(n - 1, 0), 0), dtype=bool),
            change_count=np.zeros(0, dtype=np.int64),
            samples=n,
        )
    f32 = np.column_stack(columns_f).astype(np.float32, copy=False)
    i32 = np.column_stack(columns_i).astype(np.int32, copy=False)
    raw_mat = np.stack(raws, axis=1)
    if n < 2:
        changed = np.zeros((0, len(offsets)), dtype=bool)
        change_count = np.zeros(len(offsets), dtype=np.int64)
    else:
        changed = np.any(raw_mat[1:] != raw_mat[:-1], axis=2)
        change_count = changed.sum(axis=0).astype(np.int64)
    return SlotTable(
        offset=np.array(offsets, dtype=np.int64),
        f32=f32,
        i32=i32,
        changed=changed,
        change_count=change_count,
        samples=n,
    )


def udp_f32(session: SessionData, offset: int) -> tuple[np.ndarray, np.ndarray] | None:
    if session.udp.shape[1] < offset + 4:
        return None
    raw = np.ascontiguousarray(session.udp[:, offset : offset + 4])
    values = raw.view("<f4").reshape(-1)
    present = session.udp_len >= offset + 4
    return values, present


def hex_offset(offset: int) -> str:
    return f"+0x{offset:04X}"
