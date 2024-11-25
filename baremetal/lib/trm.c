#include "../include/am.h"
#include "../include/xprintf.h"

void halt(int code) {

  asm volatile("ebreak");

  while (1)
    ;
}

void assert(int code) {
  if (code == 0)
    halt(1);
}

void panic(char *info) {
  xprintf(info);
  halt(1);
}
