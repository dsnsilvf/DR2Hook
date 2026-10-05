"""Texturas procedurais da pista sintética (Pillow).

Todas têm lado em potência de dois, como as do jogo, para poderem virar DXT1 (sem alfa) ou DXT5
(com alfa) no lugar de uma textura existente. A fonte é PNG sem perda em `source/textures/`; o
viewer usa a cópia WebP em `tex/`. Cada função recebe um gerador aleatório com semente fixa: a
mesma semente dá os mesmos pixels.
"""

from __future__ import annotations

import random
from typing import Callable

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont


def _noise(size: int, sigma: float, blur: float, rng: random.Random) -> Image.Image:
    """Ruído cinza (L) centrado em 128."""
    img = Image.effect_noise((size, size), sigma)
    # effect_noise não aceita semente: desloca por um valor do rng para variar entre texturas
    img = ImageChops.offset(img, rng.randrange(size), rng.randrange(size))
    return img.filter(ImageFilter.GaussianBlur(blur)) if blur else img


def _tint(gray: Image.Image, dark: tuple[int, int, int], light: tuple[int, int, int]) -> Image.Image:
    """Mapeia o cinza entre duas cores."""
    lut = []
    for ch in range(3):
        lut += [int(dark[ch] + (light[ch] - dark[ch]) * v / 255) for v in range(256)]
    return gray.convert("RGB").point(lut)


def _speckles(img: Image.Image, count: int, colors: list[tuple[int, int, int]], rmax: int, rng: random.Random) -> None:
    d = ImageDraw.Draw(img)
    w, h = img.size
    for _ in range(count):
        x, y, r = rng.randrange(w), rng.randrange(h), rng.randint(0, rmax)
        d.ellipse((x - r, y - r, x + r, y + r), fill=rng.choice(colors))


def _font(size: int) -> ImageFont.ImageFont:
    try:
        return ImageFont.load_default(size=size)
    except TypeError:  # Pillow antigo
        return ImageFont.load_default()


def asphalt(rng: random.Random) -> Image.Image:
    """Asfalto: u atravessa a pista (0 = borda esquerda), v corre ao longo dela. Faixas brancas nas bordas."""
    img = _tint(_noise(256, 40, 0.6, rng), (38, 38, 42), (88, 88, 94))
    _speckles(img, 900, [(120, 120, 124), (30, 30, 32), (100, 96, 90)], 1, rng)
    d = ImageDraw.Draw(img)
    for x0 in (6, 244):
        d.rectangle((x0, 0, x0 + 5, 255), fill=(225, 225, 220))
    # marcas de pneu
    for _ in range(6):
        x = rng.randrange(40, 216)
        d.line((x, 0, x + rng.randrange(-12, 12), 255), fill=(30, 30, 32), width=rng.randint(3, 7))
    return img.filter(ImageFilter.SMOOTH)


def grass(rng: random.Random) -> Image.Image:
    img = _tint(_noise(256, 50, 1.2, rng), (52, 92, 34), (110, 150, 62))
    _speckles(img, 500, [(130, 160, 70), (60, 80, 30), (150, 140, 80)], 1, rng)
    return img


def gravel(rng: random.Random) -> Image.Image:
    img = _tint(_noise(256, 30, 0.8, rng), (120, 110, 96), (175, 165, 148))
    _speckles(img, 2500, [(200, 192, 180), (90, 84, 76), (150, 140, 128), (110, 100, 90)], 2, rng)
    return img


def dirt(rng: random.Random) -> Image.Image:
    img = _tint(_noise(256, 45, 2.0, rng), (92, 70, 48), (150, 118, 84))
    _speckles(img, 600, [(80, 60, 40), (170, 140, 100)], 1, rng)
    return img


def curb(rng: random.Random) -> Image.Image:
    """Zebra: blocos vermelhos e brancos ao longo de v."""
    img = Image.new("RGB", (64, 256), (230, 230, 225))
    d = ImageDraw.Draw(img)
    for k in range(0, 256, 64):
        d.rectangle((0, k, 63, k + 31), fill=(200, 30, 28))
    noise = _noise(64, 18, 0.5, rng).resize((64, 256)).convert("RGB")
    return ImageChops.multiply(img, ImageChops.add(noise, Image.new("RGB", (64, 256), (90, 90, 90))))


def concrete_barrier(rng: random.Random) -> Image.Image:
    """Barreira de concreto: metade de cima listrada vermelho/branco, de baixo cinza (linha 0 = topo)."""
    img = _tint(_noise(256, 30, 1.0, rng), (140, 140, 136), (190, 190, 186))
    d = ImageDraw.Draw(img)
    for k in range(0, 256, 64):
        d.rectangle((k, 0, k + 31, 63), fill=(205, 35, 30))
        d.rectangle((k + 32, 0, k + 63, 63), fill=(235, 235, 230))
    return img


def tyre(rng: random.Random) -> Image.Image:
    img = Image.new("RGB", (128, 128), (22, 22, 24))
    d = ImageDraw.Draw(img)
    for cx in (32, 96):
        for cy in (32, 96):
            d.ellipse((cx - 30, cy - 30, cx + 30, cy + 30), fill=(34, 34, 36), outline=(60, 60, 64), width=3)
            d.ellipse((cx - 12, cy - 12, cx + 12, cy + 12), fill=(12, 12, 14))
    return img


def fence(rng: random.Random) -> Image.Image:
    """Alambrado com alfa (as quadrículas são transparentes): vira DXT5."""
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for k in range(-128, 128, 16):
        d.line((k, 0, k + 128, 128), fill=(170, 175, 180, 255), width=2)
        d.line((k + 128, 0, k, 128), fill=(170, 175, 180, 255), width=2)
    d.rectangle((0, 0, 127, 5), fill=(90, 95, 100, 255))
    return img


def foliage(rng: random.Random, color: tuple[int, int, int]) -> Image.Image:
    """Copa de árvore de cartão, com alfa. Linha 0 é o topo da árvore."""
    img = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for layer in range(6):
        top = 10 + layer * 30
        half = 30 + layer * 16
        shade = tuple(max(0, min(255, c + rng.randint(-25, 15))) for c in color)
        d.polygon([(128, top), (128 + half, top + 70), (128 - half, top + 70)], fill=(*shade, 255))
    d.rectangle((120, 200, 136, 255), fill=(80, 55, 35, 255))
    return img


def bark(rng: random.Random) -> Image.Image:
    img = _tint(_noise(64, 40, 0.5, rng).resize((64, 256)), (60, 42, 28), (120, 90, 60))
    return img


def board(rng: random.Random, text: str, bg: tuple[int, int, int], fg: tuple[int, int, int]) -> Image.Image:
    img = Image.new("RGB", (256, 128), bg)
    d = ImageDraw.Draw(img)
    d.rectangle((4, 4, 251, 123), outline=fg, width=6)
    font = _font(64 if len(text) <= 4 else 34)
    box = d.textbbox((0, 0), text, font=font)
    d.text(((256 - (box[2] - box[0])) / 2 - box[0], (128 - (box[3] - box[1])) / 2 - box[1]), text, fill=fg, font=font)
    return img


def seats(rng: random.Random) -> Image.Image:
    img = Image.new("RGB", (128, 128), (40, 60, 140))
    d = ImageDraw.Draw(img)
    for y in range(0, 128, 16):
        d.rectangle((0, y, 127, y + 3), fill=(180, 180, 185))
        for x in range(0, 128, 12):
            d.rectangle((x + 2, y + 6, x + 9, y + 13), fill=(30 + rng.randint(0, 30), 50, 150 + rng.randint(-20, 40)))
    return img


def rock(rng: random.Random) -> Image.Image:
    img = _tint(_noise(256, 60, 3.0, rng), (80, 84, 90), (150, 150, 145))
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, 255, 40), fill=(225, 228, 235))  # neve no topo (v pequeno = topo do morro)
    return img.filter(ImageFilter.GaussianBlur(1.0))


# nome do arquivo (sem extensão) -> fábrica. A ordem é estável: a semente de cada uma vem da posição.
TEXTURES: dict[str, Callable[[random.Random], Image.Image]] = {
    "synth_asphalt_d": asphalt,
    "synth_grass_d": grass,
    "synth_gravel_d": gravel,
    "synth_dirt_d": dirt,
    "synth_curb_d": curb,
    "synth_barrier_d": concrete_barrier,
    "synth_tyre_d": tyre,
    "synth_fence_d": fence,
    "synth_pine_d": lambda rng: foliage(rng, (40, 95, 45)),
    "synth_birch_d": lambda rng: foliage(rng, (110, 150, 60)),
    "synth_bark_d": bark,
    "synth_board_100_d": lambda rng: board(rng, "100", (240, 200, 30), (20, 20, 20)),
    "synth_board_50_d": lambda rng: board(rng, "50", (240, 200, 30), (20, 20, 20)),
    "synth_banner_d": lambda rng: board(rng, "DR2HOOK", (20, 30, 90), (250, 250, 250)),
    "synth_seats_d": seats,
    "synth_rock_d": rock,
}


def make_all(seed: int) -> dict[str, Image.Image]:
    out = {}
    for n, (name, fn) in enumerate(TEXTURES.items()):
        out[name] = fn(random.Random(seed * 1000 + n))
    return out
