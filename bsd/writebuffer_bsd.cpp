#include "writebuffer.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

inline uint64_t read_bits_lsb0(const uint8_t *bits, size_t bit_count, size_t &idx,
                               size_t width) {
  assert(bits != nullptr);
  assert(width <= 64);
  assert(idx + width <= bit_count);
  uint64_t v = 0;
  for (size_t b = 0; b < width; b++) {
    const uint64_t bit = static_cast<uint64_t>(bits[idx++] & 1u);
    v |= (bit << b);
  }
  return v;
}

inline void write_bits_lsb0(uint8_t *bits, size_t bit_count, size_t &idx,
                            uint64_t value, size_t width) {
  assert(bits != nullptr);
  assert(width <= 64);
  assert(idx + width <= bit_count);
  for (size_t b = 0; b < width; b++) {
    bits[idx++] = static_cast<uint8_t>((value >> b) & 1u);
  }
}

inline size_t wb_entry_bits() {
  return 1 + 1 + 32 + static_cast<size_t>(DCACHE_LINE_WORDS) * 32;
}

inline size_t wb_state_bits() {
  return 32 + 32 + 32 + 32 + static_cast<size_t>(LSU_LDU_COUNT) * (32 + 1) +
         static_cast<size_t>(LSU_STA_COUNT) * (1 + 1);
}

inline size_t wb_in_port_bits_without_ram() {
  return (1 + 32 + static_cast<size_t>(DCACHE_LINE_WORDS) * 32) +
         static_cast<size_t>(LSU_LDU_COUNT) * (1 + 32) +
         static_cast<size_t>(LSU_STA_COUNT) * (1 + 32 + 32 + 8) + (1 + 1);
}

inline size_t wb_out_port_bits_without_ram() {
  return 1 + static_cast<size_t>(LSU_LDU_COUNT) * (1 + 32) +
         static_cast<size_t>(LSU_STA_COUNT) * (1 + 1) +
         (1 + 32 + 8 + 8 + 64 +
          static_cast<size_t>(DCACHE_LINE_WORDS) * 32 + 1);
}

inline void decode_wb_in_ports_no_ram(WBIn &in, const uint8_t *bits,
                                      size_t bit_count, size_t &idx) {
  in.mshrwb.valid = static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
  in.mshrwb.addr = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
  for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
    in.mshrwb.data[w] = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
  }

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    in.dcachewb.bypass_req[i].valid =
        static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
    in.dcachewb.bypass_req[i].addr =
        static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    in.dcachewb.merge_req[i].valid =
        static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
    in.dcachewb.merge_req[i].addr =
        static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
    in.dcachewb.merge_req[i].data =
        static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
    in.dcachewb.merge_req[i].strb =
        static_cast<uint8_t>(read_bits_lsb0(bits, bit_count, idx, 8));
  }

  in.axi_in.req_ready = static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
  in.axi_in.resp_valid = static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
}

inline void decode_wb_state(WBState &st, const uint8_t *bits, size_t bit_count,
                            size_t &idx) {
  st.count = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
  st.head = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
  st.tail = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
  st.send = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    st.bypassdata[i] = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
    st.bypassvalid[i] = static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    st.mergevalid[i] = static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
    st.mergebusy[i] = static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
  }
}

inline void decode_wb_entries(WriteBufferEntry *entries, const uint8_t *bits,
                              size_t bit_count, size_t &idx) {
  for (int i = 0; i < WB_ENTRIES; i++) {
    entries[i].valid = static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
    entries[i].send = static_cast<bool>(read_bits_lsb0(bits, bit_count, idx, 1));
    entries[i].addr = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
      entries[i].data[w] = static_cast<uint32_t>(read_bits_lsb0(bits, bit_count, idx, 32));
    }
  }
}

inline void encode_wb_out_ports_no_ram(const WBOut &out, uint8_t *bits,
                                       size_t bit_count, size_t &idx) {
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(out.wbmshr.ready), 1);

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    write_bits_lsb0(bits, bit_count, idx,
                    static_cast<uint64_t>(out.wbdcache.bypass_resp[i].valid), 1);
    write_bits_lsb0(bits, bit_count, idx,
                    static_cast<uint64_t>(out.wbdcache.bypass_resp[i].data), 32);
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    write_bits_lsb0(bits, bit_count, idx,
                    static_cast<uint64_t>(out.wbdcache.merge_resp[i].valid), 1);
    write_bits_lsb0(bits, bit_count, idx,
                    static_cast<uint64_t>(out.wbdcache.merge_resp[i].busy), 1);
  }

  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(out.axi_out.req_valid), 1);
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(out.axi_out.req_addr), 32);
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(out.axi_out.req_total_size), 8);
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(out.axi_out.req_id), 8);
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(out.axi_out.req_wstrb), 64);
  for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
    write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(out.axi_out.req_wdata[w]), 32);
  }
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(out.axi_out.resp_ready), 1);
}

inline void encode_wb_state(const WBState &st, uint8_t *bits, size_t bit_count,
                            size_t &idx) {
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(st.count), 32);
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(st.head), 32);
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(st.tail), 32);
  write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(st.send), 32);

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(st.bypassdata[i]), 32);
    write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(st.bypassvalid[i]), 1);
  }

  for (int i = 0; i < LSU_STA_COUNT; i++) {
    write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(st.mergevalid[i]), 1);
    write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(st.mergebusy[i]), 1);
  }
}

inline void encode_wb_entries(const WriteBufferEntry *entries, uint8_t *bits,
                              size_t bit_count, size_t &idx) {
  for (int i = 0; i < WB_ENTRIES; i++) {
    write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(entries[i].valid), 1);
    write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(entries[i].send), 1);
    write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(entries[i].addr), 32);
    for (int w = 0; w < DCACHE_LINE_WORDS; w++) {
      write_bits_lsb0(bits, bit_count, idx, static_cast<uint64_t>(entries[i].data[w]), 32);
    }
  }
}

} // namespace

// bit-array convention:
// - one-dimensional array of uint8_t
// - each element is one bit (0 or 1)
// - multi-bit fields are packed LSB-first in declaration order
extern "C" size_t wb_bsd_input_bit_count() {
  return wb_in_port_bits_without_ram() + wb_state_bits() +
         static_cast<size_t>(WB_ENTRIES) * wb_entry_bits();
}

extern "C" size_t wb_bsd_output_bit_count() {
  return wb_out_port_bits_without_ram() + wb_state_bits() +
         static_cast<size_t>(WB_ENTRIES) * wb_entry_bits();
}

// Decode input bits and assign to WB input/cur/write_buffer.
extern "C" bool wb_bsd_apply_input_bits(WriteBuffer *wb, const uint8_t *input_bits,
                                        size_t input_bit_len) {
  if (wb == nullptr || input_bits == nullptr) {
    return false;
  }
  const size_t expect = wb_bsd_input_bit_count();
  if (input_bit_len != expect) {
    return false;
  }

  wb->in.clear();
  wb->out.clear();
  wb->cur = {};
  wb->nxt = {};

  size_t idx = 0;
  decode_wb_in_ports_no_ram(wb->in, input_bits, input_bit_len, idx);
  decode_wb_state(wb->cur, input_bits, input_bit_len, idx);
  decode_wb_entries(write_buffer, input_bits, input_bit_len, idx);
  std::memcpy(write_buffer_nxt, write_buffer, sizeof(write_buffer_nxt));

  wb->nxt = wb->cur;
  return idx == input_bit_len;
}

// Encode WB out/nxt/write_buffer_nxt to output bits.
extern "C" bool wb_bsd_collect_output_bits(const WriteBuffer *wb,
                                           uint8_t *output_bits,
                                           size_t output_bit_len) {
  if (wb == nullptr || output_bits == nullptr) {
    return false;
  }
  const size_t expect = wb_bsd_output_bit_count();
  if (output_bit_len != expect) {
    return false;
  }

  std::memset(output_bits, 0, output_bit_len * sizeof(uint8_t));
  size_t idx = 0;
  encode_wb_out_ports_no_ram(wb->out, output_bits, output_bit_len, idx);
  encode_wb_state(wb->nxt, output_bits, output_bit_len, idx);
  encode_wb_entries(write_buffer_nxt, output_bits, output_bit_len, idx);
  return idx == output_bit_len;
}

// Convenience wrapper: apply input bits -> run comb -> collect output bits.
extern "C" bool wb_bsd_eval_once(WriteBuffer *wb, const uint8_t *input_bits,
                                 size_t input_bit_len, uint8_t *output_bits,
                                 size_t output_bit_len) {
  if (!wb_bsd_apply_input_bits(wb, input_bits, input_bit_len)) {
    return false;
  }
  wb->comb_outputs();
  wb->comb_inputs();
  return wb_bsd_collect_output_bits(wb, output_bits, output_bit_len);
}
