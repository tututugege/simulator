#include "../include/trap.h"
#include "../include/uart.h"
#include "../include/xprintf.h"
#include <stdint.h>

#define SATP_MODE_SV32 (1u << 31)

#define PTE_V (1u << 0)
#define PTE_R (1u << 1)
#define PTE_W (1u << 2)
#define PTE_A (1u << 6)
#define PTE_D (1u << 7)

#define PAGE_SIZE 4096u
#define PAGE_MASK (~(PAGE_SIZE - 1u))

#define LOAD_PAGE_FAULT_CAUSE 13u

#define TEST_VA_PAGE 0x40000000u
#define TEST_FAULT_VA 0x50000000u
#define EXPECT_INIT 0x11223344u
#define EXPECT_NEW 0x55667788u

extern void install_trap_vector(void);
extern uint32_t mprv_load_u32(uint32_t va);
extern void mprv_store_u32(uint32_t va, uint32_t value);
extern void write_satp(uint32_t value);
extern void sfence_vma_all_asm(void);

volatile uint32_t trap_count = 0;
volatile uint32_t last_mcause = 0;
volatile uint32_t last_mtval = 0;
volatile uint32_t last_mepc = 0;

static volatile uint32_t backing_word = EXPECT_INIT;
static uint32_t l1_pt[1024] __attribute__((aligned(4096)));
static uint32_t l0_pt[1024] __attribute__((aligned(4096)));

static void clear_trap_state(void) {
  trap_count = 0;
  last_mcause = 0;
  last_mtval = 0;
  last_mepc = 0;
}

static uint32_t make_leaf_pte(uint32_t pa_page, uint32_t flags) {
  return ((pa_page >> 12) << 10) | flags;
}

static void map_single_page(uint32_t va_page, uint32_t pa_page, uint32_t flags) {
  uint32_t vpn1 = (va_page >> 22) & 0x3ffu;
  uint32_t vpn0 = (va_page >> 12) & 0x3ffu;
  uint32_t l0_ppn = ((uint32_t)l0_pt >> 12) & 0x3fffffu;

  l1_pt[vpn1] = (l0_ppn << 10) | PTE_V;
  l0_pt[vpn0] = make_leaf_pte(pa_page, flags | PTE_V);
}

static void fail(const char *msg) {
  xprintf("[VM-PF] FAIL: %s\n", msg);
  halt(1);
}

int main(void) {
  uint32_t backing_pa;
  uint32_t mapped_va;
  uint32_t satp_val;
  uint32_t load_val;

  uart_init();
  xputs("\n[VM-PF] start sv32 va->pa + page fault test\n");

  install_trap_vector();
  clear_trap_state();

  for (int i = 0; i < 1024; i++) {
    l1_pt[i] = 0;
    l0_pt[i] = 0;
  }

  backing_pa = (uint32_t)&backing_word;
  mapped_va = TEST_VA_PAGE | (backing_pa & (PAGE_SIZE - 1u));
  map_single_page(TEST_VA_PAGE, backing_pa & PAGE_MASK, PTE_R | PTE_W | PTE_A | PTE_D);

  satp_val = SATP_MODE_SV32 | ((((uint32_t)l1_pt) >> 12) & 0x3fffffu);
  write_satp(satp_val);
  sfence_vma_all_asm();

  xprintf("[VM-PF] map va=0x%08x -> pa=0x%08x\n", mapped_va, backing_pa);

  load_val = mprv_load_u32(mapped_va);
  if (load_val != EXPECT_INIT) {
    fail("translated load mismatch");
  }
  if (trap_count != 0) {
    fail("unexpected trap during translated load");
  }

  mprv_store_u32(mapped_va, EXPECT_NEW);
  if (backing_word != EXPECT_NEW) {
    fail("translated store did not update backing physical word");
  }
  if (trap_count != 0) {
    fail("unexpected trap during translated store");
  }

  clear_trap_state();
  (void)mprv_load_u32(TEST_FAULT_VA);
  if (trap_count != 1) {
    fail("missing page fault on unmapped load");
  }
  if (last_mcause != LOAD_PAGE_FAULT_CAUSE) {
    xprintf("[VM-PF] got mcause=0x%08x expected=0x%08x\n",
            (unsigned int)last_mcause, (unsigned int)LOAD_PAGE_FAULT_CAUSE);
    fail("unexpected mcause");
  }
  if (last_mtval != TEST_FAULT_VA) {
    xprintf("[VM-PF] got mtval=0x%08x expected=0x%08x\n",
            (unsigned int)last_mtval, (unsigned int)TEST_FAULT_VA);
    fail("unexpected mtval");
  }

  xprintf("[VM-PF] page fault captured: mcause=%u mtval=0x%08x mepc=0x%08x\n",
          (unsigned int)last_mcause,
          (unsigned int)last_mtval,
          (unsigned int)last_mepc);

  write_satp(0);
  sfence_vma_all_asm();

  xputs("[VM-PF] PASS\n");
  halt(0);
  return 0;
}
