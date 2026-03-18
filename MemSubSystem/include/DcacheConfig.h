#pragma once
#include <cmath>
#include <cstdint>
#include "IO.h"
// ─── Cache geometry (all configurable via -D flags) ───────────────────────────
// Number of sets; must be a power-of-2.
#define DCACHE_SETS 256
#define DCACHE_WAYS 4
#define DCACHE_OFFSET_BITS 6
#define DCACHE_LINE_BYTES  64
#define DCACHE_LINE_WORDS  16
// log2(DCACHE_SETS) — requires power-of-2.
#define DCACHE_SET_BITS    (__builtin_ctz(DCACHE_SETS))
#define DCACHE_TAG_BITS    (32 - DCACHE_SET_BITS - DCACHE_OFFSET_BITS)

#define MSHR_ENTRIES 8
#define WB_ENTRIES 8

#define DCACHE_BANKS 16

extern uint32_t tag_array  [DCACHE_SETS][DCACHE_WAYS];
extern uint32_t data_array [DCACHE_SETS][DCACHE_WAYS][DCACHE_LINE_WORDS];
extern bool     valid_array[DCACHE_SETS][DCACHE_WAYS];
extern bool     dirty_array[DCACHE_SETS][DCACHE_WAYS];


// LRU counter per set (value = most-recently-used way; simple counter scheme)
// lru_state[set][way] = age; higher value = more recently used.
extern uint8_t  lru_state[DCACHE_SETS][DCACHE_WAYS];


struct MSHR_FILL{
    bool valid;
    uint32_t way;
    uint32_t addr;
    uint32_t data[DCACHE_LINE_WORDS];

};
struct MSHRDcacheIO {
    MSHR_FILL fill;
    uint32_t free;
};

struct DcacheMSHRIO {
    LoadReq load_reqs[LSU_LDU_COUNT];
    StoreReq store_reqs[LSU_STA_COUNT];
    struct StoreHitUpdate {
        bool valid = false;
        uint32_t set_idx = 0;
        uint32_t way_idx = 0;
        uint32_t word_off = 0;
        uint32_t data = 0;
        uint8_t strb = 0;
    } store_hit_updates[LSU_STA_COUNT];
    bool fill_ack; // MSHR响应：填充完成
};

struct WBMSHRIO {
    bool ready;
};

struct MSHRWBIO{
    bool valid;
    uint32_t addr;
    uint32_t data[DCACHE_LINE_WORDS];
};

struct BypassReq {
    bool valid;
    uint32_t addr;

    BypassReq() : valid(false), addr(0) {}
};

struct BypassResp {
    bool valid;
    uint32_t data;

    BypassResp() : valid(false), data(0) {}
};

struct MergeReq {
    bool valid;
    uint32_t addr;
    uint32_t data;
    uint8_t strb;

    MergeReq() : valid(false), addr(0), data(0), strb(0) {}
};

struct MergeResp {
    bool valid;
    bool busy;

    MergeResp() : valid(false), busy(false) {}
};

struct DcacheWBIO{
    BypassReq bypass_req[LSU_LDU_COUNT];
    MergeReq merge_req[LSU_STA_COUNT];
};

struct WBDcacheIO{
    BypassResp bypass_resp[LSU_LDU_COUNT];
    MergeResp merge_resp[LSU_STA_COUNT];
};

// ─────────────────────────────────────────────────────────────────────────────
// Address field decomposition helper
// ─────────────────────────────────────────────────────────────────────────────
struct AddrFields {
    uint32_t tag;
    uint32_t set_idx;
    uint32_t word_off; // which 32-bit word within the cacheline [4:2]
    uint32_t bank;     // set_idx & (DCACHE_BANKS - 1)
};

struct MSHREntry {
    bool valid;
    bool issued;
    bool fill;
    uint32_t index;
    uint32_t tag;
};

struct WriteBufferEntry {
    bool valid;
    bool send;
    uint32_t addr;
    uint32_t data[DCACHE_LINE_WORDS];
};

extern MSHREntry mshr_entries[MSHR_ENTRIES];
extern WriteBufferEntry write_buffer[WB_ENTRIES];

void init_dcache();
AddrFields decode(uint32_t addr);
bool find_mshr_entry(uint32_t index, uint32_t tag);
uint32_t get_addr(uint32_t set_idx, uint32_t tag, uint32_t word_off);
int choose_lru_victim(uint32_t set_idx); 
void lru_reset(uint32_t set_idx, uint32_t way);

void write_dcache_line(uint32_t set_idx, uint32_t way,uint32_t tag, uint32_t data[DCACHE_LINE_WORDS]);
void apply_strobe(uint32_t &dst, uint32_t src, uint8_t strb);

bool cache_line_match(uint32_t addr1, uint32_t addr2);
