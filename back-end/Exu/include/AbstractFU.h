#pragma once

#include "ExuTypes.h"
#include "config.h"
#include "util.h"
#include <deque>
#include <string>

class AbstractFU {
public:
  struct FuInput {
    wire<1> en = 0;
    wire<1> consume = 0;
    wire<1> flush = 0;
    wire<BR_MASK_WIDTH> flush_mask = 0;
    wire<BR_MASK_WIDTH> clear_mask = 0;
    ExuInst inst;
  };

  struct FuOutput {
    wire<1> ready = 0;
    wire<1> complete = 0;
    ExuInst inst;
  };

  FuInput in;
  FuOutput out;

  AbstractFU(std::string, int) : in(), out() {}
  virtual ~AbstractFU() = default;

  // 三段组合逻辑：控制/发射/消费，避免同一个 comb 函数被多次调用。
  virtual void comb_ctrl() = 0;
  virtual void comb_issue() = 0;
  virtual void comb_consume() = 0;
  virtual void seq() = 0;
};

// 固定延迟且可流水化：ALU、MUL
class FixedLatencyFU : public AbstractFU {
private:
  struct PipeEntry {
    ExuInst uop;
    int64_t done_cycle;
  };

  int latency;
  std::deque<PipeEntry> pipeline;

  ExuInst *peek_finished_impl() {
    // 延迟为 1 时，指令与接收同拍完成：
    // done_cycle = sim_time + latency - 1 = sim_time
    // 此时最新完成项在队尾。
    // 延迟大于 1 时，按常规从队首取最早完成项。
    if (latency == 1 && !pipeline.empty() &&
        pipeline.back().done_cycle <= sim_time) {
      return &pipeline.back().uop;
    } else if (!pipeline.empty() && pipeline.front().done_cycle <= sim_time) {
      return &pipeline.front().uop;
    }
    return nullptr;
  }

  void pop_finished_impl() {
    if (!pipeline.empty()) {
      // 延迟为 1 时，out.complete 对应 pipeline.back()，
      // 这里必须从队尾弹出以保持一致。
      if (latency == 1) {
        pipeline.pop_back();
      } else {
        pipeline.pop_front();
      }
    }
  }

public:
  FixedLatencyFU(std::string n, int port_idx, int lat)
      : AbstractFU(n, port_idx), latency(lat) {}

  void seq() override {
    // 固定延迟单元只需等待时间流逝
  }

  void comb_ctrl() override {
    if (in.flush) {
      if (in.flush_mask == static_cast<wire<BR_MASK_WIDTH>>(-1)) {
        pipeline.clear();
      } else {
        auto it = pipeline.begin();
        while (it != pipeline.end()) {
          bool kill_by_mask = (it->uop.br_mask & in.flush_mask) != 0;
          if (kill_by_mask) {
            it = pipeline.erase(it);
          } else {
            ++it;
          }
        }
      }
    }
    if (in.clear_mask) {
      for (auto &entry : pipeline) {
        entry.uop.br_mask &= ~in.clear_mask;
      }
    }
    size_t limit = (latency <= 0) ? 1 : static_cast<size_t>(latency);
    out.ready = pipeline.size() < limit;
    ExuInst *finished = peek_finished_impl();
    out.complete = (finished != nullptr);
    if (finished != nullptr) out.inst = *finished;
  }

  void comb_issue() override {
    size_t limit = (latency <= 0) ? 1 : static_cast<size_t>(latency);
    if (in.en && pipeline.size() < limit) {
      ExuInst inst = in.inst;
      impl_compute(inst);
      PipeEntry entry{};
      entry.uop = inst;
      entry.done_cycle = sim_time + latency - 1;
      pipeline.push_back(entry);
    }
    out.ready = pipeline.size() < limit;
    ExuInst *finished = peek_finished_impl();
    out.complete = (finished != nullptr);
    if (finished != nullptr) out.inst = *finished;
  }

  void comb_consume() override {
    ExuInst *finished = peek_finished_impl();
    if (in.consume && finished != nullptr) {
      pop_finished_impl();
    }
    size_t limit = (latency <= 0) ? 1 : static_cast<size_t>(latency);
    out.ready = pipeline.size() < limit;
    finished = peek_finished_impl();
    out.complete = (finished != nullptr);
    if (finished != nullptr) out.inst = *finished;
  }

protected:
  // === 核心钩子：子类必须实现具体的计算逻辑 ===
  virtual void impl_compute(ExuInst &inst) = 0;
};

class IterativeFU : public AbstractFU {
private:
  ExuInst current_inst; // 迭代单元通常是非全流水化的，持有一个寄存槽即可
  int64_t done_cycle;
  bool busy;
  int max_latency;

  ExuInst *peek_finished_impl() {
    if (busy && done_cycle <= sim_time) {
      return &current_inst;
    }
    return nullptr;
  }

  void pop_finished_impl() { busy = false; }

public:
  IterativeFU(std::string n, int port_idx, int max_lat)
      : AbstractFU(n, port_idx), done_cycle(0), busy(false),
        max_latency(max_lat) {}

  void seq() override {
    // 迭代单元的时间推进由 done_cycle 显式表示。
  }

  void comb_ctrl() override {
    if (in.flush) {
      if (busy) {
        bool kill = false;
        if (in.flush_mask == static_cast<wire<BR_MASK_WIDTH>>(-1)) {
          kill = true;
        } else if ((current_inst.br_mask & in.flush_mask) != 0) {
          kill = true;
        }
        if (kill) {
          busy = false;
          done_cycle = 0;
        }
      }
    }
    if (in.clear_mask) {
      if (busy) current_inst.br_mask &= ~in.clear_mask;
    }
    out.ready = !busy;
    ExuInst *finished = peek_finished_impl();
    out.complete = (finished != nullptr);
    if (finished != nullptr) out.inst = *finished;
  }

  void comb_issue() override {
    if (in.en && !busy) {
      ExuInst inst = in.inst;
      impl_compute(inst);
      int dyn_latency = calculate_latency(inst);
      current_inst = inst;
      done_cycle = sim_time + dyn_latency;
      busy = true;
    }
    out.ready = !busy;
    ExuInst *finished = peek_finished_impl();
    out.complete = (finished != nullptr);
    if (finished != nullptr) out.inst = *finished;
  }

  void comb_consume() override {
    ExuInst *finished = peek_finished_impl();
    if (in.consume && finished != nullptr) {
      pop_finished_impl();
    }
    out.ready = !busy;
    finished = peek_finished_impl();
    out.complete = (finished != nullptr);
    if (finished != nullptr) out.inst = *finished;
  }

protected:
  // 1. 负责计算“结果值”
  virtual void impl_compute(ExuInst &inst) = 0;

  // 2. 负责计算“执行延迟”（仅迭代单元使用）
  virtual int calculate_latency(const ExuInst &inst) {
    return max_latency; // 默认返回最大延迟
  }
};
