#pragma once

#include "AbstractDcache.h"
#include "IO.h"
#include "PeripheralModel.h"
#include "types.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <deque>
using namespace std;

#define PLRU_EVICT

class SimpleCache : public AbstractDcache {
  struct PendingReq {
    MemReqIO req;
    int64_t complete_time;
  };

  static constexpr int OFFSET_WIDTH = 6;
  static constexpr int INDEX_WIDTH = 8;
  static constexpr int WAY_NUM = 4;

  uint32_t cache_tag[WAY_NUM][1 << INDEX_WIDTH];
  bool cache_valid[WAY_NUM][1 << INDEX_WIDTH];
  uint8_t plru_tree[1 << INDEX_WIDTH][(WAY_NUM - 1 + 7) / 8];
  std::deque<PendingReq> pending_reqs;
  static constexpr size_t MAX_PENDING_REQS = 256;
  MemRespIO pending_resp;
  bool stress_mode;
  int write_ready_pct;

  int get_tag(uint32_t addr) { return (addr >> (OFFSET_WIDTH + INDEX_WIDTH)); }

  int get_index(uint32_t addr) {
    return ((addr >> OFFSET_WIDTH) & ((1 << INDEX_WIDTH) - 1));
  }

public:
  SimpleCache(SimContext *ctx) {
    this->ctx = ctx;
    memset(cache_valid, 0, sizeof(cache_valid));
    memset(plru_tree, 0, sizeof(plru_tree));
    HIT_LATENCY = 1;
    MISS_LATENCY = 50;

    stress_mode = false;
    write_ready_pct = 100;
    if (const char *stress_env = std::getenv("SIM_DCACHE_STRESS")) {
      stress_mode = (std::atoi(stress_env) != 0);
    }
    if (const char *wready_env = std::getenv("SIM_DCACHE_WREADY_PCT")) {
      write_ready_pct = std::clamp(std::atoi(wready_env), 1, 100);
    }
  }
  int HIT_LATENCY;
  int MISS_LATENCY;

  SimContext *ctx;
  MemReqIO *lsu_req_io = nullptr;
  MemReqIO *lsu_wreq_io = nullptr;
  MemRespIO *lsu_resp_io = nullptr;
  MemReadyIO *lsu_wready_io = nullptr;

  
  PeripheralModel *peripheral_model = nullptr;
  int cache_select_evict(uint32_t addr);
  void cache_evict(uint32_t addr);
  int cache_access(uint32_t addr);
  int get_data_magic(uint32_t p_addr);
  void update_plru(uint32_t index, int accessed_way);
  bool should_write_ready() const;
  void handle_write_req(const MemReqIO &req);
  void accept_req(const MemReqIO &req);
  void drive_resp(MemRespIO &resp) const;
  void init() override;
  void comb() override;
  void seq() override;
};
