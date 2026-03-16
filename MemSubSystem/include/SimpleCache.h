#pragma once

#include "AbstractDcache.h"
#include "IO.h"
#include "types.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <deque>
#include "config.h"

class SimpleCache : public AbstractDcache {
  struct PendingLoadReq {
    uint32_t addr = 0;
    MicroOp uop = {};
    size_t req_id = 0;
    int64_t complete_time;
  };

  static constexpr int OFFSET_WIDTH = 6;
  static constexpr int INDEX_WIDTH = 8;
  static constexpr int WAY_NUM = 4;

  uint32_t cache_tag[WAY_NUM][1 << INDEX_WIDTH];
  bool cache_valid[WAY_NUM][1 << INDEX_WIDTH];
  uint8_t plru_tree[1 << INDEX_WIDTH][(WAY_NUM - 1 + 7) / 8];
  std::deque<PendingLoadReq> pending_load_reqs[LSU_LDU_COUNT];
  static constexpr size_t MAX_PENDING_REQS = 256;
  SimContext *ctx = nullptr;

  int get_tag(uint32_t addr) { return (addr >> (OFFSET_WIDTH + INDEX_WIDTH)); }
  int get_index(uint32_t addr) {
    return ((addr >> OFFSET_WIDTH) & ((1 << INDEX_WIDTH) - 1));
  }

public:
  void bind_context(SimContext *c) { ctx = c; }
  LsuDcacheIO *lsu2dcache = nullptr;
  DcacheLsuIO *dcache2lsu = nullptr;

  SimpleCache() {
    memset(cache_valid, 0, sizeof(cache_valid));
    memset(plru_tree, 0, sizeof(plru_tree));
    HIT_LATENCY = 2;
    MISS_LATENCY = 140;
  }

  int HIT_LATENCY;
  int MISS_LATENCY;
  int cache_select_evict(uint32_t addr);
  void cache_evict(uint32_t addr);
  int cache_access(uint32_t addr, bool &is_miss);
  void update_plru(uint32_t index, int accessed_way);
  void handle_store_req(const StoreReq &req, StoreResp &resp);
  void accept_load_req(int port, const LoadReq &req);
  void drive_load_resp(int port, LoadResp &resp);

  void init() override;
  void comb() override;
  void seq() override;
};
