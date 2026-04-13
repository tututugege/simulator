#pragma once
#include "IssueQueue.h"
#include "config.h"
#include "IO.h"
#include <cstdint>
#include <list>

struct IsuOut {
  IssPrfIO *iss2prf;
  IssDisIO *iss2dis;
  IssAwakeIO *iss_awake;
};

struct IsuIn {
  DisIssIO *dis2iss;
  PrfAwakeIO *prf_awake;
  ExeIssIO *exe2iss;
  RobBroadcastIO *rob_bcast;
  DecBroadcastIO *dec_bcast;
};

// 延迟唤醒条目
struct LatencyEntry {
  bool valid;
  int countdown; // 剩余周期数
  uint32_t dest_preg;
  wire<BR_MASK_WIDTH> br_mask;
  uint32_t rob_idx;
  uint32_t rob_flag;
};

class Isu {
private:
  SimContext *ctx;

  // 1. 发射队列 (固定顺序: INT, LD, STA, STD, BR...)
  // 直接使用 vector，但依靠 init 的顺序保证下标对应枚举
  std::vector<IssueQueue> iqs;
  std::vector<IssueQueueConfig> configs;

  // 2. 延迟唤醒
  std::list<LatencyEntry> latency_pipe;
  std::list<LatencyEntry> latency_pipe_1; // 用于时序更新
  std::vector<int> committed_indices_buf[IQ_NUM];

public:
  uint32_t port_attributes[ISSUE_WIDTH];
  IsuIn in;
  IsuOut out;
  Isu(SimContext *ctx);
  void init();
  void comb_begin(); // 默认保持寄存器状态（*_1 <- *）

  // 组合逻辑
  void comb_ready(); // 更新 iss2dis->free_slots
  void comb_enq();   // 处理 dispatch 入队
  void comb_issue(); // 调度 + 发射 + 延迟处理
  void comb_awake(); // 汇总所有唤醒源
  void comb_calc_latency_next();

  // 辅助逻辑
  void comb_flush(); // 全局流水线清空

  // 时序逻辑
  void seq();

private:
  void add_iq(const IssueQueueConfig &cfg);
  int get_latency(UopType uop_type, wire<7> func7);
  void apply_wakeup_to_uop(IqStoredUop &uop) const;
};
