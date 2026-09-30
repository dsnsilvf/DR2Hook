#pragma once

#include <cstddef>
#include <cstdint>

// Win64: preserva RCX/RDX/R8/R9 e XMM0–XMM3 em torno do logging (BUG 2 — dt em XMM1).
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)) &&                           \
    !defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)

namespace dr2hook::physics_harness_abi {

struct alignas(16) Frame {
  uint64_t rcx;
  uint64_t rdx;
  uint64_t r8;
  uint64_t r9;
  alignas(16) uint8_t xmm0[16];
  alignas(16) uint8_t xmm1[16];
  alignas(16) uint8_t xmm2[16];
  alignas(16) uint8_t xmm3[16];
  // Endereço de retorno do caller do jogo ([rsp] na entrada do thunk), não do stub.
  uint64_t caller_return;
  uint64_t rax;
  alignas(16) uint8_t xmm0_ret[16];
};

static_assert(offsetof(Frame, caller_return) == 0x60,
              "physics_harness_detour_x64.S must store caller return at [frame+0x60]");
static_assert(offsetof(Frame, rax) == 0x68,
              "physics_harness_detour_x64.S must store RAX at [frame+0x68]");
static_assert(offsetof(Frame, xmm0_ret) == 0x70,
              "physics_harness_detour_x64.S must store XMM0 ret at [frame+0x70]");

struct DetourOps {
  void (*before)(Frame *frame);
  void (*after)(Frame *frame);
  void **orig_trampoline;
};

extern DetourOps g_ops_tick_start;
extern DetourOps g_ops_integrator;
extern DetourOps g_ops_commit;
extern DetourOps g_ops_frame_loop;
extern DetourOps g_ops_physics_step;
extern DetourOps g_ops_pretick;
extern DetourOps g_ops_end_step;
extern DetourOps g_ops_commit_aux_a;
extern DetourOps g_ops_commit_aux_b;

extern "C" void PhysicsHarness_DetourTickStart();
extern "C" void PhysicsHarness_DetourIntegrator();
extern "C" void PhysicsHarness_DetourCommit();
extern "C" void PhysicsHarness_DetourFrameLoop();
extern "C" void PhysicsHarness_DetourPhysicsStep();
extern "C" void PhysicsHarness_DetourPreTick();
extern "C" void PhysicsHarness_DetourEndStep();
extern "C" void PhysicsHarness_DetourCommitAuxA();
extern "C" void PhysicsHarness_DetourCommitAuxB();

} // namespace dr2hook::physics_harness_abi

#endif
