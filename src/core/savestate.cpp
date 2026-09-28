#include "dr2hook/savestate.h"
#include "dr2hook/logger.h"
#include "dr2hook/safety.h"

namespace dr2hook {

bool SavestateManager::s_initialized = false;
bool SavestateManager::s_hasSavedState = false;
CarState SavestateManager::s_savedState{};
RestoreMode SavestateManager::s_restoreMode = RestoreMode::Normal;

bool SavestateManager::Initialize() {
  s_hasSavedState = false;
  s_savedState = CarState{};
  s_restoreMode = RestoreMode::Normal;
  s_initialized = true;
  return true;
}

void SavestateManager::Shutdown() {
  s_hasSavedState = false;
  s_savedState = CarState{};
  s_restoreMode = RestoreMode::Normal;
  s_initialized = false;
}

bool SavestateManager::HasSavedState() { return s_hasSavedState; }

const CarState &SavestateManager::GetSavedState() { return s_savedState; }

void SavestateManager::SetRestoreMode(RestoreMode mode) {
  s_restoreMode = mode;
  Logger::Info(mode == RestoreMode::WithMomentum
                   ? "SavestateManager: modo de restauracao alterado para "
                     "Com Momentum."
                   : "SavestateManager: modo de restauracao alterado para "
                     "Normal (Parado).");
}

RestoreMode SavestateManager::GetRestoreMode() { return s_restoreMode; }

bool SavestateManager::RestoreCheckpoint(RestoreMode mode) {
  if (!s_initialized) {
    return false;
  }

  if (!SafetyGuard::CanWriteState()) {
    Logger::Warn("Restauração de savestate bloqueada pelo SafetyGuard em "
                 "modo restrito/não confirmado.");
    return false;
  }

  if (!s_hasSavedState) {
    Logger::Warn("Nenhum checkpoint salvo disponivel para restauracao.");
    return false;
  }

  if (Player::ApplyState(s_savedState, mode)) {
    Logger::Info(mode == RestoreMode::WithMomentum
                     ? "Checkpoint restaurado com momentum com sucesso."
                     : "Checkpoint restaurado normalmente com sucesso.");
    return true;
  } else {
    Logger::Warn("Falha ao restaurar checkpoint.");
    return false;
  }
}

bool SavestateManager::RestoreCheckpoint() {
  return RestoreCheckpoint(s_restoreMode);
}

void SavestateManager::OnKeyAction(UINT vkCode, bool isDown) {
  if (!s_initialized || !isDown) {
    return;
  }

  constexpr UINT VK_KEY_F5 = 0x74;
  constexpr UINT VK_KEY_F6 = 0x75;
  constexpr UINT VK_KEY_F7 = 0x76;

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
    RestoreCheckpoint(s_restoreMode);
  } else if (vkCode == VK_KEY_F7) {
    RestoreCheckpoint(RestoreMode::WithMomentum);
  }
}

} // namespace dr2hook
