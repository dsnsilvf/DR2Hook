#include "dr2hook/cutscene_probe.h"
#include "dr2hook/logger.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <MinHook.h>
#include <windows.h>
#endif

namespace dr2hook {
namespace {

std::atomic<int> g_startMode{0}; // CutsceneProbe::StartMode

bool InstantStart() {
  return g_startMode.load(std::memory_order_relaxed) !=
         static_cast<int>(CutsceneProbe::StartMode::Normal);
}

#if defined(_WIN32)

constexpr uintptr_t kImageBase = 0x140000000;

// StartLightsEvent::Execute(this) (vtable 0x1412917b8, slot 2): com <Start>
// (+0x129) e ainda nao concluido (+0x121), immediate (+0x124) escolhe entre
// 0x1405efdc0 (largada imediata) e 0x1405f6280 (contagem das luzes).
constexpr uintptr_t kStartLightsExecuteRva = 0x1405f5ef0 - kImageBase;
constexpr uint8_t kStartLightsExecutePrologue[] = {
    0x40, 0x53,                              // push rbx
    0x48, 0x83, 0xec, 0x20,                  // sub rsp, 20h
    0x80, 0xb9, 0x21, 0x01, 0x00, 0x00, 0x00 // cmp byte [rcx+121h], 0
};
constexpr size_t kLightsDone = 0x121;
constexpr size_t kLightsImmediate = 0x124;
constexpr size_t kLightsHasStart = 0x129;

// Keyframe::Fire(this, flag) (chamado pela atualizacao da timeline em
// 0x140b3f605): avalia as condicoes (+0x40..+0x48, resultado em +0xec) e, se
// passam, chama o slot 3 de cada evento (+0x18..+0x20). Nome em +0x60; nome
// do evento em +0x8.
constexpr uintptr_t kKeyframeFireRva = 0x140b37260 - kImageBase;
constexpr uint8_t kKeyframeFirePrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, // mov [rsp+10h], rbx
    0x48, 0x89, 0x74, 0x24, 0x18, // mov [rsp+18h], rsi
    0x57,                         // push rdi
    0x48, 0x83, 0xec, 0x20        // sub rsp, 20h
};
constexpr size_t kKeyframeEventsBegin = 0x18;
constexpr size_t kKeyframeEventsEnd = 0x20;
constexpr size_t kKeyframeName = 0x60;
constexpr size_t kKeyframeConditionsPassed = 0xec;
constexpr size_t kEventName = 0x8;

// OSD de tempo do rival nos keyframes "rival_time" (staging, maxTime 4.0) e
// "kf_rival_time" (largada de rali, 2.0): o roteiro so segue quando o OSD
// termina. Com instant start, maxTime vira kShortRivalTime. Validado no jogo:
// staging -> largada caiu de 4,37 s para 1,20 s.
constexpr uintptr_t kOsdEventVtableRva = 0x141225b30 - kImageBase;
constexpr size_t kOsdMaxTime = 0x9c;
constexpr float kShortRivalTime = 0.05f;

// StatePreraceWaitForPlayer (vtable 0x14124c510), Update = slot 6
// (0x1402a9500): entrada em +0x40 (+0x28 freio de mao, +0x00 acelerador);
// +0xac acumula o tempo com o freio de mao acima de 0,01 (zera ao soltar) e,
// passando de 0,5 s, marca +0xb0 e o fluxo segue para a largada.
constexpr uintptr_t kWaitForPlayerVtableRva = 0x14124c510 - kImageBase;
constexpr uintptr_t kWaitForPlayerUpdateRva = 0x1402a9500 - kImageBase;
constexpr uint8_t kWaitForPlayerUpdatePrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, // mov [rsp+8], rbx
    0x48, 0x89, 0x74, 0x24, 0x10, // mov [rsp+10h], rsi
    0x57,                         // push rdi
    0x48, 0x83, 0xec, 0x30        // sub rsp, 30h
};
constexpr size_t kWaitInput = 0x40;
constexpr size_t kWaitHeldTime = 0xac;
constexpr size_t kWaitReady = 0xb0;
constexpr size_t kInputThrottle = 0x00;
constexpr size_t kInputHandbrake = 0x28;
constexpr float kThrottlePressed = 0.1f;

using StartLightsExecuteFn = bool (*)(uint8_t *event);
using WaitUpdateFn = void (*)(uint8_t *state, float dt);
using KeyframeFireFn = void (*)(uint8_t *keyframe, uint8_t flag);

StartLightsExecuteFn g_originalLightsExecute = nullptr;
KeyframeFireFn g_originalKeyframeFire = nullptr;
void *g_lightsTarget = nullptr;
void *g_keyframeTarget = nullptr;
WaitUpdateFn g_originalWaitUpdate = nullptr;
void *g_waitTarget = nullptr;
uint8_t *g_runningLights = nullptr; // so a thread do jogo toca
uintptr_t g_gameBase = 0;

// maxTime original de cada OSD encurtado, para devolver ao desligar ou no F8.
std::mutex g_osdMutex;
std::vector<std::pair<float *, float>> g_shortenedOsd;

bool IsRivalTimeKeyframe(const uint8_t *keyframe) {
  const char *name = reinterpret_cast<const char *>(keyframe + kKeyframeName);
  return std::strcmp(name, "rival_time") == 0 ||
         std::strcmp(name, "kf_rival_time") == 0;
}

void ShortenRivalTime(uint8_t *keyframe) {
  const auto *begin =
      *reinterpret_cast<uint8_t *const *const *>(keyframe + kKeyframeEventsBegin);
  const auto *end =
      *reinterpret_cast<uint8_t *const *const *>(keyframe + kKeyframeEventsEnd);
  if (begin == nullptr || end <= begin || end - begin > 32) return;
  std::lock_guard<std::mutex> lock(g_osdMutex);
  for (const auto *it = begin; it != end; ++it) {
    uint8_t *event = *it;
    if (event == nullptr ||
        *reinterpret_cast<uintptr_t *>(event) != g_gameBase + kOsdEventVtableRva) {
      continue;
    }
    auto *maxTime = reinterpret_cast<float *>(event + kOsdMaxTime);
    if (*maxTime <= kShortRivalTime) continue;
    g_shortenedOsd.emplace_back(maxTime, *maxTime);
    *maxTime = kShortRivalTime;
  }
}

void RestoreRivalTime() {
  std::lock_guard<std::mutex> lock(g_osdMutex);
  for (const auto &[maxTime, original] : g_shortenedOsd) *maxTime = original;
  if (!g_shortenedOsd.empty()) {
    Logger::Info("Cutscene: tempo do rival restaurado em " +
                 std::to_string(g_shortenedOsd.size()) + " OSD.");
  }
  g_shortenedOsd.clear();
}

// Chamadas dentro dos detours; o Shutdown espera zerar antes do F8 liberar
// a DLL.
std::atomic<int> g_inFlight{0};
struct InFlight {
  InFlight() { g_inFlight.fetch_add(1); }
  ~InFlight() { g_inFlight.fetch_sub(1); }
};

bool DetourStartLightsExecute(uint8_t *event) {
  InFlight guard;
  const bool pending = event[kLightsDone] == 0 && event[kLightsHasStart] != 0;
  if (!pending) return g_originalLightsExecute(event);

  const bool instant = InstantStart();
  if (g_runningLights != event) {
    g_runningLights = event;
    Logger::Info(std::string("Cutscene: StartLights iniciado (") +
                 (event[kLightsImmediate] ? "imediato" : "contagem") +
                 (instant ? ", instant start forcado" : "") + ").");
  }
  const uint8_t immediate = event[kLightsImmediate];
  if (instant) event[kLightsImmediate] = 1;
  const bool result = g_originalLightsExecute(event);
  event[kLightsImmediate] = immediate;
  if (event[kLightsDone] != 0) {
    g_runningLights = nullptr;
    Logger::Info("Cutscene: StartLights concluido.");
  }
  return result;
}

void DetourKeyframeFire(uint8_t *keyframe, uint8_t flag) {
  InFlight guard;
  if (InstantStart() &&
      IsRivalTimeKeyframe(keyframe)) {
    ShortenRivalTime(keyframe);
  }
  g_originalKeyframeFire(keyframe, flag);
  char name[64];
  std::snprintf(name, sizeof(name), "%.48s",
                reinterpret_cast<const char *>(keyframe + kKeyframeName));
  std::string line =
      std::string("Cutscene: keyframe '") + name + "'" +
      (keyframe[kKeyframeConditionsPassed] ? "" : " (condicao falhou)");
  const auto *begin =
      *reinterpret_cast<uint8_t *const *const *>(keyframe + kKeyframeEventsBegin);
  const auto *end =
      *reinterpret_cast<uint8_t *const *const *>(keyframe + kKeyframeEventsEnd);
  if (begin != nullptr && end > begin && end - begin <= 32) {
    line += " eventos:";
    for (const auto *it = begin; it != end; ++it) {
      if (*it == nullptr) continue;
      char eventName[48];
      std::snprintf(eventName, sizeof(eventName), " %.40s",
                    reinterpret_cast<const char *>(*it + kEventName));
      line += eventName;
    }
  }
  Logger::Info(line);
}

void DetourWaitForPlayerUpdate(uint8_t *state, float dt) {
  InFlight guard;
  const auto mode =
      static_cast<CutsceneProbe::StartMode>(g_startMode.load(std::memory_order_relaxed));
  auto *input = *reinterpret_cast<uint8_t **>(state + kWaitInput);
  const bool waiting =
      *reinterpret_cast<uintptr_t *>(state) == g_gameBase + kWaitForPlayerVtableRva &&
      input != nullptr && state[kWaitReady] == 0;
  bool release = false;
  if (waiting && mode == CutsceneProbe::StartMode::Automatic) {
    release = true;
  } else if (waiting && mode == CutsceneProbe::StartMode::OnThrottle) {
    release = *reinterpret_cast<float *>(input + kInputThrottle) > kThrottlePressed;
  }
  if (!release) {
    g_originalWaitUpdate(state, dt);
    return;
  }
  // Simula o freio de mao segurado alem dos 0,5 s exigidos so durante a
  // chamada; a entrada real volta logo depois.
  auto *handbrake = reinterpret_cast<float *>(input + kInputHandbrake);
  const float realHandbrake = *handbrake;
  *handbrake = 1.0f;
  auto *held = reinterpret_cast<float *>(state + kWaitHeldTime);
  if (*held < 1.0f) *held = 1.0f;
  g_originalWaitUpdate(state, dt);
  *handbrake = realHandbrake;
  Logger::Info(mode == CutsceneProbe::StartMode::Automatic
                   ? "Cutscene: largada liberada (automatica)."
                   : "Cutscene: largada liberada pelo acelerador.");
}

bool Hook(uintptr_t gameBase, uintptr_t rva, const uint8_t *prologue,
          size_t size, void *detour, void **original, void **target,
          const char *name) {
  void *fn = reinterpret_cast<void *>(gameBase + rva);
  if (std::memcmp(fn, prologue, size) != 0) {
    Logger::Warn(std::string("Cutscene: prologo de ") + name +
                 " diferente do esperado; hook abortado.");
    return false;
  }
  if (MH_CreateHook(fn, detour, original) != MH_OK) {
    Logger::Error(std::string("Cutscene: falha ao criar hook de ") + name);
    return false;
  }
  if (MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    Logger::Error(std::string("Cutscene: falha ao habilitar hook de ") + name);
    return false;
  }
  *target = fn;
  return true;
}

#endif

} // namespace

bool CutsceneProbe::Install(uintptr_t gameBase) {
#if defined(_WIN32)
  if (gameBase == 0) return false;
  g_gameBase = gameBase;
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    Logger::Error("Cutscene: MH_Initialize falhou.");
    return false;
  }
  const bool lights =
      Hook(gameBase, kStartLightsExecuteRva, kStartLightsExecutePrologue,
           sizeof(kStartLightsExecutePrologue),
           reinterpret_cast<void *>(&DetourStartLightsExecute),
           reinterpret_cast<void **>(&g_originalLightsExecute), &g_lightsTarget,
           "StartLightsEvent::Execute");
  const bool keyframes =
      Hook(gameBase, kKeyframeFireRva, kKeyframeFirePrologue,
           sizeof(kKeyframeFirePrologue),
           reinterpret_cast<void *>(&DetourKeyframeFire),
           reinterpret_cast<void **>(&g_originalKeyframeFire), &g_keyframeTarget,
           "Keyframe::Fire");
  const bool wait =
      Hook(gameBase, kWaitForPlayerUpdateRva, kWaitForPlayerUpdatePrologue,
           sizeof(kWaitForPlayerUpdatePrologue),
           reinterpret_cast<void *>(&DetourWaitForPlayerUpdate),
           reinterpret_cast<void **>(&g_originalWaitUpdate), &g_waitTarget,
           "StatePreraceWaitForPlayer::Update");
  Logger::Info(std::string("Cutscene: hooks no core (StartLights ") +
               (lights ? "ok" : "FALHOU") + ", Keyframe " +
               (keyframes ? "ok" : "FALHOU") + ", WaitForPlayer " +
               (wait ? "ok" : "FALHOU") + ").");
  return lights || keyframes || wait;
#else
  (void)gameBase;
  return false;
#endif
}

void CutsceneProbe::Shutdown() {
#if defined(_WIN32)
  for (void *target : {g_lightsTarget, g_keyframeTarget, g_waitTarget}) {
    if (target != nullptr) MH_DisableHook(target);
  }
  // Com os hooks desligados ninguem novo entra; espera quem ja esta dentro.
  for (int waited = 0; g_inFlight.load() > 0 && waited < 2000; ++waited) {
    Sleep(1);
  }
  for (void **target : {&g_lightsTarget, &g_keyframeTarget, &g_waitTarget}) {
    if (*target != nullptr) {
      MH_RemoveHook(*target);
      *target = nullptr;
    }
  }
  g_runningLights = nullptr;
  RestoreRivalTime(); // os objetos do jogo sobrevivem ao F8
#endif
}

void CutsceneProbe::SetStartMode(StartMode mode) {
  static constexpr const char *kNames[] = {"normal", "no countdown",
                                           "automatic", "on throttle"};
  const int value = static_cast<int>(mode);
  if (g_startMode.exchange(value) != value) {
    Logger::Info(std::string("Cutscene: largada em modo ") + kNames[value] + ".");
  }
#if defined(_WIN32)
  if (mode == StartMode::Normal) RestoreRivalTime();
#endif
}

CutsceneProbe::StartMode CutsceneProbe::GetStartMode() {
  return static_cast<StartMode>(g_startMode.load());
}

bool CutsceneProbe::SetStartMode(const char *name) {
  if (name == nullptr) return false;
  const std::string value(name);
  if (value == "normal") SetStartMode(StartMode::Normal);
  else if (value == "no_countdown") SetStartMode(StartMode::NoCountdown);
  else if (value == "automatic") SetStartMode(StartMode::Automatic);
  else if (value == "on_throttle") SetStartMode(StartMode::OnThrottle);
  else return false;
  return true;
}

} // namespace dr2hook
