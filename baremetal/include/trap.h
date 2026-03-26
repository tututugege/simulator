
#include "klib-macros.h"
#include <stdbool.h>
#include <stdint.h>
typedef unsigned int size_t;

inline void check(int cond) {
  volatile int *end_flag = (int *)0x1c;
  if (!cond) {
    *end_flag = 1;
  }
}

inline void halt(int ret) {
  volatile int *end_flag = (int *)0x1c;
  *end_flag = ret;
}
