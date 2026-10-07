"""Gera um `tracksplit.pssg` com o terreno do Ring (examples/tracks/synthetic__dr2hook_ring) no lugar do terreno
da Montalegre, para servir pela overlay da LoadProbe (sem `.nefs`). Ver docs/reverse_engineering/track_loading.md.

Regras usadas (medidas no jogo e no exe, 2026-10-06):
- `0x140a90150` lê as células, que são os filhos de `surface`. O prefixo `LAND_` marca paisagem. Os nós de render
  são achados pelo prefixo `SHADOWCASTING_`/`SHADOWCASTINGBATCH_`/`LOW_`/... O limite é de ~800 células.
- O material visível (`terrain_road`, `terrain_wsm_*`) só aparece onde a cópia de profundidade
  (`…BATCH_`, material `batchmaterial`) tem a mesma geometria. Por isso cada célula ganha os dois nós com os
  mesmos triângulos.

Todas as células da Montalegre saem e entram as do Ring. Com `--materials ring` (padrão), o editor é a fonte de
verdade: cada material do Ring vira um `terrain_road.fx` com a textura do próprio Ring (DXT sRGB com mipmaps) e as
UVs dele; as texturas de apoio do shader (normal plana, oclusão branca, mapa de cor neutro) também são geradas. O que
sobra da Montalegre sem referência (texturas, materiais, malhas) sai do arquivo; dela ficam só os moldes dos nós e as
definições dos shaders. Com `--materials host`, volta o mapeamento antigo para materiais da Montalegre.

uso: python3 scripts/research/ring_tracksplit.py <saida.pssg> [--offset dx,dy,dz] [--yaw graus] [--cell 100] [--bg-cell 500]
     [--materials ring|host]
"""
from __future__ import annotations

import argparse
import copy
import io
import json
import math
import struct
import sys
from collections import defaultdict

import numpy as np

sys.path.insert(0, ".")
from tools.egodata.nefs import NefsArchive  # noqa: E402
from tools.egodata.pssg import PSSGFile, PSSGNode  # noqa: E402
from tools.uiview import mesh  # noqa: E402

GAME = "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"
PACK = GAME + "/locations/portugal__montalegre_rallycross.nefs"
PATH = "tracks/locations/portugal/montalegre_rallycross/tracksplit.pssg"
RING = "examples/tracks/synthetic__dr2hook_ring/terrain_0.bin"
LAYOUT = "examples/tracks/synthetic__dr2hook_ring/source/layout.json"
# Largada da Montalegre route_0 (progress_track.xml: cruzamento dos portões 0 e 2, y = asfalto da colisão).
# O carro nasce no grid da Montalegre; se ali não houver asfalto do Ring no mesmo nível, ele nasce dentro do
# chão, a posição vira NaN e a carga trava (ordenação das sondas IBL em 0x1404628a0 nunca termina).
HOST_START = (59.93, 1432.678, -505.38)
HOST_AHEAD = (77.87, 1431.98, -486.73)
ROAD_ABOVE_CENTRELINE = 0.02  # asfalto do Ring acima da linha central do layout (medido na colisão)

# material do Ring -> material da Montalegre (todos com 7 fluxos e passo de 56 bytes)
MATERIALS = {
    "g|synth_asphalt": "S1_main",
    "g|synth_curb": "rumbles_01",
    "g|synth_gravel": "S3_main",
    "g|synth_dirt": "S3_main",
    "g|synth_grass": "main>dense",
}
# ST por metro de cada material (mediana medida no tracksplit da Montalegre); None = canal constante
DENSITY = {
    "S1_main": (0.09, 0.09, 0.07, 0.025),
    "rumbles_01": (0.12, 0.03, 0.12, None),
    "S3_main": (0.09, 0.09, 0.06, 0.025),
    "main>dense": (None, None, 0.7, 0.7),
}
ATLAS = 0.003  # ST1 por metro (atlas de ~300 m, igual para todos)

RING_TEX = "examples/tracks/synthetic__dr2hook_ring/source/textures"
# material do Ring -> (textura do Ring, alfa do canal de brilho; médias das texturas equivalentes da Montalegre)
GROUND = {
    "g|synth_asphalt": ("synth_asphalt_d", 168),
    "g|synth_curb": ("synth_curb_d", 168),
    "g|synth_gravel": ("synth_gravel_d", 200),
    "g|synth_dirt": ("synth_dirt_d", 220),
    "g|synth_grass": ("synth_grass_d", 242),
}
# texturas de apoio do terrain_road.fx: (cor RGBA, formato). Valores medidos nas da Montalegre: normal plana
# (128,127,200), oclusão branca, mapa de cor cinza ~131 (só tinge), máscara de chuva (185,0,0), blend ~152.
SUPPORT = {
    "dr2hook_flat_n": ((128, 127, 200, 255), "ui8x4"),  # sem compressão: o DXT1 entorta a normal
    "dr2hook_white_a": ((255, 255, 255, 255), "dxt1"),
    "dr2hook_grey_cm": ((131, 131, 131, 255), "dxt1_srgb"),
    "dr2hook_wet": ((185, 0, 0, 255), "dxt5"),
    "dr2hook_blend_b": ((152, 152, 152, 255), "dxt1"),
}
# parâmetro do terrain_road.fx -> textura (TDiffuseSpecMap1/2/3 = a do Ring; o resto de apoio)
ROAD_SLOTS = {47: "diffuse", 48: "dr2hook_flat_n", 49: "diffuse", 50: "dr2hook_flat_n", 51: "dr2hook_blend_b",
              52: "diffuse", 53: "dr2hook_flat_n", 54: "dr2hook_blend_b", 58: "dr2hook_white_a",
              59: "dr2hook_grey_cm", 60: "dr2hook_wet"}


def dxt_mips(img, fmt: str) -> tuple[bytes, int]:
    """Cadeia de mipmaps em DXT1/DXT5 (Pillow) até 4 px; devolve (dados, numberMipMapLevels = níveis - 1)."""
    from PIL import Image

    out = bytearray()
    w, h = img.size
    levels = 0
    while True:
        buf = io.BytesIO()
        (img if (w, h) == img.size else img.resize((w, h), Image.LANCZOS)).save(buf, "DDS", pixel_format=fmt)
        out += buf.getvalue()[128:]
        levels += 1
        if min(w, h) <= 4:
            return bytes(out), levels - 1
        w, h = w // 2, h // 2


def rotate_y(pos: np.ndarray, yaw: float) -> np.ndarray:
    """Gira em torno de Y; `yaw` (graus) soma ao ângulo atan2(z, x)."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    out = pos.copy()
    out[:, 0] = c * pos[:, 0] - s * pos[:, 2]
    out[:, 2] = s * pos[:, 0] + c * pos[:, 2]
    return out


def start_transform() -> tuple[float, np.ndarray]:
    """(yaw, deslocamento) que põem a reta de largada do Ring (s=0 do layout) sobre a da Montalegre."""
    s0 = json.load(open(LAYOUT))["samples"][0]
    t = s0["t"]
    yaw = math.degrees(math.atan2(HOST_AHEAD[2] - HOST_START[2], HOST_AHEAD[0] - HOST_START[0])
                       - math.atan2(t[1], t[0]))
    p = rotate_y(np.array([s0["p"]], dtype=np.float64), yaw)[0]
    return yaw, np.array(HOST_START) - p - (0, ROAD_ABOVE_CENTRELINE, 0)


def ring_triangles(offset, yaw: float = 0.0):
    """Triângulos do Ring por material: lista de (posições Nx3, índices Mx3, normais Nx3, fundo?, UVs Nx2).

    As posições são giradas por `yaw` (graus, em torno da origem do Ring) e depois somadas a `offset`."""
    meshes = mesh.unpack_geom(open(RING, "rb").read())
    fg = [m for m in meshes if len(m["positions"]) < 60000]
    xs = [p[0] for m in fg for p in m["positions"]]
    zs = [p[2] for m in fg for p in m["positions"]]
    box = (min(xs), max(xs), min(zs), max(zs))
    out = defaultdict(list)
    for m in meshes:
        raw = np.asarray(m["positions"], dtype=np.float64)
        pos = rotate_y(raw, yaw) + offset
        idx = np.asarray(m["indices"], dtype=np.int64).reshape(-1, 3)
        if len(m["positions"]) >= 60000:  # fundo: tira o que fica sob os tiles do primeiro plano
            c = (raw[idx[:, 0]] + raw[idx[:, 1]] + raw[idx[:, 2]]) / 3
            inside = (c[:, 0] >= box[0]) & (c[:, 0] <= box[1]) & (c[:, 2] >= box[2]) & (c[:, 2] <= box[3])
            idx = idx[~inside]
        # o jogo descarta a face de trás: todo triângulo do chão tem de olhar para cima, como os do terreno.
        # As faixas do synthtrack (asfalto, zebras, brita) saem com metade ou todos virados para baixo; o
        # viewer desenha os dois lados e não mostra isso.
        fy = np.cross(raw[idx[:, 1]] - raw[idx[:, 0]], raw[idx[:, 2]] - raw[idx[:, 0]])[:, 1]
        idx = idx.copy()
        idx[fy < 0] = idx[fy < 0][:, [0, 2, 1]]
        nrm = np.zeros_like(pos)
        fn = np.cross(pos[idx[:, 1]] - pos[idx[:, 0]], pos[idx[:, 2]] - pos[idx[:, 0]])
        for k in range(3):
            np.add.at(nrm, idx[:, k], fn)
        # o Ring é CCW visto de cima no plano XZ; a normal tem de apontar para +Y
        if nrm[:, 1].sum() < 0:
            nrm = -nrm
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
        uv = np.asarray(m["uvs"], dtype=np.float64).reshape(-1, 2)
        out[m["material"]].append((pos, idx, nrm, len(m["positions"]) >= 60000, uv))
    return out


def half_be(a: np.ndarray) -> np.ndarray:
    return a.astype(">f2")


class Builder:
    def __init__(self, split: PSSGFile):
        self.p = split
        self.byid = {n.id: n for n in split.find_nodes(lambda n: n.id is not None)}
        self.libs = {n.get("type"): n for n in split.find_by_type("LIBRARY")}
        self.serial = 0
        cell = self.byid["ROOT_4_9"]
        self.t_cell = cell
        self.t_vis = self.byid["SHADOWCASTING_4_9"]
        self.t_bat = self.byid["SHADOWCASTINGBATCH_4_9"]
        self.t_low = self.byid["LOW_4_9"]
        self.t_lowbat = self.byid["LOWBATCH_4_9"]
        low_inst = next(c for c in self.t_low.children if c.type_name == "RENDERSTREAMINSTANCE")
        self.t_low_ds = self.byid[low_inst.get("indices").lstrip("#")]
        # dois blocos: posição+cor e ST/normal/tangente/binormal
        self.t_low_dbs = list(dict.fromkeys(self.byid[rs.get("dataBlock").lstrip("#")] for rs in self.t_low_ds.children[1:]))
        vis_inst = next(c for c in self.t_vis.children if c.type_name == "RENDERSTREAMINSTANCE")
        bat_inst = next(c for c in self.t_bat.children if c.type_name == "RENDERSTREAMINSTANCE")
        self.t_inst = vis_inst
        self.t_vis_ds = self.byid[vis_inst.get("indices").lstrip("#")]
        self.t_bat_ds = self.byid[bat_inst.get("indices").lstrip("#")]
        self.t_vis_db = self.byid[self.t_vis_ds.children[1].get("dataBlock").lstrip("#")]
        self.t_bat_db = self.byid[self.t_bat_ds.children[1].get("dataBlock").lstrip("#")]
        self.t_segset = next(n for n in self.libs["SEGMENTSET"].children if n.type_name == "SEGMENTSET")

    def texture(self, tex_id: str, img, fmt: str) -> str:
        """TEXTURE nova em RENDERINTERFACEBOUND (molde: uma do terreno da Montalegre); devolve o id."""
        if fmt == "ui8x4":  # BGRA na memória, mipmaps até 4 px
            from PIL import Image

            data, mips, w = b"", -1, img.size[0]
            while w >= 4:
                r, g, b_, a_ = img.resize((w, w * img.size[1] // img.size[0]), Image.LANCZOS).split()
                data += Image.merge("RGBA", (b_, g, r, a_)).tobytes()
                mips, w = mips + 1, w // 2
        else:
            data, mips = dxt_mips(img, "DXT5" if fmt.startswith("dxt5") else "DXT1")
        t = self.clone(self.byid["mnt_section_4_col_cm.tga"])
        for k, v in (("id", tex_id), ("width", img.size[0]), ("height", img.size[1]), ("texelFormat", fmt),
                     ("numberMipMapLevels", mips)):
            PSSGFile.set_attr(t, k, v)
        block = t.children[0]
        PSSGFile.set_attr(block, "size", len(data))
        self.set_data(block.children[0], data)
        self.libs["RENDERINTERFACEBOUND"].children.append(t)
        return tex_id

    def road_material(self, mat_id: str, diffuse: str) -> str:
        """SHADERINSTANCE `terrain_road.fx` (molde: S1_main) com as texturas do Ring e as de apoio."""
        si = self.clone(self.byid["S1_main"])
        PSSGFile.set_attr(si, "id", mat_id)
        for c in si.children:
            if c.get("type") == "texture":
                slot = ROAD_SLOTS[int(c.get("parameterID"))]
                PSSGFile.set_attr(c, "texture", "#" + (diffuse if slot == "diffuse" else slot))
        self.libs["SHADERINSTANCE"].children.append(si)
        return mat_id

    def lod_material(self, mat_id: str, diffuse: str) -> str:
        """SHADERINSTANCE `terrain_lod.fx` (molde: mat_low_section_04), a dos nós LOW_ (300 a 800 m)."""
        si = self.clone(self.byid["mat_low_section_04"])
        PSSGFile.set_attr(si, "id", mat_id)
        for c in si.children:
            if c.get("type") == "texture":
                PSSGFile.set_attr(c, "texture", "#" + (diffuse if c.get("parameterID") in (11, "11") else "dr2hook_white_a"))
        self.libs["SHADERINSTANCE"].children.append(si)
        return mat_id

    def prune(self, root: PSSGNode) -> dict[str, int]:
        """Tira das LIBRARYs o que não é alcançável a partir de `root` (referências `#id` nos atributos)."""
        def ids(n):
            out = []
            stack = [n]
            while stack:
                x = stack.pop()
                stack.extend(x.children)
                if x.id is not None:
                    out.append(x.id)
            return out

        def refs(n):
            out = set()
            stack = [n]
            while stack:
                x = stack.pop()
                stack.extend(x.children)
                for a in x.attributes.values():
                    v = a.value
                    if isinstance(v, str) and v.startswith("#"):
                        out.add(v[1:])
            return out

        wanted = refs(root)
        entries = [(lib, c, set(ids(c))) for lib in self.libs.values() for c in lib.children]
        keep: set[int] = set()
        changed = True
        while changed:
            changed = False
            for k, (lib, c, cid) in enumerate(entries):
                if k not in keep and (cid & wanted or lib.get("type") in ("SHADERGROUP", "NODE")):
                    keep.add(k)
                    wanted |= refs(c)
                    changed = True
        kept = {id(entries[k][1]) for k in keep}
        removed: dict[str, int] = defaultdict(int)
        for lib in self.libs.values():
            before = list(lib.children)
            lib.children = [c for c in before if id(c) in kept]
            removed[lib.get("type")] += len(before) - len(lib.children)
        return dict(removed)

    def new_id(self) -> str:
        self.serial += 1
        return f"ring{self.serial}"

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

    def mesh_source(self, template_ds, template_db, vertex_bytes, count: int, indices: np.ndarray) -> str:
        """Cria DATABLOCKs (+ dados) e SEGMENTSET/RENDERDATASOURCE; devolve o id do data source.

        `template_db`/`vertex_bytes` podem ser listas paralelas (um DATABLOCK por bloco do molde)."""
        if not isinstance(template_db, list):
            template_db, vertex_bytes = [template_db], [vertex_bytes]
        new_ids = {}
        for tdb, vb in zip(template_db, vertex_bytes):
            db = self.clone(tdb)
            new_ids[tdb.id] = self.new_id()
            PSSGFile.set_attr(db, "id", new_ids[tdb.id])
            PSSGFile.set_attr(db, "size", len(vb))
            PSSGFile.set_attr(db, "elementCount", count)
            self.set_data(next(c for c in db.children if c.type_name == "DATABLOCKDATA"), vb)
            self.libs["RENDERINTERFACEBOUND"].children.append(db)

        ds = self.clone(template_ds)
        ds_id = self.new_id()
        PSSGFile.set_attr(ds, "id", ds_id)
        ix = ds.children[0]
        PSSGFile.set_attr(ix, "id", self.new_id())
        PSSGFile.set_attr(ix, "count", int(indices.size))
        PSSGFile.set_attr(ix, "maximumIndex", int(indices.max()))
        self.set_data(ix.children[0], indices.astype(">u2").tobytes())
        for k, rs in enumerate(ds.children[1:]):
            old = rs.get("dataBlock").lstrip("#")
            PSSGFile.set_attr(rs, "dataBlock", "#" + new_ids.get(old, next(iter(new_ids.values()))))
            PSSGFile.set_attr(rs, "id", f"{ds_id}_{k}")
        seg = self.clone(self.t_segset, keep_children=False)
        PSSGFile.set_attr(seg, "id", self.new_id())
        PSSGFile.set_attr(seg, "segmentCount", 1)
        seg.children = [ds]
        self.libs["SEGMENTSET"].children.append(seg)
        return ds_id

    def instance(self, ds_id: str, shader: str) -> PSSGNode:
        inst = self.clone(self.t_inst)
        PSSGFile.set_attr(inst, "id", self.new_id())
        PSSGFile.set_attr(inst, "indices", "#" + ds_id)
        PSSGFile.set_attr(inst, "shader", "#" + shader)
        PSSGFile.set_attr(inst.children[0], "source", "#" + ds_id)
        return inst

    def render_node(self, template, name: str, bbox, instances) -> PSSGNode:
        rn = self.clone(template, keep_children=False)
        PSSGFile.set_attr(rn, "id", name)
        PSSGFile.set_attr(rn, "nickname", name)
        tr, bb = (self.clone(c) for c in template.children[:2])
        self.set_data(bb, struct.pack(">6f", *bbox))
        rn.children = [tr, bb] + instances
        return rn

    def cell(self, name: str, render_nodes) -> PSSGNode:
        c = self.clone(self.t_cell, keep_children=False)
        PSSGFile.set_attr(c, "id", name)
        if "nickname" in c.attributes:
            PSSGFile.set_attr(c, "nickname", name)
        c.children = [self.clone(ch) for ch in self.t_cell.children[:2]] + render_nodes
        return c


def visible_vertices(host_mat, pos, nrm, cell_min, host_const, uv=None):
    """Bytes big-endian no formato de 56 bytes: posição, cor, ST0, ST1, normal, tangente, binormal.

    Com `uv` (materiais do Ring), ST0 = (u, v, u, v) do Ring; senão, densidade por metro do material da Montalegre."""
    n = len(pos)
    rec = np.zeros(n, dtype=np.dtype([("p", ">f4", 3), ("c", "u1", 4), ("st0", ">f2", 4), ("st1", ">f2", 4),
                                      ("n", ">f2", 4), ("t", ">f2", 4), ("b", ">f2", 4)]))
    rec["p"] = pos
    rec["c"] = host_const["color"]
    st0 = np.zeros((n, 4))
    if uv is not None:
        # desconta um inteiro por bloco: a textura repete, e o half fica com números pequenos
        local = uv - np.floor(uv.min(axis=0))
        st0[:, 0:2] = local
        st0[:, 2:4] = local
    else:
        rel = pos - cell_min
        for k, d in enumerate(DENSITY[host_mat]):
            if d is None:
                st0[:, k] = host_const["st0"][k]
            else:
                axis = 0 if k % 2 == 0 else 2
                st0[:, k] = rel[:, axis] * d + (cell_min[axis] * d) % 1.0
    rec["st0"] = st0
    u = np.clip((pos[:, 0] - host_const["atlas_min"][0]) * ATLAS, 0.0, 1.0)
    v = np.clip((pos[:, 2] - host_const["atlas_min"][1]) * ATLAS, 0.0, 1.0)
    rec["st1"] = np.stack([u, v, u, 1.0 - v], axis=1)
    rec["n"] = np.c_[nrm, np.ones(n)]
    t = np.cross(nrm, np.array([0.0, 0.0, 1.0]))  # tangente ao longo de +X na superfície (ST0.x cresce com x)
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
    b = np.cross(t, nrm)  # convenção medida no jogo: B = T x N
    rec["t"] = np.c_[t, np.ones(n)]
    rec["b"] = np.c_[b, np.ones(n)]
    return rec.tobytes()


def lod_vertices(pos, nrm, uv, color=(0, 0, 0, 128)):
    """Vértices dos nós LOW_ (`terrain_lod.fx`): bloco 1 = posição + cor, bloco 2 = ST/normal/tangente/binormal."""
    n = len(pos)
    a = np.zeros(n, dtype=np.dtype([("p", ">f4", 3), ("c", "u1", 4)]))
    a["p"] = pos
    a["c"] = color
    b = np.zeros(n, dtype=np.dtype([("st", ">f2", 4), ("n", ">f2", 4), ("t", ">f2", 4), ("b", ">f2", 4)]))
    local = uv - np.floor(uv.min(axis=0))
    b["st"] = np.c_[local, local]
    b["n"] = np.c_[nrm, np.ones(n)]
    t = np.cross(nrm, np.array([0.0, 0.0, 1.0]))
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
    b["t"] = np.c_[t, np.ones(n)]
    b["b"] = np.c_[np.cross(t, nrm), np.ones(n)]
    return [a.tobytes(), b.tobytes()]


def batch_vertices(pos, color):
    n = len(pos)
    rec = np.zeros(n, dtype=np.dtype([("p", ">f4", 3), ("c", "u1", 4), ("st", ">f2", 4)]))
    rec["p"] = pos
    rec["c"] = color
    rec["st"] = np.array([0.0, 0.0, 0.0, 1.0])
    return rec.tobytes()


def host_constants(split: PSSGFile, data: bytes):
    """Cor mediana e ST0 constante (mediana) de cada material escolhido, medidos no tracksplit da Montalegre."""
    from tools.uiview.track.terrain_patch import locate_meshes
    out = {}
    for m in locate_meshes(split):
        base = m.material.split("!")[0]
        if base not in DENSITY:
            continue
        cs = m.stream("Color")
        st = [s for s in m.streams if s.kind == "ST"][0]
        col = np.frombuffer(data, dtype=np.dtype([("c", "u1", 4), ("x", f"V{cs.stride - 4}")]), count=cs.count,
                            offset=cs.start)["c"]
        st0 = np.frombuffer(data, dtype=np.dtype([("s", ">f2", 4), ("x", f"V{st.stride - 8}")]), count=st.count,
                            offset=st.start)["s"].astype(np.float64)
        e = out.setdefault(base, {"color": [], "st0": []})
        e["color"].append(col)
        e["st0"].append(st0)
    for base, e in out.items():
        e["color"] = np.median(np.concatenate(e["color"]), axis=0).astype(np.uint8)
        e["st0"] = np.median(np.concatenate(e["st0"]), axis=0)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--offset", default="", help="dx,dy,dz somados ao Ring depois do giro (padrão: largada sobre a da Montalegre)")
    ap.add_argument("--yaw", type=float, default=None, help="giro do Ring em graus (padrão: o da largada)")
    ap.add_argument("--cell", type=float, default=100.0, help="lado da célula do primeiro plano (m)")
    ap.add_argument("--bg-cell", type=float, default=500.0, help="lado da célula do fundo (m)")
    ap.add_argument("--layout", choices=("root", "land"), default="root",
                    help="root: células ROOT com SHADOWCASTING_ (só perto da câmera); land: células LAND_ com NONLOD_")
    ap.add_argument("--far", choices=("none", "low", "land"), default="land",
                    help="low: repete a malha nos nós LOW_/LOWBATCH_ (o motor troca SHADOWCASTING_ por LOW_ de 300 a 800 m); "
                         "land: além disso, as células do fundo viram LAND_ com NONLOD_ (desenhadas além de 800 m)")
    ap.add_argument("--far-pad", type=float, default=2500.0,
                    help="metros somados à caixa das células ROOT: o motor mede a distância até a caixa, então elas "
                         "ficam com o detalhe por mais longe (perto < 290 m, longe < 800 m, contados da caixa)")
    ap.add_argument("--host", default="", help="tracksplit.pssg da Montalegre já extraído (senão lê do .nefs)")
    ap.add_argument("--materials", choices=("ring", "host"), default="ring",
                    help="ring: texturas e UVs do Ring (padrão); host: materiais da Montalegre")
    a = ap.parse_args()
    yaw, offset = start_transform()
    if a.yaw is not None:
        yaw = a.yaw
    if a.offset:
        offset = np.array([float(v) for v in a.offset.split(",")])
    print(f"Ring: giro {yaw:.3f}°, deslocamento {offset.round(3).tolist()}")

    data = open(a.host, "rb").read() if a.host else NefsArchive.open_path(PACK).read(PATH)
    split = PSSGFile(data)
    consts = host_constants(split, data)
    b = Builder(split)

    tris = ring_triangles(offset, yaw)
    allpos = np.concatenate([pos for lst in tris.values() for pos, *_ in lst])
    atlas_min = (allpos[:, 0].min(), allpos[:, 2].min())
    for c in consts.values():
        c["atlas_min"] = atlas_min

    # material de cada material do Ring (id da SHADERINSTANCE) e as constantes de vértice dele
    if a.materials == "ring":
        from PIL import Image

        for tex_id, (rgba, fmt) in SUPPORT.items():
            b.texture(tex_id, Image.new("RGBA", (16, 16), rgba), fmt)
        mat_of, const_of, lod_of = {}, {}, {}
        for ring_mat, (tex, gloss) in GROUND.items():
            img = Image.open(f"{RING_TEX}/{tex}.png").convert("RGB")
            img.putalpha(gloss)
            name = "dr2hook_" + ring_mat.split("synth_", 1)[1]
            tex_id = b.texture(f"{tex}.tga", img, "dxt5_srgb")
            mat_of[ring_mat] = b.road_material(name, tex_id)
            lod_of[mat_of[ring_mat]] = b.lod_material(name + "_lod", tex_id)
            const_of[mat_of[ring_mat]] = consts["S1_main"]
    else:
        mat_of = MATERIALS
        const_of = consts

    # agrupa por (célula, material): triângulos com vértices reindexados por célula
    groups = defaultdict(lambda: {"pos": [], "nrm": [], "idx": [], "uv": []})
    for ring_mat, lst in tris.items():
        host_mat = mat_of[ring_mat]
        for pos, idx, nrm, is_bg, uv in lst:
            size = a.bg_cell if is_bg else a.cell
            cen = (pos[idx[:, 0]] + pos[idx[:, 1]] + pos[idx[:, 2]]) / 3
            ci = np.floor(cen[:, 0] / size).astype(int)
            cj = np.floor(cen[:, 2] / size).astype(int)
            for key in set(zip(ci.tolist(), cj.tolist())):
                sel = idx[(ci == key[0]) & (cj == key[1])]
                used, local = np.unique(sel, return_inverse=True)
                g = groups[(("B" if is_bg else "F"), key, host_mat)]
                base = sum(len(x) for x in g["pos"])
                g["pos"].append(pos[used])
                g["nrm"].append(nrm[used])
                g["uv"].append(uv[used])
                g["idx"].append(local.reshape(-1, 3) + base)

    cells = defaultdict(list)
    for (kind, key, host_mat), g in groups.items():
        cells[(kind, key)].append((host_mat, np.concatenate(g["pos"]), np.concatenate(g["nrm"]),
                                   np.concatenate(g["idx"]), np.concatenate(g["uv"])))

    surface = b.byid["surface"]
    keep = [c for c in surface.children if c.type_name not in ("NODE", "RENDERNODE")]  # TRANSFORM/BOUNDINGBOX
    old = len(surface.children) - len(keep)
    surface.children = list(keep)
    nverts = ntris = 0
    for (kind, (i, j)), parts in sorted(cells.items()):
        name = f"RING{kind}_{i}_{j}"
        cell_name = f"LAND_{name}" if a.layout == "land" else name
        vis_prefix, bat_prefix = ("NONLOD_", "NONLODBATCH_") if a.layout == "land" else ("SHADOWCASTING_", "SHADOWCASTINGBATCH_")
        vis_inst, bat_pos, bat_idx = [], [], []
        lo = np.full(3, np.inf)
        hi = np.full(3, -np.inf)
        for host_mat, pos, nrm, idx, uv in parts:
            if len(pos) > 65535:
                raise SystemExit(f"{name}/{host_mat}: {len(pos)} vértices (máximo 65535); diminua a célula")
            cell_min = pos.min(axis=0)
            vb = visible_vertices(host_mat, pos, nrm, cell_min, const_of[host_mat],
                                  uv if a.materials == "ring" else None)
            ds = b.mesh_source(b.t_vis_ds, b.t_vis_db, vb, len(pos), idx.reshape(-1))
            vis_inst.append(b.instance(ds, host_mat))
            bat_idx.append(idx + sum(len(x) for x in bat_pos))
            bat_pos.append(pos)
            lo = np.minimum(lo, pos.min(axis=0))
            hi = np.maximum(hi, pos.max(axis=0))
            nverts += len(pos)
            ntris += len(idx)
        bp = np.concatenate(bat_pos)
        bi = np.concatenate(bat_idx)
        if len(bp) > 65535:
            raise SystemExit(f"{name}: lote de profundidade com {len(bp)} vértices; diminua a célula")
        bds = b.mesh_source(b.t_bat_ds, b.t_bat_db, batch_vertices(bp, const_of[parts[0][0]]["color"]), len(bp),
                            bi.reshape(-1))
        bbox = (*lo, *hi)
        if kind == "F" and a.layout == "root":
            bbox = (*(lo - a.far_pad), *(hi + a.far_pad))
        nodes = [b.render_node(b.t_bat, f"{bat_prefix}{name}", bbox, [b.instance(bds, "batchmaterial")]),
                 b.render_node(b.t_vis, f"{vis_prefix}{name}", bbox, vis_inst)]
        if a.far == "land" and a.layout == "root" and kind == "B":
            # paisagem: um nó só, com o material de longe (o motor não esconde LAND_ pela distância)
            cell_name = f"LAND_{name}"
            low = [b.instance(b.mesh_source(b.t_low_ds, b.t_low_dbs, lod_vertices(pos, nrm, uv), len(pos), idx.reshape(-1)),
                              lod_of[host_mat]) for host_mat, pos, nrm, idx, uv in parts]
            nodes = [b.render_node(b.t_bat, f"NONLODBATCH_{name}", bbox, [b.instance(bds, "batchmaterial")]),
                     b.render_node(b.t_vis, f"NONLOD_{name}", bbox, low)]
        elif a.far in ("low", "land") and a.layout == "root":
            if a.materials != "ring":
                raise SystemExit("--far low precisa de --materials ring")
            low = [b.instance(b.mesh_source(b.t_low_ds, b.t_low_dbs, lod_vertices(pos, nrm, uv), len(pos), idx.reshape(-1)),
                              lod_of[host_mat]) for host_mat, pos, nrm, idx, uv in parts]
            nodes += [b.render_node(b.t_lowbat, f"LOWBATCH_{name}", bbox, [b.instance(bds, "batchmaterial")]),
                      b.render_node(b.t_low, f"LOW_{name}", bbox, low)]
        surface.children.append(b.cell(cell_name, nodes))
    print(f"células: {old} da Montalegre -> {len(surface.children) - len(keep)} do Ring; {nverts} vértices, {ntris} triângulos")
    if a.materials == "ring":
        removed = b.prune(b.byid["Scene Root"])
        print("sem uso, retirados da Montalegre: " + ", ".join(f"{k} {v}" for k, v in removed.items() if v))
    split.file_size = 0  # recalcula o tamanho declarado no cabeçalho
    out = split.serialize()
    with open(a.out, "wb") as f:
        f.write(out)
    PSSGFile(out)  # relê para conferir a estrutura
    print(f"gravado {a.out} ({len(out) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
