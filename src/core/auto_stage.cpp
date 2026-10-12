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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
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

// Condições da especial (hora e tempo). O benchmark não lê os textos do BM
// (+0x108/+0x128). A montagem da corrida 0x1405b5bc0 (vtable 0x14128b178)
// copia um bloco global (0x1409d2b10() = 0x141f59940) para o evento, mas logo
// depois sobrescreve a struct de condições (+0x108, 0x18 bytes) com uma nova:
// 0x1409cba30(sol 90.0) + 0x1409cb9f0(.., tempo 0, variação 0), gravada por
// 0x1409d8ef0 (chamada em 0x1405b6311). Resultado: sempre meio-dia e seco. O
// evento vira corrida+0x3088, e a luz da especial (0x1406a2750) monta
// "<pista>_<rota>_<hora>_<tempo>_<NN>" dos índices da struct: +0 ângulo do sol
// (float; a carreira, 0x14041fd20, deixa 90 e escolhe só o índice), +4 hora
// (sunrise, midday, sunset, dusk, night), +8 tempo (dry, wet, rain, overcast,
// snow), +0xc variação (o _NN do lighting_*.xml). Ligamos o setter e, só
// quando quem chamou é a montagem do benchmark, trocamos hora e tempo.
constexpr uintptr_t kSetConditionsRva = 0x1409d8ef0 - kImageBase;
constexpr uint8_t kSetConditionsPrologue[] = {0x0f, 0x10, 0x02, 0x0f, 0x11, 0x81, 0x08, 0x01, 0x00, 0x00};
constexpr uintptr_t kBenchmarkConditionsReturnRva = 0x1405b6316 - kImageBase;
constexpr uintptr_t kConditionsTime = 0x108 + 4;
constexpr const char *kTimeNames[] = {"sunrise", "midday", "sunset", "dusk", "night"};
constexpr const char *kWeatherNames[] = {"dry", "wet", "rain", "overcast", "snow"};

// Acerto do carro (vehicle_setup.md, "Acerto do carro"). Na carga, 0x14098c830 prepara o acerto
// (TuningSetup, 0xf0 bytes) de cada veículo: o objeto do veículo guarda o nome do
// carro em +0x131 ("mcs") e o acerto em +0x188. Sem definição ainda, cria a do carro
// (0x140734ca0) e volta aos padrões; depois 0x140734e70 limita cada valor à faixa e
// 0x140735040 recalcula o carimbo (acerto+0xe8) que a física compara em 0x140749c80
// para recarregar. Ligamos o 0x140734e70: depois dele gravamos os valores do ini (só
// no carro do AutoStage, dentro da faixa), e o carimbo já sai com eles.
constexpr uintptr_t kTuningClampRva = 0x140734e70 - kImageBase;
constexpr uint8_t kTuningClampPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56,
                                            0x57, 0x41, 0x56, 0x41, 0x57};
constexpr uintptr_t kTuningInVehicle = 0x188;
constexpr uintptr_t kVehicleName = 0x131;
constexpr uintptr_t kSetupValues = 0x10; // float[51]
constexpr uintptr_t kDefRanges = 0xc90;  // {float padrão, min, max, passos}[51]; os 3 últimos codificados
constexpr uintptr_t kDefState = 0xff4;   // int[51]: 1 ajustável, 0 o carro não tem, 2 fixo
constexpr uint32_t kDecodeMin = 0xccd5aa19;
constexpr uint32_t kDecodeMax = 0xa069a253;
constexpr uint32_t kEncodeSteps = 0x993908c7;
constexpr int kTuningCount = 51;
// Ordem do jogo (init estático 0x14007e1b0; a tela de acerto 0x14030cb30 usa os mesmos índices).
constexpr const char *kTuningNames[kTuningCount] = {
    "brakes_bias", "brakes_strength", "brakes_rear_saturation",
    "differential_front_driving_lock", "differential_front_braking_lock", "differential_front_preload",
    "differential_front_viscosity", "differential_centre_driving_lock", "differential_centre_braking_lock",
    "differential_centre_preload", "differential_centre_viscosity", "differential_centre_torque_distribution",
    "differential_rear_driving_lock", "differential_rear_braking_lock", "differential_rear_preload",
    "differential_rear_viscosity", "gearbox_1", "gearbox_2", "gearbox_3", "gearbox_4", "gearbox_5", "gearbox_6",
    "gearbox_7", "gearbox_final_drive", "suspension_front_ride_height", "suspension_front_spring_rate",
    "suspension_front_tender_spring_rate", "suspension_front_anti_roll", "suspension_front_camber",
    "suspension_front_toe", "suspension_rear_ride_height", "suspension_rear_spring_rate",
    "suspension_rear_tender_spring_rate", "suspension_rear_anti_roll", "suspension_rear_camber",
    "suspension_rear_toe", "damping_front_bump", "damping_front_fast_bump", "damping_front_fast_bump_threshold",
    "damping_front_rebound", "damping_front_fast_rebound", "damping_front_fast_rebound_threshold",
    "damping_rear_bump", "damping_rear_fast_bump", "damping_rear_fast_bump_threshold", "damping_rear_rebound",
    "damping_rear_fast_rebound", "damping_rear_fast_rebound_threshold", "spare_tyre_count",
    "suspension_front_air_pressure", "suspension_rear_air_pressure"};

constexpr uint32_t ModInverse(uint32_t a) {
  uint32_t x = a; // a·a ≡ 1 (mod 8); cada passo de Newton dobra os bits certos
  for (int i = 0; i < 5; ++i) x *= 2u - a * x;
  return x;
}

using LoadingFillFn = void (*)(void *screen);
using SetConditionsFn = void (*)(void *owner, const void *conditions);
using GameFn = uint8_t *(*)();
using CatalogueRowFn = uint8_t **(*)(void *catalogue, uint64_t key, void *out);

LoadingFillFn g_originalLoadingFill = nullptr;
SetConditionsFn g_originalSetConditions = nullptr;
// Índices calculados no Initialize; o detour do setter só os copia.
std::atomic<bool> g_condArmed{false};
std::atomic<int> g_condTime{-1};
std::atomic<int> g_condWeather{-1};
std::atomic<uint32_t> g_condBefore{0}; // hora | tempo<<8 | variação<<16 | 1<<24 (gravou)
std::atomic<uint32_t> g_condAfter{0};
uintptr_t g_gameBase = 0;

using TuningClampFn = void (*)(void *setup);
TuningClampFn g_originalTuningClamp = nullptr;
// Valores do ini por índice (NaN = o do jogo), preparados no Initialize.
std::atomic<bool> g_tuneArmed{false};
char g_tuneCar[32] = {};
float g_tuneValues[kTuningCount] = {};
int g_tuneUnknown = 0; // nomes do ini que não existem
bool g_tuneOtherCar = false; // tuning_car diferente do vehicle: valores ignorados
// O que o detour viu, para LogTuning gravar fora da montagem.
struct TuningReport {
  char car[32];
  float def[kTuningCount], min[kTuningCount], max[kTuningCount];
  uint32_t steps[kTuningCount];
  int32_t state[kTuningCount];
  int applied, clamped, ignored;
};
TuningReport g_tuneReport;
std::atomic<bool> g_tunePending{false};

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

// Índice do nome na tabela do jogo; -1 se não existe. "morning" é o nome
// antigo do editor para sunrise.
template <size_t N>
int ConditionIndex(const char *const (&names)[N], std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (value == "morning") value = "sunrise";
  for (size_t i = 0; i < N; ++i) {
    if (value == names[i]) return static_cast<int>(i);
  }
  return -1;
}

// Mapeia tuning_<nome> do ini para os índices do jogo.
void ArmTuning(const AutoStageConfig &cfg) {
  std::fill(std::begin(g_tuneValues), std::end(g_tuneValues), std::numeric_limits<float>::quiet_NaN());
  std::memset(g_tuneCar, 0, sizeof(g_tuneCar));
  std::strncpy(g_tuneCar, cfg.vehicle.c_str(), sizeof(g_tuneCar) - 1);
  g_tuneUnknown = 0;
  g_tuneOtherCar = !cfg.tuning.empty() && !cfg.tuningCar.empty() && cfg.tuningCar != cfg.vehicle;
  if (!g_tuneOtherCar) {
    for (const auto &[name, value] : cfg.tuning) {
      const auto it = std::find_if(std::begin(kTuningNames), std::end(kTuningNames),
                                   [&](const char *n) { return name == n; });
      if (it == std::end(kTuningNames)) {
        ++g_tuneUnknown;
        continue;
      }
      g_tuneValues[it - std::begin(kTuningNames)] = value;
    }
  }
  g_tuneArmed.store(true);
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
    g_condTime.store(ConditionIndex(kTimeNames, cfg.timeOfDay));
    g_condWeather.store(ConditionIndex(kWeatherNames, cfg.surface));
    g_condArmed.store(true);
    ArmTuning(cfg);
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
        else if (keyLower == "time_of_day" || keyLower == "timeofday") cfg.timeOfDay = val;
        else if (keyLower == "surface" || keyLower == "weather") cfg.surface = val;
        else if (keyLower == "vehicle" || keyLower == "car") cfg.vehicle = val;
        else if (keyLower == "livery") cfg.livery = val;
        else if (keyLower == "cycle_camera") cfg.cycleCamera = ParseBool(val);
        else if (keyLower == "tuning_car") cfg.tuningCar = val;
        else if (keyLower.rfind("tuning_", 0) == 0) {
          char *end = nullptr;
          const float v = std::strtof(val.c_str(), &end);
          if (end != val.c_str() && std::isfinite(v)) cfg.tuning.emplace_back(keyLower.substr(7), v);
        }
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
  if (GetEnvironmentVariableA("DR2HOOK_STAGE_TIME", envVal, sizeof(envVal)) > 0) {
    cfg.timeOfDay = envVal;
  }
  if (GetEnvironmentVariableA("DR2HOOK_STAGE_WEATHER", envVal, sizeof(envVal)) > 0) {
    cfg.surface = envVal;
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

static void LogConditions();

static void DetourLoadingFill(void *screen) {
  LogConditions();
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

// O setter serve a seis montagens; só mexe na do benchmark (endereço de
// retorno). Não loga aqui (a montagem roda numa fiber de pilha curta): fica
// para LogConditions, na tela de carregamento.
static void DetourSetConditions(void *owner, const void *conditions) {
  g_originalSetConditions(owner, conditions);
  if (!g_condArmed.load(std::memory_order_relaxed) ||
      reinterpret_cast<uintptr_t>(__builtin_return_address(0)) != g_gameBase + kBenchmarkConditionsReturnRva)
    return;
  auto *cond = reinterpret_cast<int32_t *>(static_cast<uint8_t *>(owner) + kConditionsTime);
  const int t = g_condTime.load(std::memory_order_relaxed);
  const int w = g_condWeather.load(std::memory_order_relaxed);
  g_condBefore.store(static_cast<uint32_t>(cond[0] & 0xff) | static_cast<uint32_t>(cond[1] & 0xff) << 8 |
                         static_cast<uint32_t>(cond[2] & 0xff) << 16 | 1u << 24,
                     std::memory_order_relaxed);
  if (t >= 0) cond[0] = t;
  if (w >= 0) cond[1] = w;
  g_condAfter.store(static_cast<uint32_t>(cond[0] & 0xff) | static_cast<uint32_t>(cond[1] & 0xff) << 8 |
                        static_cast<uint32_t>(cond[2] & 0xff) << 16,
                    std::memory_order_relaxed);
}

// Relata uma vez o que o detour do setter trocou.
static void LogConditions() {
  const uint32_t before = g_condBefore.exchange(0);
  if (before == 0) return;
  const uint32_t after = g_condAfter.load();
  AutoStageConfig cfg = GetAutoStageConfig();
  auto triple = [](uint32_t v) {
    return std::to_string(v & 0xff) + "/" + std::to_string((v >> 8) & 0xff) + "/" + std::to_string((v >> 16) & 0xff);
  };
  Logger::Info("AutoStage: condicoes da especial " + triple(before) + " -> " + triple(after) + " (" +
               cfg.timeOfDay + ", " + cfg.surface + ")" +
               (g_condTime.load() < 0 || g_condWeather.load() < 0 ? "; nome desconhecido fica como o jogo deixou."
                                                                  : "."));
}

static float DecodeFloat(uint32_t bits, uint32_t key) {
  const uint32_t plain = bits * key;
  float f;
  std::memcpy(&f, &plain, sizeof(f));
  return f;
}

// Roda dentro da montagem (fiber de pilha curta), depois da tela de carregamento: só
// copia e grava floats; o log e o arquivo das faixas ficam para LogTuning (RaceEvent).
static void DetourTuningClamp(void *setup) {
  g_originalTuningClamp(setup);
  if (!g_tuneArmed.load(std::memory_order_relaxed) || setup == nullptr) return;
  auto *t = static_cast<uint8_t *>(setup);
  const char *name = reinterpret_cast<const char *>(t - kTuningInVehicle + kVehicleName);
  if (std::strncmp(name, g_tuneCar, sizeof(g_tuneCar)) != 0) return;
  const uint8_t *def = *reinterpret_cast<uint8_t *const *>(t);
  if (def == nullptr) return;
  auto *values = reinterpret_cast<float *>(t + kSetupValues);
  const bool report = !g_tunePending.load(std::memory_order_acquire);
  TuningReport &r = g_tuneReport;
  int applied = 0, clamped = 0, ignored = 0;
  for (int p = 0; p < kTuningCount; ++p) {
    const uint8_t *e = def + kDefRanges + p * 0x10;
    float dflt;
    std::memcpy(&dflt, e, sizeof(dflt));
    const float lo = DecodeFloat(*reinterpret_cast<const uint32_t *>(e + 4), kDecodeMin);
    const float hi = DecodeFloat(*reinterpret_cast<const uint32_t *>(e + 8), kDecodeMax);
    const int32_t state = *reinterpret_cast<const int32_t *>(def + kDefState + p * 4);
    if (report) {
      r.def[p] = dflt;
      r.min[p] = lo;
      r.max[p] = hi;
      r.steps[p] = *reinterpret_cast<const uint32_t *>(e + 12) * ModInverse(kEncodeSteps);
      r.state[p] = state;
    }
    const float v = g_tuneValues[p];
    if (std::isnan(v)) continue;
    if (state != 1) {
      ++ignored;
      continue;
    }
    const float c = std::min(std::max(v, lo), hi);
    if (c != v) ++clamped;
    values[p] = c;
    ++applied;
  }
  if (report) {
    std::memcpy(r.car, g_tuneCar, sizeof(r.car));
    r.applied = applied;
    r.clamped = clamped;
    r.ignored = ignored;
    g_tunePending.store(true, std::memory_order_release);
  }
}

// Grava <jogo>/dr2hook_tuning/<carro>.json (faixas do acerto, para o editor) e relata.
static void LogTuning() {
  if (!g_tunePending.load(std::memory_order_acquire)) return;
  const TuningReport &r = g_tuneReport;
  std::string json = "{\n \"version\": 1,\n \"car\": \"" + std::string(r.car) + "\",\n \"params\": [\n";
  char line[256];
  for (int p = 0; p < kTuningCount; ++p) {
    std::snprintf(line, sizeof(line),
                  "  {\"name\": \"%s\", \"state\": %d, \"default\": %.9g, \"min\": %.9g, \"max\": %.9g, "
                  "\"steps\": %u}%s\n",
                  kTuningNames[p], static_cast<int>(r.state[p]), r.def[p], r.min[p], r.max[p], r.steps[p],
                  p + 1 < kTuningCount ? "," : "");
    json += line;
  }
  json += " ]\n}\n";
  const auto dir = GetGameDirectory() / "dr2hook_tuning";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto path = dir / (std::string(r.car) + ".json");
  std::string old;
  if (std::ifstream in(path, std::ios::binary); in) old.assign(std::istreambuf_iterator<char>(in), {});
  if (old != json) std::ofstream(path, std::ios::binary | std::ios::trunc) << json;

  std::string what;
  if (g_tuneOtherCar) {
    what = "valores do ini sao de outro carro (tuning_car); fica o padrao do jogo";
  } else if (r.applied == 0 && r.ignored == 0 && g_tuneUnknown == 0) {
    what = "padrao do jogo";
  } else {
    what = std::to_string(r.applied) + " valor(es) do ini aplicado(s)";
    if (r.clamped) what += ", " + std::to_string(r.clamped) + " limitado(s) a faixa";
    if (r.ignored) what += ", " + std::to_string(r.ignored) + " que o carro nao ajusta";
    if (g_tuneUnknown) what += ", " + std::to_string(g_tuneUnknown) + " nome(s) desconhecido(s)";
  }
  Logger::Info("AutoStage: acerto do " + std::string(r.car) + ": " + what + "; faixas em dr2hook_tuning/" +
               r.car + ".json.");
  g_tunePending.store(false, std::memory_order_release);
}

static void InstallTuningHook(uintptr_t gameBase) {
  void *target = reinterpret_cast<void *>(gameBase + kTuningClampRva);
  if (std::memcmp(target, kTuningClampPrologue, sizeof(kTuningClampPrologue)) != 0) {
    Logger::Warn("AutoStage: limitador do acerto diferente do esperado; o carro fica com o acerto padrao.");
    return;
  }
  if (MH_CreateHook(target, reinterpret_cast<void *>(&DetourTuningClamp),
                    reinterpret_cast<void **>(&g_originalTuningClamp)) != MH_OK ||
      MH_EnableHook(target) != MH_OK) {
    Logger::Warn("AutoStage: falha no hook do acerto do carro.");
  }
}

static void InstallConditionsHook(uintptr_t gameBase) {
  void *target = reinterpret_cast<void *>(gameBase + kSetConditionsRva);
  if (std::memcmp(target, kSetConditionsPrologue, sizeof(kSetConditionsPrologue)) != 0) {
    Logger::Warn("AutoStage: setter das condicoes diferente do esperado; hora e tempo ficam os do jogo.");
    return;
  }
  if (MH_CreateHook(target, reinterpret_cast<void *>(&DetourSetConditions),
                    reinterpret_cast<void **>(&g_originalSetConditions)) != MH_OK ||
      MH_EnableHook(target) != MH_OK) {
    Logger::Warn("AutoStage: falha no hook das condicoes da especial.");
  }
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
  g_gameBase = gameBase;
  InstallConditionsHook(gameBase);
  InstallTuningHook(gameBase);
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

void LogAutoStageTuning() { LogTuning(); }

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
