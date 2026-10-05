#pragma once

#include <atomic>
#include <cstdint>

namespace dr2hook {

// Rastreio passivo dos carros fantasma para descobrir como o jogo os pausa
// (docs/reverse_engineering/investigations/ghost-pause-trace-prompt.md).
// So liga se existir dr2hook_ghost_trace.txt ao lado do exe; grava
// dr2hook_ghost_trace.log. Nenhum hook altera o comportamento do jogo.
class GhostTrace {
public:
  static bool Install(uintptr_t gameBase);
  static void Shutdown();

  // Chamado por GhostLab depois de EvaluateGhostState (thread do jogo).
  static void OnEvaluate(const uint8_t *owner, const void *time, int result,
                         const uint8_t *out);

  static bool Enabled() { return s_enabled.load(std::memory_order_relaxed); }

private:
  static std::atomic<bool> s_enabled;
};

} // namespace dr2hook
