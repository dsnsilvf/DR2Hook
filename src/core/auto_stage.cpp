#include "dr2hook/auto_stage.h"
#include "dr2hook/host.h"
#include "dr2hook/logger.h"
#include "dr2hook/race_events.h"

#include <MinHook.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace dr2hook {
namespace {

constexpr uintptr_t kImageBase = 0x140000000;
constexpr uintptr_t kBenchmarkInitRva = 0x1409d31c0 - kImageBase; // 0x9d31c0
constexpr uint8_t kBenchmarkInitPrologue[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, // mov [rsp+8], rbx
    0x48, 0x89, 0x6c, 0x24, 0x10, // mov [rsp+10h], rbp
    0x48, 0x89, 0x74, 0x24, 0x18, // mov [rsp+18h], rsi
    0x48, 0x89, 0x7c, 0x24, 0x20, // mov [rsp+20h], rdi
    0x41, 0x56                    // push r14
};

// Cria "<Documentos>\\benchmarks". O Initialize nativo so chama quando acha
// "-benchmark" no argv; quando forcamos o modo, chamamos nos mesmos.
constexpr uintptr_t kBenchmarkMakeDirRva = 0x1409d0340 - kImageBase;

// Tela de carregamento: o filler 0x14022ded0 lê distância e elevação de
// [jogo + 0x34c8/+0x34cc] (jogo = 0x140559f10()), copiadas do evento
// (+0xb040/+0xb044) em 0x14043853d. Pelo caminho do benchmark elas ficam 0 e a
// tela mostra "0.00km"; aí completamos com a linha da rota no catálogo
// (track_model: campo 15 = extensão em +0x68, campo 19 = elevação em +0x80; +0x70
// é a chave do nome, conferida antes de ler).
constexpr uintptr_t kLoadingFillRva = 0x14022ded0 - kImageBase;
constexpr uint8_t kLoadingFillPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57,
                                            0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr uintptr_t kGameRva = 0x140559f10 - kImageBase;
constexpr uintptr_t kCatalogueRowRva = 0x1400e89a0 - kImageBase;
constexpr uintptr_t kCatalogueRva = 0x141695160 - kImageBase;
constexpr uint64_t kTrackModelTable = 0xb8e12aaaull << 32;
constexpr uintptr_t kGameRouteSource = 0x32a0;
constexpr uintptr_t kGameDistance = 0x34c8;
constexpr uintptr_t kGameElevation = 0x34cc;
constexpr uintptr_t kRouteLength = 0x68;
constexpr uintptr_t kRouteName = 0x70;
constexpr uintptr_t kRouteElevation = 0x80;

using LoadingFillFn = void (*)(void *screen);
using GameFn = uint8_t *(*)();
using CatalogueRowFn = uint8_t **(*)(void *catalogue, uint64_t key, void *out);

LoadingFillFn g_originalLoadingFill = nullptr;
uintptr_t g_gameBase = 0;

using BenchmarkInitFn = void *(*)(void *thisPtr, void *arg2, void *arg3,
                                  void *arg4);
using BenchmarkMakeDirFn = void (*)(void *thisPtr);

BenchmarkInitFn g_originalBenchmarkInit = nullptr;
BenchmarkMakeDirFn g_benchmarkMakeDir = nullptr;
void *g_benchmarkInitTarget = nullptr;
std::mutex g_configMutex;
AutoStageConfig g_config;
bool g_configLoaded = false;
bool g_hookInstalled = false;

// O hook entra no DllMain, antes do Logger abrir; o detour pode rodar antes
// ou depois do Logger::Init. Guardamos o que aconteceu e LogAutoStageStatus
// relata quando o log estiver pronto.
std::atomic<bool> g_detourRan{false};
std::atomic<bool> g_detourReported{false};
std::atomic<uint32_t> g_nativeEnabled{0};
std::atomic<bool> g_applied{false};
std::atomic<bool> g_logReady{false};

std::string Trim(const std::string &str) {
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first + 1));
}

bool ParseBool(const std::string &val) {
  std::string s = val;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return (s == "1" || s == "true" || s == "yes" || s == "on");
}

void CopyStringPadded(char *dst, const std::string &src, size_t maxLen) {
  if (dst == nullptr || maxLen == 0) return;
  std::memset(dst, 0, maxLen);
  const size_t copyLen = (std::min)(src.size(), maxLen - 1);
  std::memcpy(dst, src.data(), copyLen);
  dst[copyLen] = '\0';
}

std::filesystem::path GetGameDirectory() {
#if defined(_WIN32)
  char exePath[MAX_PATH] = {};
  if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
    return std::filesystem::path(exePath).parent_path();
  }
#endif
  return std::filesystem::current_path();
}

void WriteIniBool(const std::filesystem::path &iniPath, const std::string &key,
                  bool value) {
  if (!std::filesystem::exists(iniPath)) return;
  std::ifstream in(iniPath);
  if (!in.is_open()) return;

  std::vector<std::string> lines;
  std::string line;
  bool found = false;

  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (eq != std::string::npos) {
      std::string k = Trim(line.substr(0, eq));
      if (k == key) {
        lines.push_back(key + " = " + (value ? "1" : "0"));
        found = true;
        continue;
      }
    }
    lines.push_back(line);
  }
  in.close();

  if (!found) {
    lines.push_back(key + " = " + (value ? "1" : "0"));
  }

  std::ofstream out(iniPath, std::ios::trunc);
  if (out.is_open()) {
    for (const auto &l : lines) {
      out << l << "\n";
    }
  }
}

void ReportDetour() {
  if (!g_detourRan.load() || g_detourReported.exchange(true)) return;
  Logger::Info(std::string("AutoStage: BenchmarkManager::Initialize executou; "
                           "-benchmark nativo = ") +
               (g_nativeEnabled.load() ? "sim" : "nao") +
               ", override aplicado = " + (g_applied.load() ? "sim" : "nao") +
               ".");
}

void *DetourBenchmarkInit(void *thisPtr, void *arg2, void *arg3, void *arg4) {
  void *res = g_originalBenchmarkInit(thisPtr, arg2, arg3, arg4);

  AutoStageConfig cfg;
  {
    std::lock_guard<std::mutex> lock(g_configMutex);
    cfg = g_config;
  }

  const uint32_t nativeEnabled =
      thisPtr != nullptr
          ? *reinterpret_cast<const uint32_t *>(
                static_cast<const char *>(thisPtr) +
                benchmark_offsets::kBenchmarkEnabled)
          : 0;
  g_nativeEnabled.store(nativeEnabled);

  if (cfg.enabled && thisPtr != nullptr) {
    ApplyAutoStage(thisPtr, cfg);
    if (nativeEnabled == 0 && g_benchmarkMakeDir != nullptr) {
      g_benchmarkMakeDir(thisPtr);
    }
    g_applied.store(true);

    if (cfg.once) {
      std::lock_guard<std::mutex> lock(g_configMutex);
      g_config.enabled = false;
      const auto iniPath = GetGameDirectory() / "dr2hook_autostage.ini";
      WriteIniBool(iniPath, "enabled", false);
      Logger::Info("AutoStage: flag 'once=1' ativa; auto-stage desabilitado para o proximo boot.");
    }
  }

  g_detourRan.store(true);
  if (g_logReady.load()) ReportDetour();
  return res;
}

} // namespace

void ApplyAutoStage(void *benchmarkManager, const AutoStageConfig &config) {
  if (benchmarkManager == nullptr) return;

  char *base = reinterpret_cast<char *>(benchmarkManager);

  // 1. Habilita o Fast-Path (bypass da UI de entrada)
  *reinterpret_cast<uint32_t *>(base + benchmark_offsets::kBenchmarkEnabled) = 1;

  // 2. Comportamento de camera
  *reinterpret_cast<uint8_t *>(base + benchmark_offsets::kCycleCamera) =
      config.cycleCamera ? 1 : 0;

  // 3. Parametros do estagio
  CopyStringPadded(base + benchmark_offsets::kLocation, config.location, 32);
  CopyStringPadded(base + benchmark_offsets::kTrack, config.track, 32);
  CopyStringPadded(base + benchmark_offsets::kRoute, config.route, 32);
  CopyStringPadded(base + benchmark_offsets::kTimeOfDay, config.timeOfDay, 32);
  CopyStringPadded(base + benchmark_offsets::kSurface, config.surface, 32);
  CopyStringPadded(base + benchmark_offsets::kVehicle, config.vehicle, 32);
  CopyStringPadded(base + benchmark_offsets::kLivery, config.livery, 32);

  // 4. Sessao de teste ativa
  *reinterpret_cast<uint32_t *>(base + benchmark_offsets::kActive) = 1;

  Logger::Info("AutoStage: Fast-path configurado com sucesso! Carregando [" +
               config.location + " / " + config.track + " / " + config.route +
               "] com [" + config.vehicle + "]...");
}

bool LoadAutoStageConfig(AutoStageConfig *outConfig) {
  std::lock_guard<std::mutex> lock(g_configMutex);

  AutoStageConfig cfg;
  const auto iniPath = GetGameDirectory() / "dr2hook_autostage.ini";

  if (std::filesystem::exists(iniPath)) {
    std::ifstream in(iniPath);
    if (in.is_open()) {
      std::string line;
      while (std::getline(in, line)) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == ';' || s[0] == '#') continue;

        auto eq = s.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(s.substr(0, eq));
        std::string val = Trim(s.substr(eq + 1));
        std::string keyLower = key;
        std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (keyLower == "enabled") cfg.enabled = ParseBool(val);
        else if (keyLower == "once") cfg.once = ParseBool(val);
        else if (keyLower == "location") cfg.location = val;
        else if (keyLower == "track") cfg.track = val;
        else if (keyLower == "route") cfg.route = val;
        else if (keyLower == "time_of_day" || keyLower == "timeofday" || keyLower == "weather") cfg.timeOfDay = val;
        else if (keyLower == "surface") cfg.surface = val;
        else if (keyLower == "vehicle" || keyLower == "car") cfg.vehicle = val;
        else if (keyLower == "livery") cfg.livery = val;
        else if (keyLower == "cycle_camera") cfg.cycleCamera = ParseBool(val);
      }
    }
  }

#if defined(_WIN32)
  char envVal[128] = {};
  if (GetEnvironmentVariableA("DR2HOOK_AUTOSTAGE", envVal, sizeof(envVal)) > 0) {
    cfg.enabled = ParseBool(envVal);
  }
  if (GetEnvironmentVariableA("DR2HOOK_STAGE_LOCATION", envVal, sizeof(envVal)) > 0) {
    cfg.location = envVal;
  }
  if (GetEnvironmentVariableA("DR2HOOK_STAGE_TRACK", envVal, sizeof(envVal)) > 0) {
    cfg.track = envVal;
  }
  if (GetEnvironmentVariableA("DR2HOOK_STAGE_ROUTE", envVal, sizeof(envVal)) > 0) {
    cfg.route = envVal;
  }
  if (GetEnvironmentVariableA("DR2HOOK_STAGE_VEHICLE", envVal, sizeof(envVal)) > 0) {
    cfg.vehicle = envVal;
  }
  if (GetEnvironmentVariableA("DR2HOOK_STAGE_WEATHER", envVal, sizeof(envVal)) > 0) {
    cfg.timeOfDay = envVal;
  }
  if (GetEnvironmentVariableA("DR2HOOK_STAGE_SURFACE", envVal, sizeof(envVal)) > 0) {
    cfg.surface = envVal;
  }
#endif

  g_config = cfg;
  g_configLoaded = true;

  if (outConfig != nullptr) {
    *outConfig = cfg;
  }

  if (cfg.enabled) {
    Logger::Info("AutoStage configurado: ATIVO -> [" + cfg.location + " / " +
                 cfg.track + " / " + cfg.route + "] com [" + cfg.vehicle + "].");
  }

  return cfg.enabled;
}

static void DetourLoadingFill(void *screen) {
  uint8_t *game = reinterpret_cast<GameFn>(g_gameBase + kGameRva)();
  auto *distance = game != nullptr ? reinterpret_cast<float *>(game + kGameDistance) : nullptr;
  void *source = game != nullptr ? *reinterpret_cast<void **>(game + kGameRouteSource) : nullptr;
  void *catalogue = *reinterpret_cast<void **>(g_gameBase + kCatalogueRva);
  if (distance != nullptr && source != nullptr && catalogue != nullptr) {
    using RouteIdFn = uint32_t (*)(void *, int);
    const uint32_t routeId = (*reinterpret_cast<RouteIdFn **>(source))[2](source, 0);
    uint8_t **row = reinterpret_cast<CatalogueRowFn>(g_gameBase + kCatalogueRowRva)(
        catalogue, kTrackModelTable | routeId, nullptr);
    const uint8_t *fields = row != nullptr ? *row : nullptr;
    const char *nameKey = fields != nullptr ? *reinterpret_cast<const char *const *>(fields + kRouteName) : nullptr;
    const bool known = nameKey != nullptr && std::strncmp(nameKey, "lng_", 4) == 0;
    if (known) {
      NotifyRoute(nameKey + 4); // chave da pose da largada (LoadView, olhar=auto)
    }
    if (known && *distance == 0.0f) {
      *distance = *reinterpret_cast<const float *>(fields + kRouteLength);
      auto *elevation = reinterpret_cast<float *>(game + kGameElevation);
      if (*elevation == 0.0f) {
        *elevation = *reinterpret_cast<const float *>(fields + kRouteElevation);
      }
      Logger::Info("AutoStage: tela de carregamento da rota " + std::to_string(routeId) +
                   " completada (" + nameKey + "): " + std::to_string(*distance) + " m, elevacao " +
                   std::to_string(*elevation) + ".");
    }
  }
  g_originalLoadingFill(screen);
}

static void InstallLoadingFillHook(uintptr_t gameBase) {
  void *target = reinterpret_cast<void *>(gameBase + kLoadingFillRva);
  if (std::memcmp(target, kLoadingFillPrologue, sizeof(kLoadingFillPrologue)) != 0) {
    Logger::Warn("AutoStage: filler da tela de carregamento diferente do esperado; distancia fica como o jogo deixar.");
    return;
  }
  g_gameBase = gameBase;
  if (MH_CreateHook(target, reinterpret_cast<void *>(&DetourLoadingFill),
                    reinterpret_cast<void **>(&g_originalLoadingFill)) != MH_OK ||
      MH_EnableHook(target) != MH_OK) {
    Logger::Warn("AutoStage: falha no hook da tela de carregamento.");
  }
}

AutoStageConfig GetAutoStageConfig() {
  {
    std::lock_guard<std::mutex> lock(g_configMutex);
    if (g_configLoaded) return g_config;
  }
  AutoStageConfig cfg;
  LoadAutoStageConfig(&cfg);
  return cfg;
}

void SetAutoStageConfig(const AutoStageConfig &config) {
  std::lock_guard<std::mutex> lock(g_configMutex);
  g_config = config;
  g_configLoaded = true;
}

bool InstallAutoStageHook() {
  if (g_hookInstalled) return true;

  LoadAutoStageConfig();

#if !defined(_WIN32)
  Logger::Warn("AutoStage: hooks indisponiveis em builds nao-Windows.");
  return false;
#else
  uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  if (gameBase == 0) {
    Logger::Warn("AutoStage: nao foi possivel obter o endereco base do executavel.");
    return false;
  }

  void *target = reinterpret_cast<void *>(gameBase + kBenchmarkInitRva);

  // Verificacao do prologo
  if (std::memcmp(target, kBenchmarkInitPrologue, sizeof(kBenchmarkInitPrologue)) != 0) {
    Logger::Warn("AutoStage: prologo de BenchmarkManager::Initialize nao corresponde. Hook abortado.");
    return false;
  }

  MH_STATUS status = MH_CreateHook(
      target, reinterpret_cast<void *>(&DetourBenchmarkInit),
      reinterpret_cast<void **>(&g_originalBenchmarkInit));
  if (status != MH_OK) {
    Logger::Error("AutoStage: falha ao criar hook de BenchmarkManager::Initialize (" +
                  std::to_string(status) + ")");
    return false;
  }

  status = MH_EnableHook(target);
  if (status != MH_OK) {
    Logger::Error("AutoStage: falha ao habilitar hook de BenchmarkManager::Initialize (" +
                  std::to_string(status) + ")");
    MH_RemoveHook(target);
    return false;
  }

  g_benchmarkMakeDir =
      reinterpret_cast<BenchmarkMakeDirFn>(gameBase + kBenchmarkMakeDirRva);
  InstallLoadingFillHook(gameBase);
  g_benchmarkInitTarget = target;
  g_hookInstalled = true;
  return true;
#endif
}

void LogAutoStageStatus() {
  g_logReady.store(true);
  const AutoStageConfig cfg = GetAutoStageConfig();
  if (!g_hookInstalled) {
    if (cfg.enabled) {
      Logger::Warn("AutoStage: ativo no ini, mas o hook nao foi instalado "
                   "(prologo diferente ou falha do MinHook).");
    }
    return;
  }
  Logger::Info("AutoStage: hook de BenchmarkManager::Initialize ativo desde o "
               "attach; config " +
               std::string(cfg.enabled ? "ATIVA" : "inativa") + " -> [" +
               cfg.location + " / " + cfg.track + " / " + cfg.route +
               "] com [" + cfg.vehicle + "].");
  if (g_detourRan.load()) {
    ReportDetour();
  } else {
    Logger::Info("AutoStage: Initialize ainda nao executou.");
  }
}

void UninstallAutoStageHook() {
#if defined(_WIN32)
  if (!g_hookInstalled || g_benchmarkInitTarget == nullptr) return;

  MH_DisableHook(g_benchmarkInitTarget);
  MH_RemoveHook(g_benchmarkInitTarget);
  g_benchmarkInitTarget = nullptr;
  g_originalBenchmarkInit = nullptr;
  g_hookInstalled = false;
  Logger::Info("AutoStage: hook desinstalado.");
#endif
}

} // namespace dr2hook
