#include <stdint.h>

#define UART_BASE 0x10000000u
#define TIMER_LOW_ADDR 0x1fd0e000u
#define TIMER_HIGH_ADDR 0x1fd0e004u

static inline void uart_putc(char ch) {
  *(volatile uint8_t *)UART_BASE = (uint8_t)ch;
}

static void uart_puts(const char *s) {
  while (*s != '\0') {
    uart_putc(*s++);
  }
}

static void uart_put_hex32(uint32_t val) {
  static const char kHex[] = "0123456789abcdef";
  for (int shift = 28; shift >= 0; shift -= 4) {
    uart_putc(kHex[(val >> shift) & 0xf]);
  }
}

static void uart_put_dec_digit(unsigned int val) {
  uart_putc((char)('0' + val));
}

static inline uint32_t mmio_read32(uint32_t addr) {
  uint32_t val;
  asm volatile("lw %0, 0(%1)" : "=r"(val) : "r"(addr));
  return val;
}

static uint32_t read_timer_low(void) { return mmio_read32(TIMER_LOW_ADDR); }

static uint32_t read_timer_high(void) { return mmio_read32(TIMER_HIGH_ADDR); }

static void delay_cycles(volatile uint32_t loops) {
  while (loops-- > 0) {
    asm volatile("" ::: "memory");
  }
}

int main(void) {
  uint32_t prev_lo = 0;
  uint32_t prev_hi = 0;
  int saw_progress = 0;

  uart_puts("\n[TIMER-MMIO] begin\n");

  for (unsigned int i = 0; i < 8; i++) {
    uint32_t hi1 = read_timer_high();
    uint32_t lo = read_timer_low();
    uint32_t hi2 = read_timer_high();

    while (hi1 != hi2) {
      hi1 = read_timer_high();
      lo = read_timer_low();
      hi2 = read_timer_high();
    }

    uart_puts("[TIMER-MMIO] sample=");
    uart_put_dec_digit(i);
    uart_puts(" hi=0x");
    uart_put_hex32(hi2);
    uart_puts(" lo=0x");
    uart_put_hex32(lo);
    uart_puts("\n");

    if (i != 0 && (hi2 > prev_hi || (hi2 == prev_hi && lo > prev_lo))) {
      saw_progress = 1;
    }
    prev_hi = hi2;
    prev_lo = lo;
    delay_cycles(5000);
  }

  if (saw_progress) {
    uart_puts("[TIMER-MMIO] PASS: timer progressed\n");
    return 0;
  }

  uart_puts("[TIMER-MMIO] FAIL: timer stayed constant\n");
  return 1;
}
