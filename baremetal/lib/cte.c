#include "../include/am.h"
#include <stdint.h>

static Context *(*user_handler)(Event, Context *) = NULL;

Context *__am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev;
    switch (c->mcause) {
    case ECALL_U:
      ev.e = EVENT_YIELD;
      c->mepc += 4;
      break;
    default:
      ev.e = EVENT_ERROR;
      break;
    }

    c = user_handler(ev, c);
    assert(c != NULL);
  }

  return c;
}

extern void __am_asm_trap(void);

bool cte_init(Context *(*handler)(Event, Context *)) {
  // initialize exception entry
  asm volatile("csrw mtvec, %0" : : "r"(__am_asm_trap));

  // register event handler
  user_handler = handler;

  return true;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  Context *ctx = kstack.end - sizeof(Context);
  ctx->mepc = (uint32_t)entry;
  ctx->mstatus = 0x1800; // only M-mode
  ctx->gpr[10] = (uint32_t)arg;

  *(Context **)kstack.start = ctx;
  return ctx;
}

void yield() { asm volatile("li a7, -1; ecall"); }
