#include "trap.h"
#include <stdint.h>

#define N (16384u)
#define MASK (N - 1u)
#define ROUNDS (16u)

static uint32_t a[N];
static uint32_t b[N];
static uint32_t c[N];

int main() {
  for (uint32_t i = 0; i < N; i++) {
    a[i] = (i * 2654435761u) ^ 0x12345678u;
    b[i] = (i * 97u) ^ 0x9e3779b9u;
    c[i] = 0;
  }

  uint64_t sum = 0;
  for (uint32_t round = 0; round < ROUNDS; round++) {
    for (uint32_t i = 0; i < N; i++) {
      c[i] = a[i] + b[(i * 17u) & MASK];
    }

    for (uint32_t i = 0; i < N; i++) {
      uint32_t idx = (i * 131u + round * 7u) & MASK;
      a[idx] = c[i] ^ a[(idx >> 1) & MASK];
    }

    for (uint32_t i = 0; i < N; i += 4u) {
      sum += (uint64_t)a[i] + (uint64_t)c[(i * 29u) & MASK];
    }
  }

  check(sum == 0xfffbb5d1f0bbULL);
  check(a[0] == 0x00a92302u);
  check(a[1] == 0x6350a39du);
  check(a[1234] == 0xcd2ab117u);
  check(c[5678] == 0x6901bef1u);
  return 0;
}
