.intel_syntax noprefix
.text
.globl WineDetourRunHookedCall

// int WineDetourRunHookedCall(void *targetFn, void *obsStruct)
WineDetourRunHookedCall:
  push rbp
  mov rbp, rsp
  and rsp, 0xfffffffffffffff0
  sub rsp, 0x80

  mov QWORD PTR [rsp + 0x20], rcx
  mov QWORD PTR [rsp + 0x28], rdx

  mov rbx, 0xB000000000000001
  mov rsi, 0xB000000000000002
  mov rdi, 0xB000000000000003
  mov r12, 0xB00000000000000C
  mov r13, 0xB00000000000000D
  mov r14, 0xB00000000000000E
  mov r15, 0xB00000000000000F

  mov rax, 0x3FF8000000000000
  movq xmm6, rax
  mov rax, 0x3FF9000000000000
  movq xmm7, rax
  mov rax, 0x3FFA0000000000
  movq xmm8, rax
  mov rax, 0x3FFB0000000000
  movq xmm9, rax
  mov rax, 0x3FFC0000000000
  movq xmm10, rax
  mov rax, 0x3FFD0000000000
  movq xmm11, rax
  mov rax, 0x3FFE0000000000
  movq xmm12, rax
  mov rax, 0x3FFF0000000000
  movq xmm13, rax
  mov rax, 0x4000000000000000
  movq xmm14, rax
  mov rax, 0x4001000000000000
  movq xmm15, rax

  mov rcx, QWORD PTR [rsp + 0x28]
  mov rdx, 0xA000000000000002
  mov r8, 0xA000000000000003
  mov r9, 0xA000000000000004
  mov rax, 0x3FE0000000000000
  movq xmm0, rax
  mov rax, 0x3FE8000000000000
  movq xmm1, rax
  mov rax, 0x3FF0000000000000
  movq xmm2, rax
  mov rax, 0x3FF4000000000000
  movq xmm3, rax

  mov rax, QWORD PTR [rsp + 0x20]
  call rax

  mov r10, rax
  movq rax, xmm0
  mov QWORD PTR [rsp + 0x30], rax

  mov rax, 0xB000000000000001
  cmp rbx, rax
  jne fail
  mov rax, 0xB000000000000002
  cmp rsi, rax
  jne fail
  mov rax, 0xB000000000000003
  cmp rdi, rax
  jne fail
  mov rax, 0xB00000000000000C
  cmp r12, rax
  jne fail
  mov rax, 0xB00000000000000D
  cmp r13, rax
  jne fail
  mov rax, 0xB00000000000000E
  cmp r14, rax
  jne fail
  mov rax, 0xB00000000000000F
  cmp r15, rax
  jne fail

  movq rax, xmm6
  mov rcx, 0x3FF8000000000000
  cmp rax, rcx
  jne fail
  movq rax, xmm15
  mov rcx, 0x4001000000000000
  cmp rax, rcx
  jne fail

  mov rax, 0x1234567890ABCDEF
  cmp r10, rax
  jne fail
  movq rax, QWORD PTR [rsp + 0x30]
  mov rcx, 0x3FF0000000000000
  cmp rax, rcx
  jne fail

  xor eax, eax
  jmp done

fail:
  mov eax, 1

done:
  mov rsp, rbp
  pop rbp
  ret
