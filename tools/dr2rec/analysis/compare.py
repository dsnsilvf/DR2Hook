"""Duas sessões. Separa o que ficou parado do que se moveu, sem nomear o motivo."""

from __future__ import annotations

import numpy as np

from tools.dr2rec.analysis.load import SessionData, build_slots


def compare_sessions(left: SessionData, right: SessionData) -> dict:
    a = build_slots(left)
    b = build_slots(right)
    common = sorted(set(a.offset.tolist()) & set(b.offset.tolist()))
    rows = []
    for offset in common:
        ia = a.index_of(offset)
        ib = b.index_of(offset)
        if ia is None or ib is None:
            continue
        rate_a = float(a.change_count[ia] / max(a.samples - 1, 1))
        rate_b = float(b.change_count[ib] / max(b.samples - 1, 1))
        med_a = int(np.median(a.i32[:, ia])) if a.samples else 0
        med_b = int(np.median(b.i32[:, ib])) if b.samples else 0
        dynamic_a = rate_a > 0.01
        dynamic_b = rate_b > 0.01
        if not dynamic_a and not dynamic_b:
            kind = "constante igual" if med_a == med_b else "constante diferente"
        elif dynamic_a and dynamic_b:
            kind = "dinâmico nas duas"
        elif dynamic_a:
            kind = "dinâmico só na sessão A"
        else:
            kind = "dinâmico só na sessão B"
        rows.append(
            {
                "offset": int(offset),
                "class": kind,
                "rate_a": rate_a,
                "rate_b": rate_b,
                "median_a": med_a,
                "median_b": med_b,
            }
        )
    return {
        "samples_a": a.samples,
        "samples_b": b.samples,
        "rows": rows,
        "constant_diff": [row for row in rows if row["class"] == "constante diferente"],
        "dynamic_one": [row for row in rows if row["class"].startswith("dinâmico só")],
        "shared_dynamic": [row for row in rows if row["class"] == "dinâmico nas duas"],
    }
