#include "Isu.h"
#include "config.h"
#include <cstdint>
#include <cstring>
#include <vector>

Isu::Isu(SimContext *context) : ctx(context) {}

void Isu::add_iq(const IssueQueueConfig &cfg) {
  iqs.emplace_back(cfg);
  configs.push_back(cfg);
}

void Isu::apply_wakeup_to_uop(IqStoredUop &uop) const {
  for (int k = 0; k < MAX_WAKEUP_PORTS; k++) {
    if (!out.iss_awake->wake[k].valid) {
      continue;
    }
    uint32_t preg = out.iss_awake->wake[k].preg;
    if (uop.src1_en && uop.src1_preg == preg) {
      uop.src1_busy = false;
    }
    if (uop.src2_en && uop.src2_preg == preg) {
      uop.src2_busy = false;
    }
  }
}

void Isu::init() {

  iqs.clear();
  configs.clear();
  for (auto &stage : mul_wake_pipe) {
    for (auto &e : stage)
      e = {};
  }
  for (auto &stage : mul_wake_pipe_1) {
    for (auto &e : stage)
      e = {};
  }
  for (auto &e : div_wake_slots)
    e = {};
  for (auto &e : div_wake_slots_1)
    e = {};
  for (auto &e : fp_wake_slots)
    e = {};
  for (auto &e : fp_wake_slots_1)
    e = {};

  // 遍历每一个 IQ 配置
  for (int i = 0; i < IQ_NUM; i++) {
    const auto &iq_cfg = GLOBAL_IQ_CONFIG[i];

    // 1. 设置 IQ 基本参数
    IssueQueueConfig dynamic_cfg;
    dynamic_cfg.id = iq_cfg.id;
    dynamic_cfg.size = iq_cfg.size;
    dynamic_cfg.dispatch_width = iq_cfg.dispatch_width;
    dynamic_cfg.supported_ops =
        iq_cfg.supported_ops; // 这是给 Dispatch 路由用的粗粒度 Mask

    // 2. ✨ 自动认领物理端口 ✨
    // 根据 range (start, num) 去查 GLOBAL_ISSUE_PORT_CONFIG
    for (int p = 0; p < iq_cfg.port_num; p++) {
      // 计算在全剧表中的下标
      int global_idx = iq_cfg.port_start_idx + p;

      // 拿到详细端口信息
      const auto &port_info = GLOBAL_ISSUE_PORT_CONFIG[global_idx];

      // 将这个端口绑定到当前 IQ (发射队列)
      // 注意：PortBinding 结构体里需要 port_idx 和 capability_mask
      // Use GLOBAL_ISSUE_PORT_CONFIG array index as the canonical physical
      // issue port id. This avoids TU-local __COUNTER__ differences in
      // config.h causing Isu/Exu port-id mismatch.
      dynamic_cfg.ports.push_back({
          global_idx,            // Canonical physical port id
          port_info.support_mask // Capability mask
      });
    }

    // 3. 创建 IQ
    add_iq(dynamic_cfg);
  }
}

/*
 * comb_begin
 * 功能: 调用各 IQ 的 comb_begin 初始化本拍工作副本与默认 IO，并复制两类延迟唤醒状态到 *_1。
 * 输入依赖: iqs[], mul/div/fp 延迟唤醒状态。
 * 输出更新: IQ 内部 *_1 状态与 out.free_slots，mul/div/fp *_1 状态。
 * 约束: 仅做状态镜像，不执行入队/发射/唤醒决策。
 */
void Isu::comb_begin() {
  for (auto &q : iqs) {
    q.comb_begin();
  }
  for (int d = 0; d < ISU_MUL_WAKE_DEPTH; d++) {
    for (int s = 0; s < ISU_MUL_WAKE_SLOT_NUM; s++) {
      mul_wake_pipe_1[d][s] = mul_wake_pipe[d][s];
    }
  }
  for (int i = 0; i < ISU_DIV_WAKE_SLOT_NUM; i++) {
    div_wake_slots_1[i] = div_wake_slots[i];
  }
  for (int i = 0; i < ISU_FP_WAKE_SLOT_NUM; i++) {
    fp_wake_slots_1[i] = fp_wake_slots[i];
  }
}


int Isu::get_latency(UopType uop, wire<7> func7) {
  if (uop == UOP_MUL)
    return MUL_MAX_LATENCY; // 乘法指令延迟
  if (uop == UOP_DIV)
    return DIV_MAX_LATENCY; // 除法指令延迟
  if (uop == UOP_FP) {
    switch (func7 >> 2) {
    case 0b00000: // FADD.S
    case 0b00001: // FSUB.S
      return 5;
    case 0b00010: // FMUL.S
      return 3;
    case 0b00011: // FDIV.S
      return 10;
    default:
      if (func7 == 0x2C) { // FSQRT.S
        return 10;
      }
      if (func7 == 0x60 || func7 == 0x68) { // FCVT.W.S / FCVT.S.W
        return 3;
      }
      return 5;
    }
  }
  return 1;                 // 其他指令认为是单周期，走 Fast Wakeup
}

/*
 * comb_ready
 * 功能: 计算每个 IQ 的剩余容量并反馈给 Dispatch。
 * 输入依赖: iqs[i].out.free_slots。
 * 输出更新: out.iss2dis->ready_num[i]。
 * 约束: 仅报告容量，不改变 IQ 内部状态。
 */
void Isu::comb_ready() {
  for (int i = 0; i < IQ_NUM; i++) {
    out.iss2dis->ready_num[i] = iqs[i].out.free_slots;
  }
}

/*
 * comb_enq
 * 功能: 接收 dis2iss 请求，经 iq.in.enq_reqs 驱动对应 IQ 入队。
 * 输入依赖: in.dis2iss->req[][], configs[i].dispatch_width, out.iss_awake（用于入队前叠加唤醒）。
 * 输出更新: IQ 内部 *_1 状态（通过 q.comb_enq 写入）。
 * 约束: 仅对 valid 请求入队；入队失败视为设计错误并触发 Assert。
 */
void Isu::comb_enq() {
  for (int i = 0; i < IQ_NUM; i++) {
    auto &q = iqs[i];
    int max_w = configs[i].dispatch_width; // 获取该 IQ 配置的最大入队宽

    for (int w = 0; w < max_w; w++) {
      q.in.enq_reqs[w] = {};
    }

    // 遍历该 IQ 的所有输入通道
    for (int w = 0; w < max_w; w++) {
      // 使用新接口结构 req[i][w]
      if (in.dis2iss->req[i][w].valid) {
        IqStoredUop uop = IqStoredUop::from_dis_iss_uop(in.dis2iss->req[i][w].uop);
        // 本拍入队前叠加唤醒总线，避免把“可读源”误标成 busy。
        apply_wakeup_to_uop(uop);

        IqStoredEntry new_entry;
        new_entry.uop = uop;
        new_entry.valid = true;
        q.in.enq_reqs[w] = new_entry;
      }
    }

    q.comb_enq();
  }
}

/*
 * comb_issue
 * 功能: 通过 IQ 输入 IO 驱动调度/发射，并消费 IQ 输出 grant 生成 iss2prf。
 * 输入依赖: in.exe2iss->ready, in.exe2iss->fu_ready_mask, in.rob_bcast->flush, in.dec_bcast->mispred。
 * 输出更新: out.iss2prf->iss_entry[]，IQ 内部提交结果（由 q.comb_issue 完成）。
 * 约束: 发射需同时满足端口 ready 与 FU 能力掩码；flush/mispred 时禁止新发射。
 */
void Isu::comb_issue() {

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    out.iss2prf->iss_entry[i].valid = false;
  }

  for (size_t i = 0; i < iqs.size(); i++) {
    IssueQueue &q = iqs[i];
    q.in.issue_block = in.rob_bcast->flush || in.dec_bcast->mispred;
    for (int p = 0; p < ISSUE_WIDTH; p++) {
      q.in.port_ready[p] = in.exe2iss->ready[p];
      q.in.port_fu_ready_mask[p] = in.exe2iss->fu_ready_mask[p];
    }

    q.comb_issue();

    for (int phys_port = 0; phys_port < ISSUE_WIDTH; phys_port++) {
      const auto &grant = q.out.issue_grants[phys_port];
      if (!grant.valid) {
        continue;
      }
      Assert(!out.iss2prf->iss_entry[phys_port].valid);
      out.iss2prf->iss_entry[phys_port].valid = true;
      out.iss2prf->iss_entry[phys_port].uop = grant.uop.to_iss_prf_uop();
    }
  }
}

/*
 * comb_calc_latency_next
 * 功能: 更新两类延迟唤醒状态：DIV/FP countdown 槽位 + MUL 移位寄存器。
 * 输入依赖: mul/div/fp 当前状态, out.iss2prf->iss_entry[], get_latency(op)。
 * 输出更新: mul/div/fp *_1 状态。
 * 约束: 仅 MUL/DIV/FP 且 latency>1 进入延迟唤醒；countdown==0 的 DIV/FP 条目不再保留到下一拍。
 */
void Isu::comb_calc_latency_next() {
  // DIV 槽位：countdown==0 的条目在本拍唤醒后不保留，其余条目倒计时减一。
  for (int i = 0; i < ISU_DIV_WAKE_SLOT_NUM; i++) {
    const auto &cur = div_wake_slots[i];
    auto &nxt = div_wake_slots_1[i];
    if (!cur.valid) {
      nxt = {};
      continue;
    }
    if (cur.countdown > 0) {
      nxt = cur;
      nxt.countdown = cur.countdown - 1;
    } else {
      nxt = {};
    }
  }

  // FP 槽位：与 DIV 相同。
  for (int i = 0; i < ISU_FP_WAKE_SLOT_NUM; i++) {
    const auto &cur = fp_wake_slots[i];
    auto &nxt = fp_wake_slots_1[i];
    if (!cur.valid) {
      nxt = {};
      continue;
    }
    if (cur.countdown > 0) {
      nxt = cur;
      nxt.countdown = cur.countdown - 1;
    } else {
      nxt = {};
    }
  }

  // MUL 移位寄存器：整体右移一拍，stage[0] 清空等待本拍新发射写入。
  for (auto &stage : mul_wake_pipe_1) {
    for (auto &e : stage)
      e = {};
  }
  if (MUL_MAX_LATENCY > 1) {
    for (int stage = ISU_MUL_WAKE_DEPTH - 1; stage >= 1; stage--) {
      for (int s = 0; s < ISU_MUL_WAKE_SLOT_NUM; s++) {
        mul_wake_pipe_1[stage][s] = mul_wake_pipe[stage - 1][s];
      }
    }
  }

  auto alloc_iter_slot = [](auto &slots, const IssPrfIO::IssPrfUop &uop,
                            int countdown) -> bool {
    for (auto &slot : slots) {
      if (!slot.valid) {
        slot.valid = true;
        slot.countdown = countdown;
        slot.dest_preg = uop.dest_preg;
        slot.br_mask = uop.br_mask;
        return true;
      }
    }
    return false;
  };

  auto alloc_mul_stage0 = [&](const IssPrfIO::IssPrfUop &uop) -> bool {
    if (MUL_MAX_LATENCY <= 1) {
      return false;
    }
    for (auto &slot : mul_wake_pipe_1[0]) {
      if (!slot.valid) {
        slot.valid = true;
        slot.dest_preg = uop.dest_preg;
        slot.br_mask = uop.br_mask;
        return true;
      }
    }
    return false;
  };

  // 新发射条目写入对应延迟结构。
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    const auto &inst = out.iss2prf->iss_entry[i];
    if (!(inst.valid && inst.uop.dest_en)) {
      continue;
    }
    UopType op = decode_uop_type(inst.uop.op);
    int lat = get_latency(op, inst.uop.func7);
    if (lat <= 1) {
      continue;
    }
    if (op == UOP_MUL) {
      bool ok = alloc_mul_stage0(inst.uop);
      Assert(ok && "MUL 延迟唤醒 stage0 槽位不足");
    } else if (op == UOP_DIV) {
      bool ok = alloc_iter_slot(div_wake_slots_1, inst.uop, lat - 1);
      Assert(ok && "DIV 延迟唤醒槽位不足");
    } else if (op == UOP_FP) {
      bool ok = alloc_iter_slot(fp_wake_slots_1, inst.uop, lat - 1);
      Assert(ok && "FP 延迟唤醒槽位不足");
    }
  }
}

/*
 * comb_awake
 * 功能: 汇总慢速/延迟/快速唤醒源，唤醒 IQ 内等待项并对外广播 iss_awake。
 * 输入依赖: in.prf_awake, mul/div/fp 延迟唤醒当前状态, out.iss2prf->iss_entry, get_latency(op), iqs[]。
 * 输出更新: iqs[]（通过 q.in.wake_valid/wake_pregs + q.comb_wakeup）, out.iss_awake->wake[]。
 * 约束: 唤醒端口数不超过 MAX_WAKEUP_PORTS；仅单周期且非 LOAD/STA 的新发射进入快速唤醒。
 */
void Isu::comb_awake() {
  for (int i = 0; i < MAX_WAKEUP_PORTS; i++) {
    out.iss_awake->wake[i].valid = false;
    out.iss_awake->wake[i].preg = 0;
  }
  int wake_idx = 0;

  // 来源 A: 慢速唤醒 (来自写回阶段：Load / 缓存缺失)
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (!in.prf_awake->wake[i].valid) {
      continue;
    }
    Assert(wake_idx < MAX_WAKEUP_PORTS);
    out.iss_awake->wake[wake_idx].valid = true;
    out.iss_awake->wake[wake_idx].preg = in.prf_awake->wake[i].preg;
    wake_idx++;
  }

  // 来源 B1: 延迟唤醒（DIV countdown 到 0）
  for (int i = 0; i < ISU_DIV_WAKE_SLOT_NUM; i++) {
    const auto &slot = div_wake_slots[i];
    if (slot.valid && slot.countdown == 0) {
      Assert(wake_idx < MAX_WAKEUP_PORTS);
      out.iss_awake->wake[wake_idx].valid = true;
      out.iss_awake->wake[wake_idx].preg = slot.dest_preg;
      wake_idx++;
    }
  }

  // 来源 B2: 延迟唤醒（FP countdown 到 0）
  for (int i = 0; i < ISU_FP_WAKE_SLOT_NUM; i++) {
    const auto &slot = fp_wake_slots[i];
    if (slot.valid && slot.countdown == 0) {
      Assert(wake_idx < MAX_WAKEUP_PORTS);
      out.iss_awake->wake[wake_idx].valid = true;
      out.iss_awake->wake[wake_idx].preg = slot.dest_preg;
      wake_idx++;
    }
  }

  // 来源 B3: 延迟唤醒（MUL 移位寄存器末级）
  for (int i = 0; i < ISU_MUL_WAKE_SLOT_NUM; i++) {
    if (MUL_MAX_LATENCY > 1) {
      const auto &slot = mul_wake_pipe[ISU_MUL_WAKE_DEPTH - 1][i];
      if (slot.valid) {
        Assert(wake_idx < MAX_WAKEUP_PORTS);
        out.iss_awake->wake[wake_idx].valid = true;
        out.iss_awake->wake[wake_idx].preg = slot.dest_preg;
        wake_idx++;
      }
    }
  }

  // 来源 C: 快速唤醒 (本周期发射的单周期 ALU 指令)
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    const auto &entry = out.iss2prf->iss_entry[i];
    if (entry.valid && entry.uop.dest_en) {
      UopType op = decode_uop_type(entry.uop.op);
      int lat = get_latency(op, entry.uop.func7);
      if (lat <= 1 && op != UOP_LOAD && op != UOP_STA) {
        Assert(wake_idx < MAX_WAKEUP_PORTS);
        out.iss_awake->wake[wake_idx].valid = true;
        out.iss_awake->wake[wake_idx].preg = entry.uop.dest_preg;
        wake_idx++;
      }
    }
  }

  // === 统一广播 ===
  // 1. 唤醒所有 IQ
  for (auto &q : iqs) {
    for (int i = 0; i < MAX_WAKEUP_PORTS; i++) {
      q.in.wake_valid[i] = out.iss_awake->wake[i].valid;
      q.in.wake_pregs[i] = out.iss_awake->wake[i].preg;
    }
    q.comb_wakeup();
  }
}

/*
 * comb_flush
 * 功能: 通过 IQ 输入 IO 处理 flush/mispred/clear，并同步清理延迟管线。
 * 输入依赖: in.rob_bcast->flush, in.dec_bcast->{mispred, br_mask, clear_mask}, mul/div/fp *_1, iqs[]。
 * 输出更新: iqs[]（经 q.comb_flush）, mul/div/fp 当前态与下一态。
 * 约束: flush 优先级高于 mispred；clear_mask 在 flush/mispred 处理后作用于存活条目。
 */
void Isu::comb_flush() {
  bool flush_all = in.rob_bcast->flush;
  bool flush_br = (!flush_all) && in.dec_bcast->mispred;
  wire<BR_MASK_WIDTH> br_mask = in.dec_bcast->br_mask;
  wire<BR_MASK_WIDTH> clear = in.dec_bcast->clear_mask;

  for (auto &q : iqs) {
    q.in.flush_all = flush_all;
    q.in.flush_br = flush_br;
    q.in.flush_br_mask = br_mask;
    q.in.clear_mask = clear;
    q.comb_flush();
  }

  if (in.rob_bcast->flush) {
    for (auto &stage : mul_wake_pipe) {
      for (auto &e : stage)
        e = {};
    }
    for (auto &stage : mul_wake_pipe_1) {
      for (auto &e : stage)
        e = {};
    }
    for (auto &e : div_wake_slots)
      e = {};
    for (auto &e : div_wake_slots_1)
      e = {};
    for (auto &e : fp_wake_slots)
      e = {};
    for (auto &e : fp_wake_slots_1)
      e = {};
  } else if (in.dec_bcast->mispred) {
    auto kill_by_mask = [&](auto &slot) {
      if (slot.valid && ((slot.br_mask & br_mask) != 0)) {
        slot = {};
      }
    };
    for (auto &slot : div_wake_slots_1) {
      kill_by_mask(slot);
    }
    for (auto &slot : fp_wake_slots_1) {
      kill_by_mask(slot);
    }
    for (auto &stage : mul_wake_pipe_1) {
      for (auto &slot : stage) {
        kill_by_mask(slot);
      }
    }
  }

  // 清除已解析分支的 br_mask bit（在 flush 之后，只影响存活条目）
  if (clear) {
    auto clear_br_bits = [&](auto &slot) {
      if (slot.valid) {
        slot.br_mask &= ~clear;
      }
    };
    for (auto &slot : div_wake_slots_1) {
      clear_br_bits(slot);
    }
    for (auto &slot : fp_wake_slots_1) {
      clear_br_bits(slot);
    }
    for (auto &stage : mul_wake_pipe_1) {
      for (auto &slot : stage) {
        clear_br_bits(slot);
      }
    }
  }
}

// =================================================================
// 5. 时序逻辑
// =================================================================

void Isu::seq() {
  for (auto &q : iqs) {
    q.seq();
  }

  for (int d = 0; d < ISU_MUL_WAKE_DEPTH; d++) {
    for (int s = 0; s < ISU_MUL_WAKE_SLOT_NUM; s++) {
      mul_wake_pipe[d][s] = mul_wake_pipe_1[d][s];
    }
  }
  for (int i = 0; i < ISU_DIV_WAKE_SLOT_NUM; i++) {
    div_wake_slots[i] = div_wake_slots_1[i];
  }
  for (int i = 0; i < ISU_FP_WAKE_SLOT_NUM; i++) {
    fp_wake_slots[i] = fp_wake_slots_1[i];
  }
}
