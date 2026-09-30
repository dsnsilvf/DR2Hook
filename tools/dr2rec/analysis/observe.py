"""O que mudou, com que frequência, e se o padrão parece discreto ou bit a bit.

Nenhum nome físico sai daqui.
"""

from __future__ import annotations

import numpy as np

from tools.dr2rec.analysis.load import SlotTable, hex_offset
from tools.dr2rec.anchors import anchor_at, documented_at
from tools.dr2rec.confidence import CONFIRMED, POSSIBLE, PROBABLE


def sparkline(flags: np.ndarray, width: int = 48) -> str:
    if flags.size == 0:
        return ""
    pieces = np.array_split(flags.astype(bool), min(width, flags.size))
    return "".join("█" if piece.any() else "─" for piece in pieces)


def _stats(values: np.ndarray) -> dict:
    finite = values[np.isfinite(values)]
    if finite.size == 0:
        return {
            "min": None,
            "max": None,
            "mean": None,
            "variance": None,
            "std": None,
            "nan": int(np.isnan(values).sum()),
            "inf": int(np.isinf(values).sum()),
            "zero": 0,
            "negative": 0,
        }
    return {
        "min": float(finite.min()),
        "max": float(finite.max()),
        "mean": float(finite.mean()),
        "variance": float(finite.var()),
        "std": float(finite.std()),
        "nan": int(np.isnan(values).sum()),
        "inf": int(np.isinf(values).sum()),
        "zero": int(np.count_nonzero(finite == 0)),
        "negative": int(np.count_nonzero(finite < 0)),
    }


def unique_counts(slots: SlotTable) -> np.ndarray:
    counts = np.ones(len(slots.offset), dtype=np.int64)
    if slots.samples == 0:
        return counts
    for index in range(len(slots.offset)):
        if slots.change_count[index] == 0:
            counts[index] = 1
        else:
            counts[index] = np.unique(slots.i32[:, index]).size
    return counts


def describe_slots(slots: SlotTable, anchors) -> list[dict]:
    rows = []
    if len(slots.offset) == 0:
        return rows
    uniq = unique_counts(slots)
    denom = max(slots.samples - 1, 1)
    for index, offset in enumerate(slots.offset.tolist()):
        if slots.change_count[index] == 0 and anchor_at(offset, 4, anchors) is None:
            continue
        anchor = anchor_at(offset, 4, anchors)
        documented = documented_at(offset, 4)
        stats = _stats(slots.f32[:, index])
        integers = slots.i32[:, index]
        unique = int(uniq[index])
        kind = "f32"
        confidence = POSSIBLE
        note = ""
        if anchor is not None:
            kind = anchor.kind
            confidence = CONFIRMED
            note = f"âncora {anchor.name}. {anchor.note}"
        elif documented is not None:
            kind = "documentado"
            confidence = CONFIRMED
            note = f"já documentado: {documented}"
        elif unique <= 6 and slots.change_count[index] > 0 and _small_integers(integers):
            kind = "estado discreto"
            confidence = PROBABLE
            note = f"{hex_offset(offset)} apresenta comportamento compatível com estado discreto"
        row = {
            "offset": int(offset),
            "type": kind,
            "change_count": int(slots.change_count[index]),
            "change_rate": float(slots.change_count[index] / denom),
            "unique": unique,
            "confidence": confidence,
            "note": note,
            "spark": sparkline(slots.changed[:, index]) if slots.changed.size else "",
            "anchor": anchor.name if anchor else None,
            "documented": documented,
            **stats,
        }
        if unique <= 12 and slots.change_count[index] > 0:
            values, freq = np.unique(integers, return_counts=True)
            order = np.argsort(-freq)[:8]
            row["value_freq"] = [
                {"value": int(values[pos]), "count": int(freq[pos])} for pos in order
            ]
            row["transitions"] = int(slots.change_count[index])
        rows.append(row)
    return rows


def discrete_states(rows: list[dict]) -> list[dict]:
    found = []
    for row in rows:
        if row["type"] != "estado discreto":
            continue
        if row["anchor"] or row["documented"]:
            continue
        found.append(
            {
                "offset": row["offset"],
                "unique": row["unique"],
                "transitions": row.get("transitions", row["change_count"]),
                "value_freq": row.get("value_freq", []),
                "confidence": PROBABLE,
                "text": (
                    f"{hex_offset(row['offset'])} apresenta comportamento "
                    "compatível com estado discreto"
                ),
            }
        )
    return found


def bitfields(slots: SlotTable, anchors) -> list[dict]:
    """Bits que ligam e desligam. Sem nome de flag."""
    found = []
    if slots.samples < 2:
        return found
    denom = slots.samples - 1
    for index, offset in enumerate(slots.offset.tolist()):
        if slots.change_count[index] == 0:
            continue
        if anchor_at(offset, 4, anchors) or documented_at(offset, 4):
            continue
        values = slots.i32[:, index].astype(np.int64)
        unique = np.unique(values).size
        if unique > 64 or not _small_integers(values):
            continue
        bits = []
        series = []
        for bit in range(32):
            column = ((values >> bit) & 1).astype(np.int8)
            if column.min() == column.max():
                continue
            transitions = int(np.count_nonzero(np.diff(column)))
            if transitions == 0:
                continue
            series.append(column)
            bits.append(
                {
                    "bit": bit,
                    "transitions": transitions,
                    "on_samples": int(column.sum()),
                    "change_rate": transitions / denom,
                }
            )
        if not bits:
            continue
        exclusive = []
        for left in range(len(series)):
            for right in range(left + 1, len(series)):
                both = np.count_nonzero((series[left] == 1) & (series[right] == 1))
                if both == 0 and series[left].any() and series[right].any():
                    exclusive.append((bits[left]["bit"], bits[right]["bit"]))
        found.append(
            {
                "offset": int(offset),
                "bits": bits,
                "exclusive": exclusive,
                "confidence": POSSIBLE,
            }
        )
    return found


def _small_integers(values: np.ndarray) -> bool:
    """Inteiros pequenos. O padrão de bits de um float não entra aqui."""
    if values.size == 0:
        return False
    return int(np.max(np.abs(values))) <= 65535


def byte_entropy(raw4: np.ndarray) -> float:
    flat = np.ascontiguousarray(raw4).reshape(-1)
    counts = np.bincount(flat, minlength=256)
    used = counts[counts > 0].astype(np.float64)
    prob = used / used.sum()
    return float(-(prob * np.log2(prob)).sum())
