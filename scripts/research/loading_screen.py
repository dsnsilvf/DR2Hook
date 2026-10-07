#!/usr/bin/env python3
"""Tela de carregamento de uma pista própria: foto aérea, traçado que se desenha e marcadores.

A tela é a cena `fe/screens/loading/map_stats_loading` de `game_1.dat:frontend/databases/loadingScreen.pssg`
(docs/reverse_engineering/track_loading.md §11). O UINODESWITCH `map_switch` escolhe o filho pelo nome
`<pista>_<rota>` (campos 2 e 13 da rota no catálogo); sem filho com esse nome a tela fica preta.
Cada filho é um xr para `fe/component/loading/<pista>_<rota>`, com os marcadores, o `spline` e o `bg_image`.

O que este script faz:

- clona o componente de uma rota do jogo (padrão: Montalegre) com o nome novo, põe a largada e a chegada
  nos pontos do traçado e acrescenta o filho no `map_switch`; o resto do arquivo fica byte-idêntico;
- desenha o `_spline` (1344×992) a partir do `layout.json` do synthtrack: R = linha fina, G = progresso
  (1 na largada até quase 0 na chegada, traço grosso; o shader `ui_spline_reveal` revela pelo G), B = joker;
- monta a foto aérea (2304×1296) a partir de uma captura do viewer3d, com tilt-shift e vinheta.

    python3 scripts/research/loading_screen.py --overlay captures/overlay --name dr2hook_ring \\
        --layout examples/tracks/synthetic__dr2hook_ring/source/layout.json --track-dir examples/tracks/synthetic__dr2hook_ring

A foto é renderizada pelo viewer3d (`--hide lines --shot-size`, 2× e reduzida), quase a pino, com a câmera
calculada para o traçado cair na área do `_spline` (SPLINE_BOX); o `_spline` e os marcadores saem da mesma
projeção, então a linha fica em cima da estrada da foto.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from custom_track import loading_names, write_tpk  # noqa: E402
from tools.egodata.pssg import PSSGFile, PSSGNode  # noqa: E402

PSSG_PATH = "frontend/databases/loadingScreen.pssg"
SCENE = "fe/screens/loading/map_stats_loading"
COMPONENT = "fe/component/loading/{}"
SPLINE_SIZE = (1344, 992)    # 0,01 unidade de UI por pixel; marcador = pixel / 100 (y negativo)
AERIAL_SIZE = (2304, 1296)
# área do traçado no _spline, como nas rotas do jogo (Montalegre: colunas 43–1227, linhas 202–819)
SPLINE_BOX = (60, 190, 1284, 830)
LINE_W, MASK_W = 12, 36      # largura da linha (R) e do traço de progresso (G), em pixels
MASK_END = 12 / 255          # G na chegada (a Montalegre termina em ~12)
FINISH_GAP_PX = 80           # chegada antes da largada num circuito fechado, para os ícones não se cobrirem
# Foto e _spline na mesma cena: o spline_image tem 0,01 unidade por pixel a partir de (0, 0); o bg_image (quad de
# 23,04×12,96 unidades) termina a animação de entrada em escala ~1, centrado em (9,6, −5,4): foto ≈ spline + (192, 108).
# Medido nos prints da carga (registro da linha branca e das linhas da pista em três quadros, mesmo resultado nos três):
# foto = 0,9975·spline + (194, 104,5). O zoom lento da cena vale para os dois, então o alinhamento não muda.
SPLINE_TO_PHOTO = (0.9975, 194.0, 104.5)
FOV_Y = 0.9          # OrbitCamera::kFovY do viewer3d
AERIAL_PITCH = 1.15  # quase a pino como as fotos do jogo, mas inclinada o bastante para as árvores terem volume
SHOT_SS = 2          # captura em 2× e redução: antisserrilhado


# ---- traçado ----

def route_points(layout: dict) -> tuple[list[tuple[float, float, float]], list[float]]:
    pts = [tuple(s["p"]) for s in layout["samples"]]
    dist = [s["s"] for s in layout["samples"]]
    return pts, dist


def spline_transform(pts):
    """Sem foto: mundo -> pixel do _spline, x para a direita, z para baixo, centrado em SPLINE_BOX."""
    xs, zs = [p[0] for p in pts], [p[2] for p in pts]
    bx0, by0, bx1, by1 = SPLINE_BOX
    scale = min((bx1 - bx0) / (max(xs) - min(xs)), (by1 - by0) / (max(zs) - min(zs)))
    cx, cz = (max(xs) + min(xs)) / 2, (max(zs) + min(zs)) / 2
    ox, oy = (bx0 + bx1) / 2, (by0 + by1) / 2
    return lambda p: (ox + (p[0] - cx) * scale, oy + (p[2] - cz) * scale)


def draw_spline(layout: dict, closed: bool, to_px):
    from PIL import Image, ImageDraw

    ss = 4  # superamostragem para a borda suave
    pts, dist = route_points(layout)
    px = [to_px(p) for p in pts]
    if closed:
        px.append(px[0])
        dist.append(dist[-1] + math.dist(pts[-1], pts[0]))
    total = dist[-1]
    w, h = SPLINE_SIZE
    chans = []
    for width, grad in ((LINE_W, False), (MASK_W, True)):
        im = Image.new("L", (w * ss, h * ss), 0)
        d = ImageDraw.Draw(im)
        # do fim para o começo: onde o traço se sobrepõe (fechamento do circuito), vale o trecho que aparece antes
        for i in range(len(px) - 2, -1, -1):
            v = 1.0 - (1.0 - MASK_END) * dist[i] / total if grad else 1.0
            a, b = px[i], px[i + 1]
            col = round(255 * v)
            d.line([(a[0] * ss, a[1] * ss), (b[0] * ss, b[1] * ss)], fill=col, width=width * ss)
            r = width * ss / 2
            d.ellipse([a[0] * ss - r, a[1] * ss - r, a[0] * ss + r, a[1] * ss + r], fill=col)
        chans.append(im.resize((w, h), Image.LANCZOS))
    blue = Image.new("L", (w, h), 0)
    return Image.merge("RGB", (chans[0], chans[1], blue)), pts, dist


def marker_positions(to_px, pts, dist, closed: bool):
    """Largada no começo do traçado; chegada no fim (num circuito, FINISH_GAP_PX antes da largada)."""
    start = to_px(pts[0])
    finish = to_px(pts[-1])
    if closed:
        for p in reversed(pts):
            q = to_px(p)
            if math.dist(q, start) >= FINISH_GAP_PX:
                finish = q
                break
    return [("start", start), ("finish", finish)]


# ---- foto aérea ----

def project(cam, size, pts):
    """Mundo -> pixel da captura, com a câmera orbital do viewer3d (glm::lookAt + perspective, y para baixo)."""
    import numpy as np

    yaw, pitch, dist, tx, ty, tz = cam
    w, h = size
    target = np.array([tx, ty, tz])
    eye = target + dist * np.array([math.cos(pitch) * math.sin(yaw), math.sin(pitch), math.cos(pitch) * math.cos(yaw)])
    f = (target - eye) / np.linalg.norm(target - eye)
    s = np.cross(f, [0.0, 1.0, 0.0])
    s /= np.linalg.norm(s)
    u = np.cross(s, f)
    d = np.asarray(pts, float) - eye
    k = 1.0 / math.tan(FOV_Y / 2)
    z = d @ f
    nx, ny = k * (w / h) ** -1 * (d @ s) / z, k * (d @ u) / z
    return np.c_[(nx + 1) / 2 * w, (1 - ny) / 2 * h]


def fit_camera(pts, box, size, yaw=0.0, pitch=AERIAL_PITCH):
    """Câmera que põe o traçado centrado em `box` (pixels da foto), encostando nos lados que limitam."""
    import numpy as np

    p = np.asarray(pts, float)
    bx0, by0, bx1, by1 = box
    cam = [yaw, pitch, 1000.0, (p[:, 0].min() + p[:, 0].max()) / 2, float(p[:, 1].mean()),
           (p[:, 2].min() + p[:, 2].max()) / 2]
    for _ in range(60):
        q = project(cam, size, p)
        lo, hi = q.min(0), q.max(0)
        r = max((hi[0] - lo[0]) / (bx1 - bx0), (hi[1] - lo[1]) / (by1 - by0))
        cam[2] *= r
        # metros por pixel no centro, pela diferença de projeção de 1 m em x e em z
        c = project(cam, size, [[cam[3], cam[4], cam[5]], [cam[3] + 1, cam[4], cam[5]], [cam[3], cam[4], cam[5] + 1]])
        q = project(cam, size, p)
        lo, hi = q.min(0), q.max(0)
        cam[3] += ((lo[0] + hi[0]) / 2 - (bx0 + bx1) / 2) / (c[1][0] - c[0][0])
        cam[5] += ((lo[1] + hi[1]) / 2 - (by0 + by1) / 2) / (c[2][1] - c[0][1])
    return cam


def photo_to_spline(a: float, b: float) -> tuple[float, float]:
    s, ox, oy = SPLINE_TO_PHOTO
    return (a - ox) / s, (b - oy) / s


def spline_box_in_photo():
    s, ox, oy = SPLINE_TO_PHOTO
    bx0, by0, bx1, by1 = SPLINE_BOX
    return (bx0 * s + ox, by0 * s + oy, bx1 * s + ox, by1 * s + oy)


def render_aerial(viewer: str, track_dir: str, cam, out_ppm: str, hide: str = "lines") -> None:
    import subprocess

    w, h = AERIAL_SIZE
    cmd = [viewer, "--track", track_dir, "--fresh", "--autosave", "0", "--panels", "0", "--wait-textures",
           "--frames", "40", "--tex-max-side", "0", "--tex-mb", "3072", "--hide", hide,
           "--camera", ",".join(f"{v:.5f}" for v in cam), "--shot-size", f"{w * SHOT_SS}x{h * SHOT_SS}",
           "--screenshot", out_ppm]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def base_shot(path: str) -> str:
    """Captura do mesmo ângulo só com terreno e asfalto (sem objetos nem árvores): dá a máscara dos objetos."""
    root, ext = os.path.splitext(path)
    return root + "_base" + ext


def aerial_image(path: str, base_path: str | None = None):
    """Foto aérea a partir da captura do viewer3d.

    Com a captura sem objetos (base_path), a diferença entre as duas é a máscara de árvores e enfeites: dela saem a
    sombra projetada (sol de cima à esquerda) e o escurecido em volta dos grupos. A grama ganha manchas de cor e brilho
    em várias escalas e um grão fino, como numa foto de satélite; o asfalto e os objetos ficam como estão.
    """
    from PIL import Image, ImageEnhance, ImageFilter
    import numpy as np

    def arr(im):
        return np.asarray(im).astype(np.float32) / 255

    def img(a):
        return Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))

    def blur(a, r):
        return arr(img(a).filter(ImageFilter.GaussianBlur(r)))

    im = Image.open(path).convert("RGB")
    if base_path:
        full, base = arr(im), arr(Image.open(base_path).convert("RGB"))
        hh, ww = full.shape[:2]
        k = ww / AERIAL_SIZE[0]  # a captura é SHOT_SS vezes maior
        obj = blur((np.abs(full - base).max(-1) > 0.05).astype(np.float32), 0.4 * k)
        dx, dy = round(3.5 * k), round(4.5 * k)
        shadow = np.zeros_like(obj)
        shadow[dy:, dx:] = obj[:-dy, :-dx]
        shadow = blur(shadow, 1.25 * k)
        ao = blur(obj, 15 * k)

        def noise(cells, seed):
            r = np.random.default_rng(seed).random((cells[1], cells[0])).astype(np.float32)
            return arr(img(r).resize((ww, hh), Image.BICUBIC))

        n1 = 0.55 * noise((12, 7), 1) + 0.3 * noise((40, 22), 2) + 0.15 * noise((140, 80), 3)
        n2 = 0.6 * noise((20, 11), 4) + 0.4 * noise((90, 50), 5)
        grass = blur(np.clip((base[..., 1] - np.maximum(base[..., 0], base[..., 2])) / 0.06, 0, 1), 0.75 * k)
        g = (grass * (1 - obj))[..., None]
        dry = np.clip((n1 - 0.45) / 0.3, 0, 1)[..., None]
        tone = (1 - dry) * np.array([0.95, 1.0, 0.9], np.float32) + dry * np.array([1.12, 1.03, 0.8], np.float32)
        bright = (0.88 + 0.2 * n2)[..., None]
        grain = (np.random.default_rng(6).random((hh, ww)).astype(np.float32) - 0.5) * 0.04
        a = full * (1 - g) + full * tone * bright * (1 + grain[..., None]) * g
        ground = (1 - obj)[..., None]
        a = a * (1 - 0.55 * shadow[..., None] * ground) * (1 - 0.3 * ao[..., None] * ground)
        im = img(a)
    if im.size != AERIAL_SIZE:
        im = im.resize(AERIAL_SIZE, Image.LANCZOS)  # a redução da captura maior faz o antisserrilhado
    im = ImageEnhance.Color(im).enhance(0.95)
    im = ImageEnhance.Contrast(im).enhance(1.08)
    im = im.filter(ImageFilter.UnsharpMask(radius=1.2, percent=40, threshold=2))
    # tilt-shift leve: só as bordas de cima e de baixo desfocam (a pista fica no meio, nítida)
    blur6 = im.filter(ImageFilter.GaussianBlur(6))
    h = AERIAL_SIZE[1]
    y = np.abs(np.linspace(-1.0, 1.0, h))
    m = np.clip((y - 0.55) / 0.45, 0.0, 1.0) ** 1.5
    mask = Image.fromarray(np.repeat((m * 255).astype(np.uint8)[:, None], AERIAL_SIZE[0], 1))
    im = Image.composite(blur6, im, mask)
    # vinheta e um tom quente leve, como as fotos do jogo
    a = arr(im)
    yy, xx = np.mgrid[-1:1:h * 1j, -1:1:AERIAL_SIZE[0] * 1j]
    vig = 1.0 - 0.3 * np.clip((xx ** 2 + yy ** 2) / 2.0, 0, 1) ** 1.2
    a = a * vig[..., None] * np.array([1.03, 1.0, 0.94], np.float32)
    return img(a)


# ---- loadingScreen.pssg ----

def library(p: PSSGFile, kind: str) -> PSSGNode:
    return next(c for c in p.root.children if c.type_name == "LIBRARY" and c.get("type") == kind)


def child(n: PSSGNode, nick: str) -> PSSGNode:
    return next(c for c in n.children if c.nickname == nick)


def find(n: PSSGNode, nick: str) -> PSSGNode | None:
    if n.nickname == nick:
        return n
    for c in n.children:
        r = find(c, nick)
        if r is not None:
            return r
    return None


def ensure_all_read(n: PSSGNode) -> None:
    """Nós copiados não têm payload original; marca os dados como novos para o serializador regravá-los."""
    n._raw_payload = None
    for c in n.children:
        ensure_all_read(c)


def userdata(n: PSSGNode) -> list[PSSGNode]:
    return [c for c in n.children if c.type_name == "USERDATA"]


def transform(n: PSSGNode) -> PSSGNode:
    return next(c for c in n.children if c.type_name == "TRANSFORM")


class IdMaker:
    """Ids novos no formato do arquivo (prefixo + número), sem colidir com nenhum id existente."""

    def __init__(self, p: PSSGFile):
        self.used = {n.id for n in p.find_nodes(lambda n: n.id is not None)}

    def new(self, old: str) -> str:
        prefix = old.rstrip("0123456789")
        k = 9000
        while f"{prefix}{k}" in self.used:
            k += 1
        self.used.add(f"{prefix}{k}")
        return f"{prefix}{k}"


def clone_object(p: PSSGFile, ids: IdMaker, ref: str, **attrs) -> str:
    """Copia um objeto de biblioteca (UINODEBASE, FENODECOUNT...) referenciado por '#id'; devolve a ref nova."""
    for lib in (c for c in p.root.children if c.type_name == "LIBRARY"):
        for o in lib.children:
            if o.id == ref[1:]:
                new = copy.deepcopy(o)
                ensure_all_read(new)
                PSSGFile.set_attr(new, "id", ids.new(o.id))
                for k, v in attrs.items():
                    PSSGFile.set_attr(new, k, v)
                lib.children.insert(lib.children.index(o) + 1, new)
                return "#" + new.id
    raise KeyError(ref)


def set_translation(n: PSSGNode, x: float, y: float) -> None:
    m = list(struct.unpack(">16f", transform(n).data))
    m[12], m[13] = x, y
    transform(n).data = struct.pack(">16f", *m)


def add_route(p: PSSGFile, src: str, dst: str, markers: list[tuple[str, tuple[float, float]]]) -> None:
    nodes = library(p, "NODE")
    if any(r.nickname == COMPONENT.format(dst) for r in nodes.children):
        raise SystemExit(f"{COMPONENT.format(dst)} já existe no arquivo")
    ids = IdMaker(p)
    base = next(r for r in nodes.children if r.nickname == COMPONENT.format(src))
    comp = copy.deepcopy(base)
    ensure_all_read(comp)
    PSSGFile.set_attr(comp, "nickname", COMPONENT.format(dst))
    PSSGFile.set_attr(comp, "id", COMPONENT.format(dst) + "_root")
    suffix = "!" + dst

    def relabel(n: PSSGNode) -> None:
        for c in n.children:
            if c.type_name == "NODE":
                PSSGFile.set_attr(c, "id", c.nickname + suffix)
                relabel(c)
            elif c.type_name == "USERDATA":
                obj = c.get("object")
                # FEANIMDATA (roteiro do reveal e dos marcadores) é só leitura e chama os marcadores pelo
                # nome; fica compartilhado. Os nós de UI (estado da animação) e a contagem são copiados.
                if not obj.startswith("#femad"):
                    PSSGFile.set_attr(c, "object", clone_object(p, ids, obj))
    relabel(comp)

    # marcadores: sector_marker_00, _01... na ordem pedida, cada um com o xr do tipo
    mk = child(comp, "markers")
    old = [c for c in mk.children if c.type_name == "NODE"]
    tmpl = old[0]
    mk.children = [c for c in mk.children if c.type_name != "NODE"]
    objs = library(p, "UINODEBASE")
    for i, (kind, (u, v)) in enumerate(markers):
        n = copy.deepcopy(tmpl)
        ensure_all_read(n)
        nick = f"sector_marker_{i:02d}"
        PSSGFile.set_attr(n, "nickname", nick)
        PSSGFile.set_attr(n, "id", nick + suffix)
        ud = userdata(n)[0]
        PSSGFile.set_attr(ud, "object", clone_object(p, ids, ud.get("object"), xr=COMPONENT.format(f"{kind}_marker")))
        set_translation(n, u / 100, -v / 100)
        mk.children.append(n)
    # contagem de nós do componente (FENODECOUNT): tudo abaixo da raiz
    def nodes_below(n):
        return sum(1 + nodes_below(c) for c in n.children if c.type_name == "NODE")
    fenc = next(u for u in userdata(comp) if u.get("object").startswith("#fenc"))
    fobj = next(o for o in library(p, "FENODECOUNT").children if o.id == fenc.get("object")[1:])
    PSSGFile.set_attr(fobj, "c", nodes_below(comp))
    nodes.children.insert(nodes.children.index(base) + 1, comp)

    # filho do map_switch
    scene = next(r for r in nodes.children if r.nickname == SCENE)
    sw = find(scene, "map_switch")
    tmpl = child(sw, src)
    n = copy.deepcopy(tmpl)
    ensure_all_read(n)
    PSSGFile.set_attr(n, "nickname", dst)
    PSSGFile.set_attr(n, "id", dst)
    ud = userdata(n)[0]
    PSSGFile.set_attr(ud, "object", clone_object(p, ids, ud.get("object"), xr=COMPONENT.format(dst)))
    sw.children.append(n)


def build_pssg(data: bytes, src: str, dst: str, markers) -> bytes:
    p = PSSGFile(data)
    assert p.serialize() == data, "regravação do loadingScreen.pssg não bate com o original"
    add_route(p, src, dst, markers)
    p.file_size = 0  # recalcula o tamanho no cabeçalho
    out = p.serialize()
    q = PSSGFile(out)  # relê para conferir
    assert find(library(q, "NODE"), COMPONENT.format(dst)) is not None
    return out


def read_game_pssg(game: str) -> bytes:
    from tools.egodata.cli import _open

    return _open(game, "game_1.dat").read(PSSG_PATH)


def main() -> None:
    from tools.egodata.cli import DEFAULT_GAME

    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--overlay", required=True)
    ap.add_argument("--name", required=True, help="nome da pista (campo 2 da rota)")
    ap.add_argument("--route", default="route_0")
    ap.add_argument("--layout", required=True, help="layout.json do synthtrack")
    ap.add_argument("--open", action="store_true", help="traçado aberto (rally): sem fechar o circuito")
    ap.add_argument("--track-dir", help="pista exportada para o viewer3d (examples/tracks/...): renderiza a foto aérea")
    ap.add_argument("--viewer", default="build/viewer3d/viewer3d")
    ap.add_argument("--shot", default="build/re/loading_aerial.ppm", help="onde fica a captura do viewer3d")
    ap.add_argument("--pitch", type=float, default=AERIAL_PITCH, help="inclinação da câmera (1,5 = a pino)")
    ap.add_argument("--reuse-shot", action="store_true", help="não renderiza de novo: usa a captura que já está em --shot")
    ap.add_argument("--from-route", default="montalegre_rallycross_route_0", help="componente do jogo usado de molde")
    ap.add_argument("--pssg", help="loadingScreen.pssg original (padrão: lido do game_1.dat)")
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--png-dir", help="grava aqui também os PNGs (prévia)")
    ap.add_argument("--tpk-template", default="build/re/tpk/montalegre_rallycross_route_0.tpk")
    args = ap.parse_args()

    layout = json.load(open(args.layout))
    closed = not args.open
    pts, _ = route_points(layout)
    aerial = None
    if args.track_dir:
        # a foto manda: a câmera põe a pista na área do traçado e o _spline sai da mesma projeção
        cam = fit_camera(pts, spline_box_in_photo(), AERIAL_SIZE, pitch=args.pitch)
        print("câmera da foto: " + ",".join(f"{v:.3f}" for v in cam))
        if not args.reuse_shot:
            os.makedirs(os.path.dirname(args.shot) or ".", exist_ok=True)
            render_aerial(args.viewer, args.track_dir, cam, args.shot)
            render_aerial(args.viewer, args.track_dir, cam, base_shot(args.shot), hide="lines,obj,tree")
        base = base_shot(args.shot)
        aerial = aerial_image(args.shot, base if os.path.exists(base) else None)

        def to_px(p):
            a, b = project(cam, AERIAL_SIZE, [p])[0]
            return photo_to_spline(a, b)
    else:
        to_px = spline_transform(pts)
    spline, pts, dist = draw_spline(layout, closed, to_px)
    markers = marker_positions(to_px, pts, dist, closed)
    route_name = f"{args.name}_{args.route}"

    data = open(args.pssg, "rb").read() if args.pssg else read_game_pssg(args.game)
    out = build_pssg(data, args.from_route, route_name, markers)
    dst = os.path.join(args.overlay, PSSG_PATH)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as fh:
        fh.write(out)
    print(f"{dst}: {COMPONENT.format(route_name)} + filho do map_switch, {len(out)} bytes (+{len(out) - len(data)})")
    for kind, (u, v) in markers:
        print(f"  {kind}: pixel ({u:.0f}, {v:.0f}) -> ({u / 100:.4f}, {-v / 100:.4f})")

    template = open(args.tpk_template, "rb").read()
    loading_dir = os.path.join(args.overlay, "frontend", "streamed_textures", "loading")
    image_name, spline_name = loading_names(args.name, args.route)
    imgs = [(spline_name, spline)]
    if aerial is not None:
        imgs.append((image_name, aerial))
    for tpk_name, img in imgs:
        write_tpk(os.path.join(loading_dir, tpk_name + ".tpk"), template, tpk_name, img)
        print(f"  {tpk_name}.tpk ({img.width}×{img.height})")
        if args.png_dir:
            os.makedirs(args.png_dir, exist_ok=True)
            img.save(os.path.join(args.png_dir, tpk_name + ".png"))


if __name__ == "__main__":
    main()
