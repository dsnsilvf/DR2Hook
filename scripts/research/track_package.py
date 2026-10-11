#!/usr/bin/env python3
"""Gera um pacote de pista `.dr2track` (formato em src/trackpkg/package.h) a partir da overlay de desenvolvimento.

O jogador só copia o pacote para `<jogo>/ctracks/`; a dxgi monta o resto no boot (src/trackpkg/builder.cpp).
Daqui saem:
- `manifest.json` (nomes, país, hospedeira e rotas);
- `track/`: só os arquivos da pista que diferem dos da hospedeira (o resto o jogo lê da hospedeira);
- `data/`: as imagens da tela de carregamento (`<pista>_<rota>.tpk` e `_spline.tpk`);
- `graft/`: o que a overlay acrescentou ao `loadingScreen.pssg` (componente e filho do map_switch) e as
  texturas do card (`s_$LOC`, `s_$LOC_l`, `panel_$LOC`; `$LOC` vira `dr2hook_loc<id>` no carregador);
- `preview/card.png`.

`--info <pista>/package.json` (gravado pelo viewer3d, Arquivo > Informações da pista) dá os padrões de nome, país,
descrição, autor, versão e alfinete do mapa; os argumentos explícitos valem por cima. O zip sai igual byte a byte
para a mesma entrada (datas fixas), e o `--install` só regrava o pacote instalado quando ele muda: a dxgi só refaz
o cache das pistas quando o pacote muda.

    python3 scripts/research/track_package.py --overlay captures/overlay --id dr2hook_ring --out build/re/ring_package
"""

from __future__ import annotations

import argparse
import copy
import io
import json
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from PIL import Image  # noqa: E402

from pssg_fragment import added_nodes, fragment  # noqa: E402
from tools.egodata.nefs import NefsArchive  # noqa: E402
from tools.egodata.pssg import PSSGFile, PSSGNode  # noqa: E402
from tools.uiview.export import Reader  # noqa: E402

GAME = os.environ.get("DR2_GAME_DIR", "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0")
LOADING_PSSG = "frontend/databases/loadingScreen.pssg"
NONPERSISTENT = "frontend/bundles/b_locations_nonpersistent_base.pssg"
PERSISTENT = "frontend/bundles/b_locations_persistent_base.pssg"
# (textura, molde da hospedeira, bundle, tamanho): o card da tela de locations e as outras imagens da location
CARD_TEXTURES = (("s_$LOC", "s_{host}", NONPERSISTENT, (560, 272)),
                 ("s_$LOC_l", "s_{host}_l", NONPERSISTENT, (608, 400)),
                 ("panel_$LOC", "panel_{host}", PERSISTENT, (352, 240)))


def host_files(game: str, location: str, track: str) -> dict[str, bytes]:
    arc = NefsArchive.open_path(os.path.join(game, "locations", f"{location}__{track}.nefs"))
    prefix = f"tracks/locations/{location}/{track}/"
    with open(arc.data_path, "rb") as fh:
        return {e.path[len(prefix):]: arc.read(e.path, fh) for e in arc.entries()
                if e.is_file and e.path.startswith(prefix)}


def own_track_files(track_dir: str, host: dict[str, bytes]) -> dict[str, bytes]:
    out = {}
    for root, _, files in os.walk(track_dir):
        for f in files:
            path = os.path.join(root, f)
            rel = os.path.relpath(path, track_dir).replace(os.sep, "/")
            data = open(path, "rb").read()
            if host.get(rel) != data:
                out[rel] = data
    return out


def rename_new_ids(orig: PSSGFile, nodes: list[PSSGNode], track: str) -> int:
    """Ids novos no formato do loading_screen.py (prefixo + 9000...) colidem entre pacotes: viram
    prefixo + pista + número, e as referências '#id' dentro dos nós novos acompanham."""
    used = {n.id for n in orig.find_nodes(lambda n: n.id is not None)}
    ren: dict[str, str] = {}

    def walk(n: PSSGNode):
        yield n
        for c in n.children:
            yield from walk(c)

    allnew = [m for n in nodes for m in walk(n)]
    for n in allnew:
        m = re.fullmatch(r"([a-z_]+?)(9\d{3,})", n.id or "")
        if m and n.id not in used:
            ren[n.id] = f"{m.group(1)}_{track}_{m.group(2)}"
    for n in allnew:
        for name, a in list(n.attributes.items()):
            v = a.value
            if isinstance(v, str):
                nv = ren.get(v) or ("#" + ren[v[1:]] if v.startswith("#") and v[1:] in ren else None)
                if nv is not None:
                    PSSGFile.set_attr(n, name, nv)
    return len(ren)


def dxt1_texture(template: PSSGNode, tex_id: str, img: Image.Image) -> PSSGNode:
    buf = io.BytesIO()
    img.convert("RGB").save(buf, "DDS", pixel_format="DXT1")
    data = buf.getvalue()[128:][: img.width * img.height // 2]
    t = copy.deepcopy(template)
    for k, v in (("id", tex_id), ("width", img.width), ("height", img.height), ("texelFormat", "dxt1_srgb"),
                 ("numberMipMapLevels", 0)):
        PSSGFile.set_attr(t, k, v)
    block = t.children[0]
    PSSGFile.set_attr(block, "size", len(data))
    node = block.children[0]
    node.data = data
    node._raw_payload = None
    node._orig_data = None
    node._compressed = False
    return t


def cover(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Recorta no centro para a proporção de `size` e reduz."""
    w, h = img.size
    k = max(size[0] / w, size[1] / h)
    cw, ch = round(size[0] / k), round(size[1] / k)
    x, y = (w - cw) // 2, (h - ch) // 2
    return img.crop((x, y, x + cw, y + ch)).resize(size, Image.LANCZOS)


def card_grafts(reader: Reader, host_loc_name: str, card: Image.Image) -> dict[str, bytes]:
    out = {}
    for bundle in (NONPERSISTENT, PERSISTENT):
        p = PSSGFile(reader.read("game.nefs", bundle))
        added = []
        for tex_id, tpl_id, b, size in CARD_TEXTURES:
            if b != bundle:
                continue
            tpl = p.find_by_id(tpl_id.format(host=host_loc_name))
            if not tpl:
                raise SystemExit(f"molde {tpl_id.format(host=host_loc_name)} não está em {bundle}")
            lib = next(n for n in p.find_by_type("LIBRARY") if tpl[0] in n.children)
            t = dxt1_texture(tpl[0], tex_id, cover(card, size))
            lib.children.append(t)
            added.append(t)
        out[bundle] = fragment(p, added)
    return out


def info_defaults(path: str) -> dict:
    """Padrões do package.json do viewer3d (campos ausentes ficam com os do argparse)."""
    info = json.load(open(path, encoding="utf-8"))
    out = {k: info[k] for k in ("name", "country", "author", "version") if isinstance(info.get(k), str) and info[k]}
    if isinstance(info.get("description"), str) and info["description"]:
        out["desc"] = info["description"]
    m = info.get("map")
    if isinstance(m, dict) and isinstance(m.get("lat"), (int, float)) and isinstance(m.get("lon"), (int, float)):
        out["lat"], out["lon"] = float(m["lat"]), float(m["lon"])
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--info", help="package.json da pista (viewer3d): padrões de nome, país, descrição e mapa")
    ap.add_argument("--overlay", default="captures/overlay")
    ap.add_argument("--id", default="dr2hook_ring")
    ap.add_argument("--name", default="DR2 Hook Ring")
    ap.add_argument("--country", default="Brazil")
    ap.add_argument("--desc", default="Pista sintética do DR2 Hook, gerada pelo editor de níveis.")
    ap.add_argument("--author", default="DR2Hook")
    ap.add_argument("--version", default="1")
    ap.add_argument("--host-location", default="portugal")
    ap.add_argument("--host-track", default="montalegre_rallycross")
    ap.add_argument("--host-location-name", default="montalegre", help="nome interno da location (moldes do card)")
    ap.add_argument("--route", default="route_0")
    ap.add_argument("--route-name", default="Circuito Completo")
    ap.add_argument("--length", type=float, default=2132.4, help="extensão da rota em metros")
    ap.add_argument("--lat", type=float, default=-15, help="alfinete do mapa-múndi (graus; padrão Brasília)")
    ap.add_argument("--lon", type=float, default=-48)
    ap.add_argument("--map-region", type=int, help="campo 17 da location (sem nome ainda); omitido = da hospedeira")
    ap.add_argument("--card", help="imagem do card (padrão: a foto da tela de carregamento)")
    ap.add_argument("--out", default="build/re/ring_package", help="pasta do .dr2track")
    ap.add_argument("--install", help="copia o pacote para esta pasta (ex. <jogo>/ctracks)")
    ap.add_argument("--game", default=GAME)
    pre, _ = ap.parse_known_args()
    if pre.info:
        ap.set_defaults(**info_defaults(pre.info))
    a = ap.parse_args()

    reader = Reader(a.game)
    files: dict[str, bytes] = {}
    manifest = {"format": 1, "id": a.id, "name": a.name, "country": a.country, "description": a.desc,
                "author": a.author, "version": a.version,
                "host": {"location": a.host_location, "track": a.host_track},
                "routes": [{"id": a.route, "host_route": a.route, "name": a.route_name, "length_m": a.length}]}
    manifest["map"] = {"lat": a.lat, "lon": a.lon}
    if a.map_region is not None:
        manifest["map"]["region"] = a.map_region
    files["manifest.json"] = json.dumps(manifest, ensure_ascii=False, indent=1).encode()

    track_dir = os.path.join(a.overlay, "tracks", "locations", a.host_location, a.id)
    own = own_track_files(track_dir, host_files(a.game, a.host_location, a.host_track))
    files |= {f"track/{rel}": data for rel, data in own.items()}
    print(f"track/: {len(own)} arquivos próprios")

    loading = os.path.join(a.overlay, "frontend", "streamed_textures", "loading")
    for f in sorted(os.listdir(loading)) if os.path.isdir(loading) else []:
        if f.startswith(a.id + "_"):
            files[f"data/frontend/streamed_textures/loading/{f}"] = open(os.path.join(loading, f), "rb").read()

    ov_loading = os.path.join(a.overlay, *LOADING_PSSG.split("/"))
    if os.path.exists(ov_loading):
        orig = PSSGFile(reader.read("game_1.dat", LOADING_PSSG))
        new = PSSGFile(ov_loading)
        nodes = added_nodes(orig, new)
        renamed = rename_new_ids(orig, nodes, a.id)
        files[f"graft/{LOADING_PSSG}"] = fragment(new, nodes)
        print(f"graft/{LOADING_PSSG}: {len(nodes)} nós ({renamed} ids renomeados)")

    if a.card:
        card = Image.open(a.card).convert("RGB")
    else:
        ppm = os.path.join(a.overlay, "dr2hook", f"loadcover_{a.id}_{a.route}.ppm")
        card = Image.open(ppm).convert("RGB")
    for bundle, data in card_grafts(reader, a.host_location_name, card).items():
        files[f"graft/{bundle}"] = data
    buf = io.BytesIO()
    cover(card, (560, 272)).save(buf, "PNG")
    files["preview/card.png"] = buf.getvalue()

    os.makedirs(a.out, exist_ok=True)
    out = os.path.join(a.out, f"{a.id}.dr2track")
    with zipfile.ZipFile(out + ".tmp", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name, data in files.items():
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data, compresslevel=6)
    os.replace(out + ".tmp", out)
    print(f"{out}: {len(files)} arquivos, {os.path.getsize(out) / 1e6:.1f} MB")
    if a.install:
        os.makedirs(a.install, exist_ok=True)
        dst = os.path.join(a.install, os.path.basename(out))
        data = open(out, "rb").read()
        if os.path.exists(dst) and open(dst, "rb").read() == data:
            print(f"  -> {dst} (igual ao instalado)")
            return
        with open(dst + ".tmp", "wb") as fh:
            fh.write(data)
        os.replace(dst + ".tmp", dst)
        print(f"  -> {dst}")


if __name__ == "__main__":
    main()
