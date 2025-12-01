#include <cstdint>
using namespace std;

#define OFFSET_WIDTH 6
#define INDEX_WIDTH 6
#define WAY_NUM 4

#define TAG(addr) (addr >> (offset_width + index_width))
#define INDEX(addr) ((addr >> offset_width) & ((1 << index_width) - 1))

#define HIT_LATENCY 2
#define MISS_LATENCY 120

#define PLRU_EVICT

class Cache {
  uint32_t cache_tag[WAY_NUM][1 << INDEX_WIDTH];
  uint32_t cache_valid[WAY_NUM][1 << INDEX_WIDTH];
  uint8_t plru_tree[1 << INDEX_WIDTH][(WAY_NUM - 1 + 7) / 8];

  int offset_width = OFFSET_WIDTH;
  int index_width = INDEX_WIDTH;
  int way_num = WAY_NUM;

public:
  int cache_select_evict(uint32_t addr);
  void cache_evict(uint32_t addr);
  int cache_access(uint32_t addr);
  void update_plru(uint32_t index, int accessed_way);
};
