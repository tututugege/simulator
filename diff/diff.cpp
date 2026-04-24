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
static RefCpuContext *ref_cpu_ctx = nullptr;

namespace {
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

RefCpuState to_ref_state(const CPU_state &state, uint8_t privilege) {
  RefCpuState ref_state{};
  std::memcpy(ref_state.gpr, state.gpr, sizeof(ref_state.gpr));
  std::memcpy(ref_state.csr, state.csr, sizeof(ref_state.csr));
  ref_state.pc = state.pc;
  ref_state.privilege = privilege;
  ref_state.instruction = state.instruction;
  ref_state.page_fault_inst = state.page_fault_inst;
  ref_state.page_fault_load = state.page_fault_load;
  ref_state.page_fault_store = state.page_fault_store;
  ref_state.store = state.store;
  ref_state.store_addr = state.store_addr;
  ref_state.store_data = state.store_data;
  ref_state.store_strb = state.store_strb;
  ref_state.reserve_valid = state.reserve_valid;
  ref_state.reserve_addr = state.reserve_addr;
  return ref_state;
}

CPU_state from_ref_state(const RefCpuState &ref_state) {
  CPU_state state{};
  std::memcpy(state.gpr, ref_state.gpr, sizeof(state.gpr));
  std::memcpy(state.csr, ref_state.csr, sizeof(state.csr));
  state.pc = ref_state.pc;
  state.store = ref_state.store;
  state.store_addr = ref_state.store_addr;
  state.store_data = ref_state.store_data;
  state.store_strb = ref_state.store_strb;
  state.instruction = ref_state.instruction;
  state.page_fault_inst = ref_state.page_fault_inst;
  state.page_fault_load = ref_state.page_fault_load;
  state.page_fault_store = ref_state.page_fault_store;
  state.reserve_valid = ref_state.reserve_valid;
  state.reserve_addr = ref_state.reserve_addr;
  return state;
}

void ensure_ref_cpu() {
  if (ref_cpu_ctx == nullptr) {
    ref_cpu_ctx = refcpu_init(0, nullptr, static_cast<uint32_t>(RAM_SIZE));
    Assert(ref_cpu_ctx != nullptr && "refcpu_init failed");
  }
}

RefCpuState current_ref_state() {
  ensure_ref_cpu();
  RefCpuState state{};
  refcpu_get_state(ref_cpu_ctx, &state);
  return state;
}

RefCpuStepInfo current_step_info() {
  ensure_ref_cpu();
  RefCpuStepInfo info{};
  refcpu_get_step_info(ref_cpu_ctx, &info);
  return info;
}

inline void seed_ref_io_from_backing() {
  ensure_ref_cpu();
  refcpu_clear_io_words(ref_cpu_ctx);
  for (const auto &range : kCkptIoRanges) {
    for (uint32_t off = 0; off + 4u <= range.size; off += 4u) {
      const uint32_t addr = range.base + off;
      const uint32_t data = pmem_read(addr);
      refcpu_seed_ref_io_from_backing(ref_cpu_ctx, addr, data);
    }
  }
}

void dump_mem_subsystem_snapshot() {
  for (int i = 0; i < DCACHE_MSHR_ENTRIES; ++i) {
    const auto &e = mshr_entries[i];
    std::printf("[DIFF][MSHR] idx=%d v=%d issued=%d fill=%d set=%u tag=0x%x "
                "line=0x%08x\n",
                i, static_cast<int>(e.valid), static_cast<int>(e.issued),
                static_cast<int>(e.fill), e.index, e.tag,
                get_addr(e.index, e.tag, 0));
  }
  for (int i = 0; i < DCACHE_WB_ENTRIES; ++i) {
    const auto &e = write_buffer[i];
    std::printf("[DIFF][WB] idx=%d v=%d send=%d addr=0x%08x data0=0x%08x\n", i,
                static_cast<int>(e.valid), static_cast<int>(e.send), e.addr,
                e.data[0]);
  }
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
                tag, row * 4,
                refcpu_load_word(ref_cpu_ctx, line_base + (row + 0) * 4u),
                refcpu_load_word(ref_cpu_ctx, line_base + (row + 1) * 4u),
                refcpu_load_word(ref_cpu_ctx, line_base + (row + 2) * 4u),
                refcpu_load_word(ref_cpu_ctx, line_base + (row + 3) * 4u));
  }
}

} // namespace

// relocate the init_difftest function to avoid multiple definition error
void init_difftest(int img_size) {
  if (ref_cpu_ctx != nullptr) {
    refcpu_destroy(ref_cpu_ctx);
  }
  ref_cpu_ctx = refcpu_init(0, nullptr, static_cast<uint32_t>(RAM_SIZE));
  Assert(ref_cpu_ctx != nullptr && "init_difftest: refcpu_init failed");
  refcpu_sync_ram_from_dut(ref_cpu_ctx, pmem_ram_ptr(), img_size);
  seed_ref_io_from_backing();
}

void init_diff_ckpt(CPU_state ckpt_state) {
  std::cout << "Restore for ref cpu " << std::endl;
  if (ref_cpu_ctx != nullptr) {
    refcpu_destroy(ref_cpu_ctx);
  }
  ref_cpu_ctx = refcpu_init(0, nullptr, static_cast<uint32_t>(RAM_SIZE));
  Assert(ref_cpu_ctx != nullptr && "init_diff_ckpt: refcpu_init failed");
  RefCpuState ref_state = to_ref_state(ckpt_state, RISCV_MODE_U);
  refcpu_set_state(ref_cpu_ctx, &ref_state);

  const uint32_t *ckpt_memory = pmem_ram_ptr();
  Assert(ckpt_memory != nullptr &&
         "init_diff_ckpt: pmem RAM backend is not initialized");
  refcpu_sync_ram_from_dut(ref_cpu_ctx, ckpt_memory,
                           static_cast<int>(RAM_SIZE));
  seed_ref_io_from_backing();
}

void get_state(CPU_state &dut_state, uint8_t &privilege) {
  const RefCpuState ref_state = current_ref_state();
  dut_state = from_ref_state(ref_state);
  privilege = ref_state.privilege;
  uint32_t *dut_memory = pmem_ram_ptr();
  Assert(dut_memory != nullptr &&
         "get_state: pmem RAM backend is not initialized");
  uint32_t *ref_memory = refcpu_get_ram_ptr(ref_cpu_ctx);
  Assert(ref_memory != nullptr && "get_state: refcpu RAM is not initialized");
  std::memcpy(dut_memory, ref_memory, static_cast<size_t>(RAM_SIZE));
  for (const auto &range : kCkptIoRanges) {
    for (uint32_t off = 0; off + 4u <= range.size; off += 4u) {
      const uint32_t addr = range.base + off;
      pmem_write(addr, refcpu_get_io_word(ref_cpu_ctx, addr));
    }
  }
}

static void checkregs() {
  int i;
  const RefCpuState ref_state = current_ref_state();
  const RefCpuStepInfo step_info = current_step_info();

  if (ref_state.pc != dut_cpu.pc)
    goto fault;

  // 如果没有指令缺页异常，且指令不匹配，报错
  if (!step_info.page_fault_inst && step_info.instruction != dut_cpu.instruction)
    goto fault;

  if (step_info.page_fault_inst != dut_cpu.page_fault_inst)
    goto fault;
  if (step_info.page_fault_load != dut_cpu.page_fault_load)
    goto fault;
  if (step_info.page_fault_store != dut_cpu.page_fault_store)
    goto fault;

  // 通用寄存器
  for (i = 0; i < 32; i++) {
    if (ref_state.gpr[i] != dut_cpu.gpr[i])
      goto fault;
  }

  // csr
  for (i = 0; i < CSR_NUM; i++) {
    if (ref_state.csr[i] != dut_cpu.csr[i])
      goto fault;
  }

  if (ref_state.store) {
    if (dut_cpu.store != ref_state.store)
      goto fault;

    if (dut_cpu.store_data != ref_state.store_data)
      goto fault;

    if (dut_cpu.store_addr != ref_state.store_addr)
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

  printf("        PC:\t%08x\t%08x\n", ref_state.pc, dut_cpu.pc);
  cout << "\t\tReference\tDut" << endl;
  for (int i = 0; i < 32; i++) {
    print_mismatch(reg_names[i].c_str(), ref_state.gpr[i], dut_cpu.gpr[i]);
  }

  cout << endl;
  for (int i = 0; i < CSR_NUM; i++) {
    print_mismatch(csr_names[i].c_str(), ref_state.csr[i], dut_cpu.csr[i]);
  }

  cout << endl;
  print_mismatch("store", ref_state.store, dut_cpu.store);
  print_mismatch("data", ref_state.store_data, dut_cpu.store_data);
  print_mismatch("addr", ref_state.store_addr, dut_cpu.store_addr);

  printf("Ref Inst: %08x\tDUT Inst: %08x\n", step_info.instruction,
         dut_cpu.instruction);
  std::printf("Commit PC: 0x%08x\tDUT next PC: 0x%08x\tREF next PC: 0x%08x\n",
              dut_cpu.commit_pc, dut_cpu.pc, ref_state.pc);
  std::printf("[DIFF] p_memory@a5(0x%08x)=0x%08x ref=0x%08x\n", dut_cpu.gpr[15],
              pmem_read(dut_cpu.gpr[15]),
              refcpu_load_word(ref_cpu_ctx, dut_cpu.gpr[15]));
  dump_code_line_snapshot("commit_pc", dut_cpu.commit_pc);
#if defined(LOG_ENABLE) && defined(LOG_LSU_MEM_ENABLE)
  diff_mem_trace::dump_recent();
#endif
  dump_mem_subsystem_snapshot();

  Assert(0 && "Difftest: Register or Memory mismatch detected.");
}

void difftest_skip() {
  ensure_ref_cpu();
  refcpu_step(ref_cpu_ctx, 1);
  RefCpuState ref_state = current_ref_state();
  for (int i = 0; i < 32; i++) {
    ref_state.gpr[i] = dut_cpu.gpr[i];
  }
  refcpu_set_state(ref_cpu_ctx, &ref_state);
}

void difftest_step(bool check) {
  ensure_ref_cpu();
  refcpu_step(ref_cpu_ctx, 1);
  if (check)
    checkregs();
}

bool difftest_ref_sim_end() {
  return current_step_info().sim_end;
}

void difftest_ref_set_uart_print(bool enable) {
  ensure_ref_cpu();
  refcpu_set_uart_print(ref_cpu_ctx, enable);
}

void difftest_dump_memory_line(const char *tag, uint32_t addr) {
  dump_code_line_snapshot(tag, addr);
}
