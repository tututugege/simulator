#include "WriteBuffer.h"
#include "PhysMemory.h"
#include "config.h"
#if !BSD_CONFIG
#include "types.h"
#endif
#include <cassert>
#include <cstring>

namespace {

static constexpr uint64_t full_line_wstrb_mask() {
    uint64_t mask = 0;
    for (uint32_t i = 0; i < DCACHE_LINE_SIZE && i < 64; i++) {
        mask |= (1ull << i);
    }
    return mask;
}

static uint32_t find_wb_entry(const WriteBufferEntry *entries, uint32_t head,
                                 uint32_t count, uint32_t addr) {
    if(count == 0){
        return DCACHE_WB_ENTRIES; // no valid entry
    }
    uint32_t best_match = DCACHE_WB_ENTRIES;
    const uint32_t line_addr = (addr & ~(DCACHE_LINE_SIZE - 1));
    for (uint32_t i = head, cnt = 0; cnt < count;
         i = (i + 1) % DCACHE_WB_ENTRIES, cnt++) {
        if (entries[i].addr == line_addr) {
            best_match = i;
        }
    }
    return best_match;
}

static bool valid_wb_entry_index(uint32_t idx) {
    return idx < DCACHE_WB_ENTRIES;
} 
}// namespace

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────
void WriteBuffer::init() {
    std::memset(&cur, 0, sizeof(cur));
    std::memset(&nxt, 0, sizeof(nxt));
    out.clear();
}
void WriteBuffer::comb_outputs_dcache() {
    
    out.clear();
    out.wb2dcache->free = DCACHE_WB_ENTRIES - nxt.count;
    for(int i=0;i<LSU_LDU_COUNT;i++){
        nxt.bypassvalid[i] = false;
        nxt.bypassdata[i] = 0;
        if(in.dcache2wb->bypass_req[i].valid){
            int wb_idx = find_wb_entry(nxt.write_buffer, nxt.head, nxt.count,
                                       in.dcache2wb->bypass_req[i].addr);
            if(valid_wb_entry_index(wb_idx)){
                nxt.bypassvalid[i] = true;
                nxt.bypassdata[i] =
                    nxt.write_buffer[wb_idx]
                        .data[decode(in.dcache2wb->bypass_req[i].addr)
                                  .word_off];
            }
        }
        out.wb2dcache->bypass_resp[i].valid = nxt.bypassvalid[i];
        out.wb2dcache->bypass_resp[i].data = nxt.bypassdata[i];
    }

    for(int i=0;i<LSU_STA_COUNT;i++){
        nxt.mergevalid[i] = false;
        nxt.mergebusy[i] = false;
        if(in.dcache2wb->merge_req[i].valid){
            int wb_idx = find_wb_entry(nxt.write_buffer, nxt.head, nxt.count,
                                       in.dcache2wb->merge_req[i].addr);
            if(valid_wb_entry_index(wb_idx)){
                if(wb_idx == nxt.head && nxt.count > 0 && nxt.send != 0){
                    nxt.mergebusy[i] = true;
                }
                else{
                    nxt.mergevalid[i] = true;
                    WriteBufferEntry &e = nxt.write_buffer[wb_idx];
                    uint32_t word_off =
                        decode(in.dcache2wb->merge_req[i].addr).word_off;
                    uint32_t strb = in.dcache2wb->merge_req[i].strb;
                    apply_strobe(e.data[word_off],
                                 in.dcache2wb->merge_req[i].data, strb);
                }
            }
        }
        out.wb2dcache->merge_resp[i].valid = nxt.mergevalid[i];
        out.wb2dcache->merge_resp[i].busy = nxt.mergebusy[i];
    }
}
void WriteBuffer::comb_outputs_axi() {
    out.axi_out->clear();
    out.axi_out->resp_ready = true;
    if (nxt.send != 0) {
        return;
    }
    if (nxt.count == 0) {
        return;
    }
    const WriteBufferEntry &head_e = nxt.write_buffer[nxt.head];
    out.axi_out->req_valid = true;
    out.axi_out->req_addr = head_e.addr;
    out.axi_out->req_total_size = DCACHE_LINE_SIZE - 1;
    out.axi_out->req_id = 0;
    out.axi_out->req_wstrb = full_line_wstrb_mask();
    for (int w = 0; w < DCACHE_WORD_NUM; w++) {
        out.axi_out->req_wdata[w] = head_e.data[w];
    }

}

void WriteBuffer::comb_inputs_axi(){
    const bool req_accepted = in.axi_in->req_accepted;
    const bool accepted_head = req_accepted && cur.count > 0;
    const bool resp_valid = in.axi_in->resp_valid;

    if(req_accepted){
        if (out.axi_out->req_valid) {
            out.axi_out->req_valid = false;
            out.axi_out->req_addr = 0;
            out.axi_out->req_total_size = 0;
            out.axi_out->req_id = 0;
            out.axi_out->req_wstrb = 0;
            std::memset(out.axi_out->req_wdata, 0,
                        sizeof(out.axi_out->req_wdata));
        }
        nxt.send = 1; // request has been accepted, wait for response before sending next
#if !BSD_CONFIG
        nxt.axi_write_active = true;
        nxt.axi_write_lsu_origin =
            cur.count > 0 ? cur.write_buffer[cur.head].lsu_origin : false;
        nxt.axi_write_start_cycle = ctx == nullptr ? 0 : ctx->perf.cycle;
#endif
    }
    if (resp_valid) {
#if !BSD_CONFIG
        if (ctx != nullptr) {
            const bool lsu_origin =
                cur.axi_write_active ? cur.axi_write_lsu_origin
                                     : (accepted_head &&
                                        cur.write_buffer[cur.head].lsu_origin);
            const uint64_t start_cycle =
                cur.axi_write_active ? cur.axi_write_start_cycle
                                     : (ctx == nullptr ? 0 : ctx->perf.cycle);
            if (lsu_origin && ctx->perf.cycle >= start_cycle) {
                ctx->perf.l1d_axi_write_total_cycles +=
                    ctx->perf.cycle - start_cycle;
                ctx->perf.l1d_axi_write_samples++;
            }
        }
#endif
        if (nxt.count > 0) {
            nxt.count = nxt.count - 1;
            nxt.head = (nxt.head + 1) % DCACHE_WB_ENTRIES;
        }
        else{
#if !BSD_CONFIG
            assert(false && "WriteBuffer underflow: received AXI response when buffer is empty");
#endif
        }
        nxt.send = 0; // allow sending (or retrying) the head entry
#if !BSD_CONFIG
        nxt.axi_write_active = false;
        nxt.axi_write_lsu_origin = false;
        nxt.axi_write_start_cycle = 0;
#endif
    }
}

void WriteBuffer::comb_inputs_dcache() {
    
    if(in.dcache2wb->dirty_info.valid){
        if(nxt.count < DCACHE_WB_ENTRIES){
            WriteBufferEntry &e = nxt.write_buffer[(nxt.head + nxt.count) % DCACHE_WB_ENTRIES];
            e.addr     = in.dcache2wb->dirty_info.addr;
#if !BSD_CONFIG
            e.lsu_origin = in.dcache2wb->dirty_info.lsu_origin;
#endif
            std::memcpy(e.data, in.dcache2wb->dirty_info.data, DCACHE_WORD_NUM * sizeof(uint32_t));
        }
        else{
            #if !BSD_CONFIG
                assert(false && "WriteBuffer overflow: MSHR is producing evictions faster than WriteBuffer can drain them");
            #endif
        }
    }
    if(in.dcache2wb->dirty_info.valid && nxt.count < DCACHE_WB_ENTRIES){
        nxt.count = nxt.count + 1;
    }

}

void WriteBuffer::seq() {
    cur = nxt;
}
void WriteBuffer::dump_debug_state(FILE *out) const {
    fprintf(out, "WriteBuffer State:\n");
    fprintf(out, "  Count: %u\n", cur.count);
    fprintf(out, "  Head: %u\n", cur.head);
    fprintf(out, "  Send: %d\n", cur.send);
    for (uint32_t i = 0; i < DCACHE_WB_ENTRIES; i++) {
        const WriteBufferEntry &e = cur.write_buffer[i];
        fprintf(out, "  Entry %u: Addr: 0x%08x, Data:", i, e.addr);
        for (int w = 0; w < DCACHE_WORD_NUM; w++) {
            fprintf(out, " 0x%08x", e.data[w]);
        }
        fprintf(out, "\n");
    }
}
