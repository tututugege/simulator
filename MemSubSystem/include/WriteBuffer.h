#pragma once

#include "DcacheConfig.h"
#include "IO.h"
#include <cstdint>

class SimContext;

struct WriteBufferEntry {
    reg<32> addr;
    reg<32> data[DCACHE_WORD_NUM];
};

struct WBState {
    reg<DCACHE_WB_COUNT_BITS> count; // number of valid entries in the buffer
    reg<DCACHE_WB_BITS> head;  // index of the oldest entry (next to evict)
    reg<1> send;  // flag to indicate if a request is currently being sent

    reg<1> bypassvalid[LSU_LDU_COUNT];
    reg<32> bypassdata[LSU_LDU_COUNT];

    reg<1> mergevalid[LSU_STA_COUNT];
    reg<1> mergebusy[LSU_STA_COUNT];

    WriteBufferEntry write_buffer[DCACHE_WB_ENTRIES];
};

// AXI write-channel interface signals (IC's write_ports[MASTER_DCACHE_W]).
// axi_in  — inputs from IC to WriteBuffer (driven by RealDcache bridge).
// axi_out — outputs from WriteBuffer to IC (consumed by RealDcache bridge).
struct WbAxiIn {
    wire<1> req_ready  = false;  // IC is ready to accept the current request
    wire<1> req_accepted = false; // one-cycle pulse when IC actually accepts it
    wire<1> resp_valid = false;  // B response available
    void clear() {
        req_ready = false;
        req_accepted = false;
        resp_valid = false;
    }
};

struct WbAxiOut {
    wire<1>  req_valid      = false;  // AW+W request to IC
    wire<32> req_addr       = 0;
    wire<8>  req_total_size = 0;
    wire<8>  req_id         = 0;
    wire<64> req_wstrb      = 0;
    wire<32> req_wdata[DCACHE_WORD_NUM] = {};
    wire<1>  resp_ready     = false;  // ready to accept B response

    void clear() {
        req_valid = false;
        req_addr = 0;
        req_total_size = 0;
        req_id = 0;
        req_wstrb = 0;
        resp_ready = false;
        memset(req_wdata, 0, sizeof(req_wdata));
    }
};


struct WBIn {
    DcacheWBIO *dcache2wb;
    WbAxiIn *axi_in;
};
struct WBOut {
    WBDcacheIO *wb2dcache;
    WbAxiOut *axi_out;

    void clear() {
        wb2dcache->clear();
        axi_out->clear();
    }
};
class WriteBuffer {
public:
    WriteBuffer() = default;
    
    void init();

    // Phase 1: compute out.full and out.free_count from current nxt.count.
    void comb_outputs_dcache();
    void comb_outputs_axi();
    // Phase 2: process push requests from in.push_ports[], accept B channel
    // responses, and fill axi_out with the next AW+W request.
    // Reads axi_in (set by caller before invoking); writes axi_out.
    void comb_inputs_dcache();
    void comb_inputs_axi();
    void seq();

    // Input / output signal ports (public for direct access by RealDcache).
    WBIn  in;
    WBOut out;
    WBState cur, nxt;

    void dump_debug_state(FILE *out) const;

#if !BSD_CONFIG
    void bind_context(SimContext *c) { ctx = c; }
    SimContext *ctx = nullptr;
#endif
};
