#include "include/TLB.h"
#include <iostream>
#include <cstring>

TLB::TLB(
  PTW_to_TLB *ptw2tlb_ptr,
  mmu_req_master_t *ifu_req_ptr,
  mmu_req_master_t *lsu_req_ptr,
  tlb_flush_t *tlb_flush_ptr,
  satp_t *satp_ptr  
) : 
  entries{},
  valid_next{},
  wr_index(0)
{
#ifdef TLB_PLRU
  plru_reset();
#endif

  // MMU Internal module connections
  ptw_in = ptw2tlb_ptr;

  // MMU External module connections
  ifu_io.in = ifu_req_ptr;
  for (int i = 0; i < MAX_LSU_REQ_NUM; ++i) {
    lsu_io[i].in = &lsu_req_ptr[i];
  }
  write_io.flush = tlb_flush_ptr;
  satp = satp_ptr;
}

void TLB::reset() {
  for (int i = 0; i < TLB_SIZE; ++i) {
    entries[i] = {}; // set to zero
    valid_next[i] = false;
  }
  wr_index = 0;
  
  global_arbitration.has_hit = false;
  global_arbitration.hit_index = 0;
  global_arbitration.hit_count = 0;
  
#ifdef TLB_PLRU
  plru_reset();
#endif
}

void TLB::comb_frontend() {
  if (ifu_io.in->valid) {
    lookup(ifu_io);
  }
}

void TLB::comb_backend() {
  for (int i = 0; i < MAX_LSU_REQ_NUM; ++i) {
    lookup(lsu_io[i]);
  }
}

void TLB::comb_write() {
  write_io.wr.write_valid = ptw_in->write_valid;
  write_io.wr.entry = ptw_in->entry;
  
  if (ptw_in->write_valid) {
#ifdef TLB_RANDOM
    // 随机策略：优先选择无效条目
    bool found_invalid = false;
    for (int i = 0; i < TLB_SIZE; ++i) {
      if (!entries[i].pte_valid) {
        wr_index = i;
        found_invalid = true;
        break;
      }
    }
    if (!found_invalid) {
      // wr_index = rand() % TLB_SIZE;
      wr_index = (wr_index + 1) % TLB_SIZE; // 伪随机，循环覆盖
    }
#endif

#ifdef TLB_PLRU
    wr_index = plru_get_victim();
#endif
  } else {
    wr_index = 0; // 默认值，不会被使用
  }
}

void TLB::comb_flush() {
  // 默认保持当前状态
  for (int i = 0; i < TLB_SIZE; ++i) {
    valid_next[i] = entries[i].pte_valid;
  }
  
  if (!write_io.flush->flush_valid) {
    return;
  }
  
  uint32_t flush_vpn = write_io.flush->flush_vpn;
  uint32_t flush_vpn1 = (flush_vpn >> 10) & 0x3FF;
  uint32_t flush_vpn0 = flush_vpn & 0x3FF;
  uint32_t flush_asid = write_io.flush->flush_asid;
  
  for (int i = 0; i < TLB_SIZE; ++i) {
    if (entries[i].pte_valid) {
      bool asid_match = flush_asid == 0 || entries[i].asid == flush_asid;
      bool vpn1_match = entries[i].vpn1 == flush_vpn1; 
      bool vpn0_match = entries[i].vpn0 == flush_vpn0 || entries[i].megapage;
      bool vpn_match = flush_vpn == 0 || (vpn1_match && vpn0_match);
      
      if (asid_match && vpn_match) {
        valid_next[i] = false;
      }
    }
  }
}

/*
 * 固定优先级仲裁：LSU[0] > LSU[1] > ... > IFU
 */
void TLB::comb_arbiter() {
  global_arbitration.has_hit = false;
  global_arbitration.hit_index = 0;
  
  // 按优先级顺序检查，找到第一个就使用
  for (int i = 0; i < MAX_LSU_REQ_NUM; ++i) {
    if (lsu_io[i].out.valid && lsu_io[i].out.hit) {
      global_arbitration.has_hit = true;
      global_arbitration.hit_index = lsu_io[i].out.hit_index;
      return; // 找到最高优先级，直接返回
    }
  }
  
  if (ifu_io.out.valid && ifu_io.out.hit) {
    global_arbitration.has_hit = true;
    global_arbitration.hit_index = ifu_io.out.hit_index;
  }
}

/*
 * 组合逻辑：计算替换策略相关信息
 * 基于全局仲裁结果
 */
void TLB::comb_replacement() {
#ifdef TLB_PLRU
  // 默认不更新
  plru_update_req.update_valid = false;
  plru_update_req.update_index = 0;
  
  // 使用全局仲裁结果
  if (global_arbitration.has_hit) {
    plru_update_req.update_valid = true;
    plru_update_req.update_index = global_arbitration.hit_index;
  }
  
  // PTW 写入的优先级最高（覆盖 hit 更新）
  if (write_io.wr.write_valid) {
    plru_update_req.update_valid = true;
    plru_update_req.update_index = wr_index;
  }
  
  // 计算 PLRU 树的下一个状态
  plru_comb_update();
#endif
}

/*
 * 简化的 lookup：只负责查找，不做仲裁
 * 如果有多个匹配，报错退出（TLB不应该有重复条目）
 */
void TLB::lookup(tlb_read_port_t &io_r) {
  // 默认输出
  io_r.out.valid = false;
  io_r.out.hit = false;
  io_r.out.entry = {};
  io_r.out.hit_index = 0;
  
  if (!io_r.in->valid) {
    return;
  }
  
  io_r.out.valid = true;
  uint32_t vpn1 = (io_r.in->vtag >> 10) & 0x3FF; 
  uint32_t vpn0 = io_r.in->vtag & 0x3FF; 
  
  int hit_index = -1;
  int hit_count = 0;
  
  for (int i = 0; i < TLB_SIZE; ++i) {
    if (entries[i].pte_valid
        && entries[i].vpn1 == vpn1 
        && entries[i].asid == satp->asid) {
      if (entries[i].megapage || entries[i].vpn0 == vpn0) {
        if (hit_count == 0) {
          hit_index = i;
        }
        hit_count++;
      }
    }
  }
  
  // 检查多重命中（同一查询匹配多个TLB条目是错误的）
  if (hit_count > 1) {
    cout << "TLB Fatal Error: Multiple entries match in single lookup" << endl;
    cout << "  vtag: 0x" << hex << io_r.in->vtag << dec << endl;
    cout << "  vpn1: " << vpn1 << ", vpn0: " << vpn0 << endl;
    cout << "  asid: " << satp->asid << endl;
    cout << "  Hit count: " << hit_count << endl;
    cout << "  Matched entries:" << endl;
    
    for (int i = 0; i < TLB_SIZE; ++i) {
      if (entries[i].pte_valid
          && entries[i].vpn1 == vpn1 
          && entries[i].asid == satp->asid) {
        if (entries[i].megapage || entries[i].vpn0 == vpn0) {
          cout << "    [" << i << "]: megapage=" << entries[i].megapage
               << ", vpn0=" << entries[i].vpn0 << endl;
          log_entry(i);
        }
      }
    }
    cout << "  sim_time: " << sim_time << endl;
    exit(1);
  }
  
  if (hit_count == 1) {
    io_r.out.hit = true;
    io_r.out.entry = entries[hit_index];
    io_r.out.hit_index = hit_index;
  }
}

void TLB::seq() {
  /*
   * 时序逻辑：应用 flush 的有效性更新
   */
  if (write_io.flush->flush_valid) {
    for (int i = 0; i < TLB_SIZE; ++i) {
      entries[i].pte_valid = valid_next[i];
    }
  }
  
  /*
   * 时序逻辑：应用 PTW 回填
   */
  if (write_io.wr.write_valid) {
    if (wr_index < 0 || wr_index >= TLB_SIZE) {
      cerr << "TLB: Invalid write index: " << wr_index << endl;
      exit(1);
    }
    
    // 检查是否覆盖有效条目
    if (entries[wr_index].pte_valid) {
      int valid_count = 0;
      for (int i = 0; i < TLB_SIZE; ++i) {
        if (entries[i].pte_valid) valid_count++;
      }
      // if (valid_count != TLB_SIZE) {
      if (valid_count != TLB_SIZE &&
          !write_io.flush->flush_valid) {
        cout << "[TLB::seq] Warning: Overwriting valid TLB entry at index " 
           << wr_index << " at cycle " << sim_time
           << ", valid_count = " << valid_count
           << ", TLB_SIZE = " << TLB_SIZE << endl;
        show_valid_entries();
      }
    }
    
    entries[wr_index] = write_io.wr.entry;
    
    if (LOG) {
      show_valid_entries();
      log_entry(wr_index);
      cout << "write TLB index " << dec << wr_index
          << " at sim_time: " << dec << sim_time << endl;
      cout << endl;
    }
  }

#ifdef TLB_PLRU
  // 时序逻辑：更新 PLRU 树
  plru_seq_update();
#endif
}

// ==================== PLRU Implementation ====================
#ifdef TLB_PLRU

void TLB::plru_reset() {
  memset(plru_tree, 0, sizeof(plru_tree));
  memset(plru_tree_next, 0, sizeof(plru_tree_next));
  plru_update_req.update_valid = false;
  plru_update_req.update_index = 0;
}

/*
 * 组合逻辑：计算 PLRU 树的更新
 */
void TLB::plru_comb_update() {
  // 默认保持当前状态
  memcpy(plru_tree_next, plru_tree, sizeof(plru_tree));
  
  // 如果有访问命中或写入，更新树状态
  if (plru_update_req.update_valid) {
    plru_calc_update(plru_update_req.update_index, plru_tree_next);
  }
}

/*
 * 时序逻辑：应用 PLRU 树状态更新
 */
void TLB::plru_seq_update() {
  memcpy(plru_tree, plru_tree_next, sizeof(plru_tree));
}

/*
 * 获取 PLRU 替换的受害者索引
 */
int TLB::plru_get_victim() const {
  // 优先选择无效条目
  for (int i = 0; i < TLB_SIZE; ++i) {
    if (!entries[i].pte_valid) {
      return i;
    }
  }
  
  // 使用 PLRU 算法
  int node = 0;
  
  for (int level = 0; level < PLRU_TREE_DEPTH; ++level) {
    if (plru_tree[node]) {
      node = 2 * node + 2; // 右子树
    } else {
      node = 2 * node + 1; // 左子树
    }
  }
  
  int victim_index = node - (TLB_SIZE - 1);
  
  if (victim_index < 0 || victim_index >= TLB_SIZE) {
    cerr << "TLB PLRU plru_get_victim(): Invalid victim index calculated: " << victim_index << endl;
    exit(1);
  }
  
  return victim_index;
}

/*
 * 计算访问某个条目后的树状态
 */
void TLB::plru_calc_update(int index, bool tree_state[]) {
  if (index < 0 || index >= TLB_SIZE) {
    cerr << "TLB PLRU plru_calc_update(): Invalid index for update: " << index << endl;
    exit(1);
  }
  
  int leaf = index + (TLB_SIZE - 1);
  int current = leaf;
  
  while (current > 0) {
    int parent = (current - 1) / 2;
    
    if (current == 2 * parent + 1) {
      // 左子节点：将父节点指向右子树
      tree_state[parent] = true;
    } else {
      // 右子节点：将父节点指向左子树
      tree_state[parent] = false;
    }
    
    current = parent;
  }
}

#endif