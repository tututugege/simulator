#include "../front_IO.h"
#include "PtwMemPort.h"
#include "include/ICacheTop.h"
#include "host_profile.h"
#include "include/icache_module.h"
#include "GenericTable.h"
#include <cassert>

#ifndef CONFIG_ICACHE_FOCUS_VADDR_BEGIN
#define CONFIG_ICACHE_FOCUS_VADDR_BEGIN 0u
#endif

#ifndef CONFIG_ICACHE_FOCUS_VADDR_END
#define CONFIG_ICACHE_FOCUS_VADDR_END 0u
#endif

// icache.cpp is the front-end wrapper/orchestrator:
// - own the external tables used by icache_module
// - bind the selected ICacheTop glue object into front_top
// - drive per-cycle seq/perf handoff after the combinational glue is evaluated

// Define global ICache instance
icache_module_n::ICache icache;
PtwMemPort *icache_ptw_mem_port = nullptr;
PtwWalkPort *icache_ptw_walk_port = nullptr;
axi_interconnect::ReadMasterPort_t *icache_mem_read_port = nullptr;
static SimContext *icache_ctx = nullptr;

namespace {
using LookupTimingPolicy =
#if ICACHE_LOOKUP_LATENCY == 0
    RegfileTablePolicy;
#else
    SramTablePolicy;
#endif

constexpr int kSetAddrBits = icache_module_n::ICACHE_V1_INDEX_BITS;
constexpr int kRows = icache_module_n::ICACHE_V1_SET_NUM;
constexpr int kDataChunks = ICACHE_V1_WAYS;
constexpr int kDataChunkBits = ICACHE_LINE_SIZE * 8;
constexpr int kTagChunks = ICACHE_V1_WAYS;
constexpr int kTagChunkBits = icache_module_n::ICACHE_V1_TAG_BITS;
constexpr int kValidChunks = ICACHE_V1_WAYS;
constexpr int kValidChunkBits = 1;

using DataTable = GenericTable<kRows, kDataChunks, kDataChunkBits, LookupTimingPolicy>;
using TagTable = GenericTable<kRows, kTagChunks, kTagChunkBits, LookupTimingPolicy>;
using ValidTable = GenericTable<kRows, kValidChunks, kValidChunkBits, LookupTimingPolicy>;

GenericTableTimingConfig make_lookup_timing_config() {
  GenericTableTimingConfig cfg;
  cfg.fixed_latency = ICACHE_LOOKUP_LATENCY > 0 ? ICACHE_LOOKUP_LATENCY : 1;
#if ICACHE_SRAM_RANDOM_DELAY
  cfg.random_delay = true;
  cfg.random_min = ICACHE_SRAM_RANDOM_MIN;
  cfg.random_max = ICACHE_SRAM_RANDOM_MAX;
#else
  cfg.random_delay = false;
  cfg.random_min = 1;
  cfg.random_max = 1;
#endif
  return cfg;
}

inline bool icache_focus_enabled() {
  return CONFIG_ICACHE_FOCUS_VADDR_END > CONFIG_ICACHE_FOCUS_VADDR_BEGIN;
}

inline uint32_t icache_focus_index() {
  return (CONFIG_ICACHE_FOCUS_VADDR_BEGIN >> icache_module_n::ICACHE_V1_OFFSET_BITS) &
         (icache_module_n::ICACHE_V1_SET_NUM - 1u);
}

template <typename DataTableT, typename TagTableT, typename ValidTableT>
void dump_focus_row(const char *tag, const DataTableT &data_table,
                    const TagTableT &tag_table, const ValidTableT &valid_table,
                    uint32_t index) {
  if (!SIM_DEBUG_PRINT_ACTIVE || !icache_focus_enabled() ||
      index != icache_focus_index()) {
    return;
  }

  const auto &data_payload = data_table.peek_row(index);
  const auto &tag_payload = tag_table.peek_row(index);
  const auto &valid_payload = valid_table.peek_row(index);
  std::printf("[ICACHE][TABLE][%s] idx=%u\n", tag, index);
  for (uint32_t way = 0; way < ICACHE_V1_WAYS; ++way) {
    std::printf("[ICACHE][TABLE][%s] way=%u valid=%u tag=0x%05x data=[", tag,
                way, static_cast<unsigned>(valid_payload.chunks[way][0]),
                static_cast<unsigned>(tag_payload.chunks[way][0]));
    for (uint32_t word = 0; word < icache_module_n::ICACHE_V1_WORD_NUM; ++word) {
      std::printf("%s%08x", (word == 0) ? "" : " ",
                  static_cast<unsigned>(data_payload.chunks[way][word]));
    }
    std::printf("]\n");
  }
}

void bind_icache_runtime(ICacheTop *instance) {
  static PtwMemPort *bound_mem_port = nullptr;
  static PtwWalkPort *bound_walk_port = nullptr;
  static axi_interconnect::ReadMasterPort_t *bound_read_port = nullptr;
  static SimContext *bound_ctx = nullptr;

  if (bound_mem_port != icache_ptw_mem_port) {
    instance->set_ptw_mem_port(icache_ptw_mem_port);
    bound_mem_port = icache_ptw_mem_port;
  }
  if (bound_walk_port != icache_ptw_walk_port) {
    instance->set_ptw_walk_port(icache_ptw_walk_port);
    bound_walk_port = icache_ptw_walk_port;
  }
  if (bound_read_port != icache_mem_read_port) {
    instance->set_mem_read_port(icache_mem_read_port);
    bound_read_port = icache_mem_read_port;
  }
  if (bound_ctx != icache_ctx) {
    instance->setContext(icache_ctx);
    bound_ctx = icache_ctx;
  }
}

void dump_focus_read_row(const DataTable &data_table, const TagTable &tag_table,
                         const ValidTable &valid_table, uint32_t lookup_pc,
                         uint32_t lookup_index, bool resp_valid) {
  if (!SIM_DEBUG_PRINT_ACTIVE || !icache_focus_enabled() ||
      lookup_index != icache_focus_index() ||
      !resp_valid) {
    return;
  }
  std::printf("[ICACHE][TABLE][READ] pc=0x%08x idx=%u\n",
              static_cast<unsigned>(lookup_pc),
              static_cast<unsigned>(lookup_index));
  dump_focus_row("READ", data_table, tag_table, valid_table, lookup_index);
}

struct ICacheLookupTableResp {
  bool meta_resp_valid = false;
  bool data_snapshot_valid = false;
  uint32_t lookup_index = 0;
  uint32_t set_way_line_snapshot[ICACHE_V1_WAYS][icache_module_n::ICACHE_V1_WORD_NUM] = {{0}};
  uint32_t set_tag_snapshot[ICACHE_V1_WAYS] = {0};
  bool set_valid_snapshot[ICACHE_V1_WAYS] = {false};
};

ICacheLookupTableResp g_lookup_table_resp;

void cache_lookup_table_resp(const DataTable::ReadResp &data_resp,
                             const TagTable::ReadResp &tag_resp,
                             const ValidTable::ReadResp &valid_resp,
                             uint32_t lookup_index) {
  g_lookup_table_resp = {};
  g_lookup_table_resp.meta_resp_valid = tag_resp.valid && valid_resp.valid;
  g_lookup_table_resp.data_snapshot_valid = data_resp.valid;
  g_lookup_table_resp.lookup_index = lookup_index;
  for (uint32_t way = 0; way < ICACHE_V1_WAYS; ++way) {
    g_lookup_table_resp.set_tag_snapshot[way] = tag_resp.payload.chunks[way][0];
    g_lookup_table_resp.set_valid_snapshot[way] = valid_resp.payload.chunks[way][0];
    for (uint32_t word = 0; word < icache_module_n::ICACHE_V1_WORD_NUM; ++word) {
      g_lookup_table_resp.set_way_line_snapshot[way][word] =
          data_resp.payload.chunks[way][word];
    }
  }
}

void update_icache_perf_counters(const struct icache_in *in,
                                 const struct icache_out *out) {
  if (icache_ctx == nullptr || in == nullptr || out == nullptr) {
    return;
  }

  if (!in->refetch && out->perf_req_fire) {
    icache_ctx->perf.icache_access_num++;
  }
  if (icache.perf.miss_issue_valid) {
    icache_ctx->perf.icache_miss_num++;
  }
#ifndef USE_IDEAL_ICACHE
  if (icache.perf.miss_penalty_valid) {
    icache_ctx->perf.icache_miss_penalty_total_cycles +=
        static_cast<uint64_t>(icache.perf.miss_penalty_cycles);
    icache_ctx->perf.icache_miss_penalty_samples++;
  }
  if (icache.perf.axi_read_valid) {
    icache_ctx->perf.icache_axi_read_total_cycles +=
        static_cast<uint64_t>(icache.perf.axi_read_cycles);
    icache_ctx->perf.icache_axi_read_samples++;
  }
#endif
}
} // namespace

void icache_fill_lookup_meta_input(icache_module_n::ICache_lookup_in_t &dst) {
  dst = {};
  dst.meta_resp_valid = g_lookup_table_resp.meta_resp_valid;
  if (!g_lookup_table_resp.meta_resp_valid) {
    return;
  }

  for (uint32_t way = 0; way < ICACHE_V1_WAYS; ++way) {
    dst.lookup_set_tag[way] = g_lookup_table_resp.set_tag_snapshot[way];
    dst.lookup_set_valid[way] = g_lookup_table_resp.set_valid_snapshot[way];
  }
}

void icache_fill_lookup_data_input(icache_module_n::ICache_lookup_in_t &dst,
                                   bool req_valid, uint32_t req_index,
                                   uint32_t req_way) {
  dst.data_resp_valid = false;
  dst.data_resp_way = 0;
  for (uint32_t word = 0; word < icache_module_n::ICACHE_V1_WORD_NUM; ++word) {
    dst.data_resp_line[word] = 0;
  }
  if (!req_valid || !g_lookup_table_resp.data_snapshot_valid ||
      g_lookup_table_resp.lookup_index != req_index ||
      req_way >= ICACHE_V1_WAYS) {
    return;
  }

  dst.data_resp_valid = true;
  dst.data_resp_way = static_cast<uint8_t>(req_way);
  for (uint32_t word = 0; word < icache_module_n::ICACHE_V1_WORD_NUM; ++word) {
    dst.data_resp_line[word] =
        g_lookup_table_resp.set_way_line_snapshot[req_way][word];
  }
}

void icache_seq_read(struct icache_in *in, struct icache_out *out) {
  assert(in != nullptr);
  assert(out != nullptr);
  (void)in;
  (void)out;
}

void icache_peek_ready(struct icache_in *in, struct icache_out *out) {
  assert(in != nullptr);
  assert(out != nullptr);
  if (!in->reset) {
    assert(in->csr_status != nullptr &&
           "icache_peek_ready requires csr_status when not in reset");
  }
  ICacheTop *instance = get_icache_instance();
  bind_icache_runtime(instance);
  instance->setIO(in, out);
  instance->peek_ready();
}

void icache_comb_calc(struct icache_in *in, struct icache_out *out) {
  FRONTEND_HOST_PROFILE_SCOPE(IcacheComb);
  assert(in != nullptr);
  assert(out != nullptr);
  if (!in->reset) {
    assert(in->csr_status != nullptr &&
           "icache_comb_calc requires csr_status when not in reset");
  }
  static DataTable data_table(make_lookup_timing_config());
  static TagTable tag_table(make_lookup_timing_config());
  static ValidTable valid_table(make_lookup_timing_config());
  if (in->reset) {
    data_table.reset();
    tag_table.reset();
    valid_table.reset();
    dump_focus_row("RESET", data_table, tag_table, valid_table,
                   icache_focus_index());
  }
  ICacheTop *instance = get_icache_instance();
  bind_icache_runtime(instance);
  instance->setIO(in, out);

  uint32_t lookup_pc = in->fetch_address;
  if (icache.io.regs.req_valid_r) {
    lookup_pc = icache.io.regs.req_pc_r;
  } else if (icache.io.regs.lookup_pending_r) {
    lookup_pc = icache.io.regs.lookup_pc_r;
  }
  uint32_t lookup_index =
      (lookup_pc >> icache_module_n::ICACHE_V1_OFFSET_BITS) &
      (icache_module_n::ICACHE_V1_SET_NUM - 1u);

  DataTable::ReadReq data_read{};
  TagTable::ReadReq tag_read{};
  ValidTable::ReadReq valid_read{};
  data_read.enable = !in->reset && !in->refetch &&
                     (ICACHE_LOOKUP_LATENCY == 0
                          ? (in->icache_read_valid || icache.io.regs.req_valid_r ||
                             icache.io.regs.lookup_pending_r)
                          : icache.io.regs.lookup_pending_r);
  tag_read.enable = data_read.enable;
  valid_read.enable = data_read.enable;
  data_read.address = lookup_index;
  tag_read.address = lookup_index;
  valid_read.address = lookup_index;

  DataTable::ReadResp data_resp{};
  TagTable::ReadResp tag_resp{};
  ValidTable::ReadResp valid_resp{};
  data_table.comb(data_read, data_resp);
  tag_table.comb(tag_read, tag_resp);
  valid_table.comb(valid_read, valid_resp);
  dump_focus_read_row(data_table, tag_table, valid_table, lookup_pc,
                      lookup_index,
                      data_resp.valid && tag_resp.valid && valid_resp.valid);
  cache_lookup_table_resp(data_resp, tag_resp, valid_resp, lookup_index);
  instance->comb();
  update_icache_perf_counters(in, out);
  instance->seq();
  data_read.enable = icache.io.regs.lookup_pending_r;
  tag_read.enable = data_read.enable;
  valid_read.enable = data_read.enable;
  data_read.address = icache.io.regs.lookup_index_r;
  tag_read.address = icache.io.regs.lookup_index_r;
  valid_read.address = icache.io.regs.lookup_index_r;

  DataTable::WriteReq data_write{};
  TagTable::WriteReq tag_write{};
  ValidTable::WriteReq valid_write{};
  if (icache.io.table_write.we) {
    data_write.enable = true;
    tag_write.enable = true;
    valid_write.enable = true;
    data_write.address = icache.io.table_write.index;
    tag_write.address = icache.io.table_write.index;
    valid_write.address = icache.io.table_write.index;
    tag_write.payload.chunks[icache.io.table_write.way][0] =
        icache.io.table_write.tag;
    tag_write.chunk_enable[icache.io.table_write.way] = true;
    valid_write.payload.chunks[icache.io.table_write.way][0] =
        icache.io.table_write.valid;
    valid_write.chunk_enable[icache.io.table_write.way] = true;
    for (uint32_t word = 0; word < icache_module_n::ICACHE_V1_WORD_NUM; ++word) {
      data_write.payload.chunks[icache.io.table_write.way][word] =
          icache.io.table_write.data[word];
    }
    data_write.chunk_enable[icache.io.table_write.way] = true;
  }
  if (icache.io.table_write.invalidate_all) {
    valid_table.reset();
    dump_focus_row("INVAL", data_table, tag_table, valid_table,
                   icache_focus_index());
  }
  data_table.seq(data_read, data_write);
  tag_table.seq(tag_read, tag_write);
  valid_table.seq(valid_read, valid_write);
  if (icache.io.table_write.we) {
    dump_focus_row("WRITE", data_table, tag_table, valid_table,
                   static_cast<uint32_t>(icache.io.table_write.index));
  }
}

void icache_seq_write() {}

void icache_top(struct icache_in *in, struct icache_out *out) {
  icache_seq_read(in, out);
  icache_comb_calc(in, out);
  icache_seq_write();
}

void icache_set_context(SimContext *ctx) {
  icache_ctx = ctx;
}

void icache_set_ptw_mem_port(PtwMemPort *port) {
  ICacheTop *instance = get_icache_instance();
  instance->set_ptw_mem_port(port);
}

void icache_set_ptw_walk_port(PtwWalkPort *port) {
  ICacheTop *instance = get_icache_instance();
  instance->set_ptw_walk_port(port);
}

void icache_set_mem_read_port(axi_interconnect::ReadMasterPort_t *port) {
  ICacheTop *instance = get_icache_instance();
  instance->set_mem_read_port(port);
}
