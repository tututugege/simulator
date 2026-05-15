#pragma once

#include "MSHR.h"
#include "WriteBuffer.h"
#include "config.h"
#include "types.h"
#include "DcacheConfig.h"   
#include "IO.h"
#include <cstdint>

#if !BSD_CONFIG
class SimContext;
#endif

// ─────────────────────────────────────────────────────────────────────────────
// S1S2Reg — pipeline register between Stage 1 (SRAM read) and Stage 2 (hit check)
// ─────────────────────────────────────────────────────────────────────────────
struct S1S2Reg {
    reg<LSU_LDU_WIDTH+1> icache_req; // latched from in.lsu2dcache->icache_req at stage1_comb(); used for timing the critical path of hit check and MSHR lookup
    
    FILLWrite fill_write; // latched from in.mshr2dcache->fill_req at stage1_comb(); used for timing the critical path of hit check and MSHR lookup
    // Load slots
    struct LoadSlot {
        reg<1>     valid    = false;
        reg<32>    addr     = 0;
        reg<32>     req_id   = 0;
        reg<1>    replayed  = false; // whether this load has been replayed due to MSHR full or conflict, used to avoid accepting new requests for the same load and causing starvation when there are multiple back-to-back misses
#if !BSD_CONFIG
        reg<1>    perf_replay = false;
#endif
    } loads[LSU_LDU_COUNT];

    // Store slots
    struct StoreSlot {
        reg<1>     valid    = false;
        reg<32>    addr     = 0;
        reg<32>    data     = 0; // word to write
        reg<8>     strb     = 0; // byte-enable
        reg<32>    req_id   = 0;
        reg<1>     replayed  = false; // whether this store has been replayed due to MSHR full or conflict, used to avoid accepting new requests for the same store and causing starvation when there are multiple back-to-back misses
#if !BSD_CONFIG
        reg<1>     perf_replay = false;
#endif
    } stores[LSU_STA_COUNT];
};
class RealDcache {
public:
    void init();
    void comb();
    void seq();
#if !BSD_CONFIG
    void bind_context(SimContext *c) { ctx = c; }
#endif

    DcacheINIO in;
    DcacheOUTIO out;

    void stage1_comb();
    void stage2_comb();

    void dump_debug_state(FILE *out) const;
private:
    S1S2Reg s1s2_cur; // latched at start of cycle
    S1S2Reg s1s2_nxt; // computed by comb(); committed by seq()
#if !BSD_CONFIG
    SimContext *ctx = nullptr;
#endif
};
