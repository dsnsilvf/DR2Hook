"""Texturas procedurais da pista sintética (Pillow).

Todas têm lado em potência de dois, como as do jogo, para poderem virar DXT1 (sem alfa) ou DXT5
(com alfa) no lugar de uma textura existente. A fonte é PNG sem perda em `source/textures/`; o
viewer usa a cópia WebP em `tex/`. Cada função recebe um gerador aleatório com semente fixa: a
mesma semente dá os mesmos pixels.
"""

from __future__ import annotations

import math
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


def _wrap_noise(w: int, h: int, sigma: float, blur: float, rng: random.Random) -> Image.Image:
    """Ruído cinza (L) que emenda nas bordas: borra uma grade 3×3 de cópias e recorta o centro."""
    tile = ImageChops.offset(Image.effect_noise((w, h), sigma), rng.randrange(w), rng.randrange(h))
    big = Image.new("L", (3 * w, 3 * h))
    for i in range(3):
        for j in range(3):
            big.paste(tile, (i * w, j * h))
    return big.filter(ImageFilter.GaussianBlur(blur)).crop((w, h, 2 * w, 2 * h)) if blur else tile


def _stretch(gray: Image.Image, gain: float) -> Image.Image:
    """Aumenta o contraste em torno de 128 (o ruído borrado fica quase todo perto do meio)."""
    return gray.point(lambda v: max(0, min(255, int(128 + (v - 128) * gain))))


def asphalt(rng: random.Random) -> Image.Image:
    """Asfalto, 512 px para os 10 m da pista (~2 cm/px): u atravessa a pista (0 = borda esquerda), v corre
    ao longo dela e emenda (a textura repete a cada 10 m). Granulado fino de brita, manchas largas e suaves,
    trilho de borracha discreto no meio, algumas trincas e faixas brancas gastas nas bordas."""
    n = 512
    img = _tint(_stretch(_wrap_noise(n, n, 60, 0.7, rng), 1.6), (50, 50, 53), (96, 96, 100))
    # manchas largas (remendos, desgaste): só mexem no brilho, bem de leve
    blotch = _stretch(_wrap_noise(n, n, 80, 24, rng), 5.0).point(lambda v: 222 + v * 33 // 255)
    img = ImageChops.multiply(img, Image.merge("RGB", [blotch] * 3))
    # pedrinhas claras e escuras de 1–2 px
    _speckles(img, 9000, [(112, 112, 114), (126, 124, 120), (98, 96, 92), (30, 30, 32), (36, 36, 38)], 1, rng)
    # trilho de borracha: escurece faixas largas onde passam as rodas (constante em v, então emenda)
    lane = Image.new("L", (n, 1), 255)
    for x in range(n):
        u = x / (n - 1)
        dark = sum(0.13 * math.exp(-((u - c) / 0.09) ** 2) for c in (0.36, 0.64))
        dust = 0.07 * math.exp(-((min(u, 1 - u)) / 0.06) ** 2)  # beira mais clara: poeira e pouco uso
        lane.putpixel((x, 0), max(0, min(255, int(255 * (1 - dark + dust)))))
    img = ImageChops.multiply(img, Image.merge("RGB", [lane.resize((n, n))] * 3))
    # trincas finas, desenhadas também uma volta acima/abaixo para emendar em v
    d = ImageDraw.Draw(img)
    for _ in range(5):
        x, y = rng.uniform(60, n - 60), rng.uniform(0, n)
        pts = [(x, y)]
        for _ in range(rng.randint(6, 14)):
            x += rng.uniform(-14, 14)
            y += rng.uniform(4, 16)
            pts.append((x, y))
        for dy in (-n, 0, n):
            d.line([(px, py + dy) for px, py in pts], fill=(26, 26, 28), width=1)
    # faixas brancas de borda (~15 cm, a ~30 cm da beira), gastas
    paint = Image.new("RGB", (n, n), (214, 214, 206))
    mask = Image.new("L", (n, n), 0)
    md = ImageDraw.Draw(mask)
    for x0 in (14, n - 14 - 8):
        md.rectangle((x0, 0, x0 + 7, n - 1), fill=255)
    wear = _stretch(_wrap_noise(n, n, 90, 1.5, rng), 2.5).point(lambda v: 255 if v > 70 else 120)
    mask = ImageChops.multiply(mask, wear)
    img = Image.composite(paint, img, mask)
    return img


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


def needles(rng: random.Random) -> Image.Image:
    """Agulhas de pinheiro, opaca: u dá a volta na copa, v desce do topo (0) até a borda da saia (255).
    Riscos escuros e claros descendo, e a ponta de baixo mais escura (sombra da saia de cima)."""
    img = _tint(_noise(256, 50, 0.8, rng), (18, 48, 24), (52, 98, 50))
    d = ImageDraw.Draw(img)
    for _ in range(420):
        x, y = rng.randrange(256), rng.randrange(-20, 256)
        ln = rng.randint(10, 34)
        col = rng.choice([(14, 36, 18), (24, 58, 30), (66, 116, 60), (78, 124, 64)])
        d.line((x, y, x + rng.randint(-5, 5), y + ln), fill=col, width=rng.choice((1, 1, 2)))
    shade = Image.linear_gradient("L").point(lambda v: 255 - max(0, v - 150))
    return ImageChops.multiply(img, Image.merge("RGB", [shade] * 3)).filter(ImageFilter.SMOOTH)


def leaves(rng: random.Random) -> Image.Image:
    """Folhagem de bétula, opaca e repetível: manchas de folhas em verdes claros sobre fundo escuro."""
    img = _tint(_noise(256, 40, 2.0, rng), (44, 70, 26), (78, 108, 40))
    _speckles(img, 1400, [(96, 136, 48), (120, 158, 60), (140, 170, 70), (60, 90, 32), (36, 58, 22)], 3, rng)
    _speckles(img, 300, [(170, 190, 90), (150, 170, 60)], 1, rng)
    return img.filter(ImageFilter.SMOOTH)


def birch_bark(rng: random.Random) -> Image.Image:
    """Casca de bétula: branco acinzentado com traços pretos horizontais (lenticelas) e manchas."""
    img = _tint(_noise(128, 25, 0.6, rng).resize((128, 256)), (196, 194, 186), (238, 236, 230))
    d = ImageDraw.Draw(img)
    for _ in range(70):
        x, y = rng.randrange(-10, 128), rng.randrange(256)
        d.line((x, y, x + rng.randint(6, 26), y + rng.randint(-1, 1)), fill=(36, 34, 32), width=rng.choice((1, 2, 2, 3)))
    for _ in range(7):
        x, y = rng.randrange(128), rng.randrange(256)
        d.ellipse((x - rng.randint(6, 16), y - rng.randint(3, 8), x + rng.randint(6, 16), y + rng.randint(3, 8)),
                  fill=(28, 26, 24))
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


# marcas inventadas das placas (linha k da textura = placa k); fundo, letra, faixa
ADS = [
    ("DR2HOOK", (20, 30, 90), (250, 250, 250), (230, 40, 40)),
    ("PNEUS RINGO", (250, 200, 20), (20, 20, 20), (20, 20, 20)),
    ("TURBO BRASA", (200, 30, 30), (255, 240, 200), (255, 200, 0)),
    ("CAFE DA PISTA", (30, 110, 60), (255, 255, 255), (250, 210, 60)),
]


def ads(rng: random.Random) -> Image.Image:
    """Placas de publicidade 4:1, uma marca por linha de 128 px (v = k/4 a (k+1)/4)."""
    img = Image.new("RGB", (512, 512))
    d = ImageDraw.Draw(img)
    for k, (text, bg, fg, stripe) in enumerate(ADS):
        y0 = 128 * k
        d.rectangle((0, y0, 511, y0 + 127), fill=bg)
        d.rectangle((0, y0 + 104, 511, y0 + 115), fill=stripe)
        d.rectangle((0, y0, 511, y0 + 5), fill=(235, 235, 235))
        d.rectangle((0, y0 + 122, 511, y0 + 127), fill=(235, 235, 235))
        font = _font(66 if len(text) <= 8 else 50)
        box = d.textbbox((0, 0), text, font=font)
        tw, th = box[2] - box[0], box[3] - box[1]
        d.text(((512 - tw) / 2 - box[0], y0 + (104 - th) / 2 - box[1]), text, fill=fg, font=font)
    noise = _noise(512, 10, 0.5, rng).convert("RGB")
    return ImageChops.multiply(img, ImageChops.add(noise, Image.new("RGB", (512, 512), (128, 128, 128))))


# paleta de cores sólidas, uma por faixa de 16 px (de cima para baixo), para peças pintadas
PAINT = [
    (200, 35, 30), (30, 70, 170), (240, 190, 20), (40, 140, 60), (235, 235, 230), (240, 120, 20),
    (60, 62, 66), (150, 152, 156), (20, 20, 22), (20, 150, 160), (110, 50, 150), (25, 35, 55),
    (180, 180, 175), (120, 90, 60), (90, 120, 160), (245, 245, 240),
]
PAINT_RED, PAINT_BLUE, PAINT_YELLOW, PAINT_GREEN, PAINT_WHITE, PAINT_ORANGE, PAINT_DARK, PAINT_GREY, \
    PAINT_BLACK, PAINT_TEAL, PAINT_PURPLE, PAINT_GLASS, PAINT_CONCRETE, PAINT_WOOD, PAINT_STEEL, PAINT_CANVAS = range(16)


def paint(rng: random.Random) -> Image.Image:
    img = Image.new("RGB", (256, 256))
    d = ImageDraw.Draw(img)
    for k, c in enumerate(PAINT):
        d.rectangle((0, 16 * k, 255, 16 * k + 15), fill=c)
    noise = _noise(256, 12, 0.8, rng).convert("RGB")
    return ImageChops.multiply(img, ImageChops.add(noise, Image.new("RGB", (256, 256), (128, 128, 128))))


CONTAINER_COLORS = [(170, 45, 35), (35, 80, 140), (50, 110, 70), (200, 130, 30)]


def container(rng: random.Random) -> Image.Image:
    """Chapa ondulada de contêiner: 4 cores, uma por faixa de 64 px, nervuras verticais e ferrugem."""
    img = Image.new("RGB", (256, 256))
    d = ImageDraw.Draw(img)
    for k, c in enumerate(CONTAINER_COLORS):
        y0 = 64 * k
        d.rectangle((0, y0, 255, y0 + 63), fill=c)
        for x in range(0, 256, 8):
            d.rectangle((x, y0, x + 2, y0 + 63), fill=tuple(int(v * 0.72) for v in c))
            d.line((x + 4, y0, x + 4, y0 + 63), fill=tuple(min(255, int(v * 1.15)) for v in c))
        d.rectangle((0, y0, 255, y0 + 3), fill=tuple(int(v * 0.6) for v in c))
        d.rectangle((0, y0 + 60, 255, y0 + 63), fill=tuple(int(v * 0.6) for v in c))
    _speckles(img, 160, [(110, 60, 30), (90, 50, 25)], 2, rng)
    return img


def building(rng: random.Random) -> Image.Image:
    """Fachada: reboco claro com duas fileiras de janelas (vidro escuro e caixilho branco) por ladrilho."""
    img = _tint(_noise(256, 20, 1.0, rng), (205, 205, 198), (232, 232, 226))
    d = ImageDraw.Draw(img)
    for row in (0, 128):
        d.rectangle((0, row + 118, 255, row + 127), fill=(160, 160, 155))  # friso entre andares
        for col in range(0, 256, 64):
            d.rectangle((col + 10, row + 30, col + 54, row + 96), fill=(245, 245, 245))
            d.rectangle((col + 14, row + 34, col + 50, row + 92), fill=(40, 55, 75))
            d.line((col + 32, row + 34, col + 32, row + 92), fill=(245, 245, 245), width=3)
            d.polygon([(col + 14, row + 34), (col + 30, row + 34), (col + 14, row + 60)], fill=(80, 100, 125))
    return img


def hay(rng: random.Random) -> Image.Image:
    """Palha: fios curtos dourados em v (o lado do fardo); a metade de baixo é a face redonda (espiral)."""
    img = _tint(_noise(128, 40, 0.6, rng), (150, 120, 50), (215, 185, 100))
    d = ImageDraw.Draw(img)
    for _ in range(500):
        x, y = rng.randrange(128), rng.randrange(64)
        d.line((x, y, x + rng.randint(-2, 2), y + rng.randint(3, 8)), fill=rng.choice([(120, 95, 40), (230, 205, 120)]))
    for r in range(4, 64, 5):
        d.ellipse((64 - r, 96 - r // 2, 64 + r, 96 + r // 2), outline=(140, 110, 45))
    return img


# nome do arquivo (sem extensão) -> fábrica. A ordem é estável: a semente de cada uma vem da posição.
def boulder(rng: random.Random) -> Image.Image:
    """Pedra solta, sem neve: cinza manchado, rachaduras escuras e líquen verde-amarelado. Repete nas bordas."""
    img = _tint(_stretch(_wrap_noise(256, 256, 60, 4.0, rng), 2.2), (78, 78, 76), (156, 152, 142))
    d = ImageDraw.Draw(img)
    for _ in range(40):
        x, y = rng.randrange(256), rng.randrange(256)
        pts = [(x, y)]
        for _ in range(rng.randint(2, 5)):
            x, y = x + rng.randint(-14, 14), y + rng.randint(-14, 14)
            pts.append((x, y))
        d.line(pts, fill=(52, 52, 50), width=1)
    _speckles(img, 160, [(120, 128, 78), (138, 140, 86), (104, 112, 70), (170, 166, 120)], 4, rng)
    _speckles(img, 500, [(60, 60, 58), (180, 176, 168)], 1, rng)
    return img.filter(ImageFilter.SMOOTH)


def bush(rng: random.Random) -> Image.Image:
    """Arbusto: folhas miúdas em verde escuro, mais fechado e mais escuro que a bétula."""
    img = _tint(_wrap_noise(256, 256, 40, 2.0, rng), (22, 40, 18), (52, 76, 30))
    _speckles(img, 1800, [(64, 92, 36), (78, 106, 42), (40, 62, 26), (26, 44, 18), (90, 112, 48)], 2, rng)
    _speckles(img, 160, [(120, 136, 60)], 1, rng)
    return img.filter(ImageFilter.SMOOTH)


# paleta do público (uma faixa por cor, como PAINT): peles, cabelos, calças e camisetas
PEOPLE = [
    (226, 186, 152), (184, 132, 96), (112, 76, 52), (44, 32, 24), (186, 150, 90), (52, 70, 108),
    (34, 34, 38), (200, 40, 36), (36, 84, 170), (240, 200, 40), (236, 236, 230), (46, 140, 70),
    (240, 120, 30), (120, 60, 140), (130, 132, 136), (30, 150, 160),
]
SKINS = (0, 1, 2)
HAIRS = (3, 4, 6)
PANTS = (5, 6, 14)
SHIRTS = (7, 8, 9, 10, 11, 12, 13, 15)


def people(rng: random.Random) -> Image.Image:
    img = Image.new("RGB", (64, 256))
    d = ImageDraw.Draw(img)
    for k, c in enumerate(PEOPLE):
        d.rectangle((0, 16 * k, 63, 16 * k + 15), fill=c)
    noise = _noise(64, 10, 0.8, rng).resize((64, 256)).convert("RGB")
    return ImageChops.multiply(img, ImageChops.add(noise, Image.new("RGB", (64, 256), (128, 128, 128))))


def roof(rng: random.Random) -> Image.Image:
    """Telha de barro: fiadas de 32 px com a sombra embaixo de cada uma e telhas de 32 px desencontradas.
    Repete nas bordas (o telhado usa u e v em metros)."""
    img = _tint(_wrap_noise(128, 128, 30, 1.5, rng), (150, 62, 38), (196, 96, 58))
    d = ImageDraw.Draw(img)
    for row in range(4):
        y0 = 32 * row
        d.rectangle((0, y0 + 26, 127, y0 + 31), fill=(96, 40, 26))
        d.line((0, y0, 127, y0), fill=(214, 120, 80))
        for col in range(4):
            x = 32 * col + (16 if row % 2 else 0)
            d.line((x % 128, y0 + 2, x % 128, y0 + 26), fill=(120, 50, 32), width=2)
    _speckles(img, 120, [(90, 80, 60), (70, 90, 50)], 1, rng)
    return img


TEXTURES: dict[str, Callable[[random.Random], Image.Image]] = {
    "synth_asphalt_d": asphalt,
    "synth_grass_d": grass,
    "synth_gravel_d": gravel,
    "synth_dirt_d": dirt,
    "synth_curb_d": curb,
    "synth_barrier_d": concrete_barrier,
    "synth_tyre_d": tyre,
    "synth_fence_d": fence,
    "synth_pine_d": needles,
    "synth_birch_d": leaves,
    "synth_bark_d": bark,
    "synth_board_100_d": lambda rng: board(rng, "100", (240, 200, 30), (20, 20, 20)),
    "synth_board_50_d": lambda rng: board(rng, "50", (240, 200, 30), (20, 20, 20)),
    "synth_banner_d": lambda rng: board(rng, "DR2HOOK", (20, 30, 90), (250, 250, 250)),
    "synth_seats_d": seats,
    "synth_rock_d": rock,
    "synth_birch_bark_d": birch_bark,  # no fim: a semente das outras vem da posição
    "synth_ads_d": ads,
    "synth_paint_d": paint,
    "synth_container_d": container,
    "synth_building_d": building,
    "synth_hay_d": hay,
    "synth_boulder_d": boulder,
    "synth_bush_d": bush,
    "synth_people_d": people,
    "synth_roof_d": roof,
}


def make_all(seed: int) -> dict[str, Image.Image]:
    out = {}
    for n, (name, fn) in enumerate(TEXTURES.items()):
        out[name] = fn(random.Random(seed * 1000 + n))
    return out
