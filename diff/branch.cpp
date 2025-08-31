#include "frontend.h"
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <front_IO.h>
#include <random>
#include <ref.h>

Ref_cpu br_ref;

bool perfect_fetch_valid[FETCH_WIDTH];
uint32_t perfect_fetch_PC[FETCH_WIDTH];
uint32_t perfect_pred_PC[FETCH_WIDTH];
bool perfect_pred_dir[FETCH_WIDTH];

void perfect_bpu_init(int img_size) {
  br_ref.init(0);
  memcpy(br_ref.memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
         img_size * sizeof(uint32_t));
  br_ref.memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
  br_ref.memory[uint32_t(0x0 / 4)] = 0xf1402573;
  br_ref.memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  br_ref.memory[uint32_t(0x8 / 4)] = 0x800002b7;
  br_ref.memory[uint32_t(0xc / 4)] = 0x00028067;
  br_ref.memory[uint32_t(0x00001000 / 4)] = 0x00000297; // auipc           t0,0
  br_ref.memory[uint32_t(0x00001004 / 4)] = 0x02828613; // addi a2,t0,40
  br_ref.memory[uint32_t(0x00001008 / 4)] = 0xf1402573; // csrrs a0,mhartid,zero
  br_ref.memory[uint32_t(0x0000100c / 4)] = 0x0202a583; // lw a1,32(t0)
  br_ref.memory[uint32_t(0x00001010 / 4)] = 0x0182a283; // lw t0,24(t0)
  br_ref.memory[uint32_t(0x00001014 / 4)] = 0x00028067; // jr              t0
  br_ref.memory[uint32_t(0x00001018 / 4)] = 0x80000000;
  br_ref.memory[uint32_t(0x00001020 / 4)] = 0x8fe00000;
}

void perfect_bpu_run(bool redirect) {
  int i;
  static bool mispred = false;
  random_device rd;
  mt19937 gen(rd());
  uniform_int_distribution<int> dis(1, 100);

  if (redirect)
    mispred = false;

  if (mispred) {
    for (i = 0; i < FETCH_WIDTH; i++) {
      perfect_fetch_valid[i] = false;
    }
    return;
  }

  for (i = 0; i < FETCH_WIDTH; i++) {
    perfect_fetch_valid[i] = true;
    perfect_fetch_PC[i] = br_ref.state.pc;
    br_ref.exec();
    if (br_ref.is_br) {
      mispred = (dis(gen) > 90);

      if (mispred) {
        perfect_pred_dir[i] = !br_ref.br_taken;
      } else {
        perfect_pred_PC[i] = br_ref.state.pc;
        perfect_pred_dir[i] = br_ref.br_taken;
      }

      if (br_ref.br_taken || mispred)
        break;
    } else {
      perfect_pred_dir[i] = false;
    }
  }

  for (i++; i < FETCH_WIDTH; i++) {
    perfect_fetch_valid[i] = false;
  }
}
