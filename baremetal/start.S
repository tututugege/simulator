
    .section .init;
    .globl _start;
    .type _start,@function


_start:

  la gp, __global_pointer
  la sp, _sp
  csrr a0, mstatus
  li a0, 5
  csrw mtvec, a0
  csrr a0, mtvec

  call main
  ebreak

loop:
    j loop
