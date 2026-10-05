#include "dr2hook/auto_stage.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

void TestAutoStageMemoryLayout() {
  std::vector<uint8_t> buffer(dr2hook::benchmark_offsets::kStructSize, 0xCC);

  dr2hook::AutoStageConfig cfg;
  cfg.enabled = true;
  cfg.location = "usa";
  cfg.track = "twin_peaks";
  cfg.route = "free_roam";
  cfg.timeOfDay = "midday";
  cfg.surface = "dry";
  cfg.vehicle = "fr5";
  cfg.livery = "00";
  cfg.cycleCamera = false;

  dr2hook::ApplyAutoStage(buffer.data(), cfg);

  // 1. BenchmarkEnabled flag (+0x80)
  uint32_t benchmarkEnabled = *reinterpret_cast<const uint32_t *>(
      buffer.data() + dr2hook::benchmark_offsets::kBenchmarkEnabled);
  assert(benchmarkEnabled == 1);

  // 2. CycleCamera flag (+0xa1)
  uint8_t cycleCamera =
      *(buffer.data() + dr2hook::benchmark_offsets::kCycleCamera);
  assert(cycleCamera == 0);

  // 3. String fields
  const char *loc = reinterpret_cast<const char *>(
      buffer.data() + dr2hook::benchmark_offsets::kLocation);
  assert(std::strcmp(loc, "usa") == 0);

  const char *trk = reinterpret_cast<const char *>(
      buffer.data() + dr2hook::benchmark_offsets::kTrack);
  assert(std::strcmp(trk, "twin_peaks") == 0);

  const char *rte = reinterpret_cast<const char *>(
      buffer.data() + dr2hook::benchmark_offsets::kRoute);
  assert(std::strcmp(rte, "free_roam") == 0);

  const char *tod = reinterpret_cast<const char *>(
      buffer.data() + dr2hook::benchmark_offsets::kTimeOfDay);
  assert(std::strcmp(tod, "midday") == 0);

  const char *sfc = reinterpret_cast<const char *>(
      buffer.data() + dr2hook::benchmark_offsets::kSurface);
  assert(std::strcmp(sfc, "dry") == 0);

  const char *veh = reinterpret_cast<const char *>(
      buffer.data() + dr2hook::benchmark_offsets::kVehicle);
  assert(std::strcmp(veh, "fr5") == 0);

  const char *liv = reinterpret_cast<const char *>(
      buffer.data() + dr2hook::benchmark_offsets::kLivery);
  assert(std::strcmp(liv, "00") == 0);

  // 4. Active flag (+0x198)
  uint32_t active = *reinterpret_cast<const uint32_t *>(
      buffer.data() + dr2hook::benchmark_offsets::kActive);
  assert(active == 1);

  std::cout << "[PASS] TestAutoStageMemoryLayout (DirtFish twin_peaks)"
            << std::endl;
}

void TestAutoStageCycleCameraOption() {
  std::vector<uint8_t> buffer(dr2hook::benchmark_offsets::kStructSize, 0x00);

  dr2hook::AutoStageConfig cfg;
  cfg.enabled = true;
  cfg.location = "new_zealand";
  cfg.track = "new_zealand_rally_01";
  cfg.route = "route_2";
  cfg.cycleCamera = true;

  dr2hook::ApplyAutoStage(buffer.data(), cfg);

  uint8_t cycleCamera =
      *(buffer.data() + dr2hook::benchmark_offsets::kCycleCamera);
  assert(cycleCamera == 1);

  const char *loc = reinterpret_cast<const char *>(
      buffer.data() + dr2hook::benchmark_offsets::kLocation);
  assert(std::strcmp(loc, "new_zealand") == 0);

  std::cout << "[PASS] TestAutoStageCycleCameraOption (New Zealand route_2)"
            << std::endl;
}

void TestAutoStageConfigIniRoundtrip() {
  const std::string iniContent =
      "[autostage]\n"
      "enabled = 1\n"
      "once = 0\n"
      "location = spain\n"
      "track = spain_rally_01\n"
      "route = route_1\n"
      "time_of_day = sunset\n"
      "surface = wet\n"
      "vehicle = subaru_impreza\n"
      "livery = 01\n"
      "cycle_camera = 0\n";

  {
    std::ofstream out("dr2hook_autostage.ini", std::ios::trunc);
    out << iniContent;
  }

  dr2hook::AutoStageConfig cfg;
  bool enabled = dr2hook::LoadAutoStageConfig(&cfg);
  assert(enabled == true);
  assert(cfg.enabled == true);
  assert(cfg.once == false);
  assert(cfg.location == "spain");
  assert(cfg.track == "spain_rally_01");
  assert(cfg.route == "route_1");
  assert(cfg.timeOfDay == "sunset");
  assert(cfg.surface == "wet");
  assert(cfg.vehicle == "subaru_impreza");
  assert(cfg.livery == "01");
  assert(cfg.cycleCamera == false);

  std::remove("dr2hook_autostage.ini");

  std::cout << "[PASS] TestAutoStageConfigIniRoundtrip (Spain sunset/wet/subaru)"
            << std::endl;
}

int main() {
  std::cout << "Iniciando suite de testes do AutoStage..." << std::endl;
  TestAutoStageMemoryLayout();
  TestAutoStageCycleCameraOption();
  TestAutoStageConfigIniRoundtrip();
  std::cout << "Todos os 3 testes do AutoStage passaram com 100% de sucesso!"
            << std::endl;
  return 0;
}
