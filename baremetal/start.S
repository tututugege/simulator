
    .section .init;
    .globl _start;
    .type _start,@function


_start:

  la a0, 0x80001000
  la gp, __global_pointer
  la sp, _sp

  la a1, 0x80000000
  lw a2, 0(a1)
  call main
  sw a0, 0x1c(zero)

loop:
    j loop
