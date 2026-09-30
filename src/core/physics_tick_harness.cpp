#include "dr2hook/physics_tick_harness.h"
#include "dr2hook/hook_prologue.h"
#include "dr2hook/physics_harness_addresses.h"
#include "dr2hook/physics_native_api.h"
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

// Spec parte 3 — amostras CSV (16 bytes float vec4 por offset)
constexpr uint32_t kRigVec4OffsetsPart3[] = {
    0x170, 0x180, 0x200, 0x210, 0x2b0, 0x2c0, 0x2d0, 0x2e0, 0x320, 0x330};
constexpr uint32_t kRigScalarOffsetsPart3[] = {0x2508, 0x1338};

static_assert(sizeof(kRigVec4OffsetsPart3) / sizeof(uint32_t) == 10,
              "Spec parte 3: dez offsets vec4");
static_assert(sizeof(kRigScalarOffsetsPart3) / sizeof(uint32_t) == 2,
              "Spec parte 3: dois escalares");

bool s_instrumentationEnabled = false;
bool s_writesEnabled = false;
bool s_selfTestMode = false;
bool s_experimentalNativeEnabled = false;
bool s_installed = false;
uintptr_t s_gameBase = 0;

constexpr DWORD kSelfTestLogDurationMs = 5000;

struct HookInstallRecord {
  const char *name = nullptr;
  bool installed = false;
};

constexpr size_t kHookSiteCount = 9;
HookInstallRecord s_hookInstallRecords[kHookSiteCount] = {};
size_t s_hookInstallRecordCount = 0;

std::atomic<uint64_t> s_tickCounter{0};
std::atomic<uint64_t> s_stepCounter{0};
std::atomic<uint64_t> s_boundaryFireCounts[20] = {};

std::atomic<uint64_t> s_practiceKeysTick{0};
std::atomic<bool> s_practiceKeyF5{false};
std::atomic<bool> s_practiceKeyF6{false};
std::atomic<bool> s_practiceKeyF7{false};

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
  physics_native::CallParams params{};
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

static physics_native::CallKind ToNativeCallKind(PhysicsNativeCallKind kind) {
  return static_cast<physics_native::CallKind>(static_cast<uint8_t>(kind));
}

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
  case PhysicsHarnessBoundary::H3_PhysicsStep_Entry:
    return "H3_ENTRY";
  case PhysicsHarnessBoundary::H3_PhysicsStep_Return:
    return "H3_RETURN";
  case PhysicsHarnessBoundary::H4_PreTick_Entry:
    return "H4_ENTRY";
  case PhysicsHarnessBoundary::H4_PreTick_Return:
    return "H4_RETURN";
  case PhysicsHarnessBoundary::H5_EndStep_Entry:
    return "H5_ENTRY";
  case PhysicsHarnessBoundary::H5_EndStep_Return:
    return "H5_RETURN";
  case PhysicsHarnessBoundary::H6_IntegratorReturnFilter:
    return "H6";
  case PhysicsHarnessBoundary::Log_Commit_74D190:
    return "LOG_COMMIT_74D190";
  case PhysicsHarnessBoundary::Log_Commit_73A070:
    return "LOG_COMMIT_73A070";
  case PhysicsHarnessBoundary::Log_Commit_73B620:
    return "LOG_COMMIT_73B620";
  }
  return "UNKNOWN";
}

void CapturePracticeModKeysForCurrentTick() {
  const uint64_t tick = s_tickCounter.load(std::memory_order_relaxed);
#if defined(DR2HOOK_PHYSICS_HARNESS_TESTING)
  s_practiceKeysTick.store(tick, std::memory_order_relaxed);
#elif defined(_WIN32)
  s_practiceKeyF5.store((GetAsyncKeyState(VK_F5) & 0x8000) != 0,
                        std::memory_order_relaxed);
  s_practiceKeyF6.store((GetAsyncKeyState(VK_F6) & 0x8000) != 0,
                        std::memory_order_relaxed);
  s_practiceKeyF7.store((GetAsyncKeyState(VK_F7) & 0x8000) != 0,
                        std::memory_order_relaxed);
  s_practiceKeysTick.store(tick, std::memory_order_relaxed);
#else
  s_practiceKeyF5.store(false, std::memory_order_relaxed);
  s_practiceKeyF6.store(false, std::memory_order_relaxed);
  s_practiceKeyF7.store(false, std::memory_order_relaxed);
  s_practiceKeysTick.store(tick, std::memory_order_relaxed);
#endif
}

void AppendPracticeKeyColumnsToCsv() {
  const uint64_t tick = s_tickCounter.load(std::memory_order_relaxed);
  bool f5 = false;
  bool f6 = false;
  bool f7 = false;
  if (s_practiceKeysTick.load(std::memory_order_relaxed) == tick) {
    f5 = s_practiceKeyF5.load(std::memory_order_relaxed);
    f6 = s_practiceKeyF6.load(std::memory_order_relaxed);
    f7 = s_practiceKeyF7.load(std::memory_order_relaxed);
  }
  s_csvStream << ',' << (f5 ? 1 : 0) << ',' << (f6 ? 1 : 0) << ','
              << (f7 ? 1 : 0);
}

void BumpBoundaryFire(PhysicsHarnessBoundary boundary) {
  const auto idx = static_cast<size_t>(boundary);
  if (idx < 20) {
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
  std::memcpy(&car,
              reinterpret_cast<void *>(gameBase + kRvaActiveCarPointer),
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
  std::memcpy(&self, reinterpret_cast<void *>(rig + kRigSelfPointerOffset),
              sizeof(self));
  if (self != rig) {
    return false;
  }

  uint32_t tag = 0;
  std::memcpy(&tag, reinterpret_cast<void *>(rig + kRigTypeTagOffset),
              sizeof(tag));
  if (tag != kRigExpectedTypeTag) {
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
  s_csvStream << "tick,step,boundary,thread_id,container,rig,key_f5,key_f6,key_f7";
  for (uint32_t off : kRigVec4OffsetsPart3) {
    char label[32];
    std::snprintf(label, sizeof(label), "0x%X", off);
    s_csvStream << ",vec4_" << label << "_x,vec4_" << label
                << "_y,vec4_" << label << "_z,vec4_" << label << "_w";
  }
  for (uint32_t off : kRigScalarOffsetsPart3) {
    char label[32];
    std::snprintf(label, sizeof(label), "0x%X", off);
    s_csvStream << ",scalar_" << label;
  }
  s_csvStream << ",container_vec4_0xC930_x,container_vec4_0xC930_y,"
                 "container_vec4_0xC930_z,container_vec4_0xC930_w"
                 ",rig_vec4_0x290_x,rig_vec4_0x290_y,rig_vec4_0x290_z,"
                 "rig_vec4_0x290_w";
  s_csvStream << "\n";
  s_csvHeaderWritten = true;
}

void LogBoundarySample(PhysicsHarnessBoundary boundary) {
  uintptr_t container = 0;
  uintptr_t rig = 0;
  if (!ResolvePlayerChain(s_gameBase, &container, &rig)) {
    return;
  }

  BumpBoundaryFire(boundary);

#if defined(_WIN32)
  const unsigned long threadId = GetCurrentThreadId();
#else
  const unsigned long threadId = 0;
#endif

  std::lock_guard<std::mutex> lock(s_csvMutex);
  if (!OpenCsvLogIfNeeded()) {
    return;
  }
  WriteCsvHeaderIfNeeded();

  s_csvStream << s_tickCounter.load(std::memory_order_relaxed) << ','
              << s_stepCounter.load(std::memory_order_relaxed) << ','
              << BoundaryName(boundary) << ',' << threadId << ",0x" << std::hex
              << container << ",0x" << rig << std::dec;
  AppendPracticeKeyColumnsToCsv();

  for (uint32_t off : kRigVec4OffsetsPart3) {
    float values[4] = {};
    std::memcpy(values, reinterpret_cast<void *>(rig + off), sizeof(values));
    s_csvStream << ',' << values[0] << ',' << values[1] << ',' << values[2]
                << ',' << values[3];
  }
  for (uint32_t off : kRigScalarOffsetsPart3) {
    float scalar = 0.f;
    std::memcpy(&scalar, reinterpret_cast<void *>(rig + off), sizeof(scalar));
    s_csvStream << ',' << scalar;
  }

  float containerExtra[4] = {};
  std::memcpy(containerExtra,
              reinterpret_cast<void *>(container + kContainerExtraVec4Offset),
              sizeof(containerExtra));
  s_csvStream << ',' << containerExtra[0] << ',' << containerExtra[1] << ','
              << containerExtra[2] << ',' << containerExtra[3];

  float rigExtra[4] = {};
  std::memcpy(rigExtra, reinterpret_cast<void *>(rig + kRigExtraVec4Offset),
              sizeof(rigExtra));
  s_csvStream << ',' << rigExtra[0] << ',' << rigExtra[1] << ',' << rigExtra[2]
              << ',' << rigExtra[3];

  s_csvStream << '\n';
  s_csvStream.flush();
}

void ExecuteScheduledWriteIfDue(PhysicsHarnessBoundary boundary) {
  if (s_selfTestMode) {
    return;
  }
  // Spec parte 4: só corre nos detours de física (OnBoundary), nunca no Present.
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

  char offsetHex[16] = {};
  std::snprintf(offsetHex, sizeof(offsetHex), "%X", pending.rigOffset);

  Logger::Info("PhysicsTickHarness: write tick=" +
               std::to_string(pending.tick) + " boundary=" +
               BoundaryName(boundary) + " rig_offset=0x" + offsetHex +
               " before=" + beforeHex.str() + " after=" + afterHex.str());
}

void ExecuteScheduledNativeIfDue(PhysicsHarnessBoundary boundary) {
  if (s_selfTestMode) {
    return;
  }
  // Spec parte 5: só nos detours de física; requer flag experimental + SafetyGuard.
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
    Logger::Warn("PhysicsTickHarness: native experimental recusado (rig invalido).");
    return;
  }

  void *rigPtr = reinterpret_cast<void *>(rig);
  const auto api = physics_native::ResolveEntrypoints(s_gameBase);
  const auto nativeKind = ToNativeCallKind(pending.kind);

  Logger::Info("PhysicsTickHarness: native experimental antes kind=" +
               std::to_string(static_cast<unsigned>(pending.kind)) + " boundary=" +
               BoundaryName(boundary));

  physics_native::Invoke(nativeKind, rigPtr, pending.params, api);

  Logger::Info("PhysicsTickHarness: native experimental depois kind=" +
               std::to_string(static_cast<unsigned>(pending.kind)) + " boundary=" +
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
  // Contador monotônico de tick (spec parte 3): incrementa uma vez por tick start.
  s_tickCounter.fetch_add(1, std::memory_order_relaxed);
  CapturePracticeModKeysForCurrentTick();
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
  // M2 — depois do integrator (amostra CSV; writes enfileirados em H6 filtrado).
  OnBoundary(PhysicsHarnessBoundary::M2_AfterIntegrator, false);

#if !defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)
  void *ret = nullptr;
#if defined(_MSC_VER)
  ret = _ReturnAddress();
#elif defined(__GNUC__)
  ret = __builtin_return_address(0);
#endif
  // Spec correction 3: retorno 0x14073e314 → boundary H6 (write point separado).
  if (ret == reinterpret_cast<void *>(s_gameBase + kRvaIntegratorReturnSite)) {
    OnBoundary(PhysicsHarnessBoundary::H6_IntegratorReturnFilter, true);
  }
#endif
}

void __fastcall DetourCommit(void *a1, void *a2, void *a3, void *a4) {
  // Commit @ 0x14074d190 — log-only opcional + M3/B2 (writes).
  OnBoundary(PhysicsHarnessBoundary::Log_Commit_74D190, false);
  OnBoundary(PhysicsHarnessBoundary::M3_BeforeCommit, true);
  if (g_origCommit != nullptr) {
    g_origCommit(a1, a2, a3, a4);
  }
  OnBoundary(PhysicsHarnessBoundary::B2_AfterCommit, true);
}

void __fastcall DetourFrameLoop(void *a1, void *a2, void *a3, void *a4) {
  // Spec correction 1: H2 @ 0x140dbca20 entrada; H1 retorno (between-tick); writes enfileirados.
  OnBoundary(PhysicsHarnessBoundary::H2_FrameLoopEntry, true);
  if (g_origFrameLoop != nullptr) {
    g_origFrameLoop(a1, a2, a3, a4);
  }
  OnBoundary(PhysicsHarnessBoundary::H1_FrameLoopReturn, true);
}

void __fastcall DetourPhysicsStep(void *a1, void *a2, void *a3, void *a4) {
  // H3 @ 0x140dbc500 — contador de step (spec correction 2); só log entrada/retorno.
  s_stepCounter.fetch_add(1, std::memory_order_relaxed);
  OnBoundary(PhysicsHarnessBoundary::H3_PhysicsStep_Entry, false);
  if (g_origPhysicsStep != nullptr) {
    g_origPhysicsStep(a1, a2, a3, a4);
  }
  OnBoundary(PhysicsHarnessBoundary::H3_PhysicsStep_Return, false);
}

void __fastcall DetourPreTick(void *a1, void *a2, void *a3, void *a4) {
  // H4 @ 0x140749a30 — PreTick; só log entrada/retorno.
  OnBoundary(PhysicsHarnessBoundary::H4_PreTick_Entry, false);
  if (g_origPreTick != nullptr) {
    g_origPreTick(a1, a2, a3, a4);
  }
  OnBoundary(PhysicsHarnessBoundary::H4_PreTick_Return, false);
}

void __fastcall DetourEndStep(void *a1, void *a2, void *a3, void *a4) {
  // H5 @ 0x1407511e0 — EndStep; log entrada/retorno + pontos de write opcionais.
  OnBoundary(PhysicsHarnessBoundary::H5_EndStep_Entry, true);
  if (g_origEndStep != nullptr) {
    g_origEndStep(a1, a2, a3, a4);
  }
  OnBoundary(PhysicsHarnessBoundary::H5_EndStep_Return, true);
}

void __fastcall DetourCommitAuxA(void *a1, void *a2, void *a3, void *a4) {
  OnBoundary(PhysicsHarnessBoundary::Log_Commit_73A070, false);
  if (g_origCommitAuxA != nullptr) {
    g_origCommitAuxA(a1, a2, a3, a4);
  }
}

void __fastcall DetourCommitAuxB(void *a1, void *a2, void *a3, void *a4) {
  OnBoundary(PhysicsHarnessBoundary::Log_Commit_73B620, false);
  if (g_origCommitAuxB != nullptr) {
    g_origCommitAuxB(a1, a2, a3, a4);
  }
}

#if !defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)

void RecordHookInstallResult(const char *name, bool installed) {
  if (s_hookInstallRecordCount >= kHookSiteCount) {
    return;
  }
  s_hookInstallRecords[s_hookInstallRecordCount].name = name;
  s_hookInstallRecords[s_hookInstallRecordCount].installed = installed;
  ++s_hookInstallRecordCount;
}

void LogSelfTestReport(bool afterObservationWindow) {
  const char *phase =
      afterObservationWindow ? "relatorio final" : "hooks apos instalacao";
  Logger::Info(std::string("PhysicsTickHarness: self-test (") + phase + ").");
  Logger::Info("PhysicsTickHarness: self-test writes=0 (forcado).");

  Logger::Info("PhysicsTickHarness: self-test hook sites:");
  for (size_t i = 0; i < s_hookInstallRecordCount; ++i) {
    const auto &rec = s_hookInstallRecords[i];
    Logger::Info(std::string("PhysicsTickHarness: self-test hook ") +
                 (rec.name != nullptr ? rec.name : "?") + " installed=" +
                 (rec.installed ? "1" : "0"));
  }

  Logger::Info("PhysicsTickHarness: self-test boundary fires:");
  for (size_t i = 0;
       i <= static_cast<size_t>(PhysicsHarnessBoundary::Log_Commit_73B620);
       ++i) {
    const auto boundary = static_cast<PhysicsHarnessBoundary>(i);
    const uint64_t count =
        s_boundaryFireCounts[i].load(std::memory_order_relaxed);
    Logger::Info("PhysicsTickHarness: self-test boundary " +
                 BoundaryName(boundary) + " fires=" + std::to_string(count));
  }

  const auto reportPath = std::filesystem::path(GetExecutableDirectory()) /
                          "dr2hook_physics_harness_self_test.log";
  std::ofstream report(reportPath, std::ios::out | std::ios::trunc);
  if (report.is_open()) {
    report << "phase=" << phase << "\nwrites=0\n";
    for (size_t i = 0; i < s_hookInstallRecordCount; ++i) {
      report << "hook," << s_hookInstallRecords[i].name << ","
             << (s_hookInstallRecords[i].installed ? 1 : 0) << "\n";
    }
    for (size_t i = 0;
         i <= static_cast<size_t>(PhysicsHarnessBoundary::Log_Commit_73B620);
         ++i) {
      report << "boundary," << BoundaryName(static_cast<PhysicsHarnessBoundary>(i))
             << "," << s_boundaryFireCounts[i].load(std::memory_order_relaxed)
             << "\n";
    }
  }
}

bool InstallHookSite(const char *name, void *target, void *detour,
                     PhysicsCall *originalOut,
                     const uint8_t *expectedPrologue, size_t prologueLength,
                     bool required) {
  if (!VerifyHookPrologue(target, expectedPrologue, prologueLength)) {
    LogPrologueMismatch(name, target, expectedPrologue, prologueLength);
    RecordHookInstallResult(name, false);
    return false;
  }

  if (MH_CreateHook(target, detour, reinterpret_cast<void **>(originalOut)) !=
      MH_OK) {
    Logger::Error(std::string("PhysicsTickHarness: MH_CreateHook falhou em ") +
                  name);
    RecordHookInstallResult(name, false);
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    Logger::Error(std::string("PhysicsTickHarness: MH_EnableHook falhou em ") +
                  name);
    RecordHookInstallResult(name, false);
    return false;
  }
  Logger::Info(std::string("PhysicsTickHarness: hook instalado em ") + name);
  RecordHookInstallResult(name, true);
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
  Logger::Info("PhysicsTickHarness: physics step (H3) " +
               fmt(gameBase, kRvaPhysicsStep, kVaPhysicsStep));
  Logger::Info("PhysicsTickHarness: pretick (H4) " +
               fmt(gameBase, kRvaPreTick, kVaPreTick));
  Logger::Info("PhysicsTickHarness: end step (H5) " +
               fmt(gameBase, kRvaEndStep, kVaEndStep));
  Logger::Info(
      "PhysicsTickHarness: hooks obrigatorios B1/M/B2 + H1/H2 frame_loop @ 0x"
      "140dbca20");
}

DWORD WINAPI SelfTestThread(LPVOID) {
  Logger::Info("PhysicsTickHarness: self-test a observar CSV/logs por " +
               std::to_string(kSelfTestLogDurationMs) + " ms.");
  Sleep(kSelfTestLogDurationMs);
  LogSelfTestReport(true);
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
  if (s_selfTestMode) {
    s_instrumentationEnabled = true;
    s_writesEnabled = false;
    s_experimentalNativeEnabled = false;
    Logger::Info(
        "PhysicsTickHarness: modo self-test (instrumentacao ligada, sem writes).");
  }

  return s_instrumentationEnabled;
}

bool PhysicsTickHarness::IsInstrumentationEnabled() {
  return s_instrumentationEnabled;
}

bool PhysicsTickHarness::AreWritesEnabled() {
  return s_writesEnabled && s_instrumentationEnabled && !s_selfTestMode;
}

bool PhysicsTickHarness::IsSelfTestMode() { return s_selfTestMode; }

bool PhysicsTickHarness::IsExperimentalNativeEnabled() {
  return s_experimentalNativeEnabled && s_instrumentationEnabled &&
         !s_selfTestMode;
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
  s_hookInstallRecordCount = 0;
  LogReferenceAddresses(gameModuleBase);

  using namespace physics_harness_prologues;
  void *pTickStart = reinterpret_cast<void *>(gameModuleBase + kRvaTickStart);
  void *pIntegrator = reinterpret_cast<void *>(gameModuleBase + kRvaIntegrator);
  void *pCommit = reinterpret_cast<void *>(gameModuleBase + kRvaCommit);
  void *pFrameLoop = reinterpret_cast<void *>(gameModuleBase + kRvaFrameLoop);
  void *pPhysicsStep =
      reinterpret_cast<void *>(gameModuleBase + kRvaPhysicsStep);
  void *pPreTick = reinterpret_cast<void *>(gameModuleBase + kRvaPreTick);
  void *pEndStep = reinterpret_cast<void *>(gameModuleBase + kRvaEndStep);

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
  if (!InstallHookSite("frame_loop", pFrameLoop,
                       reinterpret_cast<void *>(&DetourFrameLoop),
                       &g_origFrameLoop, kFrameLoop, kPrologueLength, true)) {
    PhysicsTickHarness::Shutdown();
    return false;
  }

  // Opcionais se prólogo falhar: physics_step, pretick, end_step, aux logs.
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

  void *pAuxA = reinterpret_cast<void *>(gameModuleBase + kRvaCommitLogAuxA);
  void *pAuxB = reinterpret_cast<void *>(gameModuleBase + kRvaCommitLogAuxB);
  (void)InstallHookSite("commit_log_73a070", pAuxA,
                         reinterpret_cast<void *>(&DetourCommitAuxA),
                         &g_origCommitAuxA, kCommitAuxA, kPrologueLength, false);
  (void)InstallHookSite("commit_log_73b620", pAuxB,
                         reinterpret_cast<void *>(&DetourCommitAuxB),
                         &g_origCommitAuxB, kCommitAuxB, kPrologueLength, false);

  s_installed = true;
  Logger::Info("PhysicsTickHarness: instrumentation ativa (opt-in).");

  if (s_selfTestMode) {
    LogSelfTestReport(false);
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
        reinterpret_cast<void *>(s_gameBase + kRvaEndStep),
        reinterpret_cast<void *>(s_gameBase + kRvaCommitLogAuxA),
        reinterpret_cast<void *>(s_gameBase + kRvaCommitLogAuxB),
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
  s_hookInstallRecordCount = 0;

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

uint64_t PhysicsTickHarness::GetStepCounter() {
  return s_stepCounter.load(std::memory_order_relaxed);
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
  if (!SafetyGuard::CanWriteState()) {
    Logger::Warn(
        "PhysicsTickHarness: ScheduleWrite recusado (SafetyGuard).");
    return false;
  }

  std::lock_guard<std::mutex> lock(s_writeQueueMutex);
  if (s_scheduledWrite.active) {
    Logger::Warn("PhysicsTickHarness: ja existe uma escrita enfileirada.");
    return false;
  }

  s_scheduledWrite.active = true;
  s_scheduledWrite.tick = tick;
  s_scheduledWrite.boundary = boundary;
  s_scheduledWrite.rigOffset = rigOffset;
  s_scheduledWrite.bytes.assign(static_cast<const uint8_t *>(bytes),
                                static_cast<const uint8_t *>(bytes) + byteCount);
  char offsetHex[16] = {};
  std::snprintf(offsetHex, sizeof(offsetHex), "%X", rigOffset);
  Logger::Info("PhysicsTickHarness: write enfileirada tick=" +
               std::to_string(tick) + " boundary=" + BoundaryName(boundary) +
               " rig_offset=0x" + offsetHex + " len=" +
               std::to_string(byteCount));
  return true;
}

bool PhysicsTickHarness::ScheduleExperimentalNative(
    uint64_t tick, PhysicsHarnessBoundary boundary,
    PhysicsNativeCallKind kind, const physics_native::CallParams &params) {
  if (!IsExperimentalNativeEnabled()) {
    Logger::Warn(
        "PhysicsTickHarness: ScheduleExperimentalNative recusado (desligado).");
    return false;
  }
  if (!SafetyGuard::CanWriteState()) {
    Logger::Warn(
        "PhysicsTickHarness: ScheduleExperimentalNative recusado (SafetyGuard).");
    return false;
  }
  std::lock_guard<std::mutex> lock(s_writeQueueMutex);
  if (s_scheduledNative.active) {
    Logger::Warn("PhysicsTickHarness: ja existe chamada nativa enfileirada.");
    return false;
  }
  s_scheduledNative.active = true;
  s_scheduledNative.tick = tick;
  s_scheduledNative.boundary = boundary;
  s_scheduledNative.kind = kind;
  s_scheduledNative.params = params;
  Logger::Info("PhysicsTickHarness: native enfileirada tick=" +
               std::to_string(tick) + " boundary=" + BoundaryName(boundary) +
               " kind=" + std::to_string(static_cast<unsigned>(kind)));
  return true;
}

#if defined(DR2HOOK_PHYSICS_HARNESS_TESTING)

void PhysicsTickHarness::TestingSetExperimentalNative(bool enabled) {
  s_experimentalNativeEnabled = enabled;
  if (enabled) {
    s_instrumentationEnabled = true;
  }
}

void PhysicsTickHarness::TestingClearScheduledNative() {
  std::lock_guard<std::mutex> lock(s_writeQueueMutex);
  s_scheduledNative = ScheduledNative{};
}

bool PhysicsTickHarnessTestingInvokeNative(
    PhysicsNativeCallKind kind, void *rig,
    const physics_native::CallParams &params,
    const physics_native::NativeEntrypoints &stubs) {
  const auto nativeKind =
      static_cast<physics_native::CallKind>(static_cast<uint8_t>(kind));
  physics_native::Invoke(nativeKind, rig, params, stubs);
  return true;
}

void PhysicsTickHarness::TestingSetInstrumentationAndWrites(bool instrumentation,
                                                            bool writes) {
  s_instrumentationEnabled = instrumentation;
  s_writesEnabled = writes;
}

void PhysicsTickHarness::TestingClearScheduledWrite() {
  std::lock_guard<std::mutex> lock(s_writeQueueMutex);
  s_scheduledWrite = ScheduledWrite{};
}

void PhysicsTickHarness::TestingSetPracticeKeysForTick(uint64_t tick, bool f5,
                                                       bool f6, bool f7) {
  s_practiceKeysTick.store(tick, std::memory_order_relaxed);
  s_practiceKeyF5.store(f5, std::memory_order_relaxed);
  s_practiceKeyF6.store(f6, std::memory_order_relaxed);
  s_practiceKeyF7.store(f7, std::memory_order_relaxed);
}

void PhysicsTickHarness::TestingSetSelfTestMode(bool enabled) {
  s_selfTestMode = enabled;
  if (enabled) {
    s_instrumentationEnabled = true;
    s_writesEnabled = false;
    s_experimentalNativeEnabled = false;
  }
}

#endif

} // namespace dr2hook
