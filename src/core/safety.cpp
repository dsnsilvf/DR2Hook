#include "dr2hook/safety.h"
#include "dr2hook/logger.h"

namespace dr2hook {

MemoryScanner *SafetyGuard::s_scanner = nullptr;
uintptr_t SafetyGuard::s_sessionModeAddress = 0;
bool SafetyGuard::s_permissiveMode = false;

void SafetyGuard::Configure(MemoryScanner *scanner,
                            uintptr_t sessionModeAddress) {
  s_scanner = scanner;
  s_sessionModeAddress = sessionModeAddress;
}

void SafetyGuard::SetPermissiveMode(bool enabled) {
  s_permissiveMode = enabled;
}

bool SafetyGuard::IsPermissiveMode() { return s_permissiveMode; }

bool SafetyGuard::IsModeAllowedForPractice(GameSessionMode mode) {
  switch (mode) {
  case GameSessionMode::DirtFish:
  case GameSessionMode::TimeTrialOffline:
  case GameSessionMode::CustomOffline:
    return true;
  case GameSessionMode::DailyChallenge:
  case GameSessionMode::WeeklyChallenge:
  case GameSessionMode::ClubOnline:
  case GameSessionMode::CareerOnline:
  case GameSessionMode::Unknown:
  default:
    return false;
  }
}

GameSessionMode SafetyGuard::EvaluateCurrentMode() {
  if (s_scanner == nullptr || s_sessionModeAddress == 0) {
    return GameSessionMode::Unknown;
  }

  IMemoryAccessor *accessor = s_scanner->GetAccessor();
  if (accessor == nullptr || !accessor->IsValidAddress(s_sessionModeAddress)) {
    return GameSessionMode::Unknown;
  }

  uint32_t rawMode = 0;
  if (!accessor->Read(s_sessionModeAddress, &rawMode, sizeof(rawMode))) {
    return GameSessionMode::Unknown;
  }

  switch (static_cast<GameSessionMode>(rawMode)) {
  case GameSessionMode::DirtFish:
    return GameSessionMode::DirtFish;
  case GameSessionMode::TimeTrialOffline:
    return GameSessionMode::TimeTrialOffline;
  case GameSessionMode::CustomOffline:
    return GameSessionMode::CustomOffline;
  case GameSessionMode::DailyChallenge:
    return GameSessionMode::DailyChallenge;
  case GameSessionMode::WeeklyChallenge:
    return GameSessionMode::WeeklyChallenge;
  case GameSessionMode::ClubOnline:
    return GameSessionMode::ClubOnline;
  case GameSessionMode::CareerOnline:
    return GameSessionMode::CareerOnline;
  default:
    return GameSessionMode::Unknown;
  }
}

bool SafetyGuard::CanWriteState() {
  if (s_permissiveMode) {
    return true;
  }
  const GameSessionMode currentMode = EvaluateCurrentMode();
  return IsModeAllowedForPractice(currentMode);
}

} // namespace dr2hook
