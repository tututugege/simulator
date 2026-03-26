#pragma once

#include "config.h"
#include "util.h"
#include <deque>
#include <string>

class AbstractFU {
protected:
  std::string name;
  int port_idx;

public:
  AbstractFU(std::string n, int port_idx) : name(n), port_idx(port_idx) {}

  virtual int get_lsu_port_id() { return -1; }  // 只给agu和sdu用
  std::string get_name() const { return name; } // For debugging
  virtual ~AbstractFU() {}

  // === 1. 准入检查 (Issue Stage 调用) ===
  // 问：我现在能往你这里塞一条新指令吗？
  virtual bool can_accept() = 0;

  // === 2. 接收指令 (Issue Stage 调用) ===
  // 动作：塞进去
  virtual void accept(MicroOp inst) = 0;

  // === 3. 时钟步进 (Execute Stage 调用) ===
  // 动作：内部状态流转一拍
  virtual void tick() = 0;

  // === 4. 获取结果 (Writeback Stage 调用) ===
  // 问：这一拍有做完的指令吗？
  // 注意：返回指针，如果为 nullptr 表示没结果
  virtual MicroOp *get_finished_uop() = 0;

  // 动作：结果被取走了，从 FU 里移除它
  virtual void pop_finished() = 0;
  virtual void flush(wire<BR_MASK_WIDTH> br_mask) = 0;
  virtual void clear_br(wire<BR_MASK_WIDTH> clear_mask) = 0;
};

// 固定延迟、可流水化：ALU、MUL、FADD、FMUL
class FixedLatencyFU : public AbstractFU {
protected:
  struct PipeEntry {
    MicroOp uop;
    int64_t done_cycle;
  };

  int latency;
  std::deque<PipeEntry> pipeline;

public:
  FixedLatencyFU(std::string n, int port_idx, int lat)
      : AbstractFU(n, port_idx), latency(lat) {}

  bool can_accept() override {
    // Pipeline depth should be at most (latency - 1)
    // For latency=1 (single-cycle), pipeline must be empty
    // For latency=N, we can have N-1 instructions in flight
    // Fix: For latency=0, we treat it as 1 capacity (immediate completion)
    size_t limit = (latency <= 0) ? 1 : static_cast<size_t>(latency);
    return pipeline.size() < limit;
  }

  void accept(MicroOp inst) override {
    // 1. 立即执行功能计算 (Magic Execution)
    //    虽然硬件是在延迟结束后才出结果，但模拟器里为了方便，
    //    通常在入队时就直接算好结果存在 inst 里。
    impl_compute(inst);

    // 2. 标记完成时间 (Timing Simulation)
    PipeEntry entry{};
    entry.uop = inst;
    entry.done_cycle = sim_time + latency - 1;

    // 3. 推入流水线
    pipeline.push_back(entry);
  }

  void tick() override {
    // 固定延迟单元只需等待时间流逝
  }

  // 新增：清空队列

  void flush(wire<BR_MASK_WIDTH> br_mask) override {
    if (br_mask == static_cast<wire<BR_MASK_WIDTH>>(-1)) {
      pipeline.clear();
    } else {
      auto it = pipeline.begin();
      while (it != pipeline.end()) {
        bool kill_by_mask = (it->uop.br_mask & br_mask) != 0;
        if (kill_by_mask) {
          it = pipeline.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  void clear_br(wire<BR_MASK_WIDTH> clear_mask) override {
    for (auto &inst : pipeline) {
      inst.uop.br_mask &= ~clear_mask;
    }
  }

  MicroOp *get_finished_uop() override {
    // For latency=1 FUs, instruction completes in the SAME cycle it's accepted
    // done_cycle = sim_time + latency - 1 = sim_time + 1 - 1 = sim_time
    // The newest instruction (back) is the one that just completed!
    // For latency>1, return the oldest (front) as usual
    if (latency == 1 && !pipeline.empty() &&
        pipeline.back().done_cycle <= sim_time) {
      return &pipeline.back().uop;
    } else if (!pipeline.empty() && pipeline.front().done_cycle <= sim_time) {
      return &pipeline.front().uop;
    }
    return nullptr;
  }

  void pop_finished() override {
    if (!pipeline.empty()) {
      // For latency=1, we return pipeline.back() in get_finished_uop()
      // So we must also pop from the back to maintain consistency
      if (latency == 1) {
        pipeline.pop_back();
      } else {
        pipeline.pop_front();
      }
    }
  }

protected:
  // === 核心钩子：子类必须实现具体的计算逻辑 ===
  virtual void impl_compute(MicroOp &inst) = 0;
};

class IterativeFU : public AbstractFU {
protected:
  MicroOp current_inst; // 迭代单元通常是非流水化的，持有一个 latch 即可
  int64_t done_cycle;
  bool busy;
  int max_latency;

public:
  IterativeFU(std::string n, int port_idx, int max_lat)
      : AbstractFU(n, port_idx), done_cycle(0), busy(false),
        max_latency(max_lat) {}

  bool can_accept() override {
    return !busy; // 忙则拒收
  }

  void accept(MicroOp inst) override {
    impl_compute(inst);
    int dyn_latency = calculate_latency(inst);
    current_inst = inst;
    done_cycle = sim_time + dyn_latency;
    busy = true;
  }

  void tick() override {
    // 迭代单元的时间推进由 done_cycle 显式表示。
  }

  MicroOp *get_finished_uop() override {
    if (busy && done_cycle <= sim_time) {
      return &current_inst;
    }
    return nullptr;
  }

  void pop_finished() override { busy = false; }

  void flush(wire<BR_MASK_WIDTH> br_mask) override {
    if (!busy)
      return;

    bool kill = false;
    if (br_mask == static_cast<wire<BR_MASK_WIDTH>>(-1)) {
      kill = true;
    } else {
      bool kill_by_mask = (current_inst.br_mask & br_mask) != 0;
      if (kill_by_mask) {
        kill = true;
      }
    }

    if (kill) {
      busy = false;
      done_cycle = 0;
    }
  }

  void clear_br(wire<BR_MASK_WIDTH> clear_mask) override {
    if (busy) {
      current_inst.br_mask &= ~clear_mask;
    }
  }

protected:
  // 1. 负责算“结果是多少” (Common for all FUs)
  virtual void impl_compute(MicroOp &inst) = 0;

  // 2. 负责算“耗时多久” (Specific to IterativeFU)
  virtual int calculate_latency(const MicroOp &inst) {
    return max_latency; // 默认返回最大延迟
  }
};
