#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dr2hook {

struct AutoStageConfig {
  bool enabled = false;
  bool once = false;
  std::string location = "usa";
  std::string track = "twin_peaks";
  std::string route = "free_roam";
  std::string timeOfDay = "midday";
  std::string surface = "dry";
  std::string vehicle = "fr5";
  std::string livery = "00";
  bool cycleCamera = false;
};

namespace benchmark_offsets {
constexpr size_t kOutputFileName = 0x00;   // char[64]
constexpr size_t kHardwareSettings = 0x40; // char[64]
constexpr size_t kBenchmarkEnabled = 0x80; // uint32_t
constexpr size_t kCycleCamera = 0xa1;      // uint8_t
constexpr size_t kCyclePeriod = 0xa4;      // uint32_t
constexpr size_t kLocation = 0xa8;         // char[32]
constexpr size_t kTrack = 0xc8;            // char[32]
constexpr size_t kRoute = 0xe8;            // char[32]
constexpr size_t kTimeOfDay = 0x108;       // char[32]
constexpr size_t kSurface = 0x128;         // char[32]
constexpr size_t kVehicle = 0x148;         // char[32]
constexpr size_t kLivery = 0x168;          // char[32]
constexpr size_t kActive = 0x198;          // uint32_t
constexpr size_t kStructSize = 0x200;
} // namespace benchmark_offsets

void ApplyAutoStage(void *benchmarkManager, const AutoStageConfig &config);

bool LoadAutoStageConfig(AutoStageConfig *outConfig = nullptr);
AutoStageConfig GetAutoStageConfig();
void SetAutoStageConfig(const AutoStageConfig &config);

// Chamar no DLL_PROCESS_ATTACH: o jogo executa BenchmarkManager::Initialize
// cedo no WinMain, antes da thread de init terminar. Nao loga nada.
bool InstallAutoStageHook();
// Chamar depois do Logger::Init: relata a instalacao e o que o detour viu.
void LogAutoStageStatus();
void UninstallAutoStageHook();

} // namespace dr2hook
