.intel_syntax noprefix
.text
.globl WineDummyTarget

// Prólogo idêntico a tick_start (12 B) — MinHook relocates mov rax,rsp como no jogo.
WineDummyTarget:
  mov rax, rsp
  mov QWORD PTR [rax + 0x18], rbx
  mov QWORD PTR [rax + 0x20], rsi
  push rbp
WineDummyTarget_body:
  mov rbp, rsp
  sub rsp, 0x40

  mov QWORD PTR [rcx + 0x00], rbx
  mov QWORD PTR [rcx + 0x08], rbp
  mov QWORD PTR [rcx + 0x10], rsi
  mov QWORD PTR [rcx + 0x18], rdi
  mov QWORD PTR [rcx + 0x20], r12
  mov QWORD PTR [rcx + 0x28], r13
  mov QWORD PTR [rcx + 0x30], r14
  mov QWORD PTR [rcx + 0x38], r15

  movaps XMMWORD PTR [rcx + 0x40], xmm6
  movaps XMMWORD PTR [rcx + 0x50], xmm7
  movaps XMMWORD PTR [rcx + 0x60], xmm8
  movaps XMMWORD PTR [rcx + 0x70], xmm9
  movaps XMMWORD PTR [rcx + 0x80], xmm10
  movaps XMMWORD PTR [rcx + 0x90], xmm11
  movaps XMMWORD PTR [rcx + 0xA0], xmm12
  movaps XMMWORD PTR [rcx + 0xB0], xmm13
  movaps XMMWORD PTR [rcx + 0xC0], xmm14
  movaps XMMWORD PTR [rcx + 0xD0], xmm15

  mov QWORD PTR [rcx + 0xE0], rcx
  mov QWORD PTR [rcx + 0xE8], rdx
  mov QWORD PTR [rcx + 0xF0], r8
  mov QWORD PTR [rcx + 0xF8], r9
  movaps XMMWORD PTR [rcx + 0x100], xmm0
  movaps XMMWORD PTR [rcx + 0x110], xmm1
  movaps XMMWORD PTR [rcx + 0x120], xmm2
  movaps XMMWORD PTR [rcx + 0x130], xmm3

  mov rax, 0x1234567890ABCDEF
  movsd xmm0, QWORD PTR [rip + ret_xmm0]
  add rsp, 0x40
  pop rbp
  ret

ret_xmm0:
  .quad 0x3FF0000000000000
