#include "softfloat_extra.h"

// RISC-V 规范的 FMIN 实现
uint32_t f32_min_riscv(uint32_t a, uint32_t b) {
  // 1. 处理 sNaN 异常 (Invalid Operation)
  if (is_snan(a) || is_snan(b)) {
    softfloat_exceptionFlags |= softfloat_flag_invalid;
  }

  bool a_nan = is_nan(a);
  bool b_nan = is_nan(b);

  // 2. NaN 处理规则
  if (a_nan && b_nan)
    return 0x7fc00000; // Canonical NaN
  if (a_nan)
    return b; // 如果 A 是 NaN，返回 B (即使 B 也是 NaN 的情况上面已处理)
  if (b_nan)
    return a; // 如果 B 是 NaN，返回 A

  // 3. 数值比较
  float32_t fa = to_f32(a);
  float32_t fb = to_f32(b);

  // 注意：f32_lt 对 -0.0 和 +0.0 返回 false (视为相等)
  if (f32_lt(fa, fb))
    return a;
  if (f32_lt(fb, fa))
    return b;

  // 4. 处理相等的情况 (主要是 -0.0 和 +0.0)
  // FMIN(-0.0, +0.0) -> -0.0
  // 原理：(10..0) | (00..0) = (10..0)
  return a | b;
}

// RISC-V 规范的 FMAX 实现
uint32_t f32_max_riscv(uint32_t a, uint32_t b) {
  // 1. 处理 sNaN 异常
  if (is_snan(a) || is_snan(b)) {
    softfloat_exceptionFlags |= softfloat_flag_invalid;
  }

  bool a_nan = is_nan(a);
  bool b_nan = is_nan(b);

  // 2. NaN 处理规则
  if (a_nan && b_nan)
    return 0x7fc00000;
  if (a_nan)
    return b;
  if (b_nan)
    return a;

  // 3. 数值比较
  float32_t fa = to_f32(a);
  float32_t fb = to_f32(b);

  if (f32_lt(fa, fb))
    return b; // A < B, 所以 Max 是 B
  if (f32_lt(fb, fa))
    return a;

  // 4. 处理相等的情况
  // FMAX(-0.0, +0.0) -> +0.0
  // 原理：(10..0) & (00..0) = (00..0)
  return a & b;
}

uint32_t f32_classify(float32_t f) {
  uint32_t bits = from_f32(f);
  uint32_t sign = (bits >> 31) & 1;
  uint32_t exp = (bits >> 23) & 0xFF;
  uint32_t mant = bits & 0x7FFFFF;

  bool is_subnormal = (exp == 0) && (mant != 0);
  bool is_zero = (exp == 0) && (mant == 0);
  bool is_inf = (exp == 0xFF) && (mant == 0);
  bool is_nan = (exp == 0xFF) && (mant != 0);
  bool is_snan = is_nan && !((mant >> 22) & 1); // MSB of mantissa is 0
  bool is_qnan = is_nan && ((mant >> 22) & 1);  // MSB of mantissa is 1

  uint32_t res = 0;

  if (is_inf && sign)
    res |= (1 << 0); // -inf
  else if (!is_inf && !is_zero && !is_nan && !is_subnormal && sign)
    res |= (1 << 1); // -normal
  else if (is_subnormal && sign)
    res |= (1 << 2); // -subnormal
  else if (is_zero && sign)
    res |= (1 << 3); // -0
  else if (is_zero && !sign)
    res |= (1 << 4); // +0
  else if (is_subnormal && !sign)
    res |= (1 << 5); // +subnormal
  else if (!is_inf && !is_zero && !is_nan && !is_subnormal && !sign)
    res |= (1 << 6); // +normal
  else if (is_inf && !sign)
    res |= (1 << 7); // +inf

  if (is_snan)
    res |= (1 << 8);
  if (is_qnan)
    res |= (1 << 9);

  return res;
}
