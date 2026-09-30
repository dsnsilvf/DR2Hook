.intel_syntax noprefix
.text
.globl PhysicsHarness_DetourCommon

// r12 = DetourOps* (callee-saved; survives C calls that clobber r11)
// r13 = caller return address at thunk entry ([rsp] before prologue push)
PhysicsHarness_DetourCommon:
.seh_proc PhysicsHarness_DetourCommon
  push rbp
  .seh_pushreg rbp
  mov rbp, rsp
  sub rsp, 0xC0
  .seh_stackalloc 0xC0
  .seh_endprologue

  mov QWORD PTR [rsp + 0x08], rcx
  mov QWORD PTR [rsp + 0x10], rdx
  mov QWORD PTR [rsp + 0x18], r8
  mov QWORD PTR [rsp + 0x20], r9
  movaps XMMWORD PTR [rsp + 0x30], xmm0
  movaps XMMWORD PTR [rsp + 0x40], xmm1
  movaps XMMWORD PTR [rsp + 0x50], xmm2
  movaps XMMWORD PTR [rsp + 0x60], xmm3
  mov QWORD PTR [rsp + 0x68], r13

  mov rax, QWORD PTR [r12]
  test rax, rax
  je skip_before
  lea rcx, [rsp + 0x08]
  call rax

skip_before:
  mov rcx, QWORD PTR [rsp + 0x08]
  mov rdx, QWORD PTR [rsp + 0x10]
  mov r8, QWORD PTR [rsp + 0x18]
  mov r9, QWORD PTR [rsp + 0x20]
  movaps xmm0, XMMWORD PTR [rsp + 0x30]
  movaps xmm1, XMMWORD PTR [rsp + 0x40]
  movaps xmm2, XMMWORD PTR [rsp + 0x50]
  movaps xmm3, XMMWORD PTR [rsp + 0x60]

  mov rax, QWORD PTR [r12 + 16]
  mov rax, QWORD PTR [rax]
  test rax, rax
  je skip_orig
  call rax

skip_orig:
  mov QWORD PTR [rsp + 0x70], rax
  movaps XMMWORD PTR [rsp + 0x78], xmm0

  mov rax, QWORD PTR [r12 + 8]
  test rax, rax
  je skip_after
  lea rcx, [rsp + 0x08]
  call rax

skip_after:
  mov rax, QWORD PTR [rsp + 0x70]
  movaps xmm0, XMMWORD PTR [rsp + 0x78]

  mov rsp, rbp
  pop rbp
  ret
.seh_endproc

.macro detour_entry name, ops
.globl \name
\name:
  lea r12, [\ops + rip]
  mov r13, QWORD PTR [rsp]
  jmp PhysicsHarness_DetourCommon
.endm

.extern g_ops_tick_start
.extern g_ops_integrator
.extern g_ops_commit
.extern g_ops_frame_loop
.extern g_ops_physics_step
.extern g_ops_pretick
.extern g_ops_end_step
.extern g_ops_commit_aux_a
.extern g_ops_commit_aux_b

detour_entry PhysicsHarness_DetourTickStart, g_ops_tick_start
detour_entry PhysicsHarness_DetourIntegrator, g_ops_integrator
detour_entry PhysicsHarness_DetourCommit, g_ops_commit
detour_entry PhysicsHarness_DetourFrameLoop, g_ops_frame_loop
detour_entry PhysicsHarness_DetourPhysicsStep, g_ops_physics_step
detour_entry PhysicsHarness_DetourPreTick, g_ops_pretick
detour_entry PhysicsHarness_DetourEndStep, g_ops_end_step
detour_entry PhysicsHarness_DetourCommitAuxA, g_ops_commit_aux_a
detour_entry PhysicsHarness_DetourCommitAuxB, g_ops_commit_aux_b
