#pragma once

#include "DcacheConfig.h"
#include "PhysMemory.h"
#include "config.h"
#include "ref.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#ifndef CONFIG_DEBUG_SATP_TRACE_BUFFER_SIZE
#define CONFIG_DEBUG_SATP_TRACE_BUFFER_SIZE 256
#endif

#ifndef CONFIG_DEBUG_SATP_TRACE_DUMP_COUNT
#define CONFIG_DEBUG_SATP_TRACE_DUMP_COUNT 64
#endif

#ifndef CONFIG_DEBUG_PTW_WALK_TRACE_BUFFER_SIZE
#define CONFIG_DEBUG_PTW_WALK_TRACE_BUFFER_SIZE 512
#endif

#ifndef CONFIG_DEBUG_PTW_WALK_TRACE_DUMP_COUNT
#define CONFIG_DEBUG_PTW_WALK_TRACE_DUMP_COUNT 128
#endif

struct DebugSatpWriteEvent {
  long long cycle = -1;
  uint32_t old_satp = 0;
  uint32_t new_satp = 0;
  uint8_t privilege = 0;
};

struct DebugPtwWalkRespEvent {
  long long cycle = -1;
  uint32_t req_addr = 0;
  uint32_t line_addr = 0;
  uint32_t pte = 0;
  uint8_t word_off = 0;
  bool backing_valid = false;
  std::array<uint32_t, DCACHE_WORD_NUM> backing_words = {};
};

namespace debug_ptw_trace {

inline std::array<DebugSatpWriteEvent, CONFIG_DEBUG_SATP_TRACE_BUFFER_SIZE>
    g_satp_events{};
inline size_t g_satp_next = 0;
inline bool g_satp_wrapped = false;

inline std::array<DebugPtwWalkRespEvent, CONFIG_DEBUG_PTW_WALK_TRACE_BUFFER_SIZE>
    g_ptw_walk_events{};
inline size_t g_ptw_walk_next = 0;
inline bool g_ptw_walk_wrapped = false;

inline void record_satp_write(uint32_t old_satp, uint32_t new_satp,
                              uint8_t privilege) {
  auto &slot = g_satp_events[g_satp_next];
  slot.cycle = sim_time;
  slot.old_satp = old_satp;
  slot.new_satp = new_satp;
  slot.privilege = privilege;
  g_satp_next = (g_satp_next + 1) % g_satp_events.size();
  if (g_satp_next == 0) {
    g_satp_wrapped = true;
  }
}

inline void record_ptw_walk_resp_detail(const uint32_t *memory, uint32_t req_addr,
                                        uint32_t pte) {
  auto &slot = g_ptw_walk_events[g_ptw_walk_next];
  slot.cycle = sim_time;
  slot.req_addr = req_addr;
  slot.line_addr = req_addr & ~(DCACHE_LINE_SIZE - 1u);
  slot.pte = pte;
  slot.word_off = static_cast<uint8_t>(
      (req_addr & (DCACHE_LINE_SIZE - 1u)) >> 2);
  slot.backing_valid = (memory != nullptr);
  slot.backing_words.fill(0);
  if (memory != nullptr) {
    for (int w = 0; w < DCACHE_WORD_NUM; w++) {
      const uint32_t paddr = slot.line_addr + static_cast<uint32_t>(w * 4);
      slot.backing_words[static_cast<size_t>(w)] = pmem_read(paddr);
    }
  }
  g_ptw_walk_next = (g_ptw_walk_next + 1) % g_ptw_walk_events.size();
  if (g_ptw_walk_next == 0) {
    g_ptw_walk_wrapped = true;
  }
}

inline void dump_recent_satp_writes(
    size_t dump_count = CONFIG_DEBUG_SATP_TRACE_DUMP_COUNT) {
  const size_t count = g_satp_wrapped ? g_satp_events.size() : g_satp_next;
  if (count == 0) {
    std::printf("[DEADLOCK][PTW_TRACE][SATP] no satp writes recorded\n");
    return;
  }
  const size_t recent = (dump_count < count) ? dump_count : count;
  const size_t start =
      g_satp_wrapped
          ? ((g_satp_next + g_satp_events.size() - recent) %
             g_satp_events.size())
          : (count - recent);
  std::printf(
      "[DEADLOCK][PTW_TRACE][SATP] dumping latest %zu satp writes (buffered=%zu)\n",
      recent, count);
  for (size_t n = 0; n < recent; ++n) {
    const auto &e = g_satp_events[(start + n) % g_satp_events.size()];
    std::printf(
        "[DEADLOCK][PTW_TRACE][SATP] cyc=%lld old=0x%08x new=0x%08x mode=%u asid=0x%03x ppn=0x%05x priv=%u\n",
        e.cycle, e.old_satp, e.new_satp,
        static_cast<unsigned>((e.new_satp >> 31) & 0x1),
        static_cast<unsigned>((e.new_satp >> 22) & 0x1ff),
        static_cast<unsigned>(e.new_satp & 0x3fffff),
        static_cast<unsigned>(e.privilege));
  }
}

inline void dump_recent_ptw_walk_resps(
    size_t dump_count = CONFIG_DEBUG_PTW_WALK_TRACE_DUMP_COUNT) {
  const size_t count =
      g_ptw_walk_wrapped ? g_ptw_walk_events.size() : g_ptw_walk_next;
  if (count == 0) {
    std::printf(
        "[DEADLOCK][PTW_TRACE][WALK] no ptw walk responses recorded\n");
    return;
  }
  const size_t recent = (dump_count < count) ? dump_count : count;
  const size_t start =
      g_ptw_walk_wrapped
          ? ((g_ptw_walk_next + g_ptw_walk_events.size() - recent) %
             g_ptw_walk_events.size())
          : (count - recent);
  std::printf(
      "[DEADLOCK][PTW_TRACE][WALK] dumping latest %zu ptw walk responses (buffered=%zu)\n",
      recent, count);
  for (size_t n = 0; n < recent; ++n) {
    const auto &e = g_ptw_walk_events[(start + n) % g_ptw_walk_events.size()];
    const bool pte_v = (e.pte & PTE_V) != 0;
    const bool pte_r = (e.pte & PTE_R) != 0;
    const bool pte_w = (e.pte & PTE_W) != 0;
    const bool pte_x = (e.pte & PTE_X) != 0;
    const uint32_t pte_ppn = (e.pte >> 10) & 0x3fffff;
    std::printf(
        "[DEADLOCK][PTW_TRACE][WALK] cyc=%lld req_addr=0x%08x line=0x%08x word_off=%u pte=0x%08x flags{v=%u r=%u w=%u x=%u} ppn=0x%05x\n",
        e.cycle, e.req_addr, e.line_addr, static_cast<unsigned>(e.word_off),
        e.pte, static_cast<unsigned>(pte_v), static_cast<unsigned>(pte_r),
        static_cast<unsigned>(pte_w), static_cast<unsigned>(pte_x),
        static_cast<unsigned>(pte_ppn));
    if (!e.backing_valid) {
      std::printf(
          "[DEADLOCK][PTW_TRACE][WALK][BACKING] unavailable(memory=null)\n");
      continue;
    }
    std::printf("[DEADLOCK][PTW_TRACE][WALK][BACKING] line=0x%08x words=[",
                e.line_addr);
    for (int w = 0; w < DCACHE_WORD_NUM; w++) {
      std::printf("%s%08x", (w == 0) ? "" : " ",
                  e.backing_words[static_cast<size_t>(w)]);
    }
    std::printf("]\n");
  }
}

} // namespace debug_ptw_trace
