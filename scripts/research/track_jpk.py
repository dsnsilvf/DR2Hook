"""Lê e gera a colisão de uma rota do DR2 (`route_N/track.jpk`).

Formato (medido nos 480 blocos da Montalegre route_0, 2026-10-06; ver docs/reverse_engineering/track_loading.md §9.11):

- JPAK: "JPAK", u32 0, u32 n, u32 16, u32 0, u32 32+32n, 8 bytes 0; n entradas de 32 bytes (u32 offset do nome,
  tamanho, offset dos dados, tamanho, 16 bytes 0); nomes a partir de 32+32n+1; dados alinhados a 16; o arquivo
  termina alinhado a 16. Entradas: `qt_XZ_XZ_..vcqtc` (um par de bits x,z por nível, a partir da caixa do
  `qt.info`), em profundidade, e `qt.info` (f32 mín xyz, máx xyz) no fim.
- Bloco `.vcqtc` (little-endian): f32 mín xyz, máx xyz (folga de 0,1 m); i32 triângulos, vértices, 8 (materiais);
  u32 offset dos vértices (0x90), dos nós, dos triângulos e das referências; 8 códigos de superfície de 4 bytes
  (os que sobram repetem o último); zeros até 0x60; "VCQT", u32 1, 0, 0; vec4 (mín x, 0, mín z, 1);
  vec4 (largura x, 1, largura z, 1).
  - Vértice: f32 x normalizado, f32 y absoluto, f32 z normalizado, u32 w (0 em metade dos vértices; ainda sem uso
    conhecido, o gerador grava 0).
  - Nó: 2 bytes big-endian. Bit 15 ligado = folha, e o resto é o offset da lista de referências; `8000` = folha
    única com a lista em 0 (35 dos 480 blocos são assim).
  - Triângulo (codificação do Showdown): byte0 = v0 >> 2; byte1 = (v0 & 3) << 6 | arestas << 3 | material;
    byte2 = v1 - v0; byte3 = v2 - v0. v0 é o menor índice. O bit k das arestas vale 1 quando a aresta
    (vk, vk+1) tem um vizinho e a dobra entre os dois fica abaixo de ~55°; na borda da malha vale 0.
  - Lista de referências: u16 big-endian do 1º triângulo, depois deltas de um byte (254 = soma sem incluir) e 0xFF.

Gerador: quadtree em XZ a partir da caixa de tudo (a raiz sempre se divide uma vez, como no DiRTbench); o triângulo
entra em toda célula que toca (SAT em XZ); a célula vira bloco quando cabe no limite dos originais (Montalegre: até
1084 triângulos e 591 vértices). Cada bloco é uma folha única.

O particionamento segue o DiRTbench (https://github.com/ItsNotPaths/DiRTbench, MIT, src/d3/dirt3_writer.odin), que
por sua vez vem do Ego-Engine-Modding (MIT). O conteúdo do bloco é o formato do DR2, medido aqui.

uso:
  python3 scripts/research/track_jpk.py info <track.jpk>
  python3 scripts/research/track_jpk.py roundtrip <track.jpk>       # decodifica, regrava e compara
  python3 scripts/research/track_jpk.py ring <saida.jpk>            # colisão do Ring (examples/tracks/synthetic__dr2hook_ring)
"""
from __future__ import annotations

import argparse
import struct
import sys
from collections import defaultdict

import numpy as np

MAX_TRIS = 1000
MAX_VERTS = 580
EDGE_ANGLE = 55.0  # graus

# material do Ring -> código de superfície do DR2 (surface_materials.xml em game_1.dat)
RING_SURFACES = {
    "g|synth_asphalt": b"TS0+",  # SMOOTHDRYTAR_RX
    "g|synth_curb": b"RRM+",  # RUMBLESTRIP_RIDGED_MED
    "g|synth_gravel": b"DB2+",  # GRAV_RX
    "g|synth_dirt": b"DR2+",  # DIRT_RX
    "g|synth_grass": b"GR1+",  # GRASS_RX
}


# --- JPAK --------------------------------------------------------------------------------------------------------

def read_jpak(d: bytes) -> list[tuple[str, bytes]]:
    assert d[:4] == b"JPAK", d[:4]
    n = struct.unpack_from("<I", d, 8)[0]
    out = []
    for i in range(n):
        no, size, off, _ = struct.unpack_from("<4I", d, 32 + 32 * i)
        out.append((d[no:d.index(b"\0", no)].decode(), d[off:off + size]))
    return out


def align16(n: int) -> int:
    return (n + 15) & ~15


def write_jpak(entries: list[tuple[str, bytes]]) -> bytes:
    n = len(entries)
    names = bytearray(b"\0")
    name_off = []
    for name, _ in entries:
        name_off.append(32 + 32 * n + len(names))
        names += name.encode() + b"\0"
    data_at = align16(32 + 32 * n + len(names))
    out = bytearray(b"JPAK" + struct.pack("<5I", 0, n, 16, 0, 32 + 32 * n) + bytes(8))
    body = bytearray()
    for (name, data), no in zip(entries, name_off):
        out += struct.pack("<4I", no, len(data), data_at + len(body), len(data)) + bytes(16)
        body += data + bytes(align16(len(data)) - len(data))
    out += names + bytes(data_at - 32 - 32 * n - len(names))
    return bytes(out + body)


# --- bloco .vcqtc ------------------------------------------------------------------------------------------------

def decode_tile(d: bytes):
    """(posições k×3×3, códigos de material k, bits de aresta k)."""
    bmin = struct.unpack_from("<3f", d, 0)
    bmax = struct.unpack_from("<3f", d, 12)
    nt, nv, nm, vo, no, to, ro = struct.unpack_from("<3i4I", d, 24)
    mats = [d[0x34 + 4 * i:0x38 + 4 * i] for i in range(8)]
    v = np.frombuffer(d, dtype="<f4", count=4 * nv, offset=vo).reshape(nv, 4).astype(np.float64)
    pos = np.stack([bmin[0] + v[:, 0] * (bmax[0] - bmin[0]), v[:, 1], bmin[2] + v[:, 2] * (bmax[2] - bmin[2])], 1)
    t = np.frombuffer(d, dtype=np.uint8, count=4 * nt, offset=to).reshape(nt, 4).astype(np.int64)
    v0 = (t[:, 0] << 2) | (t[:, 1] >> 6)
    tris = pos[np.stack([v0, v0 + t[:, 2], v0 + t[:, 3]], 1)]
    return tris, [mats[m] for m in t[:, 1] & 7], (t[:, 1] >> 3) & 7


def _order_vertices(tris: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Solda os vértices e numera por Cuthill-McKee reverso, que deixa os vértices de cada triângulo próximos."""
    key = np.round(tris.reshape(-1, 3) * 1000).astype(np.int64)
    uniq, first_at, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inv = inv.reshape(-1, 3)
    nbr = [set() for _ in range(len(uniq))]
    for a, b, c in inv:
        nbr[a] |= {b, c}
        nbr[b] |= {a, c}
        nbr[c] |= {a, b}
    deg = [len(x) for x in nbr]
    seen = np.zeros(len(uniq), dtype=bool)
    order = []
    for start in sorted(range(len(uniq)), key=lambda v: deg[v]):
        if seen[start]:
            continue
        seen[start] = True
        queue = [start]
        while queue:
            v = queue.pop(0)
            order.append(v)
            for w in sorted(nbr[v], key=lambda w: deg[w]):
                if not seen[w]:
                    seen[w] = True
                    queue.append(w)
    order = order[::-1]
    rank = np.empty(len(uniq), dtype=np.int64)
    rank[order] = np.arange(len(order))
    pos = tris.reshape(-1, 3)[first_at][order]
    return pos, rank[inv]


def _patch(pos: np.ndarray, idx: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """QuadTreeMeshData.PatchUp: aproxima o vértice longe até o deslocamento caber num byte."""
    pos = list(pos)
    idx = idx.copy()
    moves = 0
    i = 0
    while i < len(idx):
        lo = idx[i].min()
        far = next((int(v) for v in idx[i] if v - lo > 255), -1)
        if far < 0:
            i += 1
            continue
        insert = int(lo) + 204
        if insert >= far:
            raise ValueError("índices do bloco não cabem na codificação")
        pos.insert(insert, pos.pop(far))
        idx = np.where(idx == far, insert, np.where((idx >= insert) & (idx < far), idx + 1, idx))
        moves += 1
        if moves > 3 * len(idx):
            raise ValueError("índices do bloco não cabem na codificação")
        i = 0
    return np.array(pos), idx


def encode_tile(tris: np.ndarray, mats: list[bytes], edges: np.ndarray) -> bytes:
    codes = list(dict.fromkeys(mats))
    if len(codes) > 8:
        raise ValueError(f"{len(codes)} materiais num bloco (máximo 8)")
    pos, idx = _order_vertices(tris)
    pos, idx = _patch(pos, idx)
    if len(pos) > 1023:
        raise ValueError(f"{len(pos)} vértices num bloco")
    # menor índice primeiro, girando junto os bits das arestas (bit k = aresta k -> k+1)
    edges = np.asarray(edges, dtype=np.int64).copy()
    for k in range(len(idx)):
        r = int(np.argmin(idx[k]))
        if r:
            idx[k] = np.roll(idx[k], -r)
            e = edges[k]
            edges[k] = ((e >> r) | (e << (3 - r))) & 7
    bmin = pos.min(0) - 0.1
    bmax = pos.max(0) + 0.1
    span = bmax - bmin
    nt, nv = len(idx), len(pos)
    vo = 0x90
    no = vo + 16 * nv
    to = no + 2
    ro = to + 4 * nt
    head = bytearray(0x90)
    struct.pack_into("<3f3f3i4I", head, 0, *bmin, *bmax, nt, nv, 8, vo, no, to, ro)
    for i in range(8):
        head[0x34 + 4 * i:0x38 + 4 * i] = codes[min(i, len(codes) - 1)]
    head[0x60:0x70] = b"VCQT" + struct.pack("<3I", 1, 0, 0)
    struct.pack_into("<4f4f", head, 0x70, bmin[0], 0.0, bmin[2], 1.0, span[0], 1.0, span[2], 1.0)
    verts = np.zeros((nv, 4), dtype="<f4")
    verts[:, 0] = (pos[:, 0] - bmin[0]) / span[0]
    verts[:, 1] = pos[:, 1]
    verts[:, 2] = (pos[:, 2] - bmin[2]) / span[2]
    vb = bytearray(verts.tobytes())
    vb[12::16] = bytes(nv)  # w = 0 (u32)
    tb = bytearray(4 * nt)
    mi = {c: i for i, c in enumerate(codes)}
    for k in range(nt):
        v0, v1, v2 = (int(x) for x in idx[k])
        assert v1 - v0 <= 255 and v2 - v0 <= 255 and v1 > v0 and v2 > v0, (v0, v1, v2)
        tb[4 * k:4 * k + 4] = bytes([v0 >> 2, (v0 & 3) << 6 | int(edges[k]) << 3 | mi[mats[k]], v1 - v0, v2 - v0])
    refs = b"\0\0" + b"\1" * (nt - 1) + b"\xff"
    return bytes(head + vb + b"\x80\x00" + tb + refs)


# --- arestas e partição --------------------------------------------------------------------------------------------

def edge_bits(tris: np.ndarray) -> np.ndarray:
    """Bit k = aresta (vk, vk+1) com exatamente um vizinho e dobra menor que EDGE_ANGLE."""
    key = np.round(tris * 1000).astype(np.int64)
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    # o mesmo triângulo pode vir repetido (blocos vizinhos); conta cada um uma vez
    tri_key = [tuple(sorted(map(tuple, key[t]))) for t in range(len(tris))]
    canon = {}
    for t, k in enumerate(tri_key):
        canon.setdefault(k, t)
    owners = defaultdict(list)
    for t in set(canon.values()):
        for k in range(3):
            a, b = tuple(key[t, k]), tuple(key[t, (k + 1) % 3])
            owners[(a, b) if a < b else (b, a)].append(t)
    for t in range(len(tris)):
        t0 = canon[tri_key[t]]
        if t0 != t:
            n[t] = n[t0]
    cos_lim = np.cos(np.radians(EDGE_ANGLE))
    bits = np.zeros(len(tris), dtype=np.int64)
    for t in range(len(tris)):
        for k in range(3):
            a, b = tuple(key[t, k]), tuple(key[t, (k + 1) % 3])
            o = owners[(a, b) if a < b else (b, a)]
            t0 = canon[tri_key[t]]
            if len(o) == 2:
                other = o[0] if o[1] == t0 else o[1]
                if np.dot(n[t], n[other]) >= cos_lim:
                    bits[t] |= 1 << k
    return bits


def _hits_cell(p: np.ndarray, lo, hi) -> np.ndarray:
    """SAT em XZ para vários triângulos (p: k×3×2): eixos do retângulo e normais das arestas; encostar conta."""
    ok = ((p[:, :, 0].max(1) >= lo[0]) & (p[:, :, 0].min(1) <= hi[0])
          & (p[:, :, 1].max(1) >= lo[1]) & (p[:, :, 1].min(1) <= hi[1]))
    corners = np.array([[lo[0], lo[1]], [hi[0], lo[1]], [hi[0], hi[1]], [lo[0], hi[1]]])
    for k in range(3):
        e = p[:, (k + 1) % 3] - p[:, k]
        nrm = np.stack([-e[:, 1], e[:, 0]], 1)
        r = nrm @ corners.T  # k×4
        sp = np.einsum("kij,kj->ki", p, nrm)  # k×3
        ok &= ~((r.max(1) < sp.min(1)) | (sp.max(1) < r.min(1)))
    return ok


def _fits(tris: np.ndarray, mats: list[bytes], ids: list[int]) -> bool:
    if len(ids) > MAX_TRIS:
        return False
    if len({mats[i] for i in ids}) > 8:
        return False
    key = np.round(tris[ids].reshape(-1, 3) * 1000).astype(np.int64)
    return len(np.unique(key, axis=0)) <= MAX_VERTS


def build(tris: np.ndarray, mats: list[bytes], edges: np.ndarray | None = None) -> bytes:
    tris = np.asarray(tris, dtype=np.float64)
    if edges is None:
        edges = edge_bits(tris)
    lo3 = tris.reshape(-1, 3).min(0) - 0.1
    hi3 = tris.reshape(-1, 3).max(0) + 0.1
    xz = tris[:, :, [0, 2]]
    leaves = []

    def split(name, lo, hi, ids, level):
        if level > 0 and _fits(tris, mats, ids):
            leaves.append((name, ids))
            return
        if level >= 16:
            raise ValueError(f"{name} não cabe no limite no nível 16")
        mid = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2]
        for xb in (0, 1):
            for zb in (0, 1):
                clo = [lo[0] if xb == 0 else mid[0], lo[1] if zb == 0 else mid[1]]
                chi = [mid[0] if xb == 0 else hi[0], mid[1] if zb == 0 else hi[1]]
                ids_a = np.asarray(ids)
                sub = ids_a[_hits_cell(xz[ids_a], clo, chi)].tolist()
                if sub:
                    split(f"{name}_{xb}{zb}", clo, chi, sub, level + 1)

    split("qt", [lo3[0], lo3[2]], [hi3[0], hi3[2]], list(range(len(tris))), 0)
    leaves.sort(key=lambda x: x[0])
    entries = []
    for name, ids in leaves:
        # ordem espacial dentro do bloco: ajuda a manter os deslocamentos dos índices pequenos
        c = tris[ids].mean(1)
        ids = [ids[i] for i in np.lexsort((c[:, 0], c[:, 2]))]
        entries.append((name + ".vcqtc", encode_tile(tris[ids], [mats[i] for i in ids], edges[ids])))
    entries.append(("qt.info", struct.pack("<6f", *lo3, *hi3)))
    return write_jpak(entries)


def _edge_flags(tris: np.ndarray, edges) -> set:
    """Arestas ligadas, como pares de posições ordenados (independe da rotação do triângulo)."""
    key = np.round(tris, 2)
    out = set()
    for t in range(len(tris)):
        for k in range(3):
            if (int(edges[t]) >> k) & 1:
                out.add((t,) + tuple(sorted((tuple(key[t, k]), tuple(key[t, (k + 1) % 3])))))
    return {x[1:] for x in out}


def read_all(path: str):
    tris, mats, edges = [], [], []
    for name, data in read_jpak(open(path, "rb").read()):
        if name.endswith(".vcqtc"):
            t, m, e = decode_tile(data)
            tris.append(t)
            mats += m
            edges.append(e)
    return np.concatenate(tris), mats, np.concatenate(edges)


def ring_soup(bg_margin: float | None = 200.0):
    """Triângulos do Ring na posição da pista. Do fundo, só o que fica a até `bg_margin` m (em XZ) da caixa do
    primeiro plano; ninguém dirige lá e o resto só aumenta o arquivo."""
    sys.path.insert(0, "scripts/research")
    sys.path.insert(0, ".")
    import ring_tracksplit as rt

    tris, mats = [], []
    yaw, offset = rt.start_transform()
    parts_by_mat = rt.ring_triangles(offset, yaw)
    fg = np.concatenate([pos for parts in parts_by_mat.values() for pos, _, _, bg, *_ in parts if not bg])
    lo, hi = fg.min(0) - (bg_margin or 0), fg.max(0) + (bg_margin or 0)
    for m, parts in parts_by_mat.items():
        for pos, idx, _, bg, *_ in parts:
            t = pos[idx]
            if bg and bg_margin is not None:
                c = t.mean(1)
                t = t[(c[:, 0] >= lo[0]) & (c[:, 0] <= hi[0]) & (c[:, 2] >= lo[2]) & (c[:, 2] <= hi[2])]
            n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
            flip = n[:, 1] < 0  # normal para cima, como nos originais
            t[flip] = t[flip][:, [0, 2, 1]]
            tris.append(t)
            mats += [RING_SURFACES[m]] * len(t)
    return np.concatenate(tris), mats


def main() -> None:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("jpk")
    p = sub.add_parser("roundtrip")
    p.add_argument("jpk")
    p = sub.add_parser("ring")
    p.add_argument("out")
    p.add_argument("--bg-margin", type=float, default=200.0, help="fundo mantido em volta do primeiro plano (m); <0 = todo")
    a = ap.parse_args()
    if a.cmd == "info":
        ents = read_jpak(open(a.jpk, "rb").read())
        tris, mats, edges = read_all(a.jpk)
        from collections import Counter
        print(f"{len(ents) - 1} blocos, {len(tris)} triângulos")
        print("superfícies:", Counter(m.decode() for m in mats).most_common())
        print("caixa:", struct.unpack("<6f", dict(ents)["qt.info"]))
    elif a.cmd == "roundtrip":
        raw = open(a.jpk, "rb").read()
        ents = read_jpak(raw)
        print("JPAK regravado idêntico:", write_jpak(ents) == raw)
        worst = 0.0
        for name, data in ents:
            if not name.endswith(".vcqtc"):
                continue
            t, m, e = decode_tile(data)
            t2, m2, e2 = decode_tile(encode_tile(t, m, e))
            assert len(t) == len(t2) and len(e) == len(e2), name
            # casa cada triângulo novo com o original de vértices mais próximos (a ordem e a rotação mudam)
            orig = np.sort(t.reshape(-1, 9), 1)
            for i in range(len(t2)):
                b = np.sort(t2[i].reshape(9))
                dist = np.abs(orig - b).max(1)
                cands = np.flatnonzero(dist <= dist.min() + 1e-3)
                cands = [c for c in cands if m[c] == m2[i] and bin(int(e[c])).count("1") == bin(int(e2[i])).count("1")]
                assert cands, (name, i)
                j = int(cands[0])
                worst = max(worst, float(dist[j]))
                # mesma aresta ligada: compara pelo par de vértices, com tolerância
                for k in range(3):
                    if not (int(e2[i]) >> k) & 1:
                        continue
                    p, q = t2[i, k], t2[i, (k + 1) % 3]
                    hit = False
                    for kk in range(3):
                        if (int(e[j]) >> kk) & 1:
                            pp, qq = t[j, kk], t[j, (kk + 1) % 3]
                            if min(np.abs(p - pp).max() + np.abs(q - qq).max(),
                                   np.abs(p - qq).max() + np.abs(q - pp).max()) < 0.01:
                                hit = True
                    assert hit, (name, i)
        print(f"blocos recodificados: mesmos triângulos, materiais e arestas (maior diferença {worst:.3f} m)")
        tris, mats, edges = read_all(a.jpk)
        ok = (edge_bits(tris) == edges).mean()
        print(f"bits de aresta recalculados que batem com o original: {ok:.1%}")
    else:
        tris, mats = ring_soup(a.bg_margin if a.bg_margin >= 0 else None)
        data = build(tris, mats)
        open(a.out, "wb").write(data)
        n = len(read_jpak(data)) - 1
        print(f"gravado {a.out}: {len(tris)} triângulos em {n} blocos, {len(data)} bytes")


if __name__ == "__main__":
    main()
