#include "../include/uart.h"
#include "../include/xprintf.h"

uint32_t fp32_add(uint32_t a, uint32_t b) {
  uint32_t res;
  // 使用 .insn 模板生成指令
  asm volatile(".insn r 0x33, 0x0, 0x02, %0, %1, %2"
               : "=r"(res)
               : "r"(a), "r"(b));
  return res;
}

int main() {
  uart_init();
  xprintf("0X%X\n", fp32_add(0x3F800000, 0x40000000));
  return 0;
}
