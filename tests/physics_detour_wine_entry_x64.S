.intel_syntax noprefix
.text
.globl PhysicsHarness_DetourWineTest

.extern g_wine_detour_ops
.extern PhysicsHarness_DetourCommon

PhysicsHarness_DetourWineTest:
  push r12
  push r13
  mov r10, QWORD PTR [rsp + 16]
  lea rax, [g_wine_detour_ops + rip]
  jmp PhysicsHarness_DetourCommon
