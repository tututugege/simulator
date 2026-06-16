#include "../include/trap.h"
#include "../include/uart.h"
#include "../include/xprintf.h"
#include <stdint.h>

#define DMA_SRC_ADDR_REG 0x1fd0f000u
#define DMA_DST_ADDR_REG 0x1fd0f004u
#define DMA_LEN_ADDR_REG 0x1fd0f008u
#define DMA_CTRL_ADDR_REG 0x1fd0f00cu
#define DMA_STATUS_ADDR_REG 0x1fd0f010u

#define DMA_CTRL_START (1u << 0)
#define DMA_CTRL_IRQ_EN (1u << 1)
#define DMA_STATUS_BUSY (1u << 0)
#define DMA_STATUS_DONE (1u << 1)

#define PLIC_CLAIM_ADDR 0x0c201004u
#define DMA_IRQ_ID 11u

#define MSTATUS_MIE (1u << 3)
#define MIP_MEIP (1u << 11)
#define MEXT_INTERRUPT_CAUSE 0x8000000bu

extern void install_trap_vector(void);
extern uint32_t read_mstatus(void);
extern uint32_t read_mie(void);
extern void write_mie(uint32_t val);
extern void set_mstatus_bits(uint32_t mask);
extern uint32_t read_mip(void);

volatile uint32_t trap_count = 0;
volatile uint32_t last_mcause = 0;
volatile uint32_t last_mepc = 0;
volatile uint32_t last_mip = 0;
volatile uint32_t last_claim = 0;

static const char src_data[] = "Rem is waifu";
static volatile char dst_data[sizeof(src_data)]
    __attribute__((section(".dma_buffer"), aligned(64)));

static inline uint32_t mmio_read32(uint32_t addr) {
  uint32_t val;
  asm volatile("lw %0, 0(%1)" : "=r"(val) : "r"(addr));
  return val;
}

static inline void mmio_write32(uint32_t addr, uint32_t val) {
  asm volatile("sw %0, 0(%1)" : : "r"(val), "r"(addr));
}

static void fail(const char *msg) {
  xprintf("[DMA] FAIL: %s\n", msg);
  halt(1);
  while (1) {
  }
}

int main(void) {
  uint32_t timeout = 200000u;
  uint32_t status = 0;
  unsigned int idx = 0;

  uart_init();
  xputs("\n[DMA] start dma interrupt smoke test\n");

  install_trap_vector();
  trap_count = 0;
  last_mcause = 0;
  last_mepc = 0;
  last_mip = 0;
  last_claim = 0;

  write_mie(read_mie() & ~MIP_MEIP);

  mmio_write32(DMA_SRC_ADDR_REG, (uint32_t)(uintptr_t)src_data);
  mmio_write32(DMA_DST_ADDR_REG, (uint32_t)(uintptr_t)dst_data);
  mmio_write32(DMA_LEN_ADDR_REG, sizeof(src_data));

  xprintf("[DMA] src=0x%08x dst=0x%08x len=%u\n",
          (unsigned int)(uintptr_t)src_data, (unsigned int)(uintptr_t)dst_data,
          (unsigned int)sizeof(src_data));

  mmio_write32(DMA_CTRL_ADDR_REG, DMA_CTRL_IRQ_EN | DMA_CTRL_START);

  write_mie(read_mie() | MIP_MEIP);
  set_mstatus_bits(MSTATUS_MIE);

  while (trap_count == 0 && timeout-- > 0) {
    asm volatile("" ::: "memory");
  }

  if (trap_count == 0) {
    xprintf("[DMA] timeout mip=0x%08x mie=0x%08x mstatus=0x%08x claim=0x%08x status=0x%08x\n",
            (unsigned int)read_mip(), (unsigned int)read_mie(),
            (unsigned int)read_mstatus(), (unsigned int)mmio_read32(PLIC_CLAIM_ADDR),
            (unsigned int)mmio_read32(DMA_STATUS_ADDR_REG));
    fail("dma interrupt did not fire");
  }

  status = mmio_read32(DMA_STATUS_ADDR_REG);
  xprintf("[DMA] trap_count=%u mcause=0x%08x mepc=0x%08x mip=0x%08x claim=%u status=0x%08x\n",
          (unsigned int)trap_count, (unsigned int)last_mcause,
          (unsigned int)last_mepc, (unsigned int)last_mip,
          (unsigned int)last_claim, (unsigned int)status);

  if (trap_count != 1) {
    fail("expected exactly one external interrupt");
  }
  if (last_mcause != MEXT_INTERRUPT_CAUSE) {
    fail("unexpected mcause");
  }
  if (last_claim != DMA_IRQ_ID) {
    fail("unexpected PLIC claim id");
  }
  if ((status & DMA_STATUS_DONE) == 0 || (status & DMA_STATUS_BUSY) != 0) {
    fail("unexpected dma status");
  }

  for (idx = 0; idx < sizeof(src_data); ++idx) {
    if (dst_data[idx] != src_data[idx]) {
      xprintf("[DMA] mismatch idx=%u got=0x%02x expect=0x%02x\n",
              idx, (unsigned int)(unsigned char)dst_data[idx],
              (unsigned int)(unsigned char)src_data[idx]);
      fail("dma copy result mismatch");
    }
  }

  xprintf("[DMA] copied string: %s\n", (const char *)dst_data);

  xputs("[DMA] PASS\n");
  halt(0);
  return 0;
}
