#include "diff.h"
#include "Csr.h"
#include "DcacheConfig.h"
#include "DiffMemTrace.h"
#include "PhysMemory.h"
#include "RISCV.h"
#include "config.h"
#include "util.h"

#include <cstring>
#include <iostream>

CPU_state dut_cpu;
RefCpu ref_cpu;

namespace {
inline uint32_t sign_extend_12(uint32_t imm12) {
  return static_cast<uint32_t>(static_cast<int32_t>(imm12 << 20) >> 20);
}

struct IoRange {
  uint32_t base;
  uint32_t size;
};

constexpr IoRange kCkptIoRanges[] = {
    {BOOT_IO_BASE, BOOT_IO_SIZE},
    {UART_ADDR_BASE, UART_MMIO_SIZE},
    {PLIC_ADDR_BASE, PLIC_MMIO_SIZE},
    {OPENSBI_TIMER_BASE, OPENSBI_TIMER_MMIO_SIZE},
};

inline void seed_ref_io_from_backing() {
  ref_cpu.io_words.clear();
  for (const auto &range : kCkptIoRanges) {
    for (uint32_t off = 0; off + 4u <= range.size; off += 4u) {
      const uint32_t addr = range.base + off;
      const uint32_t data = pmem_read(addr);
      if (data != 0u) {
        ref_cpu.io_words[addr] = data;
      }
    }
  }
}

void dump_mem_subsystem_snapshot() {
}

void dump_code_line_snapshot(const char *tag, uint32_t pc) {
  const uint32_t line_base =
      pc & ~(static_cast<uint32_t>(ICACHE_LINE_SIZE) - 1u);
  const uint32_t word_off = (pc - line_base) >> 2;
  std::printf(
      "[DIFF][ICACHE_LINE][%s] pc=0x%08x line_base=0x%08x word_off=%u\n", tag,
      pc, line_base, word_off);
  for (int row = 0; row < ICACHE_WORD_NUM; row += 4) {
    std::printf("[DIFF][ICACHE_LINE][%s][DUT] +0x%02x: %08x %08x %08x %08x\n",
                tag, row * 4, pmem_read(line_base + (row + 0) * 4u),
                pmem_read(line_base + (row + 1) * 4u),
                pmem_read(line_base + (row + 2) * 4u),
                pmem_read(line_base + (row + 3) * 4u));
    std::printf("[DIFF][ICACHE_LINE][%s][REF] +0x%02x: %08x %08x %08x %08x\n",
                tag, row * 4, ref_cpu.load_word(line_base + (row + 0) * 4u),
                ref_cpu.load_word(line_base + (row + 1) * 4u),
                ref_cpu.load_word(line_base + (row + 2) * 4u),
                ref_cpu.load_word(line_base + (row + 3) * 4u));
  }
}

} // namespace

// relocate the init_difftest function to avoid multiple definition error
void init_difftest(int img_size) {
  ref_cpu.init(0);
  std::memcpy(ref_cpu.memory, pmem_ram_ptr(), img_size);
  seed_ref_io_from_backing();
}

void init_diff_ckpt(CPU_state ckpt_state) {
  std::cout << "Restore for ref cpu " << std::endl;
  ref_cpu.init(0);
  ref_cpu.state = ckpt_state;
  ref_cpu.privilege = RISCV_MODE_U;

  const uint32_t *ckpt_memory = pmem_ram_ptr();
  Assert(ckpt_memory != nullptr &&
         "init_diff_ckpt: pmem RAM backend is not initialized");
  std::memcpy(ref_cpu.memory, ckpt_memory,
              static_cast<size_t>(PHYSICAL_MEMORY_LENGTH) * sizeof(uint32_t));
  seed_ref_io_from_backing();

  // Keep checkpoint bootstrap aligned with RefCpu::exec(): only probe a
  // translation when SATP is active, and do not require the restored PC to be
  // immediately translatable at init time.
  if ((ref_cpu.state.csr[csr_satp] & 0x80000000u) != 0 &&
      ref_cpu.privilege != RISCV_MODE_M) {
    uint32_t p_addr = 0;
    (void)ref_cpu.va2pa(p_addr, ref_cpu.state.pc, 0);
  }
}

void get_state(CPU_state &dut_state, uint8_t &privilege) {
  dut_state = ref_cpu.state;
  privilege = ref_cpu.privilege;
  uint32_t *dut_memory = pmem_ram_ptr();
  Assert(dut_memory != nullptr &&
         "get_state: pmem RAM backend is not initialized");
  std::memcpy(dut_memory, ref_cpu.memory,
              static_cast<size_t>(PHYSICAL_MEMORY_LENGTH) * sizeof(uint32_t));
  for (const auto &kv : ref_cpu.io_words) {
    pmem_write(kv.first, kv.second);
  }
}

static void checkregs() {
  int i;

  if (ref_cpu.state.pc != dut_cpu.pc)
    goto fault;

  // 如果没有指令缺页异常，且指令不匹配，报错
  if (!ref_cpu.page_fault_inst && ref_cpu.Instruction != dut_cpu.instruction)
    goto fault;

  if (ref_cpu.page_fault_inst != dut_cpu.page_fault_inst)
    goto fault;
  if (ref_cpu.page_fault_load != dut_cpu.page_fault_load)
    goto fault;
  if (ref_cpu.page_fault_store != dut_cpu.page_fault_store)
    goto fault;

  // 通用寄存器
  for (i = 0; i < 32; i++) {
    if (ref_cpu.state.gpr[i] != dut_cpu.gpr[i])
      goto fault;
  }

  // csr
  for (i = 0; i < CSR_NUM; i++) {
    if (ref_cpu.state.csr[i] != dut_cpu.csr[i])
      goto fault;
  }

  if (ref_cpu.state.store) {
    if (dut_cpu.store != ref_cpu.state.store)
      goto fault;

    if (dut_cpu.store_data != ref_cpu.state.store_data)
      goto fault;

    if (dut_cpu.store_addr != ref_cpu.state.store_addr)
      goto fault;
  }

  return;

fault:
  cout << "Difftest: error" << endl;
  cout << "cycle: " << dec << sim_time << endl;

  auto print_mismatch = [](const char *name, uint32_t ref, uint32_t dut) {
    printf("%10s:\t%08x\t%08x%s\n", name, ref, dut,
           (ref != dut ? "\t Error" : ""));
  };

  printf("        PC:\t%08x\t%08x\n", ref_cpu.state.pc, dut_cpu.pc);
  cout << "\t\tReference\tDut" << endl;
  for (int i = 0; i < 32; i++) {
    print_mismatch(reg_names[i].c_str(), ref_cpu.state.gpr[i], dut_cpu.gpr[i]);
  }

  cout << endl;
  for (int i = 0; i < CSR_NUM; i++) {
    print_mismatch(csr_names[i].c_str(), ref_cpu.state.csr[i], dut_cpu.csr[i]);
  }

  cout << endl;
  print_mismatch("store", ref_cpu.state.store, dut_cpu.store);
  print_mismatch("data", ref_cpu.state.store_data, dut_cpu.store_data);
  print_mismatch("addr", ref_cpu.state.store_addr, dut_cpu.store_addr);

  printf("Ref Inst: %08x\tDUT Inst: %08x\n", ref_cpu.Instruction,
         dut_cpu.instruction);
  std::printf("Commit PC: 0x%08x\tDUT next PC: 0x%08x\tREF next PC: 0x%08x\n",
              dut_cpu.commit_pc, dut_cpu.pc, ref_cpu.state.pc);
  
  std::printf("[DIFF] p_memory@a5(0x%08x)=0x%08x ref=0x%08x\n", dut_cpu.gpr[15],
              pmem_read(dut_cpu.gpr[15]),
              ref_cpu.load_word(dut_cpu.gpr[15]));
  dump_code_line_snapshot("commit_pc", dut_cpu.commit_pc);
#if defined(LOG_ENABLE) && defined(LOG_LSU_MEM_ENABLE)
  diff_mem_trace::dump_recent();
#endif
  dump_mem_subsystem_snapshot();

  Assert(0 && "Difftest: Register or Memory mismatch detected.");
}

void difftest_skip() {
  ref_cpu.dut_expect_pf_inst = dut_cpu.page_fault_inst;
  ref_cpu.dut_expect_pf_load = dut_cpu.page_fault_load;
  ref_cpu.dut_expect_pf_store = dut_cpu.page_fault_store;
  ref_cpu.exec();
  for (int i = 0; i < 32; i++) {
    ref_cpu.state.gpr[i] = dut_cpu.gpr[i];
  }
}

void difftest_step(bool check) {
  ref_cpu.dut_expect_pf_inst = dut_cpu.page_fault_inst;
  ref_cpu.dut_expect_pf_load = dut_cpu.page_fault_load;
  ref_cpu.dut_expect_pf_store = dut_cpu.page_fault_store;
  ref_cpu.exec();
  if (check)
    checkregs();
}
