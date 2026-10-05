#!/usr/bin/env python3
"""dr2save.py - decifra os saves do DiRT Rally 2.0 (Steam Cloud, savegame@*).

    python3 tools/dr2save.py SAVE... [-o PASTA]

Para cada arquivo grava <nome>.bin (contêiner descomprimido) e, nos fantasmas,
<nome>.ghst (stream GHST). Precisa de pycryptodome (`pip install pycryptodome`).

Cifra (docs/reverse_engineering/ghosts.md §1): AES-256-ECB sem IV. A chave é
fixa: FNV-1a de "rp17" semeia um MT19937 (init LCG 69069, semente | 1) que
gera 64 dígitos hex com `rand % 15`; os pares viram os 32 bytes da chave.
"""

import argparse
import os
import struct
import sys
import zlib

from Crypto.Cipher import AES

CONTAINER = (4, 24, 2)  # versão, tamanho do cabeçalho, compressão (2 = zlib)


def fnv1a(data: bytes) -> int:
    h = 0x811C9DC5
    for c in data:
        h = ((h ^ c) * 0x1000193) & 0xFFFFFFFF
    return h


def mt19937(seed: int):
    """MT19937 com a inicialização do jogo (0x140859b80): LCG de Knuth."""
    mt = [(seed | 1) & 0xFFFFFFFF]
    for _ in range(623):
        mt.append((mt[-1] * 69069) & 0xFFFFFFFF)
    while True:
        for k in range(624):
            y = (mt[k] & 0x80000000) | (mt[(k + 1) % 624] & 0x7FFFFFFF)
            mt[k] = mt[(k + 397) % 624] ^ (y >> 1) ^ (0x9908B0DF if y & 1 else 0)
        for y in mt:
            y ^= y >> 11
            y ^= (y << 7) & 0x9D2C5680
            y ^= (y << 15) & 0xEFC60000
            y ^= y >> 18
            yield y


def save_key() -> bytes:
    rng = mt19937(fnv1a(b"rp17"))
    return bytes.fromhex("".join("0123456789ABCDEF"[next(rng) % 15] for _ in range(64)))


KEY = save_key()
assert KEY.hex() == "91d84b7138a2cc4dadc022db4ebd1edd6c3454746acb235b618b404170b86e71"


def decrypt(data: bytes) -> bytes:
    return AES.new(KEY, AES.MODE_ECB).decrypt(data)


def unpack(plain: bytes):
    """Devolve (payload descomprimido, stream GHST ou None); None se não for contêiner."""
    if len(plain) < 24 or struct.unpack_from("<III", plain) != CONTAINER:
        return None, None
    size = struct.unpack_from("<Q", plain, 16)[0]
    payload = zlib.decompressobj().decompress(plain[24:])
    if len(payload) != size:
        raise ValueError("tamanho descomprimido %d, esperado %d" % (len(payload), size))
    # Fantasmas: o payload EGO traz um segundo stream zlib que começa com GHST.
    for p in range(len(payload) - 1):
        if payload[p] == 0x78 and payload[p + 1] in (0x01, 0x9C, 0xDA):
            try:
                inner = zlib.decompressobj().decompress(payload[p:])
            except zlib.error:
                continue
            if inner.startswith(b"GHST"):
                return payload, inner
    return payload, None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("saves", nargs="+")
    ap.add_argument("-o", "--out", default="plain")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for path in args.saves:
        name = os.path.basename(path)
        plain = decrypt(open(path, "rb").read())
        payload, ghst = unpack(plain)
        if payload is None:
            # Descritores (#QKRHMYXE): texto direto, sem contêiner.
            open(os.path.join(args.out, name + ".txt"), "wb").write(plain)
            print("%s: descritor %r" % (name, plain.split(b"\0")[0].decode(errors="replace")))
            continue
        open(os.path.join(args.out, name + ".bin"), "wb").write(payload)
        if ghst is not None:
            open(os.path.join(args.out, name + ".ghst"), "wb").write(ghst)
        print("%s: %d bytes%s" % (name, len(payload), ", GHST %d bytes" % len(ghst) if ghst else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
