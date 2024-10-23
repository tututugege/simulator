#include <stdint.h>

#include "../include/uart.h"
#include "../include/utils.h"
#include "../include/xprintf.h"

int main(int argc, char **argv) {
  uart_init();

  // your code
  int a = 1;
  xprintf("hello world\n");

  return 0;
}
