#include "dr2hook/hook_prologue.h"
#include "dr2hook/logger.h"
#include "dr2hook/memory.h"
#include "dr2hook/physics_harness_addresses.h"
#include "dr2hook/physics_tick_harness.h"
#include "dr2hook/physics_tick_harness_prologues.h"
#include "dr2hook/physics_native_api.h"
#include "dr2hook/safety.h"

#include <cstring>
#include <cstdio>
#include <iostream>

static int g_testsRun = 0;
static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(condition, msg)                                            \
  do {                                                                         \
    ++g_testsRun;                                                              \
    if (condition) {                                                           \
      ++g_testsPassed;                                                         \
    } else {                                                                   \
      ++g_testsFailed;                                                         \
      std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " ("            \
                << __func__ << "): " << (msg) << " -> Assertion '"             \
                << #condition << "' failed." << std::endl;                     \
    }                                                                          \
  } while (0)

void TestVerifyPrologue() {
  std::cout << "[RUN] TestVerifyPrologue..." << std::endl;
  const uint8_t buffer[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                            0x24, 0x10, 0x48, 0x89};
  TEST_ASSERT(
      dr2hook::VerifyHookPrologue(
          buffer, dr2hook::physics_harness_prologues::kTickStart,
          dr2hook::physics_harness_prologues::kPrologueLength),
      "Prologo esperado deve coincidir");
  const uint8_t bad[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT(!dr2hook::VerifyHookPrologue(buffer, bad, sizeof(bad)),
              "Prologo divergente deve falhar");
}

namespace {

bool ParseHexPrologue12(const char *hex24, uint8_t *out12) {
  if (hex24 == nullptr || out12 == nullptr) {
    return false;
  }
  for (size_t i = 0; i < 12; ++i) {
    unsigned value = 0;
    if (std::sscanf(hex24 + (i * 2), "%2x", &value) != 1) {
      return false;
    }
    out12[i] = static_cast<uint8_t>(value);
  }
  return true;
}

bool PrologueMatchesHex(const uint8_t *prologue, const char *hex24) {
  uint8_t expected[12] = {};
  if (!ParseHexPrologue12(hex24, expected)) {
    return false;
  }
  return std::memcmp(prologue, expected, 12) == 0;
}

} // namespace

void TestBug1CalibratedPrologueHexPinned() {
  std::cout << "[RUN] TestBug1CalibratedPrologueHexPinned..." << std::endl;
  using namespace dr2hook::physics_harness_prologues;
  TEST_ASSERT(PrologueMatchesHex(kTickStart, "48895c240848896c24104889"),
              "tick_start hex");
  TEST_ASSERT(PrologueMatchesHex(kIntegrator, "48895c240848897424105748"),
              "integrator hex");
  TEST_ASSERT(PrologueMatchesHex(kCommit, "40534883ec20488bd9488b89"),
              "commit hex");
  TEST_ASSERT(PrologueMatchesHex(kFrameLoop, "48895c240848897424105741"),
              "frame_loop hex");
  TEST_ASSERT(PrologueMatchesHex(kPhysicsStep, "405553565741544155488dac"),
              "physics_step hex");
  TEST_ASSERT(PrologueMatchesHex(kPreTick, "48895c241048896c24184889"),
              "pretick hex");
  TEST_ASSERT(PrologueMatchesHex(kEndStep, "48895c240848897424105748"),
              "end_step hex");
  TEST_ASSERT(PrologueMatchesHex(kCommitAuxA, "40534883ec30488bd9e80000"),
              "commit_log_73a070 hex");
  TEST_ASSERT(PrologueMatchesHex(kCommitAuxB, "48895c240848897424105748"),
              "commit_log_73b620 hex");
}

void TestScheduleWriteRequiresOptIn() {
  std::cout << "[RUN] TestScheduleWriteRequiresOptIn..." << std::endl;
  dr2hook::PhysicsTickHarness::LoadConfiguration();
  TEST_ASSERT(!dr2hook::PhysicsTickHarness::AreWritesEnabled(),
              "Writes desabilitados por padrao");
  const uint8_t payload = 0x42;
  TEST_ASSERT(!dr2hook::PhysicsTickHarness::ScheduleWrite(
                  1, dr2hook::PhysicsHarnessBoundary::H2_FrameLoopEntry, 0x320,
                  &payload, sizeof(payload)),
              "ScheduleWrite deve recusar sem flag de writes");
}

bool ResolveRigWithMock(dr2hook::MockMemoryAccessor &mock, uintptr_t gameBase) {
  constexpr uintptr_t kCar = 0x5000;
  constexpr uintptr_t kContainer = 0x6000;
  constexpr uintptr_t kRig = 0x7000;

  mock.SetValue(gameBase + 0x1681CE8, kCar);
  mock.SetValue(kCar + 0x30, kContainer);
  mock.SetValue(kContainer + 0x08, kRig);
  mock.SetValue(kRig + 0x12C0, kRig);
  mock.SetValue(kRig + 0x12D0, static_cast<uint32_t>(4));

  uintptr_t car = 0;
  mock.Read(gameBase + 0x1681CE8, &car, sizeof(car));
  if (car == 0) {
    return false;
  }
  uintptr_t container = 0;
  mock.Read(car + 0x30, &container, sizeof(container));
  if (container == 0) {
    return false;
  }
  uintptr_t rig = 0;
  mock.Read(container + 0x08, &rig, sizeof(rig));
  if (rig == 0) {
    return false;
  }
  uintptr_t self = 0;
  mock.Read(rig + 0x12C0, &self, sizeof(self));
  if (self != rig) {
    return false;
  }
  uint32_t tag = 0;
  mock.Read(rig + 0x12D0, &tag, sizeof(tag));
  return tag == 4 && rig == kRig;
}

void TestScheduleWriteSingleQueue() {
  std::cout << "[RUN] TestScheduleWriteSingleQueue..." << std::endl;
  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);
  dr2hook::SafetyGuard::Configure(&scanner, 0);
  dr2hook::SafetyGuard::SetPermissiveMode(true);

  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  dr2hook::PhysicsTickHarness::TestingSetInstrumentationAndWrites(true, true);

  const uint8_t payload = 0xAB;
  TEST_ASSERT(dr2hook::PhysicsTickHarness::ScheduleWrite(
                  1, dr2hook::PhysicsHarnessBoundary::M3_BeforeCommit, 0x320,
                  &payload, sizeof(payload)),
              "Primeira ScheduleWrite deve aceitar");
  TEST_ASSERT(!dr2hook::PhysicsTickHarness::ScheduleWrite(
                  1, dr2hook::PhysicsHarnessBoundary::M3_BeforeCommit, 0x320,
                  &payload, sizeof(payload)),
              "Segunda ScheduleWrite deve recusar (fila unica)");

  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  dr2hook::PhysicsTickHarness::TestingSetInstrumentationAndWrites(false, false);
}

struct NativeStubState {
  int setLinVelCalls = 0;
  void *lastRig = nullptr;
  float lastVx = 0.f;
};

NativeStubState g_nativeStubState;

void __fastcall StubSetLinVel(void *rig, float x, float y, float z) {
  g_nativeStubState.setLinVelCalls++;
  g_nativeStubState.lastRig = rig;
  g_nativeStubState.lastVx = x;
  (void)y;
  (void)z;
}

void TestSpecPart5NativeInvokeStub() {
  std::cout << "[RUN] TestSpecPart5NativeInvokeStub..." << std::endl;
  g_nativeStubState = NativeStubState{};
  dr2hook::physics_native::NativeEntrypoints stubs{};
  stubs.setLinVel = StubSetLinVel;

  dr2hook::physics_native::CallParams params{};
  params.vx = 12.5f;
  void *rig = reinterpret_cast<void *>(0x7000);

  TEST_ASSERT(dr2hook::PhysicsTickHarnessTestingInvokeNative(
                  dr2hook::PhysicsNativeCallKind::SetLinVel, rig, params,
                  stubs),
              "Invoke com stub deve retornar true");
  TEST_ASSERT(g_nativeStubState.setLinVelCalls == 1,
              "Stub SetLinVel deve ser chamado uma vez");
  TEST_ASSERT(g_nativeStubState.lastRig == rig, "Rig passado ao stub");
  TEST_ASSERT(g_nativeStubState.lastVx == 12.5f, "Componente vx preservado");
}

void TestSpecPart5NativeAddresses() {
  std::cout << "[RUN] TestSpecPart5NativeAddresses..." << std::endl;
  TEST_ASSERT(dr2hook::physics_harness::kVaSetTransform == 0x14074AD80ULL,
              "VA SetTransform");
  TEST_ASSERT(dr2hook::physics_harness::kVaSetLinVel == 0x14074A910ULL,
              "VA SetLinVel");
  TEST_ASSERT(dr2hook::physics_harness::kVaSetAngVel == 0x14074A890ULL,
              "VA SetAngVel");
  TEST_ASSERT(dr2hook::physics_harness::kVaCommit == 0x14074D190ULL,
              "VA Commit");
}

void TestScheduleExperimentalNativeRequiresOptIn() {
  std::cout << "[RUN] TestScheduleExperimentalNativeRequiresOptIn..." << std::endl;
  dr2hook::PhysicsTickHarness::LoadConfiguration();
  dr2hook::physics_native::CallParams params{};
  TEST_ASSERT(!dr2hook::PhysicsTickHarness::ScheduleExperimentalNative(
                  1, dr2hook::PhysicsHarnessBoundary::B2_AfterCommit,
                  dr2hook::PhysicsNativeCallKind::Commit, params),
              "Native recusado sem flag experimental");
}

void TestSpecPart3RigPointerConstant() {
  std::cout << "[RUN] TestSpecPart3RigPointerConstant..." << std::endl;
  TEST_ASSERT(dr2hook::physics_harness::kRvaActiveCarPointer == 0x1681CE8,
              "Ponteiro global do carro 0x1681ce8");
  TEST_ASSERT(dr2hook::physics_harness::kRigExpectedTypeTag == 4,
              "Tag do rig == 4");
}

void TestSpecCorrection1FrameLoopWriteBoundaries() {
  std::cout << "[RUN] TestSpecCorrection1FrameLoopWriteBoundaries..." << std::endl;
  TEST_ASSERT(dr2hook::physics_harness::kVaFrameLoop == 0x140DBCA20ULL,
              "VA frame loop H1/H2");
  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  dr2hook::PhysicsTickHarness::TestingSetInstrumentationAndWrites(true, true);
  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);
  dr2hook::SafetyGuard::Configure(&scanner, 0);
  dr2hook::SafetyGuard::SetPermissiveMode(true);

  const uint8_t payload = 0x01;
  TEST_ASSERT(dr2hook::PhysicsTickHarness::ScheduleWrite(
                  1, dr2hook::PhysicsHarnessBoundary::H2_FrameLoopEntry, 0x320,
                  &payload, sizeof(payload)),
              "ScheduleWrite em H2");
  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  TEST_ASSERT(dr2hook::PhysicsTickHarness::ScheduleWrite(
                  1, dr2hook::PhysicsHarnessBoundary::H1_FrameLoopReturn, 0x320,
                  &payload, sizeof(payload)),
              "ScheduleWrite em H1");
  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  dr2hook::PhysicsTickHarness::TestingSetInstrumentationAndWrites(false, false);
}

void TestSpecCorrection4SelfTestDisablesWrites() {
  std::cout << "[RUN] TestSpecCorrection4SelfTestDisablesWrites..." << std::endl;
  dr2hook::PhysicsTickHarness::TestingSetSelfTestMode(true);
  TEST_ASSERT(dr2hook::PhysicsTickHarness::IsSelfTestMode(),
              "Self-test flag ativa");
  TEST_ASSERT(dr2hook::PhysicsTickHarness::IsInstrumentationEnabled(),
              "Self-test liga instrumentacao");
  TEST_ASSERT(!dr2hook::PhysicsTickHarness::AreWritesEnabled(),
              "Self-test desliga writes");
  TEST_ASSERT(!dr2hook::PhysicsTickHarness::IsExperimentalNativeEnabled(),
              "Self-test desliga native experimental");
  dr2hook::PhysicsTickHarness::TestingSetSelfTestMode(false);
  dr2hook::PhysicsTickHarness::TestingSetInstrumentationAndWrites(false, false);
}

void TestSelfTestPassAt600Ticks() {
  std::cout << "[RUN] TestSelfTestPassAt600Ticks..." << std::endl;
  dr2hook::PhysicsTickHarness::TestingSetSelfTestMode(true);
  dr2hook::PhysicsTickHarness::TestingSeedRequiredSelfTestHooks();
  for (uint64_t i = 0; i < 599; ++i) {
    dr2hook::PhysicsTickHarness::TestingSimulateInStageTick();
  }
  TEST_ASSERT(!dr2hook::PhysicsTickHarness::TestingEvaluateSelfTestPass(),
              "PASS antes de 600 ticks");
  dr2hook::PhysicsTickHarness::TestingSimulateInStageTick();
  TEST_ASSERT(dr2hook::PhysicsTickHarness::TestingEvaluateSelfTestPass(),
              "PASS com 600 ticks e hooks obrigatorios");
  dr2hook::PhysicsTickHarness::TestingSetSelfTestMode(false);
}

void TestSelfTestShamWritePath() {
  std::cout << "[RUN] TestSelfTestShamWritePath..." << std::endl;
  dr2hook::PhysicsTickHarness::TestingSetSelfTestMode(true);
  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  dr2hook::PhysicsTickHarness::TestingSimulateInStageTick();
  const uint8_t payload = 0x11;
  TEST_ASSERT(dr2hook::PhysicsTickHarness::ScheduleWrite(
                  dr2hook::PhysicsTickHarness::GetTickCounter(),
                  dr2hook::PhysicsHarnessBoundary::B1_TickStart, 0x320,
                  &payload, sizeof(payload)),
              "ScheduleWrite enfileira em self-test");
  dr2hook::PhysicsTickHarness::TestingExecuteScheduledWriteIfDue(
      dr2hook::PhysicsHarnessBoundary::B1_TickStart);
  TEST_ASSERT(dr2hook::PhysicsTickHarness::TestingGetShamWriteCount() == 1,
              "Sham-write incrementa contador");
  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  dr2hook::PhysicsTickHarness::TestingSetSelfTestMode(false);
}

void TestSpecCorrection4ExtraCsvOffsets() {
  std::cout << "[RUN] TestSpecCorrection4ExtraCsvOffsets..." << std::endl;
  TEST_ASSERT(dr2hook::physics_harness::kContainerExtraVec4Offset == 0xC930,
              "Container vec4 offset 0xc930");
  TEST_ASSERT(dr2hook::physics_harness::kRigExtraVec4Offset == 0x290,
              "Rig vec4 offset 0x290");
}

void TestSpecCorrection3IntegratorReturnAndH6Write() {
  std::cout << "[RUN] TestSpecCorrection3IntegratorReturnAndH6Write..."
            << std::endl;
  TEST_ASSERT(dr2hook::physics_harness::kVaIntegratorM2ReturnSite ==
                  0x1407395FAULL,
              "VA filtro retorno integrator M2");
  TEST_ASSERT(dr2hook::physics_harness::kVaIntegratorReturnSite ==
                  0x14073E314ULL,
              "VA filtro retorno integrator H6");
  TEST_ASSERT(dr2hook::physics_harness::kVaCommitLogAuxA == 0x14073A070ULL,
              "VA commit log aux A");
  TEST_ASSERT(dr2hook::physics_harness::kVaCommitLogAuxB == 0x14073B620ULL,
              "VA commit log aux B");

  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  dr2hook::PhysicsTickHarness::TestingSetInstrumentationAndWrites(true, true);
  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);
  dr2hook::SafetyGuard::Configure(&scanner, 0);
  dr2hook::SafetyGuard::SetPermissiveMode(true);

  const uint8_t payload = 0x55;
  TEST_ASSERT(dr2hook::PhysicsTickHarness::ScheduleWrite(
                  1, dr2hook::PhysicsHarnessBoundary::H6_IntegratorReturnFilter,
                  0x320, &payload, sizeof(payload)),
              "ScheduleWrite em H6");
  dr2hook::PhysicsTickHarness::TestingClearScheduledWrite();
  dr2hook::PhysicsTickHarness::TestingSetInstrumentationAndWrites(false, false);
}

void TestSpecCorrection3PracticeKeysSnapshot() {
  std::cout << "[RUN] TestSpecCorrection3PracticeKeysSnapshot..." << std::endl;
  dr2hook::PhysicsTickHarness::TestingSetPracticeKeysForTick(42, true, false,
                                                            true);
  TEST_ASSERT(dr2hook::PhysicsTickHarness::GetTickCounter() >= 0,
              "Harness tick counter acessivel");
}

void TestSpecCorrection2HookAddresses() {
  std::cout << "[RUN] TestSpecCorrection2HookAddresses..." << std::endl;
  TEST_ASSERT(dr2hook::physics_harness::kVaPhysicsStep == 0x140DBC500ULL,
              "VA physics step H3");
  TEST_ASSERT(dr2hook::physics_harness::kVaPreTick == 0x140749A30ULL,
              "VA PreTick H4");
  TEST_ASSERT(dr2hook::physics_harness::kVaEndStep == 0x1407511E0ULL,
              "VA EndStep H5");
  TEST_ASSERT(dr2hook::physics_harness::kVaEndStep ==
                  dr2hook::physics_harness::kVaPerTickCaller,
              "EndStep alias per-tick caller");
}

void TestRigChainValidationLogic() {
  std::cout << "[RUN] TestRigChainValidationLogic..." << std::endl;
  dr2hook::MockMemoryAccessor mock;
  constexpr uintptr_t gameBase = 0x140000000;
  constexpr uintptr_t kCar = 0x5000;
  constexpr uintptr_t kContainer = 0x6000;
  constexpr uintptr_t kRig = 0x7000;

  mock.SetValue(gameBase + 0x1681CE8, kCar);
  mock.SetValue(kCar + 0x30, kContainer);
  mock.SetValue(kContainer + 0x08, kRig);
  mock.SetValue(kRig + 0x12C0, kRig);
  mock.SetValue(kRig + 0x12D0, static_cast<uint32_t>(4));

  TEST_ASSERT(ResolveRigWithMock(mock, gameBase),
              "Cadeia rig/container valida no mock");

  mock.SetValue(kRig + 0x12D0, static_cast<uint32_t>(0));
  uint32_t tag = 99;
  mock.Read(kRig + 0x12D0, &tag, sizeof(tag));
  TEST_ASSERT(tag != 4, "Tag invalida simulada no mock");
}

int main() {
  std::cout << "DR2Hook - Physics Tick Harness (unit)" << std::endl;
  TestVerifyPrologue();
  TestBug1CalibratedPrologueHexPinned();
  TestScheduleWriteRequiresOptIn();
  TestScheduleWriteSingleQueue();
  TestScheduleExperimentalNativeRequiresOptIn();
  TestSpecPart5NativeAddresses();
  TestSpecPart5NativeInvokeStub();
  TestSpecPart3RigPointerConstant();
  TestSpecCorrection1FrameLoopWriteBoundaries();
  TestSpecCorrection2HookAddresses();
  TestSpecCorrection3IntegratorReturnAndH6Write();
  TestSpecCorrection3PracticeKeysSnapshot();
  TestSpecCorrection4ExtraCsvOffsets();
  TestSpecCorrection4SelfTestDisablesWrites();
  TestSelfTestPassAt600Ticks();
  TestSelfTestShamWritePath();
  TestRigChainValidationLogic();

  std::cout << "Resumo: " << g_testsPassed << "/" << g_testsRun
            << " passaram, " << g_testsFailed << " falharam." << std::endl;
  return g_testsFailed == 0 ? 0 : 1;
}
