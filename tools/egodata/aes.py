"""Decifração AES (ECB) em Python puro, por tabelas. Bytes finais fora de um bloco de 16 passam intactos."""

_SBOX = [0] * 256
_INV_SBOX = [0] * 256


def _init_sbox() -> None:
    p = q = 1
    _SBOX[0] = 0x63
    while True:
        p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        x = q ^ ((q << 1 | q >> 7) & 0xFF) ^ ((q << 2 | q >> 6) & 0xFF) ^ ((q << 3 | q >> 5) & 0xFF) ^ ((q << 4 | q >> 4) & 0xFF)
        _SBOX[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break
    for i in range(256):
        _INV_SBOX[_SBOX[i]] = i


_init_sbox()


def _xtime(a: int) -> int:
    return ((a << 1) ^ 0x1B) & 0xFF if a & 0x80 else a << 1


def _mul(a: int, b: int) -> int:
    r = 0
    while b:
        if b & 1:
            r ^= a
        a = _xtime(a)
        b >>= 1
    return r


_T = [[0] * 256 for _ in range(4)]
for _x in range(256):
    _s = _INV_SBOX[_x]
    _w = (_mul(_s, 14) << 24) | (_mul(_s, 9) << 16) | (_mul(_s, 13) << 8) | _mul(_s, 11)
    for _k in range(4):
        _T[_k][_x] = ((_w >> (8 * _k)) | (_w << (32 - 8 * _k))) & 0xFFFFFFFF


def _sub_word(t: int) -> int:
    return (_SBOX[t >> 24] << 24) | (_SBOX[(t >> 16) & 255] << 16) | (_SBOX[(t >> 8) & 255] << 8) | _SBOX[t & 255]


def _decryption_keys(key: bytes) -> tuple[list[list[int]], int]:
    nk = len(key) // 4
    nr = nk + 6
    w = [int.from_bytes(key[4 * i : 4 * i + 4], "big") for i in range(nk)]
    rc = 1
    for i in range(nk, 4 * (nr + 1)):
        t = w[i - 1]
        if i % nk == 0:
            t = _sub_word(((t << 8) | (t >> 24)) & 0xFFFFFFFF) ^ (rc << 24)
            rc = _xtime(rc)
        elif nk > 6 and i % nk == 4:
            t = _sub_word(t)
        w.append(w[i - nk] ^ t)
    rk = [w[4 * r : 4 * r + 4] for r in range(nr + 1)]
    dk = [rk[nr]]
    for r in range(nr - 1, 0, -1):
        dk.append([
            _T[0][_SBOX[v >> 24]] ^ _T[1][_SBOX[(v >> 16) & 255]] ^ _T[2][_SBOX[(v >> 8) & 255]] ^ _T[3][_SBOX[v & 255]]
            for v in rk[r]
        ])
    dk.append(rk[0])
    return dk, nr


def decrypt_ecb(data: bytes, key: bytes) -> bytes:
    dk, nr = _decryption_keys(key)
    t0, t1, t2, t3 = _T
    inv = _INV_SBOX
    out = bytearray(data)
    for o in range(0, len(data) - 15, 16):
        k = dk[0]
        s0 = int.from_bytes(data[o : o + 4], "big") ^ k[0]
        s1 = int.from_bytes(data[o + 4 : o + 8], "big") ^ k[1]
        s2 = int.from_bytes(data[o + 8 : o + 12], "big") ^ k[2]
        s3 = int.from_bytes(data[o + 12 : o + 16], "big") ^ k[3]
        for r in range(1, nr):
            k = dk[r]
            s0, s1, s2, s3 = (
                t0[s0 >> 24] ^ t1[(s3 >> 16) & 255] ^ t2[(s2 >> 8) & 255] ^ t3[s1 & 255] ^ k[0],
                t0[s1 >> 24] ^ t1[(s0 >> 16) & 255] ^ t2[(s3 >> 8) & 255] ^ t3[s2 & 255] ^ k[1],
                t0[s2 >> 24] ^ t1[(s1 >> 16) & 255] ^ t2[(s0 >> 8) & 255] ^ t3[s3 & 255] ^ k[2],
                t0[s3 >> 24] ^ t1[(s2 >> 16) & 255] ^ t2[(s1 >> 8) & 255] ^ t3[s0 & 255] ^ k[3],
            )
        k = dk[nr]
        words = (
            ((inv[s0 >> 24] << 24) | (inv[(s3 >> 16) & 255] << 16) | (inv[(s2 >> 8) & 255] << 8) | inv[s1 & 255]) ^ k[0],
            ((inv[s1 >> 24] << 24) | (inv[(s0 >> 16) & 255] << 16) | (inv[(s3 >> 8) & 255] << 8) | inv[s2 & 255]) ^ k[1],
            ((inv[s2 >> 24] << 24) | (inv[(s1 >> 16) & 255] << 16) | (inv[(s0 >> 8) & 255] << 8) | inv[s3 & 255]) ^ k[2],
            ((inv[s3 >> 24] << 24) | (inv[(s2 >> 16) & 255] << 16) | (inv[(s1 >> 8) & 255] << 8) | inv[s0 & 255]) ^ k[3],
        )
        out[o : o + 16] = b"".join(v.to_bytes(4, "big") for v in words)
    return bytes(out)
