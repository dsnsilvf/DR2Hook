#include "dr2hook/vehicle_system_patch.h"
#include "dr2hook/logger.h"

#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>

namespace dr2hook {
namespace {

constexpr uintptr_t kImageBase = 0x140000000;
// `mov edx,0x16c0` antes da alocação (0x140390661, construtor da aplicação) e
// no delete do destrutor (0x1404694e9, `operator delete(p, 0x16c0)`).
constexpr uintptr_t kAllocSizeVa = 0x140390661;
constexpr uintptr_t kDeleteSizeVa = 0x1404694e9;
constexpr uint32_t kNewSize = 0x1900;

int g_result = 0; // 0 = não tentado, 1 = ok, -1 = bytes diferentes

bool PatchImm(uintptr_t base, uintptr_t va) {
  uint8_t *p = reinterpret_cast<uint8_t *>(base + (va - kImageBase));
  const uint8_t expected[] = {0xba, 0xc0, 0x16, 0x00, 0x00}; // mov edx,0x16c0
  if (std::memcmp(p, expected, sizeof(expected)) != 0) return false;
  DWORD old = 0;
  if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(p + 1, &kNewSize, 4);
  FlushInstructionCache(GetCurrentProcess(), p, 5);
  VirtualProtect(p, 5, old, &old);
  return true;
}

} // namespace

bool InstallVehicleSystemSizePatch() {
  const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  if (base == 0) return false;
  const bool alloc = PatchImm(base, kAllocSizeVa);
  if (alloc) {
    PatchImm(base, kDeleteSizeVa); // o delete com tamanho: só informativo
    // O core confere isto antes de usar o espaço extra.
    SetEnvironmentVariableA("DR2HOOK_VEHSYS_SIZE", "0x1900");
  }
  g_result = alloc ? 1 : -1;
  return alloc;
}

void LogVehicleSystemSizePatch() {
  if (g_result == 1) {
    Logger::Info("VehicleSystem: alocacao ampliada para 0x1900 bytes.");
  } else if (g_result == -1) {
    Logger::Warn("VehicleSystem: bytes da alocacao diferentes do esperado; sem ampliacao.");
  }
}

} // namespace dr2hook

#else

namespace dr2hook {
bool InstallVehicleSystemSizePatch() { return false; }
void LogVehicleSystemSizePatch() {}
} // namespace dr2hook

#endif
