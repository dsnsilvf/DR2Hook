"""Relações medidas entre campos. O texto descreve a conta, não um nome de peça."""

from __future__ import annotations

import numpy as np

from tools.dr2rec.analysis.load import RigView, SessionData, SlotTable, hex_offset, udp_f32
from tools.dr2rec.anchors import WHEEL_ORDER, anchor_at, documented_at, stride_layouts
from tools.dr2rec.confidence import POSSIBLE, PROBABLE, STRONG


def _pearson(a: np.ndarray, b: np.ndarray) -> float:
    if a.size < 8:
        return 0.0
    x = a.astype(np.float64)
    y = b.astype(np.float64)
    x = x - x.mean()
    y = y - y.mean()
    xx = float(np.dot(x, x))
    yy = float(np.dot(y, y))
    if xx < 1e-18 or yy < 1e-18:
        return 0.0
    return float(np.dot(x, y) / np.sqrt(xx * yy))


def _candidate_indexes(slots: SlotTable, anchors, limit: int) -> list[int]:
    scored = []
    for index, offset in enumerate(slots.offset.tolist()):
        if slots.change_count[index] < 4:
            continue
        column = slots.f32[:, index]
        if np.isfinite(column).sum() < 8:
            continue
        variance = float(np.nanvar(column))
        if variance < 1e-12:
            continue
        scored.append((variance, index))
    scored.sort(reverse=True)
    return [index for _variance, index in scored[:limit]]


def correlations(slots: SlotTable, anchors, max_lag: int = 5, limit: int = 36) -> list[dict]:
    chosen = _candidate_indexes(slots, anchors, limit)
    found = []
    for left in range(len(chosen)):
        for right in range(left + 1, len(chosen)):
            i = chosen[left]
            j = chosen[right]
            a = slots.f32[:, i]
            b = slots.f32[:, j]
            best_lag = 0
            best_r = 0.0
            for lag in range(-max_lag, max_lag + 1):
                if lag < 0:
                    x, y = a[-lag:], b[:lag]
                elif lag > 0:
                    x, y = a[:-lag], b[lag:]
                else:
                    x, y = a, b
                score = _pearson(x, y)
                if abs(score) > abs(best_r):
                    best_r = score
                    best_lag = lag
            if abs(best_r) < 0.98:
                continue
            off_a = int(slots.offset[i])
            off_b = int(slots.offset[j])
            if best_lag < 0:
                off_a, off_b = off_b, off_a
                best_lag = -best_lag
            if best_lag > 0:
                relation = "A muda antes de B"
            elif best_r < 0:
                relation = "A acompanha o oposto de B"
            else:
                relation = "A acompanha B"
            found.append(
                {
                    "a": off_a,
                    "b": off_b,
                    "lag": int(best_lag),
                    "lag_signed": int(best_lag),
                    "correlation": float(best_r),
                    "relation": relation,
                    "confidence": STRONG if abs(best_r) >= 0.995 else PROBABLE,
                    "text": (
                        f"{hex_offset(off_a)} → {hex_offset(off_b)}  "
                        f"lag = {best_lag} sample  correlation = {best_r:.4f}  {relation}"
                    ),
                }
            )
    found.sort(key=lambda row: abs(row["correlation"]), reverse=True)
    return found[:30]


def _float_error(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    err = np.abs(a.astype(np.float64) - b.astype(np.float64))
    if err.size == 0:
        return 0.0, 0.0
    return float(err.max()), float(err.mean())


def math_relations(slots: SlotTable, anchors, view: RigView) -> list[dict]:
    found = []
    found.extend(_scale_relations(slots, anchors))
    found.extend(_vector_relations(view, anchors))
    return found


def _vector_ok(xyz: np.ndarray) -> bool:
    """Dois eixos com amplitude. Um float seguido de zero não é vetor."""
    peaks = np.max(np.abs(xyz), axis=0)
    if int(np.count_nonzero(peaks > 1e-3)) < 2:
        return False
    length = np.linalg.norm(xyz, axis=1)
    mean = float(np.mean(length))
    return 0.05 <= mean <= 50.0


def _plausible_pointer(value: int) -> bool:
    """Alinhado e no espaço de usuário. Float com word alta zero não entra."""
    if value % 8 != 0 or not (0x10000 <= value <= 0x00007FFFFFFFFFFF):
        return False
    if (value >> 32) > 0x8000:
        return False
    if (value >> 32) == 0:
        raw = np.array([np.uint32(value & 0xFFFFFFFF)]).view(np.float32)[0]
        if np.isfinite(raw) and 1e-4 <= abs(float(raw)) <= 1e6:
            return False
    return True


def _is_known(offset: int, anchors) -> bool:
    return anchor_at(offset, 4, anchors) is not None or documented_at(offset, 4) is not None


def _scale_relations(slots: SlotTable, anchors) -> list[dict]:
    chosen = []
    for index in _candidate_indexes(slots, anchors, 24):
        offset = int(slots.offset[index])
        if _is_known(offset, anchors):
            continue
        chosen.append(index)
    scales = (1.0, -1.0, 1000.0, -1000.0, 0.001, -0.001)
    found = []
    seen = set()
    for left in range(len(chosen)):
        for right in range(left + 1, len(chosen)):
            i = chosen[left]
            j = chosen[right]
            a = slots.f32[:, i].astype(np.float32)
            b = slots.f32[:, j].astype(np.float32)
            off_a = int(slots.offset[i])
            off_b = int(slots.offset[j])
            for scale in scales:
                scaled = (a * np.float32(scale)).astype(np.float32)
                maximum, mean = _float_error(scaled, b)
                if maximum > 1e-4:
                    continue
                key = (off_a, off_b, scale)
                if key in seen:
                    continue
                seen.add(key)
                exact = maximum == 0.0
                if scale == -1.0:
                    text = f"{hex_offset(off_a)} ≈ -({hex_offset(off_b)})"
                elif scale == 1.0:
                    text = f"{hex_offset(off_a)} ≈ {hex_offset(off_b)}"
                else:
                    text = f"{hex_offset(off_b)} ≈ {scale:g} · {hex_offset(off_a)}"
                found.append(
                    {
                        "text": text,
                        "max_error": maximum,
                        "mean_error": mean,
                        "exact": exact,
                        "confidence": STRONG if exact else PROBABLE,
                        "a": off_a,
                        "b": off_b,
                    }
                )
    return found


def _vector_relations(view: RigView, anchors) -> list[dict]:
    vectors = []
    for region in view.session.regions:
        first = (region.offset + 15) & ~15
        offset = first
        end = region.offset + region.size
        while offset + 16 <= end:
            if anchor_at(offset, 16, anchors) is not None or documented_at(offset, 16) is not None:
                offset += 16
                continue
            raw = view.cover(offset, 16)
            if raw is None:
                offset += 16
                continue
            if np.any(raw[:, 12:16]):
                offset += 16
                continue
            xyz = np.ascontiguousarray(raw[:, :12]).view("<f4").reshape(-1, 3).astype(np.float64)
            if not np.isfinite(xyz).all() or not _vector_ok(xyz):
                offset += 16
                continue
            length = np.linalg.norm(xyz, axis=1)
            vectors.append({"offset": offset, "xyz": xyz, "length": length})
            offset += 16
            if len(vectors) >= 16:
                break
        if len(vectors) >= 16:
            break
    found = []
    for item in vectors:
        length = item["length"]
        maximum, mean = _float_error(length, np.ones_like(length))
        if maximum <= 1e-4:
            found.append(
                {
                    "text": f"|{hex_offset(item['offset'])}| ≈ 1",
                    "max_error": maximum,
                    "mean_error": mean,
                    "exact": maximum == 0.0,
                    "confidence": STRONG,
                    "a": item["offset"],
                    "b": item["offset"],
                }
            )
    for left in range(len(vectors)):
        for right in range(left + 1, len(vectors)):
            dot = np.sum(vectors[left]["xyz"] * vectors[right]["xyz"], axis=1)
            maximum, mean = _float_error(dot, np.zeros_like(dot))
            if maximum <= 1e-3 and float(np.mean(np.abs(vectors[left]["length"]))) > 0.2:
                found.append(
                    {
                        "text": f"dot({hex_offset(vectors[left]['offset'])}, {hex_offset(vectors[right]['offset'])}) ≈ 0",
                        "max_error": maximum,
                        "mean_error": mean,
                        "exact": maximum == 0.0,
                        "confidence": STRONG if maximum <= 1e-4 else PROBABLE,
                        "a": vectors[left]["offset"],
                        "b": vectors[right]["offset"],
                    }
                )
    for i in range(len(vectors)):
        for j in range(len(vectors)):
            if i == j:
                continue
            for k in range(len(vectors)):
                if k == i or k == j:
                    continue
                prod = np.cross(vectors[i]["xyz"], vectors[j]["xyz"])
                maximum, mean = _float_error(prod, vectors[k]["xyz"])
                if maximum > 1e-3:
                    continue
                found.append(
                    {
                        "text": (
                            f"{hex_offset(vectors[i]['offset'])} × "
                            f"{hex_offset(vectors[j]['offset'])} ≈ "
                            f"{hex_offset(vectors[k]['offset'])}"
                        ),
                        "max_error": maximum,
                        "mean_error": mean,
                        "exact": maximum == 0.0,
                        "confidence": STRONG if maximum <= 1e-4 else PROBABLE,
                        "a": vectors[i]["offset"],
                        "b": vectors[k]["offset"],
                    }
                )
    # Uma relação de produto vetorial basta por trio.
    unique = []
    seen = set()
    for row in found:
        if row["text"] in seen:
            continue
        seen.add(row["text"])
        unique.append(row)
    return unique[:40]


def symmetry(view: RigView, slots: SlotTable, anchors) -> list[dict]:
    found = []
    for layout in stride_layouts():
        base = layout["base"]
        stride = layout["stride"]
        names = layout["names"]
        if view.cover(base, stride * layout["count"]) is None:
            continue
        reported_blocks = set()
        for rel in range(0, stride, 16):
            series = []
            ok = True
            for index in range(4):
                vec = view.vec(base + index * stride + rel)
                if vec is None:
                    ok = False
                    break
                series.append(vec[:, :3])
            if not ok:
                continue
            if all(float(np.max(np.abs(item))) < 1e-8 for item in series):
                continue
            for pair in ((0, 1), (2, 3)):
                a = series[pair[0]]
                b = series[pair[1]]
                err_x = float(np.max(np.abs(a[:, 0] + b[:, 0])))
                err_y = float(np.max(np.abs(a[:, 1] - b[:, 1])))
                err_z = float(np.max(np.abs(a[:, 2] - b[:, 2])))
                if max(err_x, err_y, err_z) > 1e-4:
                    continue
                if float(np.max(np.abs(a[:, 0]))) < 1e-6:
                    continue
                absolute = base + rel
                anchor = anchor_at(absolute, 12, anchors)
                confidence = anchor.confidence if anchor is not None else STRONG
                note = "âncora reproduzida" if anchor is not None else "sem nome físico"
                found.append(
                    {
                        "base": base,
                        "relative": rel,
                        "stride": stride,
                        "pair": (names[pair[0]], names[pair[1]]),
                        "max_error": max(err_x, err_y, err_z),
                        "mean_error": float(
                            np.mean(np.abs(a[:, 0] + b[:, 0]))
                            + np.mean(np.abs(a[:, 1] - b[:, 1]))
                            + np.mean(np.abs(a[:, 2] - b[:, 2]))
                        )
                        / 3.0,
                        "confidence": confidence,
                        "anchor": anchor.name if anchor else None,
                        "text": (
                            f"stride 0x{stride:X} base {hex_offset(base)}, "
                            f"{names[pair[0]]} e {names[pair[1]]}, "
                            f"relativo {hex_offset(rel)}: "
                            f"A.x ≈ -B.x, A.y ≈ B.y, A.z ≈ B.z. {note}"
                        ),
                    }
                )
                reported_blocks.add((pair, rel))
        for rel in range(0, stride, 4):
            columns = []
            ok = True
            for index in range(4):
                column = view.f32(base + index * stride + rel)
                if column is None:
                    ok = False
                    break
                columns.append(column)
            if not ok:
                continue
            if all(float(np.max(np.abs(item))) < 1e-8 for item in columns):
                continue
            for pair in ((0, 1), (2, 3)):
                if any(rel - block in range(0, 12) and pair == block_pair for block_pair, block in reported_blocks):
                    continue
                a = columns[pair[0]]
                b = columns[pair[1]]
                err_neg = float(np.max(np.abs(a + b)))
                if err_neg > 1e-4 or float(np.max(np.abs(a))) < 1e-6:
                    continue
                absolute = base + rel
                anchor = anchor_at(absolute, 4, anchors)
                if anchor is None and float(np.max(np.abs(np.diff(a)))) == 0.0:
                    # Igualdade estática de um campo desconhecido não entra.
                    # Negação estática entra: é simetria medida.
                    pass
                found.append(
                    {
                        "base": base,
                        "relative": rel,
                        "stride": stride,
                        "pair": (names[pair[0]], names[pair[1]]),
                        "max_error": err_neg,
                        "mean_error": float(np.mean(np.abs(a + b))),
                        "confidence": STRONG if anchor is None else anchor.confidence,
                        "anchor": anchor.name if anchor else None,
                        "text": (
                            f"stride 0x{stride:X} base {hex_offset(base)}, "
                            f"{names[pair[0]]} e {names[pair[1]]}, "
                            f"relativo {hex_offset(rel)}: A ≈ -B. "
                            f"{'âncora reproduzida' if anchor else 'sem nome físico'}"
                        ),
                    }
                )
    return found


def detect_events(slots: SlotTable, session: SessionData) -> list[dict]:
    if slots.samples < 3 or slots.changed.size == 0:
        return []
    delta = np.abs(np.diff(slots.f32.astype(np.float64), axis=0))
    median = np.median(delta, axis=0)
    threshold = np.maximum(median * 12.0, 2.0)
    abrupt = delta > threshold
    counts = abrupt.sum(axis=1)
    hits = np.flatnonzero(counts >= 3)
    if hits.size == 0:
        return []
    groups: list[list[int]] = []
    for hit in hits.tolist():
        arrival = hit + 1
        if groups and arrival - groups[-1][-1] <= 2:
            groups[-1].append(arrival)
        else:
            groups.append([arrival])
    events = []
    for number, group in enumerate(groups, start=1):
        sample = group[0]
        step = sample - 1
        magnitudes = delta[step]
        order = np.argsort(-magnitudes)
        affected = []
        for index in order.tolist():
            if not abrupt[step, index]:
                continue
            affected.append(
                {
                    "offset": int(slots.offset[index]),
                    "magnitude": float(magnitudes[index]),
                }
            )
            if len(affected) >= 12:
                break
        timestamp = int(session.timestamp[sample]) if sample < len(session.timestamp) else 0
        marker = _nearby_marker(session, sample)
        events.append(
            {
                "id": f"EVENT_{number:03d}",
                "sample": sample,
                "timestamp": timestamp,
                "affected": affected,
                "count": int(counts[step]),
                "marker": marker,
                "label": "memory transition",
                "confidence": POSSIBLE,
            }
        )
    return events


def _nearby_marker(session: SessionData, sample: int) -> str | None:
    for marker in session.markers:
        if abs(int(marker.after_sequence) - sample) <= 3:
            return marker.label
    return None


def marker_windows(session: SessionData, slots: SlotTable) -> list[dict]:
    rows = []
    if slots.samples == 0:
        return rows
    for marker in session.markers:
        center = int(marker.after_sequence)
        before = slots.f32[max(0, center - 15) : center]
        after = slots.f32[center : min(slots.samples, center + 15)]
        if before.shape[0] < 4 or after.shape[0] < 4:
            continue
        shift = np.mean(after, axis=0) - np.mean(before, axis=0)
        order = np.argsort(-np.abs(shift))
        changed = []
        for index in order.tolist():
            if abs(float(shift[index])) < 0.5:
                break
            if float(np.std(before[:, index])) > 0.05:
                continue
            changed.append(
                {
                    "offset": int(slots.offset[index]),
                    "before_mean": float(np.mean(before[:, index])),
                    "after_mean": float(np.mean(after[:, index])),
                }
            )
            if len(changed) >= 8:
                break
        rows.append(
            {
                "label": marker.label,
                "sample": center,
                "timestamp": marker.timestamp_ns,
                "offsets": changed,
            }
        )
    return rows


def udp_relations(session: SessionData, slots: SlotTable, anchors) -> list[dict]:
    if session.udp.shape[1] < 4:
        return []
    channels = []
    width = session.udp.shape[1] - (session.udp.shape[1] % 4)
    for offset in range(0, min(width, 512), 4):
        parsed = udp_f32(session, offset)
        if parsed is None:
            continue
        values, present = parsed
        if int(present.sum()) < 8:
            continue
        if float(np.nanstd(values[present])) < 1e-8:
            continue
        channels.append((offset, values, present))
        if len(channels) >= 48:
            break
    chosen = []
    for index in _candidate_indexes(slots, anchors, 40):
        offset = int(slots.offset[index])
        if anchor_at(offset, 4, anchors) is not None:
            continue
        chosen.append(index)
    scales = (1.0, -1.0, 1000.0, -1000.0, 0.001, -0.001)
    found = []
    for index in chosen:
        memory = slots.f32[:, index].astype(np.float32)
        for udp_off, values, present in channels:
            for scale in scales:
                scaled = (memory * np.float32(scale)).astype(np.float32)
                both = present & np.isfinite(memory) & np.isfinite(values)
                if int(both.sum()) < max(8, int(0.9 * len(memory))):
                    continue
                err = np.abs(scaled[both] - values[both].astype(np.float32))
                maximum = float(err.max())
                if maximum > 1e-4:
                    continue
                exact = maximum == 0.0 and int(both.sum()) == len(memory)
                if not exact:
                    continue
                mem_off = int(slots.offset[index])
                found.append(
                    {
                        "memory": mem_off,
                        "udp": udp_off,
                        "scale": scale,
                        "max_error": maximum,
                        "mean_error": float(err.mean()),
                        "samples": int(both.sum()),
                        "exact": True,
                        "confidence": STRONG,
                        "text": (
                            f"{hex_offset(mem_off)} · {scale:g} igual a UDP {hex_offset(udp_off)} "
                            f"em {int(both.sum())} amostras. Igualdade exata, não só correlação."
                        ),
                    }
                )
    found.sort(key=lambda row: (row["exact"], -row["samples"]), reverse=True)
    return found[:20]


def structures(view: RigView, slots: SlotTable, anchors, symmetry_rows: list[dict], _bit_rows: list[dict]) -> list[dict]:
    candidates = []
    number = 1
    for layout in stride_layouts():
        base = layout["base"]
        stride = layout["stride"]
        count = layout["count"]
        if view.cover(base, stride * count) is None:
            continue
        changing = []
        static = []
        vectors = []
        states = []
        for rel in range(0, stride, 4):
            rates = []
            amplitudes = []
            uniques = []
            ok = True
            for index in range(count):
                offset = base + index * stride + rel
                slot = slots.index_of(offset)
                column = view.f32(offset)
                if column is None or slot is None:
                    ok = False
                    break
                denom = max(slots.samples - 1, 1)
                rates.append(slots.change_count[slot] / denom)
                amplitudes.append(float(np.nanmax(np.abs(column))) if column.size else 0.0)
                uniques.append(int(np.unique(slots.i32[:, slot]).size) if slots.change_count[slot] else 1)
            if not ok:
                continue
            if sum(rate > 0.01 for rate in rates) >= 3:
                changing.append(rel)
            if all(rate == 0 for rate in rates) and max(amplitudes) > 1e-6:
                static.append(rel)
            if all(unique <= 6 for unique in uniques) and any(rate > 0 for rate in rates):
                if anchor_at(base + rel, 4, anchors) is None and documented_at(base + rel, 4) is None:
                    states.append(rel)
        for rel in range(0, stride, 16):
            raw = view.cover(base + rel, 16)
            if raw is None or np.any(raw[:, 12:16]):
                continue
            xyz = np.ascontiguousarray(raw[:, :12]).view("<f4").reshape(-1, 3).astype(np.float64)
            if np.isfinite(xyz).all() and _vector_ok(xyz):
                vectors.append(rel)
        symmetric = sorted({row["relative"] for row in symmetry_rows if row["base"] == base})
        pointers = _repeated_pointers(view, base, stride, count)
        if not changing and not symmetric:
            continue
        candidates.append(
            {
                "id": number,
                "base": base,
                "stride": stride,
                "elements": count,
                "names": list(layout["names"]),
                "label": layout["label"],
                "changing": changing[:24],
                "static": static[:24],
                "symmetric": symmetric[:24],
                "pointers": pointers,
                "vectors": vectors[:24],
                "states": states[:24],
                "confidence": PROBABLE if layout["label"] else POSSIBLE,
                "note": "Estrutura repetida já usada como âncora de geometria. Campos internos sem âncora continuam sem nome.",
            }
        )
        number += 1
    return candidates


def _repeated_pointers(view: RigView, base: int, stride: int, count: int) -> list[int]:
    found = []
    for rel in range(0, stride - 7, 8):
        values = []
        ok = True
        for index in range(count):
            raw = view.cover(base + index * stride + rel, 8)
            if raw is None:
                ok = False
                break
            blob = np.ascontiguousarray(raw).view("<u8").reshape(-1)
            if np.unique(blob).size > 2:
                ok = False
                break
            values.append(int(blob[0]))
        if not ok or len(set(values)) != 1:
            continue
        value = values[0]
        if not _plausible_pointer(value):
            continue
        found.append(rel)
    return found


def pointer_candidates(view: RigView, anchors) -> list[dict]:
    found = []
    seen = set()
    for region in view.session.regions:
        first = (region.offset + 7) & ~7
        offset = first
        end = region.offset + region.size
        while offset + 8 <= end:
            if anchor_at(offset, 8, anchors) or documented_at(offset, 8):
                offset += 8
                continue
            raw = view.cover(offset, 8)
            if raw is None:
                offset += 8
                continue
            values = np.ascontiguousarray(raw).view("<u8").reshape(-1)
            if np.unique(values).size > 2:
                offset += 8
                continue
            value = int(values[0])
            if not _plausible_pointer(value):
                offset += 8
                continue
            if offset in seen:
                offset += 8
                continue
            seen.add(offset)
            mapped = _mapped(view.session.meta.get("maps") or "", value)
            found.append(
                {
                    "offset": offset,
                    "value": value,
                    "stable": True,
                    "mapped": mapped,
                    "confidence": POSSIBLE,
                    "text": (
                        f"{hex_offset(offset)} permanece estável em 0x{value:X}, "
                        "alinhado a 8. Não foi seguido."
                        + (" Cai numa faixa mapeada no início da captura." if mapped else "")
                    ),
                }
            )
            offset += 8
            if len(found) >= 24:
                return found
    return found


def _mapped(maps: str, value: int) -> bool:
    for line in maps.splitlines():
        if "-" not in line:
            continue
        span = line.split()[0]
        try:
            start_s, end_s = span.split("-", 1)
            start = int(start_s, 16)
            end = int(end_s, 16)
        except ValueError:
            continue
        if start <= value < end:
            return True
    return False
