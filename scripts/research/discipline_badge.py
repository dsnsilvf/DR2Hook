#!/usr/bin/env python3
"""Selo do DR2Hook para a barra de modos (F3/F4) e os cards da tela de seleção de local.

O jogo desenha os selos de disciplina com texturas só brancas: o desenho inteiro fica no alfa, e o estado apagado é a
mesma textura com menos opacidade. Em `frontend/bundles/b_persistent.pssg`:
- `icon_dis_<x>_small` (80×48): a aba da barra (quick_nav.list, montada em 0x1402c7830);
- `icon_dis_<x>` (128×96): o selo no canto de cada card (switch.discipline_icon.image, 0x140155950).

Aqui a logo vira o mesmo padrão: preto -> transparente, branco e vermelho -> branco opaco (o vermelho conta como
branco, "opção A"), com o desgaste da logo virando alfa parcial. Sai uma cópia do b_persistent.pssg com
`icon_dis_dr2hook` e `icon_dis_dr2hook_small` (ui8x4, um nível) na overlay, e os PNGs para conferir. Com
`--fragment-inc`, grava só as duas texturas como fragmento de enxerto em C++: a dxgi embute e enxerta no
b_persistent.pssg do cache de pistas (src/trackpkg/builder.cpp).

    python3 scripts/research/discipline_badge.py --overlay captures/overlay --png build/re/badge
    python3 scripts/research/discipline_badge.py --fragment-inc src/trackpkg/badge_fragment.inc
"""

from __future__ import annotations

import argparse
import copy
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from tools.egodata.pssg import PSSGFile  # noqa: E402
from tools.uiview.export import Reader  # noqa: E402

GAME = "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"
BUNDLE = "frontend/bundles/b_persistent.pssg"
LOGO = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "assets", "logo_hook.webp")

# (textura nova, molde no bundle, caixa útil (x0, y0, x1, y1) como a dos selos do jogo)
BADGES = [
    ("icon_dis_dr2hook_small", "icon_dis_mixed_small", (4, 2, 76, 46)),
    ("icon_dis_dr2hook", "icon_dis_mixed", (5, 3, 123, 93)),
]


def badge_alpha(logo: Image.Image) -> Image.Image:
    """Logo colorida -> textura branca com o desenho no alfa, recortada no que é visível."""
    rgb = np.asarray(logo.convert("RGB")).astype(np.float32) / 255
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    red = np.clip((r - (g + b) / 2) * 1.6, 0, 1)
    white = np.clip(((r + g + b) / 3 - 0.12) / 0.75, 0, 1) * (1 - red)
    a = (np.clip(white + red, 0, 1) * 255).astype(np.uint8)
    img = Image.fromarray(np.dstack([np.full_like(a, 255)] * 3 + [a]), "RGBA")
    return img.crop(Image.fromarray(np.where(a > 40, 255, 0).astype(np.uint8)).getbbox())


def fit(img: Image.Image, size: tuple[int, int], box: tuple[int, int, int, int]) -> Image.Image:
    """Centraliza `img` na caixa, mantendo a proporção, numa textura `size` transparente."""
    bw, bh = box[2] - box[0], box[3] - box[1]
    k = min(bw / img.width, bh / img.height)
    w, h = max(1, round(img.width * k)), max(1, round(img.height * k))
    # Reduz no alfa pré-multiplicado (branco puro: basta reduzir o alfa).
    alpha = img.getchannel("A").resize((w, h), Image.LANCZOS)
    out = Image.new("L", size, 0)
    out.paste(alpha, (box[0] + (bw - w) // 2, box[1] + (bh - h) // 2))
    white = Image.new("L", size, 255)
    return Image.merge("RGBA", (white, white, white, out))


def add_texture(pssg: PSSGFile, tex_id: str, template_id: str, img: Image.Image):
    for old in pssg.find_by_id(tex_id):
        raise SystemExit(f"{tex_id} já existe no bundle (use o original do jogo como entrada)")
    tpl = pssg.find_by_id(template_id)
    if not tpl:
        raise SystemExit(f"molde {template_id} não achado")
    tpl = tpl[0]
    lib = next(n for n in pssg.find_by_type("LIBRARY") if tpl in n.children)
    t = copy.deepcopy(tpl)
    r, g, b, a = img.split()
    data = Image.merge("RGBA", (b, g, r, a)).tobytes()  # ui8x4 = BGRA, como em ring_objects.py
    for k, v in (("id", tex_id), ("width", img.width), ("height", img.height), ("texelFormat", "ui8x4"),
                 ("numberMipMapLevels", 0)):
        PSSGFile.set_attr(t, k, v)
    block = t.children[0]
    PSSGFile.set_attr(block, "size", len(data))
    node = block.children[0]
    node.data = data
    node._raw_payload = None
    node._orig_data = None
    node._compressed = False
    lib.children.append(t)
    return t


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--overlay", help="pasta da overlay (grava frontend/bundles/b_persistent.pssg)")
    ap.add_argument("--png", help="pasta para os PNGs de conferência")
    ap.add_argument("--fragment-inc", help="grava o fragmento de enxerto como array C++ (.inc)")
    ap.add_argument("--logo", default=LOGO)
    ap.add_argument("--game", default=GAME)
    a = ap.parse_args()

    src = badge_alpha(Image.open(a.logo))
    images = {}
    for tex_id, tpl_id, box in BADGES:
        size = (80, 48) if tex_id.endswith("_small") else (128, 96)
        images[tex_id] = (tpl_id, fit(src, size, box))
        if a.png:
            os.makedirs(a.png, exist_ok=True)
            images[tex_id][1].save(os.path.join(a.png, f"{tex_id}.png"))

    if a.overlay or a.fragment_inc:
        pssg = PSSGFile(Reader(a.game).read("game_1.dat", BUNDLE))
        added = [add_texture(pssg, tex_id, tpl_id, img) for tex_id, (tpl_id, img) in images.items()]
        if a.fragment_inc:
            from pssg_fragment import fragment, write_inc

            data = fragment(pssg, added)
            write_inc(a.fragment_inc, "kBadgeFragment", data, "scripts/research/discipline_badge.py --fragment-inc")
            print(f"{a.fragment_inc}: {len(data)} bytes")
    if a.overlay:
        out = os.path.join(a.overlay, *BUNDLE.split("/"))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out + ".tmp", "wb") as fh:
            fh.write(pssg.serialize())
        os.replace(out + ".tmp", out)
        print(f"{out}: {', '.join(images)}")


if __name__ == "__main__":
    main()
