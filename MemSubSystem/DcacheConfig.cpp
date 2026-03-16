#include "DcacheConfig.h"
#include <cstdint>
#include <cstring>

uint32_t tag_array[DCACHE_SETS][DCACHE_WAYS] = {};
uint32_t data_array[DCACHE_SETS][DCACHE_WAYS][DCACHE_LINE_WORDS] = {};
bool valid_array[DCACHE_SETS][DCACHE_WAYS] = {};
bool dirty_array[DCACHE_SETS][DCACHE_WAYS] = {};
uint8_t lru_state[DCACHE_SETS][DCACHE_WAYS] = {};
MSHREntry mshr_entries[MSHR_ENTRIES] = {};
WriteBufferEntry write_buffer[WB_ENTRIES] = {};

void init_dcache()
{
    std::memset(tag_array, 0, sizeof(tag_array));
    std::memset(data_array, 0, sizeof(data_array));
    std::memset(valid_array, 0, sizeof(valid_array));
    std::memset(dirty_array, 0, sizeof(dirty_array));
    std::memset(lru_state, 0, sizeof(lru_state));
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
    for (int i = 0; i < MSHR_ENTRIES; i++)
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
    lru_state[set_idx][way] = 0; // Most recently used
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
