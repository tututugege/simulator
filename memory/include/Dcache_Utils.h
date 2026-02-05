#pragma once
#include <cmath>
#include <config.h>

const int DCACHE_SIZE = 4096;                           // 4KB
const int DCACHE_TAG_BITS = 22;                         // sv32
const int DCACHE_WAY_NUM = 4;                           // 4-way set associative
const int DCACHE_LINE_SIZE = 16;                        // 16B - 4
const int DCACHE_OFFSET_NUM = DCACHE_LINE_SIZE / 4;     // 4
const int DCACHE_OFFSET_BITS = log2(DCACHE_OFFSET_NUM); // 2
const int DCACHE_INDEX_BITS =
    32 - 2 - DCACHE_TAG_BITS - DCACHE_OFFSET_BITS; // 6
const int DCACHE_LINE_NUM =
    DCACHE_SIZE / DCACHE_LINE_SIZE / DCACHE_WAY_NUM; // 64 lines

// const int TREE_BITS = DCACHE_WAY_NUM - 1;
// const int TREE_BYTES = 1<<TREE_BITS;

#define GET_TAG(addr) ((addr) >> (DCACHE_INDEX_BITS + DCACHE_OFFSET_BITS + 2))
#define GET_INDEX(addr)                                                        \
  (((addr) >> (DCACHE_OFFSET_BITS + 2)) & ((1 << DCACHE_INDEX_BITS) - 1))
#define GET_OFFSET(addr) (((addr) >> 2) & ((1 << DCACHE_OFFSET_BITS) - 1))
#define GET_ADDR(tag, index, offset)                                           \
  (((tag) << (DCACHE_INDEX_BITS + DCACHE_OFFSET_BITS) |                          \
    (index) << DCACHE_OFFSET_BITS | (offset))                                      \
   << 2)

extern uint32_t dcache_data[DCACHE_LINE_NUM][DCACHE_WAY_NUM][DCACHE_OFFSET_NUM];
extern uint32_t dcache_lru[DCACHE_LINE_NUM][DCACHE_WAY_NUM];
extern uint32_t dcache_tag[DCACHE_LINE_NUM][DCACHE_WAY_NUM]; // sv3
extern bool dcache_valid[DCACHE_LINE_NUM][DCACHE_WAY_NUM];
extern bool dcache_dirty[DCACHE_LINE_NUM][DCACHE_WAY_NUM];
// extern bool tree[DCACHE_LINE_NUM][TREE_BYTES];

void updatelru(int linenum, int way);
int getlru(int linenum);

void hit_check(uint32_t index, uint32_t tag, bool &hit, int &way_idx,
               uint32_t &hit_data, uint32_t tag_check[DCACHE_WAY_NUM],
               uint32_t data_check[DCACHE_WAY_NUM]);
uint32_t write_data_mask(uint32_t old_data, uint32_t wdata, uint32_t wstrb);
void tag_and_data_read(uint32_t index, uint32_t offset,
                       uint32_t tag[DCACHE_WAY_NUM],
                       uint32_t data[DCACHE_WAY_NUM]);
bool dcache_read(uint32_t addr, uint32_t &rdata);
