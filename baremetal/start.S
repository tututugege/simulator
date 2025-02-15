
    .section .init;
    .globl _start;
    .type _start,@function


_start:

  la gp, __global_pointer
  la sp, _sp

  la a0, 0x80001000
  sw a0, 0(a0)
  lw a0, 0(a0)
  call main
  sw a0, 0x1c(zero)

loop:
    j loop
