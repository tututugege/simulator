#ifndef ICACHE_TOP_H
#define ICACHE_TOP_H

#include "../../front_IO.h" // For icache_in, icache_out
#include "../include/icache_module.h"
#include <memory>

class SimContext; // Forward declaration
class AbstractMmu;
class PtwMemPort;
class PtwWalkPort;
namespace axi_interconnect {
struct ReadMasterPort_t;
}

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
  virtual void set_ptw_mem_port(PtwMemPort *port) { (void)port; }
  virtual void set_ptw_walk_port(PtwWalkPort *port) { (void)port; }
  virtual void set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) {
    (void)port;
  }

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
  enum class AxiFillState : uint8_t {
    IDLE = 0,
    REQ = 1,
    WAIT = 2,
    RESP_READY = 3,
  };
  bool mem_busy = false;
  int mem_latency_cnt = 0;
  AxiFillState axi_fill_state = AxiFillState::IDLE;
  bool axi_fill_stale = false;
  uint32_t axi_fill_base_addr = 0;
  uint32_t axi_fill_chunk_idx = 0;
  uint32_t axi_fill_data[ICACHE_LINE_SIZE / 4] = {};
  uint32_t current_vaddr_reg = 0;
  bool valid_reg = false;
  AbstractMmu *mmu_model = nullptr;
  uint32_t last_satp = 0;
  bool satp_seen = false;
  bool tlb_pending = false;
  uint32_t tlb_pending_vaddr = 0;
  bool tlb_set_pending_comb = false;
  bool tlb_clear_pending_comb = false;
  PtwMemPort *ptw_mem_port = nullptr;
  PtwWalkPort *ptw_walk_port = nullptr;
  axi_interconnect::ReadMasterPort_t *mem_read_port = nullptr;
  bool lookup_pending = false;
  uint32_t lookup_delay = 0;
  uint32_t lookup_index = 0;
  uint32_t lookup_pc = 0;
  uint32_t lookup_seed = 1;

  icache_module_n::ICache &icache_hw;

public:
  TrueICacheTop(icache_module_n::ICache &hw);
  void comb() override;
  void seq() override;
  void set_ptw_mem_port(PtwMemPort *port) override;
  void set_ptw_walk_port(PtwWalkPort *port) override;
  void set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) override;
};

// Implementation using the Simple ICache Model (Ideal P-Memory Access)
class SimpleICacheTop : public ICacheTop {
private:
  AbstractMmu *mmu_model = nullptr;
  uint32_t last_satp = 0;
  bool satp_seen = false;
  bool pending_req_valid = false;
  uint32_t pending_fetch_addr = 0;
  bool pend_on_retry_comb = false;
  bool resp_fire_comb = false;
  PtwMemPort *ptw_mem_port = nullptr;
  PtwWalkPort *ptw_walk_port = nullptr;

public:
  void comb() override;
  void seq() override;
  void set_ptw_mem_port(PtwMemPort *port) override;
  void set_ptw_walk_port(PtwWalkPort *port) override;
  void set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) override;
};

// Factory function to get the singleton instance
ICacheTop *get_icache_instance();

#endif
