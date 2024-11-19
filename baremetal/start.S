
    .section .init;
    .globl _start;
    .type _start,@function


_start:

  la a0, 0x80001000
  csrw mtvec, a0
  csrr a0, mtvec
  la gp, __global_pointer
  la sp, _sp
  call main
  sw a0, 0x1c(zero)

loop:
    j loop
