#pragma once

#include "dr2hook/memory.h"
#include <cstdint>

namespace dr2hook {

enum class GameSessionMode : uint32_t {
  Unknown = 0,
  DirtFish = 1,
  TimeTrialOffline = 2,
  CustomOffline = 3,
  DailyChallenge = 10,
  WeeklyChallenge = 11,
  ClubOnline = 12,
  CareerOnline = 13
};

class SafetyGuard {
public:
  static void Configure(MemoryScanner *scanner, uintptr_t sessionModeAddress);
  static GameSessionMode EvaluateCurrentMode();
  static bool CanWriteState();
  static bool IsModeAllowedForPractice(GameSessionMode mode);
  static void SetPermissiveMode(bool enabled);
  static bool IsPermissiveMode();

private:
  static MemoryScanner *s_scanner;
  static uintptr_t s_sessionModeAddress;
  static bool s_permissiveMode;
};

} // namespace dr2hook

using dr2hook::GameSessionMode;
using dr2hook::SafetyGuard;
