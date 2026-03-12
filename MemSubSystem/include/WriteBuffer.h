#pragma once

#include "DcacheConfig.h"
#include "IO.h"
#include <cstdint>
#include <cstring>

extern WriteBufferEntry write_buffer_nxt[WB_ENTRIES];

struct WBState {
    uint32_t count; // number of valid entries in the buffer
    uint32_t head;  // index of the oldest entry (next to evict)
    uint32_t tail;  // index of the next free slot for new entry
    uint32_t send;  // flag to indicate if a request is currently being sent
    uint32_t issue_pending; // saw req_ready pulse, waiting acceptance confirmation

    uint32_t bypassdata[LSU_LDU_COUNT];
    bool bypassvalid[LSU_LDU_COUNT];

    bool mergevalid[LSU_STA_COUNT];
};

// AXI write-channel interface signals (IC's write_ports[MASTER_DCACHE_W]).
// axi_in  — inputs from IC to WriteBuffer (driven by RealDcache bridge).
// axi_out — outputs from WriteBuffer to IC (consumed by RealDcache bridge).
struct WbAxiIn {
    bool req_ready  = false;  // IC accepted the AW+W request
    bool resp_valid = false;  // B response available
};

struct WbAxiOut {
    bool     req_valid      = false;  // AW+W request to IC
    uint32_t req_addr       = 0;
    uint8_t  req_total_size = 0;
    uint8_t  req_id         = 0;
    uint32_t req_wstrb      = 0;
    uint32_t req_wdata[DCACHE_LINE_WORDS] = {};
    bool     resp_ready     = false;  // ready to accept B response
};


struct WBIn {
    MSHRWBIO mshrwb;
    DcacheWBIO dcachewb;
    WbAxiIn axi_in;

    void clear() {
        std::memset(this, 0, sizeof(*this));
    }
};
struct WBOut {
    WBMSHRIO wbmshr;
    WBDcacheIO wbdcache;
    WbAxiOut axi_out;

    void clear() {
        std::memset(this, 0, sizeof(*this));
    }
};
// ─────────────────────────────────────────────────────────────────────────────
// WriteBuffer — queues dirty evictions and drains them over the AXI write port.
//
// Correct usage per cycle:
//   1. wb_.comb_outputs()   ← sets out.full / out.free_count (from nxt.count)
//   2. stage2_comb()        ← reads out.full/free_count, sets in.push_ports[]
//   3. Bridge IC outputs → wb_.axi_in
//   4. wb_.comb_inputs()    ← processes pushes + B channel + fills axi_out
//   5. Bridge wb_.axi_out → IC inputs; interconnect.comb_inputs()
//   6. wb_.seq()            ← cur = nxt, reset nxt
// ─────────────────────────────────────────────────────────────────────────────
class WriteBuffer {
public:
    WriteBuffer() = default;
    int find_wb_entry(uint32_t addr);

    void init();

    // Phase 1: compute out.full and out.free_count from current nxt.count.
    void comb_outputs();

    // Phase 2: process push requests from in.push_ports[], accept B channel
    // responses, and fill axi_out with the next AW+W request.
    // Reads axi_in (set by caller before invoking); writes axi_out.
    void comb_inputs();

    void seq();

    // Input / output signal ports (public for direct access by RealDcache).
    WBIn  in;
    WBOut out;

    WBState cur, nxt;
};
