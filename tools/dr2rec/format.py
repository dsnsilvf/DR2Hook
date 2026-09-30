"""Formato .dr2cap. Os bytes da sessão são a evidência; o JSON só descreve.

Layout, little-endian:

    0x00  magic[8]           b"DR2CAP\\0\\0"
    0x08  version u16        1
    0x0A  header_size u16    64
    0x0C  flags u32          bit 0 = arquivo ainda aberto
    0x10  sample_count u64
    0x18  meta_offset u64
    0x20  meta_size u64
    0x28  data_offset u64
    0x30  interval_ns u64
    0x38  reserved u64

O miolo é um JSON de metadados. Em seguida, registros:

    u32 total_size
    u8  type
    u8  pad[3]
    payload

Tipo 1, amostra:

    u64 timestamp_ns
    u64 sequence
    u64 physics_rig
    u64 udp_timestamp_ns
    u32 status
    u32 region_len
    u32 known_len
    u32 udp_len
    region bytes | known bytes | udp bytes

Tipo 2, marcador:

    u64 timestamp_ns
    u64 after_sequence
    u32 label_len
    utf-8

Tipo 3, fim:

    u64 timestamp_ns
    u64 samples
    u64 dropped
    u64 markers

Status da amostra: bit 0 região lida, bit 1 âncoras lidas, bit 2 UDP presente,
bit 3 captura atrasada em relação ao intervalo.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from typing import BinaryIO, Iterator

MAGIC = b"DR2CAP\x00\x00"
VERSION = 1
HEADER_SIZE = 64
HEADER = struct.Struct("<8sHHIQQQQQQ")

REC_SAMPLE = 1
REC_MARKER = 2
REC_END = 3

ST_REGION = 1
ST_KNOWN = 2
ST_UDP = 4
ST_LATE = 8

FLAG_OPEN = 1

_SAMPLE = struct.Struct("<QQQQIIII")
_MARKER = struct.Struct("<QQI")
_END = struct.Struct("<QQQQ")


def pack_header(
    *,
    sample_count: int,
    meta_offset: int,
    meta_size: int,
    data_offset: int,
    interval_ns: int,
    flags: int,
) -> bytes:
    blob = HEADER.pack(
        MAGIC,
        VERSION,
        HEADER_SIZE,
        flags,
        sample_count,
        meta_offset,
        meta_size,
        data_offset,
        interval_ns,
        0,
    )
    if len(blob) != HEADER_SIZE:
        raise RuntimeError(f"cabeçalho {len(blob)} bytes, esperado {HEADER_SIZE}")
    return blob


@dataclass
class SampleRecord:
    timestamp_ns: int
    sequence: int
    physics_rig: int
    udp_timestamp_ns: int
    status: int
    region: bytes
    known: bytes
    udp: bytes


@dataclass
class MarkerRecord:
    timestamp_ns: int
    after_sequence: int
    label: str


@dataclass
class EndRecord:
    timestamp_ns: int
    samples: int
    dropped: int
    markers: int


@dataclass
class CaptureFile:
    meta: dict
    flags: int
    interval_ns: int
    records: list[SampleRecord | MarkerRecord | EndRecord]

    @property
    def samples(self) -> list[SampleRecord]:
        return [row for row in self.records if isinstance(row, SampleRecord)]

    @property
    def markers(self) -> list[MarkerRecord]:
        return [row for row in self.records if isinstance(row, MarkerRecord)]

    @property
    def closed(self) -> bool:
        return any(isinstance(row, EndRecord) for row in self.records)


class CaptureWriter:
    """Grava amostras em ordem, sem reinterpretar os bytes."""

    def __init__(self, path: str, meta: dict, interval_ns: int):
        self.path = path
        self.meta = meta
        self.interval_ns = interval_ns
        self.samples = 0
        self.markers = 0
        self._fh: BinaryIO = open(path, "wb", buffering=1024 * 1024)
        meta_blob = json.dumps(meta, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._meta_size = len(meta_blob)
        self._data_offset = HEADER_SIZE + self._meta_size
        self._fh.write(
            pack_header(
                sample_count=0,
                meta_offset=HEADER_SIZE,
                meta_size=self._meta_size,
                data_offset=self._data_offset,
                interval_ns=interval_ns,
                flags=FLAG_OPEN,
            )
        )
        self._fh.write(meta_blob)
        self._pending_flush = 0

    def add_sample(self, sample: SampleRecord) -> None:
        payload = _SAMPLE.pack(
            sample.timestamp_ns,
            sample.sequence,
            sample.physics_rig & 0xFFFFFFFFFFFFFFFF,
            sample.udp_timestamp_ns,
            sample.status,
            len(sample.region),
            len(sample.known),
            len(sample.udp),
        )
        payload += sample.region + sample.known + sample.udp
        self._write(REC_SAMPLE, payload)
        self.samples += 1
        self._pending_flush += 1
        if self._pending_flush >= 32:
            self._fh.flush()
            self._pending_flush = 0

    def add_marker(self, marker: MarkerRecord) -> None:
        label = marker.label.encode("utf-8")
        payload = _MARKER.pack(marker.timestamp_ns, marker.after_sequence, len(label)) + label
        self._write(REC_MARKER, payload)
        self.markers += 1

    def close(self, *, dropped: int = 0, timestamp_ns: int = 0) -> None:
        if self._fh.closed:
            return
        payload = _END.pack(timestamp_ns, self.samples, dropped, self.markers)
        self._write(REC_END, payload)
        self._fh.flush()
        self._fh.seek(0)
        self._fh.write(
            pack_header(
                sample_count=self.samples,
                meta_offset=HEADER_SIZE,
                meta_size=self._meta_size,
                data_offset=self._data_offset,
                interval_ns=self.interval_ns,
                flags=0,
            )
        )
        self._fh.flush()
        self._fh.close()

    def _write(self, kind: int, payload: bytes) -> None:
        total = 8 + len(payload)
        self._fh.write(struct.pack("<IBBBB", total, kind, 0, 0, 0))
        self._fh.write(payload)


def _read_exact(fh: BinaryIO, size: int) -> bytes:
    blob = fh.read(size)
    if len(blob) != size:
        raise EOFError(f"arquivo truncado, faltam {size - len(blob)} bytes")
    return blob


def iter_records(fh: BinaryIO, data_offset: int) -> Iterator[SampleRecord | MarkerRecord | EndRecord]:
    fh.seek(data_offset)
    while True:
        head = fh.read(8)
        if not head:
            return
        if len(head) < 8:
            return
        total, kind = struct.unpack_from("<IB", head)
        if total < 8:
            raise ValueError("registro com tamanho inválido")
        payload = _read_exact(fh, total - 8)
        if kind == REC_SAMPLE:
            if len(payload) < _SAMPLE.size:
                raise ValueError("amostra curta")
            t, seq, rig, udp_t, status, region_len, known_len, udp_len = _SAMPLE.unpack_from(payload)
            cursor = _SAMPLE.size
            region = payload[cursor : cursor + region_len]
            cursor += region_len
            known = payload[cursor : cursor + known_len]
            cursor += known_len
            udp = payload[cursor : cursor + udp_len]
            if len(region) != region_len or len(known) != known_len or len(udp) != udp_len:
                raise ValueError("amostra com blobs incompletos")
            yield SampleRecord(t, seq, rig, udp_t, status, region, known, udp)
        elif kind == REC_MARKER:
            t, seq, label_len = _MARKER.unpack_from(payload)
            label = payload[_MARKER.size : _MARKER.size + label_len].decode("utf-8", errors="replace")
            yield MarkerRecord(t, seq, label)
        elif kind == REC_END:
            t, samples, dropped, markers = _END.unpack_from(payload)
            yield EndRecord(t, samples, dropped, markers)
        else:
            continue


def load_capture(path: str) -> CaptureFile:
    with open(path, "rb") as fh:
        header = _read_exact(fh, HEADER_SIZE)
        magic, version, header_size, flags, _count, meta_off, meta_size, data_off, interval_ns, _reserved = HEADER.unpack(header)
        if magic != MAGIC:
            raise ValueError("não é um arquivo .dr2cap")
        if version != VERSION:
            raise ValueError(f"versão {version} não suportada")
        if header_size != HEADER_SIZE or meta_off != HEADER_SIZE:
            raise ValueError("cabeçalho inconsistente")
        fh.seek(meta_off)
        meta = json.loads(_read_exact(fh, meta_size).decode("utf-8"))
        records = list(iter_records(fh, data_off))
    return CaptureFile(meta=meta, flags=flags, interval_ns=interval_ns, records=records)


def parse_interval(text: str) -> int:
    """Número puro é milissegundo. Também aceita 10ms, 500us e 0.05s."""
    raw = text.strip().lower()
    if raw.endswith("ms"):
        value = float(raw[:-2]) * 1_000_000
    elif raw.endswith("us"):
        value = float(raw[:-2]) * 1_000
    elif raw.endswith("s"):
        value = float(raw[:-1]) * 1_000_000_000
    else:
        value = float(raw) * 1_000_000
    interval = int(value)
    if interval <= 0:
        raise ValueError("intervalo precisa ser positivo")
    return interval


def parse_region(text: str) -> dict:
    """`physicsrig:0x0000:0x2600` é início inclusive e fim exclusivo."""
    parts = text.split(":")
    if len(parts) != 3:
        raise ValueError(f"região inválida: {text}")
    space, start_s, end_s = parts
    space = space.strip().lower()
    if space != "physicsrig":
        raise ValueError("só o espaço physicsrig é aceito nesta versão")
    start = int(start_s, 0)
    end = int(end_s, 0)
    if start < 0 or end <= start:
        raise ValueError(f"intervalo vazio ou negativo: {text}")
    size = end - start
    if size > 0x100000:
        raise ValueError("região acima de 1 MiB")
    return {"space": "physicsrig", "offset": start, "size": size}
