#pragma once
#include "config.h"
#include "Exu.h"
#include "IO.h"

struct PrfOut {
  PrfExeIO *prf2exe;
  PrfAwakeIO *prf_awake;
};

struct PrfIn {
  IssPrfIO *iss2prf;
  ExePrfIO *exe2prf;
  DecBroadcastIO *dec_bcast;
  RobBroadcastIO *rob_bcast;
};

class Prf {
public:
  Prf(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
  PrfIn in;
  PrfOut out;

  void comb_begin(); // 默认保持寄存器状态（*_1 <- *）
  void comb_complete();
  void comb_awake();
  void comb_read();
  void comb_write();
  void comb_pipeline();
  void init();
  void seq();

  reg<32> reg_file[PRF_NUM];
  ExePrfIO::ExePrfEntry inst_r[ISSUE_WIDTH];

  wire<32> reg_file_1[PRF_NUM];
  ExePrfIO::ExePrfEntry inst_r_1[ISSUE_WIDTH];
};
