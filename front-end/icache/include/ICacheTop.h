#ifndef ICACHE_TOP_H
#define ICACHE_TOP_H

#include "../../front_IO.h" // For icache_in, icache_out
#include "../include/icache_module.h"
#include <memory>

class SimContext; // Forward declaration

// Abstract Base Class for ICache Top-level Logic
class ICacheTop {
protected:
  struct icache_in *in = nullptr;
  struct icache_out *out = nullptr;
  SimContext *ctx = nullptr;

  // Local performance counters (deltas for the current cycle)
  uint64_t access_delta = 0;
  uint64_t miss_delta = 0;

public:
  void setIO(struct icache_in *in_ptr, struct icache_out *out_ptr) {
    in = in_ptr;
    out = out_ptr;
  }

  void setContext(SimContext *c) { ctx = c; }

  virtual void comb() = 0;
  virtual void seq() = 0;

  void syncPerf();

  // Template method for execution step
  virtual void step() {
    // Reset deltas at start of step? Or rely on syncPerf to clear them at end?
    // Clearing at end is safer if step is called multiple times? No, step is
    // once per cycle. But let's initialize deltas to 0 at constructor, and
    // clear in syncPerf.

    comb();
    if (!in->run_comb_only) {
      seq();
    }
    syncPerf();
  }

  virtual ~ICacheTop() {}
};

// Implementation using the True ICache Module (Detailed Simulation)
class TrueICacheTop : public ICacheTop {
private:
  bool mem_busy = false;
  int mem_latency_cnt = 0;
  uint32_t current_vaddr_reg = 0;
  bool valid_reg = false;

  ICache &icache_hw;

public:
  TrueICacheTop(ICache &hw);
  void comb() override;
  void seq() override;
};

// Implementation using the Simple ICache Model (Ideal P-Memory Access)
class SimpleICacheTop : public ICacheTop {
public:
  void comb() override;
  void seq() override;
};

// Factory function to get the singleton instance
ICacheTop *get_icache_instance();

#endif