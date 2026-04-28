#pragma once
#include "IO.h"
#include "ExuTypes.h"
#include "Fu.h" // 包含具体的 FU 定义
#include "config.h"
#include "FTQ.h"
#include <vector>

struct FuEntry {
  AbstractFU *fu;
  int fu_idx = -1;
  uint64_t support_mask;
  int lsu_agu_port = -1;
  int lsu_sdu_port = -1;
};

struct PortMapping {
  std::vector<FuEntry> entries;
};

struct Exu2FuIO {
  AbstractFU::FuInput entry[TOTAL_FU_COUNT];
};

struct Fu2ExuIO {
  AbstractFU::FuOutput entry[TOTAL_FU_COUNT];
};

struct ExuIn {
  PrfExeIO *prf2exe;
  DecBroadcastIO *dec_bcast;
  RobBroadcastIO *rob_bcast;
  CsrExeIO *csr2exe;
  LsuExeIO *lsu2exe;
  CsrStatusIO *csr_status;
  Fu2ExuIO *fu2exu;
};

struct ExuOut {
  ExePrfIO *exe2prf;
  ExeIssIO *exe2iss;
  ExeCsrIO *exe2csr;
  ExeLsuIO *exe2lsu;
  ExuIdIO *exu2id; // [New] Early Branch Resolution
  ExuRobIO *exu2rob;
  Exu2FuIO *exu2fu;
};

struct FuInstSlot {
  wire<1> valid = false;
  ExuInst uop{};
};

class Exu {
public:
  Exu(SimContext *ctx);
  ~Exu(); // 析构函数释放 FU 内存

  SimContext *ctx;
  std::vector<PortMapping> port_mappings;

  void init();
  void comb_begin(); // 默认保持寄存器状态（*_1 <- *）

  // 组合逻辑
  void comb_ftq_pc_req();
  void comb_exec(); // 核心：发射到 FU + 收集结果
  void comb_to_csr();
  void comb_pipeline(); // 将本拍 issue 入 FU 输入槽（在 pipeline 阶段生效）
  void comb_ready();    // 告诉 Issue 阶段 FU 是否空闲

  // 时序逻辑
  void seq();

  ExuIn in;
  ExuOut out;

  // === FU 资源管理 ===
  // 物理 FU 实例
  std::vector<AbstractFU *> units;
  int fu_to_port[TOTAL_FU_COUNT];
  FuInstSlot fu_inst_r[TOTAL_FU_COUNT];
  FuInstSlot fu_inst_r_1[TOTAL_FU_COUNT];

private:
  void comb_fu_ctrl();
  void comb_exu2fu_dispatch();
  void comb_fu_exec();
  void comb_fu2exu_collect();

  Exu2FuIO exu2fu_io{};
  Fu2ExuIO fu2exu_io{};
};
