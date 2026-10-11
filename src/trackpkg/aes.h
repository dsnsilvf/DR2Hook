#pragma once

#include <cstddef>
#include <cstdint>

namespace trackpkg {

// AES-256 só para decifrar em ECB, como os blocos dos pacotes NeFS do jogo. Bytes finais fora de
// um bloco de 16 ficam como estão.
class Aes256 {
public:
  explicit Aes256(const uint8_t key[32]);
  void DecryptBlock(uint8_t block[16]) const;
  void DecryptEcb(uint8_t *data, size_t size) const;

private:
  uint8_t rk_[240];
};

} // namespace trackpkg
