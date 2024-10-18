
    .section .init;
    .globl _start;
    .type _start,@function


_start:

  la gp, __global_pointer
  la sp, _sp
  call main
  sw zero, 0x1c(zero)
loop:
    j loop
