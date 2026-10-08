#!/usr/bin/env python3
"""Recorta as fontes do viewer3d e gera `src/app/icons.hpp`.

O editor embute três fontes (o CMake põe os bytes no executável, ver CMakeLists.txt):

- `Inter-Regular.ttf` e `Inter-SemiBold.ttf`: Inter 4.1 (<https://github.com/rsms/inter>, OFL 1.1), só com os
  caracteres que os painéis usam (Latim-1, aspas, travessões, setas, sinal de menos);
- `Lucide.ttf`: os ícones Lucide (<https://lucide.dev>, pacote npm `lucide-static`, ISC) listados em `ICONS`.

Sem tabelas de layout (o ImGui não usa kerning nem ligaduras) e sem as instruções de hinting (o FreeType do
editor usa o autohinter leve). Para trocar ou acrescentar um ícone, edite `ICONS` e rode de novo:

    python3 tools/viewer3d/assets/fonts/subset_fonts.py --inter <pasta do Inter-4.1.zip> --lucide <pasta package do lucide-static>

As fontes originais não ficam no repositório: o zip do Inter vem das releases do GitHub e o lucide-static de
`https://registry.npmjs.org/lucide-static/-/lucide-static-<versão>.tgz`.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
VIEWER = os.path.dirname(os.path.dirname(HERE))
HEADER = os.path.join(VIEWER, "src", "app", "icons.hpp")

# Faixas de texto (pares inclusivos). Iguais às que o editor passa ao ImGui (kTextRanges em icons.hpp).
TEXT_RANGES = (
    (0x0020, 0x007E),  # ASCII
    (0x00A0, 0x00FF),  # Latim-1: acentos, °, ·, ×, ±
    (0x2013, 0x2014),  # – —
    (0x2018, 0x201E),  # aspas
    (0x2022, 0x2022),  # •
    (0x2026, 0x2026),  # …
    (0x2190, 0x2193),  # ← ↑ → ↓
    (0x2212, 0x2212),  # −
)

# nome no Lucide → nome da macro (ICON_<nome>)
ICONS = {
    # ferramentas
    "mouse-pointer-2": "SELECT",
    "move": "MOVE",
    "rotate-cw": "ROTATE",
    # arquivo e histórico
    "folder-open": "OPEN",
    "save": "SAVE",
    "undo-2": "UNDO",
    "redo-2": "REDO",
    "history": "HISTORY",
    "log-out": "QUIT",
    # edição
    "copy": "DUPLICATE",
    "trash-2": "DELETE",
    "rotate-ccw": "RESTORE",
    "arrow-down-to-line": "SETTLE",
    "land-plot": "ALIGN",
    "scan-eye": "FRAME",
    "x": "CLOSE",
    # encaixe e chão
    "magnet": "SNAP",
    "mountain-snow": "FOLLOW_GROUND",
    # eixos do gizmo
    "globe": "AXES_WORLD",
    "cuboid": "AXES_LOCAL",
    # exibição e camadas
    "eye": "SHOW",
    "eye-off": "HIDE",
    "mountain": "TERRAIN",
    "box": "OBJECTS",
    "trees": "TREES",
    "map": "FAR_TERRAIN",
    "flag": "GATES",
    "spline": "AI_LINE",
    "video": "CAMERAS",
    "car": "CAR",
    "grid-2x2": "GRIDS",
    "route": "ROUTE",
    "ruler": "DISTANCE",
    # painéis
    "list-tree": "SCENE",
    "sliders-horizontal": "INSPECTOR",
    "layout-dashboard": "LAYOUT",
    "keyboard": "KEYBOARD",
    "search": "SEARCH",
    # jogo
    "play": "PLAY",
    "pause": "PAUSE",
    "square": "STOP",
    "gamepad-2": "GAME",
    "loader-circle": "BUSY",
    # estados
    "circle-alert": "ALERT",
    "triangle-alert": "WARNING",
    "check": "CHECK",
    "info": "INFO",
}


def subset_font(src: str, dst: str, unicodes: list[int]) -> TTFont:
    opts = subset.Options()
    opts.layout_features = []
    opts.hinting = False
    opts.notdef_outline = True
    opts.glyph_names = False
    opts.name_IDs = ["*"]  # copyright e licença ficam na fonte
    opts.drop_tables += ["GSUB", "GPOS", "GDEF", "STAT", "DSIG", "fvar", "gvar", "avar", "HVAR", "MVAR"]
    font = subset.load_font(src, opts)
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=unicodes)
    sub.subset(font)
    subset.save_font(font, dst, opts)
    return TTFont(dst)


def find(root: str, *names: str) -> str:
    for name in names:
        path = os.path.join(root, name)
        if os.path.exists(path):
            return path
    raise SystemExit(f"não achei {' nem '.join(names)} em {root}")


def utf8_escape(cp: int) -> str:
    return "".join(f"\\x{b:02x}" for b in chr(cp).encode("utf-8"))


def write_header(icons: dict[str, int]) -> None:
    lo, hi = min(icons.values()), max(icons.values())
    width = max(len(n) for n in icons)
    lines = [
        "// Gerado por assets/fonts/subset_fonts.py: não edite à mão. Ícones Lucide (ISC) no Lucide.ttf recortado.",
        "#pragma once",
        "",
        "#include <imgui.h>",
        "",
    ]
    for name, cp in icons.items():
        lines.append(f'#define ICON_{name:<{width}} "{utf8_escape(cp)}"  // U+{cp:04X}')
    lines += [
        "",
        "namespace dr2::app::icons {",
        "",
        "// Faixas que o ImGui lê das fontes (pares inclusivos, zero no fim).",
        "inline constexpr ImWchar kTextRanges[] = {"
        + ", ".join(f"0x{a:04X}, 0x{b:04X}" for a, b in TEXT_RANGES) + ", 0};",
        f"inline constexpr ImWchar kIconRanges[] = {{0x{lo:04X}, 0x{hi:04X}, 0}};",
        "",
        "}  // namespace dr2::app::icons",
        "",
    ]
    with open(HEADER, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--inter", required=True, help="pasta do Inter-4.1.zip extraído (tem extras/ttf/)")
    ap.add_argument("--lucide", required=True, help="pasta package/ do lucide-static extraído (tem font/)")
    a = ap.parse_args()

    text = sorted({cp for lo, hi in TEXT_RANGES for cp in range(lo, hi + 1)})
    for style in ("Regular", "SemiBold"):
        src = find(a.inter, f"extras/ttf/Inter-{style}.ttf", f"Inter-{style}.ttf")
        font = subset_font(src, os.path.join(HERE, f"Inter-{style}.ttf"), text)
        missing = [cp for cp in text if cp not in font.getBestCmap() and cp != 0xAD]  # hífen invisível
        print(f"Inter-{style}: {len(font.getGlyphOrder())} glifos"
              + (f", faltam {', '.join(f'U+{cp:04X}' for cp in missing)}" if missing else ""))

    codepoints = json.load(open(find(a.lucide, "font/codepoints.json"), encoding="utf-8"))
    unknown = [n for n in ICONS if n not in codepoints]
    if unknown:
        raise SystemExit(f"ícones que o lucide-static não tem: {', '.join(unknown)}")
    icons = {macro: int(codepoints[name]) for name, macro in ICONS.items()}
    font = subset_font(find(a.lucide, "font/lucide.ttf"), os.path.join(HERE, "Lucide.ttf"), sorted(icons.values()))
    cmap = font.getBestCmap()
    lost = [n for n, cp in icons.items() if cp not in cmap]
    if lost:
        raise SystemExit(f"o recorte perdeu: {', '.join(lost)}")
    version = json.load(open(find(a.lucide, "package.json"), encoding="utf-8")).get("version", "?")
    print(f"Lucide {version}: {len(icons)} ícones")
    write_header(icons)
    print(f"gravei {os.path.relpath(HEADER, VIEWER)}")
    for name in ("Inter-Regular.ttf", "Inter-SemiBold.ttf", "Lucide.ttf"):
        print(f"  {name}: {os.path.getsize(os.path.join(HERE, name))} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
