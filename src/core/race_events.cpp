#include "dr2hook/race_events.h"
#include "dr2hook/load_probe.h"
#include "dr2hook/logger.h"

#include <MinHook.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <windows.h>

namespace dr2hook {
namespace {

constexpr uintptr_t kImageBase = 0x140000000;
// RaceSession::OnNamedEvent(this, name): compara name com "racestart" e
// "startlightsstart". Chamado por 0x140533c90 (luzes) e 0x140533d10 (largada).
constexpr uintptr_t kRaceEventRva = 0x1405248e0 - kImageBase;
constexpr uint8_t kRaceEventPrologue[] = {
    0x40, 0x53,             // push rbx
    0x56,                   // push rsi
    0x57,                   // push rdi
    0x48, 0x83, 0xec, 0x20, // sub rsp, 20h
    0x48, 0x8b, 0xf9        // mov rdi, rcx
};

using RaceEventFn = void (*)(void *session, const char *name);


constexpr size_t kMaxQueuedEvents = 32;
// O jogo abre o .nefs da localidade duas vezes seguidas por carregamento.
constexpr ULONGLONG kLoadDedupMs = 2000;
// Num reinicio o jogo ja disparou "racestart" duas vezes com 4 ms de
// diferenca (2026-10-01 20:43:00).
constexpr ULONGLONG kStartDedupMs = 1000;

RaceEventFn g_originalRaceEvent = nullptr;
void *g_target = nullptr;

std::mutex g_mutex;
std::deque<Dr2StageEvent> g_events;
std::string g_stage;        // pista do ultimo carregamento
std::string g_route;        // rota do ultimo carregamento ("montalegre_rallycross_route_0")
int g_lights = 0;           // luzes desde a ultima largada
bool g_startedSinceLoad = false;
ULONGLONG g_lastLoadTick = 0;
ULONGLONG g_lastStartTick = 0;

// Chamar com g_mutex travado.
void Push(Dr2StageEventKind kind, int value) {
  if (g_events.size() >= kMaxQueuedEvents) g_events.pop_front();
  Dr2StageEvent event{};
  event.kind = kind;
  event.value = value;
  std::snprintf(event.name, sizeof(event.name), "%s", g_stage.c_str());
  g_events.push_back(event);
}

void DetourRaceEvent(void *session, const char *name) {
  LoadProbeMark(name);
  if (name != nullptr) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (std::strcmp(name, "startlightsstart") == 0) {
      Push(kDr2StageCountdown, ++g_lights);
    } else if (std::strcmp(name, "racestart") == 0 &&
               GetTickCount64() - g_lastStartTick >= kStartDedupMs) {
      g_lastStartTick = GetTickCount64();
      const bool restart = g_startedSinceLoad;
      Push(kDr2StageStart, restart ? 1 : 0);
      g_startedSinceLoad = true;
      g_lights = 0;
      Logger::Info("RaceEvent: largada em '" + g_stage + "'" +
                   (restart ? " (reinicio)." : "."));
    }
  }
  g_originalRaceEvent(session, name);
}

bool Hook(uintptr_t rva, const uint8_t *prologue, size_t size, void *detour,
          void **original, void **target, const char *name) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  void *fn = reinterpret_cast<void *>(base + rva);
  if (base == 0 || std::memcmp(fn, prologue, size) != 0) {
    Logger::Warn(std::string("RaceEvent: prologo de ") + name +
                 " diferente do esperado; hook abortado.");
    return false;
  }
  if (MH_CreateHook(fn, detour, original) != MH_OK) {
    Logger::Error(std::string("RaceEvent: falha ao criar hook de ") + name);
    return false;
  }
  if (MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    Logger::Error(std::string("RaceEvent: falha ao habilitar hook de ") + name);
    return false;
  }
  *target = fn;
  return true;
}

} // namespace

bool InstallRaceEventsHook() {
  const bool events =
      Hook(kRaceEventRva, kRaceEventPrologue, sizeof(kRaceEventPrologue),
           reinterpret_cast<void *>(&DetourRaceEvent),
           reinterpret_cast<void **>(&g_originalRaceEvent), &g_target,
           "RaceSession::OnNamedEvent");
  Logger::Info(std::string("RaceEvent: hook de OnNamedEvent ") +
               (events ? "ativo." : "FALHOU."));
  return events;
}

void NotifyStageLoad(const char *nefsFileName) {
  if (nefsFileName == nullptr) return;
  // "<local>__<pista>.nefs" -> "<pista>"
  std::string stage(nefsFileName);
  const size_t sep = stage.find("__");
  if (sep != std::string::npos) stage = stage.substr(sep + 2);
  const size_t dot = stage.rfind('.');
  if (dot != std::string::npos) stage.resize(dot);

  std::lock_guard<std::mutex> lock(g_mutex);
  const ULONGLONG now = GetTickCount64();
  if (stage == g_stage && now - g_lastLoadTick < kLoadDedupMs) return;
  g_lastLoadTick = now;
  g_stage = stage;
  g_route.clear();
  g_lights = 0;
  g_startedSinceLoad = false;
  Push(kDr2StageLoad, 0);
  Logger::Info("RaceEvent: carregando '" + stage + "'.");
}

void NotifyRoute(const char *routeKey) {
  if (routeKey == nullptr || *routeKey == '\0') return;
  const std::string key(routeKey);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (key == g_route) return;
  g_route = key;
  if (g_events.size() >= kMaxQueuedEvents) g_events.pop_front();
  Dr2StageEvent event{};
  event.kind = kDr2StageRoute;
  std::snprintf(event.name, sizeof(event.name), "%s", key.c_str());
  g_events.push_back(event);
  Logger::Info("RaceEvent: rota '" + key + "'.");
}

void UninstallRaceEventsHook() {
  for (void **target : {&g_target}) {
    if (*target == nullptr) continue;
    MH_DisableHook(*target);
    MH_RemoveHook(*target);
    *target = nullptr;
  }
}

} // namespace dr2hook

int Dr2Host_StageConsumeEvent(dr2hook::Dr2StageEvent *event) {
  std::lock_guard<std::mutex> lock(dr2hook::g_mutex);
  if (event == nullptr || dr2hook::g_events.empty()) return 0;
  *event = dr2hook::g_events.front();
  dr2hook::g_events.pop_front();
  return 1;
}

#else

namespace dr2hook {
bool InstallRaceEventsHook() { return false; }
void UninstallRaceEventsHook() {}
void NotifyStageLoad(const char *) {}
void NotifyRoute(const char *) {}
} // namespace dr2hook

int Dr2Host_StageConsumeEvent(dr2hook::Dr2StageEvent *) { return 0; }

#endif
