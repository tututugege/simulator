#include "../front_IO.h"
#include "PtwMemPort.h"
#include "include/ICacheTop.h"
#include "include/icache_module.h"
#include "GenericTable.h"
#include <cassert>

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
constexpr int kDataChunks = ICACHE_V1_WAYS * icache_module_n::ICACHE_V1_WORD_NUM;
constexpr int kDataChunkBits = icache_module_n::ICACHE_V1_WORD_BITS;
constexpr int kTagChunks = ICACHE_V1_WAYS;
constexpr int kTagChunkBits = 32;
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
} // namespace

void icache_seq_read(struct icache_in *in, struct icache_out *out) {
  assert(in != nullptr);
  assert(out != nullptr);
  (void)in;
  (void)out;
}

void icache_comb_calc(struct icache_in *in, struct icache_out *out) {
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

  icache.io.lookup_in = {};
  icache.io.lookup_in.lookup_resp_valid =
      data_resp.valid && tag_resp.valid && valid_resp.valid;
  for (uint32_t way = 0; way < ICACHE_V1_WAYS; ++way) {
    icache.io.lookup_in.lookup_set_tag[way] = tag_resp.payload.chunks[way];
    icache.io.lookup_in.lookup_set_valid[way] = valid_resp.payload.chunks[way];
    for (uint32_t word = 0; word < icache_module_n::ICACHE_V1_WORD_NUM; ++word) {
      uint32_t flat = way * icache_module_n::ICACHE_V1_WORD_NUM + word;
      icache.io.lookup_in.lookup_set_data[way][word] =
          data_resp.payload.chunks[flat];
    }
  }
  instance->comb();
  if (!in->run_comb_only) {
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
      tag_write.payload.chunks[icache.io.table_write.way] = icache.io.table_write.tag;
      tag_write.chunk_enable[icache.io.table_write.way] = true;
      valid_write.payload.chunks[icache.io.table_write.way] =
          icache.io.table_write.valid;
      valid_write.chunk_enable[icache.io.table_write.way] = true;
      for (uint32_t word = 0; word < icache_module_n::ICACHE_V1_WORD_NUM; ++word) {
        uint32_t flat = icache.io.table_write.way * icache_module_n::ICACHE_V1_WORD_NUM + word;
        data_write.payload.chunks[flat] = icache.io.table_write.data[word];
        data_write.chunk_enable[flat] = true;
      }
    }
    data_table.seq(data_read, data_write);
    tag_table.seq(tag_read, tag_write);
    valid_table.seq(valid_read, valid_write);
  }
  instance->syncPerf();
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
