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

void Isu::init() {

  iqs.clear();
  configs.clear();

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
      dynamic_cfg.ports.push_back({
          port_info.port_idx,    // 物理端口号 (例如 1)
          port_info.support_mask // 能力掩码 (例如 ALU | DIV)
      });
    }

    // 3. 创建 IQ
    add_iq(dynamic_cfg);
  }
}


int Isu::get_latency(UopType uop) {
  if (uop == UOP_MUL)
    return MUL_MAX_LATENCY; // 乘法指令延迟
  if (uop == UOP_DIV)
    return DIV_MAX_LATENCY; // 除法指令延迟
  return 1;                 // 其他指令认为是单周期，走 Fast Wakeup
}

// =================================================================
// 1. comb_ready: 告诉 Dispatch 每个 IQ 有多少空位
// =================================================================
void Isu::comb_ready() {
  for (int i = 0; i < IQ_NUM; i++) {
    // 直接用 i 索引，因为我们保证了 iqs[i].id == i
    out.iss2dis->ready_num[i] = iqs[i].size - iqs[i].count;
  }
}

// =================================================================
// 2. comb_enq: 批量入队
// =================================================================
void Isu::comb_enq() {
  for (int i = 0; i < IQ_NUM; i++) {
    auto &q = iqs[i];
    int max_w = configs[i].dispatch_width; // 获取该 IQ 配置的最大入队宽

    // 遍历该 IQ 的所有输入通道
    for (int w = 0; w < max_w; w++) {
      // 使用新接口结构 req[i][w]
      if (in.dis2iss->req[i][w].valid) {
        InstUop &uop = in.dis2iss->req[i][w].uop;

        // === 加载指令 (Load) 依赖掩码生成 ===
        if (i == IQ_LD) {
          // 扫描 STA 队列
          for (const auto &entry : iqs[IQ_STA].get_entries_1()) {
            if (entry.valid)
              uop.pre_sta_mask |= (1ULL << entry.uop.stq_idx);
          }
          // 修正：清除本周期正在发射的 STA 掩码（防止竞争状态）
          for (int k = 0; k < LSU_STA_COUNT; k++) {
            if (out.iss2prf->iss_entry[IQ_STA_PORT_BASE + k].valid) {
              uop.pre_sta_mask &=
                  ~(1ULL << out.iss2prf->iss_entry[IQ_STA_PORT_BASE + k]
                             .uop.stq_idx);
            }
          }

          }

        // 修正：检查本周期的寄存器唤醒 (快速/慢速唤醒)
        // out.iss_awake 包含在 comb_awake 中生成的所有唤醒信号
        for (int k = 0; k < MAX_WAKEUP_PORTS; k++) {
          if (out.iss_awake->wake[k].valid) {
            uint32_t preg = out.iss_awake->wake[k].preg;
            if (uop.src1_en && uop.src1_preg == preg)
              uop.src1_busy = false;
            if (uop.src2_en && uop.src2_preg == preg)
              uop.src2_busy = false;
          }
        }

        InstEntry new_entry;
        new_entry.uop = uop;
        new_entry.valid = true;
        // 记录入队时间
        new_entry.uop.enqueue_time = sim_time;
        // 调试已移除

        // 入队
        int success = q.enqueue(new_entry);
        Assert(success && "发射队列溢出！Dispatch 逻辑故障！");
      }
    }
  }
}

// =================================================================
// 3. comb_issue: 调度 + 延迟唤醒管理
// =================================================================
void Isu::comb_issue() {

  for (int i = 0; i < ISSUE_WIDTH; i++) {
    out.iss2prf->iss_entry[i].valid = false;
  }

  for (size_t i = 0; i < iqs.size(); i++) {
    IssueQueue &q = iqs[i];

    // 调用新的 schedule
    auto scheduled_pairs = q.schedule();

    std::vector<int> committed_indices;

    for (auto &pair : scheduled_pairs) {
      int entry_idx = pair.first;
      int phys_port = pair.second; // 物理端口号

      // 检查下游反压
      uint64_t req_bit = (1ULL << q.entry[entry_idx].uop.op);
      if (in.exe2iss->ready[phys_port] &&
          (in.exe2iss->fu_ready_mask[phys_port] & req_bit) &&
          !in.rob_bcast->flush && !in.dec_bcast->mispred) {

        // 发射到指定的物理端口
        out.iss2prf->iss_entry[phys_port] = q.entry[entry_idx];

        // 记录成功发射的索引
        committed_indices.push_back(entry_idx);
      }
    }

    // 提交
    q.commit_issue(committed_indices);
  }
}

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
      int lat = get_latency(inst.uop.op);

      if (lat > 1) {
        LatencyEntry new_entry;
        new_entry.valid = true;
        new_entry.countdown = lat - 1;
        new_entry.dest_preg = inst.uop.dest_preg;
        new_entry.tag = inst.uop.tag;

        latency_pipe_1.push_back(new_entry);
      }
    }
  }
}

// =================================================================
// 4. comb_awake: 统一唤醒逻辑
// =================================================================
void Isu::comb_awake() {
  bool valid_flags[MAX_WAKEUP_PORTS];
  uint32_t pregs[MAX_WAKEUP_PORTS];
  int idx = 0;

  // 来源 A: 慢速唤醒 (来自写回阶段：Load / 缓存缺失)
  for (int i = 0; i < LSU_LOAD_WB_WIDTH; i++) {
    if (in.prf_awake->wake[i].valid) {
      valid_flags[idx] = true;
      pregs[idx] = in.prf_awake->wake[i].preg;
      idx++;
    }
  }

  // 来源 B: 延迟唤醒 (乘法/除法完成)
  // 检查 latency_pipe 中倒计时为 0 的条目
  for (const auto &le : latency_pipe) {
    if (le.valid && le.countdown == 0) {
      valid_flags[idx] = true;
      pregs[idx] = le.dest_preg;
      idx++;
    }
  }

  // 来源 C: 快速唤醒 (本周期发射的单周期 ALU 指令)
  // 遍历所有发射端口
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    const auto &entry = out.iss2prf->iss_entry[i];
    if (entry.valid && entry.uop.dest_en) {
      int lat = get_latency(entry.uop.op);
      // 只有 lat <= 1 (单周期) 的指令才做快速唤醒
      // Load 指令暂时不做推测唤醒
      if (lat <= 1 && entry.uop.op != UOP_LOAD) {
        valid_flags[idx] = true;
        pregs[idx] = entry.uop.dest_preg;
        idx++;
      }
    }
  }

  Assert(idx <= MAX_WAKEUP_PORTS);
  // === 统一广播 ===
  // 1. 唤醒所有 IQ
  for (auto &q : iqs) {
    q.wakeup(valid_flags, pregs, idx);
  }

  // 2. Load 依赖唤醒 (Store Mask)
  // 直接使用常量索引 IQ_LD
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    if (out.iss2prf->iss_entry[IQ_STA_PORT_BASE + i].valid) {
      iqs[IQ_LD].clear_store_mask(
          out.iss2prf->iss_entry[IQ_STA_PORT_BASE + i].uop.stq_idx, true);
    }
  }

  // 3. 输出给外部 (iss_awake) - 用于通知 rename table 等
  // 将上面收集到的唤醒信号输出
  for (int i = 0; i < MAX_WAKEUP_PORTS; i++) {
    if (i < idx) {
      out.iss_awake->wake[i].valid = true;
      out.iss_awake->wake[i].preg = pregs[i];
    } else {
      out.iss_awake->wake[i].valid = false;
    }
  }
}

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
      if ((1ULL << it->tag) & in.dec_bcast->br_mask) {
        it = latency_pipe_1.erase(it);
      } else {
        ++it;
      }
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

IssueIO Isu::get_hardware_io() {
  IssueIO hardware;

  // --- Inputs ---
  for (int j = 0; j < IQ_NUM; j++) {
    for (int k = 0; k < MAX_IQ_DISPATCH_WIDTH; k++) {
      hardware.from_dis.valid[j][k] = in.dis2iss->req[j][k].valid;
      hardware.from_dis.uop[j][k]   = DisIssUop::filter(in.dis2iss->req[j][k].uop);
    }
  }
  hardware.from_back.flush = in.rob_bcast->flush;

  // --- Outputs ---
  for (int j = 0; j < IQ_NUM; j++) {
    hardware.to_dis.ready_num[j] = out.iss2dis->ready_num[j];
  }
  for (int i = 0; i < ISSUE_WIDTH; i++) {
    hardware.to_exe.valid[i] = out.iss2prf->iss_entry[i].valid;
    hardware.to_exe.uop[i]   = IssExeUop::filter(out.iss2prf->iss_entry[i].uop);
  }
  for (int i = 0; i < MAX_WAKEUP_PORTS; i++) {
    hardware.awake_bus.wake_valid[i] = out.iss_awake->wake[i].valid;
    hardware.awake_bus.wake_preg[i]  = out.iss_awake->wake[i].preg;
  }

  return hardware;
}
