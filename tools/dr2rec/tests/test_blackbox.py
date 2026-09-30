"""Sessão sintética. O analyzer tem de medir, não batizar."""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

import numpy as np

from tools.dr2rec.analysis.pipeline import analyze_capture, compare_to_dir, inspect_offset
from tools.dr2rec.analysis.verify import rotate_by_quaternion
from tools.dr2rec.analysis.xref import classify_site, extract_text, scan_code
from tools.dr2rec.anchors import confirmed_anchors
from tools.dr2rec.format import (
    HEADER_SIZE,
    CaptureWriter,
    MarkerRecord,
    SampleRecord,
    load_capture,
    pack_header,
    parse_interval,
    parse_region,
)
from tools.dr2rec.format import ST_KNOWN, ST_REGION, ST_UDP
from tools.dr2rec.process_mem import ArraySource, merge_ranges
from tools.dr2rec.recorder import build_meta, copy_anchors_from_image, plan_reads, record_loop
from tools.dr2rec.anchors import WHEEL_STRIDE

N = 160
RIG = 0x4B76BAB0
REGION = 0x2600


def _put_f32(buf: bytearray, offset: int, value: float) -> None:
    struct.pack_into("<f", buf, offset, float(np.float32(value)))


def _put_i32(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<i", buf, offset, int(value))


def _put_u64(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<Q", buf, offset, int(value))


def _put_vec(buf: bytearray, offset: int, x: float, y: float, z: float, w: float = 0.0) -> None:
    struct.pack_into("<4f", buf, offset, float(x), float(y), float(z), float(w))


def _series() -> dict[str, np.ndarray]:
    t = np.arange(N, dtype=np.float64)
    course = []
    for index in range(4):
        course.append((np.float32(0.01) * np.sin(t / (9.0 + index) + index)).astype(np.float32))
    spike = np.zeros(N, dtype=np.float32)
    spike[::7] = np.float32(1.0)
    spike[3::7] = np.float32(-0.5)
    delayed = np.zeros(N, dtype=np.float32)
    delayed[2:] = spike[:-2]
    discrete = np.zeros(N, dtype=np.int32)
    discrete[50:100] = 1
    discrete[100:] = 2
    bits = np.zeros(N, dtype=np.int32)
    for start in range(0, N, 20):
        bits[start : start + 10] |= 1
    bits[100:] |= 8
    jump = np.zeros(N, dtype=np.float32)
    jump[100:] = np.float32(20.0)
    step = np.zeros(N, dtype=np.float32)
    step[100:] = np.float32(7.0)
    ang = t * 0.05
    speed = (np.sin(t / 5.0).astype(np.float32) * np.float32(5.0)).astype(np.float32)
    gear = (t.astype(np.int32) % 4).astype(np.int32)
    return {
        "course": course,
        "spike": spike,
        "delayed": delayed,
        "neg": (-spike).astype(np.float32),
        "discrete": discrete,
        "bits": bits,
        "jump": jump,
        "step": step,
        "cos": np.cos(ang).astype(np.float32),
        "sin": np.sin(ang).astype(np.float32),
        "speed": speed,
        "gear": gear,
    }


def _image(sample: int, series: dict, static: float) -> bytearray:
    buf = bytearray(REGION)
    _put_vec(buf, 0x2E0, 0, 0, 0, 1)
    _put_vec(buf, 0x2F0, 1, 0, 0, 0)
    _put_vec(buf, 0x300, 0, 1, 0, 0)
    _put_vec(buf, 0x310, 0, 0, 1, 0)
    axles = (
        (0.712, 0.4, -1.538),
        (-0.712, 0.4, -1.538),
        (0.712, 0.4, 0.935),
        (-0.712, 0.4, 0.935),
    )
    for index, (x, y, z) in enumerate(axles):
        base = index * WHEEL_STRIDE
        course = float(series["course"][index][sample])
        _put_vec(buf, 0x1480 + base, x, y, z, 0)
        _put_f32(buf, 0x14E8 + base, 0.301)
        _put_f32(buf, 0x1504 + base, course)
        local_y = y - 0.301
        _put_vec(buf, 0x1670 + base, x, local_y, z, 0)
        _put_vec(buf, 0x1660 + base, x, local_y + course, z, 0)
        _put_vec(buf, 0x1680 + base, 0, 1, 0, 0)
        side = 0.25 if index % 2 == 0 else -0.25
        _put_vec(buf, 0x1580 + base, side, 1.0, 2.0, 0)
        _put_u64(buf, 0x1590 + base, 0x140ABCDE0)
    _put_i32(buf, 0x0900, int(series["discrete"][sample]))
    _put_i32(buf, 0x0910, int(series["bits"][sample]))
    _put_f32(buf, 0x0A00, float(series["spike"][sample]))
    _put_f32(buf, 0x0B00, float(series["delayed"][sample]))
    _put_f32(buf, 0x0B10, float(series["neg"][sample]))
    _put_f32(buf, 0x0A20, float(series["step"][sample]))
    for extra in range(4):
        _put_f32(buf, 0x0A40 + extra * 4, float(series["jump"][sample]))
    _put_vec(buf, 0x0C00, float(series["cos"][sample]), 0, float(series["sin"][sample]), 0)
    _put_vec(buf, 0x0C10, float(-series["sin"][sample]), 0, float(series["cos"][sample]), 0)
    _put_vec(buf, 0x0C20, 0, -1, 0, 0)
    _put_f32(buf, 0x0E00, static)
    _put_f32(buf, 0x0F00, float(series["speed"][sample]))
    _put_i32(buf, 0x1448, int(series["gear"][sample]))
    return buf


def _udp(sample: int, series: dict) -> bytes:
    packet = bytearray(256)
    struct.pack_into("<f", packet, 28, float(series["speed"][sample]))
    for index, offset in enumerate((68, 72, 76, 80)):
        scaled = (series["course"][index][sample] * np.float32(1000.0)).astype(np.float32)
        struct.pack_into("<f", packet, offset, float(scaled))
    return bytes(packet)


def write_session(path: Path, *, static: float = 0.301, discrete: bool = True) -> None:
    series = _series()
    if not discrete:
        series["discrete"] = np.zeros(N, dtype=np.int32)
    anchors = confirmed_anchors()
    meta = build_meta(
        executable="dirtrally2.exe",
        pid=1,
        module_base=0x140000000,
        physics_rig=RIG,
        interval_ns=10_000_000,
        regions=[{"space": "physicsrig", "offset": 0, "size": REGION}],
        anchors=anchors,
        udp_port=20777,
        note="sintética",
        maps="140000000-141000000 r--p 00000000 00:00 0 dirtrally2.exe\n",
    )
    writer = CaptureWriter(str(path), meta, 10_000_000)
    try:
        for index in range(N):
            image = _image(index, series, static)
            writer.add_sample(
                SampleRecord(
                    timestamp_ns=1_000_000_000 + index * 10_000_000,
                    sequence=index,
                    physics_rig=RIG,
                    udp_timestamp_ns=1_000_000_000 + index * 10_000_000,
                    status=ST_REGION | ST_KNOWN | ST_UDP,
                    region=bytes(image),
                    known=copy_anchors_from_image(bytes(image), 0, anchors),
                    udp=_udp(index, series),
                )
            )
        writer.add_marker(MarkerRecord(1_000_000_000 + 100 * 10_000_000, 100, "colisão"))
    finally:
        writer.close(timestamp_ns=1_000_000_000 + N * 10_000_000)


class FormatTests(unittest.TestCase):
    def test_interval_and_region(self):
        self.assertEqual(parse_interval("10"), 10_000_000)
        self.assertEqual(parse_interval("10ms"), 10_000_000)
        self.assertEqual(parse_interval("16ms"), 16_000_000)
        region = parse_region("physicsrig:0x1400:0x1800")
        self.assertEqual(region["offset"], 0x1400)
        self.assertEqual(region["size"], 0x400)
        with self.assertRaises(ValueError):
            parse_region("heap:0x0:0x10")

    def test_header_size_and_raw_roundtrip(self):
        blob = pack_header(
            sample_count=0,
            meta_offset=HEADER_SIZE,
            meta_size=2,
            data_offset=HEADER_SIZE + 2,
            interval_ns=1000,
            flags=0,
        )
        self.assertEqual(len(blob), HEADER_SIZE)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "raw.dr2cap"
            write_session(path)
            capture = load_capture(str(path))
            self.assertTrue(capture.closed)
            self.assertEqual(len(capture.samples), N)
            self.assertEqual(capture.markers[0].label, "colisão")
            image = _image(0, _series(), 0.301)
            self.assertEqual(capture.samples[0].region, bytes(image))
            self.assertEqual(capture.samples[0].known[:4], bytes(image[0x2E0:0x2E4]))

    def test_merge_and_record_loop(self):
        anchors = confirmed_anchors()
        regions = [{"space": "physicsrig", "offset": 0, "size": REGION}]
        plan = plan_reads(regions, anchors)
        self.assertEqual(plan.covers, ((0, REGION),))
        self.assertEqual(merge_ranges([(0, 100), (50, 120), (200, 210)]), [(0, 120), (200, 210)])
        image = bytearray(REGION)
        source = ArraySource(RIG, image)

        def mutate(index: int) -> None:
            _put_f32(source.blob, 0x1504, 0.125 + index)

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "loop.dr2cap"
            meta = build_meta(
                executable="dirtrally2.exe",
                pid=1,
                module_base=0x140000000,
                physics_rig=RIG,
                interval_ns=0,
                regions=regions,
                anchors=anchors,
                udp_port=None,
                note="",
                maps="",
            )

            class Counting:
                def __init__(self):
                    self.n = 0

                def read_many(self, ranges):
                    mutate(self.n)
                    self.n += 1
                    return source.read_many(ranges)

            record_loop(
                str(path),
                meta,
                Counting(),
                rig=RIG,
                regions=regions,
                anchors=anchors,
                interval_ns=0,
                max_samples=3,
            )
            capture = load_capture(str(path))
            self.assertEqual(len(capture.samples), 3)
            first = struct.unpack_from("<f", capture.samples[0].region, 0x1504)[0]
            third = struct.unpack_from("<f", capture.samples[2].region, 0x1504)[0]
            self.assertEqual(first, struct.unpack("<f", struct.pack("<f", np.float32(0.125)))[0])
            self.assertGreater(third, first)
            known = struct.unpack_from("<f", capture.samples[2].known, 64)[0]
            self.assertEqual(known, third)

            class Fail:
                def read_many(self, ranges):
                    return [None for _ in ranges]

            fail_path = Path(tmp) / "fail.dr2cap"
            record_loop(
                str(fail_path),
                meta,
                Fail(),
                rig=RIG,
                regions=regions,
                anchors=anchors,
                interval_ns=0,
                max_samples=1,
            )
            failed = load_capture(str(fail_path))
            self.assertEqual(failed.samples[0].status & ST_REGION, 0)
            self.assertEqual(failed.samples[0].region, bytes(REGION))


class AnalyzeTests(unittest.TestCase):
    def test_session_becomes_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "session.dr2cap"
            other = Path(tmp) / "other.dr2cap"
            write_session(path)
            write_session(other, static=0.5, discrete=False)
            result = analyze_capture(str(path))
            md = result["markdown"]
            for heading in (
                "SESSION SUMMARY",
                "MEMORY ACTIVITY",
                "STRUCTURES",
                "EVENTS",
                "CORRELATIONS",
                "MATHEMATICAL RELATIONS",
                "SYMMETRY",
                "UDP CORRELATION",
                "INTERESTING OFFSETS",
                "UNKNOWN",
                "NEXT EXPERIMENTS",
            ):
                self.assertIn(heading, md)
            self.assertIn("STRUCTURE CANDIDATE", md)
            self.assertIn("memory transition", md)
            self.assertIn("colisão", md)
            self.assertIn("estado discreto", md)
            self.assertIn("+0x0900", md)
            self.assertIn("bit 3", md)
            self.assertIn("A.x ≈ -B.x", md)
            self.assertIn("sem nome físico", md)
            self.assertIn("Igualdade exata", md)
            self.assertIn("âncora reproduzida", md)
            self.assertIn("STRONG EVIDENCE", md)
            self.assertNotIn("puncture", md.lower())
            self.assertNotIn("damage flag", md.lower())
            self.assertTrue(any(row["offset"] == 0x900 for row in result["discrete"]))
            self.assertTrue(any(row["memory"] == 0xF00 and row["exact"] for row in result["udp"]))
            self.assertTrue(any(row["value"] == 0x140ABCDE0 for row in result["pointers"]))
            self.assertFalse(any(row["value"] == 0x40000000 for row in result["pointers"]))
            self.assertTrue(
                any(
                    {row["a"], row["b"]} == {0xA00, 0xB00}
                    and abs(row["lag_signed"]) == 2
                    and row["correlation"] > 0.99
                    for row in result["correlations"]
                )
            )
            self.assertTrue(any("≈ -" in row["text"] and row["exact"] for row in result["relations"]))
            self.assertNotIn(0x1448, [row["offset"] for row in result["unknown"]])
            gear = [row for row in result["activity"] if row["offset"] == 0x1448]
            self.assertTrue(gear)
            self.assertEqual(gear[0]["confidence"], "CONFIRMED")
            text = inspect_offset(str(path), 0x1504)
            self.assertIn("suspension_position_RL", text)
            compared = compare_to_dir(str(path), str(other), str(Path(tmp) / "cmp"))
            diff = {row["offset"] for row in compared["constant_diff"]}
            self.assertIn(0xE00, diff)
            only = {row["offset"] for row in compared["dynamic_one"]}
            self.assertIn(0x900, only)

    def test_quaternion_rotation_and_xref(self):
        q = np.array([[0.0, 1.0, 0.0, 0.0]])
        spun = rotate_by_quaternion(q, np.array([[1.0, 0.0, 0.0]]))
        self.assertAlmostEqual(float(spun[0, 0]), -1.0, places=6)
        code = bytearray(64)
        code[10:14] = b"\xF3\x0F\x11\x80"
        code[14:18] = (0x1504).to_bytes(4, "little")
        self.assertEqual(classify_site(code, 14), ("writer", "movss"))
        hits = scan_code(bytes(code), 0x1000, [0x1504])
        self.assertTrue(hits)
        self.assertEqual(hits[0]["role"], "writer")
        self.assertEqual(hits[0]["confidence"], "POSSIBLE")
        pe = bytearray(0x200 + len(code))
        pe[0:2] = b"MZ"
        struct.pack_into("<I", pe, 0x3C, 0x80)
        struct.pack_into("<I", pe, 0x80, 0x4550)
        struct.pack_into("<HHIIIHH", pe, 0x84, 0x8664, 1, 0, 0, 0, 0xF0, 0x22)
        struct.pack_into("<H", pe, 0x98, 0x20B)
        struct.pack_into("<8sIIII", pe, 0x188, b".text\0\0\0", len(code), 0x1000, len(code), 0x200)
        pe[0x200 : 0x200 + len(code)] = code
        text, rva = extract_text(bytes(pe))
        self.assertEqual(rva, 0x1000)
        self.assertIn(b"\xF3\x0F\x11\x80", text)


if __name__ == "__main__":
    unittest.main()
