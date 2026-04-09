#include "WriteBuffer.h"
#include "PhysMemory.h"
#include "config.h"
#include "types.h"
#include <cassert>
#include <cstdio>
#include <cstring>

WriteBufferEntry write_buffer_nxt[DCACHE_WB_ENTRIES];

namespace {
static constexpr uint8_t kCacheLineReqTotalSize =
    static_cast<uint8_t>(DCACHE_LINE_BYTES - 1u);

static constexpr uint64_t full_line_wstrb_mask() {
    uint64_t mask = 0;
    for (uint32_t i = 0; i < DCACHE_LINE_BYTES && i < 64; i++) {
        mask |= (1ull << i);
    }
    return mask;
}

static int find_wb_entry_in_view(const WriteBufferEntry *entries, uint32_t head,
                                 uint32_t count, uint32_t addr) {
    int best_match = -1;
    const uint32_t line_addr = (addr & ~(DCACHE_LINE_BYTES - 1));
    for (uint32_t i = head, cnt = 0; cnt < count;
         i = (i + 1) % DCACHE_WB_ENTRIES, cnt++) {
        if (entries[i].valid && entries[i].addr == line_addr) {
            best_match = static_cast<int>(i);
        }
    }
    return best_match;
}

static void clear_axi_req(WBOut &out) {
    out.axi_out.req_valid = false;
    out.axi_out.req_addr = 0;
    out.axi_out.req_total_size = 0;
    out.axi_out.req_id = 0;
    out.axi_out.req_wstrb = 0;
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
        out.axi_out.req_wdata[w] = 0;
    }
}

static void drive_axi_req_from_head(WBOut &out, uint32_t send, uint32_t head,
                                    const WriteBufferEntry *entries) {
    clear_axi_req(out);
    out.axi_out.resp_ready = true;
    if (send != 0) {
        return;
    }
    const WriteBufferEntry &head_e = entries[head];
    if (!head_e.valid || head_e.send) {
        return;
    }
    out.axi_out.req_valid = true;
    out.axi_out.req_addr = head_e.addr;
    out.axi_out.req_total_size = kCacheLineReqTotalSize;
    out.axi_out.req_id = 0;
    out.axi_out.req_wstrb = full_line_wstrb_mask();
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
        out.axi_out.req_wdata[w] = head_e.data[w];
    }
}

static void check_wb_state(const char *phase, const WBState &st) {
    if (st.count > DCACHE_WB_ENTRIES || st.head >= DCACHE_WB_ENTRIES || st.tail >= DCACHE_WB_ENTRIES ||
        st.send > 1u || st.issue_pending > 1u) {
        LSU_MEM_DBG_PRINTF(
            "[WB STATE CORRUPT] phase=%s cyc=%lld count=%u head=%u tail=%u send=%u issue_pending=%u\n",
            phase, (long long)sim_time, st.count, st.head, st.tail, st.send,
            st.issue_pending);
        Assert(false && "WriteBuffer state corrupted");
    }
}
} // namespace

int WriteBuffer::find_wb_entry(uint32_t addr)
{
    return find_wb_entry_in_view(write_buffer, cur.head, cur.count, addr);
}
// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────
void WriteBuffer::init() {
    std::memset(&cur, 0, sizeof(cur));
    std::memset(&nxt, 0, sizeof(nxt));
    std::memset(write_buffer_nxt, 0, sizeof(write_buffer_nxt));
    in.clear();
    out.clear();
}
// ─────────────────────────────────────────────────────────────────────────────
// comb_outputs — Phase 1: compute full flag and free slot count.
//
// Uses nxt.count so that pushes committed in previous comb_inputs() calls
// (within the same cycle) are already reflected.
// ─────────────────────────────────────────────────────────────────────────────
void WriteBuffer::comb_outputs() {
    check_wb_state("comb_outputs.cur", cur);
    check_wb_state("comb_outputs.nxt", nxt);
    // MemSubsystem::comb() calls wb_.comb_outputs() again after wb_.comb_inputs()
    // and before mshr_.comb_inputs(). The MSHR therefore samples the refreshed
    // nxt-count view, and producing one victim in the next cycle is safe as
    // long as there is one actual slot left in the FIFO.
    out.wbmshr.ready = (nxt.count < DCACHE_WB_ENTRIES);

    for(int i=0;i<LSU_LDU_COUNT;i++){
        out.wbdcache.bypass_resp[i].valid = nxt.bypassvalid[i];
        out.wbdcache.bypass_resp[i].data = nxt.bypassdata[i];
    }

    for(int i=0;i<LSU_STA_COUNT;i++){
        out.wbdcache.merge_resp[i].valid = nxt.mergevalid[i];
        out.wbdcache.merge_resp[i].busy = nxt.mergebusy[i];
    }
    // Drive the request from nxt view so previously accumulated merges are
    // visible to the write beat snapshot.
    drive_axi_req_from_head(out, cur.send, cur.head, write_buffer_nxt);

}

// ─────────────────────────────────────────────────────────────────────────────
// comb_inputs — Phase 2: process push requests, accept B channel responses,
// and fill axi_out with the next AW+W request.
//
// Reads axi_in (bridged from IC by RealDcache before this call).
// Writes axi_out (bridged to IC by RealDcache after this call).
// ─────────────────────────────────────────────────────────────────────────────
void WriteBuffer::comb_inputs() {
    check_wb_state("comb_inputs.cur", cur);
    check_wb_state("comb_inputs.nxt.pre", nxt);
    // Default outputs: nothing to send, always ready to accept a write response.
    for(int i=0;i<LSU_LDU_COUNT;i++){
        nxt.bypassvalid[i] = false;
        nxt.bypassdata[i] = 0; // Or some default value if not found in the write buffer
    }

    
    
    for(int i=0;i<LSU_STA_COUNT;i++){
        nxt.mergevalid[i] = false;
        nxt.mergebusy[i] = false;
         if(in.dcachewb.merge_req[i].valid){
            int wb_idx = find_wb_entry_in_view(write_buffer_nxt, nxt.head,
                                               nxt.count,
                                               in.dcachewb.merge_req[i].addr);
            if(wb_idx != -1){
                WriteBufferEntry &e = write_buffer_nxt[wb_idx];
                const bool head_issue_frozen =
                    (wb_idx == static_cast<int>(cur.head)) &&
                    ((cur.issue_pending != 0u) || (nxt.issue_pending != 0u));
                // Ready-first AXI means the interconnect may capture the head
                // entry one cycle before WriteBuffer observes req_accepted.
                // Once that window opens, same-line merges must stop; otherwise
                // the buffer state moves ahead of the payload already captured
                // by the interconnect.
                if (e.send || head_issue_frozen) {
                    nxt.mergebusy[i] = true;
                } else {
                    nxt.mergevalid[i] = true;
                    uint32_t word_off = decode(in.dcachewb.merge_req[i].addr).word_off;
                    uint32_t strb = in.dcachewb.merge_req[i].strb;
                    apply_strobe(e.data[word_off], in.dcachewb.merge_req[i].data, strb);
                }
            }
            else if(cache_line_match(in.dcachewb.merge_req[i].addr,in.mshrwb.addr)&&in.mshrwb.valid){
                nxt.mergevalid[i] = true;
                uint32_t word_off = decode(in.dcachewb.merge_req[i].addr).word_off;
                uint32_t strb = in.dcachewb.merge_req[i].strb;
                apply_strobe(in.mshrwb.data[word_off], in.dcachewb.merge_req[i].data, strb);
            }
            
        }
    }
    

    if(in.mshrwb.valid){
        if(nxt.count < DCACHE_WB_ENTRIES){
            WriteBufferEntry &e = write_buffer_nxt[nxt.tail];
            e.valid    = true;
            e.send     = false;
            e.addr     = in.mshrwb.addr;
            std::memcpy(e.data, in.mshrwb.data, DCACHE_LINE_WORDS * sizeof(uint32_t));
            nxt.tail  = (nxt.tail + 1) % DCACHE_WB_ENTRIES;
            nxt.count++;
        }
        else{
            assert(false && "WriteBuffer overflow: MSHR is producing evictions faster than WriteBuffer can drain them");
        }
    }

    for(int i=0;i<LSU_LDU_COUNT;i++){
        if(in.dcachewb.bypass_req[i].valid){
            int wb_idx = find_wb_entry_in_view(write_buffer_nxt, nxt.head,
                                               nxt.count,
                                               in.dcachewb.bypass_req[i].addr);
            if(wb_idx != -1){
                nxt.bypassvalid[i] = true;
                nxt.bypassdata[i] = write_buffer_nxt[wb_idx].data[decode(in.dcachewb.bypass_req[i].addr).word_off];
            }
            else if(cache_line_match(in.dcachewb.bypass_req[i].addr,in.mshrwb.addr)&&in.mshrwb.valid){
                // Bypass from the MSHR fill data if the requested line matches the line being filled by the MSHR.
                nxt.bypassvalid[i] = true;
                nxt.bypassdata[i] = in.mshrwb.data[decode(in.dcachewb.bypass_req[i].addr).word_off];
            }
        }
    }

    // Rebuild current-cycle AXI request after same-cycle merges / pushes.
    drive_axi_req_from_head(out, cur.send, cur.head, write_buffer_nxt);

    // AXI interconnect uses ready-first timing, but only req.accepted means the
    // request has actually been captured by the interconnect.
    if (cur.send == 0) {
        WriteBufferEntry &head_e = write_buffer_nxt[cur.head];
        const bool can_issue_head = head_e.valid && !head_e.send;
        const bool req_payload_matches_head =
            out.axi_out.req_valid && (out.axi_out.req_addr == head_e.addr);
        const bool req_handshake = can_issue_head && in.axi_in.req_accepted &&
                                   req_payload_matches_head;
        if (can_issue_head && in.axi_in.req_accepted && !out.axi_out.req_valid) {
            LSU_MEM_DBG_PRINTF(
                "[AXI WRITE ISSUE WARN] cyc=%lld req_accepted=1 while req_valid=0 head=%u addr=0x%08x\n",
                (long long)sim_time, cur.head, head_e.addr);
        } else if (can_issue_head && in.axi_in.req_accepted &&
                   out.axi_out.req_valid &&
                   out.axi_out.req_addr != head_e.addr) {
            LSU_MEM_DBG_PRINTF(
                "[AXI WRITE ISSUE WARN] cyc=%lld req_accepted=1 but req_addr mismatch head=%u head_addr=0x%08x req_addr=0x%08x\n",
                (long long)sim_time, cur.head, head_e.addr,
                out.axi_out.req_addr);
        }
        if (req_handshake) {
            head_e.send = true;
            nxt.send = 1;
            nxt.issue_pending = 0;
        } else if (can_issue_head) {
            nxt.send = 0;
            nxt.issue_pending =
                (in.axi_in.req_ready && req_payload_matches_head) ? 1u : 0u;
        } else {
            nxt.issue_pending = 0;
        }
    } else {
        nxt.issue_pending = 0;
    }

    // ── Accept write response (B channel) ────────────────────────────────────
    if (in.axi_in.resp_valid) {
        WriteBufferEntry &head_e = write_buffer[cur.head];
        if (head_e.valid && head_e.send ) {
            write_buffer_nxt[cur.head].valid = false;
            write_buffer_nxt[cur.head].send = false;
            int new_head = (cur.head + 1) % DCACHE_WB_ENTRIES;
            nxt.head  = new_head;
            if (nxt.count > 0) {
                nxt.count--;
            }
        } else {
            LSU_MEM_DBG_PRINTF(
                "[AXI WRITE RESP UNEXPECTED] cyc=%lld head=%u head_valid=%d head_send=%d cur_send=%u count=%u req_ready=%d resp_valid=%d\n",
                (long long)sim_time, cur.head, static_cast<int>(head_e.valid),
                static_cast<int>(head_e.send), cur.send, cur.count,
                static_cast<int>(in.axi_in.req_ready),
                static_cast<int>(in.axi_in.resp_valid));
        }
        nxt.send = 0; // allow sending (or retrying) the head entry
        nxt.issue_pending = 0;
    }

    check_wb_state("comb_inputs.nxt.post", nxt);

    // ── Issue write request (AW + W) ─────────────────────────────────────────
    // Walk the FIFO from nxt.head to find the first unsent entry.
    
}

// ─────────────────────────────────────────────────────────────────────────────
// seq
// ─────────────────────────────────────────────────────────────────────────────
void WriteBuffer::seq() {
    check_wb_state("seq.cur.pre", cur);
    check_wb_state("seq.nxt.pre", nxt);
    cur = nxt;
    memcpy(write_buffer, write_buffer_nxt, sizeof(write_buffer));
    nxt = cur; 
    check_wb_state("seq.cur.post", cur);
    check_wb_state("seq.nxt.post", nxt);
}
