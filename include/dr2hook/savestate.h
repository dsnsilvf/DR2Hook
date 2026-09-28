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

private:
  static bool s_initialized;
  static bool s_hasSavedState;
  static CarState s_savedState;
};

} // namespace dr2hook

using dr2hook::SavestateManager;
