#include "host_profile.h"

#if FRONTEND_ENABLE_HOST_PROFILE

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <vector>

namespace frontend_host_profile {
namespace {

struct SlotStats {
  uint64_t calls = 0;
  uint64_t samples = 0;
  uint64_t sampled_ns = 0;
};

constexpr size_t kSlotCount = static_cast<size_t>(Slot::Count);

constexpr std::array<const char *, kSlotCount> kSlotNames = {
    "front_top.total",
    "front_top.seq_read",
    "front_top.comb",
    "front_top.seq_write",
    "front_top.read_stage",
    "front_top.bpu_stage",
    "front_top.icache_stage",
    "front_top.predecode_stage",
    "front_top.f2b_stage",
    "front_top.refresh_stage",
    "bpu.seq_read",
    "bpu.pre_read_req",
    "bpu.data_seq_read",
    "bpu.post_read_req",
    "bpu.submodule_seq_read",
    "bpu.core_comb",
    "bpu.type_comb",
    "bpu.tage_comb_total",
    "bpu.btb_comb_total",
    "icache.comb",
    "sim.cycle_total",
    "sim.csr_status",
    "sim.clear_axi_inputs",
    "sim.front_cycle",
    "sim.back_comb",
    "sim.mem.llc_comb_outputs",
    "sim.axi_outputs",
    "sim.bridge_axi_to_mem",
    "sim.mem.comb",
    "sim.bridge_mem_to_axi",
    "sim.axi_inputs",
    "sim.back2front",
    "sim.back_seq",
    "sim.mem.seq",
    "sim.mem.llc_seq",
    "sim.axi_seq",
};

std::array<SlotStats, kSlotCount> g_stats{};

constexpr uint64_t sample_period() {
#if FRONTEND_HOST_PROFILE_SAMPLE_SHIFT <= 0
  return 1ull;
#else
  return 1ull << FRONTEND_HOST_PROFILE_SAMPLE_SHIFT;
#endif
}

uint64_t monotonic_now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

} // namespace

void begin_sample(Slot slot, uint64_t &start_ns, bool &active) {
  auto &stat = g_stats[static_cast<size_t>(slot)];
  const uint64_t calls = ++stat.calls;
  const uint64_t period = sample_period();
  active = (period == 1ull) || ((calls & (period - 1ull)) == 0ull);
  if (!active) {
    start_ns = 0;
    return;
  }
  ++stat.samples;
  start_ns = monotonic_now_ns();
}

void end_sample(Slot slot, uint64_t start_ns, bool active) {
  if (!active) {
    return;
  }
  auto &stat = g_stats[static_cast<size_t>(slot)];
  stat.sampled_ns += monotonic_now_ns() - start_ns;
}

void print_summary() {
  struct Row {
    size_t index = 0;
    uint64_t calls = 0;
    uint64_t samples = 0;
    uint64_t sampled_ns = 0;
    uint64_t est_ns = 0;
  };

  std::vector<Row> rows;
  rows.reserve(kSlotCount);
  uint64_t total_est_ns = 0;
  const uint64_t period = sample_period();
  for (size_t i = 0; i < kSlotCount; ++i) {
    const auto &stat = g_stats[i];
    if (stat.calls == 0 || stat.samples == 0) {
      continue;
    }
    Row row;
    row.index = i;
    row.calls = stat.calls;
    row.samples = stat.samples;
    row.sampled_ns = stat.sampled_ns;
    row.est_ns = stat.sampled_ns * period;
    total_est_ns += row.est_ns;
    rows.push_back(row);
  }

  if (rows.empty()) {
    return;
  }

  std::sort(rows.begin(), rows.end(),
            [](const Row &lhs, const Row &rhs) { return lhs.est_ns > rhs.est_ns; });

  std::printf("\n=== Frontend Host Profile (inclusive, sampled) ===\n");
  std::printf("sample_shift=%d sample_period=%llu\n",
              FRONTEND_HOST_PROFILE_SAMPLE_SHIFT,
              static_cast<unsigned long long>(period));
  std::printf("%-26s %12s %12s %14s %14s %10s %12s\n", "slot", "calls",
              "samples", "avg_sample_ns", "est_ms", "share", "sample_ms");

  for (const auto &row : rows) {
    const double avg_sample_ns =
        (row.samples == 0)
            ? 0.0
            : static_cast<double>(row.sampled_ns) / static_cast<double>(row.samples);
    const double est_ms = static_cast<double>(row.est_ns) / 1.0e6;
    const double sample_ms = static_cast<double>(row.sampled_ns) / 1.0e6;
    const double share =
        (total_est_ns == 0)
            ? 0.0
            : (100.0 * static_cast<double>(row.est_ns) /
               static_cast<double>(total_est_ns));
    std::printf("%-26s %12llu %12llu %14.1f %14.3f %9.2f%% %12.3f\n",
                kSlotNames[row.index],
                static_cast<unsigned long long>(row.calls),
                static_cast<unsigned long long>(row.samples), avg_sample_ns, est_ms,
                share, sample_ms);
  }
  std::printf("=== End Frontend Host Profile ===\n");

  const auto slot_est_ns = [&](Slot slot) -> uint64_t {
    const auto &stat = g_stats[static_cast<size_t>(slot)];
    if (stat.calls == 0 || stat.samples == 0) {
      return 0;
    }
    return stat.sampled_ns * period;
  };

  const uint64_t sim_total_ns = slot_est_ns(Slot::SimCycle);
  if (sim_total_ns == 0) {
    return;
  }

  struct RollupRow {
    const char *name;
    uint64_t est_ns;
  };

  const uint64_t frontend_ns = slot_est_ns(Slot::SimFrontCycle);
  const uint64_t backend_ns =
      slot_est_ns(Slot::SimBackComb) + slot_est_ns(Slot::SimBackSeq);
  const uint64_t memsubsystem_ns =
      slot_est_ns(Slot::SimMemLlcCombOutputs) + slot_est_ns(Slot::SimMemComb) +
      slot_est_ns(Slot::SimMemSeq) + slot_est_ns(Slot::SimMemLlcSeq);
  const uint64_t submodule_ns =
      slot_est_ns(Slot::SimCsrStatus) + slot_est_ns(Slot::SimClearAxiInputs) +
      slot_est_ns(Slot::SimAxiOutputs) + slot_est_ns(Slot::SimBridgeAxiToMem) +
      slot_est_ns(Slot::SimBridgeMemToAxi) + slot_est_ns(Slot::SimAxiInputs) +
      slot_est_ns(Slot::SimBack2Front) + slot_est_ns(Slot::SimAxiSeq);
  const uint64_t categorized_ns =
      frontend_ns + backend_ns + memsubsystem_ns + submodule_ns;
  const uint64_t uncategorized_ns =
      (categorized_ns <= sim_total_ns) ? (sim_total_ns - categorized_ns) : 0;

  const std::array<RollupRow, 5> rollup_rows = {{
      {"frontend", frontend_ns},
      {"backend", backend_ns},
      {"memsubsystem", memsubsystem_ns},
      {"submodule_glue", submodule_ns},
      {"uncategorized", uncategorized_ns},
  }};

  std::printf("\n=== Sim Host Profile Rollup (share of sim.cycle_total) ===\n");
  std::printf("%-18s %14s %10s\n", "category", "est_ms", "share");
  for (const auto &row : rollup_rows) {
    const double est_ms = static_cast<double>(row.est_ns) / 1.0e6;
    const double share =
        100.0 * static_cast<double>(row.est_ns) / static_cast<double>(sim_total_ns);
    std::printf("%-18s %14.3f %9.2f%%\n", row.name, est_ms, share);
  }

  const std::array<RollupRow, 8> glue_rows = {{
      {"csr_status", slot_est_ns(Slot::SimCsrStatus)},
      {"clear_axi_inputs", slot_est_ns(Slot::SimClearAxiInputs)},
      {"axi_outputs", slot_est_ns(Slot::SimAxiOutputs)},
      {"bridge_axi_to_mem", slot_est_ns(Slot::SimBridgeAxiToMem)},
      {"bridge_mem_to_axi", slot_est_ns(Slot::SimBridgeMemToAxi)},
      {"axi_inputs", slot_est_ns(Slot::SimAxiInputs)},
      {"back2front", slot_est_ns(Slot::SimBack2Front)},
      {"axi_seq", slot_est_ns(Slot::SimAxiSeq)},
  }};
  std::printf("\n=== Sim Submodule/Glue Breakdown ===\n");
  std::printf("%-18s %14s %10s\n", "slot", "est_ms", "share");
  for (const auto &row : glue_rows) {
    const double est_ms = static_cast<double>(row.est_ns) / 1.0e6;
    const double share =
        100.0 * static_cast<double>(row.est_ns) / static_cast<double>(sim_total_ns);
    std::printf("%-18s %14.3f %9.2f%%\n", row.name, est_ms, share);
  }
  std::printf("=== End Sim Host Profile Rollup ===\n");
}

} // namespace frontend_host_profile

#endif
