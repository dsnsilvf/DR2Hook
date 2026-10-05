"""Exporta uma pista (`locations/*.nefs`) para o Track Explorer.

Saída em `<out>/tracks/<id>/`:

    track.json          terreno, rotas (portões, limites, splits) e tipos de objeto
    terrain_<n>.bin     blocos do `tracksplit.pssg` perto da rota, mais as malhas de fundo (DR2M, ver `mesh.py`)
    objects.bin         malhas dos tipos de objeto, árvore e ornamento usados (DR2M)
    inst_<rota>.bin     instâncias da rota (DR2I, ver `pack_instances`)

O `tracksplit.pssg` passa de 500 MB só por causa das texturas: a malha em si tem
poucos MB, e o `PSSGFile` abre o arquivo inteiro em cerca de 1 s.
"""

from __future__ import annotations

import json
import os
import re
import struct
import xml.etree.ElementTree as ET
from typing import Any

from tools.egodata import bxml
from tools.egodata.nefs import NefsArchive
from tools.pssg import PSSGFile, PSSGNode
from tools.uiview import mesh

_ID_UNSAFE = re.compile(r"[^A-Za-z0-9_]+")


def track_id(rel: str) -> str:
    return _ID_UNSAFE.sub("_", os.path.basename(rel).replace(".nefs", "")).strip("_")


def track_base(arc: NefsArchive) -> str:
    """Pasta `tracks/locations/<país>/<pista>/` dentro do pacote."""
    for entry in arc.entries():
        if entry.is_file and entry.path.startswith("tracks/locations/") and entry.path.endswith("/tracksplit.pssg"):
            return entry.path[: -len("tracksplit.pssg")]
    raise ValueError("pacote sem tracksplit.pssg")


def _floats(text: str) -> list[float]:
    return [float(v) for v in text.split()]


def _read_xml(data: bytes) -> ET.Element:
    """XML de texto ou BXML da EGO."""
    if data[:5] == b"\x00BXML" or data[:4] == b"\x1a\"Rr":
        return ET.fromstring(bxml.to_xml(bxml.decode(data)))
    return ET.fromstring(data.decode("utf-8-sig"))


def _pack_meshes(meshes: list[dict[str, Any]]) -> bytes:
    return mesh.pack_geom(meshes)


def instances(ens: bytes) -> list[dict[str, Any]]:
    """`TEMPLATEENTITYINSTANCE` de `objects.ens`: tipo e matriz 4×4 (linha-maior, translação no fim)."""
    root = ET.fromstring(ens.decode("utf-8-sig"))
    out = []
    for node in root.iter("TEMPLATEENTITYINSTANCE"):
        uri = node.get("uri", "")
        transform = node.find("TEMPLATETRANSFORM")
        if "#" not in uri or transform is None or not transform.text:
            continue
        m = _floats(transform.text)
        if len(m) != 16:
            continue
        out.append({"id": node.get("id", ""), "type": uri.split("#", 1)[1], "m": m})
    return out


def _records(data: bytes, start_at: int, count_at: int, stride: int, rot_at: int, pos_at: int) -> list[dict[str, Any]]:
    start = struct.unpack_from("<I", data, start_at)[0]
    count = struct.unpack_from("<I", data, count_at)[0]
    out = []
    for i in range(count):
        o = start + i * stride
        if o + stride > len(data):
            break
        h, ident = struct.unpack_from("<II", data, o)
        r = struct.unpack_from("<9f", data, o + rot_at)
        p = struct.unpack_from("<3f", data, o + pos_at)
        out.append({"hash": h, "id": ident, "m": [r[0], r[1], r[2], 0, r[3], r[4], r[5], 0, r[6], r[7], r[8], 0, p[0], p[1], p[2], 1]})
    return out


def trees_bin(data: bytes) -> list[dict[str, Any]]:
    """`trees.bin`: cabeçalho de 72 bytes (caixa, contagem em 48, início em 60) e registros de 96 bytes:
    hash do tipo, índice, matriz 3×3 em 8 (linhas = eixos, já com escala), posição em 44."""
    return _records(data, 60, 48, 96, 8, 44)


def ornaments_bin(data: bytes) -> list[dict[str, Any]]:
    """`ornaments.bin`: início em 80, contagem em 88, registros de 212 bytes:
    hash do tipo, índice, matriz 3×3 em 16, posição em 52."""
    return _records(data, 80, 88, 212, 16, 52)


def references(xml: bytes) -> dict[int, str]:
    """`reference_id` → `filename` de `trees_references.xml` / `ornaments_references.xml`."""
    root = ET.fromstring(xml.decode("utf-8-sig"))
    return {int(n.get("reference_id", 0)): n.get("filename", "") for n in root.iter("instanceref")}


def nick_index(root: PSSGNode) -> dict[str, PSSGNode]:
    out: dict[str, PSSGNode] = {}
    stack = [root]
    while stack:
        node = stack.pop()
        for key in ("nick", "nickname", "id"):
            attr = node.attributes.get(key)
            if attr is not None:
                out.setdefault(str(attr.value), node)
        stack.extend(node.children)
    return out


def object_renderables(types_xml: bytes) -> dict[str, str]:
    """Tipo de objeto → nó `default!N` de `objects.pssg`.

    O arquivo repete, para cada tipo: `TEMPLATEENTITYREFERENCE`, `TEMPLATEENTITY`,
    formas de colisão e um `TEMPLATERENDERABLE uri="objects.pssg#default!N"`.
    """
    root = ET.fromstring(types_xml.decode("utf-8-sig"))
    db = next(root.iter("PSSGDATABASE"))
    current = None
    out: dict[str, str] = {}
    for node in db:
        if node.tag == "TEMPLATEENTITYREFERENCE":
            current = node.get("id")
        elif node.tag == "TEMPLATERENDERABLE" and current:
            uri = node.get("uri", "")
            if uri.startswith("objects.pssg#"):
                out.setdefault(current, uri.split("#", 1)[1])
    return out


INST_MAGIC = b"DR2I"


def pack_instances(items: list[dict[str, Any]], type_index: dict[str, int]) -> bytes:
    """Instâncias de uma rota, little-endian, tudo alinhado em 4 bytes:

        char[4] "DR2I", uint32 n
        uint16[n] índice do tipo (preenchido até múltiplo de 4)
        uint32[n] identificador: posição do registro no arquivo de origem (`objects.ens`, `ornaments.bin` ou `trees.bin`)
        float32[n * 12] linhas 0..2 da matriz 3×3 (com escala) e a posição
    """
    n = len(items)
    out = bytearray(INST_MAGIC + struct.pack("<I", n))
    out += struct.pack(f"<{n}H", *(type_index[i["type"]] for i in items))
    while len(out) % 4:
        out += b"\0"
    out += struct.pack(f"<{n}I", *(int(i["idnum"]) for i in items))
    flat: list[float] = []
    for i in items:
        m = i["m"]
        flat.extend((m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14]))
    out += struct.pack(f"<{n * 12}f", *flat)
    return bytes(out)


TERRAIN_MARGIN = 1200.0
BACKGROUND_SPAN = 3000.0


def route_points(entry: dict[str, Any]) -> list[list[float]]:
    pts: list[list[float]] = []
    progress = entry.get("progress")
    if progress:
        for g in progress["gates"]:
            pts.append(g["l"])
            pts.append(g["r"])
    for line in entry.get("ai") or []:
        pts.extend(line["pts"])
    return pts


def near_route(terrain: list[dict[str, Any]], pts: list[list[float]], margin: float = TERRAIN_MARGIN) -> list[dict[str, Any]]:
    """Blocos cuja caixa (em x e z) chega a `margin` metros da rota; malhas maiores que
    `BACKGROUND_SPAN` (a paisagem de fundo) ficam sempre."""
    if not pts:
        return terrain
    xs = [p[0] for p in pts]
    zs = [p[2] for p in pts]
    x0, x1, z0, z1 = min(xs) - margin, max(xs) + margin, min(zs) - margin, max(zs) + margin
    keep = []
    for m in terrain:
        p = m["positions"]
        if not p:
            continue
        lo_x = min(v[0] for v in p)
        hi_x = max(v[0] for v in p)
        lo_z = min(v[2] for v in p)
        hi_z = max(v[2] for v in p)
        big = max(hi_x - lo_x, hi_z - lo_z) > BACKGROUND_SPAN
        if big or (hi_x >= x0 and lo_x <= x1 and hi_z >= z0 and lo_z <= z1):
            keep.append(m)
    return keep


TEX_MAX = 512
STUB_AREA = 16
_NOT_DIFFUSE = ("_n", "_s", "_e", "_m", "_nm", "_spec", "_normal")


def shader_textures(root: PSSGNode) -> dict[str, list[str]]:
    """`SHADERINSTANCE id` → nomes de textura, na ordem de `parameterID`."""
    out: dict[str, list[str]] = {}
    stack = [root]
    while stack:
        node = stack.pop()
        if node.type_name == "SHADERINSTANCE":
            found = []
            for child in node.children:
                if child.type_name == "SHADERINPUT" and "texture" in child.attributes:
                    found.append((int(child.attributes["parameterID"].value), str(child.attributes["texture"].value).split("#", 1)[-1]))
            out[str(node.attributes["id"].value)] = [name for _, name in sorted(found)]
        stack.extend(node.children)
    return out


def pick_diffuse(names: list[str]) -> str | None:
    """A textura de cor do material: a primeira `_d`; senão `_a`/`_b`/`_col`; senão a primeira que não é normal, brilho ou emissiva."""
    stems = [(n, os.path.splitext(n)[0].lower()) for n in names]
    for suffixes in (("_d",), ("_a", "_b", "_col", "_cm", "_dif", "_diff")):
        for name, stem in stems:
            if stem.endswith(suffixes):
                return name
    for name, stem in stems:
        if not stem.endswith(_NOT_DIFFUSE):
            return name
    return None


def _tex_file(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", os.path.splitext(name)[0]) + ".webp"


def export_textures(sources, wanted: set[str], dest: str, log=print) -> dict[str, str]:
    """Grava em `<dest>/tex/` as texturas de `wanted` (nome `.tga`) achadas em `sources`.

    `sources` é uma lista de funções que devolvem um `PSSGFile` (ou None); cada PSSG é aberto, lido
    e descartado antes do próximo, porque `patchup_ot.pssg` passa de 800 MB."""
    from tools.uiview.content import decode_texture

    done: dict[str, str] = {}
    area: dict[str, int] = {}
    os.makedirs(os.path.join(dest, "tex"), exist_ok=True)
    for load in sources:
        # um pacote pode trazer só um marcador 4×4 do nome; a imagem de verdade vem de outro
        missing = {n for n in wanted if area.get(n, 0) <= STUB_AREA}
        if not missing:
            break
        pssg = load()
        if pssg is None:
            continue
        stack = [pssg.root]
        while stack:
            node = stack.pop()
            stack.extend(node.children)
            if node.type_name != "TEXTURE":
                continue
            name = str(node.attributes["id"].value)
            if name not in missing:
                continue
            block = next((c for c in node.children if c.type_name == "TEXTUREIMAGEBLOCK"), None)
            data = next((c.data for c in block.children if c.data), None) if block else None
            if not data:
                continue
            try:
                image = decode_texture(str(node.attributes["texelFormat"].value), int(node.attributes["width"].value), int(node.attributes["height"].value), data)
            except Exception as exc:  # formato raro: a malha fica sem textura
                log(f"  textura não lida {name}: {exc}")
                continue
            if image is None:
                continue
            if image.width * image.height <= area.get(name, 0):
                continue
            area[name] = image.width * image.height
            if image.mode not in ("RGB", "RGBA"):
                image = image.convert("RGBA" if "A" in image.mode else "RGB")
            image.thumbnail((TEX_MAX, TEX_MAX))
            file = _tex_file(name)
            image.save(os.path.join(dest, "tex", file), "WEBP", quality=78, method=4)
            done[name] = "tex/" + file
        del pssg
    return done


def _compose(inner: tuple[float, ...], outer: tuple[float, ...]) -> tuple[float, ...]:
    return mesh.multiply_matrix(inner, outer)


def node_meshes(root: PSSGNode, idx: dict[str, PSSGNode], node_id: str, prefix: str = "") -> list[dict[str, Any]]:
    """Malhas sob `node_id`, com as matrizes dos nós filhos aplicadas (sem a do próprio nó)."""
    top = idx.get(node_id)
    if top is None:
        return []
    out: list[dict[str, Any]] = []
    seen: dict[str, int] = {}

    def walk(node: PSSGNode, m: tuple[float, ...]) -> None:
        if node.type_name in ("RENDERSTREAMINSTANCE", "MATRIXPALETTERENDERINSTANCE"):
            source_id = mesh._ref(mesh._attr(node, "indices"))
            if not source_id:
                for child in node.children:
                    if child.type_name == "RENDERINSTANCESOURCE":
                        source_id = mesh._ref(mesh._attr(child, "source"))
            raw = mesh._read_source(idx, source_id) if source_id else None
            if raw is None:
                return
            material = prefix + (mesh._ref(mesh._attr(node, "shader")) or source_id)
            seen[material] = seen.get(material, 0) + 1
            name = material if seen[material] == 1 else f"{material}_{seen[material]}"
            out.append({
                "name": name,
                "material": material,
                "positions": [mesh.transform_point(m, *p) for p in raw["positions"]],
                "uvs": raw["uvs"],
                "indices": raw["indices"],
            })
            return
        for child in node.children:
            if child.type_name in ("NODE", "RENDERNODE", "SEGMENTSET"):
                walk(child, _compose(mesh._matrix(child), m))
            elif child.type_name in ("RENDERSTREAMINSTANCE", "MATRIXPALETTERENDERINSTANCE"):
                walk(child, m)

    walk(top, mesh._IDENT)
    return out


def _route_dirs(arc: NefsArchive, base: str) -> list[str]:
    routes = set()
    for entry in arc.entries():
        if entry.is_file and entry.path.startswith(base + "route_"):
            routes.add(entry.path[len(base):].split("/", 1)[0])
    return sorted(routes)


def _gates_progress(root: ET.Element) -> dict[str, Any]:
    out = []
    for route in root.iter("route"):
        splits = [{"type": s.get("type"), "gate": int(s.get("gate", 0))} for s in route.iter("split")]
        out.append({"id": int(route.get("id", 0)), "direction": route.get("direction"), "splits": splits})
    gates = []
    for gate in root.iter("gate"):
        left, right = gate.find("left"), gate.find("right")
        if left is None or right is None or not left.text or not right.text:
            continue
        gates.append({"d": float(gate.get("distance", 0)), "l": _floats(left.text), "r": _floats(right.text)})
    return {"routes": out, "gates": gates}


def _ai_line(root: ET.Element) -> list[dict[str, Any]]:
    """Uma entrada por `<track>`: nome e posição de cada portão da IA."""
    out = []
    for track in root.iter("track"):
        pts = []
        for gate in track.iter("gate"):
            pos = gate.find("position")
            if pos is not None and pos.text:
                pts.append(_floats(pos.text))
        out.append({"name": track.get("name", ""), "pts": pts})
    return out


def export_track(game: str, rel: str, out: str, log=print) -> dict[str, Any]:
    from tools.uiview.models import open_package

    arc = open_package(game, rel)
    base = track_base(arc)
    tid = track_id(rel)
    dest = os.path.join(out, "tracks", tid)
    os.makedirs(dest, exist_ok=True)
    result: dict[str, Any] = {"id": tid, "src": rel, "base": base}

    log(f"{tid}: terreno")
    split = PSSGFile(arc.read(base + "tracksplit.pssg"))
    terrain = mesh.extract_meshes(split.root)
    for m in terrain:
        m["material"] = "g|" + m["material"]
    material_tex: dict[str, list[str]] = {"g|" + k: v for k, v in shader_textures(split.root).items()}
    result["terrain"] = {"meshes": len(terrain), "verts": sum(len(m["positions"]) for m in terrain)}

    routes = []
    types_used: set[str] = set()
    route_inst: dict[str, list[dict[str, Any]]] = {}
    for name in _route_dirs(arc, base):
        prefix = base + name + "/"
        entry: dict[str, Any] = {"name": name}
        for key, fname, reader in (("progress", "progress_track.xml", _gates_progress), ("ai", "ai_track.xml", _ai_line)):
            try:
                entry[key] = reader(_read_xml(arc.read(prefix + fname)))
            except (KeyError, FileNotFoundError, ValueError):
                pass
        try:
            inst = instances(arc.read(prefix + "objects.ens"))
        except (KeyError, FileNotFoundError):
            inst = []
        entry["ens_ids"] = [i["id"] for i in inst]
        for n, i in enumerate(inst):
            i["type"] = "e:" + i["type"]
            i["idnum"] = n
        types_used.update(i["type"][2:] for i in inst)
        route_inst[name] = inst
        routes.append(entry)
    result["routes"] = routes

    renderables: dict[str, str] = {}
    for name in _route_dirs(arc, base):
        try:
            renderables.update(object_renderables(arc.read(base + name + "/route_objecttypes.pssg")))
        except (KeyError, FileNotFoundError, ET.ParseError):
            pass
    objs = PSSGFile(arc.read(base + "objects.pssg"))
    idx = mesh._index(objs.root)
    lib_meshes: list[dict[str, Any]] = []
    types: dict[str, dict[str, Any]] = {}
    for t in sorted(types_used):
        node_id = renderables.get(t)
        ms = node_meshes(objs.root, idx, node_id, "o|") if node_id else []
        types[t] = {"node": node_id, "first": len(lib_meshes), "count": len(ms)}
        lib_meshes.extend(ms)

    # árvores (trees.bin → trees.pssg) e ornamentos (ornaments.bin → objects.pssg)
    nicks = nick_index(objs.root)
    types = {"e:" + k: v for k, v in types.items()}

    def resolve(key: str, candidates: list[str], index: dict[str, PSSGNode], root: PSSGNode, ids: dict[str, PSSGNode], mat_prefix: str) -> None:
        if key in types:
            return
        for cand in candidates:
            node = index.get(cand)
            if node is None:
                continue
            ms = node_meshes(root, ids, str(node.attributes["id"].value), mat_prefix) if "id" in node.attributes else []
            if ms:
                types[key] = {"node": cand, "first": len(lib_meshes), "count": len(ms)}
                lib_meshes.extend(ms)
                return
        types[key] = {"node": None, "first": len(lib_meshes), "count": 0}

    material_tex.update({"o|" + k: v for k, v in shader_textures(objs.root).items()})
    tree_lib: tuple | None = None
    for name in _route_dirs(arc, base):
        prefix = base + name + "/"
        try:
            orn = ornaments_bin(arc.read(prefix + "ornaments.bin"))
            orn_refs = references(arc.read(base + "ornaments_references.xml"))
        except (KeyError, FileNotFoundError, ET.ParseError, struct.error):
            orn, orn_refs = [], {}
        for n, r in enumerate(orn):
            fn = orn_refs.get(r["hash"], f"#{r['hash']}")
            key = "o:" + fn
            plain = fn.split("~")[0]
            resolve(key, [fn + " Root", fn + "_physics", fn, plain + " Root", plain + "_physics", plain], nicks, objs.root, idx, "o|")
            route_inst[name].append({"idnum": n, "type": key, "m": r["m"]})
        try:
            trees = trees_bin(arc.read(prefix + "trees.bin"))
        except (KeyError, FileNotFoundError, struct.error):
            trees = []
        if trees:
            if tree_lib is None:
                tp = PSSGFile(arc.read(base + "trees.pssg"))
                tree_lib = (tp, nick_index(tp.root), mesh._index(tp.root), references(arc.read(base + "trees_references.xml")))
                material_tex.update({"t|" + k: v for k, v in shader_textures(tp.root).items()})
            tp, tnicks, tidx, tree_refs = tree_lib
            for n, r in enumerate(trees):
                fn = tree_refs.get(r["hash"], f"#{r['hash']}")
                key = "t:" + fn
                resolve(key, [fn + "_x0", fn, fn + "_fo"], tnicks, tp.root, tidx, "t|")
                route_inst[name].append({"idnum": n, "type": key, "m": r["m"]})
    with open(os.path.join(dest, "objects.bin"), "wb") as fh:
        fh.write(_pack_meshes(lib_meshes))
    terrain_files: dict[tuple[int, ...], str] = {}
    for entry in routes:
        keep = near_route(terrain, route_points(entry))
        picked = tuple(id(m) for m in keep)
        if picked not in terrain_files:
            terrain_files[picked] = f"terrain_{len(terrain_files)}.bin"
            with open(os.path.join(dest, terrain_files[picked]), "wb") as fh:
                fh.write(_pack_meshes(keep))
        entry["terrain"] = {"file": terrain_files[picked], "meshes": len(keep), "verts": sum(len(m["positions"]) for m in keep)}
    type_index = {k: n for n, k in enumerate(types)}
    result["types"] = types
    result["type_order"] = list(types)
    for entry in routes:
        items = route_inst.get(entry["name"], [])
        entry["instances"] = len(items)
        with open(os.path.join(dest, f"inst_{entry['name']}.bin"), "wb") as fh:
            fh.write(pack_instances(items, type_index))

    # texturas: a de cor de cada material usado (terreno perto de alguma rota, objetos, árvores)
    used = {m["material"] for m in lib_meshes}
    for entry in routes:
        pass
    used.update(m["material"] for m in terrain)
    pick = {mat: pick_diffuse(material_tex.get(mat, [])) for mat in used}
    wanted = {n for n in pick.values() if n}
    paths = {e.path for e in arc.entries() if e.is_file}

    def pssg_at(rel_path: str):
        return lambda: PSSGFile(arc.read(rel_path)) if rel_path in paths else None

    log(f"{tid}: {len(wanted)} texturas")
    sources = [lambda: split] + [pssg_at(base + f) for f in ("objectstextures.pssg", "treestextures.pssg")]
    sources += [pssg_at(base + r["name"] + "/route_objectstextures.pssg") for r in routes]
    sources += [pssg_at(base + "patchup_ot.pssg")] + [pssg_at(base + r["name"] + "/route_patchup_ot.pssg") for r in routes]
    files = export_textures(sources, wanted, dest, log)
    result["materials"] = {mat: files[name] for mat, name in pick.items() if name in files}
    result["textures"] = {"wanted": len(wanted), "found": len(files)}
    with open(os.path.join(dest, "track.json"), "w", encoding="utf-8") as fh:
        json.dump(result, fh, separators=(",", ":"))
    solved = sum(1 for v in types.values() if v["count"])
    log(f"{tid}: {len(terrain)} blocos de terreno, {len(types)} tipos ({solved} com malha), {sum(len(v) for v in route_inst.values())} instâncias, {len(files)}/{len(wanted)} texturas")
    return result


def index_entry(dest: str) -> dict[str, Any] | None:
    try:
        with open(os.path.join(dest, "track.json"), encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return None
    return {
        "id": data["id"], "src": data["src"],
        "n": data["id"].split("__", 1)[-1].replace("_", " ").title(),
        "c": data["id"].split("__", 1)[0].replace("_", " ").title() if "__" in data["id"] else "",
        "meshes": data["terrain"]["meshes"], "verts": data["terrain"]["verts"],
        "types": len(data["types"]), "inst": sum(r.get("instances", 0) for r in data["routes"]),
        "routes": [r["name"] for r in data["routes"]],
    }


def write_index(out: str) -> int:
    """`data/tracks.js` com todas as pistas já exportadas em `<out>/tracks/`."""
    root = os.path.join(out, "tracks")
    rows = []
    if os.path.isdir(root):
        for name in sorted(os.listdir(root)):
            row = index_entry(os.path.join(root, name))
            if row:
                rows.append(row)
    os.makedirs(os.path.join(out, "data"), exist_ok=True)
    with open(os.path.join(out, "data", "tracks.js"), "w", encoding="utf-8") as fh:
        fh.write("const TRACK_DATA = " + json.dumps(rows, ensure_ascii=False, separators=(",", ":")) + ";\n")
    return len(rows)


def main(argv: list[str] | None = None) -> int:
    import argparse

    from tools.egodata.cli import DEFAULT_GAME
    from tools.uiview.models import package_list

    parser = argparse.ArgumentParser(prog="python -m tools.uiview.track", description="Exporta pistas para o Track Explorer")
    parser.add_argument("--game", default=DEFAULT_GAME)
    parser.add_argument("-o", "--output", default="build/uiview")
    parser.add_argument("--tracks", default="", help="trechos do nome, separados por vírgula (ex.: montalegre,finland_rally_01); vazio só reindexa")
    args = parser.parse_args(argv)
    tokens = [t.strip().lower() for t in args.tracks.split(",") if t.strip()]
    for rel in package_list(args.game):
        if rel.startswith("locations/") and any(t in rel.lower() for t in tokens):
            export_track(args.game, rel, args.output)
    print(f"{write_index(args.output)} pistas no índice")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
