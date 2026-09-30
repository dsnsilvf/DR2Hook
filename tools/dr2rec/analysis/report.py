"""Relatório da sessão. Evidência medida, hipótese separada, confiança explícita."""

from __future__ import annotations

import html
import json
from pathlib import Path

from tools.dr2rec.analysis.load import hex_offset


def _num(value) -> str:
    if value is None:
        return "—"
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def render_md(result: dict) -> str:
    summary = result["summary"]
    lines = [
        "# DR2 Vehicle Black Box",
        "",
        "SESSION SUMMARY",
        "",
        f"- Duration: {summary['duration_s']:.3f} s",
        f"- Samples: {summary['samples']}",
        f"- Capture interval: {summary['interval_ns']} ns",
        f"- PhysicsRig: {summary['physics_rig']}",
        f"- Executable: {summary['executable']}",
        f"- Recorder: {summary['recorder']}",
        f"- Regions: {summary['regions']}",
        f"- Markers: {summary['markers']}",
        f"- Closed: {summary['closed']}",
        "",
        "ANCHOR VERIFICATION",
        "",
    ]
    if not result["anchor_checks"]:
        lines.append("Nenhuma âncora pôde ser checada nesta captura.")
    for row in result["anchor_checks"]:
        lines.append(
            f"- {row['name']}: {row['verdict']}. "
            f"erro máximo {_num(row['max_error'])}, erro médio {_num(row['mean_error'])}. "
            f"Confiança: {row['confidence']}"
        )
    lines += ["", "MEMORY ACTIVITY", "", "Offset | Type | Change Rate | Unique Values | Confidence", "--- | --- | --- | --- | ---"]
    for row in result["activity"][:40]:
        lines.append(
            f"{hex_offset(row['offset'])} | {row['type']} | {row['change_rate']:.4f} | {row['unique']} | {row['confidence']}"
        )
    if len(result["activity"]) > 40:
        lines.append("")
        lines.append(f"A tabela completa tem {len(result['activity'])} offsets em activity.tsv.")
    lines += ["", "STRUCTURES", ""]
    if not result["structures"]:
        lines.append("Nenhuma estrutura repetida coberta pela captura.")
    for row in result["structures"]:
        lines += [
            f"STRUCTURE CANDIDATE #{row['id']:02d}",
            "",
            f"- base: {hex_offset(row['base'])}",
            f"- stride: 0x{row['stride']:X}",
            f"- elements: {row['elements']} ({', '.join(row['names'])})",
            f"- label: {row['label']}",
            f"- changing fields: {_offs(row['changing'])}",
            f"- static fields: {_offs(row['static'])}",
            f"- symmetric fields: {_offs(row['symmetric'])}",
            f"- likely pointers: {_offs(row['pointers'])}",
            f"- likely vectors: {_offs(row['vectors'])}",
            f"- likely states: {_offs(row['states'])}",
            f"- Confiança: {row['confidence']}",
            f"- {row['note']}",
            "",
        ]
    lines += ["EVENTS", ""]
    if not result["events"]:
        lines.append("Nenhuma memory transition passou do limiar.")
    for event in result["events"]:
        affected = ", ".join(hex_offset(item["offset"]) for item in event["affected"][:8])
        magnitude = ", ".join(_num(item["magnitude"]) for item in event["affected"][:4])
        extra = f" Marcador próximo: {event['marker']}." if event["marker"] else ""
        lines.append(
            f"- {event['id']} timestamp={event['timestamp']} sample={event['sample']} "
            f"label={event['label']} affected=[{affected}] magnitude=[{magnitude}].{extra} "
            f"Confiança: {event['confidence']}"
        )
    if result["marker_windows"]:
        lines.append("")
        lines.append("Marcadores manuais, antes e depois:")
        for marker in result["marker_windows"]:
            offsets = ", ".join(
                f"{hex_offset(item['offset'])} {_num(item['before_mean'])} → {_num(item['after_mean'])}"
                for item in marker["offsets"]
            ) or "nenhum offset estável mudou de média"
            lines.append(f"- {marker['label']} @ sample {marker['sample']}: {offsets}")
    lines += ["", "CORRELATIONS", ""]
    if not result["correlations"]:
        lines.append("Nenhum par passou de |correlation| = 0.98 nos lags testados.")
    for row in result["correlations"]:
        lines.append(f"- {row['text']}. Confiança: {row['confidence']}")
    lines += ["", "MATHEMATICAL RELATIONS", ""]
    if not result["relations"]:
        lines.append("Nenhuma relação simples fora das âncoras passou do limiar.")
    for row in result["relations"]:
        lines.append(
            f"- {row['text']}. erro máximo {_num(row['max_error'])}, "
            f"erro médio {_num(row['mean_error'])}. Confiança: {row['confidence']}"
        )
    lines += ["", "SYMMETRY", ""]
    if not result["symmetry"]:
        lines.append("Nenhuma simetria de stride passou do limiar.")
    for row in result["symmetry"]:
        lines.append(
            f"- {row['text']}. erro máximo {_num(row['max_error'])}, "
            f"erro médio {_num(row['mean_error'])}. Confiança: {row['confidence']}"
        )
    lines += ["", "UDP CORRELATION", ""]
    if not result["udp"]:
        lines.append("Sessão sem igualdade exata nova entre memória e UDP, ou sem UDP.")
    for row in result["udp"]:
        lines.append(f"- {row['text']} Confiança: {row['confidence']}")
    lines += ["", "DISCRETE STATES", ""]
    if not result["discrete"]:
        lines.append("Nenhum offset desconhecido ficou em poucos valores.")
    for row in result["discrete"]:
        freq = ", ".join(f"{item['value']}×{item['count']}" for item in row["value_freq"])
        lines.append(f"- {row['text']}. valores: {freq}. Confiança: {row['confidence']}")
    lines += ["", "BITFIELDS", ""]
    if not result["bitfields"]:
        lines.append("Nenhum inteiro desconhecido teve bits que ligam e desligam.")
    for row in result["bitfields"]:
        for bit in row["bits"]:
            joined = ""
            if row.get("event_bits") and bit["bit"] in row["event_bits"]:
                joined = f" {hex_offset(row['offset'])} bit {bit['bit']} mudou durante memory transition {row['event_bits'][bit['bit']]}."
            lines.append(
                f"- {hex_offset(row['offset'])} bit {bit['bit']}: {bit['transitions']} transições, "
                f"{bit['on_samples']} amostras em 1.{joined} Confiança: {row['confidence']}"
            )
        for left, right in row["exclusive"]:
            lines.append(
                f"- {hex_offset(row['offset'])} bit {left} e bit {right} não ficaram ligados juntos."
            )
    lines += ["", "POINTERS", ""]
    if not result["pointers"]:
        lines.append("Nenhum valor estável com cara de ponteiro. Nenhum ponteiro foi seguido.")
    for row in result["pointers"]:
        lines.append(f"- {row['text']} Confiança: {row['confidence']}")
    lines += ["", "DATA FLOW", ""]
    if not result["dataflow"]:
        lines.append("Não executado, ou nenhum deslocamento imediato classificável. Passe --exe para varrer o executável local.")
    for row in result["dataflow"]:
        lines += [
            f"FIELD {hex_offset(row['offset'])}",
            "SOURCE",
            "  desconhecido",
            "WRITER",
        ]
        if row["writers"]:
            for hit in row["writers"]:
                lines.append(f"  RVA 0x{hit['disp_rva']:X} {hit['kind']} store. Confiança: {hit['confidence']}")
        else:
            lines.append("  nenhum site classificado")
        lines.append("READER")
        if row["readers"]:
            for hit in row["readers"]:
                lines.append(f"  RVA 0x{hit['disp_rva']:X} {hit['kind']} load. Confiança: {hit['confidence']}")
        else:
            lines.append("  nenhum site classificado")
        lines += ["RESULT", "  desconhecido", ""]
    lines += ["", "INTERESTING OFFSETS", ""]
    if not result["interesting"]:
        lines.append("Nenhum offset desconhecido se destacou.")
    for row in result["interesting"]:
        lines.append(
            f"- {hex_offset(row['offset'])} interesse {row['interest']:.0f}. "
            f"{row['why']} Confiança: {row['confidence']}"
        )
    lines += ["", "UNKNOWN", ""]
    if not result["unknown"]:
        lines.append("Nada fora das âncoras ficou acima do corte de interesse.")
    for row in result["unknown"]:
        entropy = row.get("entropy")
        extra = f" Entropia aproximada dos bytes: {_num(entropy)}." if entropy is not None else ""
        lines.append(f"- {hex_offset(row['offset'])}: {row['why']}{extra} Confiança: {row['confidence']}")
    lines += ["", "NEXT EXPERIMENTS", ""]
    for line in result["experiments"]:
        lines.append(f"- {line}")
    lines.append("")
    return "\n".join(lines)


def _offs(values: list[int]) -> str:
    if not values:
        return "—"
    return ", ".join(hex_offset(value) for value in values)


def render_html(markdown: str) -> str:
    body = html.escape(markdown)
    return (
        "<!DOCTYPE html><html lang=\"pt\"><head><meta charset=\"utf-8\">"
        "<title>DR2 Vehicle Black Box</title>"
        "<style>body{font:14px/1.45 ui-monospace,monospace;margin:2rem;max-width:980px}"
        "pre{white-space:pre-wrap}</style></head><body><pre>"
        f"{body}</pre></body></html>\n"
    )


def render_compare_md(result: dict) -> str:
    lines = [
        "SESSION COMPARISON",
        "",
        f"- Samples A: {result['samples_a']}",
        f"- Samples B: {result['samples_b']}",
        "",
        "CONSTANT DIFFERENCES",
        "",
    ]
    for row in result["constant_diff"][:40]:
        lines.append(
            f"- {hex_offset(row['offset'])}: mediana A {row['median_a']} / mediana B {row['median_b']}"
        )
    if not result["constant_diff"]:
        lines.append("Nenhuma constante divergiu.")
    lines += ["", "DYNAMIC ONLY IN ONE SESSION", ""]
    for row in result["dynamic_one"][:40]:
        lines.append(f"- {hex_offset(row['offset'])}: {row['class']}")
    if not result["dynamic_one"]:
        lines.append("Nenhum offset foi dinâmico em só uma sessão.")
    lines += ["", "SHARED DYNAMIC", ""]
    for row in result["shared_dynamic"][:40]:
        lines.append(
            f"- {hex_offset(row['offset'])}: taxa A {row['rate_a']:.3f}, taxa B {row['rate_b']:.3f}"
        )
    if not result["shared_dynamic"]:
        lines.append("Nenhum offset dinâmico em comum.")
    lines.append("")
    return "\n".join(lines)


def write_report(directory: Path, result: dict) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    markdown = result["markdown"]
    (directory / "report.md").write_text(markdown, encoding="utf-8")
    (directory / "report.html").write_text(render_html(markdown), encoding="utf-8")
    activity = ["offset\ttype\tchange_rate\tunique\tconfidence\tnote"]
    for row in result["activity"]:
        activity.append(
            f"{hex_offset(row['offset'])}\t{row['type']}\t{row['change_rate']:.6f}\t"
            f"{row['unique']}\t{row['confidence']}\t{row['note']}"
        )
    (directory / "activity.tsv").write_text("\n".join(activity) + "\n", encoding="utf-8")
    changemap = []
    for row in result["activity"]:
        if row["change_count"] <= 0:
            continue
        changemap.append(f"{hex_offset(row['offset'])} {row['spark']}")
    (directory / "changemap.txt").write_text("\n".join(changemap) + "\n", encoding="utf-8")
    payload = {key: value for key, value in result.items() if key != "markdown"}
    (directory / "analysis.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, default=_json_default),
        encoding="utf-8",
    )


def write_compare(directory: Path, result: dict, markdown: str) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "compare.md").write_text(markdown, encoding="utf-8")
    (directory / "compare.html").write_text(render_html(markdown), encoding="utf-8")
    (directory / "compare.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2, default=_json_default),
        encoding="utf-8",
    )


def _json_default(value):
    if isinstance(value, (bytes, bytearray)):
        return value.hex()
    raise TypeError(f"não serializável: {type(value)!r}")
