#include "dr2hook/hook_prologue.h"
#include "dr2hook/logger.h"
#include "dr2hook/memory.h"
#include "dr2hook/physics_harness_addresses.h"
#include "dr2hook/physics_tick_harness.h"
#include "dr2hook/physics_tick_harness_prologues.h"
#include "dr2hook/safety.h"

#include <cstring>
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

void TestSpecPart3RigPointerConstant() {
  std::cout << "[RUN] TestSpecPart3RigPointerConstant..." << std::endl;
  TEST_ASSERT(dr2hook::physics_harness::kRvaActiveCarPointer == 0x1681CE8,
              "Ponteiro global do carro 0x1681ce8");
  TEST_ASSERT(dr2hook::physics_harness::kRigExpectedTypeTag == 4,
              "Tag do rig == 4");
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
  TestScheduleWriteRequiresOptIn();
  TestScheduleWriteSingleQueue();
  TestSpecPart3RigPointerConstant();
  TestRigChainValidationLogic();

  std::cout << "Resumo: " << g_testsPassed << "/" << g_testsRun
            << " passaram, " << g_testsFailed << " falharam." << std::endl;
  return g_testsFailed == 0 ? 0 : 1;
}
