"""Edição no lugar das malhas do `tracksplit.pssg` (terreno e pista de uma localidade).

O PSSG guarda os vértices e os índices em big-endian dentro de nós `DATABLOCKDATA` e
`INDEXSOURCEDATA`. Aqui cada malha é localizada pelo deslocamento absoluto no arquivo, e as
edições são gravadas por cima dos mesmos bytes (o tamanho do arquivo não muda). O resultado
é entregue ao gravador de `.nefs` como `BlockPatch`, que recomprime só os blocos tocados.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

from tools.egodata.nefs_write import BLOCK_SIZE, BlockPatch
from tools.egodata.pssg import PSSGFile, PSSGNode

_WIDTH = {"float3": 12, "float2": 8, "float4": 16, "half2": 4, "half4": 8, "float": 4, "uint_color_argb": 4}


@dataclass
class Stream:
    kind: str
    dtype: str
    start: int
    stride: int
    count: int


@dataclass
class MeshRef:
    material: str
    source_id: str
    primitive: str
    index_start: int
    index_count: int
    index_format: str
    streams: list[Stream] = field(default_factory=list)

    @property
    def vertex_count(self) -> int:
        return max((s.count for s in self.streams), default=0)

    def stream(self, kind: str, dtype: str | None = None) -> Stream | None:
        for s in self.streams:
            if s.kind == kind and (dtype is None or s.dtype == dtype):
                return s
        return None


def _ref(value: object) -> str:
    return str(value or "").lstrip("#")


def _payload_start(node: PSSGNode) -> int:
    return node.offset + 12 + node.attr_size


def locate_meshes(split: PSSGFile) -> list[MeshRef]:
    """Uma `MeshRef` por instância de renderização, com os deslocamentos dos vértices e dos índices."""
    by_id: dict[str, PSSGNode] = {}
    stack = [split.root]
    while stack:
        node = stack.pop()
        node_id = node.get("id")
        if node_id:
            by_id[str(node_id)] = node
        stack.extend(node.children)
    meshes: list[MeshRef] = []
    for inst in split.find_by_type("RENDERSTREAMINSTANCE"):
        source = by_id.get(_ref(inst.get("indices")))
        if source is None:
            continue
        ref = MeshRef(_ref(inst.get("shader")), str(source.get("id")), str(source.get("primitive") or "triangles"), 0, 0, "ushort")
        for child in source.children:
            if child.type_name == "RENDERINDEXSOURCE":
                data = next((c for c in child.children if c.type_name == "INDEXSOURCEDATA"), None)
                if data is not None and data.data:
                    assert not data._compressed
                    ref.index_start = _payload_start(data)
                    ref.index_format = str(child.get("format") or "ushort")
                    ref.index_count = len(data.data) // (2 if ref.index_format == "ushort" else 4)
            elif child.type_name == "RENDERSTREAM":
                block = by_id.get(_ref(child.get("dataBlock")))
                if block is None:
                    continue
                data = next((c for c in block.children if c.type_name == "DATABLOCKDATA"), None)
                if data is None or not data.data:
                    continue
                assert not data._compressed
                base = _payload_start(data)
                count = int(block.get("elementCount") or 0)
                for stream in block.children:
                    if stream.type_name != "DATABLOCKSTREAM":
                        continue
                    kind, dtype = str(stream.get("renderType") or ""), str(stream.get("dataType") or "")
                    entry = Stream(kind, dtype, base + int(stream.get("offset") or 0), int(stream.get("stride") or 0), count)
                    if not any(o.kind == kind and o.start == entry.start for o in ref.streams):
                        ref.streams.append(entry)
        meshes.append(ref)
    return meshes


class TerrainBuffer:
    """O conteúdo do `tracksplit.pssg` em memória, com leitura e escrita das malhas e controle dos blocos tocados."""

    def __init__(self, data: bytes):
        self.buf = bytearray(data)
        self.touched: set[int] = set()

    def _mark(self, start: int, size: int) -> None:
        for block in range(start // BLOCK_SIZE, (start + size - 1) // BLOCK_SIZE + 1):
            self.touched.add(block)

    def read_positions(self, mesh: MeshRef) -> list[tuple[float, float, float]]:
        s = mesh.stream("Vertex", "float3")
        if s is None:
            return []
        return [struct.unpack_from(">3f", self.buf, s.start + i * s.stride) for i in range(s.count)]

    def write_positions(self, mesh: MeshRef, positions: list[tuple[float, float, float]]) -> None:
        """Grava as posições em todos os fluxos `Vertex float3` da malha (há malhas com a posição em bloco à parte)."""
        for s in mesh.streams:
            if s.kind != "Vertex" or s.dtype != "float3":
                continue
            if len(positions) != s.count:
                raise ValueError(f"{mesh.source_id}: {len(positions)} posições para {s.count} vértices")
            for i, (x, y, z) in enumerate(positions):
                struct.pack_into(">3f", self.buf, s.start + i * s.stride, x, y, z)
            self._mark(s.start, (s.count - 1) * s.stride + 12)

    def read_indices(self, mesh: MeshRef) -> list[int]:
        fmt = ">%d%s" % (mesh.index_count, "H" if mesh.index_format == "ushort" else "I")
        return list(struct.unpack_from(fmt, self.buf, mesh.index_start))

    def write_indices(self, mesh: MeshRef, indices: list[int]) -> None:
        if len(indices) != mesh.index_count:
            raise ValueError(f"{mesh.source_id}: {len(indices)} índices para {mesh.index_count}")
        fmt = ">%d%s" % (len(indices), "H" if mesh.index_format == "ushort" else "I")
        struct.pack_into(fmt, self.buf, mesh.index_start, *indices)
        self._mark(mesh.index_start, len(indices) * (2 if mesh.index_format == "ushort" else 4))

    def patch(self) -> BlockPatch:
        return BlockPatch(self.buf, self.touched)


def locate_bounding_boxes(split: PSSGFile) -> list[int]:
    """Deslocamento do payload de cada `BOUNDINGBOX` (6 floats big-endian: mínimo e máximo)."""
    return [_payload_start(n) for n in split.find_by_type("BOUNDINGBOX") if n.data and len(n.data) == 24]


def widen_bounding_boxes(buf: TerrainBuffer, offsets: list[int], limit: float = 100000.0) -> None:
    """Abre todas as caixas para o mundo todo: nada é descartado por culling. Só as que já tinham volume."""
    for off in offsets:
        lo = struct.unpack_from(">3f", buf.buf, off)
        hi = struct.unpack_from(">3f", buf.buf, off + 12)
        if lo == (0.0, 0.0, 0.0) and hi == (0.0, 0.0, 0.0):
            continue
        struct.pack_into(">6f", buf.buf, off, -limit, -limit, -limit, limit, limit, limit)
        buf._mark(off, 24)
