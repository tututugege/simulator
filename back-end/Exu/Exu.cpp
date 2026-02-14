#include "Exu.h"
#include "config.h"

Exu::Exu(SimContext *ctx, FTQ *ftq) : ctx(ctx), ftq(ftq) {
  // 可以在这里或 init 创建 backend
}

Exu::~Exu() {
  for (auto fu : units) {
    delete fu;
  }
}

void Exu::init() {
  int alu_cnt = 0;
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
      auto bru = new BruUnit("BRU", i, ftq);
      units.push_back(bru);
      port_mappings[i].entries.push_back({bru, OP_MASK_BR});
    }

    // 7. ALU
    if (mask & OP_MASK_ALU) {
      std::string alu_name = "ALU" + std::to_string(alu_cnt++);
      auto alu = new AluUnit(alu_name, i);
      units.push_back(alu);
      port_mappings[i].entries.push_back({alu, OP_MASK_ALU});
    }

    // 8. CSR
    if (mask & OP_MASK_CSR) {
      auto csr = new CsrUnit("CSR", i, out.exe2csr, in.csr2exe);
      units.push_back(csr);
      port_mappings[i].entries.push_back({csr, OP_MASK_CSR});
    }
  }

  // 初始化流水线寄存器
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    inst_r[i].valid = false;
    inst_r_1[i].valid = false;
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
    if (inst_r[i].valid && in.dec_bcast->mispred &&
        ((1ULL << inst_r[i].uop.tag) & in.dec_bcast->br_mask)) {
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
    out.exe2csr->we = inst_r[0].uop.func3 == 1 || inst_r[0].uop.src1_areg != 0;
    out.exe2csr->re = inst_r[0].uop.func3 != 1 || inst_r[0].uop.dest_areg != 0;

    out.exe2csr->idx = inst_r[0].uop.csr_idx;
    out.exe2csr->wcmd = inst_r[0].uop.func3 & 0b11;
    if (inst_r[0].uop.src2_is_imm) {
      out.exe2csr->wdata = inst_r[0].uop.imm;
    } else {
      out.exe2csr->wdata = inst_r[0].uop.src1_rdata;
    }
  }
}

// ==========================================
// 2. 组合逻辑：流水线控制 (Flush + Latch + Filter)
// ==========================================

void Exu::comb_pipeline() {
  // 1. 全局 Flush (最高优先级)
  // 如果 Flush，所有东西都清空，没商量
  if (in.rob_bcast->flush) {
    for (int i = 0; i < ISSUE_WIDTH; i++) {
      inst_r_1[i].valid = false;
    }
    for (auto fu : units)
      fu->flush((mask_t)-1);
    return;
  }

  // 2. 分支误预测 (Selective Flush)
  // 这里必须做两件事：
  // A. Flush FU 内部 (您已经做了)
  // B. Flush inst_r 本身！(您漏了)

  if (in.dec_bcast->mispred) {
    mask_t mask = in.dec_bcast->br_mask;

    // A. Flush FU
    for (auto fu : units)
      fu->flush(mask);

    // 步骤 B：明确检查并清除 inst_r 中的待冲刷指令
    // 注意：我们在计算 Next State (inst_r_1) 时，
    // 需要基于“过滤后”的 inst_r 状态来决定是 Hold 还是 Accept New。
    // 为了简单，我们可以直接在下面的主循环中进行判断。
  }

  // 3. 主循环：计算 Next State (inst_r_1)
  for (int i = 0; i < ISSUE_WIDTH; i++) {

    // 🔍 Step 1: 检查当前 inst_r 是否被 Kill (Mispred)
    bool current_killed = false;
    if (inst_r[i].valid && in.dec_bcast->mispred) {
      if ((1ULL << inst_r[i].uop.tag) & in.dec_bcast->br_mask) {
        current_killed = true;
      }
    }

    // 🚦 Step 2: 决定 Next State

    if (current_killed) {
      // 💀 如果当前指令被杀死了
      // 那么不管是否停顿 (Stall)，它都不能留到下一拍！
      // 此时 inst_r_1 应该置空 (或者查看发射阶段是否有新指令补位)
      // 通常误预测发生的那一拍，发射阶段也会被冲刷 (Flush)，所以大概率无新指令
      inst_r_1[i].valid = false;

      // 注意：如果被杀了，issue_stall[i] 应该被忽略
      continue;
    }

    // --- 下面是未被冲刷指令的逻辑 ---
    if (inst_r[i].valid && issue_stall[i]) {
      inst_r_1[i] = inst_r[i]; // 保持不变 (Hold)
    } else if (in.prf2exe->iss_entry[i].valid) {
      inst_r_1[i] = in.prf2exe->iss_entry[i];
    } else {
      inst_r_1[i].valid = false;
    }

    if (inst_r_1[i].valid) {
      // debug removed
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
      if (in.dec_bcast->mispred &&
          ((1ULL << inst_r[i].uop.tag) & in.dec_bcast->br_mask)) {
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
      out.exe2prf->bypass[fu_global_idx].uop = *res;
      out.exe2prf->bypass[fu_global_idx].valid = true;
    }

    fu_global_idx++;
  }

  // ==========================================
  // 二、写回逻辑 (Writeback) - 终极解耦重构
  // ==========================================

  // 结果收集容器 (利用现有 UopEntry 静态数组)
  UopEntry int_res[ALU_NUM];
  UopEntry br_res[BRU_NUM];

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

    bool flushed = in.rob_bcast->flush || 
                  (in.dec_bcast->mispred && ((1ULL << u->tag) & in.dec_bcast->br_mask));

    // A. 立即驱动 ROB (非访存指令在此完成)
    // 注意：LOAD/STA 的完成通报由 LSU 回调阶段处理
    if (u->op != UOP_LOAD && u->op != UOP_STA) {
      out.exu2rob->entry[p_idx].valid = true;
      out.exu2rob->entry[p_idx].uop = *u;
    }

    // B. 立即外发 LSU 请求
    if (!flushed) {
      int lsu_idx = winner_fu->get_lsu_port_id();
      if (u->op == UOP_STA || u->op == UOP_LOAD) {
        out.exe2lsu->agu_req[lsu_idx].valid = true;
        out.exe2lsu->agu_req[lsu_idx].uop = *u;
      } else if (u->op == UOP_STD) {
        out.exe2lsu->sdu_req[lsu_idx].valid = true;
        out.exe2lsu->sdu_req[lsu_idx].uop = *u;
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
    if (p_idx >= IQ_BR_PORT_BASE && p_idx < IQ_BR_PORT_BASE + BRU_NUM) {
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
      out.exe2prf->entry[p_idx].uop = int_res[i].uop;
    }
  }

  // B. 处理 LSU 回调写回 (LOAD/STA 数据回流)
  // 此处同时驱动 PRF 和 ROB
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (in.lsu2exe->wb_req[i].valid) {
      int p_idx = IQ_LD_PORT_BASE + i;
      MicroOp &u = in.lsu2exe->wb_req[i].uop;
      Assert(!out.exe2prf->entry[p_idx].valid);
      out.exe2prf->entry[p_idx].valid = true;
      out.exe2prf->entry[p_idx].uop = u;
      out.exu2rob->entry[p_idx].valid = true;
      out.exu2rob->entry[p_idx].uop = u;
    }
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (in.lsu2exe->sta_wb_req[i].valid) {
      int p_idx = IQ_STA_PORT_BASE + i;
      MicroOp &u = in.lsu2exe->sta_wb_req[i].uop;
      Assert(!out.exe2prf->entry[p_idx].valid);
      out.exe2prf->entry[p_idx].valid = true;
      out.exe2prf->entry[p_idx].uop = u;
      out.exu2rob->entry[p_idx].valid = true;
      out.exu2rob->entry[p_idx].uop = u;
    }
  }

  // ==========================================
  // 三、分支误预测仲裁 (Early Recovery)
  // ==========================================
  bool mispred = false;
  MicroOp *mispred_uop = nullptr;

  for (int i = 0; i < BRU_NUM; i++) {
    if (br_res[i].valid && br_res[i].uop.mispred) {
      if (!mispred) {
        mispred = true;
        mispred_uop = &br_res[i].uop;
      } else if (cmp_inst_age(*mispred_uop, br_res[i].uop)) {
        mispred_uop = &br_res[i].uop;
      }
    }
  }

  out.exu2id->mispred = mispred;
  if (mispred) {
    out.exu2id->redirect_pc = mispred_uop->diag_val;
    out.exu2id->redirect_rob_idx = mispred_uop->rob_idx;
    out.exu2id->br_tag = mispred_uop->tag;
    out.exu2id->ftq_idx = mispred_uop->ftq_idx;
  } else {
    out.exu2id->redirect_pc = 0;
    out.exu2id->redirect_rob_idx = 0;
    out.exu2id->br_tag = 0;
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

ExuIO Exu::get_hardware_io() {
  ExuIO hardware;

  // --- Inputs ---
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    hardware.from_iss.valid[i] = in.prf2exe->iss_entry[i].valid;
    hardware.from_iss.uop[i]   = IssExeUop::filter(in.prf2exe->iss_entry[i].uop);
    hardware.from_iss.src1_data[i] = in.prf2exe->iss_entry[i].uop.src1_rdata;
    hardware.from_iss.src2_data[i] = in.prf2exe->iss_entry[i].uop.src2_rdata;
  }
  hardware.from_back.flush = in.rob_bcast->flush;

  // --- Outputs ---
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    hardware.to_back.valid[i] = out.exe2prf->entry[i].valid;
    hardware.to_back.uop[i]   = ExeWbUop::filter(out.exe2prf->entry[i].uop);
  }

  return hardware;
}
