#include "WriteBuffer.h"
#include "PhysMemory.h"
#include "config.h"
#include "types.h"
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>

WriteBufferEntry write_buffer_nxt[WB_ENTRIES];

namespace {
static uint64_t g_wb_issue_seq = 0;
static uint64_t g_wb_resp_seq = 0;
static bool g_warned_req_size_gt_32b = false;

static inline void dump_line_words(const char *tag, const uint32_t *data) {
    LSU_MEM_DBG_PRINTF("%s[", tag);
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
        LSU_MEM_DBG_PRINTF("%s%08x", (w == 0) ? "" : " ", data[w]);
    }
    LSU_MEM_DBG_PRINTF("]\n");
}

static inline int first_word_diff(const uint32_t *a, const uint32_t *b) {
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
        if (a[w] != b[w]) {
            return w;
        }
    }
    return -1;
}

static inline bool has_nonzero_words_from(const uint32_t *data, int start_word) {
    for (int w = start_word; w < DCACHE_LINE_WORDS; w++) {
        if (data[w] != 0) {
            return true;
        }
    }
    return false;
}

static inline void dump_issue_trace(const char *tag, const WbIssueTrace &t) {
    if (!t.valid) {
        LSU_MEM_DBG_PRINTF("%s<invalid>\n", tag);
        return;
    }
    LSU_MEM_DBG_PRINTF(
        "%sseq=%" PRIu64 " cyc=%" PRIu64 " head=%u addr=0x%08x total_size=%u "
        "wstrb=0x%016" PRIx64 "\n",
        tag, t.seq, t.issue_cycle, t.head, t.addr, t.req_total_size,
        static_cast<uint64_t>(t.req_wstrb));
    dump_line_words("[AXI TRACE][ISSUE_DATA] ", t.data);
}

static inline void dump_resp_trace(const char *tag, const WbRespTrace &t) {
    if (!t.valid) {
        LSU_MEM_DBG_PRINTF("%s<invalid>\n", tag);
        return;
    }
    LSU_MEM_DBG_PRINTF(
        "%sseq=%" PRIu64 " cyc=%" PRIu64 " head=%u addr=0x%08x "
        "matched_issue_seq=%" PRIu64 " matched_issue_cyc=%" PRIu64 "\n",
        tag, t.seq, t.resp_cycle, t.head, t.addr, t.issue_seq, t.issue_cycle);
    dump_line_words("[AXI TRACE][RESP_DATA] ", t.data);
}

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
    out.axi_out.req_total_size = kCacheLineReqTotalSize;
    out.axi_out.req_id = 0;
    out.axi_out.req_wstrb = full_line_wstrb_mask();
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
        out.axi_out.req_wdata[w] = head_e.data[w];
    }
}

static void check_wb_state(const char *phase, const WBState &st) {
    if (st.count > WB_ENTRIES || st.head >= WB_ENTRIES || st.tail >= WB_ENTRIES ||
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
    cur_check = {};
    nxt_check = {};
    cur_issue = {};
    nxt_issue = {};
    cur_last_issue = {};
    nxt_last_issue = {};
    cur_last_resp = {};
    nxt_last_resp = {};
    std::memset(write_buffer_nxt, 0, sizeof(write_buffer_nxt));
    g_wb_issue_seq = 0;
    g_wb_resp_seq = 0;
    g_warned_req_size_gt_32b = false;
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
    check_wb_state("comb_inputs.cur", cur);
    check_wb_state("comb_inputs.nxt.pre", nxt);
    // Deferred verification: check one cycle after B response, so backing
    // memory has passed through interconnect/memory seq updates.
    //
    // This is only valid when the WB drains directly to backing memory.
    // With LLC enabled the B response means the LLC accepted the write-back,
    // not that DDR backing memory has already been updated. In that mode the
    // dirty line can legitimately remain newer in LLC than in p_memory until a
    // later LLC eviction reaches memory, so comparing against p_memory here
    // would produce false mismatches.
    if (cur_check.valid && pmem_ram_ptr() != nullptr) {
#if CONFIG_AXI_LLC_ENABLE
        cur_check.valid = false;
#else
        const uint32_t line_addr = cur_check.addr;
        bool write_mismatch = false;
        int first_bad = -1;
        uint32_t mem_word = 0;
        uint32_t exp_word = 0;
        uint32_t mem_line[DCACHE_LINE_WORDS] = {};
        for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
            mem_line[w] = pmem_read(line_addr + static_cast<uint32_t>(w * 4));
            if (!write_mismatch && mem_line[w] != cur_check.data[w]) {
                write_mismatch = true;
                first_bad = w;
                mem_word = mem_line[w];
                exp_word = cur_check.data[w];
            }
        }
        if (write_mismatch) {
            LSU_MEM_DBG_PRINTF(
                "[AXI WRITE MISMATCH] cyc=%lld resp_cyc=%llu line_addr=0x%08x word=%d exp=0x%08x mem=0x%08x\n",
                (long long)sim_time, (unsigned long long)cur_check.resp_cycle,
                line_addr, first_bad, exp_word, mem_word);
            LSU_MEM_DBG_PRINTF(
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
                LSU_MEM_DBG_PRINTF(
                    "[AXI WRITE MISMATCH][ISSUE] issue_cyc=%llu head=%u addr=0x%08x issue_vs_resp_word=%d\n",
                    (unsigned long long)cur_issue.issue_cycle, cur_issue.head,
                    cur_issue.addr, issue_bad);
                dump_line_words("[AXI WRITE MISMATCH][ISSUE_DATA] ", cur_issue.data);
            } else {
                LSU_MEM_DBG_PRINTF("[AXI WRITE MISMATCH][ISSUE] no in-flight issue snapshot captured\n");
            }
            dump_issue_trace("[AXI WRITE MISMATCH][LAST_ISSUE] ", cur_last_issue);
            dump_resp_trace("[AXI WRITE MISMATCH][LAST_RESP] ", cur_last_resp);
            const int last_issue_diff =
                cur_last_issue.valid ? first_word_diff(cur_last_issue.data, cur_check.data) : -1;
            LSU_MEM_DBG_PRINTF(
                "[AXI WRITE MISMATCH][CHECK_CTX] check_addr=0x%08x "
                "check_vs_last_issue_word=%d hi8_exp_nonzero=%d hi8_mem_nonzero=%d\n",
                cur_check.addr, last_issue_diff,
                static_cast<int>(has_nonzero_words_from(cur_check.data, 8)),
                static_cast<int>(has_nonzero_words_from(mem_line, 8)));
            bool low32b_match = true;
            for (int w = 0; w < DCACHE_LINE_WORDS && w < 8; w++) {
                if (cur_check.data[w] != mem_line[w]) {
                    low32b_match = false;
                    break;
                }
            }
            if (first_bad >= 8 && low32b_match) {
                LSU_MEM_DBG_PRINTF(
                    "[AXI WRITE MISMATCH][HINT] first mismatch is at word %d (>=8) "
                    "while low 32B matches. Suspect 32B write-path truncation "
                    "between WB and AXI interconnect bridge.\n",
                    first_bad);
            }
            dump_line_words("[AXI WRITE MISMATCH][EXP] ", cur_check.data);
            dump_line_words("[AXI WRITE MISMATCH][MEM] ", mem_line);
            Assert(false && "WriteBuffer AXI write mismatch after deferred verification. Likely root causes: write response accepted before backing memory commit, incorrect line payload/address tracking across WB head movement, or AXI write-path ordering bug.");
        }
#endif
    }
    nxt_check = {};
    nxt_issue = cur_issue;
    nxt_last_issue = cur_last_issue;
    nxt_last_resp = cur_last_resp;

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
            nxt_issue.valid = true;
            nxt_issue.seq = ++g_wb_issue_seq;
            nxt_issue.addr = head_e.addr;
            nxt_issue.head = cur.head;
            nxt_issue.issue_cycle = (uint64_t)sim_time;
            nxt_issue.req_total_size = out.axi_out.req_total_size;
            nxt_issue.req_wstrb = out.axi_out.req_wstrb;
            if (out.axi_out.req_valid) {
                std::memcpy(nxt_issue.data, out.axi_out.req_wdata, sizeof(nxt_issue.data));
            } else {
                std::memcpy(nxt_issue.data, head_e.data, sizeof(nxt_issue.data));
            }
            nxt_last_issue = nxt_issue;
            if (!g_warned_req_size_gt_32b && out.axi_out.req_total_size > 31) {
                g_warned_req_size_gt_32b = true;
                LSU_MEM_DBG_PRINTF(
                    "[AXI WRITE ISSUE WARN] cyc=%lld req_total_size=%u (>31) "
                    "means write larger than 32B. Please verify bridge/interconnect "
                    "write payload width is not capped at 8 words.\n",
                    (long long)sim_time,
                    static_cast<unsigned>(out.axi_out.req_total_size));
            }
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
            // Delay the memory-consistency check by one cycle.
            nxt_check.valid = true;
            nxt_check.addr = head_e.addr;
            nxt_check.resp_cycle = (uint64_t)sim_time;
            std::memcpy(nxt_check.data, head_e.data, sizeof(head_e.data));
            nxt_last_resp.valid = true;
            nxt_last_resp.seq = ++g_wb_resp_seq;
            nxt_last_resp.addr = head_e.addr;
            nxt_last_resp.head = cur.head;
            nxt_last_resp.resp_cycle = static_cast<uint64_t>(sim_time);
            std::memcpy(nxt_last_resp.data, head_e.data, sizeof(head_e.data));
            nxt_last_resp.issue_seq = 0;
            nxt_last_resp.issue_cycle = 0;
            if (cur_issue.valid && cur_issue.addr == head_e.addr) {
                nxt_last_resp.issue_seq = cur_issue.seq;
                nxt_last_resp.issue_cycle = cur_issue.issue_cycle;
            } else if (cur_last_issue.valid && cur_last_issue.addr == head_e.addr) {
                nxt_last_resp.issue_seq = cur_last_issue.seq;
                nxt_last_resp.issue_cycle = cur_last_issue.issue_cycle;
            }
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
            LSU_MEM_DBG_PRINTF(
                "[AXI WRITE RESP UNEXPECTED] cyc=%lld head=%u head_valid=%d head_send=%d cur_send=%u count=%u req_ready=%d resp_valid=%d\n",
                (long long)sim_time, cur.head, static_cast<int>(head_e.valid),
                static_cast<int>(head_e.send), cur.send, cur.count,
                static_cast<int>(in.axi_in.req_ready),
                static_cast<int>(in.axi_in.resp_valid));
            if (cur_issue.valid) {
                LSU_MEM_DBG_PRINTF(
                    "[AXI WRITE RESP UNEXPECTED][ISSUE] issue_cyc=%llu issue_head=%u issue_addr=0x%08x\n",
                    (unsigned long long)cur_issue.issue_cycle, cur_issue.head,
                    cur_issue.addr);
                dump_line_words("[AXI WRITE RESP UNEXPECTED][ISSUE_DATA] ", cur_issue.data);
            }
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
    cur_check = nxt_check;
    cur_issue = nxt_issue;
    cur_last_issue = nxt_last_issue;
    cur_last_resp = nxt_last_resp;
    memcpy(write_buffer, write_buffer_nxt, sizeof(write_buffer));
    nxt = cur; 
    nxt_check = cur_check;
    nxt_issue = cur_issue;
    nxt_last_issue = cur_last_issue;
    nxt_last_resp = cur_last_resp;
    check_wb_state("seq.cur.post", cur);
    check_wb_state("seq.nxt.post", nxt);
}
