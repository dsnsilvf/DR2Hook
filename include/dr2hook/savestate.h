#pragma once

#include "dr2hook/common.h"
#include "dr2hook/player.h"

namespace dr2hook {

class SavestateManager {
public:
  static bool Initialize();
  static void Shutdown();
  static void OnKeyAction(UINT vkCode, bool isDown);
  static bool HasSavedState();
  static const CarState &GetSavedState();

  static void SetRestoreMode(RestoreMode mode);
  static RestoreMode GetRestoreMode();
  static bool RestoreCheckpoint(RestoreMode mode);
  static bool RestoreCheckpoint();

private:
  static bool s_initialized;
  static bool s_hasSavedState;
  static CarState s_savedState;
  static RestoreMode s_restoreMode;
};

} // namespace dr2hook

using dr2hook::SavestateManager;
