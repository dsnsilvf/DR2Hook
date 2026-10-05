"""Exporta uma pista (`locations/*.nefs`) para o Track Explorer.

Saída em `<out>/tracks/<id>/`:

    track.json     terreno, rotas (portões, limites, splits), tipos e instâncias de objetos
    terrain.bin    malha do `tracksplit.pssg` (formato DR2M, ver `mesh.py`)
    objects.bin    malhas dos tipos de objeto usados em `route_N/objects.ens` (DR2M)

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


def _compose(inner: tuple[float, ...], outer: tuple[float, ...]) -> tuple[float, ...]:
    return mesh.multiply_matrix(inner, outer)


def node_meshes(root: PSSGNode, idx: dict[str, PSSGNode], node_id: str) -> list[dict[str, Any]]:
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
            material = mesh._ref(mesh._attr(node, "shader")) or source_id
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
    with open(os.path.join(dest, "terrain.bin"), "wb") as fh:
        fh.write(_pack_meshes(terrain))
    result["terrain"] = {"meshes": len(terrain), "verts": sum(len(m["positions"]) for m in terrain)}
    del split

    routes = []
    types_used: set[str] = set()
    all_instances: list[dict[str, Any]] = []
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
        for i in inst:
            i["route"] = name
        entry["instances"] = len(inst)
        types_used.update(i["type"] for i in inst)
        all_instances.extend(inst)
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
        ms = node_meshes(objs.root, idx, node_id) if node_id else []
        types[t] = {"node": node_id, "first": len(lib_meshes), "count": len(ms)}
        lib_meshes.extend(ms)
    with open(os.path.join(dest, "objects.bin"), "wb") as fh:
        fh.write(_pack_meshes(lib_meshes))
    result["types"] = types
    result["instances"] = all_instances
    with open(os.path.join(dest, "track.json"), "w", encoding="utf-8") as fh:
        json.dump(result, fh, separators=(",", ":"))
    log(f"{tid}: {len(terrain)} blocos de terreno, {len(types)} tipos, {len(all_instances)} instâncias")
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
        "types": len(data["types"]), "inst": len(data["instances"]),
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
