/*
 * mmu_io.h
 *
 * MMU module I/O definitions, including internal
 * module connections and external interfaces.
 */
#pragma once
#include <config.h>
#include <cstdint>
#include <TLBEntry.h>

#define MAX_LSU_REQ_NUM 4  // 支持 LSU 同时发起的最大地址翻译请求数

namespace mmu_n {
  /*
   * AXI Channel States
   */
  enum channel_state {
    AXI_IDLE, // 空闲状态
    AXI_BUSY, // 忙碌状态
  };

  /*
   * Machine Privilege Levels
   */
  enum Privilege {
    M_MODE = 3, // Machine Mode
    S_MODE = 1, // Supervisor Mode
    U_MODE = 0  // User Mode
  };

  /*
   * Operation Types
   */
  enum MMU_OP_TYPE {
    OP_FETCH = 0, // 执行 (fetch)
    OP_LOAD = 1,  // 读 (load)
    OP_STORE = 2  // 写 (store)
  };

  /*
   * SATP Modes
   */
  enum SATP_MODE {
    MODE_BARE = 0, // Bare mode
    MODE_SV32 = 1  // Sv32 mode
  };

}

/*
 * MMU Inner Module connections
 *  - PTW: Page Table Walker
 *  - TLB: Translation Lookaside Buffer
 */
struct PTW_to_TLB {
  bool write_valid;   // PTW 命中，需要写入 MMU // 设计时，保证下一拍写入
  TLBEntry entry; // 准备填入的 TLB 条目
};

struct TLB_to_PTW {
  bool tlb_miss; // TLB 未命中，需要访问页表（PTW）
  uint32_t vpn1; // 虚拟页号一级索引
  uint32_t vpn0; // 虚拟页号二级索引
  mmu_n::MMU_OP_TYPE op_type; // TLB 失效对应的操作类型（读/写/INST）
};

struct TLB_IO_PTW {
  TLB_to_PTW *tlb2ptw; // TLB 到 PTW 的接口
  PTW_to_TLB *ptw2tlb; // PTW 到 TLB 的接口
};

/*
 *  MMU connections with external modules
 *  - MMU <-> Frontend (IFU)
 *  - MMU <-> Backend (LSU/DCache/CSR)
 */
struct satp_t {
  uint32_t ppn : 22;   // Page Table Base Address
  uint32_t asid : 9;   // ASID (Address Space Identifier)
  uint32_t mode : 1;   // 0: Bare, 1: Sv32
};

struct mmu_state_t {
  satp_t satp;      // Current satp register value
  uint32_t mstatus; // Current mstatus register value
  uint32_t sstatus; // Current sstatus register value
  mmu_n::Privilege privilege; // Current privilege level
};

// Flush 接口
struct tlb_flush_t {
  bool flush_valid;     // 是否有效
  uint32_t flush_vpn;   // 用于 sfence.vma 的虚拟页号
  uint32_t flush_asid;  // 用于 sfence.vma 的 ASID
};

// mmu_request & mmu_response interface (master + slave)
struct mmu_req_master_t {
  bool valid;    // 请求是否有效
  uint32_t vtag; // 请求的虚拟页号
  mmu_n::MMU_OP_TYPE op_type; // 请求的操作类型
};

struct mmu_req_slave_t {
  bool ready; // 是否准备好接收请求(正在做PTW时为 false)
};

struct mmu_resp_master_t {
  bool valid;     // miss/hit 响应是否有效
  bool excp;      // 响应是否产生异常
  bool miss;      // TLB 未命中
  uint32_t ptag;  // 命中时返回的物理页号
                  // (20/22 bits for 32/34-bit address)
};

struct mmu_resp_slave_t {
  // 是否准备好接收响应
  // (always true for current blocking and non-blocking design)
  bool ready; 
};

// dcache_request & dcache_response interface
struct dcache_req_master_t {
  bool valid;       // 请求是否有效
  uint32_t paddr;  // 请求的物理地址
};

struct dcache_req_slave_t {
  bool ready; // 是否准备好接收请求
};

struct dcache_resp_master_t {
  bool valid;      // 响应是否有效
  bool miss;       // 未命中 DCache
  uint32_t data;   // 命中时返回的数据(32-bit PTE)
};

struct dcache_resp_slave_t {
  bool ready; // 是否准备好接收响应
};

struct MMU_in_t {
  /*
   * Frontend -> MMU
   */
  mmu_req_master_t mmu_ifu_req;
  mmu_resp_slave_t mmu_ifu_resp;

  /*
   * LSU -> MMU
   *
   * 非阻塞式地址翻译请求设计，支持 LSU 同时发起多个地址翻译请求(也可以只发1
   * 个地址翻译请求)
   *
   * 约定：
   *  1. 多个请求/响应通道（数组不同下标）之间互不影响
   *  2. 各个请求/响应通道采用独立的多组 ready/valid 信号握手
   *  3. 支持同一个周期内，LSU 同时发起多个地址翻译请求
   *  4. 某个周期发起的请求，恰好在下一个周期得到响应(valid 拉高)；LSU 在
   *     完成 REQ 通道的 valid＆ready 握手后，需要将 RESP 通道的 ready 
   *     信号拉高（表示准备好接收响应）
   *  5. RESP 通道的 valid 信号拉高时， 出现 miss 或 hit 都表示有效的响
   *     应；hit 可以分为正常命中和异常命中(excp=0/1)，LSU 应当能够接受
   *     异常命中并做相应处理/最终传递给 ROB
   *  6. 如果多条请求中，存在若干条请求没有命中，MMU 会在所有未命中通道返
   *     回  miss 信号，并（与可能 miss 的 IFU 请求一起）仲裁选择其中一
   *     条请求进行 PTW 处理
   *  7. 对未命中的请求，(若没有被清流水线）LSU需要重新发起请求（直到命中）
   */
  mmu_req_master_t mmu_lsu_req[MAX_LSU_REQ_NUM];
  mmu_resp_slave_t mmu_lsu_resp[MAX_LSU_REQ_NUM];

  /*
   * DCache -> MMU
   *
   * MMU 发起对 DCache 的读请求，尝试读取可能已经写入DCache但是还没有写回
   * 内存的页表项(PTE)
   *
   * 约定：
   *  1. MMU 向 DCache 发送的请求地址是物理地址，不需要再做虚实地址转换
   *  2. REQ 通道和 RESP 通道分别有独立的 ready/valid 信号
   *  3. DCache 可以不命中 MMU 的请求，且未命中时只需要把 RESP 通道的 
   *     valid 信号和 miss 信号均置为有效，不需要再从内存加载到 Dcache
   *  4. 请求读取的数据规模为 32-bit (PTE 大小)
   *  5. 出现异常/中断/分支预测错误等情况需要清空对应的流水线时，MMU 取消对 
   *     DCache 的未完成请求，DCache 根据清空流水线信号取消对应请求的处理
   *      (具体而言：如果以状态机维护 REQ/RESP 通道的状态，则将状态机复位到
   *       初始状态)
   *  6. 请求读取时，只需要查看已经写入 DCache 的数据，不需要查看处在 Store
   *     Buffer 中但还没有写入 DCache 的数据；SFENCE.VMA 执行后，Store
   *     Buffer 需要清空/回写到 DCache，确保 TLB 请求访问时不会读到过时数据
   */
  dcache_req_slave_t mmu_dcache_req;
  dcache_resp_master_t mmu_dcache_resp;
  
  /*
   * Backend -> MMU
   */
  mmu_state_t state;
  tlb_flush_t tlb_flush;

};

struct MMU_out_t {
  /*
   * MMU -> Frontend
   */
  mmu_req_slave_t mmu_ifu_req;
  mmu_resp_master_t mmu_ifu_resp;

  /*
   * MMU -> LSU
   */
  mmu_req_slave_t mmu_lsu_req[MAX_LSU_REQ_NUM];
  mmu_resp_master_t mmu_lsu_resp[MAX_LSU_REQ_NUM];

  /*
   * MMU -> DCache
   */
  dcache_req_master_t mmu_dcache_req;
  dcache_resp_slave_t mmu_dcache_resp;

};

struct MMU_IO_t {
  MMU_in_t in;
  MMU_out_t out;
};
