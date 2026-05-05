#include "DcacheConfig.h"
#include <cstdint>
#include <cstring>

uint32_t tag_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM] = {};
uint32_t data_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM][DCACHE_WORD_NUM] = {};
bool valid_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM] = {};
bool dirty_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM] = {};
bool pending_fill_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM] = {};
bool plru_tree_state[DCACHE_SETS_NUM][DCACHE_PLRU_TREE_BITS] = {};

namespace {
inline bool invalid_set_or_way(uint32_t set_idx, uint32_t way) {
    return set_idx >= DCACHE_SETS_NUM || way >= DCACHE_WAYS_NUM;
}

inline void plru_touch_way(uint32_t set_idx, uint32_t way) {
    if (set_idx >= DCACHE_SETS_NUM || way >= DCACHE_WAYS_NUM) {
        return;
    }

    uint32_t node = 0;
    uint32_t left_way = 0;
    uint32_t span = DCACHE_WAYS_NUM;
    while (span > 1) {
        const uint32_t half = span / 2;
        const bool went_right = way >= left_way + half;
        plru_tree_state[set_idx][node] = went_right ? 0 : 1;
        node = node * 2 + (went_right ? 2 : 1);
        if (went_right) {
            left_way += half;
        }
        span = half;
    }
}
} // namespace

void init_dcache()
{
    std::memset(tag_array, 0, sizeof(tag_array));
    std::memset(data_array, 0, sizeof(data_array));
    std::memset(valid_array, 0, sizeof(valid_array));
    std::memset(dirty_array, 0, sizeof(dirty_array));
    std::memset(pending_fill_array, 0, sizeof(pending_fill_array));
    std::memset(plru_tree_state, 0, sizeof(plru_tree_state));
}

AddrFields decode(uint32_t addr)
{
    AddrFields f;
    f.word_off = (addr >> 2) & (DCACHE_WORD_NUM - 1);
    f.set_idx = (addr >> DCACHE_OFFSET_BITS) & (DCACHE_SETS_NUM - 1);
    f.tag = addr >> (DCACHE_SET_BITS + DCACHE_OFFSET_BITS);
    return f;
}

uint32_t get_addr(uint32_t set_idx, uint32_t tag, uint32_t word_off)
{
    return (tag << (DCACHE_SET_BITS + DCACHE_OFFSET_BITS)) | (set_idx << DCACHE_OFFSET_BITS) | (word_off << 2);
}

uint32_t choose_plru_tree_victim(const bool plru_tree[DCACHE_PLRU_TREE_BITS], const bool valid[DCACHE_WAYS_NUM])
{
    for (int w = 0; w < DCACHE_WAYS_NUM; w++) {
        if (!valid[w]) {
            return w;
        }
    }

    uint32_t node = 0;
    uint32_t victim_way = 0;
    uint32_t span = DCACHE_WAYS_NUM;
    while (span > 1) {
        const uint32_t half = span / 2;
        const bool go_right = (plru_tree[node] & 1) != 0;
        node = node * 2 + (go_right ? 2 : 1);
        if (go_right) {
            victim_way += half;
        }
        span = half;
    }
    return victim_way;
}


void apply_strobe(uint32_t &dst, uint32_t src, uint8_t strb)
{
    for (int b = 0; b < 4; b++)
    {
        if (strb & (1u << b))
        {
            uint32_t mask = 0xFFu << (b * 8);
            dst = (dst & ~mask) | (src & mask);
        }
    }
}

void write_dcache_line(uint32_t set_idx, uint32_t way, uint32_t tag, const uint32_t data[DCACHE_WORD_NUM])
{
    uint32_t target_way = way;
    for (uint32_t w = 0; w < DCACHE_WAYS_NUM; w++)
    {
        if (valid_array[set_idx][w] && tag_array[set_idx][w] == tag)
        {
            target_way = w;
            break;
        }
    }

    valid_array[set_idx][target_way] = true;
    dirty_array[set_idx][target_way] = false;
    tag_array[set_idx][target_way] = tag;
    plru_tree_touch(set_idx, target_way);
    for (int w = 0; w < DCACHE_WORD_NUM; w++)
    {
        data_array[set_idx][target_way][w] = data[w];
    }
}

bool cache_line_match(uint32_t addr1, uint32_t addr2){
    return (addr1 >> DCACHE_OFFSET_BITS) == (addr2 >> DCACHE_OFFSET_BITS);
}
void Dcache_Read(const DcacheLineReadReq read_req[LSU_LDU_COUNT+LSU_STA_COUNT],DcacheLineReadResp resp[LSU_LDU_COUNT+LSU_STA_COUNT], const FillOut &fillout,FillIn &fillin)
{
    for (int i = 0; i < LSU_LDU_COUNT + LSU_STA_COUNT; i++) {
        const auto &req = read_req[i];
        memcpy(resp[i].valid, valid_array[req.set_idx], sizeof(valid_array[req.set_idx]));
        memcpy(resp[i].tag, tag_array[req.set_idx], sizeof(tag_array[req.set_idx]));
        memcpy(resp[i].dirty, dirty_array[req.set_idx], sizeof(dirty_array[req.set_idx]));
        memcpy(resp[i].data, data_array[req.set_idx], sizeof(data_array[req.set_idx]));
    }

    if(fillout.valid){
        memcpy(fillin.valid_snap,valid_array[fillout.set_idx], sizeof(valid_array[fillout.set_idx]));
        memcpy(fillin.tag_snap,tag_array[fillout.set_idx], sizeof(tag_array[fillout.set_idx]));
        memcpy(fillin.dirty_snap,dirty_array[fillout.set_idx], sizeof(dirty_array[fillout.set_idx]));
        memcpy(fillin.data_snap,data_array[fillout.set_idx], sizeof(data_array[fillout.set_idx]));
        memcpy(fillin.plru_tree_state,plru_tree_state[fillout.set_idx], sizeof(plru_tree_state[fillout.set_idx]));
    }
    else{
        memset(&fillin, 0, sizeof(fillin));
    }
}
void Dcache_Write(const PendingWrite pws[LSU_LDU_COUNT+LSU_STA_COUNT], const LruUpdate lru_updates[LSU_LDU_COUNT+LSU_STA_COUNT], const FILLWrite &fillwrite){
    for(int i=0;i<LSU_LDU_COUNT+LSU_STA_COUNT;i++){
        const PendingWrite &pw = pws[i];
        const LruUpdate &lru_update = lru_updates[i];
        if(pw.valid){
            apply_strobe(data_array[pw.set_idx][pw.way_idx][pw.word_off], pw.data, pw.strb);
            dirty_array[pw.set_idx][pw.way_idx] = true;
        }

        if(lru_update.valid){
            plru_touch_way(lru_update.set_idx, lru_update.way);
        }
    }
    if(fillwrite.valid){
        write_dcache_line(fillwrite.set_idx, fillwrite.way_idx, fillwrite.tag, fillwrite.data);
    }

}

void plru_tree_touch(uint32_t set_idx, uint32_t way) {
    plru_touch_way(set_idx, way);
}

bool CheckAddr(uint32_t addr1, uint8_t strb1, uint32_t addr2, uint8_t strb2) {
    // 1. 地址完全相同，直接比较选通掩码
    if (addr1 == addr2) {
        return (strb1 & strb2) != 0;
    } 
    // 2. addr1 在低地址，addr2 在高地址
    else if (addr1 < addr2) {
        uint32_t diff = addr2 - addr1;
        // 如果地址跨度大于等于8字节（strb的表示范围），绝对不可能重叠
        if (diff >= 8) return false; 
        // 将高地址的 strb 左移 diff 位，与低地址对齐后按位与
        return (strb1 & (strb2 << diff)) != 0;
    } 
    // 3. addr1 在高地址，addr2 在低地址
    else {
        uint32_t diff = addr1 - addr2;
        if (diff >= 8) return false;
        return (strb2 & (strb1 << diff)) != 0;
    }
}