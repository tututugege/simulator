#include <am.h>
#include <uart.h>
#include <xprintf.h>
#define STACK_SIZE (4096 * 8)
typedef union {
  uint8_t stack[STACK_SIZE];
  struct {
    Context *cp;
  };
} PCB;
static PCB pcb[2], pcb_boot, *current = &pcb_boot;

static void f(void *arg) {
  int i = 0;
  while (1) {
    if ((uint32_t)arg == 1) {
      xprintf("This is proc 1(%d)\n", i);
    } else {
      xprintf("This is proc 2(%d)\n", i);
    }
    yield();
  }
}

static Context *schedule(Event ev, Context *prev) {
  current->cp = prev;
  current = (current == &pcb[0] ? &pcb[1] : &pcb[0]);
  return current->cp;
}

int main() {
  cte_init(schedule);
  uart_init();
  pcb[0].cp = kcontext((Area){pcb[0].stack, &pcb[0] + 1}, f, (void *)1L);
  pcb[1].cp = kcontext((Area){pcb[1].stack, &pcb[1] + 1}, f, (void *)2L);
  yield();
  panic("Should not reach here!");
}
