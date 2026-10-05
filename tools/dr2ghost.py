#!/usr/bin/env python3
"""dr2ghost.py - lê fantasmas do DiRT Rally 2.0 (stream GHST).

    python3 tools/dr2ghost.py ARQUIVO... [--csv PASTA]

ARQUIVO pode ser um save cifrado (savegame@ghosts#*, decifrado com dr2save.py,
precisa de pycryptodome) ou um .ghst já extraído. Imprime cabeçalho, metadados
e canais; com --csv grava a trajetória (posição + rotação + entradas) por
amostra de posição.

Formato (docs/reverse_engineering/ghosts.md §1, conferido byte a byte contra a
memória do jogo com o fantasma carregado):
    "GHST" u32 tamanho (resto do arquivo) | u8 versão (7) | u8 máscara de canais
    por bit da máscara: u32 contagem, u8 tamanho da amostra
    u16 tamanho dos metadados, depois itens (u8 id, u8 len, valor)
    segmentos até o fim: u32 tamanho, u32 t0 (ms), registros
    registro: u8 delta_ms, u8 máscara, payload de cada canal marcado (ordem dos bits)
    "ff 00" (delta 255, nenhum canal) só avança o relógio.
"""

import argparse
import csv
import math
import os
import struct
import sys

# bit -> (nome, tamanho esperado no arquivo)
CHANNELS = {
    0: ("posicao", 14),   # 3 float + u16 progresso (/65535)
    1: ("rotacao", 4),    # quaternion int8 x,y,z,w (/127), só quando muda
    2: ("entradas", 4),   # u8 x4 (acelerador, freio, direção?, marcha?)
    3: ("1hz", 4),        # float; conversão para a memória ainda desconhecida
    4: ("rodas", 8),      # u8 x8 (/127)
    5: ("unico", 4),      # uma amostra no início
    6: ("eventos", 20),   # = amostra+0x10 na memória
}

META = {0: "carro?", 1: "carro_f1", 3: "setores_ms", 4: "tempo_ms", 10: "carro_u10", 15: "carro_u15"}


def parse(d: bytes) -> dict:
    if d[:4] != b"GHST":
        raise ValueError("não é GHST")
    size, ver, mask = struct.unpack_from("<IBB", d, 4)
    if size != len(d) - 8:
        raise ValueError("tamanho %d, esperado %d" % (size, len(d) - 8))
    o = 10
    chans = {}
    for b in range(8):
        if mask >> b & 1:
            chans[b] = struct.unpack_from("<IB", d, o)
            o += 5
    mlen = struct.unpack_from("<H", d, o)[0]
    o += 2
    meta, end = {}, o + mlen
    while o < end:
        k, n = d[o], d[o + 1]
        meta[k] = d[o + 2:o + 2 + n]
        o += 2 + n
    samples = {c: [] for c in chans}
    segs = []
    while o < len(d):
        blen, t = struct.unpack_from("<II", d, o)
        segend = o + 4 + blen
        o += 8
        segs.append((t, blen))
        first = True
        while o < segend:
            dt, m = d[o], d[o + 1]
            o += 2
            if not first:
                t += dt
            first = False
            if m & ~mask:
                raise ValueError("máscara 0x%x inválida em %d" % (m, o - 2))
            for c in sorted(chans):
                if m >> c & 1:
                    sz = chans[c][1]
                    samples[c].append((t, d[o:o + sz]))
                    o += sz
        if o != segend:
            raise ValueError("segmento passou do fim (%d > %d)" % (o, segend))
    for c, (n, _) in chans.items():
        if len(samples[c]) != n:
            raise ValueError("canal %d: %d amostras, cabeçalho diz %d" % (c, len(samples[c]), n))
    return {"version": ver, "mask": mask, "channels": chans, "meta": meta, "segments": segs, "samples": samples}


def load(path: str) -> bytes:
    d = open(path, "rb").read()
    if d[:4] == b"GHST":
        return d
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import dr2save
    _, ghst = dr2save.unpack(dr2save.decrypt(d))
    if ghst is None:
        raise ValueError("save sem GHST")
    return ghst


def meta_str(k: int, v: bytes) -> str:
    if k == 0:
        return v.decode(errors="replace")
    if k == 3:
        return str(list(struct.unpack("<%dI" % (len(v) // 4), v)))
    if k in (1, 8, 9) and len(v) == 4:
        return "%.3f" % struct.unpack("<f", v)[0]
    if len(v) == 4:
        return str(struct.unpack("<I", v)[0])
    return v.hex()


def fmt_ms(ms: int) -> str:
    return "%d:%06.3f" % (ms // 60000, ms % 60000 / 1000)


def write_csv(g: dict, path: str) -> None:
    s = g["samples"]
    rot, inp = s.get(1, []), s.get(2, [])
    ri = ii = 0
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_ms", "x", "y", "z", "progresso", "qx", "qy", "qz", "qw", "in0", "in1", "in2", "in3"])
        for t, p in s.get(0, []):
            x, y, z, prog = struct.unpack("<3fH", p)
            while ri + 1 < len(rot) and rot[ri + 1][0] <= t:
                ri += 1
            while ii + 1 < len(inp) and inp[ii + 1][0] <= t:
                ii += 1
            q = [c / 127 for c in struct.unpack("<4b", rot[ri][1])] if rot else [0] * 4
            i4 = list(inp[ii][1]) if inp else [0] * 4
            w.writerow([t, "%.3f" % x, "%.3f" % y, "%.3f" % z, "%.5f" % (prog / 65535)]
                       + ["%.3f" % c for c in q] + i4)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("files", nargs="+")
    ap.add_argument("--csv", help="pasta para gravar <nome>.csv")
    args = ap.parse_args()
    for path in args.files:
        name = os.path.basename(path)
        g = parse(load(path))
        pos = g["samples"].get(0, [])
        dist = sum(math.dist(struct.unpack_from("<3f", a), struct.unpack_from("<3f", b))
                   for (_, a), (_, b) in zip(pos, pos[1:]))
        total = struct.unpack("<I", g["meta"][4])[0] if 4 in g["meta"] else (pos[-1][0] if pos else 0)
        print("%s: versão %d, tempo %s, %.2f km, segmentos %s" % (
            name, g["version"], fmt_ms(total), dist / 1000, [t for t, _ in g["segments"]]))
        for c, (n, sz) in sorted(g["channels"].items()):
            print("  canal %d %-8s %5d x %2d bytes" % (c, CHANNELS.get(c, ("?",))[0], n, sz))
        for k, v in sorted(g["meta"].items()):
            print("  meta %2d %-10s %s" % (k, META.get(k, ""), meta_str(k, v)))
        if args.csv:
            os.makedirs(args.csv, exist_ok=True)
            write_csv(g, os.path.join(args.csv, name + ".csv"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
