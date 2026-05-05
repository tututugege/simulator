#pragma once
#include <cmath>
#include <cstdint>
#include "IO.h"
#include "config.h"

extern uint32_t tag_array  [DCACHE_SETS_NUM][DCACHE_WAYS_NUM];
extern uint32_t data_array [DCACHE_SETS_NUM][DCACHE_WAYS_NUM][DCACHE_WORD_NUM];
extern wire<1>  valid_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM];
extern wire<1>  dirty_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM];

static_assert((DCACHE_WAYS_NUM & (DCACHE_WAYS_NUM - 1)) == 0,
              "tree-PLRU requires power-of-two DCACHE_WAYS_NUM");
constexpr int DCACHE_PLRU_TREE_BITS =
    (DCACHE_WAYS_NUM > 1) ? (DCACHE_WAYS_NUM - 1) : 1;
extern wire<1>  plru_tree_state[DCACHE_SETS_NUM][DCACHE_PLRU_TREE_BITS];

struct MSHRFINDReq{
    wire<1> valid;
    wire<DCACHE_SET_BITS> set_idx;
    wire<DCACHE_TAG_BITS> tag;
};
struct MSHRFINDResp{
    wire<1> valid;
    wire<1> hit;
};

struct MSHRReq{
    wire<1> valid;
    wire<32> addr;
};

struct MSHR_FILLReq{
    wire<1> valid;
    wire<DCACHE_WAY_BITS> way_idx;
    wire<32> addr;
    wire<32> data[DCACHE_WORD_NUM];

};
struct MSHR_FILLResp{
    wire<1> done;
};
struct MSHRDcacheIO {
    MSHR_FILLReq fill_req;
    MSHRFINDResp find_resp[LSU_LDU_COUNT + LSU_STA_COUNT]; // one MSHRResp per load/store slot, for miss allocation and hit check
    wire<DCACHE_MSHR_COUNT_BITS> free;
    void clear() {
        memset(this, 0, sizeof(MSHRDcacheIO));
    }
};

struct DcacheMSHRIO {
    MSHRFINDReq find_req[LSU_LDU_COUNT + LSU_STA_COUNT]; // one MSHRReq per load/store slot, for miss allocation and hit check
    MSHRReq mshr_req[DCACHE_MISS_NUM];
    MSHR_FILLResp fill_resp;
    
    void clear() {
        memset(this, 0, sizeof(DcacheMSHRIO));
    }
};

struct BypassReq {
    wire<1> valid;
    wire<32> addr;
};

struct BypassResp {
    wire<1> valid;
    wire<32> data;
};

struct MergeReq {
    wire<1> valid;
    wire<32> addr;
    wire<32> data;
    wire<8> strb;
};

struct MergeResp {
    wire<1> valid;
    wire<1> busy;
};

struct DirtyInfo {
    wire<1> valid;
    wire<32> addr;
    wire<32> data[DCACHE_WORD_NUM];
};

struct DcacheWBIO{
    DirtyInfo dirty_info;
    BypassReq bypass_req[LSU_LDU_COUNT];
    MergeReq merge_req[LSU_STA_COUNT];

    void clear() {
        memset(this, 0, sizeof(DcacheWBIO));
    }
};

struct WBDcacheIO{
    wire<DCACHE_WB_COUNT_BITS> free;

    BypassResp bypass_resp[LSU_LDU_COUNT];
    MergeResp merge_resp[LSU_STA_COUNT];

    void clear() {
        memset(this, 0, sizeof(WBDcacheIO));
    }
};
struct PendingWrite {
    wire<1>     valid    = false;
    wire<DCACHE_SET_BITS> set_idx  = 0;
    wire<DCACHE_WAY_BITS> way_idx  = 0;
    wire<DCACHE_OFFSET_BITS> word_off = 0;
    wire<32> data     = 0;
    wire<8>  strb     = 0;   // byte-enable (4 bits used for a 32-bit word)
};
struct FILLWrite {
    wire<1>     valid    = false;
    wire<DCACHE_SET_BITS> set_idx  = 0;
    wire<DCACHE_TAG_BITS> tag      = 0;
    wire<DCACHE_WAY_BITS> way_idx  = 0;
    wire<32> data[DCACHE_WORD_NUM] = {};
};

struct LruUpdate {
    wire<1>     valid   = false;
    wire<DCACHE_SET_BITS> set_idx = 0;
    wire<DCACHE_WAY_BITS> way     = 0;
};

struct DcacheLineReadReq {
    wire<DCACHE_SET_BITS> set_idx;
};


struct DcacheLineReadResp {
    wire<1> valid[DCACHE_WAYS_NUM];
    wire<DCACHE_TAG_BITS> tag[DCACHE_WAYS_NUM];
    wire<1> dirty[DCACHE_WAYS_NUM];
    wire<32> data[DCACHE_WAYS_NUM][DCACHE_WORD_NUM];
};
struct FillIn {
    wire<1> valid_snap[DCACHE_WAYS_NUM];
    wire<1> dirty_snap[DCACHE_WAYS_NUM];
    wire<DCACHE_TAG_BITS> tag_snap [DCACHE_WAYS_NUM];
    wire<32> data_snap [DCACHE_WAYS_NUM][DCACHE_WORD_NUM];
    wire<1> plru_tree_state[DCACHE_PLRU_TREE_BITS];
};

struct FillOut {
    wire<1> valid;
    wire<DCACHE_SET_BITS> set_idx;
};

struct DcacheINIO {
    LsuDcacheIO  *lsu2dcache  = nullptr;  // LSU → DCache requests
    MSHRDcacheIO *mshr2dcache = nullptr;  // MSHR fill/free → DCache
    WBDcacheIO   *wb2dcache   = nullptr;  // WB bypass/merge resp → DCache
    DcacheLineReadResp *dcachelinereadresp[LSU_LDU_COUNT + LSU_STA_COUNT]; // For BSD: output the chosen victim for debugging
    FillIn *fillin; 
};

struct DcacheOUTIO {
    DcacheLsuIO  *dcache2lsu  = nullptr;  // DCache → LSU responses
    DcacheMSHRIO *dcache2mshr = nullptr;  // DCache miss alloc → MSHR
    DcacheWBIO   *dcache2wb   = nullptr;  // DCache bypass/merge req → WB
    PendingWrite *pendingwrite[LSU_LDU_COUNT + LSU_STA_COUNT]; // For tracking pending misses for the store queue and load queue
    LruUpdate *lru_updates[LSU_LDU_COUNT + LSU_STA_COUNT]; // For tracking PLRU updates for the store queue and load queue
    FILLWrite *fill_write; // For tracking MSHR fill responses for the store queue and load queue
    DcacheLineReadReq *dcachereadreq[LSU_LDU_COUNT + LSU_STA_COUNT] ; // For BSD: output the chosen victim for debugging
    FillOut *fillout; 
};

// ─────────────────────────────────────────────────────────────────────────────
// Address field decomposition helper
// ─────────────────────────────────────────────────────────────────────────────
struct AddrFields {
    wire<DCACHE_TAG_BITS> tag;
    wire<DCACHE_SET_BITS> set_idx;
    wire<DCACHE_OFFSET_BITS> word_off; // which 32-bit word within the cacheline [4:2]
};

void init_dcache();
AddrFields decode(uint32_t addr);
uint32_t get_addr(uint32_t set_idx, uint32_t tag, uint32_t word_off);
uint32_t choose_plru_tree_victim(const bool plru_tree[DCACHE_PLRU_TREE_BITS], const bool valid[DCACHE_WAYS_NUM]);
void plru_tree_touch(uint32_t set_idx, uint32_t way);


void write_dcache_line(uint32_t set_idx, uint32_t way,uint32_t tag, uint32_t data[DCACHE_WORD_NUM]);
void apply_strobe(uint32_t &dst, uint32_t src, uint8_t strb);

bool cache_line_match(uint32_t addr1, uint32_t addr2);

void Dcache_Read(const DcacheLineReadReq read_req[LSU_LDU_COUNT+LSU_STA_COUNT],DcacheLineReadResp resp[LSU_LDU_COUNT+LSU_STA_COUNT], const FillOut &fillout,FillIn &fillin);
void Dcache_Write(const PendingWrite pws[LSU_LDU_COUNT+LSU_STA_COUNT], const LruUpdate lru_updates[LSU_LDU_COUNT+LSU_STA_COUNT], const FILLWrite &fillwrite);

bool CheckAddr(uint32_t addr1, uint8_t strb1, uint32_t addr2, uint8_t strb2);