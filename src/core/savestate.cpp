#include "dr2hook/savestate.h"
#include "dr2hook/logger.h"
#include "dr2hook/safety.h"

namespace dr2hook {

bool SavestateManager::s_initialized = false;
bool SavestateManager::s_hasSavedState = false;
CarState SavestateManager::s_savedState{};

bool SavestateManager::Initialize() {
  s_hasSavedState = false;
  s_savedState = CarState{};
  s_initialized = true;
  return true;
}

void SavestateManager::Shutdown() {
  s_hasSavedState = false;
  s_savedState = CarState{};
  s_initialized = false;
}

bool SavestateManager::HasSavedState() { return s_hasSavedState; }

const CarState &SavestateManager::GetSavedState() { return s_savedState; }

void SavestateManager::OnKeyAction(UINT vkCode, bool isDown) {
  if (!s_initialized || !isDown) {
    return;
  }

  constexpr UINT VK_KEY_F5 = 0x74;
  constexpr UINT VK_KEY_F6 = 0x75;

  if (vkCode == VK_KEY_F5) {
    CarState currentState{};
    if (Player::CaptureState(currentState)) {
      s_savedState = currentState;
      s_hasSavedState = true;
      Logger::Info("Checkpoint gravado com sucesso.");
    } else {
      Logger::Warn("Falha ao capturar estado do veiculo para checkpoint.");
    }
  } else if (vkCode == VK_KEY_F6) {
    if (!SafetyGuard::CanWriteState()) {
      Logger::Warn("Restauração de savestate bloqueada pelo SafetyGuard em "
                   "modo restrito/não confirmado.");
      return;
    }

    if (!s_hasSavedState) {
      Logger::Warn("Nenhum checkpoint salvo disponivel para restauracao.");
      return;
    }

    if (Player::ApplyState(s_savedState)) {
      Logger::Info("Checkpoint restaurado com sucesso.");
    } else {
      Logger::Warn("Falha ao restaurar checkpoint.");
    }
  }
}

} // namespace dr2hook
