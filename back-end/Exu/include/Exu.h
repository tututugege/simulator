#pragma once
#pragma once
#include "IO.h"
#include <Fu.h> // 包含具体的 FU 定义
#include <config.h>
#include <vector>

struct FuEntry {
  AbstractFU *fu;
  uint64_t support_mask;
};

struct PortMapping {
  std::vector<FuEntry> entries;
};

class ExuIn {
public:
  PrfExeIO *prf2exe;
  DecBroadcastIO *dec_bcast;
  RobBroadcastIO *rob_bcast;
  CsrExeIO *csr2exe;
  LsuExeIO *lsu2exe;
  CsrStatusIO *csr_status;
};

class ExuOut {
public:
  ExePrfIO *exe2prf;
  ExeIssIO *exe2iss;
  ExeCsrIO *exe2csr;
  ExeLsuIO *exe2lsu;
};

class Exu {
public:
  Exu(SimContext *ctx);
  ~Exu(); // 析构函数释放 FU 内存

  SimContext *ctx;
  std::vector<PortMapping> port_mappings;

  void init();

  // 组合逻辑
  void comb_exec(); // 核心：发射到 FU + 收集结果
  void comb_to_csr();
  void comb_pipeline(); // 准备下一周期的 inst_r
  void comb_ready();    // 告诉 Issue 阶段 FU 是否空闲

  // 时序逻辑
  void seq();

  ExuIO get_hardware_io(); // Hardware Reference

  ExuIn in;
  ExuOut out;

  // === FU 资源管理 ===
  // 物理 FU 实例
  std::vector<AbstractFU *> units;

  // pipeline registers
  InstEntry inst_r[ISSUE_WIDTH];   // 当前执行级指令
  InstEntry inst_r_1[ISSUE_WIDTH]; // 下一周期指令 (Latch)
                                   //
private:
  bool issue_stall[ISSUE_WIDTH];
};
