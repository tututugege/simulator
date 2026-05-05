#include "WriteBuffer.h"
#include "PhysMemory.h"
#include "config.h"
#include "types.h"
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
    out.wb2dcache->free = DCACHE_WB_ENTRIES - cur.count;
    for(int i=0;i<LSU_LDU_COUNT;i++){
        out.wb2dcache->bypass_resp[i].valid = cur.bypassvalid[i];
        out.wb2dcache->bypass_resp[i].data = cur.bypassdata[i];
    }

    for(int i=0;i<LSU_STA_COUNT;i++){
        out.wb2dcache->merge_resp[i].valid = cur.mergevalid[i];
        out.wb2dcache->merge_resp[i].busy = cur.mergebusy[i];
    }
}
void WriteBuffer::comb_outputs_axi() {
    out.axi_out->resp_ready = true;
    if (cur.send != 0) {
        return;
    }
    if (cur.count == 0) {
        return;
    }
    const WriteBufferEntry &head_e = cur.write_buffer[cur.head];
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
    if(in.axi_in->req_accepted){
        nxt.send = 1; // request has been accepted, wait for response before sending next
    }
    if (in.axi_in->resp_valid) {
        nxt.send = 0; // allow sending (or retrying) the head entry
    }
}

void WriteBuffer::comb_inputs_dcache() {
    
    for(int i=0;i<LSU_LDU_COUNT;i++){
        if(in.dcache2wb->bypass_req[i].valid){
            int wb_idx = find_wb_entry(cur.write_buffer, cur.head,cur.count,in.dcache2wb->bypass_req[i].addr);
            if(valid_wb_entry_index(wb_idx)){
                nxt.bypassvalid[i] = true;
                nxt.bypassdata[i] = cur.write_buffer[wb_idx].data[decode(in.dcache2wb->bypass_req[i].addr).word_off];
            }
            else{
                nxt.bypassvalid[i] = false;
                nxt.bypassdata[i] = 0;
            }
        }
        else{
            nxt.bypassvalid[i] = false;
            nxt.bypassdata[i] = 0;
        }
    }


    for(int i=0;i<LSU_STA_COUNT;i++){
        if(in.dcache2wb->merge_req[i].valid){
            int wb_idx = find_wb_entry(cur.write_buffer, cur.head,cur.count,in.dcache2wb->merge_req[i].addr);
            if(valid_wb_entry_index(wb_idx)){

                if(wb_idx == cur.head&&cur.count > 0){
                    nxt.mergebusy[i] = true; // the entry being merged is currently being sent out, mark as busy to stall new merges until we know if the current one will be accepted or not
                    nxt.mergevalid[i] = false; // the merge can't be accepted in the same cycle as the send, even if the address matches, to avoid a
                }
                else{
                    nxt.mergebusy[i] = false;
                    nxt.mergevalid[i] = true;
                    WriteBufferEntry &e = nxt.write_buffer[wb_idx];
                    uint32_t word_off = decode(in.dcache2wb->merge_req[i].addr).word_off;
                    uint32_t strb = in.dcache2wb->merge_req[i].strb;
                    apply_strobe(e.data[word_off], in.dcache2wb->merge_req[i].data, strb);
                }
            }
            else 
            {
                nxt.mergevalid[i] = false;
                nxt.mergebusy[i] = false;
            }
        }
        else{
            nxt.mergevalid[i] = false;
            nxt.mergebusy[i] = false;
        }
    }
    
    if(in.dcache2wb->dirty_info.valid){
        if(cur.count < DCACHE_WB_ENTRIES){
            WriteBufferEntry &e = nxt.write_buffer[(cur.head + nxt.count) % DCACHE_WB_ENTRIES];
            e.addr     = in.dcache2wb->dirty_info.addr;
            std::memcpy(e.data, in.dcache2wb->dirty_info.data, DCACHE_WORD_NUM * sizeof(uint32_t));
        }
        else{
            #if !BSD_CONFIG
                assert(false && "WriteBuffer overflow: MSHR is producing evictions faster than WriteBuffer can drain them");
            #endif
        }
    }

    uint32_t new_count = cur.count;
    if(in.axi_in->resp_valid){
        if(new_count > 0){
            new_count = new_count - 1;
            nxt.head = (cur.head + 1) % DCACHE_WB_ENTRIES;
        }
        else{
            #if !BSD_CONFIG
                assert(false && "WriteBuffer underflow: received AXI response when buffer is empty");
            #endif
        }
    }
    if(in.dcache2wb->dirty_info.valid && cur.count < DCACHE_WB_ENTRIES){
        new_count = new_count + 1;
    }
    nxt.count = new_count;
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