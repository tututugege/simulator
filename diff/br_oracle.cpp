#include "config.h"
#include "diff.h"
#include "front_IO.h"
#include "ref.h"
#include "util.h"
#include <cstdint>
#include <cstring>

static RefCpu oracle;

uint64_t get_oracle_timer() { return oracle.oracle_timer; }

void init_oracle(int img_size) {
  oracle.init(0);
  std::memcpy(oracle.memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
              img_size * sizeof(uint32_t));
  oracle.memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
  oracle.memory[uint32_t(0x0 / 4)] = 0xf1402573;
  oracle.memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  oracle.memory[uint32_t(0x8 / 4)] = 0x800002b7;
  oracle.memory[uint32_t(0xc / 4)] = 0x00028067;
  oracle.memory[uint32_t(0x00001000 / 4)] = 0x00000297; // auipc t0,0
  oracle.memory[uint32_t(0x00001004 / 4)] = 0x02828613; // addi a2,t0,40
  oracle.memory[uint32_t(0x00001008 / 4)] = 0xf1402573; // csrrs a0,mhartid,zero
  oracle.memory[uint32_t(0x0000100c / 4)] = 0x0202a583; // lw a1,32(t0)
  oracle.memory[uint32_t(0x00001010 / 4)] = 0x0182a283; // lw t0,24(t0)
  oracle.memory[uint32_t(0x00001014 / 4)] = 0x00028067; // jr              t0
  oracle.memory[uint32_t(0x00001018 / 4)] = 0x80000000;
  oracle.memory[uint32_t(0x00001020 / 4)] = 0x8fe00000;
}

void get_oracle(struct front_top_in &in, struct front_top_out &out) {
  int i;
  static bool stall = false;

  if (in.refetch) {
    Assert(!in.is_mispred && "Mispred width oracle");
    Assert(in.refetch_address == oracle.state.pc && "Error refetch PC");

    bool state_mismatch = false;
    // 检查 GPR
    for (int i = 0; i < 32; i++) {
      if (oracle.state.gpr[i] != dut_cpu.gpr[i]) {
        printf("[ORACLE ERROR] GPR[%d] mismatch at sync! Oracle:0x%08x, "
               "DUT:0x%08x\n",
               i, oracle.state.gpr[i], dut_cpu.gpr[i]);
        state_mismatch = true;
      }
    }

    if (state_mismatch) {
      printf("[ORACLE ERROR] State divergence detected at PC 0x%08x "
             "(Refetch to 0x%08x)\n",
             oracle.state.pc, in.refetch_address);
      Assert(0);
    }

    stall = false;
  }

  if (stall) {
    // 添加stall状态日志
    if (LOG) {
      printf("[ORACLE] Still stalled at PC 0x%08x, waiting for flush. Flags: "
             "Ex:%d CSR:%d MMIO:%d\n",
             oracle.state.pc, oracle.is_exception, oracle.is_csr,
             oracle.is_mmio_load);
    }
    out.FIFO_valid = false;
    for (i = 0; i < FETCH_WIDTH; i++) {
      out.inst_valid[i] = false;
    }

    return;
  }

  out.FIFO_valid = true;
  for (i = 0; i < FETCH_WIDTH; i++) {
    out.inst_valid[i] = true;
    out.pc[i] = oracle.state.pc;
    out.page_fault_inst[i] = false;

    oracle.exec();
    out.instructions[i] = oracle.Instruction;

    if (oracle.is_exception || oracle.is_csr || oracle.is_mmio_load) {
      if (LOG) {
        printf("[ORACLE] Stalling at PC 0x%08x, Inst 0x%08x (Ex:%d, CSR:%d, "
               "MMIO:%d)\n",
               out.pc[i], out.instructions[i], oracle.is_exception,
               oracle.is_csr, oracle.is_mmio_load);
      }
      out.predict_dir[i] = false;
      stall = true;

      if (oracle.page_fault_inst) {
        out.page_fault_inst[i] = true;
      }
      break;
    }

    if (oracle.is_br) {
      if (stall) {
        out.predict_dir[i] = !oracle.br_taken;
        out.predict_next_fetch_address = 0;
      } else {
        out.predict_dir[i] = oracle.br_taken;
        out.predict_next_fetch_address = oracle.state.pc;
      }

      if (oracle.br_taken || stall)
        break;
    } else {
      out.predict_dir[i] = false;
    }
  }

  for (i++; i < FETCH_WIDTH; i++) {
    out.inst_valid[i] = false;
  }
}
