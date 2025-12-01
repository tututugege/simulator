#include "Cache.h"
#include "config.h"
#include <cstdint>
#include <cstdlib>

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

int Cache::cache_select_evict(uint32_t addr) {
  // 提取索引
  uint32_t index_mask = (1 << index_width) - 1;
  uint32_t index = (addr >> offset_width) & index_mask;

  // 计算需要的PLRU树深度和节点数
  int tree_depth = 0;
  int temp = way_num;
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
  if (way_num != (1 << tree_depth)) {
    // 检查selected_way是否超出实际way数量
    if (selected_way >= way_num) {
      selected_way = way_num - 1;
    }
  }

  return selected_way;
}

#else

int Cache::cache_select_evict(uint32_t addr) { return rand() % way_num; }

#endif

void Cache::update_plru(uint32_t index, int accessed_way) {
  // 更新PLRU树，标记accessed_way为最近使用
  int tree_depth = 0;
  int temp = way_num;
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

// miss
void Cache::cache_evict(uint32_t addr) {
  int evicit_way = cache_select_evict(addr);
  cache_tag[evicit_way][INDEX(addr)] = TAG(addr);
  cache_valid[evicit_way][INDEX(addr)] = true;
}

int Cache::cache_access(uint32_t addr) {
  perf.cache_access_num++;
  uint32_t tag;
  int i;

  for (i = 0; i < way_num; i++) {
    tag = cache_tag[i][INDEX(addr)];
    if (cache_valid[i][INDEX(addr)] && tag == TAG(addr)) {
      break;
    }
  }

  if (i == way_num) {
    cache_evict(addr);
    perf.cache_miss_num++;
    return MISS_LATENCY;
  } else {
#ifdef PLRU_EVICT
    update_plru(INDEX(addr), i);
#endif
  }

  return HIT_LATENCY;
}
