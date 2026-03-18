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
    bool is_cache_miss = false;
  };

  static constexpr int L1_OFFSET_WIDTH = DCACHE_OFFSET_BITS;
  static constexpr int L1_INDEX_WIDTH = DCACHE_INDEX_BITS;
  static constexpr int L1_WAY_NUM = DCACHE_WAY_NUM;
  static constexpr int L1_PLRU_BYTES = (L1_WAY_NUM - 1 + 7) / 8;

  static constexpr int L2_OFFSET_WIDTH = DCACHE_L2_OFFSET_BITS;
  static constexpr int L2_INDEX_WIDTH = DCACHE_L2_INDEX_BITS;
  static constexpr int L2_WAY_NUM = DCACHE_L2_WAY_NUM;
  static constexpr int L2_PLRU_BYTES = (L2_WAY_NUM - 1 + 7) / 8;

  uint32_t l1_cache_tag[L1_WAY_NUM][1 << L1_INDEX_WIDTH];
  bool l1_cache_valid[L1_WAY_NUM][1 << L1_INDEX_WIDTH];
  uint8_t l1_plru_tree[1 << L1_INDEX_WIDTH][L1_PLRU_BYTES];

  uint32_t l2_cache_tag[L2_WAY_NUM][1 << L2_INDEX_WIDTH];
  bool l2_cache_valid[L2_WAY_NUM][1 << L2_INDEX_WIDTH];
  uint8_t l2_plru_tree[1 << L2_INDEX_WIDTH][L2_PLRU_BYTES];

  std::deque<PendingReq> pending_reqs;
  static constexpr size_t MAX_PENDING_REQS = DCACHE_MAX_PENDING_REQS;
  MemRespIO pending_resp;

  int l1_get_tag(uint32_t addr) const {
    return (addr >> (L1_OFFSET_WIDTH + L1_INDEX_WIDTH));
  }
  int l1_get_index(uint32_t addr) const {
    return ((addr >> L1_OFFSET_WIDTH) & ((1 << L1_INDEX_WIDTH) - 1));
  }
  int l2_get_tag(uint32_t addr) const {
    return (addr >> (L2_OFFSET_WIDTH + L2_INDEX_WIDTH));
  }
  int l2_get_index(uint32_t addr) const {
    return ((addr >> L2_OFFSET_WIDTH) & ((1 << L2_INDEX_WIDTH) - 1));
  }

public:
  SimpleCache(SimContext *ctx) {
    this->ctx = ctx;
    memset(l1_cache_valid, 0, sizeof(l1_cache_valid));
    memset(l1_plru_tree, 0, sizeof(l1_plru_tree));
    memset(l2_cache_valid, 0, sizeof(l2_cache_valid));
    memset(l2_plru_tree, 0, sizeof(l2_plru_tree));
  }

  SimContext *ctx;
  int cache_select_evict(uint8_t tree[], int way_num);
  void l1_cache_evict(uint32_t addr);
  void l2_cache_evict(uint32_t addr);
  int cache_access(uint32_t addr);
  int get_data_magic(uint32_t p_addr);
  void update_plru(uint8_t tree[], int way_num, int accessed_way);
  void handle_write_req(const MemReqIO &req);
  void accept_req(const MemReqIO &req);
  void drive_resp(MemRespIO &resp) const;
  void init() override;
  void comb() override;
  void seq() override;
};
