#pragma once

#include "AbstractDcache.h"
#include "IO.h"
#include "types.h"
#include <cstdint>
#include <cstring>
#include <deque>
using namespace std;

#define PLRU_EVICT

class SimpleCache : public AbstractDcache {
  struct PendingReq {
    MemReqIO req;
    int64_t complete_time;
  };

  static constexpr int OFFSET_WIDTH = DCACHE_OFFSET_BITS;
  static constexpr int INDEX_WIDTH = DCACHE_INDEX_BITS;
  static constexpr int WAY_NUM = DCACHE_WAY_NUM;

  uint32_t cache_tag[WAY_NUM][1 << INDEX_WIDTH];
  bool cache_valid[WAY_NUM][1 << INDEX_WIDTH];
  uint8_t plru_tree[1 << INDEX_WIDTH][(WAY_NUM - 1 + 7) / 8];
  std::deque<PendingReq> pending_reqs;
  static constexpr size_t MAX_PENDING_REQS = DCACHE_MAX_PENDING_REQS;
  MemRespIO pending_resp;

  int get_tag(uint32_t addr) { return (addr >> (OFFSET_WIDTH + INDEX_WIDTH)); }

  int get_index(uint32_t addr) {
    return ((addr >> OFFSET_WIDTH) & ((1 << INDEX_WIDTH) - 1));
  }

public:
  SimpleCache(SimContext *ctx) {
    this->ctx = ctx;
    memset(cache_valid, 0, sizeof(cache_valid));
    memset(plru_tree, 0, sizeof(plru_tree));
  }

  SimContext *ctx;
  int cache_select_evict(uint32_t addr);
  void cache_evict(uint32_t addr);
  int cache_access(uint32_t addr);
  int get_data_magic(uint32_t p_addr);
  void update_plru(uint32_t index, int accessed_way);
  void handle_write_req(const MemReqIO &req);
  void accept_req(const MemReqIO &req);
  void drive_resp(MemRespIO &resp) const;
  void init() override;
  void comb() override;
  void seq() override;
};
