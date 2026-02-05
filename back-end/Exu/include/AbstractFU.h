#pragma once

#include "config.h"
#include <deque>
#include <string>

class AbstractFU {
protected:
  std::string name;
  int port_idx;

public:
  AbstractFU(std::string n, int port_idx) : name(n), port_idx(port_idx) {}

  virtual int get_lsu_port_id() { return -1; } // 只给agu和sdu用
  std::string get_name() const { return name; } // For debugging
  virtual ~AbstractFU() {}

  // === 1. 准入检查 (Issue Stage 调用) ===
  // 问：我现在能往你这里塞一条新指令吗？
  virtual bool can_accept() = 0;

  // === 2. 接收指令 (Issue Stage 调用) ===
  // 动作：塞进去
  virtual void accept(InstUop inst) = 0;

  // === 3. 时钟步进 (Execute Stage 调用) ===
  // 动作：内部状态流转一拍
  virtual void tick() = 0;

  // === 4. 获取结果 (Writeback Stage 调用) ===
  // 问：这一拍有做完的指令吗？
  // 注意：返回指针，如果为 nullptr 表示没结果
  virtual InstUop *get_finished_uop() = 0;

  // 动作：结果被取走了，从 FU 里移除它
  virtual void pop_finished() = 0;

  virtual void flush(uint32_t br_mask) = 0;
};

// 固定延迟、可流水化：ALU、MUL、FADD、FMUL
class FixedLatencyFU : public AbstractFU {
protected:
  int latency;
  std::deque<InstUop> pipeline;

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

  void accept(InstUop inst) override {
    // 1. 立即执行功能计算 (Magic Execution)
    //    虽然硬件是在延迟结束后才出结果，但模拟器里为了方便，
    //    通常在入队时就直接算好结果存在 inst 里。
    impl_compute(inst);

    // 2. 标记完成时间 (Timing Simulation)
    inst.cplt_time = sim_time + latency - 1;

    // 3. 推入流水线
    pipeline.push_back(inst);
  }

  void tick() override {
    // 固定延迟单元只需等待时间流逝
  }

  // 新增：清空队列

  void flush(uint32_t br_mask) override {
    if (br_mask == 0xFFFFFFFF) {
      // 全局 Flush
      pipeline.clear();
    } else {
      // 分支恢复：仅移除受影响的指令
      // 使用迭代器遍历 deque
      auto it = pipeline.begin();
      while (it != pipeline.end()) {
        // 假设 InstUop 有 br_mask 域 (依赖掩码) 或 tag 域
        // 检查：如果指令依赖于被误预测的分支
        if ((1 << it->tag) & br_mask) { // 或者 check dependency mask
          it = pipeline.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  InstUop *get_finished_uop() override {
    // For latency=1 FUs, instruction completes in the SAME cycle it's accepted
    // cplt_time = sim_time + latency - 1 = sim_time + 1 - 1 = sim_time
    // The newest instruction (back) is the one that just completed!
    // For latency>1, return the oldest (front) as usual
    if (latency == 1 && !pipeline.empty() && pipeline.back().cplt_time <= sim_time) {
      return &pipeline.back();
    } else if (!pipeline.empty() && pipeline.front().cplt_time <= sim_time) {
      return &pipeline.front();
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
  virtual void impl_compute(InstUop &inst) = 0;
};

class IterativeFU : public AbstractFU {
protected:
  int remaining_cycles;
  InstUop current_inst; // 迭代单元通常是非流水化的，持有一个 latch 即可
  bool busy;
  int max_latency;

public:
  IterativeFU(std::string n, int port_idx, int max_lat)
      : AbstractFU(n, port_idx), remaining_cycles(0), busy(false),
        max_latency(max_lat) {}

  bool can_accept() override {
    return !busy; // 忙则拒收
  }

  void accept(InstUop inst) override {
    // === 1. 功能计算 (Functional) ===
    // 在这里调用虚函数，计算 result = src1 / src2
    impl_compute(inst);

    // === 2. 时序计算 (Timing) ===
    // 在这里调用虚函数，计算 SRT 需要多少周期
    int dyn_latency = calculate_latency(inst);

    // 更新状态
    current_inst = inst; // 拷贝指令到内部寄存器
    current_inst.cplt_time = sim_time + dyn_latency; // 标记完成时刻

    remaining_cycles = dyn_latency;
    busy = true;
  }

  void tick() override {
    if (busy && remaining_cycles > 0) {
      remaining_cycles--;
    }
  }

  InstUop *get_finished_uop() override {
    if (busy && remaining_cycles == 0) {
      return &current_inst;
    }
    return nullptr;
  }

  void pop_finished() override { busy = false; }

  void flush(uint32_t br_mask) override {
    if (!busy)
      return;

    bool kill = false;
    if (br_mask == 0xFFFFFFFF) {
      kill = true;
    } else {
      // 检查当前正在计算的指令是否在错误路径上
      if ((1 << current_inst.tag) & br_mask) {
        kill = true;
      }
    }

    if (kill) {
      busy = false; // 立即释放 FU！
      remaining_cycles = 0;
      // 这样下一拍 comb_ready 就会发现 DIV 变空闲了
    }
  }

protected:
  // 1. 负责算“结果是多少” (Common for all FUs)
  virtual void impl_compute(InstUop &inst) = 0;

  // 2. 负责算“耗时多久” (Specific to IterativeFU)
  virtual int calculate_latency(const InstUop &inst) {
    return max_latency; // 默认返回最大延迟
  }
};
