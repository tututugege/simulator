#include "trap.h"
#include <stdint.h>
#include <stdio.h>

#define N 16

uint32_t a[N][N];
uint32_t b[N][N];
uint32_t c[N][N];

static inline uint32_t fadd(uint32_t a, uint32_t b) {
  uint32_t res;
  asm volatile(".insn r 0x53, 0, 0, %0, %1, %2" : "=r"(res) : "r"(a), "r"(b));
  return res;
}

static inline uint32_t fmul(uint32_t a, uint32_t b) {
  uint32_t res;
  asm volatile(".insn r 0x53, 0, 8, %0, %1, %2" : "=r"(res) : "r"(a), "r"(b));
  return res;
}

void matmul_zfinx() {
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      uint32_t sum = 0;
      for (int k = 0; k < N; k++) {
        sum = fadd(sum, fmul(a[i][k], b[k][j]));
      }
      c[i][j] = sum;
    }
  }
}

int main() {
  // Init matrices bits (1.0f and 2.0f)
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      a[i][j] = 0x3f800000;
      b[i][j] = 0x40000000;
    }
  }

  matmul_zfinx();

  // sum is 16 * (1.0 * 2.0) = 32.0f = 0x42000000
  check(c[0][0] == 0x42000000);

  return 0;
}
