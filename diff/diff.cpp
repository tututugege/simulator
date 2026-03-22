#include "diff.h"
#include "RISCV.h"
#include "DiffMemTrace.h"
#include "DcacheConfig.h"
#include "config.h"
#include "util.h"
#include "Csr.h"

#include <cstring>
#include <iostream>

CPU_state dut_cpu;
RefCpu ref_cpu;

namespace {
inline uint32_t sign_extend_12(uint32_t imm12) {
  return static_cast<uint32_t>(static_cast<int32_t>(imm12 << 20) >> 20);
}

void dump_addr_snapshot(const char *tag, uint32_t addr) {
  const uint32_t word_idx = addr >> 2;
  const AddrFields f = decode(addr);
  std::printf("[DIFF][ADDR][%s] addr=0x%08x word_idx=%u set=%u tag=0x%x word_off=%u\n",
              tag, addr, word_idx, f.set_idx, f.tag, f.word_off);
  std::printf("[DIFF][ADDR][%s] mem dut=0x%08x ref=0x%08x\n", tag,
              p_memory[word_idx], ref_cpu.memory[word_idx]);
  for (int w = 0; w < DCACHE_WAYS; ++w) {
    std::printf(
        "[DIFF][DCACHE] set=%u way=%d valid=%d dirty=%d tag=0x%x data[word_off]=0x%08x\n",
        f.set_idx, w, static_cast<int>(valid_array[f.set_idx][w]),
        static_cast<int>(dirty_array[f.set_idx][w]), tag_array[f.set_idx][w],
        data_array[f.set_idx][w][f.word_off]);
  }
}

void dump_mem_subsystem_snapshot() {
  for (int i = 0; i < MSHR_ENTRIES; ++i) {
    const auto &e = mshr_entries[i];
    std::printf("[DIFF][MSHR] idx=%d v=%d issued=%d fill=%d set=%u tag=0x%x line=0x%08x\n",
                i, static_cast<int>(e.valid), static_cast<int>(e.issued),
                static_cast<int>(e.fill), e.index, e.tag,
                get_addr(e.index, e.tag, 0));
  }
  for (int i = 0; i < WB_ENTRIES; ++i) {
    const auto &e = write_buffer[i];
    std::printf("[DIFF][WB] idx=%d v=%d send=%d addr=0x%08x data0=0x%08x\n", i,
                static_cast<int>(e.valid), static_cast<int>(e.send), e.addr,
                e.data[0]);
  }
}

void dump_code_line_snapshot(const char *tag, uint32_t pc) {
  const uint32_t line_base = pc & ~(static_cast<uint32_t>(ICACHE_LINE_SIZE) - 1u);
  const uint32_t start_idx = line_base >> 2;
  const uint32_t word_off = (pc - line_base) >> 2;
  std::printf("[DIFF][ICACHE_LINE][%s] pc=0x%08x line_base=0x%08x word_off=%u\n",
              tag, pc, line_base, word_off);
  for (int row = 0; row < ICACHE_WORD_NUM; row += 4) {
    std::printf(
        "[DIFF][ICACHE_LINE][%s][DUT] +0x%02x: %08x %08x %08x %08x\n", tag,
        row * 4, p_memory[start_idx + row + 0], p_memory[start_idx + row + 1],
        p_memory[start_idx + row + 2], p_memory[start_idx + row + 3]);
    std::printf(
        "[DIFF][ICACHE_LINE][%s][REF] +0x%02x: %08x %08x %08x %08x\n", tag,
        row * 4, ref_cpu.memory[start_idx + row + 0],
        ref_cpu.memory[start_idx + row + 1], ref_cpu.memory[start_idx + row + 2],
        ref_cpu.memory[start_idx + row + 3]);
  }
}

void dump_inst_related_snapshot(uint32_t inst) {
  const uint32_t opcode = inst & 0x7F;
  if (opcode != 0x03 && opcode != 0x23) {
    return;
  }
  const uint32_t rs1 = (inst >> 15) & 0x1F;
  const uint32_t base = dut_cpu.gpr[rs1];
  uint32_t addr = 0;
  if (opcode == 0x03) {
    const uint32_t imm12 = (inst >> 20) & 0xFFF;
    addr = base + sign_extend_12(imm12);
    dump_addr_snapshot("inst_mem_addr", addr);
  } else {
    const uint32_t imm12 =
        (((inst >> 25) & 0x7F) << 5) | ((inst >> 7) & 0x1F);
    addr = base + sign_extend_12(imm12);
    dump_addr_snapshot("inst_store_addr", addr);
  }
}
} // namespace

// relocate the init_difftest function to avoid multiple definition error
void init_difftest(int img_size) {
  ref_cpu.init(0);
  std::memcpy(ref_cpu.memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
              img_size);
  ref_cpu.memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
  ref_cpu.memory[uint32_t(0x0 / 4)] = 0xf1402573;
  ref_cpu.memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  ref_cpu.memory[uint32_t(0x8 / 4)] = 0x800002b7;
  ref_cpu.memory[uint32_t(0xc / 4)] = 0x00028067;
}

void init_diff_ckpt(CPU_state ckpt_state, uint32_t *ckpt_memory) {
  std::cout << "Restore for ref cpu " << std::endl;
  ref_cpu.init(0);
  ref_cpu.state = ckpt_state;
  ref_cpu.privilege = RISCV_MODE_U;

  std::memcpy(ref_cpu.memory, ckpt_memory,
              (uint64_t)PHYSICAL_MEMORY_LENGTH * sizeof(uint32_t));

  // Keep checkpoint bootstrap aligned with RefCpu::exec(): only probe a
  // translation when SATP is active, and do not require the restored PC to be
  // immediately translatable at init time.
  if ((ref_cpu.state.csr[csr_satp] & 0x80000000u) != 0 &&
      ref_cpu.privilege != RISCV_MODE_M) {
    uint32_t p_addr = 0;
    (void)ref_cpu.va2pa(p_addr, ref_cpu.state.pc, 0);
  }
}

void get_state(CPU_state &dut_state, uint8_t &privilege, uint32_t *dut_memory) {
  dut_state = ref_cpu.state;
  privilege = ref_cpu.privilege;
  memcpy(dut_memory, ref_cpu.memory,
         (uint64_t)PHYSICAL_MEMORY_LENGTH * sizeof(uint32_t));
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
              p_memory[dut_cpu.gpr[15] >> 2], ref_cpu.memory[dut_cpu.gpr[15] >> 2]);
  dump_code_line_snapshot("commit_pc", dut_cpu.commit_pc);
#if SIM_LSU_MEM_DEBUG_PRINT
  dump_inst_related_snapshot(dut_cpu.instruction);
  diff_mem_trace::dump_recent();
#endif
  dump_mem_subsystem_snapshot();

  Assert(0 && "Difftest: Register or Memory mismatch detected.");
}

void difftest_skip() {
  ref_cpu.set_dut_page_fault_expect(false, false, false, false);
  ref_cpu.exec();
  for (int i = 0; i < 32; i++) {
    ref_cpu.state.gpr[i] = dut_cpu.gpr[i];
  }
}

void difftest_step(bool check) {
#ifdef CONFIG_BPU
  ref_cpu.set_dut_page_fault_expect(check, dut_cpu.page_fault_inst,
                                    dut_cpu.page_fault_load,
                                    dut_cpu.page_fault_store);
#else
  // Non-BPU mode uses oracle-front flow; disable fault injection here.
  ref_cpu.set_dut_page_fault_expect(false, false, false, false);
#endif
  ref_cpu.exec();
  if (check)
    checkregs();
}
