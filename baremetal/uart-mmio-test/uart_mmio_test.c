#include "../include/uart.h"
#include "../include/xprintf.h"

#define UART_MMIO_BASE 0x10000000u

static inline unsigned char uart_mmio_read8(unsigned int offset) {
  return readb((const volatile void *)(UART_MMIO_BASE + offset));
}

static inline void uart_mmio_write8(unsigned int offset, unsigned char value) {
  writeb(value, (volatile void *)(UART_MMIO_BASE + offset));
}

static void uart_mmio_puts(const char *s) {
  while (*s != '\0') {
    uart_mmio_write8(UART_THR_OFFSET, (unsigned char)*s);
    s++;
  }
}

int main(void) {
  unsigned char lsr = 0;

  uart_init();

  xputs("\n[UART-MMIO] smoke test begin\n");

  lsr = uart_mmio_read8(UART_LSR_OFFSET);
  xprintf("[UART-MMIO] LSR=0x%02x THRE=%d TEMT=%d\n",
          (unsigned int)lsr,
          (lsr & UART_LSR_THRE) ? 1 : 0,
          (lsr & UART_LSR_TEMT) ? 1 : 0);

  xputs("[UART-MMIO] xprintf path says hello.\n");
  uart_mmio_puts("[UART-MMIO] raw MMIO write path says hello.\n");
  uart_mmio_puts("[UART-MMIO] end.\n");

  return 0;
}
