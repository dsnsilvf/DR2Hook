#pragma once

#include <cstdint>

namespace dr2hook {

// Reinicio da especial sem rede: tira o "CONTATANDO SERVIDOR", o popup
// "FALHA DE CONEXAO" e a espera de ate 5 s que o NetworkGuard provoca ao
// bloquear a RaceNet (docs/reverse_engineering/network_guard.md). Hooks no
// core (F8).
class NetDialog {
public:
  static bool Install(uintptr_t gameBase);
  static void Shutdown();
};

} // namespace dr2hook
