#include "WriteBuffer.h"
#include "types.h"
#include <cassert>
#include <cstdio>
#include <cstring>

extern uint32_t *p_memory;

WriteBufferEntry write_buffer_nxt[WB_ENTRIES];

namespace {
static inline void dump_line_words(const char *tag, const uint32_t *data) {
    std::printf("%s[", tag);
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
        std::printf("%s%08x", (w == 0) ? "" : " ", data[w]);
    }
    std::printf("]\n");
}

static int find_wb_entry_in_view(const WriteBufferEntry *entries, uint32_t head,
                                 uint32_t count, uint32_t addr) {
    int best_match = -1;
    const uint32_t line_addr = (addr & ~(DCACHE_LINE_BYTES - 1));
    for (uint32_t i = head, cnt = 0; cnt < count;
         i = (i + 1) % WB_ENTRIES, cnt++) {
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
    out.axi_out.req_total_size = 31;
    out.axi_out.req_id = 0;
    out.axi_out.req_wstrb = 0xffffffffu;
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
        out.axi_out.req_wdata[w] = head_e.data[w];
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
    std::memset(&cur_check, 0, sizeof(cur_check));
    std::memset(&nxt_check, 0, sizeof(nxt_check));
    std::memset(&cur_issue, 0, sizeof(cur_issue));
    std::memset(&nxt_issue, 0, sizeof(nxt_issue));
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
    out.wbmshr.ready = (nxt.count < WB_ENTRIES);

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
    // Deferred verification: check one cycle after B response, so backing
    // memory has passed through interconnect/memory seq updates.
    if (cur_check.valid && p_memory != nullptr) {
        const uint32_t line_addr = cur_check.addr;
        const uint32_t word_base = (line_addr >> 2);
        bool write_mismatch = false;
        int first_bad = -1;
        uint32_t mem_word = 0;
        uint32_t exp_word = 0;
        uint32_t mem_line[DCACHE_LINE_WORDS] = {};
        for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
            mem_line[w] = p_memory[word_base + w];
            if (!write_mismatch && mem_line[w] != cur_check.data[w]) {
                write_mismatch = true;
                first_bad = w;
                mem_word = mem_line[w];
                exp_word = cur_check.data[w];
            }
        }
        if (write_mismatch) {
            std::printf(
                "[AXI WRITE MISMATCH] cyc=%lld resp_cyc=%llu line_addr=0x%08x word=%d exp=0x%08x mem=0x%08x\n",
                (long long)sim_time, (unsigned long long)cur_check.resp_cycle,
                line_addr, first_bad, exp_word, mem_word);
            std::printf(
                "[AXI WRITE MISMATCH][WB_STATE] cur{count=%u head=%u tail=%u send=%u issue_pending=%u} nxt{count=%u head=%u tail=%u send=%u issue_pending=%u}\n",
                cur.count, cur.head, cur.tail, cur.send, cur.issue_pending,
                nxt.count, nxt.head, nxt.tail, nxt.send, nxt.issue_pending);
            if (cur_issue.valid) {
                int issue_bad = -1;
                for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
                    if (cur_issue.data[w] != cur_check.data[w]) {
                        issue_bad = w;
                        break;
                    }
                }
                std::printf(
                    "[AXI WRITE MISMATCH][ISSUE] issue_cyc=%llu head=%u addr=0x%08x issue_vs_resp_word=%d\n",
                    (unsigned long long)cur_issue.issue_cycle, cur_issue.head,
                    cur_issue.addr, issue_bad);
                dump_line_words("[AXI WRITE MISMATCH][ISSUE_DATA] ", cur_issue.data);
            } else {
                std::printf("[AXI WRITE MISMATCH][ISSUE] no in-flight issue snapshot captured\n");
            }
            dump_line_words("[AXI WRITE MISMATCH][EXP] ", cur_check.data);
            dump_line_words("[AXI WRITE MISMATCH][MEM] ", mem_line);
            Assert(false && "WriteBuffer AXI write mismatch after deferred verification. Likely root causes: write response accepted before backing memory commit, incorrect line payload/address tracking across WB head movement, or AXI write-path ordering bug.");
        }
    }
    nxt_check = {};
    nxt_issue = cur_issue;

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
                if (e.send) {
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
        if(nxt.count < WB_ENTRIES){
            WriteBufferEntry &e = write_buffer_nxt[nxt.tail];
            e.valid    = true;
            e.send     = false;
            e.addr     = in.mshrwb.addr;
            std::memcpy(e.data, in.mshrwb.data, DCACHE_LINE_WORDS * sizeof(uint32_t));
            nxt.tail  = (nxt.tail + 1) % WB_ENTRIES;
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

    // AXI interconnect uses ready-first handshake:
    // if req.ready is observed while req.valid is held, treat it as accepted.
    if (cur.send == 0) {
        WriteBufferEntry &head_e = write_buffer_nxt[cur.head];
        const bool can_issue_head = head_e.valid && !head_e.send;
        if (can_issue_head && in.axi_in.req_ready) {
            if (!out.axi_out.req_valid) {
                std::printf(
                    "[AXI WRITE ISSUE WARN] cyc=%lld req_ready=1 while req_valid=0 head=%u addr=0x%08x\n",
                    (long long)sim_time, cur.head, head_e.addr);
            }
            head_e.send = true;
            nxt.send = 1;
            nxt.issue_pending = 1;
            nxt_issue.valid = true;
            nxt_issue.addr = head_e.addr;
            nxt_issue.head = cur.head;
            nxt_issue.issue_cycle = (uint64_t)sim_time;
            if (out.axi_out.req_valid) {
                std::memcpy(nxt_issue.data, out.axi_out.req_wdata, sizeof(nxt_issue.data));
            } else {
                std::memcpy(nxt_issue.data, head_e.data, sizeof(nxt_issue.data));
            }
        } else if (can_issue_head) {
            nxt.send = 0;
            nxt.issue_pending = 0;
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
            // Delay the memory-consistency check by one cycle.
            nxt_check.valid = true;
            nxt_check.addr = head_e.addr;
            nxt_check.resp_cycle = (uint64_t)sim_time;
            std::memcpy(nxt_check.data, head_e.data, sizeof(head_e.data));
            if (ctx != nullptr && cur_issue.valid) {
                const uint64_t now = static_cast<uint64_t>(sim_time);
                if (now >= cur_issue.issue_cycle) {
                    ctx->perf.l1d_axi_write_total_cycles +=
                        (now - cur_issue.issue_cycle);
                    ctx->perf.l1d_axi_write_samples++;
                }
            }
            write_buffer_nxt[cur.head].valid = false;
            write_buffer_nxt[cur.head].send = false;
            int new_head = (cur.head + 1) % WB_ENTRIES;
            nxt.head  = new_head;
            if (nxt.count > 0) {
                nxt.count--;
            }
            nxt_issue.valid = false;
        } else {
            std::printf(
                "[AXI WRITE RESP UNEXPECTED] cyc=%lld head=%u head_valid=%d head_send=%d cur_send=%u count=%u req_ready=%d resp_valid=%d\n",
                (long long)sim_time, cur.head, static_cast<int>(head_e.valid),
                static_cast<int>(head_e.send), cur.send, cur.count,
                static_cast<int>(in.axi_in.req_ready),
                static_cast<int>(in.axi_in.resp_valid));
            if (cur_issue.valid) {
                std::printf(
                    "[AXI WRITE RESP UNEXPECTED][ISSUE] issue_cyc=%llu issue_head=%u issue_addr=0x%08x\n",
                    (unsigned long long)cur_issue.issue_cycle, cur_issue.head,
                    cur_issue.addr);
                dump_line_words("[AXI WRITE RESP UNEXPECTED][ISSUE_DATA] ", cur_issue.data);
            }
        }
        nxt.send = 0; // allow sending (or retrying) the head entry
        nxt.issue_pending = 0;
    }

    // ── Issue write request (AW + W) ─────────────────────────────────────────
    // Walk the FIFO from nxt.head to find the first unsent entry.
    
}

// ─────────────────────────────────────────────────────────────────────────────
// seq
// ─────────────────────────────────────────────────────────────────────────────
void WriteBuffer::seq() {
    cur = nxt;
    cur_check = nxt_check;
    cur_issue = nxt_issue;
    memcpy(write_buffer, write_buffer_nxt, sizeof(write_buffer));
    nxt = cur; 
    nxt_check = cur_check;
    nxt_issue = cur_issue;
}
