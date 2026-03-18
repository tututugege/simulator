#include "SimpleCache.h"
#include "PeripheralModel.h"
#include "PhysMemory.h"
#include "oracle.h"
#include <cstdint>
#include <cstdlib>
#include <algorithm>

// 辅助函数：获取PLRU树中特定位的值
bool get_plru_bit(uint8_t tree[], int bit_index) {
  int byte_idx = bit_index / 8;
  int bit_offset = bit_index % 8;
  return (tree[byte_idx] >> bit_offset) & 1;
}

// 辅助函数：设置PLRU树中特定位的值
void set_plru_bit(uint8_t tree[], int bit_index, bool value) {
  int byte_idx = bit_index / 8;
  int bit_offset = bit_index % 8;
  if (value) {
    tree[byte_idx] |= (1 << bit_offset);
  } else {
    tree[byte_idx] &= ~(1 << bit_offset);
  }
}

#ifdef PLRU_EVICT

int SimpleCache::cache_select_evict(uint32_t addr) {
  // 提取索引
  uint32_t index_mask = (1 << INDEX_WIDTH) - 1;
  uint32_t index = (addr >> OFFSET_WIDTH) & index_mask;

  // 计算需要的PLRU树深度和节点数
  int tree_depth = 0;
  int temp = WAY_NUM;
  while (temp > 1) {
    tree_depth++;
    temp >>= 1;
  }

  // 从根节点开始遍历PLRU树，选择应该被替换的路
  int current_node = 0; // 从根节点开始
  int selected_way = 0;

  for (int level = 0; level < tree_depth; level++) {
    bool go_left = !get_plru_bit(plru_tree[index], current_node);

    // 根据当前节点的值决定走向左子树还是右子树
    if (go_left) {
      // 走向左子树，标记应该向右走（因为我们要替换一个）
      set_plru_bit(plru_tree[index], current_node, true);
      current_node = 2 * current_node + 1; // 左子节点
    } else {
      // 走向右子树，标记应该向左走
      set_plru_bit(plru_tree[index], current_node, false);
      current_node = 2 * current_node + 2; // 右子节点
    }

    // 将路径转换为way编号
    selected_way = (selected_way << 1) | (go_left ? 0 : 1);
  }

  // 处理非2的幂次方的情况（如果WAY_NUM不是2的幂）
  // 这里简化处理，实际应用中需要更复杂的逻辑
  if (WAY_NUM != (1 << tree_depth)) {
    // 检查selected_way是否超出实际way数量
    if (selected_way >= WAY_NUM) {
      selected_way = WAY_NUM - 1;
    }
  }

  return selected_way;
}

void SimpleCache::update_plru(uint32_t index, int accessed_way) {
  // 更新PLRU树，标记accessed_way为最近使用
  int tree_depth = 0;
  int temp = WAY_NUM;
  while (temp > 1) {
    tree_depth++;
    temp >>= 1;
  }

  // 从根节点到叶子节点的路径
  int current_node = 0;
  int mask = 1 << (tree_depth - 1); // 用于提取way的每一位

  for (int level = 0; level < tree_depth; level++) {
    bool went_left = !(accessed_way & mask); // 如果该位为0则向左走

    if (went_left) {
      // 访问了左子树，所以右子树更可能被替换，设置节点为1
      set_plru_bit(plru_tree[index], current_node, true);
      current_node = 2 * current_node + 1; // 左子节点
    } else {
      // 访问了右子树，所以左子树更可能被替换，设置节点为0
      set_plru_bit(plru_tree[index], current_node, false);
      current_node = 2 * current_node + 2; // 右子节点
    }

    mask >>= 1;
  }
}

#else

int SimpleCache::cache_select_evict(uint32_t addr) { return rand() % way_num; }

#endif

// miss
void SimpleCache::cache_evict(uint32_t addr) {
  int evicit_way = cache_select_evict(addr);
  cache_tag[evicit_way][get_index(addr)] = get_tag(addr);
  cache_valid[evicit_way][get_index(addr)] = true;
}

int SimpleCache::cache_access(uint32_t addr) {
  ctx->perf.dcache_access_num++;
  uint32_t tag;
  int i;

  for (i = 0; i < WAY_NUM; i++) {
    tag = cache_tag[i][get_index(addr)];
    if (cache_valid[i][get_index(addr)] && tag == (uint32_t)get_tag(addr)) {
      break;
    }
  }

  if (addr < 0x80000000) {
    return 1;
  }
  if (i == WAY_NUM) {
    cache_evict(addr);
    ctx->perf.dcache_miss_num++;
    return MISS_LATENCY + rand() % 10;
  } else {
#ifdef PLRU_EVICT
    update_plru(get_index(addr), i);
#endif
  }

  return HIT_LATENCY;
}

bool SimpleCache::should_write_ready() const {
  if (!stress_mode) {
    return true;
  }
  return (rand() % 100) < write_ready_pct;
}

void SimpleCache::handle_write_req(const MemReqIO &req) {
  if (!req.en || !req.wen) {
    return;
  }

  cache_access(req.addr);

  uint32_t paddr = req.addr;
  uint32_t old_val = pmem_read(paddr);
  uint32_t wdata = req.wdata;
  uint32_t wmask = 0;
  for (int i = 0; i < 4; i++) {
    if (req.wstrb & (1 << i)) {
      wmask |= (0xFFu << (i * 8));
    }
  }
  uint32_t new_val = (old_val & ~wmask) | (wdata & wmask);
  pmem_write(paddr, new_val);

  if (peripheral_model != nullptr) {
    peripheral_model->on_mem_store_effective(paddr, new_val);
  }
}

void SimpleCache::accept_req(const MemReqIO &req) {
  if (!req.en) {
    return;
  }
  Assert(pending_reqs.size() < MAX_PENDING_REQS &&
         "SimpleCache: pending read queue overflow");

  int latency = cache_access(req.addr);
  PendingReq pending{};
  pending.req = req;
  pending.req.uop.is_cache_miss = (latency >= MISS_LATENCY);
  pending.complete_time = sim_time + latency;
  pending_reqs.push_back(pending);
}

void SimpleCache::drive_resp(MemRespIO &resp) const { resp = pending_resp; }

void SimpleCache::init() {}

void SimpleCache::comb() {
  bool wready = should_write_ready();
  if (lsu_wready_io != nullptr) {
    lsu_wready_io->ready = wready;
  }

  if (wready && lsu_wreq_io != nullptr) {
    handle_write_req(*lsu_wreq_io);
  }

  if (lsu_req_io != nullptr) {
    accept_req(*lsu_req_io);
  }

  pending_resp = {};

  if (pending_reqs.empty()) {
    if (lsu_resp_io != nullptr) {
      *lsu_resp_io = pending_resp;
    }
    return;
  }

  const PendingReq &front = pending_reqs.front();
  if (sim_time < front.complete_time) {
    if (lsu_resp_io != nullptr) {
      *lsu_resp_io = pending_resp;
    }
    return;
  }

  const uint32_t p_addr = front.req.addr;
  pending_resp.valid = true;
  pending_resp.wen = front.req.wen;
  pending_resp.addr = p_addr;
  pending_resp.uop = front.req.uop;

  uint32_t mem_val = pmem_read(p_addr);
  if (p_addr == 0x1fd0e000) {
#ifdef CONFIG_BPU
    mem_val = sim_time;
#else
    mem_val = get_oracle_timer();
#endif
    pending_resp.uop.difftest_skip = true;
  } else if (p_addr == 0x1fd0e004) {
    mem_val = 0;
    pending_resp.uop.difftest_skip = true;
  } else {
    pending_resp.uop.difftest_skip = false;
  }
  pending_resp.data = mem_val;

  pending_reqs.pop_front();

  if (lsu_resp_io != nullptr) {
    *lsu_resp_io = pending_resp;
  }
}

void SimpleCache::seq() {}
