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
  latency_pipe.clear();
  latency_pipe_1.clear();
  for (int i = 0; i < IQ_NUM; i++) {
    committed_indices_buf[i].clear();
    committed_indices_buf[i].reserve(GLOBAL_IQ_CONFIG[i].size);
  }

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
 * 功能: 组合阶段开始时复制各 IQ 与延迟唤醒管线状态到 *_1 工作副本。
 * 输入依赖: iqs[].entry/count, latency_pipe。
 * 输出更新: iqs[].entry_1/count_1, latency_pipe_1。
 * 约束: 仅做状态镜像，不执行入队/发射/唤醒决策。
 */
void Isu::comb_begin() {
  for (auto &q : iqs) {
    q.entry_1 = q.entry;
    q.count_1 = q.count;
  }
  latency_pipe_1 = latency_pipe;
}


int Isu::get_latency(UopType uop) {
  if (uop == UOP_MUL)
    return MUL_MAX_LATENCY; // 乘法指令延迟
  if (uop == UOP_DIV)
    return DIV_MAX_LATENCY; // 除法指令延迟
  return 1;                 // 其他指令认为是单周期，走 Fast Wakeup
}

/*
 * comb_ready
 * 功能: 计算每个 IQ 的剩余容量并反馈给 Dispatch。
 * 输入依赖: iqs[i].size/count。
 * 输出更新: out.iss2dis->ready_num[i]。
 * 约束: 仅报告容量，不改变 IQ 内部状态。
 */
void Isu::comb_ready() {
  for (int i = 0; i < IQ_NUM; i++) {
    // 直接用 i 索引，因为我们保证了 iqs[i].id == i
    out.iss2dis->ready_num[i] = iqs[i].size - iqs[i].count;
  }
}

/*
 * comb_enq
 * 功能: 接收 dis2iss 请求并批量写入对应 IQ。
 * 输入依赖: in.dis2iss->req[][], configs[i].dispatch_width, out.iss_awake（用于入队前叠加唤醒）, 当前 IQ 状态。
 * 输出更新: iqs[i].entry_1/count_1（通过 enqueue 写入）。
 * 约束: 仅对 valid 请求入队；入队失败视为设计错误并触发 Assert。
 */
void Isu::comb_enq() {
  for (int i = 0; i < IQ_NUM; i++) {
    auto &q = iqs[i];
    int max_w = configs[i].dispatch_width; // 获取该 IQ 配置的最大入队宽

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
        // 记录入队时间 (已移除)
        // 调试已移除

        // 入队
        int success = q.enqueue(new_entry);
        Assert(success && "发射队列溢出！Dispatch 逻辑故障！");
      }
    }
  }
}

/*
 * comb_issue
 * 功能: 对各 IQ 执行调度选择并向 PRF/EXU 发射指令。
 * 输入依赖: q.schedule() 结果, in.exe2iss->ready, in.exe2iss->fu_ready_mask, in.rob_bcast->flush, in.dec_bcast->mispred。
 * 输出更新: out.iss2prf->iss_entry[], 各 IQ 的已发射条目提交结果（commit_issue）。
 * 约束: 发射需同时满足端口 ready 与 FU 能力掩码；flush/mispred 时禁止新发射。
 */
void Isu::comb_issue() {

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    out.iss2prf->iss_entry[i].valid = false;
  }

  for (size_t i = 0; i < iqs.size(); i++) {
    IssueQueue &q = iqs[i];

    // 调用新的 schedule
    auto scheduled_pairs = q.schedule();
    auto &committed_indices = committed_indices_buf[i];
    committed_indices.clear();

    for (auto &pair : scheduled_pairs) {
      int entry_idx = pair.first;
      int phys_port = pair.second; // 物理端口号

      // 检查下游反压
      uint64_t req_bit = (1ULL << static_cast<uint32_t>(q.entry[entry_idx].uop.op));
      if (in.exe2iss->ready[phys_port] &&
          (in.exe2iss->fu_ready_mask[phys_port] & req_bit) &&
          !in.rob_bcast->flush && !in.dec_bcast->mispred) {

        // 发射到指定的物理端口
        out.iss2prf->iss_entry[phys_port].valid = true;
        out.iss2prf->iss_entry[phys_port].uop = q.entry[entry_idx].uop.to_iss_prf_uop();

        // 记录成功发射的索引
        committed_indices.push_back(entry_idx);
      }
    }

    // 提交
    q.commit_issue(committed_indices);
  }
}

/*
 * comb_calc_latency_next
 * 功能: 计算下一拍延迟唤醒管线（旧条目倒计时 + 新发射多周期条目入管线）。
 * 输入依赖: latency_pipe, out.iss2prf->iss_entry[], get_latency(op)。
 * 输出更新: latency_pipe_1。
 * 约束: 仅 latency>1 且 dest_en 指令进入延迟管线；countdown==0 的旧条目不再保留到下一拍。
 */
void Isu::comb_calc_latency_next() {
  // 清空 Next State (重新计算)
  latency_pipe_1.clear();

  // === Part 1: 处理旧指令 (Countdown) ===
  // 逻辑：读取 Current State，倒计时 > 0 的保留并减 1
  for (const auto &entry : latency_pipe) {
    // 如果倒计时为 0，说明本周期已经在 comb_awake 里唤醒了，
    // 下一周期它就消失了，所以这里只处理 > 0 的
    if (entry.valid && entry.countdown > 0) {
      LatencyEntry next_entry = entry;
      next_entry.countdown--;
      latency_pipe_1.push_back(next_entry);
    }
  }

  // === Part 2: 处理新指令 (New Issue) ===
  // 逻辑：直接从 Output Port 读取刚刚发射的指令
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    const auto &inst = out.iss2prf->iss_entry[i];

    if (inst.valid && inst.uop.dest_en) {
      UopType op = decode_uop_type(inst.uop.op);
      int lat = get_latency(op);

      if (lat > 1) {
        LatencyEntry new_entry;
        new_entry.valid = true;
        new_entry.countdown = lat - 1;
        new_entry.dest_preg = inst.uop.dest_preg;
        new_entry.br_mask = inst.uop.br_mask;
        new_entry.rob_idx = inst.uop.rob_idx;
        new_entry.rob_flag = inst.uop.rob_flag;

        latency_pipe_1.push_back(new_entry);
      }
    }
  }
}

/*
 * comb_awake
 * 功能: 汇总慢速/延迟/快速唤醒源，唤醒 IQ 内等待项并对外广播 iss_awake。
 * 输入依赖: in.prf_awake, latency_pipe, out.iss2prf->iss_entry, get_latency(op), iqs[]。
 * 输出更新: iqs[].wakeup(...) 结果, out.iss_awake->wake[]。
 * 约束: 唤醒端口数不超过 MAX_WAKEUP_PORTS；仅单周期且非 LOAD/STA 的新发射进入快速唤醒。
 */
void Isu::comb_awake() {
  std::vector<uint32_t> pregs;
  pregs.reserve(MAX_WAKEUP_PORTS); // 预分配避免重复分配

  // 来源 A: 慢速唤醒 (来自写回阶段：Load / 缓存缺失)
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (in.prf_awake->wake[i].valid) {
      pregs.push_back(in.prf_awake->wake[i].preg);
    }
  }

  // 来源 B: 延迟唤醒 (乘法/除法完成)
  for (const auto &le : latency_pipe) {
    if (le.valid && le.countdown == 0) {
      pregs.push_back(le.dest_preg);
    }
  }

  // 来源 C: 快速唤醒 (本周期发射的单周期 ALU 指令)
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    const auto &entry = out.iss2prf->iss_entry[i];
    if (entry.valid && entry.uop.dest_en) {
      UopType op = decode_uop_type(entry.uop.op);
      int lat = get_latency(op);
      if (lat <= 1 && op != UOP_LOAD && op != UOP_STA) {
        pregs.push_back(entry.uop.dest_preg);
      }
    }
  }

  Assert(pregs.size() <= MAX_WAKEUP_PORTS);
  
  // === 统一广播 ===
  // 1. 唤醒所有 IQ
  for (auto &q : iqs) {
    q.wakeup(pregs);
  }



  // 3. 输出给外部 (iss_awake) - 用于通知 rename table 等
  for (size_t i = 0; i < MAX_WAKEUP_PORTS; i++) {
    if (i < pregs.size()) {
      out.iss_awake->wake[i].valid = true;
      out.iss_awake->wake[i].preg = pregs[i];
    } else {
      out.iss_awake->wake[i].valid = false;
    }
  }
}

/*
 * comb_flush
 * 功能: 处理 flush/mispred 的 IQ 与延迟管线清理，并清除已解析分支 bit。
 * 输入依赖: in.rob_bcast->flush, in.dec_bcast->{mispred, br_mask, clear_mask}, latency_pipe_1, iqs[]。
 * 输出更新: iqs[]（flush_all/flush_br/clear_br 后状态）, latency_pipe/latency_pipe_1。
 * 约束: flush 优先级高于 mispred；clear_mask 在 flush/mispred 处理后作用于存活条目。
 */
void Isu::comb_flush() {
  if (in.rob_bcast->flush) {
    for (auto &q : iqs)
      q.flush_all();
    latency_pipe.clear();
    latency_pipe_1.clear();
  } else if (in.dec_bcast->mispred) {
    for (auto &q : iqs)
      q.flush_br(in.dec_bcast->br_mask);

    // 清空延迟流水线管道条目
    auto it = latency_pipe_1.begin();
    while (it != latency_pipe_1.end()) {
      bool match_mask = (it->br_mask & in.dec_bcast->br_mask) != 0;
      if (match_mask) {
        it = latency_pipe_1.erase(it);
        continue;
      }
      ++it;
    }
  }

  // 清除已解析分支的 br_mask bit（在 flush 之后，只影响存活条目）
  wire<BR_MASK_WIDTH> clear = in.dec_bcast->clear_mask;
  if (clear) {
    for (auto &q : iqs)
      q.clear_br(clear);
    for (auto &entry : latency_pipe_1) {
      entry.br_mask &= ~clear;
    }
  }
}

// =================================================================
// 5. 时序逻辑
// =================================================================

void Isu::seq() {
  for (auto &q : iqs) {
    q.tick();
  }

  latency_pipe = latency_pipe_1;
}
