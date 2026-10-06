"""Gera um `tracksplit.pssg` com o terreno do Ring (examples/tracks/synthetic__dr2hook_ring) no lugar do terreno
da Montalegre, para servir pela overlay da LoadProbe (sem `.nefs`). Ver docs/reverse_engineering/track_loading.md.

Regras usadas (medidas no jogo e no exe, 2026-10-06):
- `0x140a90150` lê as células, que são os filhos de `surface`. O prefixo `LAND_` marca paisagem. Os nós de render
  são achados pelo prefixo `SHADOWCASTING_`/`SHADOWCASTINGBATCH_`/`LOW_`/... O limite é de ~800 células.
- O material visível (`terrain_road`, `terrain_wsm_*`) só aparece onde a cópia de profundidade
  (`…BATCH_`, material `batchmaterial`) tem a mesma geometria. Por isso cada célula ganha os dois nós com os
  mesmos triângulos.

Todas as células da Montalegre saem e entram as do Ring. Os materiais (texturas) são da Montalegre. As bibliotecas
antigas (segment sets e datablocks sem uso) ficam no arquivo.

uso: python3 scripts/research/ring_tracksplit.py <saida.pssg> [--offset dx,dy,dz] [--cell 100] [--bg-cell 500]
"""
from __future__ import annotations

import argparse
import copy
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


def ring_triangles(offset):
    """Triângulos do Ring por material: lista de (posições Nx3, índices Mx3, normais Nx3)."""
    meshes = mesh.unpack_geom(open(RING, "rb").read())
    fg = [m for m in meshes if len(m["positions"]) < 60000]
    xs = [p[0] for m in fg for p in m["positions"]]
    zs = [p[2] for m in fg for p in m["positions"]]
    box = (min(xs), max(xs), min(zs), max(zs))
    out = defaultdict(list)
    for m in meshes:
        pos = np.asarray(m["positions"], dtype=np.float64) + offset
        idx = np.asarray(m["indices"], dtype=np.int64).reshape(-1, 3)
        if len(m["positions"]) >= 60000:  # fundo: tira o que fica sob os tiles do primeiro plano
            c = (pos[idx[:, 0]] + pos[idx[:, 1]] + pos[idx[:, 2]]) / 3 - offset
            inside = (c[:, 0] >= box[0]) & (c[:, 0] <= box[1]) & (c[:, 2] >= box[2]) & (c[:, 2] <= box[3])
            idx = idx[~inside]
        nrm = np.zeros_like(pos)
        fn = np.cross(pos[idx[:, 1]] - pos[idx[:, 0]], pos[idx[:, 2]] - pos[idx[:, 0]])
        for k in range(3):
            np.add.at(nrm, idx[:, k], fn)
        # o Ring é CCW visto de cima no plano XZ; a normal tem de apontar para +Y
        if nrm[:, 1].sum() < 0:
            nrm = -nrm
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
        out[m["material"]].append((pos, idx, nrm, len(m["positions"]) >= 60000))
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
        vis_inst = next(c for c in self.t_vis.children if c.type_name == "RENDERSTREAMINSTANCE")
        bat_inst = next(c for c in self.t_bat.children if c.type_name == "RENDERSTREAMINSTANCE")
        self.t_inst = vis_inst
        self.t_vis_ds = self.byid[vis_inst.get("indices").lstrip("#")]
        self.t_bat_ds = self.byid[bat_inst.get("indices").lstrip("#")]
        self.t_vis_db = self.byid[self.t_vis_ds.children[1].get("dataBlock").lstrip("#")]
        self.t_bat_db = self.byid[self.t_bat_ds.children[1].get("dataBlock").lstrip("#")]
        self.t_segset = next(n for n in self.libs["SEGMENTSET"].children if n.type_name == "SEGMENTSET")

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

    def mesh_source(self, template_ds, template_db, vertex_bytes: bytes, count: int, indices: np.ndarray) -> str:
        """Cria DATABLOCK (+ dados) e SEGMENTSET/RENDERDATASOURCE; devolve o id do data source."""
        db = self.clone(template_db)
        db_id = self.new_id()
        PSSGFile.set_attr(db, "id", db_id)
        PSSGFile.set_attr(db, "size", len(vertex_bytes))
        PSSGFile.set_attr(db, "elementCount", count)
        self.set_data(next(c for c in db.children if c.type_name == "DATABLOCKDATA"), vertex_bytes)
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
            PSSGFile.set_attr(rs, "dataBlock", "#" + db_id)
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


def visible_vertices(host_mat, pos, nrm, cell_min, host_const):
    """Bytes big-endian no formato de 56 bytes: posição, cor, ST0, ST1, normal, tangente, binormal."""
    n = len(pos)
    rec = np.zeros(n, dtype=np.dtype([("p", ">f4", 3), ("c", "u1", 4), ("st0", ">f2", 4), ("st1", ">f2", 4),
                                      ("n", ">f2", 4), ("t", ">f2", 4), ("b", ">f2", 4)]))
    rec["p"] = pos
    rec["c"] = host_const["color"]
    dens = DENSITY[host_mat]
    rel = pos - cell_min
    st0 = np.zeros((n, 4))
    for k, d in enumerate(dens):
        if d is None:
            st0[:, k] = host_const["st0"][k]
        else:
            axis = 0 if k % 2 == 0 else 2
            # desconta um inteiro por célula: a textura repete, e o half fica com números pequenos
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
    ap.add_argument("--offset", default="8,1334,-290", help="dx,dy,dz somados ao Ring (largada sobre a da Montalegre)")
    ap.add_argument("--cell", type=float, default=100.0, help="lado da célula do primeiro plano (m)")
    ap.add_argument("--bg-cell", type=float, default=500.0, help="lado da célula do fundo (m)")
    ap.add_argument("--layout", choices=("root", "land"), default="root",
                    help="root: células ROOT com SHADOWCASTING_ (só perto da câmera); land: células LAND_ com NONLOD_")
    ap.add_argument("--host", default="", help="tracksplit.pssg da Montalegre já extraído (senão lê do .nefs)")
    a = ap.parse_args()
    offset = np.array([float(v) for v in a.offset.split(",")])

    data = open(a.host, "rb").read() if a.host else NefsArchive.open_path(PACK).read(PATH)
    split = PSSGFile(data)
    consts = host_constants(split, data)
    b = Builder(split)

    tris = ring_triangles(offset)
    allpos = np.concatenate([pos for lst in tris.values() for pos, *_ in lst])
    atlas_min = (allpos[:, 0].min(), allpos[:, 2].min())
    for c in consts.values():
        c["atlas_min"] = atlas_min

    # agrupa por (célula, material do host): triângulos com vértices reindexados por célula
    groups = defaultdict(lambda: {"pos": [], "nrm": [], "idx": []})
    for ring_mat, lst in tris.items():
        host_mat = MATERIALS[ring_mat]
        for pos, idx, nrm, is_bg in lst:
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
                g["idx"].append(local.reshape(-1, 3) + base)

    cells = defaultdict(list)
    for (kind, key, host_mat), g in groups.items():
        cells[(kind, key)].append((host_mat, np.concatenate(g["pos"]), np.concatenate(g["nrm"]),
                                   np.concatenate(g["idx"])))

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
        for host_mat, pos, nrm, idx in parts:
            if len(pos) > 65535:
                raise SystemExit(f"{name}/{host_mat}: {len(pos)} vértices (máximo 65535); diminua a célula")
            cell_min = pos.min(axis=0)
            vb = visible_vertices(host_mat, pos, nrm, cell_min, consts[host_mat])
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
        bds = b.mesh_source(b.t_bat_ds, b.t_bat_db, batch_vertices(bp, consts[parts[0][0]]["color"]), len(bp),
                            bi.reshape(-1))
        bbox = (*lo, *hi)
        surface.children.append(b.cell(cell_name, [
            b.render_node(b.t_bat, f"{bat_prefix}{name}", bbox, [b.instance(bds, "batchmaterial")]),
            b.render_node(b.t_vis, f"{vis_prefix}{name}", bbox, vis_inst),
        ]))
    print(f"células: {old} da Montalegre -> {len(surface.children) - len(keep)} do Ring; {nverts} vértices, {ntris} triângulos")
    split.file_size = 0  # recalcula o tamanho declarado no cabeçalho
    out = split.serialize()
    with open(a.out, "wb") as f:
        f.write(out)
    PSSGFile(out)  # relê para conferir a estrutura
    print(f"gravado {a.out} ({len(out) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
