// PTW: Page Table Walker
// 用于处理缺页异常和页表项的加载
#pragma once

#include <mmu_io.h>
#include <cstdint>

struct PTW_in {
  // from TLB
  TLB_to_PTW *tlb2ptw; // TLB 到 PTW 的接口
  // from D-Cache
  dcache_resp_master_t *dcache_resp;
  dcache_req_slave_t *dcache_req;
};

struct PTW_out {
  // to TLB
  PTW_to_TLB *ptw2tlb; // PTW 到 TLB 的接口
  // to D-Cache
  dcache_req_master_t *dcache_req;
  dcache_resp_slave_t *dcache_resp;
};

enum PTW_DCache_state {
  DCACHE_IDLE, // D-Cache 空闲状态
  DCACHE_BUSY, // D-Cache 忙碌状态
};

// define namespace ptw_n
namespace ptw_n {
  enum ptw_state {
    IDLE,       // 空闲状态
    CACHE_1,    // 一级页表加载状态
    MEM_1,      // D-Cache miss，访问内存
    CACHE_2,    // 二级页表加载状态
    MEM_2       // D-Cache miss，访问内存
  };
}

class PTW {
  public:
    PTW(
      TLB_to_PTW *tlb2ptw_ptr,
      PTW_to_TLB *ptw2tlb_ptr,
      dcache_req_master_t *req_m,
      dcache_req_slave_t *req_s,
      dcache_resp_master_t *resp_m,
      dcache_resp_slave_t *resp_s,
      mmu_state_t *mmu_state_ptr
    );
    void reset(); // 初始化 PTW 状态
    void comb(); // 组合逻辑，处理 PTW 请求
    void seq(); // 时序逻辑，更新 PTW 状态

    // interface
    PTW_in in;  // PTW 输入接口
    PTW_out out; // PTW 输出接口

    // PTW 共享 MMU 的状态信息
    mmu_state_t *mmu_state; // MMU 状态指针

    // PTW 状态
    ptw_n::ptw_state ptw_state, ptw_state_next; // 当前 PTW 状态

    // for walker
    pte_t pte1; // 一级页表项
    pte_t pte2; // 二级页表项

    // register save from TLB miss
    uint32_t vpn1; // 虚拟页号一级索引, 10-bit
    uint32_t vpn0; // 虚拟页号二级索引, 10-bit
    uint8_t op_type; // TLB 失效对应的操作类型（读/写/INST），2-bits

    /*
     * 打印 PTE 的所有字段
     */
    void log_pte(pte_t pte);
    string ptw_state_to_string(ptw_n::ptw_state state);

  private:
    PTW_DCache_state dcache_state; // D-Cache 状态
    PTW_DCache_state dcache_state_next; // 下个cycle D-Cache 状态

    /*
     * 检查页表项是否有效
     * PTE: Page Table Entry [31:0]
     * op_type: 操作类型
     *  - 0: 执行 (fetch)
     *  - 1: 读 (load)
     *  - 2: 写 (store)
     * stage_1: 是否为一级页表项检查
     */
    bool is_page_fault(pte_t pte, uint8_t op_type, bool stage_1, bool *is_megapage);

};