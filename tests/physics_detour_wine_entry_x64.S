.intel_syntax noprefix
.text
.globl PhysicsHarness_DetourWineTest

.extern g_wine_detour_ops
.extern PhysicsHarness_DetourCommon

PhysicsHarness_DetourWineTest:
  lea r11, [g_wine_detour_ops + rip]
  jmp PhysicsHarness_DetourCommon
