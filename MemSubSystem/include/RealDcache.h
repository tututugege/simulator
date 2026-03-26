#pragma once

#include "AbstractDcache.h"
#include "AbstractLsu.h"
#include "MSHR.h"
#include "WriteBuffer.h"
#include "config.h"
#include "types.h"
#include "DcacheConfig.h"   
#include "IO.h"
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Pending store write (hit path) — records a store hit so that seq() can apply
// the byte-merge to the data SRAM.
// ─────────────────────────────────────────────────────────────────────────────
struct PendingWrite {
    bool     valid    = false;
    uint32_t set_idx  = 0;
    uint32_t way_idx  = 0;
    uint32_t word_off = 0;
    uint32_t data     = 0;
    uint8_t  strb     = 0;   // byte-enable (4 bits used for a 32-bit word)
};

struct FillWrite{
    bool     valid    = false;
    uint32_t set_idx  = 0;
    uint32_t way_idx  = 0;
    uint32_t data[DCACHE_LINE_WORDS] = {};
};
// ─────────────────────────────────────────────────────────────────────────────
// S1S2Reg — pipeline register between Stage 1 (SRAM read) and Stage 2 (hit check)
// ─────────────────────────────────────────────────────────────────────────────
struct S1S2Reg {
    // Load slots
    struct LoadSlot {
        bool     valid    = false;
        bool     replayed = false; // bank conflict — do not process in S2
        uint32_t addr     = 0;
        MicroOp  uop;
        size_t   req_id   = 0;

        // SRAM snapshot captured in S1 (index already decoded)
        uint32_t set_idx  = 0;
        uint32_t tag_snap [DCACHE_WAYS] = {};
        bool     valid_snap[DCACHE_WAYS] = {};
        bool     dirty_snap[DCACHE_WAYS] = {};
        uint32_t data_snap [DCACHE_WAYS][DCACHE_LINE_WORDS] = {};

        bool mshr_hit = false; // for load hits, whether the MSHR entry is the primary one (for write-allocate ordering)
    } loads[LSU_LDU_COUNT];

    // Store slots
    struct StoreSlot {
        bool     valid    = false;
        bool     replayed = false;
        uint32_t addr     = 0;
        uint32_t data     = 0; // word to write
        uint8_t  strb     = 0; // byte-enable
        StqEntry uop;
        size_t   req_id   = 0;

        uint32_t set_idx  = 0;
        uint32_t tag_snap [DCACHE_WAYS] = {};
        bool     valid_snap[DCACHE_WAYS] = {};
        bool     dirty_snap[DCACHE_WAYS] = {};
        uint32_t data_snap [DCACHE_WAYS][DCACHE_LINE_WORDS] = {};
        bool mshr_hit = false; // for store misses, whether there's a primary MSHR entry matching this line (for write-allocate decision)
    } stores[LSU_STA_COUNT];
};

// ─────────────────────────────────────────────────────────────────────────────
// RealDcache — non-blocking, write-allocate, write-back D-Cache.
//
// Geometry (configurable via config.h):
//   DCACHE_SETS sets × DCACHE_WAYS ways × 32-byte cachelines.
//
// Pipeline: 2 stages.
//   Stage 1 (S1): index decode + SRAM array read → feeds S1S2 pipeline register.
//   Stage 2 (S2): tag compare, hit/miss decision, MSHR/WB interaction → outputs.
//
// All SRAM state is updated in seq() only; comb() is pure-read + output logic.
// ─────────────────────────────────────────────────────────────────────────────
class RealDcache : public AbstractDcache {
public:
    enum class CoherentQueryResult : uint8_t {
        Miss = 0,
        Retry = 1,
        Hit = 2,
    };

    void bind_context(SimContext *c) { ctx = c; }
    void init() override;
    void comb() override;
    void seq()  override;
    CoherentQueryResult query_coherent_word(uint32_t addr,
                                            uint32_t &data) const;

    // IO ports — set by the owning module (e.g. MemSubsystem) before init().
    LsuDcacheIO  *lsu2dcache  = nullptr;  // LSU → DCache requests
    DcacheLsuIO  *dcache2lsu  = nullptr;  // DCache → LSU responses
    MSHRDcacheIO *mshr2dcache = nullptr;  // MSHR fill/free → DCache
    DcacheMSHRIO *dcache2mshr = nullptr;  // DCache miss alloc → MSHR
    DcacheWBIO   *dcache2wb   = nullptr;  // DCache bypass/merge req → WB
    WBDcacheIO   *wb2dcache   = nullptr;  // WB bypass/merge resp → DCache

    // Stage 1: decode incoming requests, detect bank conflicts, snapshot SRAMs.
    void stage1_comb();

    // Drive WB bypass/merge queries for the S2 pipeline register currently
    // being evaluated. This must stay aligned with s1s2_cur rather than the
    // new requests seen by stage1 in the same cycle.
    void prepare_wb_queries_for_stage2();

    // Stage 2: evaluate S1S2 pipeline register, generate responses.
    void stage2_comb();
private:
    SimContext *ctx = nullptr;

    struct ReqTrack {
        bool valid = false;
        bool is_store = false;
        size_t req_id = 0;
        uint32_t rob_idx = 0;
        uint32_t rob_flag = 0;
        uint64_t first_cycle = 0;
    };
    static constexpr int kReqTrackSize = 256;
    ReqTrack req_track_[kReqTrackSize]{};
    int req_track_rr_ = 0;


    // // Sub-modules
    // MSHR        mshr_;
    // WriteBuffer wb_;

    // ── SRAM arrays (updated only in seq()) ──────────────────────────────────
    

    // ── Pipeline registers ────────────────────────────────────────────────────
    S1S2Reg s1s2_cur; // latched at start of cycle
    S1S2Reg s1s2_nxt; // computed by comb(); committed by seq()

    // ── Pending store hits (recorded by comb(), applied by seq()) ─────────────
    // One pending write per store port.
    PendingWrite pending_writes_[LSU_STA_COUNT];

    // ── LRU update log (recorded by comb(), applied by seq()) ─────────────────
    struct LruUpdate {
        bool     valid   = false;
        uint32_t set_idx = 0;
        int      way     = -1;
    } lru_updates_[LSU_LDU_COUNT + LSU_STA_COUNT];


    bool special_load_addr(uint32_t addr,uint32_t& mem_val,MicroOp &uop);
    bool begin_req_track(bool is_store, size_t req_id, uint32_t rob_idx,
                         uint32_t rob_flag);
    void end_req_track(bool is_store, size_t req_id, uint32_t rob_idx,
                       uint32_t rob_flag);
};
