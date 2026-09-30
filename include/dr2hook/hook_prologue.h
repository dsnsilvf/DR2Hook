#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dr2hook {

// Política compartilhada com hooks de UI/game: não instalar MinHook se o prólogo
// no endereço alvo não coincidir com o build esperado do dirtrally2.exe.
inline bool VerifyHookPrologue(const void *target, const uint8_t *expected,
                               size_t expectedLength) {
  if (target == nullptr || expected == nullptr || expectedLength == 0) {
    return false;
  }
  const auto *actual = static_cast<const uint8_t *>(target);
  return std::memcmp(actual, expected, expectedLength) == 0;
}

} // namespace dr2hook
