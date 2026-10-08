#!/usr/bin/env python3
"""Objetos do Ring (editor de níveis) como ornamentos do jogo, com malhas e texturas próprias.

Entra: `examples/tracks/synthetic__dr2hook_ring/` (objects.bin = malhas por tipo, inst_route_0.bin = instâncias,
track.json = ordem dos tipos e materiais, source/textures = PNGs). Sai, numa pasta:

- `objects.pssg`: o da Montalegre + um ROOTNODE "<tipo> Root" por tipo do Ring (molde: `mnt_turbine Root`, nó
  "lod" com uma malha por material e a árvore default/rigidbody/<tipo>_physics com as mesmas malhas). Os da
  Montalegre ficam porque o `route_objecttypes.pssg` aponta para nós deles (objects.pssg#default!N);
- `objectstextures.pssg`: o da Montalegre + as texturas do Ring em resolução cheia (as do jogo aqui são 4×4 e o
  `patchup_ot.pssg` troca pelo nome; as nossas não estão lá);
- `ornaments_references.xml`: um `instanceref` por tipo (reference_id = FNV-1a do nome, como o synthtrack);
- `route_0/ornaments.bin`: cabeçalho da Montalegre, sem path anims, um registro de 212 bytes por instância
  (referenceId, instanceId = índice da camada 2 do track.vis, 3×3 em +16, posição em +52; o resto copiado de um
  registro estático da Montalegre);
- `route_0/track.vis`: células do tracksplit do Ring + a camada 2 com a caixa de cada instância.

Com `--edits`, as edições do viewer3d (edits.json) entram antes: objetos movidos, apagados e copiados.

As instâncias passam pelo mesmo giro e deslocamento do terreno (`ring_tracksplit.start_transform`). Sem colisão:
ornamento não tem física (o carro atravessa).

    python3 scripts/research/ring_objects.py build/re/ring_objects --tracksplit build/re/ring_pad/tracksplit.pssg
"""
from __future__ import annotations

import argparse
import copy
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from ring_tracksplit import dxt_mips, rotate_y, start_transform  # noqa: E402
from tools.egodata.pssg import PSSGFile, PSSGNode  # noqa: E402
from tools.uiview import mesh  # noqa: E402
from track_vis import TrackVis, tracksplit_cells  # noqa: E402

RING = "examples/tracks/synthetic__dr2hook_ring"
HOST = "build/re/montalegre_objects"  # cópias originais da Montalegre (objects.pssg, objectstextures.pssg, ornaments.bin)
HOST_VIS = "build/re/montalegre/route_0__track.vis"
# moldes dentro do objects.pssg da Montalegre
T_ROOT = "mnt_turbine Root"
T_SOURCE = "!TzB"  # RENDERDATASOURCE de 7 streams: Vertex (bloco 1) + ST, ST, Normal, Tangent, Binormal, Color (bloco 2)
T_SHADER = "mnt_blackbollard_01!0"  # Object_Norm.fx: um lado, opaco (peças fechadas)
# object_2sided_normalpha.fx: dois lados e alfa, para o que é plano ou vazado (cerca, placas). Não grava a ordem
# certa entre faces: numa peça fechada (faixa do pórtico) o verso desenhado depois aparece por cima e espelha o texto
T_SHADER_2SIDED = "mnt_barr_concrete_fence_a_02"
T_TEXTURE = "mnt_blackbollard_01_d.tga"
# parâmetros do shader -> textura (21 TDiffuseAlphaMap, 22 TNormalMap, 23 TSpecularMap, 24 TOcclusionMap)
# Canais como os dos objetos da Montalegre (patchup_ot.pssg): normal em DXT5 com X no alfa e Y no verde (R = 255,
# B = 0; plana = 128/128; a RGB "comum" deita a normal e o objeto fica com cara de vidro); especular R = 255, G e B
# variam (concreto mnt_barr_blockfence_a_01_s ≈ 255/76/247); oclusão cinza ~190 nos originais, branca aqui.
SUPPORT = {
    22: ("dr2hook_obj_n.tga", (255, 128, 0, 128), "dxt5"),
    23: ("dr2hook_obj_s.tga", (255, 76, 247, 255), "dxt1"),
    24: ("dr2hook_obj_a.tga", (255, 255, 255, 255), "dxt1"),
}
RECORD = 212
RECORD_TEMPLATE = 0  # registro da Montalegre usado de base (cerca estática: +8 = 0, +116 = 1, +196 = 0)
HEADER = 140


def fnv1a(name: str) -> int:
    h = 2166136261
    for ch in name.encode("utf-8"):
        h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return h


def read_instances(path: str, type_order: list[str], with_ids: bool = False) -> list[tuple]:
    """(tipo, matriz 4×4 vetor-linha) de cada instância do inst_<rota>.bin do viewer; com `with_ids`,
    (tipo, matriz, identificador = posição no arquivo de origem, a chave do edits.json)."""
    data = open(path, "rb").read()
    assert data[:4] == b"DR2I"
    (n,) = struct.unpack_from("<I", data, 4)
    types = struct.unpack_from(f"<{n}H", data, 8)
    o = 8 + 2 * n
    o += -o % 4
    ids = struct.unpack_from(f"<{n}I", data, o)
    o += 4 * n
    flat = np.frombuffer(data, "<f4", n * 12, o).reshape(n, 12).astype(np.float64)
    out = []
    for t, i, f in zip(types, ids, flat):
        m = matrix12(f)
        out.append((type_order[t], m, i) if with_ids else (type_order[t], m))
    return out


def matrix12(f) -> np.ndarray:
    """12 floats do viewer (3 eixos e a translação, linha a linha) para a 4×4 vetor-linha."""
    m = np.eye(4)
    m[0, :3], m[1, :3], m[2, :3], m[3, :3] = f[0:3], f[3:6], f[6:9], f[9:12]
    return m


def apply_edits(insts: list[tuple], edits: dict, route: str) -> tuple[list[tuple[str, np.ndarray]], list[str]]:
    """Aplica o edits.json do viewer3d (dr2-track-edits v1) às instâncias (tipo, matriz, id) de `read_instances`.

    Como o viewer: a chave é (tipo, id); movida troca a matriz, apagada sai, cópia (`added`, `src` = id da
    original) entra no fim com o tipo da original. Devolve (instâncias, avisos do que ficou de fora)."""
    if edits.get("format") != "dr2-track-edits" or edits.get("version") != 1:
        raise SystemExit("não é um edits.json (dr2-track-edits v1)")
    by_key = {(t, i): k for k, (t, _, i) in enumerate(insts)}
    mats = [m for _, m, _ in insts]
    gone = set()
    added = []
    skipped = []
    for n, e in enumerate(edits.get("edits", [])):
        if e.get("route") != route:
            continue
        name = f"{e.get('kind')}:{e.get('type')}"
        key = e.get("src") if e.get("added") else e.get("index")
        k = by_key.get((name, key))
        m = e.get("m")
        if k is None or not isinstance(m, list) or len(m) != 12:
            skipped.append(f"#{n + 1} {name} {key}")
            continue
        if e.get("added"):
            added.append((name, matrix12(np.asarray(m, np.float64))))
        elif e.get("deleted"):
            gone.add(k)
        else:
            mats[k] = matrix12(np.asarray(m, np.float64))
    out = [(t, mats[k]) for k, (t, _, _) in enumerate(insts) if k not in gone]
    return out + added, skipped


def to_game(m: np.ndarray, yaw: float, offset: np.ndarray) -> np.ndarray:
    """Mesmo giro/deslocamento do terreno: as linhas (eixos e translação) giram em Y; a translação soma o offset."""
    g = m.copy()
    g[:4, :3] = rotate_y(m[:4, :3], yaw)
    g[3, :3] += offset
    return g


def vertex_blocks(pos: np.ndarray, uv: np.ndarray, idx: np.ndarray) -> list[bytes]:
    """Bloco 1: Vertex float3 BE (12 bytes). Bloco 2 (36 bytes): ST0, ST1 (half2), Normal, Tangent, Binormal
    (half4, w = 1) e Color ARGB ff000000, como os ornamentos da Montalegre."""
    nrm = np.zeros_like(pos)
    tri = idx.reshape(-1, 3)
    fn = np.cross(pos[tri[:, 1]] - pos[tri[:, 0]], pos[tri[:, 2]] - pos[tri[:, 0]])
    for k in range(3):
        np.add.at(nrm, tri[:, k], fn)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
    ref = np.where(np.abs(nrm[:, 1:2]) > 0.9, [[1.0, 0.0, 0.0]], [[0.0, 1.0, 0.0]])
    tan = np.cross(ref, nrm)
    tan /= np.maximum(np.linalg.norm(tan, axis=1, keepdims=True), 1e-9)
    binormal = np.cross(nrm, tan)
    n = len(pos)
    one = np.ones((n, 1))
    halves = np.hstack([uv, uv, nrm, one, tan, one, binormal, one]).astype(">f2")
    rec = np.zeros(n, dtype=[("h", ">f2", 16), ("c", "u1", 4)])
    rec["h"] = halves
    rec["c"] = (0xFF, 0, 0, 0)
    return [pos.astype(">f4").tobytes(), rec.tobytes()]


class ObjectsBuilder:
    def __init__(self, objects: PSSGFile, textures: PSSGFile):
        self.p, self.t = objects, textures
        self.byid = {n.id: n for n in objects.find_nodes(lambda n: n.id is not None)}
        self.libs = {n.get("type"): n for n in objects.find_by_type("LIBRARY")}
        self.tex_lib = textures.find_by_type("LIBRARY")[0]
        self.tex_template = textures.find_by_id(T_TEXTURE)[0]
        self.serial = 0
        root = self.byid[T_ROOT]
        self.t_root = root
        self.t_lod = next(c for c in root.children if c.type_name == "LODVISIBLERENDERNODE")
        self.t_inst = next(c for c in self.t_lod.children if c.type_name == "RENDERSTREAMINSTANCE")
        self.t_lodlist = next(c for c in self.t_lod.children if c.type_name == "LODRENDERINSTANCES")
        self.t_default = next(c for c in root.children if c.type_name == "NODE")
        self.t_ds = self.byid[T_SOURCE]
        self.t_dbs = list(dict.fromkeys(self.byid[rs.get("dataBlock").lstrip("#")] for rs in self.t_ds.children[1:]))
        self.t_segset = next(n for n in self.libs["SEGMENTSET"].children if n.type_name == "SEGMENTSET")
        self.textures: set[str] = set()

    def new_id(self, kind: str = "") -> str:
        self.serial += 1
        return f"dr2hook{kind}!{self.serial}"

    @staticmethod
    def clone(node: PSSGNode, keep_children=True) -> PSSGNode:
        n = copy.deepcopy(node)
        if not keep_children:
            n.children = []
        return n

    @staticmethod
    def set_data(node: PSSGNode, data: bytes) -> None:
        node.data = data
        node._raw_payload = None
        node._orig_data = None
        node._compressed = False

    def texture(self, tex_id: str, img, fmt: str) -> str:
        if tex_id in self.textures:
            return tex_id
        from PIL import Image

        if fmt == "ui8x4":
            data, mips, w = b"", -1, img.size[0]
            while w >= 4:
                r, g, b_, a_ = img.resize((w, w * img.size[1] // img.size[0]), Image.LANCZOS).split()
                data += Image.merge("RGBA", (b_, g, r, a_)).tobytes()
                mips, w = mips + 1, w // 2
        else:
            data, mips = dxt_mips(img, "DXT5" if fmt.startswith("dxt5") else "DXT1")
        t = self.clone(self.tex_template)
        for k, v in (("id", tex_id), ("width", img.size[0]), ("height", img.size[1]), ("texelFormat", fmt),
                     ("numberMipMapLevels", mips)):
            PSSGFile.set_attr(t, k, v)
        block = t.children[0]
        PSSGFile.set_attr(block, "size", len(data))
        self.set_data(block.children[0], data)
        self.tex_lib.children.append(t)
        self.textures.add(tex_id)
        return tex_id

    def material(self, mat_id: str, diffuse: str, two_sided: bool) -> str:
        si = self.clone(self.byid[T_SHADER_2SIDED if two_sided else T_SHADER])
        PSSGFile.set_attr(si, "id", mat_id)
        for c in si.children:
            if c.get("type") != "texture":
                continue
            pid = int(c.get("parameterID"))
            tex = diffuse if pid == 21 else SUPPORT[pid][0]
            PSSGFile.set_attr(c, "texture", "objectstextures.pssg#" + tex)
        self.libs["SHADERINSTANCE"].children.append(si)
        return mat_id

    def mesh_source(self, blocks: list[bytes], count: int, indices: np.ndarray) -> str:
        new_ids = {}
        for tdb, vb in zip(self.t_dbs, blocks):
            db = self.clone(tdb)
            new_ids[tdb.id] = self.new_id("db")
            PSSGFile.set_attr(db, "id", new_ids[tdb.id])
            PSSGFile.set_attr(db, "size", len(vb))
            PSSGFile.set_attr(db, "elementCount", count)
            self.set_data(next(c for c in db.children if c.type_name == "DATABLOCKDATA"), vb)
            self.libs["RENDERINTERFACEBOUND"].children.append(db)
        ds = self.clone(self.t_ds)
        ds_id = self.new_id("ds")
        PSSGFile.set_attr(ds, "id", ds_id)
        ix = ds.children[0]
        PSSGFile.set_attr(ix, "id", self.new_id("ix"))
        PSSGFile.set_attr(ix, "count", int(indices.size))
        PSSGFile.set_attr(ix, "maximumIndex", int(indices.max()))
        self.set_data(ix.children[0], indices.astype(">u2").tobytes())
        for k, rs in enumerate(ds.children[1:]):
            PSSGFile.set_attr(rs, "dataBlock", "#" + new_ids[rs.get("dataBlock").lstrip("#")])
            PSSGFile.set_attr(rs, "id", f"{ds_id}_{k}")
        seg = self.clone(self.t_segset, keep_children=False)
        PSSGFile.set_attr(seg, "id", self.new_id("seg"))
        PSSGFile.set_attr(seg, "segmentCount", 1)
        seg.children = [ds]
        self.libs["SEGMENTSET"].children.append(seg)
        return ds_id

    def instance(self, ds_id: str, shader: str) -> PSSGNode:
        inst = self.clone(self.t_inst)
        PSSGFile.set_attr(inst, "id", self.new_id("rsi"))
        PSSGFile.set_attr(inst, "indices", "#" + ds_id)
        PSSGFile.set_attr(inst, "shader", "#" + shader)
        PSSGFile.set_attr(inst.children[0], "source", "#" + ds_id)
        return inst

    def node(self, template: PSSGNode, node_id: str, nick: str | None, bbox, children) -> PSSGNode:
        n = self.clone(template, keep_children=False)
        PSSGFile.set_attr(n, "id", node_id)
        if nick is not None and "nickname" in n.attributes:
            PSSGFile.set_attr(n, "nickname", nick)
        tr, bb = (self.clone(c) for c in template.children[:2])
        if bbox is not None:
            self.set_data(bb, struct.pack(">6f", *bbox))
        n.children = [tr, bb] + children
        return n

    def root(self, name: str, parts: list[tuple[str, str]], bbox) -> PSSGNode:
        """ROOTNODE "<name> Root" no molde da turbina; `parts` = (data source, shader)."""
        rb = self.t_default.children[2]  # NODE rigidbody
        phys = rb.children[2]  # RENDERNODE <nome>_physics
        k = self.serial
        lod = self.node(self.t_lod, f"lod!dr2hook{k}", "lod", bbox,
                        [self.instance(ds, sh) for ds, sh in parts] + [self.clone(self.t_lodlist)])
        physics = self.node(phys, f"{name}_physics", f"{name}_physics", bbox,
                            [self.instance(ds, sh) for ds, sh in parts])
        body = self.node(rb, f"rigidbody!dr2hook{k}", "rigidbody", None, [physics])
        default = self.node(self.t_default, f"default!dr2hook{k}", "default", None, [body])
        return self.node(self.t_root, f"{name} Root", None, None, [lod, default])


def ornaments_bin(host: bytes, records: list[tuple[int, np.ndarray]], lo, hi) -> bytes:
    """Cabeçalho da Montalegre (sem path anims) + um registro por instância; `records` = (referenceId, matriz)."""
    head = bytearray(host[:HEADER])
    template = host[HEADER + RECORD * RECORD_TEMPLATE:HEADER + RECORD * (RECORD_TEMPLATE + 1)]
    body = bytearray()
    for i, (ref, m) in enumerate(records):
        r = bytearray(template)
        struct.pack_into("<II", r, 0, ref, i)
        struct.pack_into("<9f", r, 16, *m[0, :3], *m[1, :3], *m[2, :3])
        struct.pack_into("<3f", r, 52, *m[3, :3])
        struct.pack_into("<I", r, 80, 60000 + i)  # único por registro na Montalegre
        body += r
    end = HEADER + len(body)
    struct.pack_into("<4f", head, 40, *lo, 1.0)
    struct.pack_into("<4f", head, 56, *hi, 1.0)
    struct.pack_into("<I", head, 72, len(records))  # tamanho da lista de itens (ids 0..n-1)
    struct.pack_into("<I", head, 88, len(records))
    for off in (100, 112, 128):  # listas de dependentes e de path anims, vazias, no fim
        struct.pack_into("<Q", head, off, end)
    struct.pack_into("<I", head, 136, 0)  # path anims (o helicóptero da Montalegre)
    return bytes(head + body + b"\0" * 16)


def references_xml(refs: list[dict]) -> str:
    lines = ['﻿<?xml version="1.0" encoding="utf-8"?>', "<instancedata>",
             f'  <instancelist reference_num="{len(refs)}" lowlod_count="0">']
    for r in refs:
        lo = " ".join(f"{v:.6g}" for v in r["lo"]) + " "
        hi = " ".join(f"{v:.6g}" for v in r["hi"]) + " "
        lines.append(f'    <instanceref reference_id="{r["id"]}" filename="{r["name"]}" prebaked_shadows="0" '
                     f'bounds_min="{lo}" bounds_max="{hi}" max_instances="{r["count"]}" />')
    lines += ["  </instancelist>", "</instancedata>", ""]
    return "\r\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--tracksplit", required=True, help="tracksplit.pssg do Ring (células da camada 0 do track.vis)")
    ap.add_argument("--route", default="route_0")
    ap.add_argument("--kinds", default="eo", help="prefixos de tipo do editor a portar (e entidades, o ornamentos, t árvores)")
    ap.add_argument("--edits", help="edits.json do viewer3d: objetos movidos, apagados e copiados")
    a = ap.parse_args()
    from PIL import Image

    track = json.load(open(os.path.join(RING, "track.json")))
    lib = mesh.unpack_geom(open(os.path.join(RING, "objects.bin"), "rb").read())
    yaw, offset = start_transform()
    objects = PSSGFile(open(os.path.join(HOST, "objects.pssg"), "rb").read())
    textures = PSSGFile(open(os.path.join(HOST, "objectstextures.pssg"), "rb").read())
    b = ObjectsBuilder(objects, textures)
    for pid, (tex_id, rgba, fmt) in SUPPORT.items():
        b.texture(tex_id, Image.new("RGBA", (16, 16), rgba), fmt)

    materials: dict[tuple[str, bool], str] = {}

    def material(name: str, flat: bool) -> str:
        base = name.split("|", 1)[1]
        # o material aponta a textura no track.json (t|synth_trunk usa synth_bark_d)
        tex = track["materials"].get(name)
        tex_name = os.path.splitext(os.path.basename(tex))[0] if tex else f"{base}_d"
        img = Image.open(os.path.join(RING, "source", "textures", f"{tex_name}.png"))
        alpha = img.mode == "RGBA" and img.getextrema()[3][0] < 255
        key = (name, flat or alpha)
        if key not in materials:
            tex = b.texture(f"dr2hook_{base}_d.tga", img.convert("RGBA"), "dxt5_srgb" if alpha else "dxt1_srgb")
            materials[key] = b.material(f"dr2hook_{base}" + ("_2s" if key[1] else ""), tex, key[1])
        return materials[key]

    insts = read_instances(os.path.join(RING, f"inst_{a.route}.bin"), track["type_order"], with_ids=True)
    if a.edits:
        insts, skipped = apply_edits(insts, json.load(open(a.edits, encoding="utf-8")), a.route)
        print(f"  edições de {a.edits}: {len(skipped)} ficaram de fora" + (f" ({', '.join(skipped[:5])})" if skipped else ""))
    else:
        insts = [(t, m) for t, m, _ in insts]
    # o jogo espera o XML e os registros do ornaments.bin em ordem crescente de reference_id
    wanted = sorted((t for t in track["type_order"] if t[0] in a.kinds and track["types"][t]["count"]),
                    key=lambda t: fnv1a(t[2:]))
    refs, boxes, records = [], [], []
    node_lib = b.libs["NODE"]
    for t in wanted:
        info = track["types"][t]
        parts, corners = [], []
        for m in lib[info["first"]:info["first"] + info["count"]]:
            pos = np.asarray(m["positions"], dtype=np.float64)
            uv = np.asarray(m["uvs"], dtype=np.float64).reshape(-1, 2)
            idx = np.asarray(m["indices"], dtype=np.int64)
            ds = b.mesh_source(vertex_blocks(pos, uv, idx), len(pos), idx)
            flat = np.linalg.matrix_rank(pos - pos.mean(0), tol=1e-4) < 3  # placa de um quad só: ver dos dois lados
            parts.append((ds, material(m["material"], flat)))
            corners.append(pos)
        allp = np.vstack(corners)
        lo, hi = allp.min(0), allp.max(0)
        name = t[2:]
        node_lib.children.append(b.root(name, parts, (*lo, *hi)))
        mine = [g for tt, g in insts if tt == t]
        ref = fnv1a(name)
        refs.append({"id": ref, "name": name, "lo": lo, "hi": hi, "count": len(mine)})
        box8 = np.array([[x, y, z, 1.0] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
        for m in mine:
            g = to_game(m, yaw, offset)
            w = box8 @ g
            boxes.append((tuple(w[:, :3].min(0)), tuple(w[:, :3].max(0))))
            records.append((ref, g))
        print(f"  {name}: {len(parts)} malha(s), {len(mine)} instância(s)")

    os.makedirs(os.path.join(a.out, a.route), exist_ok=True)
    with open(os.path.join(a.out, "objects.pssg"), "wb") as fh:
        fh.write(objects.serialize())
    with open(os.path.join(a.out, "objectstextures.pssg"), "wb") as fh:
        fh.write(textures.serialize())
    with open(os.path.join(a.out, "ornaments_references.xml"), "w", encoding="utf-8", newline="") as fh:
        fh.write(references_xml(refs))
    lo = np.min([bx[0] for bx in boxes], axis=0)
    hi = np.max([bx[1] for bx in boxes], axis=0)
    host_bin = open(os.path.join(HOST, "ornaments.bin"), "rb").read()
    with open(os.path.join(a.out, a.route, "ornaments.bin"), "wb") as fh:
        fh.write(ornaments_bin(host_bin, records, lo, hi))
    vis = TrackVis(open(HOST_VIS, "rb").read())
    with open(os.path.join(a.out, a.route, "track.vis"), "wb") as fh:
        fh.write(vis.with_cells(tracksplit_cells(a.tracksplit), keep_layers={2}, extra={2: boxes}))
    print(f"{a.out}: {len(refs)} tipos, {len(records)} instâncias, {len(materials)} materiais")


if __name__ == "__main__":
    main()
