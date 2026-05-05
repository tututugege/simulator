// MemUtils.h
#pragma once

#include <cstdint>

struct StoreTag {
  reg<STQ_IDX_WIDTH> idx = 0;
  reg<1> flag = false;
};

namespace {

static uint32_t mem_mask_for_width(int width) {
  return width >= 4 ? 0xFFFFFFFFu : ((1u << (width * 8)) - 1);
}

static int get_mem_width(int func3) {
  switch (func3 & 0b11) {
  case 0b00:
    return 1;
  case 0b01:
    return 2;
  case 0b10:
  default:
    return 4;
  }
}

static uint32_t extract_data(uint32_t raw_mem_val, uint32_t addr, int func3) {
  const int bit_offset = (addr & 0x3) * 8;
  const int width = get_mem_width(func3);
  const uint32_t mask = mem_mask_for_width(width);
  uint32_t result = (raw_mem_val >> bit_offset) & mask;

  if ((func3 & 0b100) == 0 && width < 4) {
    const uint32_t sign_bit = 1u << (width * 8 - 1);
    if (result & sign_bit) {
      result |= ~mask;
    }
  }

  return result;
}

static inline uint8_t get_store_strb(uint32_t addr, uint8_t func3) {
  uint32_t off = addr & 0x3;
  switch (func3 & 0x3) {
  case 0:
    return 0x1u << off; // SB
  case 1:
    return 0x3u << off; // SH, 要求 off 为 0 或 2
  case 2:
    return 0xFu; // SW, 要求 off 为 0
  default:
    return 0;
  }
}

static inline uint32_t align_store_data(uint32_t data, uint32_t addr,
                                        uint8_t func3) {
  uint32_t off = addr & 0x3;
  switch (func3 & 0x3) {
  case 0:
    return (data & 0x000000FFu) << (off * 8);
  case 1:
    return (data & 0x0000FFFFu) << (off * 8);
  case 2:
    return data;
  default:
    return 0;
  }
}

constexpr uint32_t kFinishSize = LDQ_SIZE + STQ_SIZE;
constexpr uint32_t kLsuReqIdGenBits = 31 - LDQ_IDX_WIDTH;
constexpr uint32_t kLsuReqIdIdxMask = (1u << LDQ_IDX_WIDTH) - 1;
constexpr uint32_t kLsuReqIdGenMask = (1u << kLsuReqIdGenBits) - 1;

static uint32_t normalize_lsu_req_gen(uint32_t gen) {
  return gen & kLsuReqIdGenMask;
}

template <typename PtrT, typename FlagT>
static void advance_ring_ptr(PtrT &ptr, FlagT &flag, uint32_t size) {
  const uint32_t next = static_cast<uint32_t>(ptr) + 1;
  if (next >= size) {
    ptr = 0;
    flag = !flag;
  } else {
    ptr = next;
  }
}

template <typename PtrT>
static void advance_ring_ptr(PtrT &ptr, uint32_t size) {
  const uint32_t next = static_cast<uint32_t>(ptr) + 1;
  ptr = next >= size ? 0 : next;
}

static bool ldq_idx_alive_after_flush(uint32_t idx, uint32_t head,
                                      uint32_t new_count) {
  return ((idx + LDQ_SIZE - head) % LDQ_SIZE) < new_count;
}

static bool stq_idx_alive_after_flush(uint32_t idx, uint32_t head,
                                      uint32_t new_count) {
  return ((idx + STQ_SIZE - head) % STQ_SIZE) < new_count;
}

static bool stq_tail_flag(uint32_t head, uint32_t count, bool head_flag) {
  return (head + count) >= STQ_SIZE ? !head_flag : head_flag;
}

static uint32_t stq_idx_after(uint32_t head, uint32_t count) {
  return (head + count) % STQ_SIZE;
}

static bool lsu_is_mmio_addr(uint32_t paddr) {
  return ((paddr & UART_ADDR_MASK) == UART_ADDR_BASE) ||
         ((paddr & PLIC_ADDR_MASK) == PLIC_ADDR_BASE) ||
         (paddr == OPENSBI_TIMER_LOW_ADDR) ||
         (paddr == OPENSBI_TIMER_HIGH_ADDR);
}

static bool lsu_mmio_is_oldest_unfinished(const RobBroadcastIO *rob_bcast,
                                          uint32_t rob_idx) {
  if (rob_bcast == nullptr) {
    return false;
  }
  if (rob_bcast->head_incomplete_valid) {
    return rob_bcast->head_incomplete_rob_idx == rob_idx;
  }
  return rob_bcast->head_valid && rob_bcast->head_rob_idx == rob_idx;
}

static uint32_t stq_tag_value(uint32_t idx, bool flag) {
  return (flag ? STQ_SIZE : 0) + idx;
}

static bool stq_distance_from_head_to_boundary(uint32_t head,
                                               bool head_flag,
                                               uint32_t count,
                                               StoreTag boundary,
                                               uint32_t &distance) {
  constexpr uint32_t kRing = STQ_SIZE * 2;
  const uint32_t h = stq_tag_value(head, head_flag);
  const uint32_t b = stq_tag_value(boundary.idx, boundary.flag);

  distance = (b + kRing - h) % kRing;

  // boundary 必须落在当前 active window 或 tail 上：
  // [head, head + count]
  return distance <= count;
}

static uint32_t make_lsu_load_req_id(uint32_t wait_idx, uint32_t gen) {
  return (normalize_lsu_req_gen(gen) << LDQ_IDX_WIDTH) |
         (wait_idx & kLsuReqIdIdxMask);
}

static uint32_t lsu_req_id_gen(uint32_t req_id) {
  return (req_id >> LDQ_IDX_WIDTH) & kLsuReqIdGenMask;
}

static uint32_t lsu_req_id_wait_idx(uint32_t req_id) {
  return req_id & kLsuReqIdIdxMask;
}

static bool lsu_is_timer_addr(uint32_t paddr) {
  return paddr == OPENSBI_TIMER_LOW_ADDR ||
         paddr == OPENSBI_TIMER_HIGH_ADDR;
}
} // namespace
