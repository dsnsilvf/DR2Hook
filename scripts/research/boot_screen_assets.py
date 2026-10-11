#!/usr/bin/env python3
"""Gera `src/core/boot_screen_assets.inc`: o logo e as fontes da tela de boot do DR2Hook (src/core/boot_screen.cpp).

- logo: `docs/assets/logo.png` reduzido para a largura da tela, em BGR de 24 bits comprimido com zlib (a dxgi
  descomprime com o miniz). O fundo do logo vira a cor de fundo da tela (kBootLogoBackground);
- fontes: os recortes do Inter do editor (`tools/viewer3d/assets/fonts`, OFL), carregados com AddFontMemResourceEx.

    python3 scripts/research/boot_screen_assets.py
"""

from __future__ import annotations

import argparse
import os
import zlib

from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
LOGO = os.path.join(ROOT, "docs", "assets", "logo.png")
FONTS = os.path.join(ROOT, "tools", "viewer3d", "assets", "fonts")
OUT = os.path.join(ROOT, "src", "core", "boot_screen_assets.inc")
LOGO_WIDTH = 330


def array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    return f"const unsigned char {name}[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()

    logo = Image.open(LOGO).convert("RGB")
    bg = logo.getpixel((2, 2))
    size = (LOGO_WIDTH, round(logo.height * LOGO_WIDTH / logo.width))
    logo = logo.resize(size, Image.LANCZOS)
    r, g, b = logo.split()
    bgr = Image.merge("RGB", (b, g, r)).tobytes()
    parts = [
        "// Gerado por scripts/research/boot_screen_assets.py; não editar.\n",
        f"constexpr int kBootLogoWidth = {size[0]};\n",
        f"constexpr int kBootLogoHeight = {size[1]};\n",
        f"constexpr unsigned long kBootLogoBackground = 0x{bg[2]:02x}{bg[1]:02x}{bg[0]:02x}; // COLORREF\n",
        array("kBootLogoBgr", zlib.compress(bgr, 9)),
    ]
    for name, file in (("kBootFontRegular", "Inter-Regular.ttf"), ("kBootFontSemiBold", "Inter-SemiBold.ttf")):
        with open(os.path.join(FONTS, file), "rb") as fh:
            parts.append(array(name, fh.read()))
    with open(a.out, "w", encoding="utf-8") as fh:
        fh.write("".join(parts))
    print(f"{a.out}: logo {size[0]}x{size[1]}, fundo {bg}")


if __name__ == "__main__":
    main()
