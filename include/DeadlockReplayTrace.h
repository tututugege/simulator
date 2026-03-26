#pragma once

#include "config.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

enum class DeadlockReplayTraceKind : uint8_t {
  MshrBroadcast = 0,
  MemBroadcast,
  MemToLsu,
  LsuBroadcast,
  LsuLdqCheck,
  LsuLdqWake,
  LsuLoadReplayLatch,
};

struct DeadlockReplayTraceEvent {
  long long cycle = -1;
  DeadlockReplayTraceKind kind = DeadlockReplayTraceKind::MshrBroadcast;
  uint16_t slot = 0;
  uint8_t replay = 0;
  uint8_t state = 0;
  uint8_t flag0 = 0;
  uint8_t flag1 = 0;
  uint32_t pc = 0;
  uint32_t addr = 0;
  uint32_t line = 0;
  uint32_t aux0 = 0;
  uint32_t aux1 = 0;
};

namespace deadlock_replay_trace {

inline std::array<DeadlockReplayTraceEvent,
                  CONFIG_DEADLOCK_REPLAY_TRACE_BUFFER_SIZE>
    g_events{};
inline size_t g_next = 0;
inline bool g_wrapped = false;

inline const char *kind_name(DeadlockReplayTraceKind kind) {
  switch (kind) {
  case DeadlockReplayTraceKind::MshrBroadcast:
    return "MSHR_BCAST";
  case DeadlockReplayTraceKind::MemBroadcast:
    return "MEM_BCAST";
  case DeadlockReplayTraceKind::MemToLsu:
    return "MEM_TO_LSU";
  case DeadlockReplayTraceKind::LsuBroadcast:
    return "LSU_BCAST";
  case DeadlockReplayTraceKind::LsuLdqCheck:
    return "LSU_LDQ_CHECK";
  case DeadlockReplayTraceKind::LsuLdqWake:
    return "LSU_LDQ_WAKE";
  case DeadlockReplayTraceKind::LsuLoadReplayLatch:
    return "LSU_LD_LATCH";
  }
  return "UNKNOWN";
}

inline const char *state_name(uint8_t state) {
  switch (state) {
  case 0:
    return "WAIT_EXEC";
  case 1:
    return "WAIT_SEND";
  case 2:
    return "WAIT_RESP";
  case 3:
    return "WAIT_RETRY";
  case 4:
    return "READY_AT";
  default:
    return "UNKNOWN";
  }
}

inline void record(DeadlockReplayTraceKind kind, uint16_t slot, uint8_t replay,
                   uint8_t state, uint8_t flag0, uint8_t flag1, uint32_t pc,
                   uint32_t addr, uint32_t line, uint32_t aux0,
                   uint32_t aux1) {
  auto &slot_ref = g_events[g_next];
  slot_ref.cycle = sim_time;
  slot_ref.kind = kind;
  slot_ref.slot = slot;
  slot_ref.replay = replay;
  slot_ref.state = state;
  slot_ref.flag0 = flag0;
  slot_ref.flag1 = flag1;
  slot_ref.pc = pc;
  slot_ref.addr = addr;
  slot_ref.line = line;
  slot_ref.aux0 = aux0;
  slot_ref.aux1 = aux1;
  g_next = (g_next + 1) % g_events.size();
  if (g_next == 0) {
    g_wrapped = true;
  }
}

inline void dump_recent(
    size_t dump_count = CONFIG_DEADLOCK_REPLAY_TRACE_DUMP_COUNT) {
  const size_t count = g_wrapped ? g_events.size() : g_next;
  if (count == 0) {
    std::printf(
        "[DEADLOCK][REPLAY_TRACE] no replay-related events have been "
        "recorded\n");
    return;
  }
  const size_t recent = (dump_count < count) ? dump_count : count;
  const size_t start =
      g_wrapped ? ((g_next + g_events.size() - recent) % g_events.size())
                : (count - recent);
  size_t printed = 0;
  std::printf(
      "[DEADLOCK][REPLAY_TRACE] dumping latest %zu replay events "
      "(buffered=%zu)\n",
      recent, count);
  for (size_t n = 0; n < recent; ++n) {
    const auto &e = g_events[(start + n) % g_events.size()];
    std::printf(
        "[DEADLOCK][REPLAY_TRACE] cyc=%lld kind=%s slot=%u replay=%u "
        "state=%s flag0=%u flag1=%u pc=0x%08x addr=0x%08x line=0x%08x "
        "aux0=0x%08x aux1=0x%08x\n",
        e.cycle, kind_name(e.kind), static_cast<unsigned>(e.slot),
        static_cast<unsigned>(e.replay), state_name(e.state),
        static_cast<unsigned>(e.flag0), static_cast<unsigned>(e.flag1), e.pc,
        e.addr, e.line, e.aux0, e.aux1);
    ++printed;
  }
  if (printed == 0) {
    std::printf(
        "[DEADLOCK][REPLAY_TRACE] no replay-related events in requested "
        "range\n");
  }
}

} // namespace deadlock_replay_trace
