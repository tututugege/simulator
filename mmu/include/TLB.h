#pragma once

#include <TLBEntry.h>
#include <iostream>
#include <mmu_io.h>
using namespace std;

// #define TLB_SIZE 128 // TLB 的大小
// #define TLB_SIZE 4 // TLB 的大小
#define TLB_SIZE 32 // TLB 的大小

struct tlb_read_port_t {
  // input
  mmu_req_master_t *in;
  // output
  struct {
    bool valid;     // 响应是否有效
    TLBEntry entry; // TLB 条目输出
    bool hit;       // TLB 命中
    int hit_index;  // 命中的条目索引（用于 PLRU 更新）
  } out;
};

struct tlb_write_port_t {
  // input
  tlb_flush_t *flush;
  struct {
    bool write_valid; // 是否需要写入 TLB
    TLBEntry entry;   // 准备写入的 TLB 条目
  } wr;
};

// 定义替换算法：TLB_RANDOM/TLB_PLRU (其中之一必须被定义)
// #define TLB_RANDOM // 使用随机替换算法
#define TLB_PLRU // 使用 PLRU 替换算法

class TLB {
public:
  TLB(PTW_to_TLB *ptw2tlb_ptr,
      // IFU Port Signals (Input+Output)
      mmu_req_master_t *ifu_req_ptr,
      // LSU Port Signals (Input+Output)
      mmu_req_master_t *lsu_req_ptr,
      // Other Signals
      tlb_flush_t *tlb_flush, satp_t *satp_ptr);

  void reset(); // 重置 TLB
  void comb_frontend();
  void comb_backend();
  void comb_write();
  void comb_flush();
  void comb_arbiter();
  void comb_replacement(); // 组合逻辑：计算替换策略更新
  void lookup(tlb_read_port_t &io_r);
  void seq();

  // Interface 0 (IFU)
  tlb_read_port_t ifu_io;
  // Interface 1 (LSU)
  tlb_read_port_t lsu_io[MAX_LSU_REQ_NUM];

  // Interface (Write)
  tlb_write_port_t write_io;
  PTW_to_TLB *ptw_in; // PTW 到 TLB 的接口

  // satp.asid, 9bits
  satp_t *satp;

  void log_entry(int index) const {
    if (index < 0 || index >= TLB_SIZE) {
      cerr << "TLB: Invalid index for log_entry!" << endl;
      return;
    }
    entries[index].log_entry();
  }

private:
  TLBEntry entries[TLB_SIZE];
  bool valid_next[TLB_SIZE]; // 组合逻辑计算的下一个有效性
  int wr_index;              // 组合逻辑计算的写入索引

  /*
   * 仲裁逻辑：统一处理所有端口的命中结果
   */
  struct {
    bool has_hit;  // 是否有任何端口命中
    int hit_index; // 仲裁选择的命中索引
    int hit_count; // 总命中次数（用于调试）
  } global_arbitration;

#ifdef TLB_PLRU
  // 要求：TLB_SIZE 必须是 2 的幂次方
  static constexpr int PLRU_TREE_SIZE = TLB_SIZE - 1;
  static constexpr int PLRU_TREE_DEPTH =
      __builtin_ctz(TLB_SIZE); // log2(TLB_SIZE)
  reg<1> plru_tree[PLRU_TREE_SIZE];
  wire<1> plru_tree_next[PLRU_TREE_SIZE]; // 组合逻辑计算的下一个状态

  struct {
    bool update_valid; // 是否需要更新 PLRU 树
    int update_index;  // 需要更新的条目索引
  } plru_update_req;   // 组合逻辑计算的 PLRU 更新请求

  void plru_reset();
  void plru_comb_update(); // 组合逻辑：计算 PLRU 树的更新
  void plru_seq_update();  // 时序逻辑：应用 PLRU 树的更新
  int plru_get_victim() const;
  void plru_calc_update(int index, bool tree_state[]);
#endif

  /*
   * Show TLB valid entries (by bits)
   */
  void show_valid_entries() const {
    cout << "Valid TLB entries:\n ";
    for (int i = 0; i < TLB_SIZE; ++i) {
      if (entries[i].pte_valid) {
        cout << "1"; // 有效条目
      } else {
        cout << "0"; // 无效条目
      }
      if ((i + 1) % 32 == 0) {
        cout << "\n ";
      }
    }
    cout << endl;
  }
};
