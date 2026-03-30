#include "DcacheConfig.h"
#include <cstdint>
#include <cstring>

uint32_t tag_array[DCACHE_SETS][DCACHE_WAYS] = {};
uint32_t data_array[DCACHE_SETS][DCACHE_WAYS][DCACHE_LINE_WORDS] = {};
bool valid_array[DCACHE_SETS][DCACHE_WAYS] = {};
bool dirty_array[DCACHE_SETS][DCACHE_WAYS] = {};
uint8_t lru_state[DCACHE_SETS][DCACHE_WAYS] = {};
MSHREntry mshr_entries[DCACHE_MSHR_ENTRIES] = {};
WriteBufferEntry write_buffer[DCACHE_WB_ENTRIES] = {};

namespace {
// Maintain per-set LRU ranks in [0, DCACHE_WAYS-1]:
// larger rank means more recently used.
inline void lru_touch_way(uint32_t set_idx, uint32_t way) {
    if (set_idx >= DCACHE_SETS || way >= DCACHE_WAYS) {
        return;
    }
    const uint8_t old_rank = lru_state[set_idx][way];
    for (uint32_t w = 0; w < DCACHE_WAYS; w++) {
        if (w == way) {
            continue;
        }
        uint8_t &rank = lru_state[set_idx][w];
        if (rank > old_rank) {
            rank--;
        }
    }
    lru_state[set_idx][way] = static_cast<uint8_t>(DCACHE_WAYS - 1);
}
} // namespace

void init_dcache()
{
    std::memset(tag_array, 0, sizeof(tag_array));
    std::memset(data_array, 0, sizeof(data_array));
    std::memset(valid_array, 0, sizeof(valid_array));
    std::memset(dirty_array, 0, sizeof(dirty_array));
    for (uint32_t s = 0; s < DCACHE_SETS; s++) {
        for (uint32_t w = 0; w < DCACHE_WAYS; w++) {
            lru_state[s][w] = static_cast<uint8_t>(w);
        }
    }
    std::memset(mshr_entries, 0, sizeof(mshr_entries));
    std::memset(write_buffer, 0, sizeof(write_buffer));
}

AddrFields decode(uint32_t addr)
{
    AddrFields f;
    f.word_off = (addr >> 2) & (DCACHE_LINE_WORDS - 1);
    f.set_idx = (addr >> DCACHE_OFFSET_BITS) & (DCACHE_SETS - 1);
    f.tag = addr >> (DCACHE_SET_BITS + DCACHE_OFFSET_BITS);
    f.bank = f.set_idx & (DCACHE_BANKS - 1);
    return f;
}

bool find_mshr_entry(uint32_t index, uint32_t tag)
{
    for (int i = 0; i < DCACHE_MSHR_ENTRIES; i++)
    {
        if (mshr_entries[i].valid && mshr_entries[i].index == index && mshr_entries[i].tag == tag)
        {
            return true;
        }
    }
    return false;
}

uint32_t get_addr(uint32_t set_idx, uint32_t tag, uint32_t word_off)
{
    return (tag << (DCACHE_SET_BITS + DCACHE_OFFSET_BITS)) | (set_idx << DCACHE_OFFSET_BITS) | (word_off << 2);
}

int choose_lru_victim(uint32_t set_idx)
{
    for (int w = 0; w < DCACHE_WAYS; w++) {
        if (!valid_array[set_idx][w]) {
            return w;
        }
    }
    int lru_way = 0;
    for (int w = 1; w < DCACHE_WAYS; w++)
    {
        if (lru_state[set_idx][w] < lru_state[set_idx][lru_way])
            lru_way = w;
    }
    return lru_way;
}
void lru_reset(uint32_t set_idx, uint32_t way)
{
    lru_touch_way(set_idx, way); // Mark as most recently used
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

void write_dcache_line(uint32_t set_idx, uint32_t way, uint32_t tag, uint32_t data[DCACHE_LINE_WORDS])
{
    uint32_t target_way = way;
    for (uint32_t w = 0; w < DCACHE_WAYS; w++)
    {
        if (valid_array[set_idx][w] && tag_array[set_idx][w] == tag)
        {
            target_way = w;
            break;
        }
    }

    // Keep one physical copy per (set, tag) to avoid stale duplicate lines
    // surviving in other ways and being written back later.
    for (uint32_t w = 0; w < DCACHE_WAYS; w++)
    {
        if (w != target_way && valid_array[set_idx][w] && tag_array[set_idx][w] == tag)
        {
            valid_array[set_idx][w] = false;
            dirty_array[set_idx][w] = false;
            lru_state[set_idx][w] = 0;
        }
    }

    valid_array[set_idx][target_way] = true;
    dirty_array[set_idx][target_way] = false;
    tag_array[set_idx][target_way] = tag;
    lru_reset(set_idx, target_way);
    for (int w = 0; w < DCACHE_LINE_WORDS; w++)
    {
        data_array[set_idx][target_way][w] = data[w];
    }
}

bool cache_line_match(uint32_t addr1, uint32_t addr2){
    return (addr1 >> DCACHE_OFFSET_BITS) == (addr2 >> DCACHE_OFFSET_BITS);
}
