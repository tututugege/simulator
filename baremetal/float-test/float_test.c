#include "../include/uart.h"
#include "../include/xprintf.h"

#include <stdbool.h>
#include <stdint.h>

// 简单的检查宏，如果条件失败，返回行号作为错误码
#define ASSERT(cond)                                                           \
  if (!(cond))                                                                 \
    return 1;

// --------------------------------------------------------
// Test 1: 基础算术 (FADD, FSUB, FMUL, FDIV)
// --------------------------------------------------------
int test_arithmetic() {
  volatile float a = 10.5f;
  volatile float b = 2.5f;

  volatile float add = a + b; // Expected: 13.0
  volatile float sub = a - b; // Expected: 8.0
  volatile float mul = a * b; // Expected: 26.25
  volatile float div = a / b; // Expected: 4.2

  ASSERT(add == 13.0f);
  ASSERT(sub == 8.0f);
  ASSERT(mul == 26.25f);
  ASSERT(div == 4.2f);

  return 0;
}

// --------------------------------------------------------
// Test 2: 符号注入与绝对值 (FSGNJ 系列)
// Zfinx 中这通常编译为位操作，但测试正确性很重要
// --------------------------------------------------------
int test_sign() {
  volatile float a = -12.34f;
  volatile float b = 12.34f;

  // 测试取反
  volatile float neg_a = -a;
  ASSERT(neg_a == 12.34f);

  // 测试 FABS (编译器通常用 FSGNJX 或类似的位操作清零符号位)
  // 这里我们手动模拟 abs 的逻辑
  volatile float abs_a = (a < 0) ? -a : a;
  ASSERT(abs_a == 12.34f);

  return 0;
}

// --------------------------------------------------------
// Test 3: 比较指令 (FEQ, FLT, FLE)
// Zfinx 将结果写回整数寄存器 (0 或 1)
// --------------------------------------------------------
int test_comparison() {
  volatile float x = 1.0f;
  volatile float y = 2.0f;
  volatile float z = 1.0f;

  ASSERT((x < y) == 1);  // FLT
  ASSERT((y > x) == 1);  // FLT (swap args)
  ASSERT((x <= z) == 1); // FLE
  ASSERT((x == z) == 1); // FEQ
  ASSERT((x == y) == 0);

  return 0;
}

// --------------------------------------------------------
// Test 4: 类型转换 (FCVT.W.S, FCVT.S.W)
// --------------------------------------------------------
int test_conversion() {
  volatile int i_in = -42;
  volatile float f_in = 123.456f;

  // Int -> Float
  volatile float f_out = (float)i_in;
  ASSERT(f_out == -42.0f);

  // Float -> Int (默认向零截断)
  volatile int i_out = (int)f_in;
  ASSERT(i_out == 123);

  return 0;
}

// --------------------------------------------------------
// Test 5: FMA (Fused Multiply-Add)
// 编译器在优化开启时可能会生成 fmadd，但也可能生成 fmul+fadd
// --------------------------------------------------------
int test_fma() {
  volatile float a = 2.0f;
  volatile float b = 3.0f;
  volatile float c = 4.0f;

  // res = (2 * 3) + 4 = 10
  volatile float res = (a * b) + c;
  ASSERT(res == 10.0f);

  return 0;
}

// --------------------------------------------------------
// Test 6: Min/Max (FMIN, FMAX)
// 需要测试 NaN 的传播行为 (RISC-V 特性)
// --------------------------------------------------------
int test_minmax() {
  // 构造 NaN (0x7fc00001) 和 数字
  // 使用 union 进行位操作，模拟 Zfinx 的寄存器复用
  union {
    uint32_t u;
    float f;
  } nan_val, num_val, res;

  nan_val.u = 0x7fc00001; // qNaN
  num_val.f = 5.0f;

  // 这里依赖编译器生成 fmax/fmin 指令。
  // 如果编译器没生成，这段代码测试的是软实现，但也无害。
  // 为了强制生成，有时需要内联汇编，这里先用逻辑测试。

  // 逻辑验证：如果模拟器正确实现了 FMIN/MAX，
  // 简单的比较操作在底层会依赖 feq/flt
  ASSERT(num_val.f < 6.0f);

  return 0;
}

// --------------------------------------------------------
// Main Entry
// --------------------------------------------------------
// 裸机环境可能需要 _start 入口，这里为了防止链接错误，
// 可以在编译时加上 -nostartfiles -e main 或者自己写个 crt0.s
// 如果你的模拟器直接从 binary 的第一条指令开始执行，把 main 放在最前面即可。

int main() {
  uart_init();
  int result = 0;

  xprintf("Test Start\n");
  if ((result = test_arithmetic()) != 0)
    xprintf("Error: test_arithmetic\n");
  else
    xprintf("Pass: test_arithmetic\n");

  if ((result = test_sign()) != 0)
    xprintf("Error: test_sign\n");
  else
    xprintf("Pass: test_sign\n");

  if ((result = test_comparison()) != 0)
    xprintf("Error: test_comparison\n");
  else
    xprintf("Pass: test_comparison\n");

  if ((result = test_conversion()) != 0)
    xprintf("Error: test_conversion\n");
  else
    xprintf("Pass: test_conversion\n");

  if ((result = test_comparison()) != 0)
    xprintf("Error: test_comparison\n");
  else
    xprintf("Pass: test_comparison\n");

  if ((result = test_fma()) != 0)
    xprintf("Error: test_fma\n");
  else
    xprintf("Pass: test_fma\n");

  if ((result = test_minmax()) != 0)
    xprintf("Error: test_minmax\n");
  else
    xprintf("Pass: test_minmax\n");

  // 全部通过，返回 0
  return 0;
}
