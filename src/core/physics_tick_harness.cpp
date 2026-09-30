#include "dr2hook/physics_tick_harness.h"
#include "dr2hook/hook_prologue.h"
#include "dr2hook/physics_harness_addresses.h"
#include "dr2hook/physics_tick_harness_prologues.h"
#include "dr2hook/logger.h"
#include "dr2hook/safety.h"

#if !defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)
#include <MinHook.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace dr2hook {

namespace {

using namespace physics_harness;

constexpr uintptr_t kRvaPhysicsStep = 0xDBC500;
constexpr uintptr_t kRvaPreTick = 0x749A30;
constexpr uintptr_t kRvaCommitAuxA = 0x73A070;
constexpr uintptr_t kRvaCommitAuxB = 0x73B620;
constexpr uintptr_t kRvaIntegratorReturnSite = 0x73E314;

constexpr uintptr_t kRvaSetTransform = 0x74AD80;
constexpr uintptr_t kRvaSetLinVel = 0x74A910;
constexpr uintptr_t kRvaSetAngVel = 0x74A890;

constexpr uintptr_t kGlobalCarPointer = 0x1681CE8;

constexpr uint32_t kRigVec4Offsets[] = {0x170, 0x180, 0x200, 0x210, 0x2b0,
                                        0x2c0, 0x2d0, 0x2e0, 0x320, 0x330};
constexpr uint32_t kRigScalarOffsets[] = {0x2508, 0x1338};
constexpr uint32_t kRigExtraVec4Offset = 0x290;
constexpr uint32_t kContainerExtraVec4Offset = 0xC930;

bool s_instrumentationEnabled = false;
bool s_writesEnabled = false;
bool s_selfTestMode = false;
bool s_experimentalNativeEnabled = false;
bool s_installed = false;
uintptr_t s_gameBase = 0;

std::atomic<uint64_t> s_tickCounter{0};
std::atomic<uint64_t> s_boundaryFireCounts[16] = {};

std::mutex s_csvMutex;
std::ofstream s_csvStream;
bool s_csvHeaderWritten = false;

struct ScheduledWrite {
  bool active = false;
  uint64_t tick = 0;
  PhysicsHarnessBoundary boundary = PhysicsHarnessBoundary::B1_TickStart;
  uint32_t rigOffset = 0;
  std::vector<uint8_t> bytes;
};

struct ScheduledNative {
  bool active = false;
  uint64_t tick = 0;
  PhysicsHarnessBoundary boundary = PhysicsHarnessBoundary::B1_TickStart;
  PhysicsNativeCallKind kind = PhysicsNativeCallKind::Commit;
};

std::mutex s_writeQueueMutex;
ScheduledWrite s_scheduledWrite;
ScheduledNative s_scheduledNative;

using PhysicsCall = void(__fastcall *)(void *, void *, void *, void *);

PhysicsCall g_origTickStart = nullptr;
PhysicsCall g_origIntegrator = nullptr;
PhysicsCall g_origCommit = nullptr;
PhysicsCall g_origFrameLoop = nullptr;
PhysicsCall g_origPhysicsStep = nullptr;
PhysicsCall g_origPreTick = nullptr;
PhysicsCall g_origEndStep = nullptr;
PhysicsCall g_origCommitAuxA = nullptr;
PhysicsCall g_origCommitAuxB = nullptr;

using RigOnlyCall = void(__fastcall *)(void *rig);
using SetLinVelCall = void(__fastcall *)(void *rig, float x, float y, float z);

std::string GetExecutableDirectory() {
#if defined(_WIN32)
  char path[MAX_PATH] = {};
  const DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return ".";
  }
  std::filesystem::path p(path);
  if (p.has_parent_path()) {
    return p.parent_path().string();
  }
#endif
  return ".";
}

bool EqualsIgnoreCase(const char *value, const char *literal) {
  if (value == nullptr || literal == nullptr) {
    return false;
  }
  while (*value != '\0' && *literal != '\0') {
    const char a =
        static_cast<char>(std::tolower(static_cast<unsigned char>(*value)));
    const char b =
        static_cast<char>(std::tolower(static_cast<unsigned char>(*literal)));
    if (a != b) {
      return false;
    }
    ++value;
    ++literal;
  }
  return *value == '\0' && *literal == '\0';
}

bool ParseTruthy(const char *value) {
  if (value == nullptr) {
    return false;
  }
  return EqualsIgnoreCase(value, "1") || EqualsIgnoreCase(value, "true") ||
         EqualsIgnoreCase(value, "yes") || EqualsIgnoreCase(value, "on");
}

bool ReadIniBool(const std::filesystem::path &iniPath, const char *key) {
  std::ifstream in(iniPath);
  if (!in.is_open()) {
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string k = line.substr(0, eq);
    std::string v = line.substr(eq + 1);
    if (k == key) {
      return ParseTruthy(v.c_str());
    }
  }
  return false;
}

std::string BoundaryName(PhysicsHarnessBoundary boundary) {
  switch (boundary) {
  case PhysicsHarnessBoundary::B1_TickStart:
    return "B1";
  case PhysicsHarnessBoundary::M1_BeforeIntegrator:
    return "M1";
  case PhysicsHarnessBoundary::M2_AfterIntegrator:
    return "M2";
  case PhysicsHarnessBoundary::M3_BeforeCommit:
    return "M3";
  case PhysicsHarnessBoundary::B2_AfterCommit:
    return "B2";
  case PhysicsHarnessBoundary::H1_FrameLoopReturn:
    return "H1";
  case PhysicsHarnessBoundary::H2_FrameLoopEntry:
    return "H2";
  case PhysicsHarnessBoundary::H3_PhysicsStep:
    return "H3";
  case PhysicsHarnessBoundary::H4_PreTick:
    return "H4";
  case PhysicsHarnessBoundary::H5_EndStep:
    return "H5";
  case PhysicsHarnessBoundary::H6_IntegratorReturnFilter:
    return "H6";
  case PhysicsHarnessBoundary::Log_CommitAuxA:
    return "LOG_COMMIT_A";
  case PhysicsHarnessBoundary::Log_CommitAuxB:
    return "LOG_COMMIT_B";
  }
  return "UNKNOWN";
}

void BumpBoundaryFire(PhysicsHarnessBoundary boundary) {
  const auto idx = static_cast<size_t>(boundary);
  if (idx < 16) {
    s_boundaryFireCounts[idx].fetch_add(1, std::memory_order_relaxed);
  }
}

bool ResolvePlayerChain(uintptr_t gameBase, uintptr_t *outContainer,
                        uintptr_t *outRig) {
  if (outContainer != nullptr) {
    *outContainer = 0;
  }
  if (outRig != nullptr) {
    *outRig = 0;
  }
  if (gameBase == 0) {
    return false;
  }

  uintptr_t car = 0;
  std::memcpy(&car, reinterpret_cast<void *>(gameBase + kGlobalCarPointer),
              sizeof(car));
  if (car == 0) {
    return false;
  }

  uintptr_t container = 0;
  std::memcpy(&container, reinterpret_cast<void *>(car + 0x30),
              sizeof(container));
  if (container == 0) {
    return false;
  }

  uintptr_t rig = 0;
  std::memcpy(&rig, reinterpret_cast<void *>(container + 0x08), sizeof(rig));
  if (rig == 0) {
    return false;
  }

  uintptr_t self = 0;
  std::memcpy(&self, reinterpret_cast<void *>(rig + 0x12C0), sizeof(self));
  if (self != rig) {
    return false;
  }

  uint32_t tag = 0;
  std::memcpy(&tag, reinterpret_cast<void *>(rig + 0x12D0), sizeof(tag));
  if (tag != 4) {
    return false;
  }

  if (outContainer != nullptr) {
    *outContainer = container;
  }
  if (outRig != nullptr) {
    *outRig = rig;
  }
  return true;
}

void LogPrologueMismatch(const char *siteName, const void *target,
                         const uint8_t *expected, size_t expectedLength) {
  std::ostringstream oss;
  oss << "PhysicsTickHarness: prologue mismatch em " << siteName << " @ "
      << target << " esperado=";
  for (size_t i = 0; i < expectedLength; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(expected[i]);
  }
  oss << " actual=";
  const auto *actual = static_cast<const uint8_t *>(target);
  for (size_t i = 0; i < expectedLength; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(actual[i]);
  }
  Logger::Error(oss.str());
}

bool OpenCsvLogIfNeeded() {
  if (s_csvStream.is_open()) {
    return true;
  }
  const auto csvPath = std::filesystem::path(GetExecutableDirectory()) /
                       "dr2hook_physics_tick_harness.csv";
  s_csvStream.open(csvPath, std::ios::out | std::ios::app);
  if (!s_csvStream.is_open()) {
    Logger::Error("PhysicsTickHarness: falha ao abrir CSV " +
                  csvPath.string());
    return false;
  }
  return true;
}

void WriteCsvHeaderIfNeeded() {
  if (s_csvHeaderWritten || !s_csvStream.is_open()) {
    return;
  }
  s_csvStream << "tick,boundary,thread_id,rig,f5,f6,f7";
  for (uint32_t off : kRigVec4Offsets) {
    char label[32];
    std::snprintf(label, sizeof(label), "0x%X", off);
    s_csvStream << ",vec4_" << label << "_x,vec4_" << label
                << "_y,vec4_" << label << "_z,vec4_" << label << "_w";
  }
  for (uint32_t off : kRigScalarOffsets) {
    char label[32];
    std::snprintf(label, sizeof(label), "0x%X", off);
    s_csvStream << ",scalar_" << label;
  }
  s_csvStream << ",vec4_rig_0x290_x,vec4_rig_0x290_y,vec4_rig_0x290_z,vec4_"
                 "rig_0x290_w";
  s_csvStream << ",vec4_container_0xc930_x,vec4_container_0xc930_y,vec4_"
                 "container_0xc930_z,vec4_container_0xc930_w\n";
  s_csvHeaderWritten = true;
}

bool IsFunctionKeyDown(int vk) {
#if defined(_WIN32)
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
  (void)vk;
  return false;
#endif
}

void LogBoundarySample(PhysicsHarnessBoundary boundary) {
  uintptr_t container = 0;
  uintptr_t rig = 0;
  if (!ResolvePlayerChain(s_gameBase, &container, &rig)) {
    return;
  }

  BumpBoundaryFire(boundary);

  std::lock_guard<std::mutex> lock(s_csvMutex);
  if (!OpenCsvLogIfNeeded()) {
    return;
  }
  WriteCsvHeaderIfNeeded();

#if defined(_WIN32)
  const unsigned long threadId = GetCurrentThreadId();
#else
  const unsigned long threadId = 0;
#endif

  s_csvStream << s_tickCounter.load(std::memory_order_relaxed) << ','
              << BoundaryName(boundary) << ',' << threadId << ",0x" << std::hex
              << rig << std::dec << ',' << (IsFunctionKeyDown(VK_F5) ? 1 : 0)
              << ',' << (IsFunctionKeyDown(VK_F6) ? 1 : 0) << ','
              << (IsFunctionKeyDown(VK_F7) ? 1 : 0);

  for (uint32_t off : kRigVec4Offsets) {
    float values[4] = {};
    std::memcpy(values, reinterpret_cast<void *>(rig + off), sizeof(values));
    s_csvStream << ',' << values[0] << ',' << values[1] << ',' << values[2]
                << ',' << values[3];
  }
  for (uint32_t off : kRigScalarOffsets) {
    float scalar = 0.f;
    std::memcpy(&scalar, reinterpret_cast<void *>(rig + off), sizeof(scalar));
    s_csvStream << ',' << scalar;
  }

  float rigExtra[4] = {};
  std::memcpy(rigExtra, reinterpret_cast<void *>(rig + kRigExtraVec4Offset),
              sizeof(rigExtra));
  s_csvStream << ',' << rigExtra[0] << ',' << rigExtra[1] << ',' << rigExtra[2]
              << ',' << rigExtra[3];

  float containerExtra[4] = {};
  std::memcpy(containerExtra,
              reinterpret_cast<void *>(container + kContainerExtraVec4Offset),
              sizeof(containerExtra));
  s_csvStream << ',' << containerExtra[0] << ',' << containerExtra[1] << ','
              << containerExtra[2] << ',' << containerExtra[3] << '\n';
  s_csvStream.flush();
}

void ExecuteScheduledWriteIfDue(PhysicsHarnessBoundary boundary) {
  ScheduledWrite pending{};
  {
    std::lock_guard<std::mutex> lock(s_writeQueueMutex);
    if (!s_scheduledWrite.active) {
      return;
    }
    if (s_scheduledWrite.tick != s_tickCounter.load(std::memory_order_relaxed)) {
      return;
    }
    if (s_scheduledWrite.boundary != boundary) {
      return;
    }
    pending = s_scheduledWrite;
    s_scheduledWrite.active = false;
  }

  if (!PhysicsTickHarness::AreWritesEnabled()) {
    Logger::Warn("PhysicsTickHarness: escrita recusada (writes desabilitados).");
    return;
  }
  if (!SafetyGuard::CanWriteState()) {
    Logger::Warn(
        "PhysicsTickHarness: escrita recusada (SafetyGuard fail-closed).");
    return;
  }

  uintptr_t rig = 0;
  if (!ResolvePlayerChain(s_gameBase, nullptr, &rig)) {
    Logger::Warn("PhysicsTickHarness: escrita recusada (rig invalido).");
    return;
  }

  const uintptr_t target = rig + pending.rigOffset;
  std::vector<uint8_t> before(pending.bytes.size());
  std::memcpy(before.data(), reinterpret_cast<void *>(target), before.size());

  std::ostringstream beforeHex;
  for (uint8_t b : before) {
    beforeHex << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(b);
  }

  std::memcpy(reinterpret_cast<void *>(target), pending.bytes.data(),
              pending.bytes.size());

  std::vector<uint8_t> after(pending.bytes.size());
  std::memcpy(after.data(), reinterpret_cast<void *>(target), after.size());
  std::ostringstream afterHex;
  for (uint8_t b : after) {
    afterHex << std::hex << std::setw(2) << std::setfill('0')
             << static_cast<int>(b);
  }

  Logger::Info("PhysicsTickHarness: write tick=" +
               std::to_string(pending.tick) + " boundary=" +
               BoundaryName(boundary) + " offset=0x" +
               std::to_string(pending.rigOffset) + " before=" + beforeHex.str() +
               " after=" + afterHex.str());
}

void ExecuteScheduledNativeIfDue(PhysicsHarnessBoundary boundary) {
  ScheduledNative pending{};
  {
    std::lock_guard<std::mutex> lock(s_writeQueueMutex);
    if (!s_scheduledNative.active) {
      return;
    }
    if (s_scheduledNative.tick != s_tickCounter.load(std::memory_order_relaxed)) {
      return;
    }
    if (s_scheduledNative.boundary != boundary) {
      return;
    }
    pending = s_scheduledNative;
    s_scheduledNative.active = false;
  }

  if (!PhysicsTickHarness::IsExperimentalNativeEnabled()) {
    Logger::Warn("PhysicsTickHarness: native experimental recusado (desligado).");
    return;
  }
  if (!SafetyGuard::CanWriteState()) {
    Logger::Warn("PhysicsTickHarness: native experimental recusado (SafetyGuard).");
    return;
  }

  uintptr_t rig = 0;
  if (!ResolvePlayerChain(s_gameBase, nullptr, &rig)) {
    return;
  }

  void *rigPtr = reinterpret_cast<void *>(rig);
  switch (pending.kind) {
  case PhysicsNativeCallKind::SetTransform: {
    auto fn = reinterpret_cast<RigOnlyCall>(s_gameBase + kRvaSetTransform);
    fn(rigPtr);
    break;
  }
  case PhysicsNativeCallKind::SetLinVel: {
    auto fn = reinterpret_cast<SetLinVelCall>(s_gameBase + kRvaSetLinVel);
    fn(rigPtr, 0.f, 0.f, 0.f);
    break;
  }
  case PhysicsNativeCallKind::SetAngVel: {
    auto fn = reinterpret_cast<SetLinVelCall>(s_gameBase + kRvaSetAngVel);
    fn(rigPtr, 0.f, 0.f, 0.f);
    break;
  }
  case PhysicsNativeCallKind::Commit: {
    auto fn = reinterpret_cast<RigOnlyCall>(s_gameBase + kRvaCommit);
    fn(rigPtr);
    break;
  }
  }

  Logger::Info("PhysicsTickHarness: native experimental executado boundary=" +
               BoundaryName(boundary));
}

void OnBoundary(PhysicsHarnessBoundary boundary, bool allowWritePoints) {
  if (allowWritePoints) {
    ExecuteScheduledWriteIfDue(boundary);
    ExecuteScheduledNativeIfDue(boundary);
  }
  LogBoundarySample(boundary);
}

void __fastcall DetourTickStart(void *a1, void *a2, void *a3, void *a4) {
  s_tickCounter.fetch_add(1, std::memory_order_relaxed);
  // B1 — antes do tick start @ 0x14074b8f0
  OnBoundary(PhysicsHarnessBoundary::B1_TickStart, true);
  if (g_origTickStart != nullptr) {
    g_origTickStart(a1, a2, a3, a4);
  }
}

void __fastcall DetourIntegrator(void *a1, void *a2, void *a3, void *a4) {
  // M1 — antes do integrator @ 0x140746150
  OnBoundary(PhysicsHarnessBoundary::M1_BeforeIntegrator, true);
  if (g_origIntegrator != nullptr) {
    g_origIntegrator(a1, a2, a3, a4);
  }
  // M2 — depois do integrator
  OnBoundary(PhysicsHarnessBoundary::M2_AfterIntegrator, true);

#if defined(_WIN32) && !defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)
  void *ret = nullptr;
#if defined(_MSC_VER)
  ret = _ReturnAddress();
#elif defined(__GNUC__)
  ret = __builtin_return_address(0);
#endif
  if (ret == reinterpret_cast<void *>(s_gameBase + kRvaIntegratorReturnSite)) {
    OnBoundary(PhysicsHarnessBoundary::H6_IntegratorReturnFilter, true);
  }
#endif
}

void __fastcall DetourCommit(void *a1, void *a2, void *a3, void *a4) {
  // M3 — antes do Commit @ 0x14074d190
  OnBoundary(PhysicsHarnessBoundary::M3_BeforeCommit, true);
  if (g_origCommit != nullptr) {
    g_origCommit(a1, a2, a3, a4);
  }
  // B2 — depois do Commit
  OnBoundary(PhysicsHarnessBoundary::B2_AfterCommit, true);
}

void __fastcall DetourFrameLoop(void *a1, void *a2, void *a3, void *a4) {
  OnBoundary(PhysicsHarnessBoundary::H2_FrameLoopEntry, true);
  if (g_origFrameLoop != nullptr) {
    g_origFrameLoop(a1, a2, a3, a4);
  }
  OnBoundary(PhysicsHarnessBoundary::H1_FrameLoopReturn, true);
}

void __fastcall DetourPhysicsStep(void *a1, void *a2, void *a3, void *a4) {
  OnBoundary(PhysicsHarnessBoundary::H3_PhysicsStep, false);
  if (g_origPhysicsStep != nullptr) {
    g_origPhysicsStep(a1, a2, a3, a4);
  }
}

void __fastcall DetourPreTick(void *a1, void *a2, void *a3, void *a4) {
  OnBoundary(PhysicsHarnessBoundary::H4_PreTick, false);
  if (g_origPreTick != nullptr) {
    g_origPreTick(a1, a2, a3, a4);
  }
}

void __fastcall DetourEndStep(void *a1, void *a2, void *a3, void *a4) {
  OnBoundary(PhysicsHarnessBoundary::H5_EndStep, true);
  if (g_origEndStep != nullptr) {
    g_origEndStep(a1, a2, a3, a4);
  }
}

void __fastcall DetourCommitAuxA(void *a1, void *a2, void *a3, void *a4) {
  OnBoundary(PhysicsHarnessBoundary::Log_CommitAuxA, false);
  if (g_origCommitAuxA != nullptr) {
    g_origCommitAuxA(a1, a2, a3, a4);
  }
}

void __fastcall DetourCommitAuxB(void *a1, void *a2, void *a3, void *a4) {
  OnBoundary(PhysicsHarnessBoundary::Log_CommitAuxB, false);
  if (g_origCommitAuxB != nullptr) {
    g_origCommitAuxB(a1, a2, a3, a4);
  }
}

#if !defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)

bool InstallHookSite(const char *name, void *target, void *detour,
                     PhysicsCall *originalOut,
                     const uint8_t *expectedPrologue, size_t prologueLength,
                     bool required) {
  if (!VerifyHookPrologue(target, expectedPrologue, prologueLength)) {
    LogPrologueMismatch(name, target, expectedPrologue, prologueLength);
    return false;
  }

  if (MH_CreateHook(target, detour, reinterpret_cast<void **>(originalOut)) !=
      MH_OK) {
    Logger::Error(std::string("PhysicsTickHarness: MH_CreateHook falhou em ") +
                  name);
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    Logger::Error(std::string("PhysicsTickHarness: MH_EnableHook falhou em ") +
                  name);
    return false;
  }
  Logger::Info(std::string("PhysicsTickHarness: hook instalado em ") + name);
  return true;
}

void LogReferenceAddresses(uintptr_t gameBase) {
  auto fmt = [](uintptr_t base, uintptr_t rva, uintptr_t specVa) {
    std::ostringstream oss;
    oss << "0x" << std::hex << (base + rva) << " (RVA 0x" << rva
        << ", spec VA 0x" << specVa << ")";
    return oss.str();
  };
  char baseHex[24] = {};
  std::snprintf(baseHex, sizeof(baseHex), "%llX",
                static_cast<unsigned long long>(gameBase));
  Logger::Info(std::string("PhysicsTickHarness: image base spec=0x140000000 "
                           "actual=0x") +
               baseHex);
  Logger::Info("PhysicsTickHarness: integrator " +
               fmt(gameBase, kRvaIntegrator, kVaIntegrator));
  Logger::Info("PhysicsTickHarness: tick start " +
               fmt(gameBase, kRvaTickStart, kVaTickStart));
  Logger::Info("PhysicsTickHarness: commit " + fmt(gameBase, kRvaCommit, kVaCommit));
  Logger::Info("PhysicsTickHarness: per-tick caller " +
               fmt(gameBase, kRvaPerTickCaller, kVaPerTickCaller));
  Logger::Info("PhysicsTickHarness: SetPose " +
               fmt(gameBase, kRvaSetPose, kVaSetPose));
  Logger::Info("PhysicsTickHarness: frame loop " +
               fmt(gameBase, kRvaFrameLoop, kVaFrameLoop));
  Logger::Info(
      "PhysicsTickHarness: hook B1/M1-M3/B2 -> tick_start, integrator, commit");
}

DWORD WINAPI SelfTestThread(LPVOID) {
  Sleep(3000);
  Logger::Info("PhysicsTickHarness: self-test concluido (sem writes).");
  for (size_t i = 0; i < 16; ++i) {
    const uint64_t count =
        s_boundaryFireCounts[i].load(std::memory_order_relaxed);
    if (count == 0) {
      continue;
    }
    Logger::Info("PhysicsTickHarness: self-test boundary idx=" +
                 std::to_string(i) + " fires=" + std::to_string(count));
  }
  return 0;
}

#endif // !DR2HOOK_PHYSICS_HARNESS_NO_HOOKS

} // namespace

uintptr_t PhysicsTickHarness::ResolvePlayerRig(uintptr_t gameModuleBase) {
  uintptr_t rig = 0;
  if (!ResolvePlayerChain(gameModuleBase, nullptr, &rig)) {
    return 0;
  }
  return rig;
}

bool PhysicsTickHarness::LoadConfiguration() {
  s_instrumentationEnabled = false;
  s_writesEnabled = false;
  s_selfTestMode = false;
  s_experimentalNativeEnabled = false;

#if defined(_WIN32)
  char envInstrumentation[64] = {};
  char envWrites[64] = {};
  char envSelfTest[64] = {};
  char envNative[64] = {};
  GetEnvironmentVariableA("DR2HOOK_PHYSICS_HARNESS", envInstrumentation,
                          static_cast<DWORD>(sizeof(envInstrumentation)));
  GetEnvironmentVariableA("DR2HOOK_PHYSICS_HARNESS_WRITES", envWrites,
                          static_cast<DWORD>(sizeof(envWrites)));
  GetEnvironmentVariableA("DR2HOOK_PHYSICS_HARNESS_SELF_TEST", envSelfTest,
                          static_cast<DWORD>(sizeof(envSelfTest)));
  GetEnvironmentVariableA("DR2HOOK_PHYSICS_HARNESS_EXPERIMENTAL_NATIVE",
                          envNative, static_cast<DWORD>(sizeof(envNative)));
  if (envInstrumentation[0] != '\0') {
    s_instrumentationEnabled = ParseTruthy(envInstrumentation);
  }
  if (envWrites[0] != '\0') {
    s_writesEnabled = ParseTruthy(envWrites);
  }
  if (envSelfTest[0] != '\0') {
    s_selfTestMode = ParseTruthy(envSelfTest);
  }
  if (envNative[0] != '\0') {
    s_experimentalNativeEnabled = ParseTruthy(envNative);
  }

  const auto iniPath = std::filesystem::path(GetExecutableDirectory()) /
                       "dr2hook_physics_harness.ini";
  if (ReadIniBool(iniPath, "instrumentation")) {
    s_instrumentationEnabled = true;
  }
  if (ReadIniBool(iniPath, "writes")) {
    s_writesEnabled = true;
  }
  if (ReadIniBool(iniPath, "self_test")) {
    s_selfTestMode = true;
  }
  if (ReadIniBool(iniPath, "experimental_native")) {
    s_experimentalNativeEnabled = true;
  }
#endif

  if (s_writesEnabled && !s_instrumentationEnabled) {
    Logger::Warn(
        "PhysicsTickHarness: writes=1 exige instrumentation=1; writes ignorado.");
    s_writesEnabled = false;
  }
  if (s_experimentalNativeEnabled && !s_instrumentationEnabled) {
    s_experimentalNativeEnabled = false;
  }

  return s_instrumentationEnabled;
}

bool PhysicsTickHarness::IsInstrumentationEnabled() {
  return s_instrumentationEnabled;
}

bool PhysicsTickHarness::AreWritesEnabled() {
  return s_writesEnabled && s_instrumentationEnabled;
}

bool PhysicsTickHarness::IsSelfTestMode() { return s_selfTestMode; }

bool PhysicsTickHarness::IsExperimentalNativeEnabled() {
  return s_experimentalNativeEnabled && s_instrumentationEnabled;
}

bool PhysicsTickHarness::TryInstall(uintptr_t gameModuleBase) {
  if (!s_instrumentationEnabled || s_installed) {
    return false;
  }
#if !defined(_WIN32) || defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)
  Logger::Warn("PhysicsTickHarness: hooks indisponiveis neste build.");
  return false;
#else
  if (gameModuleBase == 0) {
    Logger::Error("PhysicsTickHarness: gameModuleBase invalido.");
    return false;
  }

  s_gameBase = gameModuleBase;
  LogReferenceAddresses(gameModuleBase);

  using namespace physics_harness_prologues;
  void *pTickStart = reinterpret_cast<void *>(gameModuleBase + kRvaTickStart);
  void *pIntegrator = reinterpret_cast<void *>(gameModuleBase + kRvaIntegrator);
  void *pCommit = reinterpret_cast<void *>(gameModuleBase + kRvaCommit);
  void *pFrameLoop = reinterpret_cast<void *>(gameModuleBase + kRvaFrameLoop);
  void *pPhysicsStep =
      reinterpret_cast<void *>(gameModuleBase + kRvaPhysicsStep);
  void *pPreTick = reinterpret_cast<void *>(gameModuleBase + kRvaPreTick);
  void *pEndStep =
      reinterpret_cast<void *>(gameModuleBase + kRvaPerTickCaller);

  if (!InstallHookSite("tick_start", pTickStart,
                       reinterpret_cast<void *>(&DetourTickStart),
                       &g_origTickStart, kTickStart, kPrologueLength, true)) {
    return false;
  }
  if (!InstallHookSite("integrator", pIntegrator,
                       reinterpret_cast<void *>(&DetourIntegrator),
                       &g_origIntegrator, kIntegrator, kPrologueLength, true)) {
    PhysicsTickHarness::Shutdown();
    return false;
  }
  if (!InstallHookSite("commit", pCommit, reinterpret_cast<void *>(&DetourCommit),
                       &g_origCommit, kCommit, kPrologueLength, true)) {
    PhysicsTickHarness::Shutdown();
    return false;
  }

  // Spec parte 2: apenas tick_start (B1), integrator (M1/M2), commit (M3/B2).
  // Frame loop / per-tick caller / SetPose são referência ou extensões opcionais.
  (void)InstallHookSite("frame_loop", pFrameLoop,
                         reinterpret_cast<void *>(&DetourFrameLoop),
                         &g_origFrameLoop, kFrameLoop, kPrologueLength, false);

  (void)InstallHookSite("physics_step", pPhysicsStep,
                         reinterpret_cast<void *>(&DetourPhysicsStep),
                         &g_origPhysicsStep, kPhysicsStep, kPrologueLength,
                         false);
  (void)InstallHookSite("pretick", pPreTick,
                         reinterpret_cast<void *>(&DetourPreTick), &g_origPreTick,
                         kPreTick, kPrologueLength, false);
  (void)InstallHookSite("end_step", pEndStep,
                         reinterpret_cast<void *>(&DetourEndStep), &g_origEndStep,
                         kEndStep, kPrologueLength, false);

  void *pAuxA = reinterpret_cast<void *>(gameModuleBase + kRvaCommitAuxA);
  void *pAuxB = reinterpret_cast<void *>(gameModuleBase + kRvaCommitAuxB);
  (void)InstallHookSite("commit_aux_a", pAuxA,
                         reinterpret_cast<void *>(&DetourCommitAuxA),
                         &g_origCommitAuxA, kCommitAuxA, kPrologueLength, false);
  (void)InstallHookSite("commit_aux_b", pAuxB,
                         reinterpret_cast<void *>(&DetourCommitAuxB),
                         &g_origCommitAuxB, kCommitAuxB, kPrologueLength, false);

  s_installed = true;
  Logger::Info("PhysicsTickHarness: instrumentation ativa (opt-in).");

  if (s_selfTestMode) {
    CreateThread(nullptr, 0, SelfTestThread, nullptr, 0, nullptr);
  }
  return true;
#endif
}

void PhysicsTickHarness::Shutdown() {
#if defined(_WIN32) && !defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)
  if (s_gameBase != 0) {
    void *targets[] = {
        reinterpret_cast<void *>(s_gameBase + kRvaTickStart),
        reinterpret_cast<void *>(s_gameBase + kRvaIntegrator),
        reinterpret_cast<void *>(s_gameBase + kRvaCommit),
        reinterpret_cast<void *>(s_gameBase + kRvaFrameLoop),
        reinterpret_cast<void *>(s_gameBase + kRvaPhysicsStep),
        reinterpret_cast<void *>(s_gameBase + kRvaPreTick),
        reinterpret_cast<void *>(s_gameBase + kRvaPerTickCaller),
        reinterpret_cast<void *>(s_gameBase + kRvaCommitAuxA),
        reinterpret_cast<void *>(s_gameBase + kRvaCommitAuxB),
    };
    for (void *target : targets) {
      if (target != nullptr) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
      }
    }
  }
#endif
  g_origTickStart = nullptr;
  g_origIntegrator = nullptr;
  g_origCommit = nullptr;
  g_origFrameLoop = nullptr;
  g_origPhysicsStep = nullptr;
  g_origPreTick = nullptr;
  g_origEndStep = nullptr;
  g_origCommitAuxA = nullptr;
  g_origCommitAuxB = nullptr;
  s_installed = false;

  {
    std::lock_guard<std::mutex> lock(s_writeQueueMutex);
    s_scheduledWrite = ScheduledWrite{};
    s_scheduledNative = ScheduledNative{};
  }

  {
    std::lock_guard<std::mutex> lock(s_csvMutex);
    if (s_csvStream.is_open()) {
      s_csvStream.flush();
      s_csvStream.close();
    }
    s_csvHeaderWritten = false;
  }
}

uint64_t PhysicsTickHarness::GetTickCounter() {
  return s_tickCounter.load(std::memory_order_relaxed);
}

bool PhysicsTickHarness::ScheduleWrite(uint64_t tick,
                                       PhysicsHarnessBoundary boundary,
                                       uint32_t rigOffset, const void *bytes,
                                       size_t byteCount) {
  if (bytes == nullptr || byteCount == 0 || byteCount > 64) {
    return false;
  }
  if (!AreWritesEnabled()) {
    Logger::Warn("PhysicsTickHarness: ScheduleWrite recusado (writes off).");
    return false;
  }

  std::lock_guard<std::mutex> lock(s_writeQueueMutex);
  if (s_scheduledWrite.active || s_scheduledNative.active) {
    Logger::Warn("PhysicsTickHarness: ja existe operacao enfileirada.");
    return false;
  }

  s_scheduledWrite.active = true;
  s_scheduledWrite.tick = tick;
  s_scheduledWrite.boundary = boundary;
  s_scheduledWrite.rigOffset = rigOffset;
  s_scheduledWrite.bytes.assign(static_cast<const uint8_t *>(bytes),
                                static_cast<const uint8_t *>(bytes) + byteCount);
  Logger::Info("PhysicsTickHarness: write enfileirada tick=" +
               std::to_string(tick) + " boundary=" + BoundaryName(boundary) +
               " offset=0x" + std::to_string(rigOffset) + " len=" +
               std::to_string(byteCount));
  return true;
}

bool PhysicsTickHarness::ScheduleExperimentalNative(
    uint64_t tick, PhysicsHarnessBoundary boundary,
    PhysicsNativeCallKind kind) {
  if (!IsExperimentalNativeEnabled()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(s_writeQueueMutex);
  if (s_scheduledWrite.active || s_scheduledNative.active) {
    return false;
  }
  s_scheduledNative.active = true;
  s_scheduledNative.tick = tick;
  s_scheduledNative.boundary = boundary;
  s_scheduledNative.kind = kind;
  return true;
}

} // namespace dr2hook
