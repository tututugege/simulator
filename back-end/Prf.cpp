#include "Prf.h"
#include "IO.h"
#include "config.h"
#include "util.h"
#include <cstring>

namespace {
static inline bool is_killed(const ExePrfIO::ExePrfWbUop &uop,
                             const DecBroadcastIO *db) {
  if (!db->mispred) return false;
  return (uop.br_mask & db->br_mask) != 0;
}

inline uint32_t read_operand_with_bypass(
    uint32_t preg, bool src_en, const reg<32> *reg_file,
    const ExePrfIO::ExePrfEntry *inst_r, const ExePrfIO *exe2prf) {
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

static inline bool is_load_wb(const ExePrfIO::ExePrfWbUop &uop) {
  return decode_uop_type(uop.op) == UOP_LOAD;
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

// 功能：复制 PRF 当前状态到本拍工作副本（*_1）。
// 输入依赖：reg_file[]、inst_r[]。
// 输出更新：reg_file_1[]、inst_r_1[]。
// 约束：仅状态镜像，不进行读/写/旁路决策。
void Prf::comb_begin() {
  for (int i = 0; i < PRF_NUM; i++) {
    reg_file_1[i] = reg_file[i];
  }
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    inst_r_1[i] = inst_r[i];
  }
}

// 功能：为本拍发射条目读取源操作数，并应用写回级与 EXU 广播旁路。
// 输入依赖：in.iss2prf->iss_entry[]、reg_file[]、inst_r[]、in.exe2prf->bypass[]。
// 输出更新：out.prf2exe->iss_entry[]（含 src1_rdata/src2_rdata）。
// 约束：src_en=0 时读值强制为 0；旁路优先级高于寄存器堆读值。
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

// 功能：从写回流水寄存器中提取可见的 Load 完成事件并生成唤醒广播。
// 输入依赖：inst_r[]、in.dec_bcast（mispred/br_mask）。
// 输出更新：out.prf_awake->wake[]。
// 约束：仅 Load 且未被 squash 的目的寄存器参与唤醒；端口数受 LSU_LOAD_WB_WIDTH 限制。
void Prf::comb_awake() {
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    out.prf_awake->wake[i].valid = false;
  }

  int awake_idx = 0;
  // 遍历寻找有效的 Load 唤醒 (支持多端口)
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en && is_load_wb(inst_r[i].uop)) {
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

// 功能：保留接口（当前 PRF 无额外 complete 组合逻辑）。
// 输入依赖：无。
// 输出更新：无。
// 约束：函数存在用于保持模块阶段结构一致性。
void Prf::comb_complete() {
  // 保留接口：当前 PRF 不承载额外 complete 组合逻辑。
}

// 功能：将写回级结果写入物理寄存器堆下一拍副本。
// 输入依赖：inst_r[]（valid/dest_en/dest_preg/result）。
// 输出更新：reg_file_1[]。
// 约束：x0 不可写，reg_file_1[0] 始终保持 0。
void Prf::comb_write() {
  // 将写回级结果写入寄存器堆，x0 始终保持为 0。
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en && inst_r[i].uop.dest_preg != 0) {
      reg_file_1[inst_r[i].uop.dest_preg] = inst_r[i].uop.result;
    }
  }
  reg_file_1[0] = 0;
}

// 功能：推进 PRF 写回流水寄存器，并处理 flush/mispred/clear_mask。
// 输入依赖：in.rob_bcast->flush、in.exe2prf->entry[]、in.dec_bcast->{mispred,br_mask,clear_mask}。
// 输出更新：inst_r_1[]（valid/uop 及 br_mask 清理）。
// 约束：flush 优先清空；mispred 命中分支掩码的条目被杀；存活条目需清除 clear_mask。
void Prf::comb_pipeline() {
  bool global_flush = in.rob_bcast->flush;
  wire<BR_MASK_WIDTH> clear = in.dec_bcast->clear_mask;
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (global_flush) {
      inst_r_1[i].valid = false;
    } else if (in.exe2prf->entry[i].valid) {
      inst_r_1[i].valid = true;
      inst_r_1[i].uop = in.exe2prf->entry[i].uop;
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
