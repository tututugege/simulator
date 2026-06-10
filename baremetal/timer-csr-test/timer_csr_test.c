#include "../include/trap.h"
#include "../include/uart.h"
#include "../include/xprintf.h"
#include <stdint.h>

#define TIMER_LOW_ADDR 0x1fd0e000u
#define TIMER_HIGH_ADDR 0x1fd0e004u
#define CSR_STIMECMP 0x5c0u

#define MSTATUS_MIE (1u << 3)
#define MIP_MTIP (1u << 7)

#define MTIMER_INTERRUPT_CAUSE 0x80000007u

extern void install_trap_vector(void);
extern void write_stimecmp(uint32_t val);
extern uint32_t read_stimecmp(void);
extern uint32_t read_mstatus(void);
extern uint32_t read_mie(void);
extern void write_mie(uint32_t val);
extern void set_mstatus_bits(uint32_t mask);
extern uint32_t read_mip(void);

volatile uint32_t trap_count = 0;
volatile uint32_t last_mcause = 0;
volatile uint32_t last_mepc = 0;
volatile uint32_t last_mip = 0;

static inline uint32_t mmio_read32(uint32_t addr) {
  uint32_t val;
  asm volatile("lw %0, 0(%1)" : "=r"(val) : "r"(addr));
  return val;
}

static uint32_t read_timer_low(void) { return mmio_read32(TIMER_LOW_ADDR); }

static uint32_t read_timer_high(void) { return mmio_read32(TIMER_HIGH_ADDR); }

static uint32_t read_timer_stable_low(void) {
  uint32_t hi1 = read_timer_high();
  uint32_t lo = read_timer_low();
  uint32_t hi2 = read_timer_high();
  while (hi1 != hi2) {
    hi1 = read_timer_high();
    lo = read_timer_low();
    hi2 = read_timer_high();
  }
  return lo;
}

static void fail(const char *msg) {
  xprintf("[TIMER-CSR] FAIL: %s\n", msg);
  halt(1);
}

int main(void) {
  uint32_t now;
  uint32_t target;
  uint32_t timeout = 200000u;

  uart_init();
  xputs("\n[TIMER-CSR] start machine timer interrupt test\n");

  install_trap_vector();
  trap_count = 0;
  last_mcause = 0;
  last_mepc = 0;
  last_mip = 0;

  write_stimecmp(0);
  write_mie(read_mie() & ~MIP_MTIP);

  now = read_timer_stable_low();
  target = now + 2000u;
  write_stimecmp(target);

  xprintf("[TIMER-CSR] now=0x%08x target=0x%08x\n",
          (unsigned int)now, (unsigned int)target);

  write_mie(read_mie() | MIP_MTIP);
  set_mstatus_bits(MSTATUS_MIE);

  while (trap_count == 0 && timeout-- > 0) {
    asm volatile("" ::: "memory");
  }

  if (trap_count == 0) {
    xprintf("[TIMER-CSR] timeout now=0x%08x mip=0x%08x mie=0x%08x mstatus=0x%08x\n",
            (unsigned int)read_timer_stable_low(), (unsigned int)read_mip(),
            (unsigned int)read_mie(), (unsigned int)read_mstatus());
    fail("timer interrupt did not fire");
  }

  xprintf("[TIMER-CSR] trap_count=%u mcause=0x%08x mepc=0x%08x mip=0x%08x\n",
          (unsigned int)trap_count, (unsigned int)last_mcause,
          (unsigned int)last_mepc, (unsigned int)last_mip);

  if (trap_count != 1) {
    fail("expected exactly one timer interrupt");
  }
  if (last_mcause != MTIMER_INTERRUPT_CAUSE) {
    fail("unexpected mcause");
  }

  xputs("[TIMER-CSR] PASS\n");
  halt(0);
  return 0;
}
