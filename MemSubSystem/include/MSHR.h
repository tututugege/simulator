#pragma once

#include "config.h"
#include "types.h"
#include "WriteBuffer.h"
#include "DcacheConfig.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "IO.h"

// ─────────────────────────────────────────────────────────────────────────────
// MSHRState — the latched state of the MSHR (cur/nxt pair).
// ─────────────────────────────────────────────────────────────────────────────
extern MSHREntry mshr_entries_nxt[MSHR_ENTRIES];

struct MSHR_STATE{
    // one-cycle replay pulse for LSU after a fill is accepted
    bool fill;
    uint32_t fill_addr;
    uint32_t mshr_count;

    // Registered outputs: produced in comb_inputs(), consumed by comb_outputs()
    // in the next cycle.
    bool fill_valid;
    uint32_t fill_way;
    uint32_t fill_data[DCACHE_LINE_WORDS];

    bool wb_valid;
    uint32_t wb_addr;
    uint32_t wb_data[DCACHE_LINE_WORDS];
};

// AXI read-channel interface signals (IC's read_ports[MASTER_DCACHE_R]).
// axi_in  — inputs from IC to MSHR (driven by RealDcache bridge).
// axi_out — outputs from MSHR to IC (consumed by RealDcache bridge).
struct MshrAxiIn {
    bool     req_ready  = false;   // IC accepted the AR request
    bool     resp_valid = false;   // R data available from IC
    uint32_t resp_data[DCACHE_LINE_WORDS] = {};
    uint8_t  resp_id    = 0;       // transaction ID echoed back
};

struct MshrAxiOut {
    bool     req_valid      = false;  // AR request to IC
    uint32_t req_addr       = 0;
    uint8_t  req_total_size = 0;
    uint8_t  req_id         = 0;
    bool     resp_ready     = false;  // ready to accept R data
};

enum MSHR_StateMachine{
    MSHR_IDLE,
    MSHR_DEAL,
    MSHR_TRAN,
    MSHR_WRITEBACK,
    MSHR_FORWARD
};

struct MSHRINIO{
    DcacheMSHRIO dcachemshr;
    WBMSHRIO wbmshr;
    MshrAxiIn  axi_in;
};
struct MSHROUTIO{
    MSHRDcacheIO mshr2dcache;
    MshrAxiOut axi_out;
    MSHRWBIO mshrwb;
    ReplayResp replay_resp;
};
class MSHR {
public:
    MSHR() = default;

    MSHR_StateMachine state = MSHR_IDLE;

    void init();
    int entries_add(int set_idx,int tag);

    // Phase 1: compute lookup results, full flag, and fill delivery from cur.
    // Must be called BEFORE stage2_comb() so lookup results are ready.
    void comb_outputs();

    // Phase 2: process alloc/secondary signals from in.ports, handle AXI R
    // channel (including WB forwarding), and fill axi_out with the next AR.
    // Reads axi_in (set by caller before invoking); writes axi_out.
    // Must be called AFTER stage2_comb() and after the IC→axi_in bridge.
    void comb_inputs();

    // Advance state: auto-consume the delivered fill, then cur = nxt, retire /
    // promote consumed entries, reset nxt.
    void seq();

    // Input / output signal ports (public for direct access by RealDcache).
    MSHRINIO in;
    MSHROUTIO out;

    MSHR_STATE cur,nxt;

    // AXI channel signals — set by the RealDcache bridge before/after comb_inputs().

};
