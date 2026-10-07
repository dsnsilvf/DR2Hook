#!/usr/bin/env python3
"""Registra uma pista nova (nome próprio) no catálogo e monta a pasta dela na overlay.

O que o jogo precisa para achar uma rota pelo nome (docs/reverse_engineering/track_loading.md §10):

- uma linha em `track_model` (rota) cujo campo 2 é o nome da pista (%track%), 6 a location e 13 a
  rota; o benchmark/AutoStage acha a rota por esses três nomes (0x140590fa0);
- uma linha em `track` (campo 12 da rota aponta para ela) e as linhas de `track_model_surface`
  da rota (campo 3 = id da rota);
- os arquivos em `tracks/locations/<loc>/<pista>/...`, que aqui vêm todos da overlay.

As linhas são cópias das da pista de origem com ids novos. O pacote `locations/<loc>__<pista>.nefs`
não existe para o nome novo; a LoadProbe monta o da origem no lugar (`track_alias`).

    python3 scripts/research/custom_track.py --overlay captures/overlay --name dr2hook_ring \\
        --from-track montalegre_rallycross --route route_0 \\
        --file tracksplit.pssg=build/terrain/ring_tracksplit.pssg --file route_0/track.vis=build/re/track_ring.vis
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from ctpk import Ctpk, Row, decode, djb2, encode  # noqa: E402

TRACK_MODEL = 0xB8E12AAA
TRACK = 0x1072479A
TRACK_MODEL_SURFACE = 0x29199252
LOCATION = 0x7604D91E
COUNTRY = 0xD3F53B19
# país: 4 nome em maiúsculas, 7 nome normal (a tela de carregamento mostra o do país, não o da location)
COUNTRY_TEXT_FIELDS = {4: "{p}_country_caps", 7: "{p}_country"}
LENGTH_FIELD = 15  # extensão da rota em metros (float)
# chaves de texto: rota (17 nome, 18 em maiúsculas) e location (10 nome, 11 descrição, 13 "local, país",
# 19 minúsculas). Apontam para lng_dr2hook_*, que a dxgi responde com dr2hook_texts.ini
# (a chave com _caps sem texto próprio vira a sem _caps em maiúsculas).
ROUTE_TEXT_FIELDS = {17: "{p}_{route}", 18: "{p}_{route}_caps"}
LOCATION_TEXT_FIELDS = {10: "{p}_location_caps", 11: "{p}_desc", 13: "{p}_location_full", 19: "{p}_location"}
# campos de track_model com nome de imagem do frontend: 74 = tela de carregamento, 75 = mapa (`_spline`);
# o jogo lê frontend/streamed_textures/loading/<nome>.tpk
LOADING_FIELDS = (74, 75)
GAME = "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"


def field(fields, n):
    return next(v for fn, _, v in fields if fn == n)


def replace(fields, changes: dict[int, int]):
    out = []
    for fn, wt, v in fields:
        if fn in changes:
            v = changes[fn] if wt == 0 else (changes[fn], None)
        out.append((fn, wt, v))
    return out


def find_route(c: Ctpk, track: str, route: str) -> Row:
    th, rh = djb2(track), djb2(route)
    for r in c.tables[TRACK_MODEL].rows:
        f = r.fields
        if field(f, 2) == th and field(f, 13) == rh:
            return r
    raise SystemExit(f"rota {track}/{route} não está no catálogo")


def loading_names(name: str, route: str) -> tuple[str, str]:
    return f"{name}_{route}", f"{name}_{route}_spline"


def write_tpk(path: str, template: bytes, name: str, img) -> None:
    """`.tpk` BC1 de um nível (molde: um tpk do jogo; cabeçalho de 0x80, nome em 0x44, dados em 0x80)."""
    import io
    import struct

    buf = io.BytesIO()
    img.convert("RGB").save(buf, "DDS", pixel_format="DXT1")
    data = buf.getvalue()[128:]
    w, h = img.size
    head = bytearray(template[:0x80])
    struct.pack_into("<3I", head, 11 * 4, w, h, 0)
    struct.pack_into("<I", head, 14 * 4, len(data))
    head[0x44:0x80] = name.encode().ljust(0x80 - 0x44, b"\0")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(bytes(head) + data[: w * h // 2])


def text_prefix(name: str) -> str:
    return f"lng_dr2hook_{name.removeprefix('dr2hook_')}"


def register(c: Ctpk, name: str, src_track: str, route: str, length: float | None = None) -> tuple[int, int]:
    src = find_route(c, src_track, route)
    name_h = c.add_string(name)
    # Já registrada (rodar de novo regrava as mesmas linhas)
    for r in c.tables[TRACK_MODEL].rows:
        if field(r.fields, 2) == name_h and field(r.fields, 13) == djb2(route):
            return r.id, field(r.fields, 12)
    src_track_id = field(src.fields, 12)
    track_row = next(r for r in c.tables[TRACK].rows if r.id == src_track_id)
    new_track = max(r.id for r in c.tables[TRACK].rows) + 1
    new_route = max(r.id for r in c.tables[TRACK_MODEL].rows) + 1
    src_h = djb2(src_track)
    tf = [(fn, wt, name_h if wt == 0 and v == src_h else v) for fn, wt, v in track_row.fields]
    c.put_row(TRACK, Row(new_track, encode(replace(tf, {1: new_track}))))
    # location própria (cópia da de origem com os textos trocados); nomes internos ficam os da origem
    p = text_prefix(name)
    src_loc = next(r for r in c.tables[LOCATION].rows if r.id == field(src.fields, 5))
    new_loc = max(r.id for r in c.tables[LOCATION].rows) + 1
    loc_changes = {1: new_loc} | {fn: c.add_string(k.format(p=p)) for fn, k in LOCATION_TEXT_FIELDS.items()}
    # país próprio pelo mesmo caminho; o nome interno (campo 2) fica, porque a pasta vem dele
    src_country = next(r for r in c.tables[COUNTRY].rows if r.id == field(src.fields, 4))
    new_country = max(r.id for r in c.tables[COUNTRY].rows) + 1
    country_changes = {1: new_country} | {fn: c.add_string(k.format(p=p)) for fn, k in COUNTRY_TEXT_FIELDS.items()}
    c.put_row(COUNTRY, Row(new_country, encode(replace(src_country.fields, country_changes))))
    loc_changes[3] = new_country
    c.put_row(LOCATION, Row(new_loc, encode(replace(src_loc.fields, loc_changes))))
    changes = {1: new_route, 2: name_h, 4: new_country, 5: new_loc, 12: new_track}
    changes |= {fn: c.add_string(k.format(p=p, route=route)) for fn, k in ROUTE_TEXT_FIELDS.items()}
    if length is not None:
        changes[LENGTH_FIELD] = struct.unpack("<I", struct.pack("<f", length))[0]
    for fn, img in zip(LOADING_FIELDS, loading_names(name, route)):
        changes[fn] = c.add_string(img)
    c.put_row(TRACK_MODEL, Row(new_route, encode(replace(src.fields, changes))))
    surf = c.tables[TRACK_MODEL_SURFACE]
    for r in [r for r in surf.rows if field(r.fields, 3) == src.id]:
        nid = max(x.id for x in surf.rows) + 1
        c.put_row(TRACK_MODEL_SURFACE, Row(nid, encode(replace(r.fields, {1: nid, 3: new_route}))))
    return new_route, new_track


def copy_host_files(overlay: str, location: str, src_track: str, name: str, files: dict[str, str]) -> None:
    from tools.egodata.nefs import NefsArchive

    dst = os.path.join(overlay, "tracks", "locations", location, name)
    prefix = f"tracks/locations/{location}/{src_track}/"
    nefs = os.path.join(GAME, "locations", f"{location}__{src_track}.nefs")
    arc = NefsArchive.open_path(nefs)
    fh_in = open(arc.data_path, "rb")
    for e in arc.entries():
        if not e.is_file or not e.path.startswith(prefix):
            continue
        rel = e.path[len(prefix):]
        out = os.path.join(dst, rel)
        if rel in files or (os.path.exists(out) and os.path.getsize(out) == e.size):
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out + ".tmp", "wb") as fh:
            fh.write(arc.read(e.path, fh_in))
        os.replace(out + ".tmp", out)
        print(f"  {rel} ({e.size} bytes)")
    for rel, path in files.items():
        out = os.path.join(dst, rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(path, out)
        print(f"  {rel} <- {path}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--catalogue", default="build/re/ctpk/base.ctpk", help="base.ctpk original")
    ap.add_argument("--overlay", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--location", default="portugal")
    ap.add_argument("--from-track", required=True)
    ap.add_argument("--route", default="route_0")
    ap.add_argument("--file", action="append", default=[], help="rel=caminho: troca um arquivo da cópia")
    ap.add_argument("--no-files", action="store_true", help="só o catálogo")
    ap.add_argument("--loading-image", help="imagem da tela de carregamento (vira <pista>_<rota>.tpk, 2304×1296)")
    ap.add_argument("--map-image", help="mapa do traçado (vira <pista>_<rota>_spline.tpk, 1344×992)")
    ap.add_argument("--title", help="nome exibido (location e pista); com --texts-ini")
    ap.add_argument("--route-title", help="nome exibido da rota")
    ap.add_argument("--country-title", help="país exibido no nome completo da location (padrão: --location)")
    ap.add_argument("--length", type=float, help="extensão da rota em metros (campo 15)")
    ap.add_argument("--desc", default="", help="descrição da location")
    ap.add_argument("--texts-ini", help="grava aqui o dr2hook_texts.ini com os textos da pista")
    ap.add_argument("--tpk-template", default="build/re/tpk/montalegre_rallycross_route_0.tpk",
                    help="tpk do jogo usado só como molde do cabeçalho")
    args = ap.parse_args()

    c = Ctpk.open(args.catalogue)
    assert c.build() == c.data, "regravação do catálogo não bate com o original"
    route_id, track_id = register(c, args.name, args.from_track, args.route, args.length)
    out = os.path.join(args.overlay, "catalogues", "base.ctpk")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = c.build()
    Ctpk(data)  # relê para conferir
    with open(out, "wb") as fh:
        fh.write(data)
    print(f"{out}: rota {route_id}, pista {track_id}, {len(data)} bytes")
    if args.texts_ini:
        p = text_prefix(args.name)
        title = args.title or args.name
        texts = {p: title.upper(), f"{p}_location": title, f"{p}_location_full": f"{title}, {args.country_title or args.location.title()}",
                 f"{p}_country": args.country_title or args.location.title(), f"{p}_desc": args.desc, f"{p}_{args.route}": args.route_title or args.route}
        with open(args.texts_ini, "w", encoding="utf-8") as fh:
            fh.write("; textos das pistas próprias (scripts/research/custom_track.py)\n")
            fh.writelines(f"{k} = {v}\n" for k, v in texts.items())
        print(f"{args.texts_ini}: {len(texts)} textos")
    if args.loading_image or args.map_image:
        from PIL import Image

        template = open(args.tpk_template, "rb").read()
        loading_dir = os.path.join(args.overlay, "frontend", "streamed_textures", "loading")
        for src, tpk_name, size in zip((args.loading_image, args.map_image), loading_names(args.name, args.route),
                                       ((2304, 1296), (1344, 992))):
            if src:
                write_tpk(os.path.join(loading_dir, tpk_name + ".tpk"), template, tpk_name,
                          Image.open(src).convert("RGB").resize(size, Image.LANCZOS))
                print(f"  {tpk_name}.tpk <- {src}")
    if not args.no_files:
        files = dict(f.split("=", 1) for f in args.file)
        copy_host_files(args.overlay, args.location, args.from_track, args.name, files)


if __name__ == "__main__":
    main()
