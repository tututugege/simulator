
    .section .init;
    .globl _start;
    .type _start,@function


_start:

  la gp, __global_pointer
  la sp, _sp

  call main
  ebreak

loop:
    j loop
