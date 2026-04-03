#include "../include/trap.h"
#include "../include/uart.h"
#include "../include/xprintf.h"
#include <stdint.h>

#define SATP_MODE_SV32 (1u << 31)
#define SATP_ASID_SHIFT 22u
#define SATP_ASID_MASK 0x1FFu

#define PTE_V (1u << 0)
#define PTE_R (1u << 1)
#define PTE_W (1u << 2)
#define PTE_A (1u << 6)
#define PTE_D (1u << 7)

#define PAGE_SIZE 4096u

#define TEST_VA_PAGE 0x40000000u
#define ASID_A 0x000u
#define ASID_B 0x100u

#define EXPECT_A 0x11223344u
#define EXPECT_A_NEW 0x13579bdfu
#define EXPECT_B 0x55667788u
#define EXPECT_B_NEW 0x2468ace0u

extern void install_trap_vector(void);
extern uint32_t mprv_load_u32(uint32_t va);
extern void mprv_store_u32(uint32_t va, uint32_t value);
extern void write_satp(uint32_t value);
extern void sfence_vma_all_asm(void);

volatile uint32_t trap_count = 0;
volatile uint32_t last_mcause = 0;
volatile uint32_t last_mtval = 0;
volatile uint32_t last_mepc = 0;

static volatile uint32_t backing_page_a[PAGE_SIZE / sizeof(uint32_t)]
    __attribute__((aligned(PAGE_SIZE)));
static volatile uint32_t backing_page_b[PAGE_SIZE / sizeof(uint32_t)]
    __attribute__((aligned(PAGE_SIZE)));

static uint32_t l1_pt_a[1024] __attribute__((aligned(PAGE_SIZE)));
static uint32_t l0_pt_a[1024] __attribute__((aligned(PAGE_SIZE)));
static uint32_t l1_pt_b[1024] __attribute__((aligned(PAGE_SIZE)));
static uint32_t l0_pt_b[1024] __attribute__((aligned(PAGE_SIZE)));

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

static uint32_t make_satp(uint32_t root_pt_pa, uint32_t asid) {
  return SATP_MODE_SV32 |
         ((asid & SATP_ASID_MASK) << SATP_ASID_SHIFT) |
         ((root_pt_pa >> 12) & 0x3fffffu);
}

static void map_single_page(uint32_t *l1_pt, uint32_t *l0_pt, uint32_t va_page,
                            uint32_t pa_page, uint32_t flags) {
  uint32_t vpn1 = (va_page >> 22) & 0x3ffu;
  uint32_t vpn0 = (va_page >> 12) & 0x3ffu;
  uint32_t l0_ppn = ((uint32_t)l0_pt >> 12) & 0x3fffffu;

  l1_pt[vpn1] = (l0_ppn << 10) | PTE_V;
  l0_pt[vpn0] = make_leaf_pte(pa_page, flags | PTE_V);
}

static void fail(const char *msg) {
  xprintf("[VM-ASID] FAIL: %s\n", msg);
  halt(1);
}

static void expect_no_trap(const char *phase) {
  if (trap_count != 0) {
    xprintf("[VM-ASID] trap during %s: count=%u mcause=0x%08x mtval=0x%08x mepc=0x%08x\n",
            phase, (unsigned int)trap_count, (unsigned int)last_mcause,
            (unsigned int)last_mtval, (unsigned int)last_mepc);
    fail("unexpected trap");
  }
}

static void expect_word(const char *phase, uint32_t got, uint32_t expected) {
  if (got != expected) {
    xprintf("[VM-ASID] %s got=0x%08x expected=0x%08x\n", phase,
            (unsigned int)got, (unsigned int)expected);
    fail("unexpected data");
  }
}

int main(void) {
  const uint32_t mapped_va = TEST_VA_PAGE;
  const uint32_t pte_flags = PTE_R | PTE_W | PTE_A | PTE_D;
  uint32_t satp_a;
  uint32_t satp_b;
  uint32_t load_val;

  uart_init();
  xputs("\n[VM-ASID] start sv32 asid separation test\n");

  install_trap_vector();
  clear_trap_state();

  clear_page(l1_pt_a);
  clear_page(l0_pt_a);
  clear_page(l1_pt_b);
  clear_page(l0_pt_b);
  clear_page((uint32_t *)backing_page_a);
  clear_page((uint32_t *)backing_page_b);
  backing_page_a[0] = EXPECT_A;
  backing_page_b[0] = EXPECT_B;

  map_single_page(l1_pt_a, l0_pt_a, TEST_VA_PAGE, (uint32_t)backing_page_a,
                  pte_flags);
  map_single_page(l1_pt_b, l0_pt_b, TEST_VA_PAGE, (uint32_t)backing_page_b,
                  pte_flags);

  satp_a = make_satp((uint32_t)l1_pt_a, ASID_A);
  satp_b = make_satp((uint32_t)l1_pt_b, ASID_B);

  write_satp(satp_a);
  sfence_vma_all_asm();

  clear_trap_state();
  load_val = mprv_load_u32(mapped_va);
  expect_word("asid_a first load", load_val, EXPECT_A);
  expect_no_trap("asid_a first load");

  // Switch to a different ASID without SFENCE.VMA. Correct Sv32 behavior must
  // miss the prior TLB entry instead of aliasing ASID[8].
  write_satp(satp_b);

  clear_trap_state();
  load_val = mprv_load_u32(mapped_va);
  expect_word("asid_b load", load_val, EXPECT_B);
  expect_no_trap("asid_b load");

  clear_trap_state();
  mprv_store_u32(mapped_va, EXPECT_B_NEW);
  expect_no_trap("asid_b store");
  expect_word("asid_b backing", backing_page_b[0], EXPECT_B_NEW);
  expect_word("asid_a backing unchanged", backing_page_a[0], EXPECT_A);

  // Switch back, still without SFENCE.VMA. The older ASID-tagged entry should
  // remain usable and isolated from the ASID_B mapping.
  write_satp(satp_a);

  clear_trap_state();
  load_val = mprv_load_u32(mapped_va);
  expect_word("asid_a second load", load_val, EXPECT_A);
  expect_no_trap("asid_a second load");

  clear_trap_state();
  mprv_store_u32(mapped_va, EXPECT_A_NEW);
  expect_no_trap("asid_a store");
  expect_word("asid_a backing", backing_page_a[0], EXPECT_A_NEW);
  expect_word("asid_b backing unchanged", backing_page_b[0], EXPECT_B_NEW);

  write_satp(satp_b);

  clear_trap_state();
  load_val = mprv_load_u32(mapped_va);
  expect_word("asid_b second load", load_val, EXPECT_B_NEW);
  expect_no_trap("asid_b second load");

  write_satp(0);
  sfence_vma_all_asm();

  xputs("[VM-ASID] PASS\n");
  halt(0);
  return 0;
}
