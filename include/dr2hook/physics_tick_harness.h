#pragma once

#include "dr2hook/common.h"
#include "dr2hook/physics_native_api.h"
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
  H3_PhysicsStep_Entry = 7,
  H3_PhysicsStep_Return = 8,
  H4_PreTick_Entry = 9,
  H4_PreTick_Return = 10,
  H5_EndStep_Entry = 11,
  H5_EndStep_Return = 12,
  H6_IntegratorReturnFilter = 13,
  Log_Commit_74D190 = 14,
  Log_Commit_73A070 = 15,
  Log_Commit_73B620 = 16,
};

static_assert(static_cast<uint8_t>(PhysicsHarnessBoundary::Log_Commit_73B620) < 20,
              "s_boundaryFireCounts size in physics_tick_harness.cpp");

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
  static uint64_t GetStepCounter();

  static bool ScheduleWrite(uint64_t tick, PhysicsHarnessBoundary boundary,
                            uint32_t rigOffset, const void *bytes,
                            size_t byteCount);

  static bool ScheduleExperimentalNative(
      uint64_t tick, PhysicsHarnessBoundary boundary,
      PhysicsNativeCallKind kind,
      const physics_native::CallParams &params = {});

  static uintptr_t ResolvePlayerRig(uintptr_t gameModuleBase);

#if defined(DR2HOOK_PHYSICS_HARNESS_TESTING)
  static void TestingSetInstrumentationAndWrites(bool instrumentation,
                                                 bool writes);
  static void TestingSetExperimentalNative(bool enabled);
  static void TestingClearScheduledWrite();
  static void TestingClearScheduledNative();
  static void TestingSetPracticeKeysForTick(uint64_t tick, bool f5, bool f6,
                                            bool f7);
  static void TestingSetSelfTestMode(bool enabled);
  static void TestingSimulateInStageTick();
  static uint64_t TestingGetReentrancyCount();
  static uint64_t TestingGetShamWriteCount();
  static bool TestingEvaluateSelfTestPass();
  static void TestingSeedRequiredSelfTestHooks();
  static void TestingExecuteScheduledWriteIfDue(PhysicsHarnessBoundary boundary);
#endif
};

#if defined(DR2HOOK_PHYSICS_HARNESS_TESTING)
// Executa Invoke() com stubs (unit tests, memória simulada).
bool PhysicsTickHarnessTestingInvokeNative(PhysicsNativeCallKind kind,
                                           void *rig,
                                           const physics_native::CallParams &params,
                                           const physics_native::NativeEntrypoints &stubs);
#endif

} // namespace dr2hook

using dr2hook::PhysicsHarnessBoundary;
using dr2hook::PhysicsNativeCallKind;
using dr2hook::PhysicsTickHarness;
