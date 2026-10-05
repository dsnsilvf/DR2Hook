"""Malha 3D legível de um PSSG de modelo (carro, personagem, objeto).

O visualizador usa o LOD0 quando o arquivo tem esse pacote. Cada
`MATRIXPALETTERENDERINSTANCE`, `RENDERSTREAMINSTANCE` ou
`MODIFIERNETWORKINSTANCE` vira uma malha. No carro o índice de osso é um float
por vértice; a posição final é o vértice local vezes a matriz do osso
(convenção do PSSG: 16 floats big-endian, translação em 12, 13 e 14). No
personagem o vértice `SkinnableVertex` já está na pose de repouso, então entra
como está. Se o mesmo material aparece em `_x1`, `_x2`, … fica só o `_x1`.
Fatias `MATRIXPALETTEJOINTRENDERINSTANCE` repetem a mesma malha e não entram.

O arquivo de geometria (`DR2M`) é little-endian:

    char[4] magia "DR2M"
    uint32  número de malhas
    por malha:
        uint16 tamanho do nome, uint16 tamanho do material
        uint32 vértices, uint32 índices, uint32 flags (bit 0 = índice de 32 bits)
        nome, material, preenchimento até múltiplo de 4
        float32 posição xyz, float32 uv
        uint16 ou uint32 índices
        preenchimento até múltiplo de 4
"""

from __future__ import annotations

import re
import struct
from typing import Any

from tools.pssg import PSSGNode

GEOM_MAGIC = b"DR2M"
_IDENT = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)


def decode_half(raw: int) -> float:
    return struct.unpack("<e", struct.pack("<H", raw & 0xFFFF))[0]


def transform_point(m: tuple[float, ...], x: float, y: float, z: float) -> tuple[float, float, float]:
    """Aplica uma matriz 4×4 no layout do PSSG (vetor-linha, translação no fim)."""
    return (
        x * m[0] + y * m[4] + z * m[8] + m[12],
        x * m[1] + y * m[5] + z * m[9] + m[13],
        x * m[2] + y * m[6] + z * m[10] + m[14],
    )


def multiply_matrix(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    out = [0.0] * 16
    for row in range(4):
        for col in range(4):
            out[row * 4 + col] = sum(a[row * 4 + k] * b[k * 4 + col] for k in range(4))
    return tuple(out)


def _attr(node: PSSGNode, key: str) -> Any:
    attr = node.attributes.get(key)
    return None if attr is None else attr.value


def _ref(value: Any) -> str:
    return str(value or "").lstrip("#")


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


def _parents(root: PSSGNode) -> dict[int, PSSGNode | None]:
    parent: dict[int, PSSGNode | None] = {}

    def walk(node: PSSGNode, up: PSSGNode | None) -> None:
        parent[id(node)] = up
        for child in node.children:
            walk(child, node)

    walk(root, None)
    return parent


def _matrix(node: PSSGNode | None) -> tuple[float, ...]:
    if node is None:
        return _IDENT
    for child in node.children:
        if child.type_name == "TRANSFORM" and child.data and len(child.data) >= 64:
            return struct.unpack(">16f", child.data[:64])
    return _IDENT


def _world(node: PSSGNode, parent: dict[int, PSSGNode | None], cache: dict[int, tuple[float, ...]]) -> tuple[float, ...]:
    key = id(node)
    if key in cache:
        return cache[key]
    local = _matrix(node)
    up = parent.get(key)
    world = multiply_matrix(local, _world(up, parent, cache)) if up is not None else local
    cache[key] = world
    return world


def _read_source(idx: dict[str, PSSGNode], source_id: str) -> dict[str, Any] | None:
    source = idx.get(source_id)
    if source is None:
        return None
    positions: list[tuple[float, float, float]] | None = None
    uvs: list[tuple[float, float]] | None = None
    skins: list[int] | None = None
    indices: list[int] = []
    primitive = "triangles"
    for child in source.children:
        if child.type_name == "RENDERINDEXSOURCE":
            primitive = str(_attr(child, "primitive") or "triangles")
            fmt = "H" if _attr(child, "format") == "ushort" else "I"
            data = next((c.data for c in child.children if c.type_name == "INDEXSOURCEDATA" and c.data), b"") or b""
            size = struct.calcsize(">" + fmt)
            count = len(data) // size
            if count:
                indices = list(struct.unpack(f">{count}{fmt}", data[: count * size]))
        elif child.type_name == "RENDERSTREAM":
            block = idx.get(_ref(_attr(child, "dataBlock")))
            if block is None:
                continue
            raw = next((c.data for c in block.children if c.type_name == "DATABLOCKDATA" and c.data), b"") or b""
            count = int(_attr(block, "elementCount") or 0)
            for stream in block.children:
                if stream.type_name != "DATABLOCKSTREAM":
                    continue
                kind, dtype = str(_attr(stream, "renderType") or ""), str(_attr(stream, "dataType") or "")
                offset, stride = int(_attr(stream, "offset") or 0), int(_attr(stream, "stride") or 0)
                width = {"float3": 12, "float2": 8, "half2": 4, "half4": 4, "float": 4}.get(dtype, 0)
                if stride <= 0 or count <= 0 or width <= 0:
                    continue
                if offset + (count - 1) * stride + width > len(raw):
                    continue
                if kind in ("Vertex", "SkinnableVertex") and dtype == "float3" and positions is None:
                    positions = [struct.unpack_from(">3f", raw, offset + i * stride) for i in range(count)]
                elif kind == "ST" and uvs is None and dtype in ("half2", "half4", "float2"):
                    coords = []
                    for i in range(count):
                        base = offset + i * stride
                        if dtype == "float2":
                            coords.append(struct.unpack_from(">2f", raw, base))
                        else:
                            u, v = struct.unpack_from(">2H", raw, base)
                            coords.append((decode_half(u), decode_half(v)))
                    uvs = coords
                elif kind == "SkinIndices" and skins is None and dtype == "float":
                    skins = [int(struct.unpack_from(">f", raw, offset + i * stride)[0]) for i in range(count)]
    if primitive == "triangle_strip" and len(indices) > 2:
        tris: list[int] = []
        for i in range(len(indices) - 2):
            a, b, c = indices[i : i + 3]
            tris += [a, b, c] if i % 2 == 0 else [b, a, c]
        indices = tris
    if not positions or len(indices) < 3:
        return None
    return {"positions": positions, "uvs": uvs, "skins": skins, "indices": indices}


def _lod_level(node: PSSGNode) -> int | None:
    match = re.search(r"_x(\d+)$", str(_attr(node, "id") or ""))
    return int(match.group(1)) if match else None


def _keep_detailed_lod(nodes: list[PSSGNode]) -> list[PSSGNode]:
    """Personagens repetem a mesma peça em `_x1` (detalhe) e `_x2`/`_x3`. Fica o `_x1`."""
    groups: dict[str, list[PSSGNode]] = {}
    kept: list[PSSGNode] = []
    for node in nodes:
        if node.type_name != "MODIFIERNETWORKINSTANCE" or _lod_level(node) is None:
            kept.append(node)
            continue
        groups.setdefault(_ref(_attr(node, "shader")), []).append(node)
    for group in groups.values():
        kept.append(min(group, key=lambda item: (0 if _lod_level(item) == 1 else 1, _lod_level(item) or 99)))
    return kept


def _has_lod0(root: PSSGNode) -> bool:
    stack = [root]
    while stack:
        node = stack.pop()
        if node.type_name == "MATRIXPALETTEBUNDLENODE" and str(_attr(node, "nickname") or "").upper().startswith("LOD0"):
            return True
        stack.extend(node.children)
    return False


def extract_meshes(root: PSSGNode) -> list[dict[str, Any]]:
    """Malhas do LOD0 (ou de todas as instâncias, se não houver LOD0)."""
    idx = _index(root)
    parent = _parents(root)
    worlds: dict[int, tuple[float, ...]] = {}
    only_lod0 = _has_lod0(root)
    found: list[PSSGNode] = []

    def walk(node: PSSGNode, in_lod0: bool) -> None:
        here = in_lod0
        if only_lod0 and node.type_name == "MATRIXPALETTEBUNDLENODE":
            here = str(_attr(node, "nickname") or "").upper().startswith("LOD0")
        if node.type_name in ("MATRIXPALETTERENDERINSTANCE", "RENDERSTREAMINSTANCE", "MODIFIERNETWORKINSTANCE") and (here or not only_lod0):
            found.append(node)
        for child in node.children:
            walk(child, here)

    walk(root, not only_lod0)
    found = _keep_detailed_lod(found)
    meshes = []
    seen: dict[str, int] = {}
    for instance in found:
        source_id = _ref(_attr(instance, "indices"))
        if not source_id:
            for child in instance.children:
                if child.type_name == "RENDERINSTANCESOURCE":
                    source_id = _ref(_attr(child, "source"))
        raw = _read_source(idx, source_id) if source_id else None
        if raw is None:
            continue
        joints = []
        for child in instance.children:
            if child.type_name != "MATRIXPALETTESKINJOINT":
                continue
            joint = idx.get(_ref(_attr(child, "joint")))
            joints.append(_world(joint, parent, worlds) if joint is not None else _IDENT)
        positions = []
        skins = raw["skins"]
        for i, (x, y, z) in enumerate(raw["positions"]):
            if skins and joints and i < len(skins) and 0 <= skins[i] < len(joints):
                positions.append(transform_point(joints[skins[i]], x, y, z))
            else:
                positions.append((x, y, z))
        material = _ref(_attr(instance, "shader")) or source_id
        seen[material] = seen.get(material, 0) + 1
        name = material if seen[material] == 1 else f"{material}_{seen[material]}"
        meshes.append({
            "name": name,
            "material": material,
            "positions": positions,
            "uvs": raw["uvs"],
            "indices": raw["indices"],
        })
    return meshes


def _align(buf: bytearray) -> None:
    pad = (-len(buf)) % 4
    if pad:
        buf += b"\0" * pad


def pack_geom(meshes: list[dict[str, Any]]) -> bytes:
    out = bytearray(GEOM_MAGIC)
    out += struct.pack("<I", len(meshes))
    for mesh in meshes:
        name = str(mesh["name"]).encode("utf-8")[:200]
        material = str(mesh["material"]).encode("utf-8")[:200]
        positions = mesh["positions"]
        uvs = mesh["uvs"]
        indices = mesh["indices"]
        wide = any(i > 65535 for i in indices)
        out += struct.pack("<HHIII", len(name), len(material), len(positions), len(indices), 1 if wide else 0)
        out += name + material
        _align(out)
        for x, y, z in positions:
            out += struct.pack("<3f", x, y, z)
        for i in range(len(positions)):
            if uvs and i < len(uvs):
                out += struct.pack("<2f", uvs[i][0], uvs[i][1])
            else:
                out += struct.pack("<2f", 0.0, 0.0)
        fmt = "<I" if wide else "<H"
        for index in indices:
            out += struct.pack(fmt, index)
        _align(out)
    return bytes(out)


def summarize_geom(path: str) -> dict[str, Any] | None:
    """Contagens de um `DR2M` já gravado, sem carregar os vértices. None se o arquivo não serve."""
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError:
        return None
    if len(data) < 8 or data[:4] != GEOM_MAGIC:
        return None
    count = struct.unpack_from("<I", data, 4)[0]
    pos = 8
    mats: list[str] = []
    verts = 0
    tris = 0
    try:
        for _ in range(count):
            if pos + 16 > len(data):
                return None
            name_len, mat_len, nv, ni, flags = struct.unpack_from("<HHIII", data, pos)
            pos += 16 + name_len
            if pos + mat_len > len(data):
                return None
            material = data[pos:pos + mat_len].decode("utf-8")
            pos += mat_len
            pos = (pos + 3) & ~3
            step = 4 if flags & 1 else 2
            pos += nv * 20 + ni * step
            pos = (pos + 3) & ~3
            if pos > len(data):
                return None
            mats.append(material)
            verts += nv
            tris += ni // 3
    except (struct.error, UnicodeDecodeError):
        return None
    return {
        "meshes": count,
        "mats": list(dict.fromkeys(mats)),
        "verts": verts,
        "tris": tris,
    }


def unpack_geom(data: bytes) -> list[dict[str, Any]]:
    if data[:4] != GEOM_MAGIC:
        raise ValueError("geometria sem a magia DR2M")
    count = struct.unpack_from("<I", data, 4)[0]
    pos = 8
    meshes = []
    for _ in range(count):
        name_len, mat_len, verts, indices, flags = struct.unpack_from("<HHIII", data, pos)
        pos += 16
        name = data[pos : pos + name_len].decode("utf-8")
        pos += name_len
        material = data[pos : pos + mat_len].decode("utf-8")
        pos += mat_len
        pos = (pos + 3) & ~3
        positions = [struct.unpack_from("<3f", data, pos + i * 12) for i in range(verts)]
        pos += verts * 12
        uvs = [struct.unpack_from("<2f", data, pos + i * 8) for i in range(verts)]
        pos += verts * 8
        wide = flags & 1
        fmt = "<I" if wide else "<H"
        size = 4 if wide else 2
        index_list = [struct.unpack_from(fmt, data, pos + i * size)[0] for i in range(indices)]
        pos += indices * size
        pos = (pos + 3) & ~3
        meshes.append({"name": name, "material": material, "positions": positions, "uvs": uvs, "indices": index_list})
    return meshes
