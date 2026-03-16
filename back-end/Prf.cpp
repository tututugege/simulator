#include "Prf.h"
#include "IO.h"
#include "config.h"
#include "util.h"
#include <cstring>

namespace {
static inline bool is_killed(const MicroOp &uop, const DecBroadcastIO *db) {
  if (!db->mispred) return false;
  return (uop.br_mask & db->br_mask) != 0;
}

inline uint32_t read_operand_with_bypass(
    uint32_t preg, bool src_en, const reg<32> *reg_file, const UopEntry *inst_r,
    const ExePrfIO *exe2prf) {
  if (!src_en) {
    return 0;
  }

  uint32_t data = reg_file[preg];

  // 写回级旁路：优先于寄存器堆读值。
  for (int j = 0; j < ISSUE_WIDTH; j++) {
    if (inst_r[j].valid && inst_r[j].uop.dest_en && inst_r[j].uop.dest_preg == preg) {
      data = inst_r[j].uop.result;
    }
  }

  // Exu 广播旁路：同拍 FU 结果可直接使用。
  for (int k = 0; k < TOTAL_FU_COUNT; k++) {
    if (exe2prf->bypass[k].valid) {
      const auto &bypass_uop = exe2prf->bypass[k].uop;
      if (bypass_uop.dest_en && bypass_uop.dest_preg == preg) {
        data = bypass_uop.result;
        break;
      }
    }
  }

  return data;
}
} // namespace

void Prf::init() {
  for (int i = 0; i < PRF_NUM; i++) {
    reg_file[i] = 0;
    reg_file_1[i] = 0;
  }
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    inst_r[i] = {};
    inst_r_1[i] = {};
  }
}

// ==========================================
// 1. 寄存器读取（发射前）+ 旁路
// ==========================================
void Prf::comb_read() {
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    out.prf2exe->iss_entry[i].valid = in.iss2prf->iss_entry[i].valid;
    if (!out.prf2exe->iss_entry[i].valid)
      continue;

    auto &entry = out.prf2exe->iss_entry[i];
    entry.uop = PrfExeIO::PrfExeUop::from_iss_prf_uop(in.iss2prf->iss_entry[i].uop);
    entry.uop.src1_rdata =
        read_operand_with_bypass(entry.uop.src1_preg, entry.uop.src1_en,
                                 reg_file, inst_r, in.exe2prf);
    entry.uop.src2_rdata =
        read_operand_with_bypass(entry.uop.src2_preg, entry.uop.src2_en,
                                 reg_file, inst_r, in.exe2prf);
  }
}

// ==========================================
// 2. 唤醒逻辑
// ==========================================
void Prf::comb_awake() {
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    out.prf_awake->wake[i].valid = false;
  }

  int awake_idx = 0;
  // 遍历寻找有效的 Load 唤醒 (支持多端口)
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en && is_load(inst_r[i].uop)) {
      bool is_squashed = is_killed(inst_r[i].uop, in.dec_bcast);
      if (is_squashed) {
        continue;
      }

      if (awake_idx < LSU_LOAD_WB_WIDTH) {
        out.prf_awake->wake[awake_idx].valid = true;
        out.prf_awake->wake[awake_idx].preg = inst_r[i].uop.dest_preg;
        awake_idx++;
      }
    }
  }
}

void Prf::comb_complete() {
  // 保留接口：当前 PRF 不承载额外 complete 组合逻辑。
}

// ==========================================
// 3. 写物理寄存器
// ==========================================
void Prf::comb_write() {
  // 将写回级结果写入寄存器堆，x0 始终保持为 0。
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en && inst_r[i].uop.dest_preg != 0) {
      reg_file_1[inst_r[i].uop.dest_preg] = inst_r[i].uop.result;
    }
  }
  reg_file_1[0] = 0;
}

// ==========================================
// 4. 流水寄存器更新
// ==========================================
void Prf::comb_pipeline() {
  bool global_flush = in.rob_bcast->flush;
  mask_t clear = in.dec_bcast->clear_mask;
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (global_flush) {
      inst_r_1[i].valid = false;
    } else if (in.exe2prf->entry[i].valid) {
      inst_r_1[i].valid = true;
      inst_r_1[i].uop = in.exe2prf->entry[i].uop.to_micro_op();
      if (is_killed(inst_r_1[i].uop, in.dec_bcast)) {
        inst_r_1[i].valid = false;
      } else if (inst_r_1[i].valid && clear) {
        inst_r_1[i].uop.br_mask &= ~clear;
      }
    } else {
      inst_r_1[i].valid = false;
    }
  }
}

void Prf::seq() {
  for (int i = 0; i < PRF_NUM; i++) {
    reg_file[i] = reg_file_1[i];
  }
  reg_file[0] = 0;
  reg_file_1[0] = 0;

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
  }
}
