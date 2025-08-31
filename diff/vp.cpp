#include "frontend.h"
#include <config.h>
#include <cstdint>
#include <cvt.h>
#include <diff.h>
#include <front_IO.h>
#include <random>
#include <ref.h>

Ref_cpu vp_ref;

bool vp_valid[FETCH_WIDTH];
uint32_t vp_src1_rdata[FETCH_WIDTH];
uint32_t vp_src2_rdata[FETCH_WIDTH];

void perfect_vp_init(int img_size) {
  vp_ref.init(0);
  memcpy(vp_ref.memory + 0x80000000 / 4, p_memory + 0x80000000 / 4,
         img_size * sizeof(uint32_t));
  vp_ref.memory[0x10000004 / 4] = 0x00006000; // 和进入 OpenSBI 相关
  vp_ref.memory[uint32_t(0x0 / 4)] = 0xf1402573;
  vp_ref.memory[uint32_t(0x4 / 4)] = 0x83e005b7;
  vp_ref.memory[uint32_t(0x8 / 4)] = 0x800002b7;
  vp_ref.memory[uint32_t(0xc / 4)] = 0x00028067;
  vp_ref.memory[uint32_t(0x00001000 / 4)] = 0x00000297; // auipc           t0,0
  vp_ref.memory[uint32_t(0x00001004 / 4)] = 0x02828613; // addi a2,t0,40
  vp_ref.memory[uint32_t(0x00001008 / 4)] = 0xf1402573; // csrrs a0,mhartid,zero
  vp_ref.memory[uint32_t(0x0000100c / 4)] = 0x0202a583; // lw a1,32(t0)
  vp_ref.memory[uint32_t(0x00001010 / 4)] = 0x0182a283; // lw t0,24(t0)
  vp_ref.memory[uint32_t(0x00001014 / 4)] = 0x00028067; // jr              t0
  vp_ref.memory[uint32_t(0x00001018 / 4)] = 0x80000000;
  vp_ref.memory[uint32_t(0x00001020 / 4)] = 0x8fe00000;
}

static bool stop = false;

void perfect_vp_continue() { stop = false; }
void perfect_vp_run(bool *fetch_valid) {
  int i;
  random_device rd;
  mt19937 gen(rd());
  uniform_int_distribution<int> dis(1, 100);

  if (stop) {
    for (i = 0; i < FETCH_WIDTH; i++) {
      vp_valid[i] = false;
    }
    return;
  }

  for (i = 0; i < FETCH_WIDTH; i++) {
    // 解码判断是否能预测
    bool src1_en, src2_en;
    int src1_areg, src2_areg;
    // 可以值预测的概率
    if (vp_ref.vp_decode(src1_en, src2_en, src1_areg, src2_areg) &&
        fetch_valid[i]) {
      vp_valid[i] = true;
      // 值预测正确的概率
      if (dis(gen) > 10) {
        if (src1_en) {
          vp_src1_rdata[i] = vp_ref.state.gpr[src1_areg];
        } else {
          vp_src1_rdata[i] = 0;
        }

        if (src2_en) {
          vp_src2_rdata[i] = vp_ref.state.gpr[src2_areg];
        } else {
          vp_src2_rdata[i] = 0;
        }
      } else {
        // 错误预测
        if (src1_en) {
          vp_src1_rdata[i] = ~vp_ref.state.gpr[src1_areg];
        }

        if (src2_en) {
          vp_src2_rdata[i] = vp_ref.state.gpr[src2_areg] + 114514;
        }
        stop = true;
        break;
      }
    } else
      vp_valid[i] = false;

    if (fetch_valid[i])
      vp_ref.exec();
  }

  for (i++; i < FETCH_WIDTH; i++) {
    vp_valid[i] = false;
  }
}
