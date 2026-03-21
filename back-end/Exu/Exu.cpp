#include "Exu.h"
#include "FPU.h"
#include "config.h"

static inline bool is_br_killed(const MicroOp &uop, const DecBroadcastIO *db) {
  if (!db->mispred) return false;
  return (uop.br_mask & db->br_mask) != 0;
}

Exu::Exu(SimContext *ctx) : ctx(ctx) {
  // 可以在这里或 init 创建 backend
}

Exu::~Exu() {
  for (auto fu : units) {
    delete fu;
  }
}

void Exu::init() {
  int alu_cnt = 0;
  int bru_cnt = 0;
  int agu_cnt = 0;
  int sdu_cnt = 0;

  port_mappings.resize(ISSUE_WIDTH);

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    uint64_t mask = GLOBAL_ISSUE_PORT_CONFIG[i].support_mask;

    // 1. MUL (Priority for writeback)
    if (mask & OP_MASK_MUL) {
      auto mul = new MulUnit("MUL", i, MUL_MAX_LATENCY);
      units.push_back(mul);
      port_mappings[i].entries.push_back({mul, OP_MASK_MUL});
    }

    // 2. DIV
    if (mask & OP_MASK_DIV) {
      auto div = new DivUnit("DIV", i, DIV_MAX_LATENCY);
      units.push_back(div);
      port_mappings[i].entries.push_back({div, OP_MASK_DIV});
    }

    // 3. AGU (Load)
    if (mask & OP_MASK_LD) {
      auto ldu = new AguUnit("AGU_LD", i, out.exe2lsu, agu_cnt++);
      units.push_back(ldu);
      port_mappings[i].entries.push_back({ldu, OP_MASK_LD});
    }

    // 4. AGU (STA)
    if (mask & OP_MASK_STA) {
      auto sta = new AguUnit("AGU_STA", i, out.exe2lsu, agu_cnt++);
      units.push_back(sta);
      port_mappings[i].entries.push_back({sta, OP_MASK_STA});
    }

    // 5. SDU (STD)
    if (mask & OP_MASK_STD) {
      auto sdu = new SduUnit("SDU", i, out.exe2lsu, sdu_cnt++);
      units.push_back(sdu);
      port_mappings[i].entries.push_back({sdu, OP_MASK_STD});
    }

    // 6. BRU
    if (mask & OP_MASK_BR) {
      auto bru = new BruUnit("BRU", i, in.ftq_pc_resp, ALU_NUM + bru_cnt++);
      units.push_back(bru);
      port_mappings[i].entries.push_back({bru, OP_MASK_BR});
    }

    // 7. ALU
    if (mask & OP_MASK_ALU) {
      std::string alu_name = "ALU" + std::to_string(alu_cnt++);
      auto alu = new AluUnit(alu_name, i, in.ftq_pc_resp, alu_cnt - 1);
      units.push_back(alu);
      port_mappings[i].entries.push_back({alu, OP_MASK_ALU});
    }

    // 8. CSR
    if (mask & OP_MASK_CSR) {
      auto csr = new CsrUnit("CSR", i, out.exe2csr, in.csr2exe);
      units.push_back(csr);
      port_mappings[i].entries.push_back({csr, OP_MASK_CSR});
    }

    // 9. FP
    if (mask & OP_MASK_FP) {
      auto fpu = new FPURtl("FPU", i, 1);
      units.push_back(fpu);
      FuEntry fpu_entry;
      fpu_entry.fu = fpu;
      fpu_entry.support_mask = OP_MASK_FP;
      port_mappings[i].entries.push_back(fpu_entry);
    }
  }

  // 初始化流水线寄存器
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    inst_r[i].valid = false;
    inst_r_1[i].valid = false;
  }
}

void Exu::comb_ftq_pc_req() {
  for (auto &req : out.ftq_pc_req->req) {
    req = {};
  }

  for (int p_idx = 0; p_idx < ISSUE_WIDTH; p_idx++) {
    if (!inst_r[p_idx].valid) {
      continue;
    }

    const MicroOp &uop = inst_r[p_idx].uop;
    if (in.rob_bcast->flush || is_br_killed(uop, in.dec_bcast)) {
      continue;
    }

    uint64_t req_bit = (1ULL << uop.op);
    if (p_idx >= IQ_ALU_PORT_BASE && p_idx < IQ_ALU_PORT_BASE + ALU_NUM &&
        (req_bit & OP_MASK_ALU) && uop.src1_is_pc) {
      int slot = p_idx - IQ_ALU_PORT_BASE;
      out.ftq_pc_req->req[slot].valid = true;
      out.ftq_pc_req->req[slot].ftq_idx = uop.ftq_idx;
      out.ftq_pc_req->req[slot].ftq_offset = uop.ftq_offset;
    } else if (p_idx >= IQ_BR_PORT_BASE &&
               p_idx < IQ_BR_PORT_BASE + BRU_NUM && (req_bit & OP_MASK_BR)) {
      int slot = ALU_NUM + (p_idx - IQ_BR_PORT_BASE);
      out.ftq_pc_req->req[slot].valid = true;
      out.ftq_pc_req->req[slot].ftq_idx = uop.ftq_idx;
      out.ftq_pc_req->req[slot].ftq_offset = uop.ftq_offset;
    }
  }
}

// ==========================================
// 1. 组合逻辑：生成反压与 Ready 信号
// ==========================================
//
void Exu::comb_ready() {
  // 异常状态下（Flush/Mispred），Exu 停止接收新指令，防止脏数据进入
  if (in.rob_bcast->flush) {
    for (int i = 0; i < ISSUE_WIDTH; i++) {
      out.exe2iss->ready[i] = false;
      out.exe2iss->fu_ready_mask[i] = 0;
    }
    return;
  }

  for (int i = 0; i < ISSUE_WIDTH; i++) {

    bool is_killed = false;
    if (inst_r[i].valid && is_br_killed(inst_r[i].uop, in.dec_bcast)) {
      is_killed = true;
    }

    out.exe2iss->ready[i] = !inst_r[i].valid || is_killed || (!issue_stall[i]);

    // B. 检查 FU 详细状态 (Credit)
    uint64_t mask = 0;
    for (auto &entry : port_mappings[i].entries) {
      // 无参 can_accept: 只需要看 FU 内部是否满/忙
      if (entry.fu->can_accept()) {
        mask |= entry.support_mask;
      }
    }

    out.exe2iss->fu_ready_mask[i] = mask;
  }
}

void Exu::comb_to_csr() {
  out.exe2csr->we = false;
  out.exe2csr->re = false;

  if (inst_r[0].valid && inst_r[0].uop.op == UOP_CSR && !in.rob_bcast->flush) {
    const auto &uop = inst_r[0].uop;
    bool is_csrrw_family = (uop.func3 == 0b001) || (uop.func3 == 0b101);
    bool src_is_zero = (uop.imm == 0);
    bool rd_is_zero = (uop.dest_preg == 0);

    out.exe2csr->we = is_csrrw_family || !src_is_zero;
    out.exe2csr->re = !is_csrrw_family || !rd_is_zero;

    out.exe2csr->idx = uop.csr_idx;
    out.exe2csr->wcmd = uop.func3 & 0b11;
    if (uop.src2_is_imm) {
      out.exe2csr->wdata = uop.imm;
    } else {
      out.exe2csr->wdata = uop.src1_rdata;
    }
  }
}

// ==========================================
// 2. 组合逻辑：流水线控制 (Flush + Latch + Filter)
// ==========================================

void Exu::comb_pipeline() {
  // 1. 全局 Flush (最高优先级)
  if (in.rob_bcast->flush) {
    for (int i = 0; i < ISSUE_WIDTH; i++) {
      inst_r_1[i].valid = false;
    }
    for (auto fu : units)
      fu->flush(static_cast<wire<BR_MASK_WIDTH>>(-1));
    return;
  }

  wire<BR_MASK_WIDTH> clear = in.dec_bcast->clear_mask;

  // 2. 分支误预测选择性冲刷（先 flush，再 clear）
  if (in.dec_bcast->mispred) {
    wire<BR_MASK_WIDTH> mask = in.dec_bcast->br_mask;
    for (auto fu : units)
      fu->flush(mask);
  }

  // 3. 清除已解析分支的 br_mask bit（在 flush 之后）
  if (clear) {
    for (auto fu : units)
      fu->clear_br(clear);
  }

  // 4. 主循环：计算下一拍流水寄存器
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    bool current_killed = false;
    if (inst_r[i].valid && is_br_killed(inst_r[i].uop, in.dec_bcast)) {
      current_killed = true;
    }

    if (current_killed) {
      inst_r_1[i].valid = false;
      continue;
    }

    if (inst_r[i].valid && issue_stall[i]) {
      inst_r_1[i] = inst_r[i];
    } else if (in.prf2exe->iss_entry[i].valid) {
      inst_r_1[i].valid = true;
      inst_r_1[i].uop = in.prf2exe->iss_entry[i].uop.to_micro_op();
    } else {
      inst_r_1[i].valid = false;
    }

    // 对存活条目清除已解析分支的 bit
    if (inst_r_1[i].valid && clear) {
      inst_r_1[i].uop.br_mask &= ~clear;
    }
  }
}

// ==========================================
// 3. 组合逻辑：执行与写回
// ==========================================
void Exu::comb_exec() {

  for (int i = 0; i < ISSUE_WIDTH; i++)
    issue_stall[i] = false;

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    if (inst_r[i].valid) {
      bool is_killed = false;
      if (in.rob_bcast->flush)
        is_killed = true;
      if (is_br_killed(inst_r[i].uop, in.dec_bcast)) {
        is_killed = true;
      }

      if (is_killed) {
        continue;
      }

      AbstractFU *target_fu = nullptr;
      uint64_t req_bit = (1ULL << inst_r[i].uop.op);

      // 查表路由：找到支持该 OpCode 的 FU
      for (auto &entry : port_mappings[i].entries) {
        if (entry.support_mask & req_bit) {
          target_fu = entry.fu;
          break;
        }
      }

      if (target_fu && target_fu->can_accept()) {
        target_fu->accept(inst_r[i].uop);
      } else {
        issue_stall[i] = true;
      }
    }
  }

  //  valid 信号清空
  for (int i = 0; i < TOTAL_FU_COUNT; i++) {
    out.exe2prf->bypass[i].valid = false;
  }

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    out.exe2prf->entry[i].valid = false;
    out.exu2rob->entry[i].valid = false;
  }

  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    out.exe2lsu->agu_req[i].valid = false;
  }

  for (int i = 0; i < LSU_SDU_COUNT; i++) {
    out.exe2lsu->sdu_req[i].valid = false;
  }

  // 旁路逻辑
  int fu_global_idx = 0; // 用于给每个 FU 编号
  // 遍历所有 FU 单元
  for (auto fu : units) {
    MicroOp *res = fu->get_finished_uop(); // 看看这个 FU 算完没

    if (res) {
      // ✅ 无论是否能写回，先广播出去给 Bypass 用！
      out.exe2prf->bypass[fu_global_idx].uop =
          ExePrfIO::ExePrfWbUop::from_micro_op(*res);
      out.exe2prf->bypass[fu_global_idx].valid = true;
    }

    fu_global_idx++;
  }

  // ==========================================
  // 二、写回逻辑 (Writeback) - 终极解耦重构
  // ==========================================

  // 结果收集容器
  UopEntry int_res[ALU_NUM] = {};
  UopEntry br_res[BRU_NUM] = {};

  // 1. 全局端口扫描与立即分发 (Total Port Scan)
  for (int p_idx = 0; p_idx < ISSUE_WIDTH; p_idx++) {
    AbstractFU *winner_fu = nullptr;
    MicroOp *u = nullptr;

    // 仲裁：选择该端口的胜出 FU
    for (auto &map_entry : port_mappings[p_idx].entries) {
      if (MicroOp *res = map_entry.fu->get_finished_uop()) {
        u = res;
        winner_fu = map_entry.fu;
        break;
      }
    }
    if (!u) continue;

    bool flushed = in.rob_bcast->flush || is_br_killed(*u, in.dec_bcast);
    // A. 立即驱动 ROB (非访存指令在此完成)
    // 注意：LOAD/STA 的完成通报由 LSU 回调阶段处理
    if (!flushed && u->op != UOP_LOAD && u->op != UOP_STA) {
      out.exu2rob->entry[p_idx].valid = true;
      out.exu2rob->entry[p_idx].uop = ExuRobIO::ExuRobUop::from_micro_op(*u);
    }

    // B. 立即外发 LSU 请求
    if (!flushed) {
      int lsu_idx = winner_fu->get_lsu_port_id();
      if (u->op == UOP_STA || u->op == UOP_LOAD) {
        out.exe2lsu->agu_req[lsu_idx].valid = true;
        out.exe2lsu->agu_req[lsu_idx].uop =
            ExeLsuIO::ExeLsuReqUop::from_micro_op(*u);
      } else if (u->op == UOP_STD) {
        out.exe2lsu->sdu_req[lsu_idx].valid = true;
        out.exe2lsu->sdu_req[lsu_idx].uop =
            ExeLsuIO::ExeLsuReqUop::from_micro_op(*u);
      }
    }

    // C. 收集分类结果
    // 收集 INT 写回信息 (仅限需要写回目的寄存器的 ALU 指令)
    if (p_idx >= IQ_ALU_PORT_BASE && p_idx < IQ_ALU_PORT_BASE + ALU_NUM && u->dest_en) {
      int idx = p_idx - IQ_ALU_PORT_BASE;
      int_res[idx].uop = *u;
      int_res[idx].valid = true;
    }
    // 收集 BR 信息 (用于分支仲裁)
    // 仅保留存活分支结果；被 flush/kill 的分支不应继续驱动恢复广播。
    if (!flushed && p_idx >= IQ_BR_PORT_BASE &&
        p_idx < IQ_BR_PORT_BASE + BRU_NUM) {
      int idx = p_idx - IQ_BR_PORT_BASE;
      br_res[idx].uop = *u;
      br_res[idx].valid = true;
    }

    winner_fu->pop_finished();
  }

  // 2. 选择性写回分发 (Writeback Distribution)
  
  // A. 处理常规计算写回 (仅扫描 INT 结果)
  for (int i = 0; i < ALU_NUM; i++) {
    if (int_res[i].valid) {
      int p_idx = IQ_ALU_PORT_BASE + i;
      out.exe2prf->entry[p_idx].valid = true;
      out.exe2prf->entry[p_idx].uop =
          ExePrfIO::ExePrfWbUop::from_micro_op(int_res[i].uop);
    }
  }

  // B. 处理 LSU 回调写回 (LOAD/STA 数据回流)
  // 此处同时驱动 PRF 和 ROB
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (in.lsu2exe->wb_req[i].valid) {
      int p_idx = IQ_LD_PORT_BASE + i;
      MicroOp u = in.lsu2exe->wb_req[i].uop.to_micro_op();
      bool flushed = in.rob_bcast->flush || is_br_killed(u, in.dec_bcast);
      Assert(!out.exe2prf->entry[p_idx].valid);
      out.exe2prf->entry[p_idx].valid = true;
      out.exe2prf->entry[p_idx].uop = ExePrfIO::ExePrfWbUop::from_micro_op(u);
      if (!flushed) {
        out.exu2rob->entry[p_idx].valid = true;
        out.exu2rob->entry[p_idx].uop = ExuRobIO::ExuRobUop::from_micro_op(u);
      }
    }
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (in.lsu2exe->sta_wb_req[i].valid) {
      int p_idx = IQ_STA_PORT_BASE + i;
      MicroOp u = in.lsu2exe->sta_wb_req[i].uop.to_micro_op();
      bool flushed = in.rob_bcast->flush || is_br_killed(u, in.dec_bcast);
      Assert(!out.exe2prf->entry[p_idx].valid);
      out.exe2prf->entry[p_idx].valid = true;
      out.exe2prf->entry[p_idx].uop = ExePrfIO::ExePrfWbUop::from_micro_op(u);
      if (!flushed) {
        out.exu2rob->entry[p_idx].valid = true;
        out.exu2rob->entry[p_idx].uop = ExuRobIO::ExuRobUop::from_micro_op(u);
      }
    }
  }

  // ==========================================
  // 三、分支误预测仲裁 (Early Recovery)
  // ==========================================
  bool mispred = false;
  MicroOp *mispred_uop = nullptr;
  wire<BR_MASK_WIDTH> clear_mask = 0;

  for (int i = 0; i < BRU_NUM; i++) {
    if (br_res[i].valid) {
      // 所有已解析的 branch 贡献 clear_mask
      clear_mask |= (wire<BR_MASK_WIDTH>(1) << br_res[i].uop.br_id);

      if (br_res[i].uop.mispred) {
        if (!mispred) {
          mispred = true;
          mispred_uop = &br_res[i].uop;
        } else if (cmp_inst_age(*mispred_uop, br_res[i].uop)) {
          mispred_uop = &br_res[i].uop;
        }
      }
    }
  }

  // clear_mask 统一经由 IDU 广播到各模块，EXU 不再本地同拍清除
  out.exu2id->clear_mask = clear_mask;
  out.exu2id->mispred = mispred;
  if (mispred) {
    out.exu2id->redirect_pc = mispred_uop->diag_val;
    out.exu2id->redirect_rob_idx = mispred_uop->rob_idx;
    out.exu2id->br_id = mispred_uop->br_id;
    out.exu2id->ftq_idx = mispred_uop->ftq_idx;
  } else {
    out.exu2id->redirect_pc = 0;
    out.exu2id->redirect_rob_idx = 0;
    out.exu2id->br_id = 0;
    out.exu2id->ftq_idx = 0;
  }
}

// ==========================================
// 时序逻辑
// ==========================================
void Exu::seq() {
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    inst_r[i] = inst_r_1[i];
  }

  for (auto fu : units) {
    fu->tick();
  }
}
