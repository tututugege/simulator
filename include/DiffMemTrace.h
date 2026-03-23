#pragma once

#include "config.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

enum class DiffMemTraceOp : uint8_t {
  Load = 0,
  Store = 1,
};

enum class DiffMemTracePhase : uint8_t {
  Req = 0,
  Resp = 1,
};

enum class DiffMemTraceDetail : uint8_t {
  Req = 0,
  OkSpecial,
  OkDcacheHit,
  OkWbBypass,
  OkMshrFill,
  OkMshrMerge,
  OkWbMerge,
  ReplayBankConflict,
  ReplayMshrHit,
  ReplayMshrFull,
  ReplayFirstAlloc,
  ReplayFillWait,
  ReplayFillReplaceConflict,
  ReplayWbBusy,
};

struct DiffMemTraceEvent {
  long long cycle = -1;
  DiffMemTraceOp op = DiffMemTraceOp::Load;
  DiffMemTracePhase phase = DiffMemTracePhase::Req;
  DiffMemTraceDetail detail = DiffMemTraceDetail::Req;
  uint8_t port = 0;
  uint8_t func3 = 0;
  size_t req_id = 0;
  uint32_t rob_idx = 0;
  uint32_t rob_flag = 0;
  uint32_t addr = 0;
  uint32_t data = 0;
  uint32_t aux0 = 0;
  uint32_t aux1 = 0;
};

namespace diff_mem_trace {

inline std::array<DiffMemTraceEvent, CONFIG_DIFF_DEBUG_MEMTRACE_BUFFER_SIZE>
    g_events{};
inline size_t g_next = 0;
inline bool g_wrapped = false;

inline const char *op_name(DiffMemTraceOp op) {
  return op == DiffMemTraceOp::Load ? "LD" : "ST";
}

inline const char *phase_name(DiffMemTracePhase phase) {
  return phase == DiffMemTracePhase::Req ? "REQ" : "RESP";
}

inline const char *detail_name(DiffMemTraceDetail detail) {
  switch (detail) {
  case DiffMemTraceDetail::Req:
    return "req";
  case DiffMemTraceDetail::OkSpecial:
    return "ok_special";
  case DiffMemTraceDetail::OkDcacheHit:
    return "ok_dcache_hit";
  case DiffMemTraceDetail::OkWbBypass:
    return "ok_wb_bypass";
  case DiffMemTraceDetail::OkMshrFill:
    return "ok_mshr_fill";
  case DiffMemTraceDetail::OkMshrMerge:
    return "ok_mshr_merge";
  case DiffMemTraceDetail::OkWbMerge:
    return "ok_wb_merge";
  case DiffMemTraceDetail::ReplayBankConflict:
    return "replay_bank_conflict";
  case DiffMemTraceDetail::ReplayMshrHit:
    return "replay_mshr_hit";
  case DiffMemTraceDetail::ReplayMshrFull:
    return "replay_mshr_full";
  case DiffMemTraceDetail::ReplayFirstAlloc:
    return "replay_first_alloc";
  case DiffMemTraceDetail::ReplayFillWait:
    return "replay_fill_wait";
  case DiffMemTraceDetail::ReplayFillReplaceConflict:
    return "replay_fill_replace_conflict";
  case DiffMemTraceDetail::ReplayWbBusy:
    return "replay_wb_busy";
  }
  return "unknown";
}

inline void record(DiffMemTraceOp op, DiffMemTracePhase phase,
                   DiffMemTraceDetail detail, uint8_t port, uint8_t func3,
                   size_t req_id, uint32_t rob_idx, uint32_t rob_flag,
                   uint32_t addr, uint32_t data, uint32_t aux0,
                   uint32_t aux1) {
  auto &slot = g_events[g_next];
  slot.cycle = sim_time;
  slot.op = op;
  slot.phase = phase;
  slot.detail = detail;
  slot.port = port;
  slot.func3 = func3;
  slot.req_id = req_id;
  slot.rob_idx = rob_idx;
  slot.rob_flag = rob_flag;
  slot.addr = addr;
  slot.data = data;
  slot.aux0 = aux0;
  slot.aux1 = aux1;
  g_next = (g_next + 1) % g_events.size();
  if (g_next == 0) {
    g_wrapped = true;
  }
}

inline void dump_recent(
    size_t dump_count = CONFIG_DIFF_DEBUG_MEMTRACE_DUMP_COUNT) {
  const size_t count = g_wrapped ? g_events.size() : g_next;
  if (count == 0) {
    std::printf("[DIFF][MEMTRACE] no load/store events have been recorded\n");
    return;
  }
  const size_t recent = (dump_count < count) ? dump_count : count;
  const size_t start =
      g_wrapped ? ((g_next + g_events.size() - recent) % g_events.size())
                : (count - recent);
  size_t printed = 0;
  std::printf("[DIFF][MEMTRACE] dumping latest %zu events (buffered=%zu)\n",
              recent, count);
  for (size_t n = 0; n < recent; ++n) {
    const DiffMemTraceEvent &e = g_events[(start + n) % g_events.size()];
    std::printf(
        "[DIFF][MEMTRACE] cyc=%lld op=%s phase=%s detail=%s port=%u req_id=%zu "
        "rob=%u flag=%u addr=0x%08x data=0x%08x func3=0x%x aux0=0x%08x "
        "aux1=0x%08x\n",
        e.cycle, op_name(e.op), phase_name(e.phase), detail_name(e.detail),
        static_cast<unsigned>(e.port), e.req_id, e.rob_idx, e.rob_flag, e.addr,
        e.data, static_cast<unsigned>(e.func3), e.aux0, e.aux1);
    ++printed;
  }
  if (printed == 0) {
    std::printf("[DIFF][MEMTRACE] no load/store events in the requested range\n");
  }
}

} // namespace diff_mem_trace
