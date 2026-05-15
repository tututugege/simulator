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
#define STORE_PAGE_FAULT_CAUSE 15u

#define TEST_VA_PAGE 0x40000000u
#define EXPECT_INIT 0x11223344u
#define EXPECT_WARM 0x55667788u
#define EXPECT_RESTORED 0x99aabbccu

extern void install_trap_vector(void);
extern uint32_t mprv_load_u32(uint32_t va);
extern void mprv_store_u32(uint32_t va, uint32_t value);
extern void write_satp(uint32_t value);
extern void sfence_vma_all_asm(void);

volatile uint32_t trap_count = 0;
volatile uint32_t last_mcause = 0;
volatile uint32_t last_mtval = 0;
volatile uint32_t last_mepc = 0;

static volatile uint32_t backing_page[PAGE_SIZE / sizeof(uint32_t)]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t l1_pt[1024] __attribute__((aligned(PAGE_SIZE)));
static uint32_t l0_pt[1024] __attribute__((aligned(PAGE_SIZE)));

static void clear_trap_state(void) {
  trap_count = 0;
  last_mcause = 0;
  last_mtval = 0;
  last_mepc = 0;
}

static void clear_page(uint32_t *page) {
  for (int i = 0; i < (int)(PAGE_SIZE / sizeof(uint32_t)); i++) {
    page[i] = 0;
  }
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

static void set_leaf_flags(uint32_t va_page, uint32_t pa_page, uint32_t flags) {
  uint32_t vpn0 = (va_page >> 12) & 0x3ffu;
  l0_pt[vpn0] = make_leaf_pte(pa_page, flags | PTE_V);
}

static void fail(const char *msg) {
  xprintf("[VM-SFENCE-PERM] FAIL: %s\n", msg);
  halt(1);
}

static void expect_no_trap(const char *phase) {
  if (trap_count != 0) {
    xprintf(
        "[VM-SFENCE-PERM] trap during %s: count=%u mcause=0x%08x mtval=0x%08x "
        "mepc=0x%08x\n",
        phase, (unsigned int)trap_count, (unsigned int)last_mcause,
        (unsigned int)last_mtval, (unsigned int)last_mepc);
    fail("unexpected trap");
  }
}

static void expect_word(const char *phase, uint32_t got, uint32_t expected) {
  if (got != expected) {
    xprintf("[VM-SFENCE-PERM] %s got=0x%08x expected=0x%08x\n", phase,
            (unsigned int)got, (unsigned int)expected);
    fail("unexpected data");
  }
}

static void expect_store_pf(uint32_t expected_va) {
  if (trap_count != 1) {
    fail("missing store page fault");
  }
  if (last_mcause != STORE_PAGE_FAULT_CAUSE) {
    xprintf("[VM-SFENCE-PERM] got mcause=0x%08x expected=0x%08x\n",
            (unsigned int)last_mcause, (unsigned int)STORE_PAGE_FAULT_CAUSE);
    fail("unexpected store fault mcause");
  }
  if (last_mtval != expected_va) {
    xprintf("[VM-SFENCE-PERM] got mtval=0x%08x expected=0x%08x\n",
            (unsigned int)last_mtval, (unsigned int)expected_va);
    fail("unexpected store fault mtval");
  }
}

int main(void) {
  const uint32_t mapped_va = TEST_VA_PAGE;
  const uint32_t writable_flags = PTE_R | PTE_W | PTE_A | PTE_D;
  const uint32_t readonly_flags = PTE_R | PTE_A;
  const uint32_t backing_pa = (uint32_t)backing_page;
  const uint32_t satp_val =
      SATP_MODE_SV32 | ((((uint32_t)l1_pt) >> 12) & 0x3fffffu);
  uint32_t load_val = 0;

  uart_init();
  xputs("\n[VM-SFENCE-PERM] start sfence permission downgrade test\n");

  install_trap_vector();
  clear_trap_state();
  clear_page(l1_pt);
  clear_page(l0_pt);
  clear_page((uint32_t *)backing_page);
  backing_page[0] = EXPECT_INIT;

  map_single_page(TEST_VA_PAGE, backing_pa & PAGE_MASK, writable_flags);
  write_satp(satp_val);
  sfence_vma_all_asm();

  clear_trap_state();
  load_val = mprv_load_u32(mapped_va);
  expect_word("initial load", load_val, EXPECT_INIT);
  expect_no_trap("initial load");

  clear_trap_state();
  mprv_store_u32(mapped_va, EXPECT_WARM);
  expect_no_trap("warm store");
  expect_word("backing after warm store", backing_page[0], EXPECT_WARM);

  set_leaf_flags(TEST_VA_PAGE, backing_pa & PAGE_MASK, readonly_flags);
  sfence_vma_all_asm();

  clear_trap_state();
  load_val = mprv_load_u32(mapped_va);
  expect_word("readonly load", load_val, EXPECT_WARM);
  expect_no_trap("readonly load");

  clear_trap_state();
  mprv_store_u32(mapped_va, 0xdeadbeefu);
  expect_store_pf(mapped_va);
  expect_word("backing unchanged after readonly store", backing_page[0],
              EXPECT_WARM);

  set_leaf_flags(TEST_VA_PAGE, backing_pa & PAGE_MASK, writable_flags);
  sfence_vma_all_asm();

  clear_trap_state();
  mprv_store_u32(mapped_va, EXPECT_RESTORED);
  expect_no_trap("restored store");
  expect_word("backing after restored store", backing_page[0], EXPECT_RESTORED);

  write_satp(0);
  sfence_vma_all_asm();

  xputs("[VM-SFENCE-PERM] PASS\n");
  halt(0);
  return 0;
}
