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
  in.fu2exu = &fu2exu_io;
  out.exu2fu = &exu2fu_io;
}

Exu::~Exu() {
  for (auto fu : units) {
    delete fu;
  }
}

void Exu::init() {
  int agu_cnt = 0;
  int sdu_cnt = 0;

  port_mappings.resize(ISSUE_WIDTH);

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    uint64_t mask = GLOBAL_ISSUE_PORT_CONFIG[i].support_mask;

    // 1. MUL (Priority for writeback)
    if (mask & OP_MASK_MUL) {
      auto mul = new MulUnit("MUL", i, MUL_MAX_LATENCY);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(mul);
      port_mappings[i].entries.push_back({mul, fu_idx, OP_MASK_MUL});
    }

    // 2. DIV
    if (mask & OP_MASK_DIV) {
      auto div = new DivUnit("DIV", i, DIV_MAX_LATENCY);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(div);
      port_mappings[i].entries.push_back({div, fu_idx, OP_MASK_DIV});
    }

    // 3. AGU (Load)
    if (mask & OP_MASK_LD) {
      int lsu_agu_port = agu_cnt++;
      auto ldu = new AguUnit("AGU_LD", i);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(ldu);
      port_mappings[i].entries.push_back(
          {ldu, fu_idx, OP_MASK_LD, lsu_agu_port, -1});
    }

    // 4. AGU (STA)
    if (mask & OP_MASK_STA) {
      int lsu_agu_port = agu_cnt++;
      auto sta = new AguUnit("AGU_STA", i);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(sta);
      port_mappings[i].entries.push_back(
          {sta, fu_idx, OP_MASK_STA, lsu_agu_port, -1});
    }

    // 5. SDU (STD)
    if (mask & OP_MASK_STD) {
      int lsu_sdu_port = sdu_cnt++;
      auto sdu = new SduUnit("SDU", i);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(sdu);
      port_mappings[i].entries.push_back(
          {sdu, fu_idx, OP_MASK_STD, -1, lsu_sdu_port});
    }

    // 6. BRU
    if (mask & OP_MASK_BR) {
      auto bru = new BruUnit("BRU", i);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(bru);
      port_mappings[i].entries.push_back({bru, fu_idx, OP_MASK_BR});
    }

    // 7. ALU
    if (mask & OP_MASK_ALU) {
      std::string alu_name = "ALU";
      auto alu = new AluUnit(alu_name, i);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(alu);
      port_mappings[i].entries.push_back({alu, fu_idx, OP_MASK_ALU});
    }

    // 8. CSR
    if (mask & OP_MASK_CSR) {
      auto csr = new CsrUnit("CSR", i);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(csr);
      port_mappings[i].entries.push_back({csr, fu_idx, OP_MASK_CSR});
    }

    // 9. FP
    if (mask & OP_MASK_FP) {
      auto fpu = new FPUSoftfloat("FPU", i, 10);
      int fu_idx = static_cast<int>(units.size());
      units.push_back(fpu);
      FuEntry fpu_entry;
      fpu_entry.fu = fpu;
      fpu_entry.fu_idx = fu_idx;
      fpu_entry.support_mask = OP_MASK_FP;
      port_mappings[i].entries.push_back(fpu_entry);
    }
  }

  for (int i = 0; i < TOTAL_FU_COUNT; i++) {
    fu_inst_r[i] = {};
    fu_inst_r_1[i] = {};
    fu_to_port[i] = -1;
  }

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    for (auto &entry : port_mappings[i].entries) {
      fu_to_port[entry.fu_idx] = i;
    }
  }
}

/*
 * comb_begin
 * 功能: 组合阶段开始默认清零 EXU 输出，并初始化各 FU 的输入。
 * 输入依赖: units[] 当前状态。
 * 输出更新: EXU 各输出默认值，以及 units[i]->in 的默认清零。
 * 约束: 不执行发射/写回/冲刷，仅建立本拍组合逻辑初值。
 */
void Exu::comb_begin() {
  for (int i = 0; i < TOTAL_FU_COUNT; i++) {
    fu_inst_r_1[i] = fu_inst_r[i];
  }

  // 每拍默认清零：统一在组合开始阶段完成，避免分散在各 comb_xxx。

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

  for (int i = 0; i < TOTAL_FU_COUNT; i++) {
    out.exu2fu->entry[i].en = false;
    out.exu2fu->entry[i].consume = false;
    out.exu2fu->entry[i].flush = false;
    out.exu2fu->entry[i].flush_mask = 0;
    out.exu2fu->entry[i].clear_mask = 0;
  }
  for (size_t i = 0; i < units.size(); i++) {
    units[i]->comb_begin();
    in.fu2exu->entry[i] = units[i]->out;
  }
}



/*
 * comb_ready
 * 功能: 生成 EXU->ISU 反压信息（端口 ready 与 FU ready mask）。
 * 输入依赖: FU 输入槽占用状态、in.rob_bcast->flush、port_mappings、units[].out.ready。
 * 输出更新: out.exe2iss->ready[], out.exe2iss->fu_ready_mask[]。
 * 约束: flush 时所有端口 ready 置 0；FU 输入槽 ready 条件为 (!valid || fu_ready)。
 */
void Exu::comb_ready() {
  // 异常状态下（Flush/Mispred），Exu 停止接收新指令，防止脏数据进入
  if (in.rob_bcast->flush) {
    for (int i = 0; i < ISSUE_WIDTH; i++) {
      out.exe2iss->fu_ready_mask[i] = 0;
    }
    return;
  }

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    // FU 输入槽作为 FU 内部一级流水：slot_ready = !valid || fu_ready。
    uint64_t mask = 0;
    for (auto &entry : port_mappings[i].entries) {
      Assert(entry.fu_idx >= 0 && entry.fu_idx < TOTAL_FU_COUNT);
      const bool slot_valid = fu_inst_r_1[entry.fu_idx].valid;
      const bool fu_ready = in.fu2exu->entry[entry.fu_idx].ready;
      if (!slot_valid || fu_ready) {
        mask |= entry.support_mask;
      }
    }

    out.exe2iss->fu_ready_mask[i] = mask;
  }
}

/*
 * comb_to_csr
 * 功能: 从 CSR 执行槽提取 CSR 读写请求并驱动 exe2csr 接口。
 * 输入依赖: 端口 0 下 FU 的 fu_inst_r、in.rob_bcast->flush、CSR 编码字段。
 * 输出更新: out.exe2csr->{we,re,idx,wcmd,wdata}。
 * 约束: 非 CSR 或 flush 场景下输出默认无请求；仅端口 0 的 CSR 指令参与驱动。
 */
void Exu::comb_to_csr() {
  if (in.rob_bcast->flush) {
    return;
  }
  const ExuInst *csr_uop = nullptr;
  for (int fu_idx = 0; fu_idx < TOTAL_FU_COUNT; fu_idx++) {
    if (!fu_inst_r[fu_idx].valid) {
      continue;
    }
    if (fu_to_port[fu_idx] != 0) {
      continue;
    }
    const ExuInst &uop = fu_inst_r[fu_idx].uop;
    if (uop.op == UOP_CSR && !is_br_killed(uop, in.dec_bcast)) {
      csr_uop = &uop;
      break;
    }
  }
  if (!csr_uop) {
    return;
  }
  const ExuInst &uop = *csr_uop;

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

/*
 * comb_pipeline
 * 功能: pipeline 阶段将本拍 issue 指令入 FU 输入槽（fu_inst_r_1）。
 * 输入依赖: in.prf2exe->iss_entry[]、in.rob_bcast->flush、in.dec_bcast、FU 输入槽下一态。
 * 输出更新: FU 输入槽下一态（fu_inst_r_1）。
 * 约束: flush 时不接收新指令；每端口按映射顺序选择首个可接收 FU。
 */
void Exu::comb_pipeline() {
  if (in.rob_bcast->flush) {
    for (int i = 0; i < TOTAL_FU_COUNT; i++) {
      fu_inst_r_1[i].valid = false;
    }
    return;
  }

  wire<BR_MASK_WIDTH> clear = in.dec_bcast->clear_mask;
  for (int fu_idx = 0; fu_idx < TOTAL_FU_COUNT; fu_idx++) {
    if (!fu_inst_r_1[fu_idx].valid) {
      continue;
    }
    if (is_br_killed(fu_inst_r_1[fu_idx].uop, in.dec_bcast)) {
      fu_inst_r_1[fu_idx].valid = false;
      continue;
    }
    if (clear) {
      fu_inst_r_1[fu_idx].uop.br_mask &= ~clear;
    }
  }

  for (int p_idx = 0; p_idx < ISSUE_WIDTH; p_idx++) {
    if (!in.prf2exe->iss_entry[p_idx].valid) {
      continue;
    }
    ExuInst uop = ExuInst::from_prf_exe_uop(in.prf2exe->iss_entry[p_idx].uop);
    if (is_br_killed(uop, in.dec_bcast)) {
      continue;
    }
    if (clear) {
      uop.br_mask &= ~clear;
    }
    uint64_t req_bit = (1ULL << uop.op);
    int target_fu_idx = -1;
    for (const auto &entry : port_mappings[p_idx].entries) {
      const bool slot_valid = fu_inst_r_1[entry.fu_idx].valid;
      const bool fu_ready = in.fu2exu->entry[entry.fu_idx].ready;
      if ((entry.support_mask & req_bit) &&
          (!slot_valid || fu_ready)) {
        target_fu_idx = entry.fu_idx;
        break;
      }
    }
    Assert(target_fu_idx >= 0 && "EXU enqueue without free FU input slot");
    fu_inst_r_1[target_fu_idx].valid = true;
    fu_inst_r_1[target_fu_idx].uop = uop;
  }
}

/*
 * comb_fu_ctrl
 * 功能: Stage-0，FU 控制阶段；每拍对所有 FU 下发 flush/clear 控制。
 * 输入依赖: in.rob_bcast->flush, in.dec_bcast->{mispred, br_mask, clear_mask}。
 * 输出更新: FU 内部状态（经 units[].comb_ctrl）。
 * 约束: 每个 FU 每拍仅调用一次 comb_ctrl。
 */
void Exu::comb_fu_ctrl() {
  const bool flush_all = in.rob_bcast->flush;
  const bool flush_br = (!flush_all) && in.dec_bcast->mispred;
  const wire<BR_MASK_WIDTH> flush_mask =
      flush_all ? static_cast<wire<BR_MASK_WIDTH>>(-1)
                : (flush_br ? in.dec_bcast->br_mask : 0);
  const wire<BR_MASK_WIDTH> clear_mask = in.dec_bcast->clear_mask;

  for (size_t i = 0; i < units.size(); i++) {
    out.exu2fu->entry[i].flush = (flush_all || flush_br);
    out.exu2fu->entry[i].flush_mask = flush_mask;
    out.exu2fu->entry[i].clear_mask = clear_mask;
    
    units[i]->in = out.exu2fu->entry[i];
    units[i]->comb_ctrl();
  }
}

/*
 * comb_exu2fu_dispatch
 * 功能: Stage-1，将 FU 输入槽中的有效条目分发给对应的 FU 执行模块。
 * 输入依赖: FU 输入槽（fu_inst_r）、in.rob_bcast、in.dec_bcast、in.ftq_pc_resp。
 * 输出更新: units[].in.{en, inst} 以及下一态输入槽 fu_inst_r_1 的清除。
 * 约束: flush/kill 条目不下发。
 */
void Exu::comb_exu2fu_dispatch() {
  for (int fu_idx = 0; fu_idx < TOTAL_FU_COUNT; fu_idx++) {
    if (!fu_inst_r[fu_idx].valid) {
      continue;
    }
    ExuInst issued_uop = fu_inst_r[fu_idx].uop;
    int p_idx = fu_to_port[fu_idx];
    if (p_idx < 0 || p_idx >= ISSUE_WIDTH) {
      continue;
    }
    if (in.rob_bcast->flush || is_br_killed(issued_uop, in.dec_bcast)) {
      fu_inst_r_1[fu_idx].valid = false;
      continue;
    }
    if (in.dec_bcast->clear_mask) {
      issued_uop.br_mask &= ~in.dec_bcast->clear_mask;
    }

    out.exu2fu->entry[fu_idx].en = true;
    out.exu2fu->entry[fu_idx].inst = issued_uop;
  }
}

/*
 * comb_fu_exec
 * 功能: Stage-2，FU 执行阶段；触发各个 FU 的组合逻辑。
 * 输入依赖: units[].in。
 * 输出更新: units[].out（ready/complete/inst）。
 * 约束: 每个 FU 每拍仅调用一次 comb_issue。
 */
void Exu::comb_fu_exec() {
  for (size_t i = 0; i < units.size(); i++) {
    units[i]->in = out.exu2fu->entry[i];
    units[i]->comb_issue();
    in.fu2exu->entry[i].complete = units[i]->out.complete;
    in.fu2exu->entry[i].inst = units[i]->out.inst;
  }
}

/*
 * comb_fu2exu_collect
 * 功能: Stage-3，从各 FU 收集执行结果，完成旁路/写回/LSU请求分发/分支仲裁。
 * 输入依赖: units[].out、port_mappings、in.rob_bcast、in.dec_bcast、in.lsu2exe、in.csr2exe。
 * 输出更新: out.exe2prf->{bypass,entry}、out.exu2rob->entry[]、out.exe2lsu->{agu_req,sdu_req}、out.exu2id->{mispred,clear_mask,redirect_*}、units[].in.consume。
 * 约束: 被 flush/kill 的结果不产生有效完成；端口内按映射顺序选择 winner FU；分支误预测取最老条目。
 */
void Exu::comb_fu2exu_collect() {
  // 旁路逻辑
  int fu_global_idx = 0; // 用于给每个 FU 编号
  // 遍历所有 FU 单元
  for (size_t i = 0; i < units.size(); i++) {
    if (in.fu2exu->entry[i].complete) {
      // ✅ 无论是否能写回，先广播出去给 Bypass 用！
      out.exe2prf->bypass[fu_global_idx].uop =
          in.fu2exu->entry[i].inst.to_exe_prf_wb_uop();
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
    const FuEntry *winner_entry = nullptr;
    ExuInst u = {};
    bool has_u = false;

    // 仲裁：选择该端口的胜出 FU
    for (auto &map_entry : port_mappings[p_idx].entries) {
      if (in.fu2exu->entry[map_entry.fu_idx].complete) {
        u = in.fu2exu->entry[map_entry.fu_idx].inst;
        has_u = true;
        winner_entry = &map_entry;
        break;
      }
    }
    if (!has_u)
      continue;

    Assert(winner_entry != nullptr);

    bool flushed = in.rob_bcast->flush || is_br_killed(u, in.dec_bcast);

    if (u.op == UOP_CSR && out.exe2csr->re) {
      u.result = in.csr2exe->rdata;
    }

    // A. 立即驱动 ROB (非访存指令在此完成)
    // 注意：LOAD/STA 的完成通报由 LSU 回调阶段处理
    if (!flushed && u.op != UOP_LOAD && u.op != UOP_STA) {
      out.exu2rob->entry[p_idx].valid = true;
      out.exu2rob->entry[p_idx].uop = u.to_exu_rob_uop();
    }

    // B. 立即外发 LSU 请求
    if (!flushed) {
      if (u.op == UOP_STA || u.op == UOP_LOAD) {
        int lsu_idx = winner_entry->lsu_agu_port;
        Assert(lsu_idx >= 0 && lsu_idx < LSU_AGU_COUNT);
        out.exe2lsu->agu_req[lsu_idx].valid = true;
        out.exe2lsu->agu_req[lsu_idx].uop = u.to_exe_lsu_req_uop();
      } else if (u.op == UOP_STD) {
        int lsu_idx = winner_entry->lsu_sdu_port;
        Assert(lsu_idx >= 0 && lsu_idx < LSU_SDU_COUNT);
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

    out.exu2fu->entry[winner_entry->fu_idx].consume = true;
  }

  // 消费阶段：FU 基于 consume 弹出完成项。
  for (size_t i = 0; i < units.size(); i++) {
    units[i]->in = out.exu2fu->entry[i];
    units[i]->comb_consume();
    in.fu2exu->entry[i].ready = units[i]->out.ready;
  }
  // consume 后由 FU.ready 决定是否释放 EXU 输入槽位
  for (int fu_idx = 0; fu_idx < TOTAL_FU_COUNT; fu_idx++) {
    if (fu_inst_r_1[fu_idx].valid && in.fu2exu->entry[fu_idx].ready) {
      fu_inst_r_1[fu_idx].valid = false;
    }
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

/*
 * comb_exec
 * 功能: FU 交互执行总控（fu_ctrl -> dispatch -> fu_exec -> collect）。
 * 输入依赖: 各子阶段依赖集合。
 * 输出更新: 各子阶段输出集合。
 * 约束: 严格按照逻辑依赖排序，保持正确的组合逻辑语义。
 */
void Exu::comb_exec() {
  // Stage 0: FU 冲刷与掩码控制下发（flush/clear）
  comb_fu_ctrl();

  // Stage 1: 将输入槽的指令分发到对应 FU
  comb_exu2fu_dispatch();

  // Stage 2: 触发 FU 内部执行组合逻辑
  comb_fu_exec();

  // Stage 3: 从 FU 回收结果、分发旁路写回以及冲刷仲裁
  comb_fu2exu_collect();
}

// ==========================================
// 时序逻辑
// ==========================================
void Exu::seq() {
  for (int i = 0; i < TOTAL_FU_COUNT; i++) {
    fu_inst_r[i] = fu_inst_r_1[i];
  }
  for (auto fu : units) {
    fu->seq();
  }
}
