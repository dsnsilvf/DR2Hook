#pragma once

#include <cstddef>
#include <cstdint>

// Win64: preserva RCX/RDX/R8/R9 e XMM0–XMM3 em torno do logging (BUG 2 — dt em XMM1).
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)) &&                           \
    !defined(DR2HOOK_PHYSICS_HARNESS_NO_HOOKS)

namespace dr2hook::physics_harness_abi {

// Stack layout in PhysicsHarness_DetourCommon (matches physics_harness_detour_x64.S):
// [rsp+0x00 .. rsp+0x1F]  Win64 home/shadow for C calls
// [rsp+0x20 .. rsp+0x9F]  Frame (16-byte aligned)
// [rsp+0xA8]              DetourOps* (above Frame; mov rax,rsp prologues clobber +0x18/+0x20)
inline constexpr size_t kDetourShadowSpaceBytes = 0x20;
inline constexpr size_t kDetourOpsStackSlotOffset = 0xA8;
inline constexpr size_t kDetourFrameRspOffset = kDetourShadowSpaceBytes;
inline constexpr size_t kDetourStackAllocBytes = 0xC0;

struct alignas(16) Frame {
  uint64_t rcx;
  uint64_t rdx;
  uint64_t r8;
  uint64_t r9;
  alignas(16) uint8_t xmm0[16];
  alignas(16) uint8_t xmm1[16];
  alignas(16) uint8_t xmm2[16];
  alignas(16) uint8_t xmm3[16];
  uint64_t caller_return;
  uint64_t rax;
  alignas(16) uint8_t xmm0_ret[16];
};

static_assert(kDetourOpsStackSlotOffset >= sizeof(Frame) + kDetourFrameRspOffset,
              "DetourOps slot must sit above Frame on stack");
static_assert(alignof(Frame) == 16, "Frame must be 16-byte aligned");
static_assert(sizeof(Frame) == 0x80, "Frame size must match detour stack frame");

static_assert(offsetof(Frame, rcx) == 0x00, "Frame::rcx");
static_assert(offsetof(Frame, rdx) == 0x08, "Frame::rdx");
static_assert(offsetof(Frame, r8) == 0x10, "Frame::r8");
static_assert(offsetof(Frame, r9) == 0x18, "Frame::r9");
static_assert(offsetof(Frame, xmm0) == 0x20, "Frame::xmm0 vs [rsp+0x40]");
static_assert(offsetof(Frame, xmm1) == 0x30, "Frame::xmm1");
static_assert(offsetof(Frame, xmm2) == 0x40, "Frame::xmm2");
static_assert(offsetof(Frame, xmm3) == 0x50, "Frame::xmm3");
static_assert(offsetof(Frame, caller_return) == 0x60,
              "Frame::caller_return vs [rsp+0x80]");
static_assert(offsetof(Frame, rax) == 0x68, "Frame::rax vs [rsp+0x88]");
static_assert(offsetof(Frame, xmm0_ret) == 0x70,
              "Frame::xmm0_ret vs [rsp+0x90]");

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
