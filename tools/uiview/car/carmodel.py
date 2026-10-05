"""Modelo estrutural de um carro (camada de engenharia reversa do Car Model Explorer).

Diferente de `mesh.extract_meshes`, que funde tudo por material e joga fora a
estrutura, aqui a hierarquia do PSSG é mantida como está:

    ROOTNODE → (NODE | MATRIXPALETTEBUNDLENODE "LOD0_") → MATRIXPALETTEJOINTNODE "x0_wheel_fl"
                                                         → MATRIXPALETTEJOINTRENDERINSTANCE (fatia)

Uma fatia é um intervalo dentro de um `RENDERDATASOURCE` compartilhado:
`streamOffset`/`elementCountFromOffset` (vértices) e `indexOffset`/
`indicesCountFromOffset` (índices, absolutos no buffer). Os vértices ficam locais
ao osso; a transformação do nó (`world`) leva o objeto para o espaço do carro.
Vários nós e várias fatias apontam para o mesmo buffer, então ele é exportado uma
vez (`pack_resources`) e a árvore só guarda referências.

Nada é inventado: cada nó e fatia leva `extra`, com todos os atributos crus do
PSSG que não viraram campo próprio, e `notes` registra o que foi estranho
(referência quebrada, intervalo fora do buffer…). Campo novo descoberto aparece
no inspector sem mexer na interface.

Formato dos recursos (`DR2C`, little-endian):

    char[4] magia "DR2C"
    uint32  número de recursos
    por recurso:
        uint16 tamanho do id, uint16 reservado (0)
        uint32 vértices, uint32 índices, uint32 flags (bit 0 = índice de 32 bits)
        id, preenchimento até múltiplo de 4
        float32 posição xyz (local ao osso), float32 uv
        uint16 ou uint32 índices (triângulos)
        preenchimento até múltiplo de 4
"""

from __future__ import annotations

import re
import struct
import sys
from typing import Any

from tools.egodata.pssg import PSSGNode
from tools.uiview.mesh import (
    _IDENT,
    _attr,
    _index,
    _matrix,
    _parents,
    _read_source,
    _ref,
    _world,
)

CAR_REV = 1
RES_MAGIC = b"DR2C"

# Tipos que formam a árvore de cena; o resto dentro de um nó é dado do nó.
_SCENE_NODES = {
    "ROOTNODE",
    "NODE",
    "MATRIXPALETTEBUNDLENODE",
    "MATRIXPALETTEJOINTNODE",
    "MATRIXPALETTENODE",
}
_DATA_CHILDREN = {"TRANSFORM", "BOUNDINGBOX"}
_SLICE_TYPES = {"MATRIXPALETTEJOINTRENDERINSTANCE"}
# Atributos de fatia que viram campo próprio (o resto vai para `extra`).
_SLICE_FIELDS = {
    "streamOffset", "elementCountFromOffset", "indexOffset", "indicesCountFromOffset",
    "jointID", "indices", "shader", "id",
}
_NODE_FIELDS = {"id", "nickname"}


def _log(message: str) -> None:
    print(f"[carmodel] {message}", file=sys.stderr)


def _plain(value: Any) -> Any:
    """Valor de atributo PSSG pronto para JSON (bytes viram hexadecimal)."""
    if isinstance(value, (bytes, bytearray)):
        return bytes(value).hex()
    if isinstance(value, float):
        return round(value, 6)
    return value


def _extra(node: PSSGNode, known: set[str]) -> dict[str, Any]:
    return {k: _plain(a.value) for k, a in node.attributes.items() if k not in known}


def _rounded(values: tuple[float, ...], digits: int = 6) -> list[float]:
    return [round(v, digits) for v in values]


def _bbox(node: PSSGNode) -> list[float] | None:
    for child in node.children:
        if child.type_name == "BOUNDINGBOX" and child.data and len(child.data) >= 24:
            return _rounded(struct.unpack(">6f", child.data[:24]), 5)
    return None


def lod_label(name: str) -> str | None:
    """`LOD0_` → `LOD0`; qualquer outro nome de pacote não é LOD."""
    match = re.match(r"(?i)^(?:x?\d*_)?(lod\d+)", name)
    return match.group(1).upper() if match else None


# ── materiais ────────────────────────────────────────────────────────────


def _group_inputs(idx: dict[str, PSSGNode]) -> dict[str, list[dict[str, str]]]:
    out: dict[str, list[dict[str, str]]] = {}
    for key, node in idx.items():
        if node.type_name != "SHADERGROUP":
            continue
        out[key] = [
            {"name": str(_attr(d, "name")), "type": str(_attr(d, "type")), "format": str(_attr(d, "format") or "")}
            for d in node.children
            if d.type_name == "SHADERINPUTDEFINITION"
        ]
    return out


def read_material(idx: dict[str, PSSGNode], name: str, groups: dict[str, list[dict[str, str]]], notes: list[str]) -> dict[str, Any] | None:
    node = idx.get(name)
    if node is None or node.type_name != "SHADERINSTANCE":
        return None
    group_id = _ref(_attr(node, "shaderGroup"))
    inputs = groups.get(group_id)
    if inputs is None:
        notes.append(f"material {name}: grupo {group_id!r} sem SHADERGROUP no arquivo")
        inputs = []
    params: dict[str, Any] = {}
    textures: dict[str, str] = {}
    unread = 0
    for child in node.children:
        if child.type_name != "SHADERINPUT":
            continue
        pid = _attr(child, "parameterID")
        if not isinstance(pid, int) or pid >= len(inputs):
            unread += 1
            continue
        definition = inputs[pid]
        label = definition["name"]
        texture = _attr(child, "texture")
        if texture:
            textures[label] = _ref(texture).rsplit(".", 1)[0]
        elif child.data and len(child.data) % 4 == 0 and len(child.data) <= 64:
            values = struct.unpack(f">{len(child.data) // 4}f", child.data)
            params[label] = _rounded(values, 5) if len(values) > 1 else round(values[0], 5)
        else:
            unread += 1
    if unread:
        notes.append(f"material {name}: {unread} SHADERINPUT sem nome ou valor legível")
    return {
        "id": name,
        "group": group_id,
        "params": params,
        "textures": textures,
        "paramCount": _attr(node, "parameterCount"),
        "savedCount": _attr(node, "parameterSavedCount"),
        "extra": _extra(node, {"id", "shaderGroup", "parameterCount", "parameterSavedCount"}),
    }


# ── árvore ───────────────────────────────────────────────────────────────


class _Builder:
    def __init__(self, root: PSSGNode) -> None:
        self.root = root
        self.idx = _index(root)
        self.parent = _parents(root)
        self.worlds: dict[int, tuple[float, ...]] = {}
        self.groups = _group_inputs(self.idx)
        self.notes: list[str] = []
        self.materials: dict[str, dict[str, Any]] = {}
        self.sources: dict[str, dict[str, Any] | None] = {}
        self.used: dict[str, dict[str, Any]] = {}
        self.counter = 0

    def material(self, ref: str) -> str:
        name = _ref(ref)
        if name not in self.materials:
            mat = read_material(self.idx, name, self.groups, self.notes)
            if mat is None:
                self.notes.append(f"material {name!r} sem SHADERINSTANCE")
                mat = {"id": name, "group": "", "params": {}, "textures": {}, "extra": {}}
            self.materials[name] = mat
        return name

    def source(self, rds: str) -> dict[str, Any] | None:
        if rds not in self.sources:
            data = _read_source(self.idx, rds)
            if data is None:
                self.notes.append(f"RENDERDATASOURCE {rds!r} sem geometria legível")
            self.sources[rds] = data
        return self.sources[rds]

    def slice(self, node: PSSGNode, owner: str) -> dict[str, Any]:
        rds = _ref(_attr(node, "indices"))
        vo = int(_attr(node, "streamOffset") or 0)
        vc = int(_attr(node, "elementCountFromOffset") or 0)
        io = int(_attr(node, "indexOffset") or 0)
        ic = int(_attr(node, "indicesCountFromOffset") or 0)
        material = self.material(_attr(node, "shader") or "")
        item: dict[str, Any] = {
            "id": str(_attr(node, "id") or ""),
            "material": material,
            "rds": rds,
            "vo": vo, "vc": vc, "io": io, "ic": ic,
            "tris": ic // 3,
            "joint": _attr(node, "jointID"),
            "extra": _extra(node, _SLICE_FIELDS),
        }
        data = self.source(rds)
        if data is None:
            item["ok"] = False
            return item
        total_v, total_i = len(data["positions"]), len(data["indices"])
        if vo + vc > total_v or io + ic > total_i:
            self.notes.append(f"fatia {item['id']} ({owner}) fora do buffer {rds}: vértices {vo}+{vc}/{total_v}, índices {io}+{ic}/{total_i}")
            item["ok"] = False
            return item
        if ic % 3:
            self.notes.append(f"fatia {item['id']} ({owner}): {ic} índices não é múltiplo de 3")
        item["ok"] = True
        self.used.setdefault(rds, data)
        return item

    def node(self, src: PSSGNode, depth: int) -> dict[str, Any]:
        name = str(_attr(src, "id") or _attr(src, "nickname") or src.type_name)
        local = _matrix(src)
        world = _world(src, self.parent, self.worlds)
        item: dict[str, Any] = {
            "uid": self.counter,
            "id": name,
            "nickname": str(_attr(src, "nickname") or ""),
            "type": src.type_name,
            "local": _rounded(local),
            "world": _rounded(world),
            "bbox": _bbox(src),
            "extra": _extra(src, _NODE_FIELDS),
            "slices": [],
            "children": [],
        }
        self.counter += 1
        if local == _IDENT:
            item["identity"] = True
        for child in src.children:
            if child.type_name in _SLICE_TYPES:
                item["slices"].append(self.slice(child, name))
            elif child.type_name in _SCENE_NODES:
                item["children"].append(self.node(child, depth + 1))
            elif child.type_name == "MATRIXPALETTERENDERINSTANCE":
                continue  # instância do palette: aparece em `skinSets`, não como objeto
            elif child.type_name not in _DATA_CHILDREN:
                self.notes.append(f"nó {name}: filho {child.type_name} não interpretado")
        return item


def _count(node: dict[str, Any]) -> tuple[int, int, int]:
    """(nós, fatias, triângulos) da subárvore."""
    nodes, slices, tris = 1, len(node["slices"]), sum(s["tris"] for s in node["slices"] if s.get("ok"))
    for child in node["children"]:
        a, b, c = _count(child)
        nodes, slices, tris = nodes + a, slices + b, tris + c
    return nodes, slices, tris


def _skin_sets(builder: _Builder) -> list[dict[str, Any]]:
    """`MATRIXPALETTERENDERINSTANCE`: material + buffer + lista de ossos que o skinning usa."""
    out = []
    for node in builder.idx.values():
        if node.type_name != "MATRIXPALETTERENDERINSTANCE":
            continue
        out.append({
            "id": str(_attr(node, "id") or ""),
            "rds": _ref(_attr(node, "indices")),
            "material": _ref(_attr(node, "shader")),
            "joints": [_ref(_attr(c, "joint")) for c in node.children if c.type_name == "MATRIXPALETTESKINJOINT"],
        })
    return out


def build_car_model(root: PSSGNode, car_id: str, source: dict[str, Any] | None = None) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    """Retorna `(modelo, recursos)`: o modelo vai para JSON, os recursos para `pack_resources`."""
    builder = _Builder(root)
    tree = None
    for lib in root.children:
        if lib.type_name == "LIBRARY" and _attr(lib, "type") == "NODE":
            for top in lib.children:
                if top.type_name in _SCENE_NODES:
                    tree = builder.node(top, 0)
                    break
    if tree is None:
        builder.notes.append("sem LIBRARY NODE/ROOTNODE: nada para mostrar")
        tree = {"uid": -1, "id": "(vazio)", "nickname": "", "type": "ROOTNODE", "local": list(_IDENT), "world": list(_IDENT),
                "bbox": None, "extra": {}, "slices": [], "children": []}

    lods = []
    for child in tree["children"]:
        label = lod_label(child["nickname"] or child["id"])
        if label:
            nodes, slices, tris = _count(child)
            lods.append({"name": label, "uid": child["uid"], "nodes": nodes, "slices": slices, "tris": tris})

    model = {
        "rev": CAR_REV,
        "id": car_id,
        "source": source or {},
        "tree": tree,
        "lods": lods,
        "materials": builder.materials,
        "skinSets": _skin_sets(builder),
        "resources": {k: {"verts": len(v["positions"]), "tris": len(v["indices"]) // 3} for k, v in builder.used.items()},
        "notes": builder.notes,
    }
    for line in builder.notes:
        _log(f"{car_id}: {line}")
    return model, builder.used


# ── recursos compartilhados ──────────────────────────────────────────────


def _align(buf: bytearray) -> None:
    pad = (-len(buf)) % 4
    if pad:
        buf += b"\0" * pad


def pack_resources(resources: dict[str, dict[str, Any]]) -> bytes:
    out = bytearray(RES_MAGIC)
    out += struct.pack("<I", len(resources))
    for rid, data in resources.items():
        name = rid.encode("utf-8")[:200]
        positions, uvs, indices = data["positions"], data["uvs"], data["indices"]
        wide = len(positions) > 65535
        out += struct.pack("<HHIII", len(name), 0, len(positions), len(indices), 1 if wide else 0)
        out += name
        _align(out)
        for x, y, z in positions:
            out += struct.pack("<3f", x, y, z)
        for i in range(len(positions)):
            u, v = uvs[i] if uvs and i < len(uvs) else (0.0, 0.0)
            out += struct.pack("<2f", u, v)
        out += struct.pack(f"<{len(indices)}{'I' if wide else 'H'}", *indices)
        _align(out)
    return bytes(out)


def unpack_resources(data: bytes) -> dict[str, dict[str, Any]]:
    if data[:4] != RES_MAGIC:
        raise ValueError("recursos sem a magia DR2C")
    count = struct.unpack_from("<I", data, 4)[0]
    pos = 8
    out: dict[str, dict[str, Any]] = {}
    for _ in range(count):
        name_len, _res, nv, ni, flags = struct.unpack_from("<HHIII", data, pos)
        pos += 16
        rid = data[pos:pos + name_len].decode("utf-8")
        pos += name_len
        pos = (pos + 3) & ~3
        positions = [struct.unpack_from("<3f", data, pos + i * 12) for i in range(nv)]
        pos += nv * 12
        uvs = [struct.unpack_from("<2f", data, pos + i * 8) for i in range(nv)]
        pos += nv * 8
        wide = flags & 1
        indices = list(struct.unpack_from(f"<{ni}{'I' if wide else 'H'}", data, pos))
        pos += ni * (4 if wide else 2)
        pos = (pos + 3) & ~3
        out[rid] = {"positions": positions, "uvs": uvs, "indices": indices}
    return out
