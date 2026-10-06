"""Lê o `track.vis` (PVS da rota, pool VISIBILITY_SYSTEM) e gera versões novas: "tudo visível" ou com as células
de outro `tracksplit.pssg`. Ver docs/reverse_engineering/track_loading.md §9.7–9.9.

Formato (versão 4, little-endian; medido em 172 arquivos de 40 locais, 2026-10-06):
- 0x00: u32 versão (4), nós do PVS, folhas (nós = 2*folhas - 1), nós da kd-tree de itens, bytes do bitset por folha,
  offset dos nós (0x80), offset dos blocos das folhas, offset da kd-tree.
- 0x20: caixa da árvore do PVS: f32 mín xyz, u32 offset da seção de caixas do PVS, f32 máx xyz, u32 0.
- 0x40: itens por grupo (u32 x 16). Grupo 0 = células do `tracksplit.pssg` (Montalegre 470), na ordem do vetor
  de células do jogo, que é a ordem reversa do arquivo.
- Nós do PVS (6 bytes): folha = u16 0, u24 offset do bloco, u8 1. Interno = u16 a, u16 b, u16 c: filho do lado
  baixo a&0x1fff, do lado alto a-1, eixo (a>>13)&3 (0 x, 1 y, 2 z), bit 15 de a = flag (só nas pistas grandes),
  corte = ((b>>12)<<16 | c) / 2^20 dentro da caixa do nó.
- Bloco da folha (alinhado a 16, com pelo menos um byte 0 depois): bitset em PackBits (c >= 0x80: repete o próximo byte c-0x80 vezes; c < 0x80: copia
  c bytes), bits em ordem little-endian. Tamanho = ceil(bits/8) arredondado a 16; bits = nós da kd + itens.
  Folhas sob o chão têm o bloco "tudo 1".
- kd-tree de itens, em pré-ordem, nó = 48 bytes + 32 por registro:
  f32 mín xyz, u32 bit do nó (byte | bit<<16); f32 máx xyz, u16 índice do nó, u16 registros;
  u32 offset do próximo nó (0 no último), u32 profundidade<<16 | tamanho do próximo nó, u32 0, u32 2 (interno) / 0.
  Registro: f32 mín xyz, u32 grupo; f32 máx xyz, u32 índice no grupo. O item fica no nó mais fundo que contém a
  caixa inteira. Bits do PVS em largura (BFS): o nó, depois um bit por registro dele.
- Seção de caixas do PVS: por nó do PVS, em ordem, f32 mín xyz, f32 máx xyz, u32 n, u32 índice, n pontos vec4.

O jogo monta por quadro a lista de células com essa kd-tree (job de `0x140c11b00`) e só avalia e desenha as células
dela (`0x140a7f700` -> `0x140bcb2d0`); uma pista nova precisa da kd-tree com as próprias células.

uso:
  python3 scripts/research/track_vis.py info <track.vis>
  python3 scripts/research/track_vis.py all-visible <track.vis> <saida.vis>
  python3 scripts/research/track_vis.py cells <track.vis> <tracksplit.pssg> <saida.vis>
"""
from __future__ import annotations

import argparse
import struct
import sys
from collections import deque
from dataclasses import dataclass, field

Box = tuple[tuple[float, float, float], tuple[float, float, float]]


def unpack_rle(data: bytes, pos: int, size: int) -> tuple[bytes, int]:
    out = bytearray()
    while len(out) < size:
        c = data[pos]
        pos += 1
        if c >= 0x80:
            out += bytes([data[pos]]) * (c - 0x80)
            pos += 1
        else:
            out += data[pos:pos + c]
            pos += c
    if len(out) != size:
        raise ValueError(f"bloco com {len(out)} bytes (esperado {size})")
    return bytes(out), pos


def pack_rle(raw: bytes) -> bytes:
    """PackBits do formato: corridas de até 127 bytes iguais; literais de até 127 bytes."""
    out = bytearray()
    i = 0
    while i < len(raw):
        j = i
        while j < len(raw) and raw[j] == raw[i] and j - i < 127:
            j += 1
        if j - i >= 2:
            out += bytes([0x80 + (j - i), raw[i]])
            i = j
            continue
        k = i
        while k < len(raw) and k - i < 127 and not (k + 1 < len(raw) and raw[k + 1] == raw[k]):
            k += 1
        k = max(k, i + 1)
        out += bytes([k - i]) + raw[i:k]
        i = k
    return bytes(out)


def align16(n: int) -> int:
    return (n + 15) & ~15


@dataclass
class KdNode:
    lo: tuple[float, float, float]
    hi: tuple[float, float, float]
    depth: int
    inner: bool
    recs: list[tuple[tuple[float, float, float], tuple[float, float, float], int, int]]  # (mín, máx, grupo, índice)
    kids: list[int] = field(default_factory=list)

    def contains(self, box: Box) -> bool:
        return all(self.lo[j] <= box[0][j] and box[1][j] <= self.hi[j] for j in range(3))


class TrackVis:
    def __init__(self, data: bytes):
        self.data = data
        (self.version, self.n_nodes, self.n_leaves, self.u3, self.leaf_bytes, self.nodes_off, self.blocks_off,
         self.kd_off) = struct.unpack_from("<8I", data, 0)
        if self.version != 4:
            raise ValueError(f"versão {self.version} (esperado 4)")
        self.box_min = struct.unpack_from("<3f", data, 0x20)
        self.box_max = struct.unpack_from("<3f", data, 0x30)
        self.pvs_boxes_off = struct.unpack_from("<I", data, 0x2C)[0]
        self.groups = [g for g in struct.unpack_from("<16I", data, 0x40)]
        self.n_bits = self.u3 + sum(self.groups)
        self.leaves: dict[int, int] = {}  # índice do nó -> offset do bloco
        self.inner: dict[int, tuple[int, int, int, float]] = {}  # índice -> (filho baixo, filho alto, eixo, fração)
        for i in range(self.n_nodes):
            a, b, c = struct.unpack_from("<3H", data, self.nodes_off + 6 * i)
            if a == 0:
                v = b | c << 16
                self.leaves[i] = v & 0xFFFFFF
            else:
                kid = a & 0x1FFF
                self.inner[i] = (kid, kid - 1, (a >> 13) & 3, ((b >> 12) << 16 | c) / float(1 << 20))

    def leaf_bits(self, node: int) -> bytes:
        return unpack_rle(self.data, self.leaves[node], self.leaf_bytes)[0]

    def leaf_regions(self) -> dict[int, tuple[list[float], list[float]]]:
        out = {}
        stack = [(0, list(self.box_min), list(self.box_max))]
        while stack:
            i, lo, hi = stack.pop()
            if i in self.leaves:
                out[i] = (lo, hi)
                continue
            a, b, axis, f = self.inner[i]
            cut = lo[axis] + f * (hi[axis] - lo[axis])
            lo2, hi1 = list(lo), list(hi)
            hi1[axis] = cut
            lo2[axis] = cut
            stack += [(a, lo, hi1), (b, lo2, hi)]
        return out

    def all_visible(self) -> bytes:
        """Mesmo arquivo com todo bloco de folha trocado por "tudo 1"; nada muda de lugar."""
        full = bytearray(b"\xff" * self.leaf_bytes)
        full[self.n_bits // 8:] = b"\0" * (self.leaf_bytes - self.n_bits // 8)
        if self.n_bits % 8:
            full[self.n_bits // 8] = (1 << (self.n_bits % 8)) - 1
        block = pack_rle(bytes(full))
        if unpack_rle(block, 0, self.leaf_bytes)[0] != bytes(full):
            raise AssertionError("PackBits não fecha")
        out = bytearray(self.data)
        for off in self.leaves.values():
            _, end = unpack_rle(self.data, off, self.leaf_bytes)
            if len(block) > end - off:
                raise ValueError(f"bloco em {off:#x} tem {end - off} bytes; o \"tudo 1\" precisa de {len(block)}")
            out[off:end] = block + b"\0" * (end - off - len(block))
        return bytes(out)

    def kd_nodes(self) -> list[KdNode]:
        """kd-tree de itens, em pré-ordem, com os filhos resolvidos."""
        out = []
        o = self.kd_off
        while True:
            lo = struct.unpack_from("<3f", self.data, o)
            hi = struct.unpack_from("<3f", self.data, o + 16)
            idx, cnt = struct.unpack_from("<HH", self.data, o + 28)
            nxt, a, _, c = struct.unpack_from("<4I", self.data, o + 32)
            if idx != len(out):
                raise ValueError(f"nó da kd em {o:#x} com índice {idx} (esperado {len(out)})")
            recs = []
            for r in range(cnt):
                x = struct.unpack_from("<3fI3fI", self.data, o + 48 + 32 * r)
                recs.append((x[0:3], x[4:7], x[3], x[7]))
            out.append(KdNode(lo, hi, a >> 16, c == 2, recs))
            if nxt == 0:
                break
            o = nxt
        if len(out) != self.u3:
            raise ValueError(f"kd com {len(out)} nós; o cabeçalho diz {self.u3}")
        link_kids(out)
        return out

    def leaf_blocks(self) -> dict[int, bytes]:
        """Bloco de cada folha do PVS, como está no arquivo (sem o preenchimento)."""
        return {n: self.data[off:unpack_rle(self.data, off, self.leaf_bytes)[1]] for n, off in self.leaves.items()}

    def build(self, kd: list[KdNode], groups: list[int], blocks: dict[int, bytes] | None = None) -> bytes:
        """Monta o arquivo com outra kd-tree; o PVS (nós e caixas) é mantido. Sem `blocks`, toda folha fica "tudo 1"."""
        n_bits = len(kd) + sum(groups)
        leaf_bytes = align16((n_bits + 7) // 8)
        if blocks is None:
            full = bytearray(b"\xff" * leaf_bytes)
            full[n_bits // 8:] = b"\0" * (leaf_bytes - n_bits // 8)
            if n_bits % 8:
                full[n_bits // 8] = (1 << (n_bits % 8)) - 1
            block = pack_rle(bytes(full))
            blocks = {n: block for n in self.leaves}
        elif leaf_bytes != self.leaf_bytes:
            raise ValueError("blocos antigos só servem com o mesmo tamanho de bitset")

        out = bytearray(self.data[:self.blocks_off])
        o = self.blocks_off
        body = bytearray()
        for n in sorted(self.leaves, key=self.leaves.get):  # mesma ordem do original
            struct.pack_into("<HHH", out, self.nodes_off + 6 * n, 0, o & 0xFFFF, (o >> 16) | 1 << 8)
            body += blocks[n]
            body += b"\0" * (align16(len(body) + 1) - len(body))  # pelo menos um zero depois do bloco
            o = self.blocks_off + len(body)
        out += body
        kd_off = len(out)

        bit = {}
        pos = 0
        queue = deque([0])
        while queue:
            i = queue.popleft()
            bit[i] = pos
            pos += 1 + len(kd[i].recs)
            queue.extend(kd[i].kids)
        sizes = [48 + 32 * len(n.recs) for n in kd]
        for i, n in enumerate(kd):
            last = i == len(kd) - 1
            nxt = 0 if last else len(out) + sizes[i]
            out += struct.pack("<3fI", *n.lo, bit[i] // 8 | (bit[i] % 8) << 16)
            out += struct.pack("<3fHH", *n.hi, i, len(n.recs))
            out += struct.pack("<4I", nxt, n.depth << 16 | (0 if last else sizes[i + 1]), 0, 2 if n.inner else 0)
            for lo, hi, g, k in n.recs:
                out += struct.pack("<3fI3fI", *lo, g, *hi, k)
        pvs_boxes_off = len(out)
        out += self.data[self.pvs_boxes_off:]

        struct.pack_into("<I", out, 0x0C, len(kd))
        struct.pack_into("<I", out, 0x10, leaf_bytes)
        struct.pack_into("<I", out, 0x1C, kd_off)
        struct.pack_into("<I", out, 0x2C, pvs_boxes_off)
        struct.pack_into("<16I", out, 0x40, *groups)
        return bytes(out)

    def with_cells(self, cells: list[Box]) -> bytes:
        """Troca o grupo 0 (células) pelas caixas dadas, na ordem do vetor de células do jogo (reversa do arquivo)."""
        kd = self.kd_nodes()
        for n in kd:
            n.recs = [r for r in n.recs if r[2] != 0]
        for k, box in enumerate(cells):
            i = 0
            while True:
                kid = next((j for j in kd[i].kids if kd[j].contains(box)), None)
                if kid is None:
                    break
                i = kid
            kd[i].recs.append((box[0], box[1], 0, k))
        groups = list(self.groups)
        groups[0] = len(cells)
        return self.build(kd, groups)


def link_kids(kd: list[KdNode]) -> None:
    def walk(i: int) -> int:
        if not kd[i].inner:
            kd[i].kids = []
            return i + 1
        j = walk(i + 1)
        end = walk(j)
        kd[i].kids = [i + 1, j]
        return end

    if walk(0) != len(kd):
        raise ValueError("kd-tree não fecha em pré-ordem")


def tracksplit_cells(path: str) -> list[Box]:
    """Caixa de cada célula (união das BOUNDINGBOX dos nós de render), na ordem do vetor do jogo."""
    sys.path.insert(0, ".")
    from tools.egodata.pssg import PSSGFile

    split = PSSGFile(open(path, "rb").read())
    surface = split.find_by_id("surface")[0]
    boxes = []
    for cell in surface.children:
        if cell.type_name not in ("NODE", "RENDERNODE"):
            continue
        lo, hi = [float("inf")] * 3, [float("-inf")] * 3
        stack = [cell]
        while stack:
            n = stack.pop()
            stack.extend(n.children)
            if n.type_name != "RENDERNODE":
                continue
            bb = next((c for c in n.children if c.type_name == "BOUNDINGBOX"), None)
            if bb is None or not bb.data:
                continue
            v = struct.unpack(">6f", bb.data[:24])
            lo = [min(lo[j], v[j]) for j in range(3)]
            hi = [max(hi[j], v[3 + j]) for j in range(3)]
        if lo[0] == float("inf"):
            lo = hi = [0.0, 0.0, 0.0]
        boxes.append((tuple(lo), tuple(hi)))
    return boxes[::-1]


def main() -> None:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("vis")
    p = sub.add_parser("all-visible")
    p.add_argument("vis")
    p.add_argument("out")
    p = sub.add_parser("cells")
    p.add_argument("vis")
    p.add_argument("tracksplit")
    p.add_argument("out")
    a = ap.parse_args()
    vis = TrackVis(open(a.vis, "rb").read())
    if a.cmd == "info":
        print(f"versão {vis.version}: {vis.n_nodes} nós, {vis.n_leaves} folhas, bitset de {vis.n_bits} bits "
              f"({vis.leaf_bytes} bytes), u3 {vis.u3}")
        print(f"caixa {tuple(round(v, 1) for v in vis.box_min)} .. {tuple(round(v, 1) for v in vis.box_max)}")
        print(f"grupos: {[g for g in vis.groups if g]} (1º = células do tracksplit)")
        full = sum(1 for n in vis.leaves if all(x == 0xFF for x in vis.leaf_bits(n)[:vis.n_bits // 8]))
        print(f"folhas \"tudo 1\": {full}")
        kd = vis.kd_nodes()
        print(f"kd: {len(kd)} nós, {sum(len(n.recs) for n in kd)} registros, profundidade máx {max(n.depth for n in kd)}")
        # ida e volta: remontar com a mesma kd e os mesmos blocos tem que dar o mesmo arquivo
        same = vis.build(kd, vis.groups, vis.leaf_blocks()) == vis.data
        print(f"remontagem idêntica: {same}")
    elif a.cmd == "cells":
        cells = tracksplit_cells(a.tracksplit)
        out = vis.with_cells(cells)
        check = TrackVis(out)
        kd = check.kd_nodes()
        got = sorted(r[3] for n in kd for r in n.recs if r[2] == 0)
        assert got == list(range(len(cells))), "células faltando na kd"
        open(a.out, "wb").write(out)
        print(f"gravado {a.out} ({len(out)} bytes): {len(cells)} células no grupo 0, bitset de {check.n_bits} bits "
              f"({check.leaf_bytes} bytes), kd em {check.kd_off:#x}")
    else:
        out = vis.all_visible()
        check = TrackVis(out)
        assert all(all(x == 0xFF for x in check.leaf_bits(n)[:vis.n_bits // 8]) for n in check.leaves)
        open(a.out, "wb").write(out)
        print(f"gravado {a.out} ({len(out)} bytes, {len(check.leaves)} folhas tudo 1)")


if __name__ == "__main__":
    main()
