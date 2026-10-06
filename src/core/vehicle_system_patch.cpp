#include "dr2hook/vehicle_system_patch.h"
#include "dr2hook/logger.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>

namespace dr2hook {
namespace {

constexpr uintptr_t kImageBase = 0x140000000;

// `mov edx,<tamanho>` antes da alocação de objetos com arrays por carro de 16
// posições; o core põe arrays de 24 no espaço extra (ghost_lab.cpp).
struct SizePatch {
  uintptr_t va;
  uint32_t from;
  uint32_t to;
  const char *name;
  const char *env; // avisa o core que o objeto nasce ampliado
};
constexpr SizePatch kPatches[] = {
    // VEHICLE_SYSTEM (construtor da aplicação) e o delete do destrutor.
    {0x140390661, 0x16c0, 0x1900, "VEHICLE_SYSTEM", "DR2HOOK_VEHSYS_SIZE"},
    {0x1404694e9, 0x16c0, 0x1900, "VEHICLE_SYSTEM (delete)", nullptr},
    // Pilotos/animações internas (0x140b46bc0): + 24 parâmetros de 0x68.
    {0x140b93e82, 0xb180, 0xbb40, "pilotos", "DR2HOOK_DRIVERSYS_SIZE"},
    // Gerenciador de render (0x1409451a0): lista de 16 flags por carro (+0x194a8) vai
    // para +0x1ab30 com 24.
    {0x140b947aa, 0x1ab30, 0x1ab90, "render", "DR2HOOK_RENDERMGR_SIZE"},
    // Dono dos slots de 0x6228 (construtor 0x14091ecc0, array em +0x408). 16 cabem
    // até +0x62688; 24 pedem +8*0x6228. O core move a cauda e constrói 24. A lista de
    // ponteiros da cauda (+0x62360, 13 vagas antes de +0x623c8) também passa a 24, então
    // o que vem depois dela anda 0x31198 em vez de 0x31140.
    {0x140526ebb, 0x62750, 0x938e8, "slots 0x6228", "DR2HOOK_SLOTOWNER_SIZE"},
    {0x1409218dd, 0x62750, 0x938e8, "slots 0x6228 (delete)", nullptr},
    {0x140921879, 0x62418, 0x935b0, "slots 0x6228 (subobjeto)", nullptr},
};
constexpr size_t kPatchCount = sizeof(kPatches) / sizeof(kPatches[0]);
int g_result[kPatchCount] = {}; // 0 = não tentado, 1 = ok, -1 = bytes diferentes

bool PatchImm(uintptr_t base, const SizePatch &p) {
  uint8_t *at = reinterpret_cast<uint8_t *>(base + (p.va - kImageBase));
  uint8_t expected[5] = {0xba}; // mov edx,imm32
  std::memcpy(expected + 1, &p.from, 4);
  if (std::memcmp(at, expected, sizeof(expected)) != 0) return false;
  DWORD old = 0;
  if (!VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(at + 1, &p.to, 4);
  FlushInstructionCache(GetCurrentProcess(), at, 5);
  VirtualProtect(at, 5, old, &old);
  return true;
}

} // namespace

bool InstallVehicleSystemSizePatch() {
  const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  if (base == 0) return false;
  bool any = false;
  for (size_t i = 0; i < kPatchCount; ++i) {
    const bool ok = PatchImm(base, kPatches[i]);
    g_result[i] = ok ? 1 : -1;
    if (ok && kPatches[i].env != nullptr) {
      char size[16];
      std::snprintf(size, sizeof(size), "%x", kPatches[i].to); // o core confere o tamanho
      SetEnvironmentVariableA(kPatches[i].env, size);
    }
    any = any || ok;
  }
  return any;
}

void LogVehicleSystemSizePatch() {
  for (size_t i = 0; i < kPatchCount; ++i) {
    char msg[160];
    if (g_result[i] == 1) {
      std::snprintf(msg, sizeof(msg), "VehicleSystem: alocacao de %s ampliada de 0x%x para 0x%x.",
                    kPatches[i].name, kPatches[i].from, kPatches[i].to);
      Logger::Info(msg);
    } else if (g_result[i] == -1) {
      std::snprintf(msg, sizeof(msg),
                    "VehicleSystem: bytes da alocacao de %s diferentes do esperado; sem ampliacao.",
                    kPatches[i].name);
      Logger::Warn(msg);
    }
  }
}

} // namespace dr2hook

#else

namespace dr2hook {
bool InstallVehicleSystemSizePatch() { return false; }
void LogVehicleSystemSizePatch() {}
} // namespace dr2hook

#endif
