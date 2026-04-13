#include "Exu.h"
#include "FPU.h"
#include "config.h"

static inline bool is_br_killed(const ExuInst &uop, const DecBroadcastIO *db) {
  if (!db->mispred)
    return false;
  return (uop.br_mask & db->br_mask) != 0;
}

static inline bool is_br_killed(const MicroOp &uop, const DecBroadcastIO *db) {
  if (!db->mispred)
    return false;
  return (uop.br_mask & db->br_mask) != 0;
}

static inline bool cmp_inst_age(const ExuInst &inst1, const ExuInst &inst2) {
  if (inst1.rob_flag == inst2.rob_flag) {
    return inst1.rob_idx > inst2.rob_idx;
  }
  return inst1.rob_idx < inst2.rob_idx;
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
      auto fpu = new FPUSoftfloat("FPU", i, 10);
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

/*
 * comb_begin
 * 功能: 组合阶段开始时复制执行级流水寄存器到 *_1 工作副本，并统一完成本拍默认清零。
 * 输入依赖: inst_r[]。
 * 输出更新: inst_r_1[] 与各类 EXU 输出接口默认值、FU 输入握手默认值。
 * 约束: 不执行发射/写回/冲刷，仅建立默认组合初值。
 */
void Exu::comb_begin() {
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    inst_r_1[i] = inst_r[i];
  }

  // 每拍默认清零：统一在组合开始阶段完成，避免分散在各 comb_xxx。
  for (auto &req : out.ftq_pc_req->req) {
    req = {};
  }

  out.exe2csr->we = false;
  out.exe2csr->re = false;
  out.exe2csr->idx = 0;
  out.exe2csr->wcmd = 0;
  out.exe2csr->wdata = 0;

  for (int i = 0; i < TOTAL_FU_COUNT; i++) {
    out.exe2prf->bypass[i].valid = false;
  }

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    out.exe2prf->entry[i].valid = false;
    out.exu2rob->entry[i].valid = false;
    out.exe2iss->ready[i] = false;
    out.exe2iss->fu_ready_mask[i] = 0;
  }

  for (int i = 0; i < LSU_AGU_COUNT; i++) {
    out.exe2lsu->agu_req[i].valid = false;
  }
  for (int i = 0; i < LSU_SDU_COUNT; i++) {
    out.exe2lsu->sdu_req[i].valid = false;
  }

  out.exu2id->clear_mask = 0;
  out.exu2id->mispred = false;
  out.exu2id->redirect_pc = 0;
  out.exu2id->redirect_rob_idx = 0;
  out.exu2id->br_id = 0;
  out.exu2id->ftq_idx = 0;

  for (auto fu : units) {
    fu->in.en = false;
    fu->in.consume = false;
    fu->in.flush = false;
    fu->in.flush_mask = 0;
    fu->in.clear_mask = 0;
  }
}

/*
 * comb_ftq_pc_req
 * 功能: 为需要 PC 上下文的 ALU/BR 指令生成 FTQ 读请求。
 * 输入依赖: inst_r[], in.rob_bcast->flush, in.dec_bcast, 端口范围常量（IQ_ALU_PORT_BASE/IQ_BR_PORT_BASE）。
 * 输出更新: out.ftq_pc_req->req[]。
 * 约束: 被 flush 或分支 kill 的条目不发请求；仅匹配端口能力与操作类型的条目发起请求。
 */
void Exu::comb_ftq_pc_req() {
  for (int p_idx = 0; p_idx < ISSUE_WIDTH; p_idx++) {
    if (!inst_r[p_idx].valid) {
      continue;
    }

    const ExuInst &uop = inst_r[p_idx].uop;
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

/*
 * comb_ready
 * 功能: 生成 EXU->ISU 反压信息（端口 ready 与 FU ready mask）。
 * 输入依赖: inst_r[], issue_stall[], in.rob_bcast->flush, in.dec_bcast, port_mappings[].entries[].fu->out.ready。
 * 输出更新: out.exe2iss->ready[], out.exe2iss->fu_ready_mask[]。
 * 约束: flush 时所有端口 ready 置 0；被 kill 的在飞条目视作可释放端口占用。
 */
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
      // 读取 FU 对外 ready 信号
      if (entry.fu->out.ready) {
        mask |= entry.support_mask;
      }
    }

    out.exe2iss->fu_ready_mask[i] = mask;
  }
}

/*
 * comb_to_csr
 * 功能: 从 CSR 执行槽提取 CSR 读写请求并驱动 exe2csr 接口。
 * 输入依赖: inst_r[0], in.rob_bcast->flush, CSR 指令编码字段（func3/csr_idx/src*）。
 * 输出更新: out.exe2csr->{we,re,idx,wcmd,wdata}。
 * 约束: 非 CSR 或 flush 场景下输出默认无请求；仅端口 0 的 CSR 指令参与驱动。
 */
void Exu::comb_to_csr() {
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

/*
 * comb_pipeline
 * 功能: 管理执行级流水寄存器推进，并处理 flush/mispred/clear_mask 相关清理。
 * 输入依赖: inst_r[], inst_r_1[], issue_stall[], in.prf2exe->iss_entry[], in.rob_bcast->flush, in.dec_bcast->{mispred, br_mask, clear_mask}, units[]。
 * 输出更新: inst_r_1[]，并通过 FU 输入 IO 驱动 flush/clear_mask。
 * 约束: flush 最高优先级直接清空；mispred 先 flush 再 clear；被 kill 条目不保留到下一拍。
 */
void Exu::comb_pipeline() {
  // 1. 全局 Flush (最高优先级)
  if (in.rob_bcast->flush) {
    for (int i = 0; i < ISSUE_WIDTH; i++) {
      inst_r_1[i].valid = false;
    }
    for (auto fu : units) {
      fu->in.flush = true;
      fu->in.flush_mask = static_cast<wire<BR_MASK_WIDTH>>(-1);
      fu->in.clear_mask = 0;
      fu->comb_ctrl();
      fu->in.flush = false;
      fu->in.flush_mask = 0;
    }
    return;
  }

  wire<BR_MASK_WIDTH> clear = in.dec_bcast->clear_mask;
  const bool mispred = in.dec_bcast->mispred;
  const wire<BR_MASK_WIDTH> mispred_mask = in.dec_bcast->br_mask;

  // 2. 分支误预测选择性冲刷 + clear_mask（由 FU 内部按先 flush 再 clear 顺序处理）
  if (mispred || clear) {
    for (auto fu : units) {
      fu->in.flush = mispred;
      fu->in.flush_mask = mispred ? mispred_mask : 0;
      fu->in.clear_mask = clear;
      fu->comb_ctrl();
      fu->in.flush = false;
      fu->in.flush_mask = 0;
      fu->in.clear_mask = 0;
    }
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

    if (!(inst_r[i].valid && issue_stall[i]) && in.prf2exe->iss_entry[i].valid) {
      inst_r_1[i].valid = true;
      inst_r_1[i].uop = ExuInst::from_prf_exe_uop(in.prf2exe->iss_entry[i].uop);
    } else if (!(inst_r[i].valid && issue_stall[i])) {
      inst_r_1[i].valid = false;
    }

    // 对存活条目清除已解析分支的 bit
    if (inst_r_1[i].valid && clear) {
      inst_r_1[i].uop.br_mask &= ~clear;
    }
  }
}

/*
 * comb_exec
 * 功能: 驱动 FU 接收执行、收集完成结果并完成旁路/写回/LSU 请求/分支仲裁输出。
 * 输入依赖: inst_r[], issue_stall[], in.rob_bcast, in.dec_bcast, port_mappings, units, in.lsu2exe 回写请求。
 * 输出更新: out.iss2prf->bypass[], out.exe2prf->entry[], out.exu2rob->entry[], out.exe2lsu->{agu_req,sdu_req}[], out.exu2id->{mispred,clear_mask,redirect_*}，issue_stall[]。
 * 约束: 被 flush/kill 的结果不产生有效完成；每端口按映射优先命中的 FU 进行仲裁；clear_mask 统一经 exu2id 广播。
 */
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

      if (target_fu && target_fu->out.ready) {
        target_fu->in.en = true;
        target_fu->in.inst = inst_r[i].uop;
      } else {
        issue_stall[i] = true;
      }
    }
  }

  // 发射阶段：每个 FU 仅调用一次 comb_issue。
  for (auto fu : units) {
    fu->comb_issue();
  }

  // 旁路逻辑
  int fu_global_idx = 0; // 用于给每个 FU 编号
  // 遍历所有 FU 单元
  for (auto fu : units) {
    if (fu->out.complete) {
      // ✅ 无论是否能写回，先广播出去给 Bypass 用！
      out.exe2prf->bypass[fu_global_idx].uop = fu->out.inst.to_exe_prf_wb_uop();
      out.exe2prf->bypass[fu_global_idx].valid = true;
    }

    fu_global_idx++;
  }

  // ==========================================
  // 二、写回逻辑 (Writeback) - 终极解耦重构
  // ==========================================

  // 结果收集容器
  ExuEntry int_res[ALU_NUM] = {};
  ExuEntry br_res[BRU_NUM] = {};

  // 1. 全局端口扫描与立即分发 (Total Port Scan)
  for (int p_idx = 0; p_idx < ISSUE_WIDTH; p_idx++) {
    AbstractFU *winner_fu = nullptr;
    ExuInst u = {};
    bool has_u = false;

    // 仲裁：选择该端口的胜出 FU
    for (auto &map_entry : port_mappings[p_idx].entries) {
      if (map_entry.fu->out.complete) {
        u = map_entry.fu->out.inst;
        has_u = true;
        winner_fu = map_entry.fu;
        break;
      }
    }
    if (!has_u)
      continue;

    bool flushed = in.rob_bcast->flush || is_br_killed(u, in.dec_bcast);
    // A. 立即驱动 ROB (非访存指令在此完成)
    // 注意：LOAD/STA 的完成通报由 LSU 回调阶段处理
    if (!flushed && u.op != UOP_LOAD && u.op != UOP_STA) {
      out.exu2rob->entry[p_idx].valid = true;
      out.exu2rob->entry[p_idx].uop = u.to_exu_rob_uop();
    }

    // B. 立即外发 LSU 请求
    if (!flushed) {
      if (u.op == UOP_STA || u.op == UOP_LOAD) {
        auto *agu = dynamic_cast<AguUnit *>(winner_fu);
        Assert(agu != nullptr);
        int lsu_idx = agu->lsu_port_id();
        out.exe2lsu->agu_req[lsu_idx].valid = true;
        out.exe2lsu->agu_req[lsu_idx].uop = u.to_exe_lsu_req_uop();
      } else if (u.op == UOP_STD) {
        auto *sdu = dynamic_cast<SduUnit *>(winner_fu);
        Assert(sdu != nullptr);
        int lsu_idx = sdu->lsu_port_id();
        out.exe2lsu->sdu_req[lsu_idx].valid = true;
        out.exe2lsu->sdu_req[lsu_idx].uop = u.to_exe_lsu_req_uop();
      }
    }

    // C. 收集分类结果
    // 收集 INT 写回信息 (仅限需要写回目的寄存器的 ALU 指令)
    if (p_idx >= IQ_ALU_PORT_BASE && p_idx < IQ_ALU_PORT_BASE + ALU_NUM &&
        u.dest_en) {
      int idx = p_idx - IQ_ALU_PORT_BASE;
      int_res[idx].uop = u;
      int_res[idx].valid = true;
    }
    // 收集 BR 信息 (用于分支仲裁)
    // 仅保留存活分支结果；被 flush/kill 的分支不应继续驱动恢复广播。
    if (!flushed && p_idx >= IQ_BR_PORT_BASE &&
        p_idx < IQ_BR_PORT_BASE + BRU_NUM) {
      int idx = p_idx - IQ_BR_PORT_BASE;
      br_res[idx].uop = u;
      br_res[idx].valid = true;
    }

    winner_fu->in.consume = true;
  }

  // 消费阶段：每个 FU 仅调用一次 comb_consume。
  for (auto fu : units) {
    fu->comb_consume();
  }

  // 2. 选择性写回分发 (Writeback Distribution)

  // A. 处理常规计算写回 (仅扫描 INT 结果)
  for (int i = 0; i < ALU_NUM; i++) {
    if (int_res[i].valid) {
      int p_idx = IQ_ALU_PORT_BASE + i;
      out.exe2prf->entry[p_idx].valid = true;
      out.exe2prf->entry[p_idx].uop = int_res[i].uop.to_exe_prf_wb_uop();
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
  ExuInst *mispred_uop = nullptr;
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
    fu->seq();
  }
}
