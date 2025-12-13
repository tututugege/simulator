extern "C" {
#include "softfloat.h"
}

uint32_t f32_min_riscv(uint32_t a, uint32_t b);
uint32_t f32_max_riscv(uint32_t a, uint32_t b);
uint32_t f32_classify(float32_t f);
// ---------------- 辅助工具 ----------------
static inline float32_t to_f32(uint32_t v) {
  float32_t f;
  f.v = v;
  return f;
}

static inline uint32_t from_f32(float32_t f) { return f.v; }

static inline bool is_nan(uint32_t v) {
  // 指数全1 (0xFF)，尾数不为0
  return ((v & 0x7F800000) == 0x7F800000) && ((v & 0x007FFFFF) != 0);
}

static inline bool is_snan(uint32_t v) {
  // 是NaN，且尾数最高位(bit 22)为0
  return is_nan(v) && !((v >> 22) & 1);
}
