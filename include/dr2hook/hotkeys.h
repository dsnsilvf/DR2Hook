#pragma once

#include <atomic>

namespace dr2hook {

// Teclas de depuracao que o Debug Mode pode desligar (Debug.setHotkey no Lua).
// Desligada, a tecla fisica segue para o jogo; o comando remoto `key` continua valendo.
struct Hotkeys {
  static inline std::atomic<bool> freeCamera{true}; // F9
  static inline std::atomic<bool> instaCrash{true}; // F11
};

} // namespace dr2hook
