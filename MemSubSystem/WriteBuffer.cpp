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
} // namespace

int WriteBuffer::find_wb_entry(uint32_t addr)
{
    int best_match = -1;
    const uint32_t line_addr = (addr & ~(DCACHE_LINE_BYTES - 1));
    for(int i = cur.head, cnt = 0; cnt < cur.count; i = (i + 1) % WB_ENTRIES, cnt++){
        if(write_buffer[i].valid && (write_buffer[i].addr == line_addr)){
            best_match = i;
        }
    }
    return best_match; // Not found => -1
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
    }
    out.axi_out.req_valid  = false;
    out.axi_out.resp_ready = true;
    if(cur.send == 0){
        // Use nxt view so same-cycle merge updates are visible to the issued beat.
        const WriteBufferEntry &ne_check = write_buffer[cur.head];
        if (ne_check.valid && !ne_check.send){
            out.axi_out.req_valid      = true;
            out.axi_out.req_addr       = ne_check.addr;
            out.axi_out.req_total_size = 31; // 32B cacheline
            out.axi_out.req_id         = 0;
            out.axi_out.req_wstrb      = 0xffffffffu;
            for (int w = 0; w < DCACHE_LINE_WORDS; w++)
                out.axi_out.req_wdata[w] = ne_check.data[w];
        }
    }

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

    // AXI interconnect uses ready-first handshake:
    // if req.ready is observed while req.valid is held, treat it as accepted.
    if (cur.send == 0) {
        const WriteBufferEntry &head_e = write_buffer[cur.head];
        const bool can_issue_head = head_e.valid && !head_e.send;
        if (can_issue_head && in.axi_in.req_ready) {
            if (!out.axi_out.req_valid) {
                std::printf(
                    "[AXI WRITE ISSUE WARN] cyc=%lld req_ready=1 while req_valid=0 head=%u addr=0x%08x\n",
                    (long long)sim_time, cur.head, head_e.addr);
            }
            write_buffer_nxt[cur.head].send = true;
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

    // Default outputs: nothing to send, always ready to accept a write response.
    for(int i=0;i<LSU_LDU_COUNT;i++){
        nxt.bypassvalid[i] = false;
        nxt.bypassdata[i] = 0; // Or some default value if not found in the write buffer
        if(in.dcachewb.bypass_req[i].valid){
            int wb_idx = find_wb_entry(in.dcachewb.bypass_req[i].addr);
            if(wb_idx != -1){
                nxt.bypassvalid[i] = true;
                nxt.bypassdata[i] = write_buffer[wb_idx].data[decode(in.dcachewb.bypass_req[i].addr).word_off]; // For simplicity, we only support bypassing the first word in the cache line. Extend as needed.
            }
            else if(cache_line_match(in.dcachewb.bypass_req[i].addr,in.mshrwb.addr)&&in.mshrwb.valid){
                // Bypass from the MSHR fill data if the requested line matches the line being filled by the MSHR.
                nxt.bypassvalid[i] = true;
                nxt.bypassdata[i] = in.mshrwb.data[decode(in.dcachewb.bypass_req[i].addr).word_off];
            }
        }
    }

    
    
    for(int i=0;i<LSU_STA_COUNT;i++){
        nxt.mergevalid[i] = false;
         if(in.dcachewb.merge_req[i].valid){
            int wb_idx = find_wb_entry(in.dcachewb.merge_req[i].addr);
            if(wb_idx != -1 && !write_buffer_nxt[wb_idx].send){ // Only merge unsent WB entry
                nxt.mergevalid[i] = true;
                WriteBufferEntry &e = write_buffer_nxt[wb_idx];
                uint32_t word_off = decode(in.dcachewb.merge_req[i].addr).word_off;
                uint32_t strb = in.dcachewb.merge_req[i].strb;
                apply_strobe(e.data[word_off], in.dcachewb.merge_req[i].data, strb);
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
