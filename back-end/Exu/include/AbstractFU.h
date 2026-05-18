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
  int issue_port_idx = -1;

  AbstractFU(std::string, int port_idx) : in(), out(), issue_port_idx(port_idx) {}
  virtual ~AbstractFU() = default;

  virtual void comb_begin() = 0;
  virtual void comb_ctrl() = 0;
  virtual void comb_issue() = 0;
  virtual void comb_consume() = 0;
  virtual void seq() = 0;
};

// 确定单周期单元：ALU/BRU/AGU/SDU/CSR。
class SingleCycleFU : public AbstractFU {
public:
  SingleCycleFU(std::string n, int port_idx) : AbstractFU(n, port_idx) {
    out.ready = true;
  }

  void comb_begin() override {
    out.complete = false;
  }

  void seq() override {}

  void comb_ctrl() override {}

  void comb_issue() override {
    if (in.en) {
      ExuInst inst = in.inst;
      impl_compute(inst);
      out.complete = true;
      out.inst = inst;
    }
  }

  void comb_consume() override { out.ready = (!in.en) || in.consume; }

protected:
  virtual void impl_compute(ExuInst &inst) = 0;
};

// 固定延迟且可流水化：MUL（lat>1）。
class FixedLatencyFU : public AbstractFU {
private:
  struct PipeEntry {
    ExuInst uop;
    int64_t done_cycle;
  };

  int latency;
  std::deque<PipeEntry> pipeline;
  std::deque<PipeEntry> pipeline_1;

  bool has_finished(const std::deque<PipeEntry> &pipe) const {
    if (!pipe.empty() && pipe.front().done_cycle <= sim_time) {
      return true;
    }
    return false;
  }

  ExuInst get_finished_inst(const std::deque<PipeEntry> &pipe) const {
    return pipe.front().uop;
  }

  void pop_finished(std::deque<PipeEntry> &pipe) {
    if (!has_finished(pipe)) {
      return;
    }
    pipe.pop_front();
  }

public:
  FixedLatencyFU(std::string n, int port_idx, int lat)
      : AbstractFU(n, port_idx), latency(lat) {
    out.ready = true;
  }

  void comb_begin() override {
    pipeline_1 = pipeline;
    size_t limit = (latency <= 0) ? 1 : static_cast<size_t>(latency);
    out.ready = pipeline.size() < limit;
    out.complete = has_finished(pipeline);
    if (out.complete) {
      out.inst = get_finished_inst(pipeline);
    }
  }

  void seq() override { pipeline = pipeline_1; }

  void comb_ctrl() override {
    if (in.flush) {
      if (in.flush_mask == static_cast<wire<BR_MASK_WIDTH>>(-1)) {
        pipeline_1.clear();
      } else {
        auto it = pipeline_1.begin();
        while (it != pipeline_1.end()) {
          bool kill_by_mask = (it->uop.br_mask & in.flush_mask) != 0;
          if (kill_by_mask) {
            it = pipeline_1.erase(it);
          } else {
            ++it;
          }
        }
      }
    }
    if (in.clear_mask) {
      for (auto &entry : pipeline_1) {
        entry.uop.br_mask &= ~in.clear_mask;
      }
    }
  }

  void comb_issue() override {
    size_t limit = (latency <= 0) ? 1 : static_cast<size_t>(latency);
    if (in.en && pipeline_1.size() < limit) {
      ExuInst inst = in.inst;
      impl_compute(inst);
      PipeEntry entry{};
      entry.uop = inst;
      entry.done_cycle = sim_time + latency - 1;
      pipeline_1.push_back(entry);
    }

    out.complete = has_finished(pipeline_1);
    if (out.complete) {
      out.inst = get_finished_inst(pipeline_1);
    }
  }

  void comb_consume() override {
    if (in.consume) {
      pop_finished(pipeline_1);
    }
    size_t limit = (latency <= 0) ? 1 : static_cast<size_t>(latency);
    out.ready = pipeline_1.size() < limit;
  }

protected:
  virtual void impl_compute(ExuInst &inst) = 0;
};

class IterativeFU : public AbstractFU {
private:
  ExuInst current_inst;
  ExuInst current_inst_1;
  int64_t done_cycle;
  int64_t done_cycle_1;
  bool busy;
  bool busy_1;
  int max_latency;

public:
  IterativeFU(std::string n, int port_idx, int max_lat)
      : AbstractFU(n, port_idx), done_cycle(0), done_cycle_1(0), busy(false),
        busy_1(false), max_latency(max_lat) {
    out.ready = true;
  }

  void comb_begin() override {
    current_inst_1 = current_inst;
    done_cycle_1 = done_cycle;
    busy_1 = busy;
    out.ready = !busy;
    out.complete = (busy && done_cycle <= sim_time);
    if (out.complete) {
      out.inst = current_inst;
    }
  }

  void seq() override {
    current_inst = current_inst_1;
    done_cycle = done_cycle_1;
    busy = busy_1;
  }

  void comb_ctrl() override {
    if (in.flush && busy_1 &&
        (in.flush_mask == static_cast<wire<BR_MASK_WIDTH>>(-1) ||
         (current_inst_1.br_mask & in.flush_mask) != 0)) {
      busy_1 = false;
      done_cycle_1 = 0;
    }
    if (in.clear_mask && busy_1) {
      current_inst_1.br_mask &= ~in.clear_mask;
    }
  }

  void comb_issue() override {
    if (in.en && !busy_1) {
      ExuInst inst = in.inst;
      impl_compute(inst);
      int dyn_latency = calculate_latency(inst);
      current_inst_1 = inst;
      done_cycle_1 = sim_time + dyn_latency;
      busy_1 = true;
    }

    out.complete = (busy_1 && done_cycle_1 <= sim_time);
    if (out.complete) {
      out.inst = current_inst_1;
    }
  }

  void comb_consume() override {
    if (in.consume && busy_1 && done_cycle_1 <= sim_time) {
      busy_1 = false;
      done_cycle_1 = 0;
    }
    out.ready = !busy_1;
  }

protected:
  virtual void impl_compute(ExuInst &inst) = 0;
  virtual int calculate_latency(const ExuInst &inst) { return max_latency; }
};
