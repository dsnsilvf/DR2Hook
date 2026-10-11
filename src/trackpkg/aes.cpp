#include "trackpkg/aes.h"

#include <array>
#include <cstring>

namespace trackpkg {
namespace {

struct Tables {
  uint8_t sbox[256];
  uint8_t inv[256];
  Tables() {
    // S-box pela inversa em GF(2^8) e a transformação afim (como em tools/egodata/aes.py).
    uint8_t p = 1, q = 1;
    sbox[0] = 0x63;
    do {
      p = static_cast<uint8_t>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
      q ^= q << 1;
      q ^= q << 2;
      q ^= q << 4;
      if (q & 0x80) q ^= 0x09;
      auto rot = [](uint8_t v, int s) { return static_cast<uint8_t>((v << s) | (v >> (8 - s))); };
      sbox[p] = static_cast<uint8_t>(q ^ rot(q, 1) ^ rot(q, 2) ^ rot(q, 3) ^ rot(q, 4) ^ 0x63);
    } while (p != 1);
    for (int i = 0; i < 256; ++i) inv[sbox[i]] = static_cast<uint8_t>(i);
  }
};

const Tables &T() {
  static const Tables t;
  return t;
}

uint8_t Xtime(uint8_t a) { return static_cast<uint8_t>((a << 1) ^ ((a & 0x80) ? 0x1b : 0)); }

uint8_t Mul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  while (b) {
    if (b & 1) r ^= a;
    a = Xtime(a);
    b >>= 1;
  }
  return r;
}

} // namespace

Aes256::Aes256(const uint8_t key[32]) {
  const Tables &t = T();
  std::memcpy(rk_, key, 32);
  uint8_t rcon = 1;
  for (int i = 8; i < 60; ++i) {
    uint8_t w[4];
    std::memcpy(w, rk_ + (i - 1) * 4, 4);
    if (i % 8 == 0) {
      const uint8_t first = w[0];
      w[0] = static_cast<uint8_t>(t.sbox[w[1]] ^ rcon);
      w[1] = t.sbox[w[2]];
      w[2] = t.sbox[w[3]];
      w[3] = t.sbox[first];
      rcon = Xtime(rcon);
    } else if (i % 8 == 4) {
      for (auto &b : w) b = t.sbox[b];
    }
    for (int k = 0; k < 4; ++k) rk_[i * 4 + k] = static_cast<uint8_t>(rk_[(i - 8) * 4 + k] ^ w[k]);
  }
}

void Aes256::DecryptBlock(uint8_t s[16]) const {
  const Tables &t = T();
  auto addKey = [&](int round) {
    for (int i = 0; i < 16; ++i) s[i] ^= rk_[round * 16 + i];
  };
  addKey(14);
  for (int round = 13; round >= 0; --round) {
    // InvShiftRows + InvSubBytes
    uint8_t c[16];
    for (int col = 0; col < 4; ++col)
      for (int row = 0; row < 4; ++row) c[((col + row) % 4) * 4 + row] = t.inv[s[col * 4 + row]];
    std::memcpy(s, c, 16);
    addKey(round);
    if (round == 0) break;
    for (int col = 0; col < 4; ++col) {
      uint8_t *a = s + col * 4;
      const uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
      a[0] = Mul(a0, 14) ^ Mul(a1, 11) ^ Mul(a2, 13) ^ Mul(a3, 9);
      a[1] = Mul(a0, 9) ^ Mul(a1, 14) ^ Mul(a2, 11) ^ Mul(a3, 13);
      a[2] = Mul(a0, 13) ^ Mul(a1, 9) ^ Mul(a2, 14) ^ Mul(a3, 11);
      a[3] = Mul(a0, 11) ^ Mul(a1, 13) ^ Mul(a2, 9) ^ Mul(a3, 14);
    }
  }
}

void Aes256::DecryptEcb(uint8_t *data, size_t size) const {
  for (size_t off = 0; off + 16 <= size; off += 16) DecryptBlock(data + off);
}

} // namespace trackpkg
