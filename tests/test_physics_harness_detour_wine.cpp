#include "dr2hook/physics_harness_detour_abi.h"

#include <MinHook.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" void WineDummyTarget();
extern "C" int WineDetourRunHookedCall(void *targetFn, void *obsStruct);
extern "C" void PhysicsHarness_DetourWineTest();

namespace {

struct WineObs {
  std::uint64_t rbx;
  std::uint64_t rbp;
  std::uint64_t rsi;
  std::uint64_t rdi;
  std::uint64_t r12;
  std::uint64_t r13;
  std::uint64_t r14;
  std::uint64_t r15;
  alignas(16) std::uint8_t xmm6[16];
  alignas(16) std::uint8_t xmm7[16];
  alignas(16) std::uint8_t xmm8[16];
  alignas(16) std::uint8_t xmm9[16];
  alignas(16) std::uint8_t xmm10[16];
  alignas(16) std::uint8_t xmm11[16];
  alignas(16) std::uint8_t xmm12[16];
  alignas(16) std::uint8_t xmm13[16];
  alignas(16) std::uint8_t xmm14[16];
  alignas(16) std::uint8_t xmm15[16];
  std::uint64_t rcx;
  std::uint64_t rdx;
  std::uint64_t r8;
  std::uint64_t r9;
  alignas(16) std::uint8_t xmm0[16];
  alignas(16) std::uint8_t xmm1[16];
  alignas(16) std::uint8_t xmm2[16];
  alignas(16) std::uint8_t xmm3[16];
};

} // namespace

extern "C" {

dr2hook::physics_harness_abi::DetourOps g_wine_detour_ops = {nullptr, nullptr,
                                                             nullptr};

}

static bool ObsArgsLookSane(const WineObs &obs) {
  return obs.rcx != 0 && obs.rdx == 0xA000000000000002ULL &&
         obs.r8 == 0xA000000000000003ULL && obs.r9 == 0xA000000000000004ULL;
}

int main() {
  if (MH_Initialize() != MH_OK) {
    std::fprintf(stderr, "MH_Initialize failed\n");
    return 10;
  }

  void *orig = nullptr;
  g_wine_detour_ops.orig_trampoline = &orig;

  if (MH_CreateHook(reinterpret_cast<void *>(WineDummyTarget),
                    reinterpret_cast<void *>(PhysicsHarness_DetourWineTest),
                    &orig) != MH_OK) {
    std::fprintf(stderr, "MH_CreateHook failed\n");
    return 11;
  }
  if (orig == nullptr) {
    std::fprintf(stderr, "trampoline null\n");
    return 12;
  }
  g_wine_detour_ops.orig_trampoline = &orig;

  if (MH_EnableHook(reinterpret_cast<void *>(WineDummyTarget)) != MH_OK) {
    std::fprintf(stderr, "MH_EnableHook failed\n");
    return 13;
  }

  WineObs obs{};
  std::memset(&obs, 0xCC, sizeof(obs));

  if (WineDetourRunHookedCall(reinterpret_cast<void *>(WineDummyTarget), &obs) !=
      0) {
    std::fprintf(stderr, "register preservation check failed\n");
    return 20;
  }

  if (!ObsArgsLookSane(obs)) {
    std::fprintf(stderr, "argument snapshot inside dummy failed\n");
    return 21;
  }

  if (obs.rbx != 0xB000000000000001ULL || obs.r12 != 0xB00000000000000CULL) {
    std::fprintf(stderr, "dummy body saw clobbered nonvolatiles\n");
    return 22;
  }

  MH_DisableHook(reinterpret_cast<void *>(WineDummyTarget));
  MH_RemoveHook(reinterpret_cast<void *>(WineDummyTarget));
  MH_Uninitialize();

  std::printf("PASS physics harness detour wine ABI test\n");
  return 0;
}
