"""Exporta telas, cenas, textos e texturas do jogo para o visualizador.

Só lê a pasta do jogo. A saída é uma pasta com `index.html`, `viewer.js`,
`data/*.js` e `tex/*.png`, que abre direto no navegador (file://).
"""

from __future__ import annotations

import json
import os
import shutil
import struct
import sys
from collections import defaultdict
from typing import Any

from tools.egodata import bxml, cfgxml, lng
from tools.egodata.cli import _open
from tools.egodata.pssg import PSSGFile, PSSGNode

HERE = os.path.dirname(os.path.abspath(__file__))

# (pacote, caminho). As cenas do frontend e do HUD; os bundles só têm texturas.
SCENES = [
    ("fe", "game_1.dat", "frontend/databases/persistentDB.pssg"),
    ("osd", "game_1.dat", "frontend/databases/d_osd.pssg"),
    ("load", "game_1.dat", "frontend/databases/loadingScreen.pssg"),
]
BUNDLES = [
    ("game_1.dat", "frontend/bundles/b_persistent.pssg"),
    ("game_1.dat", "frontend/bundles/b_nonpersistent.pssg"),
    ("game_1.dat", "frontend/bundles/b_osd.pssg"),
]
LANGS = {"eng": "language/language_eng.lng", "bra": "language/language_bra.lng"}
SYSTEM = ["screens", "states", "flow"]

# Parâmetros de material que o visualizador usa.
MATERIAL_PARAMS = {
    "DiffuseColour", "Alpha", "DiffuseColour2", "Alpha2", "Bold", "ShadowColour",
    "ShadowAlpha", "ShadowOffsetX", "ShadowOffsetY", "GradientLeftColour",
    "GradientRightColour", "GradientCentreColour", "PatternStrength",
}
MATERIAL_TEXTURES = {"TDiffuseMap", "TPatternMap"}


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


class Reader:
    """Lê arquivos dos pacotes, abrindo cada pacote uma vez."""

    def __init__(self, game: str):
        self.game = game
        self._archives: dict[str, Any] = {}

    def read(self, archive: str, path: str) -> bytes:
        if archive not in self._archives:
            self._archives[archive] = _open(self.game, archive)
        return self._archives[archive].read(path)

    def entries(self, archive: str):
        if archive not in self._archives:
            self._archives[archive] = _open(self.game, archive)
        return self._archives[archive].entries()

    def items(self, archive: str) -> set[int]:
        """Ids que têm dados neste pacote (a listagem pode citar outros)."""
        if archive not in self._archives:
            self._archives[archive] = _open(self.game, archive)
        return set(self._archives[archive]._items)

    def read_any(self, path: str) -> bytes:
        for archive in ("game_1.dat", "game.nefs", "game.dat"):
            try:
                return self.read(archive, path)
            except KeyError:
                continue
        raise KeyError(path)


# ---------------------------------------------------------------- PSSG

def _attr(node: PSSGNode, key: str) -> Any:
    attr = node.attributes.get(key)
    if attr is None:
        return None
    value = attr.value
    if isinstance(value, int) and value == 0xFFFFFFFF:
        return -1
    if isinstance(value, bytes):
        return value.hex()
    return value


def _index(root: PSSGNode) -> dict[str, PSSGNode]:
    out: dict[str, PSSGNode] = {}
    stack = [root]
    while stack:
        node = stack.pop()
        node_id = _attr(node, "id")
        if node_id is not None:
            out[str(node_id)] = node
        stack.extend(node.children)
    return out


def _ref(value: Any) -> str:
    return str(value or "").lstrip("#")


def _half(raw: int) -> float:
    return struct.unpack("<e", struct.pack("<H", raw))[0]


def _round(values, digits=4):
    return [round(v, digits) for v in values]


class SceneExporter:
    def __init__(self, prefix: str, pssg: PSSGFile):
        self.prefix = prefix
        self.idx = _index(pssg.root)
        self.root = pssg.root
        self.groups = {
            key: [str(_attr(d, "name")) for d in node.children if d.type_name == "SHADERINPUTDEFINITION"]
            for key, node in self.idx.items() if node.type_name == "SHADERGROUP"
        }
        self.meshes: dict[str, Any] = {}
        self.materials: dict[str, Any] = {}
        self.textures: set[str] = set()

    def material(self, ref: str) -> str | None:
        node = self.idx.get(ref)
        if node is None or node.type_name != "SHADERINSTANCE":
            return None
        key = f"{self.prefix}:{ref}"
        if key in self.materials:
            return key
        group = _ref(_attr(node, "shaderGroup"))
        names = self.groups.get(group, [])
        mat: dict[str, Any] = {"g": group.rsplit(".", 1)[0]}
        for param in node.children:
            index = _attr(param, "parameterID")
            if not isinstance(index, int) or index >= len(names):
                continue
            name = names[index]
            if name in MATERIAL_TEXTURES and _attr(param, "texture"):
                texture = _ref(_attr(param, "texture")).rsplit(".", 1)[0]
                mat[name] = texture
                self.textures.add(texture)
            elif name in MATERIAL_PARAMS and param.data and len(param.data) % 4 == 0 and len(param.data) <= 16:
                values = _round(struct.unpack(f">{len(param.data) // 4}f", param.data))
                mat[name] = values if len(values) > 1 else values[0]
        self.materials[key] = mat
        return key

    def mesh(self, instance: PSSGNode) -> str | None:
        source = self.idx.get(_ref(_attr(instance, "indices")))
        if source is None:
            for child in instance.children:
                if child.type_name == "RENDERINSTANCESOURCE":
                    source = self.idx.get(_ref(_attr(child, "source")))
        if source is None:
            return None
        key = f"{self.prefix}:{_ref(_attr(source, 'id'))}"
        if key in self.meshes:
            return key
        indices: list[int] = []
        primitive = "triangles"
        positions: list[float] = []
        uvs: list[float] = []
        for child in source.children:
            if child.type_name == "RENDERINDEXSOURCE":
                primitive = str(_attr(child, "primitive") or "triangles")
                fmt = ">H" if _attr(child, "format") == "ushort" else ">I"
                for data in child.children:
                    if data.type_name == "INDEXSOURCEDATA" and data.data:
                        size = struct.calcsize(fmt)
                        count = len(data.data) // size
                        indices = list(struct.unpack(f">{count}{fmt[1]}", data.data[: count * size]))
            elif child.type_name == "RENDERSTREAM":
                block = self.idx.get(_ref(_attr(child, "dataBlock")))
                if block is None:
                    continue
                count = int(_attr(block, "elementCount") or 0)
                raw = next((c.data for c in block.children if c.type_name == "DATABLOCKDATA"), b"") or b""
                for stream in block.children:
                    if stream.type_name != "DATABLOCKSTREAM":
                        continue
                    kind, dtype = _attr(stream, "renderType"), _attr(stream, "dataType")
                    offset, stride = int(_attr(stream, "offset") or 0), int(_attr(stream, "stride") or 0)
                    if kind == "Vertex" and dtype == "float3" and not positions:
                        for i in range(count):
                            x, y, _z = struct.unpack_from(">3f", raw, offset + i * stride)
                            positions += [x, y]
                    elif kind == "ST" and not uvs:
                        for i in range(count):
                            if dtype == "half2":
                                u, v = struct.unpack_from(">2H", raw, offset + i * stride)
                                uvs += [_half(u), _half(v)]
                            elif dtype == "float2":
                                uvs += list(struct.unpack_from(">2f", raw, offset + i * stride))
        if primitive == "triangle_strip" and len(indices) > 2:
            tris = []
            for i in range(len(indices) - 2):
                a, b, c = indices[i : i + 3]
                tris += [a, b, c] if i % 2 == 0 else [b, a, c]
            indices = tris
        if not positions:
            return None
        if not indices:
            indices = list(range(len(positions) // 2))
        self.meshes[key] = {"v": _round(positions), "uv": _round(uvs) if uvs else None, "i": indices}
        return key

    def ui(self, node: PSSGNode) -> dict[str, Any]:
        out: dict[str, Any] = {}
        for child in node.children:
            if child.type_name != "USERDATA":
                continue
            obj = self.idx.get(_ref(_attr(child, "object")))
            if obj is None or not (obj.type_name.startswith("UINODE") or obj.type_name.startswith("UIMOD")):
                continue
            attrs = {k: _attr(obj, k) for k in obj.attributes if k != "id"}
            out[obj.type_name] = attrs
        return out

    def node(self, node: PSSGNode) -> dict[str, Any] | None:
        if node.type_name not in ("ROOTNODE", "NODE", "RENDERNODE"):
            return None
        out: dict[str, Any] = {"n": _attr(node, "nickname") or ""}
        children = []
        for child in node.children:
            if child.type_name == "TRANSFORM" and child.data and len(child.data) == 64:
                m = struct.unpack(">16f", child.data)
                affine = _round([m[0], m[1], m[4], m[5], m[12], m[13]])
                if affine != [1, 0, 0, 1, 0, 0]:
                    out["m"] = affine
            elif child.type_name == "RENDERSTREAMINSTANCE":
                mesh = self.mesh(child)
                material = self.material(_ref(_attr(child, "shader")))
                if mesh:
                    out.setdefault("draw", []).append([mesh, material])
            else:
                exported = self.node(child)
                if exported is not None:
                    children.append(exported)
        ui = self.ui(node)
        if ui:
            out["ui"] = ui
        if children:
            out["c"] = children
        return out

    def animation(self, root: PSSGNode) -> dict[str, Any] | None:
        """Curvas dos parâmetros de material e eventos com nome de uma raiz.

        Pacote NeAnimPacketData_B1/B4 (W = 1 ou 4 componentes), por canal:
        nome "material#Parâmetro", "parameter", u32 nº de chaves, dois u32
        não usados; cada chave tem o tempo, 4·W coeficientes de tempo e 4·W
        coeficientes do valor (cúbico v0 + v1·s + v2·s² + v3·s³, em estrutura
        de arrays); a última chave tem o tempo, 4·W de tempo e o valor final.
        """
        clips = []
        for child in root.children:
            if child.type_name != "USERDATA":
                continue
            anim_set = self.idx.get(_ref(_attr(child, "object")))
            if anim_set is None or anim_set.type_name != "NeAnimSet":
                continue
            for ref in anim_set.children:
                clip = self.idx.get(_ref(_attr(ref, "clip")))
                if clip is not None:
                    clips.append(clip)
        if not clips:
            return None
        out: dict[str, Any] = {"ch": []}
        for clip in clips:
            for ref in clip.children:
                if ref.type_name == "USERDATA":
                    data = self.idx.get(_ref(_attr(ref, "object")))
                    if data is not None and data.type_name == "FEANIMDATA":
                        out["df"] = _attr(data, "defaultFrame")
                        out["ev"] = self._events(_ref(_attr(data, "firstchild")))
                elif ref.type_name == "NeAnimClipPacketRef":
                    packet = self.idx.get(_ref(_attr(ref, "packet")))
                    if packet is not None:
                        for data in packet.children:
                            if data.data:
                                out["ch"] += self._channels(data.data, 4 if data.type_name.endswith("B4") else 1)
        return out if out["ch"] or out.get("ev") else None

    def _channels(self, data: bytes, width: int) -> list[Any]:
        import re
        header = re.compile(rb"\x00\x00\x00([\x01-\xc8])([\x20-\x7e]+?#[A-Za-z0-9_]+)\x00\x00\x00([\x01-\x20])([a-z_]+)", re.S)
        out = []
        for m in header.finditer(data):
            if m.group(1)[0] != len(m.group(2)) or m.group(3)[0] != len(m.group(4)):
                continue
            target, param = m.group(2).decode().rsplit("#", 1)
            pos = m.end()
            (count,) = struct.unpack_from(">I", data, pos)
            pos += 12
            need = ((count - 1) * (1 + 8 * width) + 1 + 5 * width) * 4
            if count < 1 or pos + need > len(data):
                continue
            floats = struct.unpack_from(f">{need // 4}f", data, pos)
            keys, i = [], 0
            for _ in range(count - 1):
                t = floats[i]
                coef = floats[i + 1 + 4 * width : i + 1 + 8 * width]
                keys.append([round(t, 4)] + _round(coef))
                i += 1 + 8 * width
            end = [round(floats[i], 4)] + _round(floats[i + 1 + 4 * width : i + 1 + 5 * width])
            out.append([f"{self.prefix}:{target}", param, width, keys, end])
        return out

    def _events(self, first: str) -> dict[str, float]:
        """Evento com apelido -> quadro onde a sequência dele termina."""
        events: dict[str, float] = {}
        node = self.idx.get(first)
        guard = 0
        while node is not None and guard < 200:
            guard += 1
            name = _attr(node, "nickname")
            frame = self._final_frame(_ref(_attr(node, "firstchild")))
            if name and frame is not None:
                events[str(name)] = round(frame, 4)
            node = self.idx.get(_ref(_attr(node, "nextevent")))
        return events

    def _final_frame(self, ref: str) -> float | None:
        node, frame, guard = self.idx.get(ref), None, 0
        while node is not None and guard < 50:
            guard += 1
            if node.type_name in ("FEEVENTPLAYTO", "FEEVENTGOTO", "FEEVENTPLAYTOATSPEED"):
                frame = _attr(node, "frame")
            elif node.type_name in ("FEEVENTNEAREST", "FEEVENT"):
                inner = self._final_frame(_ref(_attr(node, "firstchild")))
                if inner is not None:
                    frame = inner
            node = self.idx.get(_ref(_attr(node, "nextevent")))
        return frame if isinstance(frame, (int, float)) else None

    def scenes(self) -> dict[str, Any]:
        out = {}
        for node in self.idx.values():
            if node.type_name == "ROOTNODE":
                name = str(_attr(node, "nickname") or _attr(node, "id"))
                out[name] = self.node(node)
                anim = self.animation(node)
                if anim:
                    out[name]["anim"] = anim
        return out


# ---------------------------------------------------------------- telas e fluxo

def _node_json(node: bxml.Node) -> dict[str, Any]:
    name, text, attrs, children = node
    out: dict[str, Any] = {"t": name}
    if attrs:
        out["a"] = dict(attrs)
    if text and text.strip():
        out["x"] = text
    return out


def export_screens(reader: Reader) -> dict[str, Any]:
    roots = {name: bxml.decode(reader.read("game_1.dat", f"system/{name}.bin")) for name in SYSTEM}

    screens = []
    for w_index, world in enumerate(roots["screens"][3]):
        for screen in world[3]:
            if screen[0] != "Screen":
                continue
            attrs = dict(screen[2])
            entry: dict[str, Any] = {"id": attrs.get("id"), "a": attrs, "world": w_index,
                                     "world_glyph": dict(world[2]).get("glyph"), "items": [], "beh": []}
            for part in screen[3]:
                if part[0] == "items":
                    for item in part[3]:
                        entry["items"].append({"a": dict(item[2]), "b": [_node_json(b) for b in item[3]]})
                elif part[0] == "behaviours":
                    entry["beh"] = [_node_json(b) for b in part[3]]
            screens.append(entry)

    states = {}
    for state in roots["states"][3]:
        attrs = dict(state[2])
        states[attrs.get("id")] = {"c": state[0], "a": {k: v for k, v in attrs.items() if k != "id"}}

    nodes: dict[str, dict[str, Any]] = {}

    def walk(node: bxml.Node, parent: str | None) -> None:
        for child in node[3]:
            if child[0] != "node":
                continue
            attrs = dict(child[2])
            nid = attrs["id"]
            links = [dict(l[2]) for l in child[3] if l[0] == "link"]
            nodes[nid] = {"s": attrs.get("state"), "p": parent, "l": links}
            if "jump_id" in attrs:
                nodes[nid]["j"] = attrs["jump_id"]
            walk(child, nid)

    walk(roots["flow"], None)
    entry_points = [dict(e[2]) for e in roots["flow"][3] if e[0] == "entrypoint"]
    return {"screens": screens, "states": states, "flow": nodes, "entry": entry_points,
            "exe_names": _exe_names(reader.game, [s["id"] for s in screens])}


def _exe_names(game: str, names: list[str]) -> list[str]:
    """Ids de tela que o executável tem como string (o C++ pode abri-los)."""
    exe = open(os.path.join(game, "dirtrally2.exe"), "rb").read()
    return sorted(n for n in names if n and b"\0" + n.encode() + b"\0" in exe)


def export_styles(reader: Reader) -> dict[str, Any]:
    out: dict[str, Any] = {}
    root = cfgxml.decode(reader.read_any("frontend/configs/text_styles.xml"))
    for group in root[3]:
        for style in group[3]:
            attrs = dict(style[2])
            out[attrs.get("id")] = attrs
    return out


# ---------------------------------------------------------------- saída

def _write_js(path: str, var: str, value: Any) -> None:
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"window.{var}=")
        json.dump(value, f, ensure_ascii=False, separators=(",", ":"))
        f.write(";\n")


def _load_js(path: str, var: str) -> Any:
    text = open(path, encoding="utf-8").read().strip()
    prefix = f"window.{var}="
    if not text.startswith(prefix):
        raise ValueError(path)
    body = text[len(prefix):]
    if body.endswith(";"):
        body = body[:-1]
    return json.loads(body)


def _copy_web(out: str) -> None:
    for name in os.listdir(os.path.join(HERE, "web")):
        shutil.copy(os.path.join(HERE, "web", name), os.path.join(out, name))
    _bust_cache(out)


def _bust_cache(out: str) -> None:
    """`?v=<hash>` nos scripts e no CSS do index.html: o navegador não reaproveita uma versão velha."""
    import hashlib
    import re

    path = os.path.join(out, "index.html")

    def stamp(match: re.Match) -> str:
        attr, ref = match.group(1), match.group(2)
        file = os.path.join(out, ref)
        if not os.path.isfile(file):
            return match.group(0)
        with open(file, "rb") as fh:
            digest = hashlib.md5(fh.read()).hexdigest()[:8]
        return f'{attr}="{ref}?v={digest}"'

    with open(path, encoding="utf-8") as fh:
        html = fh.read()
    html = re.sub(r'(src|href)="([^":?]+\.(?:js|css))(?:\?v=\w+)?"', stamp, html)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(html)


def run(game: str, out: str, force_textures: bool = False, scene_images_only: bool = False,
        models: list[str] | None = None, all_models: bool = False, jobs: int | None = None) -> str:
    reader = Reader(game)
    os.makedirs(os.path.join(out, "data"), exist_ok=True)
    assets_js = os.path.join(out, "data", "assets.js")
    if all_models and not force_textures and os.path.exists(assets_js):
        return _refresh_models(game, out, log, jobs)

    log("telas, estados e fluxo")
    ui = export_screens(reader)
    ui["styles"] = export_styles(reader)

    scenes: dict[str, Any] = {}
    meshes: dict[str, Any] = {}
    materials: dict[str, Any] = {}
    wanted: set[str] = set()
    for prefix, archive, path in SCENES:
        log(f"cena: {path}")
        exporter = SceneExporter(prefix, PSSGFile(reader.read(archive, path)))
        scenes.update(exporter.scenes())
        meshes.update(exporter.meshes)
        materials.update(exporter.materials)
        wanted |= exporter.textures

    for screen in ui["screens"]:
        for item in screen["items"]:
            for b in item["b"]:
                texture = b.get("a", {}).get("texture")
                if texture:
                    wanted.add(texture)

    from tools.uiview.content import ContentExporter, export_dialogs

    content = ContentExporter(reader, out, force_textures, log)
    if scene_images_only:
        content.only = wanted
    assets = content.run()
    textures = {name: [a["p"], a["w"], a["h"]] for name, a in content.by_name.items()}
    log(f"texturas das cenas: {len(wanted & set(textures))} de {len(wanted)}")
    ui["dialogs"] = export_dialogs(reader)

    from tools.uiview.models import export_models

    model_assets, model_data = export_models(game, out, force_textures, log, models, all_models=all_models, jobs=jobs)
    # A galeria das cenas fica com a primeira ocorrência do nome; as texturas de
    # modelo entram depois e não substituem uma textura de UI homônima.
    known = {(a["g"], a["n"]) for a in assets}
    for asset in model_assets:
        if (asset["g"], asset["n"]) not in known:
            assets.append(asset)
            known.add((asset["g"], asset["n"]))
    _write_js(os.path.join(out, "data", "assets.js"), "ASSET_DATA", assets)
    _write_js(os.path.join(out, "data", "models.js"), "MODEL_DATA", model_data)

    strings = {}
    for lang, path in LANGS.items():
        log(f"textos: {path}")
        strings[lang] = lng.decode(reader.read("game_1.dat", path))

    _write_js(os.path.join(out, "data", "ui.js"), "UI_DATA", ui)
    _write_js(os.path.join(out, "data", "scenes.js"), "SCENE_DATA",
              {"scenes": scenes, "meshes": meshes, "materials": materials,
               "textures": textures})
    _write_js(os.path.join(out, "data", "strings.js"), "STRING_DATA", strings)
    _copy_web(out)
    log(f"pronto: {os.path.join(out, 'index.html')}")
    return os.path.join(out, "index.html")


def _refresh_models(game: str, out: str, log, jobs: int | None = None) -> str:
    """Só os modelos, um pacote por vez. A interface já exportada permanece."""
    from tools.uiview.models import export_models

    log("modelos: interface já exportada, só as malhas")
    model_assets, model_data = export_models(game, out, False, log, None, all_models=True, jobs=jobs)
    assets = _load_js(os.path.join(out, "data", "assets.js"), "ASSET_DATA")
    known = {(a["g"], a["n"]) for a in assets}
    for asset in model_assets:
        key = (asset["g"], asset["n"])
        if key not in known:
            assets.append(asset)
            known.add(key)
    _write_js(os.path.join(out, "data", "assets.js"), "ASSET_DATA", assets)
    _write_js(os.path.join(out, "data", "models.js"), "MODEL_DATA", model_data)
    _copy_web(out)
    log(f"pronto: {os.path.join(out, 'index.html')}")
    return os.path.join(out, "index.html")
