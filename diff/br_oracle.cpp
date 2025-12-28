#include "oracle.h"
#include "ref.h"
static RefCpu oracle;

#include <config.h>
#include <cvt.h>
#include <diff.h>
#include <front_IO.h>
#include <random>
#include <ref.h>

#define BP_ACCURACY 95

void init_oracle(int img_size) {
  oracle.init(0);
  memcpy(oracle.memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
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
  random_device rd;
  mt19937 gen(rd());
  uniform_int_distribution<int> dis(1, 100);

  if (in.refetch) {
    stall = false;
    assert(in.refetch_address == oracle.state.pc);
  }

  if (stall) {
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

    if (oracle.is_exception || oracle.is_csr) {
      out.predict_dir[i] = false;
      stall = true;

      if (oracle.page_fault_inst) {
        out.page_fault_inst[i] = true;
      }
      break;
    }

    if (oracle.is_br) {
      // stall = (rand() % 100 >= BP_ACCURACY);

      stall = false;
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
