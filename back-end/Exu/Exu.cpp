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

  // 二、写回逻辑 (Writeback)
  for (int p_idx = 0; p_idx < ISSUE_WIDTH; p_idx++) {
    for (auto &map_entry : port_mappings[p_idx].entries) {
      AbstractFU *fu = map_entry.fu;

      // 检查这个 FU 有没有结果吐出来
      MicroOp *res = fu->get_finished_uop();

      if (res) {
        if (out.exe2prf->entry[p_idx].valid) {
          // 糟糕！端口已经被同组的兄弟占了！
          continue;
        }

        // --- 正常的写回处理 ---
        bool flushed = in.rob_bcast->flush;
        if (in.dec_bcast->mispred &&
            ((1ULL << res->tag) & in.dec_bcast->br_mask)) {
          flushed = true;
        }

        // 关键修正：写回端口用于唤醒发射队列中的依赖指令
        // 即使指令被冲刷，也必须写回以广播唤醒信号！
        // PRF 阶段会负责过滤掉被冲刷的指令
        // [修正] STA 和 LOAD 都不再通过 native 路径写回端口，而是等 LSU 返回
        if (res->op != UOP_LOAD && res->op != UOP_STA) {
          out.exe2prf->entry[p_idx].valid = true;
          out.exe2prf->entry[p_idx].uop = *res;
        }

        // LSU 请求：只在非冲刷 (Flush) 时发送
        if (!flushed) {
          if (res->op == UOP_STA || res->op == UOP_LOAD) {
            int lsu_idx = fu->get_lsu_port_id();
            out.exe2lsu->agu_req[lsu_idx].valid = true;
            out.exe2lsu->agu_req[lsu_idx].uop = *res;
          } else if (res->op == UOP_STD) {
            int lsu_idx = fu->get_lsu_port_id();
            out.exe2lsu->sdu_req[lsu_idx].valid = true;
            out.exe2lsu->sdu_req[lsu_idx].uop = *res;
          }
        }

        // ✅ 总是移除结果，避免重复广播
        fu->pop_finished();

        // 既然这个端口被我占了，同组的其他 FU 就别想了，跳出内层循环
        break;
      }
    }
  }

  // LSU 的 Load 结果
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (in.lsu2exe->wb_req[i].valid) {
      MicroOp &load_uop = in.lsu2exe->wb_req[i].uop;

      int wb_port_idx = IQ_LD_PORT_BASE + i;

      if (!out.exe2prf->entry[wb_port_idx].valid) {
        out.exe2prf->entry[wb_port_idx].valid = true;
        out.exe2prf->entry[wb_port_idx].uop = load_uop;
      } else {
        Assert(0);
      }
    }
  }

  // [新增] LSU 的 STA 结果 (带 Page Fault)
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (in.lsu2exe->sta_wb_req[i].valid) {
      MicroOp &sta_uop = in.lsu2exe->sta_wb_req[i].uop;

      int wb_port_idx = IQ_STA_PORT_BASE + i;

      if (!out.exe2prf->entry[wb_port_idx].valid) {
        out.exe2prf->entry[wb_port_idx].valid = true;
        out.exe2prf->entry[wb_port_idx].uop = sta_uop;
      } else {
        // 如果端口已被同周期的其他非访存指令占领，这里会报错。
        // 由于 STA 是独占端口的，且 AGU 已被跳过写回，这里理论上应该是干净的。
        Assert(0);
      }
    }
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
