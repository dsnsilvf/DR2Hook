#pragma once

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
  uint64_t rax;
  alignas(16) uint8_t xmm0_ret[16];
};

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
