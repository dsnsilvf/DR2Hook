"""Checagens das âncoras. Reproduzir não é descobrir, e falhar não apaga a âncora."""

from __future__ import annotations

import numpy as np

from tools.dr2rec.analysis.load import RigView, SessionData, hex_offset, udp_f32
from tools.dr2rec.anchors import UDP_SUSPENSION, WHEEL_ORDER, WHEEL_STRIDE
from tools.dr2rec.confidence import CONFIRMED, UNKNOWN


def rotate_by_quaternion(q: np.ndarray, vector: np.ndarray) -> np.ndarray:
    """`q * v * conjugado(q)`, com q em (x, y, z, w)."""
    xyz = q[:, :3]
    w = q[:, 3:4]
    t = 2.0 * np.cross(xyz, vector)
    return vector + w * t + np.cross(xyz, t)


def _max_err(a: np.ndarray, b) -> float:
    if np.size(a) == 0:
        return 0.0
    return float(np.max(np.abs(np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64))))


def _mean_err(a: np.ndarray, b) -> float:
    if np.size(a) == 0:
        return 0.0
    return float(np.mean(np.abs(np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64))))


def verify_anchors(session: SessionData, view: RigView) -> list[dict]:
    checks = []
    checks.extend(_basis(view))
    checks.extend(_geometry(view))
    checks.extend(_suspension_udp(session, view))
    return checks


def _basis(view: RigView) -> list[dict]:
    q = view.vec(0x2E0)
    right = view.vec(0x2F0)
    up = view.vec(0x300)
    forward = view.vec(0x310)
    if q is None or right is None or up is None or forward is None:
        return []
    out = []
    length_q = np.linalg.norm(q, axis=1)
    out.append(_check("módulo do quatérnion do chassi ≈ 1", _max_err(length_q, 1.0), _mean_err(length_q, 1.0)))
    for name, vec in (("Right", right[:, :3]), ("Up", up[:, :3]), ("Forward", forward[:, :3])):
        length = np.linalg.norm(vec, axis=1)
        out.append(_check(f"módulo de {name} ≈ 1", _max_err(length, 1.0), _mean_err(length, 1.0)))
    pairs = (("Right·Up", right[:, :3], up[:, :3]), ("Up·Forward", up[:, :3], forward[:, :3]), ("Forward·Right", forward[:, :3], right[:, :3]))
    for name, a, b in pairs:
        dot = np.sum(a * b, axis=1)
        out.append(_check(f"{name} ≈ 0", _max_err(dot, 0.0), _mean_err(dot, 0.0)))
    cross = np.cross(right[:, :3], up[:, :3])
    out.append(
        _check(
            "Right × Up ≈ Forward",
            _max_err(cross, forward[:, :3]),
            _mean_err(cross, forward[:, :3]),
        )
    )
    spun = rotate_by_quaternion(q, np.tile(np.array([[1.0, 0.0, 0.0]]), (len(q), 1)))
    out.append(_check("rotate(q, (1,0,0)) ≈ Right", _max_err(spun, right[:, :3]), _mean_err(spun, right[:, :3])))
    return out


def _geometry(view: RigView) -> list[dict]:
    q = view.vec(0x2E0)
    chassis_up = view.vec(0x300)
    if q is None or chassis_up is None:
        return []
    out = []
    for index, wheel in enumerate(WHEEL_ORDER):
        axle = view.vec(0x1480 + index * WHEEL_STRIDE)
        setup = view.f32(0x14E8 + index * WHEEL_STRIDE)
        course = view.f32(0x1504 + index * WHEEL_STRIDE)
        with_susp = view.vec(0x1660 + index * WHEEL_STRIDE)
        without = view.vec(0x1670 + index * WHEEL_STRIDE)
        if any(item is None for item in (axle, setup, course, with_susp, without)):
            continue
        local = np.stack([axle[:, 0], axle[:, 1] - setup, axle[:, 2]], axis=1)
        predicted = rotate_by_quaternion(q, local)
        out.append(
            _check(
                f"{wheel} +0x1670 ≈ rotate(q, L - c·Y)",
                _max_err(predicted, without[:, :3]),
                _mean_err(predicted, without[:, :3]),
            )
        )
        predicted_with = predicted + course[:, None] * chassis_up[:, :3]
        out.append(
            _check(
                f"{wheel} +0x1660 ≈ +0x1670 + [+0x1504]·Up",
                _max_err(predicted_with, with_susp[:, :3]),
                _mean_err(predicted_with, with_susp[:, :3]),
            )
        )
    return out


def _suspension_udp(session: SessionData, view: RigView) -> list[dict]:
    out = []
    if session.udp.shape[1] == 0:
        return out
    for index, wheel in enumerate(WHEEL_ORDER):
        memory = view.f32(0x1504 + index * WHEEL_STRIDE)
        parsed = udp_f32(session, UDP_SUSPENSION[index])
        if memory is None or parsed is None:
            continue
        udp, present = parsed
        both = present & np.isfinite(memory) & np.isfinite(udp)
        if int(both.sum()) == 0:
            continue
        scaled = (memory.astype(np.float32) * np.float32(1000.0)).astype(np.float32)
        err = np.abs(scaled[both] - udp[both].astype(np.float32))
        maximum = float(err.max())
        mean = float(err.mean())
        exact = maximum == 0.0 and int(both.sum()) == len(memory)
        out.append(
            {
                "name": f"UDP suspension_position {wheel} = float32([{hex_offset(0x1504 + index * WHEEL_STRIDE)}] × 1000)",
                "max_error": maximum,
                "mean_error": mean,
                "samples": int(both.sum()),
                "exact": exact,
                "confidence": CONFIRMED if exact else UNKNOWN,
                "verdict": "âncora reproduzida" if exact else "âncora não reproduzida nesta sessão",
            }
        )
    return out


def _check(name: str, maximum: float, mean: float) -> dict:
    reproduced = maximum <= 1e-4
    return {
        "name": name,
        "max_error": maximum,
        "mean_error": mean,
        "exact": maximum == 0.0,
        "confidence": CONFIRMED if reproduced else UNKNOWN,
        "verdict": "âncora reproduzida" if reproduced else "âncora não reproduzida nesta sessão",
    }
