#include "config.h"
#include <cstdint>
#include <cstring>
using namespace std;

#define PLRU_EVICT

class SimpleCache {
  static constexpr int OFFSET_WIDTH = 6;
  static constexpr int INDEX_WIDTH = 8;
  static constexpr int WAY_NUM = 4;
  static constexpr int HIT_LATENCY = 1;
  static constexpr int MISS_LATENCY = 1;

  uint32_t cache_tag[WAY_NUM][1 << INDEX_WIDTH];
  bool cache_valid[WAY_NUM][1 << INDEX_WIDTH];
  uint8_t plru_tree[1 << INDEX_WIDTH][(WAY_NUM - 1 + 7) / 8];

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
};
