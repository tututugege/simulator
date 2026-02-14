#include "config.h"
#include <IO.h>
#include <Prf.h>
#include <cstring>
#include <iostream>
#include <util.h>

void Prf::init() {}

// ==========================================
// 1. 分支检查 (Writeback 阶段)
// ==========================================

// ==========================================
// 2. 寄存器读取 (Dispatch 阶段) + Bypass
// ==========================================
void Prf::comb_read() {
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    // 1. 直接传递 Issue 内容
    out.prf2exe->iss_entry[i] = in.iss2prf->iss_entry[i];
    UopEntry *entry = &out.prf2exe->iss_entry[i];

    if (!entry->valid)
      continue;

    // === SRC1 读取与旁路 ===
    if (entry->uop.src1_en) {
      // A. 读物理寄存器堆 (Register File)
      entry->uop.src1_rdata = reg_file[entry->uop.src1_preg];

      // B. 旁路：检查 Writeback 阶段 (inst_r)
      //    (这是上一拍刚流进来的指令，正在准备写 RegFile)
      for (int j = 0; j < ISSUE_WIDTH; j++) {
        if (inst_r[j].valid && inst_r[j].uop.dest_en &&
            inst_r[j].uop.dest_preg == entry->uop.src1_preg) {
          entry->uop.src1_rdata = inst_r[j].uop.result;
        }
      }

      // C. ✨ 旁路修正：检查所有 FU 的广播 (Exe Bypass) ✨
      // 只要 FU 算完了，不管它在不在写回总线上，数据都是可用的！
      for (int k = 0; k < TOTAL_FU_COUNT; k++) {
        if (in.exe2prf->bypass[k].valid) { // 如果这个 FU 有结果
          const auto &bypass_uop = in.exe2prf->bypass[k].uop;
          if (bypass_uop.dest_en &&
              bypass_uop.dest_preg == entry->uop.src1_preg) {

            entry->uop.src1_rdata = bypass_uop.result;
            // 找到了就可以 break 吗？
            // 通常越年轻（越晚生成）的数据越新，但在同一拍 Exu 里
            // 不会出现两个 FU 写同一个 Preg 的情况（重命名保证了唯一性）
            // 所以找到一个就可以 break
            break;
          }
        }
      }
    } else {
      entry->uop.src1_rdata = 0;
    }

    // === SRC2 读取与旁路 (逻辑同上) ===
    if (entry->uop.src2_en) {
      entry->uop.src2_rdata = reg_file[entry->uop.src2_preg];

      for (int j = 0; j < ISSUE_WIDTH; j++) {
        if (inst_r[j].valid && inst_r[j].uop.dest_en &&
            inst_r[j].uop.dest_preg == entry->uop.src2_preg) {
          entry->uop.src2_rdata = inst_r[j].uop.result;
        }
      }

      for (int k = 0; k < TOTAL_FU_COUNT; k++) {
        if (in.exe2prf->bypass[k].valid) { // 如果这个 FU 有结果
          const auto &bypass_uop = in.exe2prf->bypass[k].uop;
          if (bypass_uop.dest_en &&
              bypass_uop.dest_preg == entry->uop.src2_preg) {

            entry->uop.src2_rdata = bypass_uop.result;
            break;
          }
        }
      }
    } else {
      entry->uop.src2_rdata = 0;
    }
  }
}

// ==========================================
// 3. 提交辅助 (Forward to ROB)
// ==========================================
void Prf::comb_complete() {}

// ==========================================
// 4. 唤醒逻辑 (Wakeup)
// ==========================================
void Prf::comb_awake() {
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    out.prf_awake->wake[i].valid = false;
  }

  int awake_idx = 0;
  // 遍历寻找有效的 Load 唤醒 (支持多端口)
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en && is_load(inst_r[i].uop)) {
      // FIX: 跳过被 Mispred Squash 的指令
      bool is_squashed = in.dec_bcast->mispred &&
                         (in.dec_bcast->br_mask & (1ULL << inst_r[i].uop.tag));
      if (is_squashed) {
        continue; // 不发送被 Squash 指令的 Wakeup
      }

      if (awake_idx < LSU_LOAD_WB_WIDTH) {
        out.prf_awake->wake[awake_idx].valid = true;
        out.prf_awake->wake[awake_idx].preg = inst_r[i].uop.dest_preg;
        awake_idx++;
      }
    }
  }
}

void Prf::comb_branch() {
  if (in.dec_bcast->mispred) {
    for (int i = 0; i < ISSUE_WIDTH; i++) {
      if (inst_r[i].valid &&
          (in.dec_bcast->br_mask & (1ULL << inst_r[i].uop.tag))) {
        inst_r_1[i].valid = false;
      }
    }
  }
}

void Prf::comb_flush() {
  if (in.rob_bcast->flush) {
    for (int i = 0; i < ISSUE_WIDTH; i++) {
      inst_r_1[i].valid = false;
    }
  }
}

// ==========================================
// 5. 写物理寄存器 (Write Register File)
// ==========================================
void Prf::comb_write() {
  // 将 Writeback 阶段 (inst_r) 的结果写入 RegFile
  // 这里的数据已经是处理好（对齐、扩展）的最终结果
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (inst_r[i].valid && inst_r[i].uop.dest_en) {
      reg_file_1[inst_r[i].uop.dest_preg] = inst_r[i].uop.result;
    }
  }
}

// ==========================================
// 6. 流水线寄存器更新 (Latch Logic)
// ==========================================
void Prf::comb_pipeline() {

  // 从 Exu 接收结果 (Exec -> WB)
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (in.exe2prf->entry[i].valid) {
      inst_r_1[i] = in.exe2prf->entry[i];
    } else {
      inst_r_1[i].valid = false;
    }
  }
}

void Prf::seq() {
  for (int i = 0; i < PRF_NUM; i++) {
    reg_file[i] = reg_file_1[i];
  }

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
  }
}
