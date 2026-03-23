#include "DeadlockDebug.h"
#include "DeadlockReplayTrace.h"
#include "DiffMemTrace.h"

#include <cstdio>

namespace deadlock_debug {
namespace {
DumpCallback g_lsu_dump_cb = nullptr;
DumpCallback g_mem_dump_cb = nullptr;
DumpCallback g_front_dump_cb = nullptr;
DumpCallback g_soc_dump_cb = nullptr;
} // namespace

void register_lsu_dump_cb(DumpCallback cb) { g_lsu_dump_cb = cb; }

void register_mem_dump_cb(DumpCallback cb) { g_mem_dump_cb = cb; }

void register_front_dump_cb(DumpCallback cb) { g_front_dump_cb = cb; }

void register_soc_dump_cb(DumpCallback cb) { g_soc_dump_cb = cb; }

void dump_all() {
  std::printf("[DEADLOCK] ===== LSU/MEM State Dump Begin =====\n");
  if (g_lsu_dump_cb != nullptr) {
    g_lsu_dump_cb();
  } else {
    std::printf("[DEADLOCK] LSU dump callback is not registered.\n");
  }
  if (g_mem_dump_cb != nullptr) {
    g_mem_dump_cb();
  } else {
    std::printf("[DEADLOCK] MemSubsystem dump callback is not registered.\n");
  }
  if (g_front_dump_cb != nullptr) {
    g_front_dump_cb();
  } else {
    std::printf("[DEADLOCK] Front-end dump callback is not registered.\n");
  }
  if (g_soc_dump_cb != nullptr) {
    g_soc_dump_cb();
  } else {
    std::printf("[DEADLOCK] SoC dump callback is not registered.\n");
  }
  deadlock_replay_trace::dump_recent();
  diff_mem_trace::dump_recent();
  std::printf("[DEADLOCK] ===== LSU/MEM State Dump End =====\n");
}
} // namespace deadlock_debug
