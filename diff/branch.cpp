#include "TOP.h"
#include "frontend.h"
#include <config.h>
#include <cstdint>
#include <cstdlib>
#include <cvt.h>
#include <diff.h>
#include <front_IO.h>
#include <random>
#include <ref.h>

Ref_cpu br_ref;
extern Back_Top back;

bool perfect_fetch_valid[FETCH_WIDTH];
uint32_t perfect_fetch_PC[FETCH_WIDTH];
uint32_t perfect_pred_PC[FETCH_WIDTH];
bool perfect_pred_dir[FETCH_WIDTH];
bool vp_valid[FETCH_WIDTH];
bool vp_mispred[FETCH_WIDTH];

uint32_t vp_src1_rdata[FETCH_WIDTH];
uint32_t vp_src2_rdata[FETCH_WIDTH];

int flush_store_num = 0;
int fetch_num = 0;

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

void perfect_bpu_run(bool br_redirect, bool flush) {
  int i;
  static bool stall = false;
  random_device rd;
  mt19937 gen(rd());
  uniform_int_distribution<int> dis(1, 100);

  if (br_redirect) {
    assert(br_ref.state.pc == back.out.redirect_pc);
    assert(stall);
    stall = false;

    if (flush) {
      for (int i = 0; i < 32; i++) {
        if (br_ref.state.gpr[i] != back.prf.reg_file[back.rename.arch_RAT[i]]) {
          cout << sim_time << endl;
          for (int i = 0; i < 32; i++) {
            cout << br_ref.state.gpr[i] << " "
                 << back.prf.reg_file[back.rename.arch_RAT[i]] << endl;
          }
          exit(1);
        }
      }
    }
  }

  if (stall) {
    for (i = 0; i < FETCH_WIDTH; i++) {
      perfect_fetch_valid[i] = false;
    }

    return;
  }

  fetch_num++;
  for (i = 0; i < FETCH_WIDTH; i++) {
    perfect_fetch_valid[i] = true;
    perfect_fetch_PC[i] = br_ref.state.pc;
#ifdef CONFIG_PERFECT_VP
    bool src1_en, src2_en;
    int src1_areg, src2_areg;
    // 可以值预测的概率
    if (br_ref.vp_decode(src1_en, src2_en, src1_areg, src2_areg) &&
        (rand() % 100 > 30)) {
      vp_valid[i] = true;
      // 值预测正确的概率
      if ((!src1_en && !src2_en) || (rand() % 1000) > 5) {
        // if (1) {
        vp_mispred[i] = false;
        if (src1_en) {
          vp_src1_rdata[i] = br_ref.state.gpr[src1_areg];
        } else {
          vp_src1_rdata[i] = 0;
        }

        if (src2_en) {
          vp_src2_rdata[i] = br_ref.state.gpr[src2_areg];
        } else {
          vp_src2_rdata[i] = 0;
        }
      } else {
        // 错误预测
        vp_mispred[i] = true;
        if (src1_en) {
          vp_src1_rdata[i] = ~br_ref.state.gpr[src1_areg];
        }

        if (src2_en) {
          vp_src2_rdata[i] = br_ref.state.gpr[src2_areg] + 114514;
        }
        stall = true;
        break;
      }
    } else {
      vp_valid[i] = false;
    }

    if (stall) {
      break;
    }
#endif

    br_ref.exec();

    if (br_ref.is_exception || br_ref.is_csr) {
      perfect_pred_dir[i] = false;
      stall = true;
      break;
    }

    if (br_ref.is_br) {
      stall = (rand() % 100 > 65);
      /*stall = false;*/

      if (stall) {
        perfect_pred_dir[i] = !br_ref.br_taken;
        perfect_pred_PC[i] = 0;
      } else {
        perfect_pred_PC[i] = br_ref.state.pc;
        perfect_pred_dir[i] = br_ref.br_taken;
      }

      if (br_ref.br_taken || stall)
        break;
    } else {
      perfect_pred_dir[i] = false;
    }
  }

  for (i++; i < FETCH_WIDTH; i++) {
    perfect_fetch_valid[i] = false;
  }
}
