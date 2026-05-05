#pragma once

#include "config.h"
#include "types.h"
#include "WriteBuffer.h"
#include "DcacheConfig.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "IO.h"

struct MSHREntry {
    reg<1> valid;
    reg<1> issued;
    reg<32> addr;
};


struct MSHR_STATE{
    reg<DCACHE_MSHR_COUNT_BITS> mshr_count; 

    // Hold one blocked AXI read response when WB backpressure prevents
    // immediate consumption. This register is intentionally bounded to one slot.
    reg<1> axi_resp_hold_valid;
    reg<8> axi_resp_hold_id;
    reg<32> axi_resp_hold_data[DCACHE_WORD_NUM];

    reg<1> find_hit[LSU_LDU_COUNT + LSU_STA_COUNT]; // combinational hit result for each load/store slot
    MSHREntry mshr_entries[DCACHE_MSHR_ENTRIES];
};

struct MshrAxiIn {
    wire<1>     req_ready  = false;   // IC ready-first hint for AR request
    wire<1>     req_accepted = false; // true AR handshake committed in IC seq
    wire<8>     req_accepted_id = 0; // accepted request ID from IC
    wire<1>     resp_valid = false;   // R data available from IC
    wire<32>    resp_data[DCACHE_WORD_NUM] = {};
    wire<8>     resp_id    = 0;       // transaction ID echoed back
};

struct MshrAxiOut {
    wire<1>     req_valid      = false;  // AR request to IC
    wire<32>    req_addr       = 0;
    wire<8>     req_total_size = 0;
    wire<8>     req_id         = 0;
    wire<1>     resp_ready     = false;  // ready to accept R data
    void clear() {
        req_valid = false;
        req_addr = 0;
        req_total_size = 0;
        req_id = 0;
        resp_ready = false;
    }
};


struct MSHRINIO{
    DcacheMSHRIO* dcache2mshr;
    MshrAxiIn*  axi_in;
};
struct MSHROUTIO{
    MSHRDcacheIO* mshr2dcache;
    MshrAxiOut* axi_out;
};
class MSHR {
public:
    MSHR() = default;
    void init();

    void comb_outputs_axi();
    void comb_outputs_dcache();
    void comb_inputs_axi();
    void comb_inputs_dcache();
    void seq();

    MSHRINIO in;
    MSHROUTIO out;

    MSHR_STATE cur,nxt;

    void dump_debug_state(FILE *out) const;
};
