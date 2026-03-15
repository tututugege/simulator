#include "SimpleCache.h"
#include "PhysMemory.h"
#include "oracle.h"
#include <algorithm>
#include <cstdint>

namespace {
bool get_plru_bit(const uint8_t tree[], int bit_index) {
  const int byte_idx = bit_index / 8;
  const int bit_offset = bit_index % 8;
  return ((tree[byte_idx] >> bit_offset) & 1) != 0;
}

void set_plru_bit(uint8_t tree[], int bit_index, bool value) {
  const int byte_idx = bit_index / 8;
  const int bit_offset = bit_index % 8;
  if (value) {
    tree[byte_idx] |= static_cast<uint8_t>(1u << bit_offset);
  } else {
    tree[byte_idx] &= static_cast<uint8_t>(~(1u << bit_offset));
  }
}
} // namespace

int SimpleCache::cache_select_evict(uint32_t addr) {
  const uint32_t index_mask = (1u << INDEX_WIDTH) - 1u;
  const uint32_t index = (addr >> OFFSET_WIDTH) & index_mask;

  int tree_depth = 0;
  for (int tmp = WAY_NUM; tmp > 1; tmp >>= 1) {
    tree_depth++;
  }

  int current_node = 0;
  int selected_way = 0;
  for (int level = 0; level < tree_depth; level++) {
    const bool go_left = !get_plru_bit(plru_tree[index], current_node);
    if (go_left) {
      set_plru_bit(plru_tree[index], current_node, true);
      current_node = 2 * current_node + 1;
    } else {
      set_plru_bit(plru_tree[index], current_node, false);
      current_node = 2 * current_node + 2;
    }
    selected_way = (selected_way << 1) | (go_left ? 0 : 1);
  }
  if (selected_way >= WAY_NUM) {
    selected_way = WAY_NUM - 1;
  }
  return selected_way;
}

void SimpleCache::update_plru(uint32_t index, int accessed_way) {
  int tree_depth = 0;
  for (int tmp = WAY_NUM; tmp > 1; tmp >>= 1) {
    tree_depth++;
  }

  int current_node = 0;
  int mask = 1 << (tree_depth - 1);
  for (int level = 0; level < tree_depth; level++) {
    const bool went_left = (accessed_way & mask) == 0;
    if (went_left) {
      set_plru_bit(plru_tree[index], current_node, true);
      current_node = 2 * current_node + 1;
    } else {
      set_plru_bit(plru_tree[index], current_node, false);
      current_node = 2 * current_node + 2;
    }
    mask >>= 1;
  }
}

void SimpleCache::cache_evict(uint32_t addr) {
  const int evict_way = cache_select_evict(addr);
  cache_tag[evict_way][get_index(addr)] = static_cast<uint32_t>(get_tag(addr));
  cache_valid[evict_way][get_index(addr)] = true;
}

int SimpleCache::cache_access(uint32_t addr, bool &is_miss) {
  is_miss = false;

  int hit_way = -1;
  const uint32_t tag = static_cast<uint32_t>(get_tag(addr));
  const int index = get_index(addr);
  for (int i = 0; i < WAY_NUM; i++) {
    if (cache_valid[i][index] && cache_tag[i][index] == tag) {
      hit_way = i;
      break;
    }
  }

  if (addr < 0x80000000u) {
    // Legacy behavior: low address region is treated as always-hit.
    return HIT_LATENCY;
  }

  if (hit_way < 0) {
    is_miss = true;
    cache_evict(addr);
    return MISS_LATENCY + (std::rand() % 10);
  }

  update_plru(static_cast<uint32_t>(index), hit_way);
  return HIT_LATENCY;
}

void SimpleCache::handle_store_req(const StoreReq &req, StoreResp &resp) {
  bool is_miss = false;
  const int latency = cache_access(req.addr, is_miss);
  (void)latency;

  if (ctx != nullptr) {
    ctx->perf.dcache_access_num++;
    ctx->perf.l1d_req_all++;
    ctx->perf.l1d_req_initial++;
    if (is_miss) {
      ctx->perf.dcache_miss_num++;
      ctx->perf.l1d_miss_mshr_alloc++;
    }
  }

  const uint32_t paddr = req.addr;
  const uint32_t old_val = pmem_read(paddr);
  const uint32_t wdata = req.data;
  uint32_t wmask = 0;
  for (int i = 0; i < 4; i++) {
    if (req.strb & (1u << i)) {
      wmask |= (0xFFu << (i * 8));
    }
  }
  const uint32_t new_val = (old_val & ~wmask) | (wdata & wmask);
  pmem_write(paddr, new_val);

  resp = {};
  resp.valid = true;
  resp.replay = 0;
  resp.req_id = req.req_id;
  resp.is_cache_miss = is_miss;
}

void SimpleCache::accept_load_req(int port, const LoadReq &req) {
  if (!req.valid) {
    return;
  }
  Assert(port >= 0 && port < LSU_LDU_COUNT);
  auto &q = pending_load_reqs[port];
  Assert(q.size() < MAX_PENDING_REQS &&
         "SimpleCache: pending load queue overflow");

  bool is_miss = false;
  const int latency = cache_access(req.addr, is_miss);
  PendingLoadReq pending{};
  pending.addr = req.addr;
  pending.uop = req.uop;
  pending.req_id = req.req_id;
  pending.uop.is_cache_miss = is_miss;
  pending.complete_time = sim_time + latency;
  q.push_back(pending);

  if (ctx != nullptr) {
    ctx->perf.dcache_access_num++;
    ctx->perf.l1d_req_all++;
    ctx->perf.l1d_req_initial++;
    if (is_miss) {
      ctx->perf.dcache_miss_num++;
      ctx->perf.l1d_miss_mshr_alloc++;
    }
  }
}

void SimpleCache::drive_load_resp(int port, LoadResp &resp) {
  resp = {};
  Assert(port >= 0 && port < LSU_LDU_COUNT);
  auto &q = pending_load_reqs[port];
  if (q.empty()) {
    return;
  }

  const PendingLoadReq &front = q.front();
  if (sim_time < front.complete_time) {
    return;
  }

  uint32_t mem_val = pmem_read(front.addr);
  MicroOp uop = front.uop;
  if (front.addr == 0x1fd0e000u) {
#ifdef CONFIG_BPU
    mem_val = static_cast<uint32_t>(sim_time);
#else
    mem_val = get_oracle_timer();
#endif
    uop.difftest_skip = true;
  } else if (front.addr == 0x1fd0e004u) {
    mem_val = 0;
    uop.difftest_skip = true;
  } else {
    uop.difftest_skip = false;
  }

  resp.valid = true;
  resp.replay = 0;
  resp.data = mem_val;
  resp.uop = uop;
  resp.req_id = front.req_id;
  q.pop_front();
}

void SimpleCache::init() {
  // Nothing else to initialize; constructor already resets all arrays/state.
}

void SimpleCache::comb() {
  if (dcache2lsu != nullptr) {
    dcache2lsu->resp_ports.clear();
  }
  if (lsu2dcache == nullptr || dcache2lsu == nullptr) {
    return;
  }

  // Simple model: stores complete in the same cycle without replay.
  for (int i = 0; i < LSU_STA_COUNT; i++) {
    const auto &req = lsu2dcache->req_ports.store_ports[i];
    if (!req.valid) {
      continue;
    }
    handle_store_req(req, dcache2lsu->resp_ports.store_resps[i]);
  }

  // Enqueue new load requests by source port so response routing stays stable.
  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    accept_load_req(i, lsu2dcache->req_ports.load_ports[i]);
  }

  // Emit at most one load response per port each cycle.
  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    drive_load_resp(i, dcache2lsu->resp_ports.load_resps[i]);
  }
}

void SimpleCache::seq() {}
