#pragma once
#include "IO.h"

class Csr;
class SimContext;
class PtwMemPort;
class PtwWalkPort;

// ==========================================
// LSU IO 接口定义
// ==========================================

// 输入信号 (来自各个流水级)
typedef struct {
  RobCommitIO *rob_commit;
  RobBroadcastIO *rob_bcast;
  DecBroadcastIO *dec_bcast;
  CsrStatusIO *csr_status;
  DisLsuIO *dis2lsu;
  ExeLsuIO *exe2lsu;
  // MemRespIO *dcache_resp;
  // MemReadyIO *dcache_wready;
  
  DcacheLsuIO *dcache2lsu;
} LsuIn;

// 输出信号 (发送给各个流水级)
typedef struct {
  LsuDisIO *lsu2dis;
  LsuRobIO *lsu2rob;
  LsuExeIO *lsu2exe;
  // MemReqIO *dcache_req;
  // MemReqIO *dcache_wreq;
  LsuDcacheIO *lsu2dcache;
} LsuOut;

// StqEntry is defined in IO.h (included above).

// ==========================================
// LSU Backend 基类
// ==========================================
class AbstractLsu {
public:
  AbstractLsu(SimContext *ctx) : ctx(ctx) {}
  virtual ~AbstractLsu() {}

  // IO 端口
  LsuIn in;
  LsuOut out;
  PeripheralIO peripheral_io; // 供外设访问的 IO 端口
  SimContext *ctx;
  PtwMemPort *ptw_mem_port = nullptr;
  PtwWalkPort *ptw_walk_port = nullptr;

  // 组合逻辑 (按数据流向分为不同阶段)
  virtual void init() = 0;
  virtual void comb_lsu2dis_info() = 0; // -> lsu2dis (Dispatch Stage)
  virtual void comb_recv() = 0;         // -> Internal State (Execute Stage)
  virtual void comb_load_res() = 0;     // -> lsu2exe (Writeback Stage)
  virtual void comb_flush() = 0;        // -> Internal State Reset (Exception)

  // 时序逻辑
  virtual void seq() = 0; // Update registers & Tick latency

  // debug接口
  virtual StqEntry get_stq_entry(int stq_idx) = 0;

  virtual void set_csr(Csr *csr) {}
  virtual void set_ptw_mem_port(PtwMemPort *port) { ptw_mem_port = port; }
  virtual void set_ptw_walk_port(PtwWalkPort *port) { ptw_walk_port = port; }

  // 一致性访存接口 (供 MMU 使用)
  virtual uint32_t coherent_read(uint32_t p_addr) = 0;
  // sfence.vma 提交门控：是否存在“已提交但尚未从 STQ retire”的 store
  virtual bool has_committed_store_pending() const = 0;

  // 辅助函数
  int get_mem_width(int func3) {
    switch (func3 & 0b11) { // 忽略符号位
    case 0b00:
      return 1; // Byte
    case 0b01:
      return 2; // Half
    case 0b10:
      return 4; // Word
    default:
      return 4;
    }
  }

  // ==========================================
  // 1. 读对齐助手：从 raw_mem_val 中提取数据
  // ==========================================
  uint32_t extract_data(uint32_t raw_mem_val, uint32_t addr, int func3) {
    int bit_offset = (addr & 0x3) * 8; // 0, 8, 16, 24
    uint32_t result = 0;

    // 先移位，把目标数据移到最低位
    uint32_t shifted = raw_mem_val >> bit_offset;

    switch (func3) {
    case 0b000: // LB (Sign Ext)
      result = shifted & 0xFF;
      if (result & 0x80)
        result |= 0xFFFFFF00;
      break;
    case 0b001: // LH (Sign Ext)
      result = shifted & 0xFFFF;
      if (result & 0x8000)
        result |= 0xFFFF0000;
      break;
    case 0b010:         // LW
      result = shifted; // 取全部 32位
      break;
    case 0b100: // LBU (Zero Ext)
      result = shifted & 0xFF;
      break;
    case 0b101: // LHU (Zero Ext)
      result = shifted & 0xFFFF;
      break;
    default:
      result = shifted;
      break;
    }
    return result;
  }

  // ==========================================
  // 2. 写对齐助手：把 data 写入 raw_mem_val 的正确位置
  // ==========================================
  uint32_t merge_data_to_word(uint32_t old_word, uint32_t new_data,
                              uint32_t addr, int func3) {
    int bit_offset = (addr & 0x3) * 8;
    uint32_t mask = 0;

    // 1. 准备掩码 (Mask)
    switch (func3 & 0b11) { // 忽略符号位，只看宽度
    case 0b00:
      mask = 0xFF;
      break; // Byte
    case 0b01:
      mask = 0xFFFF;
      break; // Half
    default:
      mask = 0xFFFFFFFF;
      break; // Word
    }

    // 2. 清除旧数据的对应位
    // 比如写 Byte 到 offset 1: mask=0xFF, shift=8 -> mask=0xFF00,
    // ~mask=0xFFFF00FF
    uint32_t clear_mask = ~(mask << bit_offset);
    uint32_t result = old_word & clear_mask;

    // 3. 填入新数据
    // 确保新数据只取有效位，然后移位
    result |= ((new_data & mask) << bit_offset);

    return result;
  }
};
