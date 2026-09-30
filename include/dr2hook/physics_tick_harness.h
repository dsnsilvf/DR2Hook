#pragma once

#include "dr2hook/common.h"
#include <cstddef>
#include <cstdint>

namespace dr2hook {

enum class PhysicsHarnessBoundary : uint8_t {
  B1_TickStart = 0,
  M1_BeforeIntegrator = 1,
  M2_AfterIntegrator = 2,
  M3_BeforeCommit = 3,
  B2_AfterCommit = 4,
  H1_FrameLoopReturn = 5,
  H2_FrameLoopEntry = 6,
  H3_PhysicsStep = 7,
  H4_PreTick = 8,
  H5_EndStep = 9,
  H6_IntegratorReturnFilter = 10,
  Log_CommitAuxA = 11,
  Log_CommitAuxB = 12,
};

enum class PhysicsNativeCallKind : uint8_t {
  SetTransform = 0,
  SetLinVel = 1,
  SetAngVel = 2,
  Commit = 3,
};

class PhysicsTickHarness {
public:
  static bool LoadConfiguration();
  static bool IsInstrumentationEnabled();
  static bool AreWritesEnabled();
  static bool IsSelfTestMode();
  static bool IsExperimentalNativeEnabled();

  static bool TryInstall(uintptr_t gameModuleBase);
  static void Shutdown();

  static uint64_t GetTickCounter();

  static bool ScheduleWrite(uint64_t tick, PhysicsHarnessBoundary boundary,
                            uint32_t rigOffset, const void *bytes,
                            size_t byteCount);

  static bool ScheduleExperimentalNative(uint64_t tick,
                                         PhysicsHarnessBoundary boundary,
                                         PhysicsNativeCallKind kind);

  static bool VerifyPrologue(const void *target, const uint8_t *expected,
                             size_t expectedLength);

  static uintptr_t ResolvePlayerRig(uintptr_t gameModuleBase);
};

} // namespace dr2hook

using dr2hook::PhysicsHarnessBoundary;
using dr2hook::PhysicsNativeCallKind;
using dr2hook::PhysicsTickHarness;
