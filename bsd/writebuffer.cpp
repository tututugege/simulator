#include "writebuffer.h"
#include "config.h"
#include "types.h"
#include "util.h"
#include <cassert>
#include <cstring>

extern uint32_t *p_memory;

WriteBufferEntry write_buffer[WB_ENTRIES];
WriteBufferEntry write_buffer_nxt[WB_ENTRIES];

namespace {
static constexpr uint32_t kWbMshrSafetyReserve = 1;

static constexpr uint8_t kCacheLineReqTotalSize =
    static_cast<uint8_t>(DCACHE_LINE_BYTES - 1u);

static constexpr uint64_t full_line_wstrb_mask() {
  uint64_t mask = 0;
  for (uint32_t i = 0; i < DCACHE_LINE_BYTES && i < 64; i++) {
    mask |= (1ull << i);
  }
  return mask;
}

static inline uint32_t word_offset(uint32_t addr) {
  return (addr >> 2) & (DCACHE_LINE_WORDS - 1);
}

static inline bool cache_line_match_local(uint32_t addr1, uint32_t addr2) {
  return (addr1 >> DCACHE_OFFSET_BITS) == (addr2 >> DCACHE_OFFSET_BITS);
}

static inline void apply_strobe_local(uint32_t &dst, uint32_t src, uint8_t strb) {
  for (int b = 0; b < 4; b++) {
    if (strb & (1u << b)) {
      uint32_t mask = 0xFFu << (b * 8);
      dst = (dst & ~mask) | (src & mask);
    }
  }
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
} // namespace

int WriteBuffer::find_wb_entry(uint32_t addr) {
  return find_wb_entry_in_view(write_buffer, cur.head, cur.count, addr);
}

void WriteBuffer::init() {
  cur = {};
  nxt = {};
  for (auto &e : write_buffer) {
    e = {};
  }
  for (auto &e : write_buffer_nxt) {
    e = {};
  }
  in.clear();
  out.clear();
}

void WriteBuffer::comb_outputs() {
  out.wbmshr.ready = ((nxt.count + kWbMshrSafetyReserve) < WB_ENTRIES);

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    out.wbdcache.bypass_resp[i].valid = nxt.bypassvalid[i];
    out.wbdcache.bypass_resp[i].data = nxt.bypassdata[i];
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    out.wbdcache.merge_resp[i].valid = nxt.mergevalid[i];
    out.wbdcache.merge_resp[i].busy = nxt.mergebusy[i];
  }
  drive_axi_req_from_head(out, cur.send, cur.head, write_buffer_nxt);
}

void WriteBuffer::comb_inputs() {

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    nxt.bypassvalid[i] = false;
    nxt.bypassdata[i] = 0;
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    nxt.mergevalid[i] = false;
    nxt.mergebusy[i] = false;
    if (in.dcachewb.merge_req[i].valid) {
      int wb_idx = find_wb_entry_in_view(write_buffer_nxt, nxt.head, nxt.count,
                                         in.dcachewb.merge_req[i].addr);
      if (wb_idx != -1) {
        WriteBufferEntry &e = write_buffer_nxt[wb_idx];
        if (e.send) {
          nxt.mergebusy[i] = true;
        } else {
          nxt.mergevalid[i] = true;
          uint32_t off = word_offset(in.dcachewb.merge_req[i].addr);
          apply_strobe_local(e.data[off], in.dcachewb.merge_req[i].data,
                             in.dcachewb.merge_req[i].strb);
        }
      } else if (cache_line_match_local(in.dcachewb.merge_req[i].addr,
                                        in.mshrwb.addr) &&
                 in.mshrwb.valid) {
        nxt.mergevalid[i] = true;
        uint32_t off = word_offset(in.dcachewb.merge_req[i].addr);
        apply_strobe_local(in.mshrwb.data[off], in.dcachewb.merge_req[i].data,
                           in.dcachewb.merge_req[i].strb);
      }
    }
  }

  if (in.mshrwb.valid) {
    if (nxt.count < WB_ENTRIES) {
      WriteBufferEntry &e = write_buffer_nxt[nxt.tail];
      e.valid = true;
      e.send = false;
      e.addr = in.mshrwb.addr;
      std::memcpy(e.data, in.mshrwb.data, DCACHE_LINE_WORDS * sizeof(uint32_t));
      nxt.tail = (nxt.tail + 1) % WB_ENTRIES;
      nxt.count++;
    } else {
      // assert(false && "WriteBuffer overflow");
    }
  }

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    if (in.dcachewb.bypass_req[i].valid) {
      int wb_idx = find_wb_entry_in_view(write_buffer_nxt, nxt.head, nxt.count,
                                         in.dcachewb.bypass_req[i].addr);
      if (wb_idx != -1) {
        nxt.bypassvalid[i] = true;
        nxt.bypassdata[i] =
            write_buffer_nxt[wb_idx].data[word_offset(in.dcachewb.bypass_req[i].addr)];
      } else if (cache_line_match_local(in.dcachewb.bypass_req[i].addr,
                                        in.mshrwb.addr) &&
                 in.mshrwb.valid) {
        nxt.bypassvalid[i] = true;
        nxt.bypassdata[i] = in.mshrwb.data[word_offset(in.dcachewb.bypass_req[i].addr)];
      }
    }
  }

  drive_axi_req_from_head(out, cur.send, cur.head, write_buffer_nxt);

  if (cur.send == 0) {
    WriteBufferEntry &head_e = write_buffer_nxt[cur.head];
    const bool can_issue_head = head_e.valid && !head_e.send;
    const bool req_payload_matches_head =
        out.axi_out.req_valid && (out.axi_out.req_addr == head_e.addr);
    const bool req_handshake =
        can_issue_head && in.axi_in.req_ready && req_payload_matches_head;
    if (req_handshake) {
      head_e.send = true;
      nxt.send = 1;
    } else if (can_issue_head) {
      nxt.send = 0;
    }
  }

  if (in.axi_in.resp_valid) {
    WriteBufferEntry &head_e = write_buffer[cur.head];
    if (head_e.valid && head_e.send) {
      write_buffer_nxt[cur.head].valid = false;
      write_buffer_nxt[cur.head].send = false;
      nxt.head = (cur.head + 1) % WB_ENTRIES;
      if (nxt.count > 0) {
        nxt.count--;
      }
    }
    nxt.send = 0;
  }
}

void WriteBuffer::seq() {
  cur = nxt;
  std::memcpy(write_buffer, write_buffer_nxt, sizeof(write_buffer));
  nxt = cur;
}
