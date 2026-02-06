#pragma once
#include "config.h"
#include <Exu.h>
#include <IO.h>

class PrfOut {
public:
  PrfExeIO *prf2exe;
  PrfRobIO *prf2rob;
  PrfDecIO *prf2dec;
  PrfAwakeIO *prf_awake;
};

class PrfIn {
public:
  IssPrfIO *iss2prf;
  ExePrfIO *exe2prf;
  DecBroadcastIO *dec_bcast;
  RobBroadcastIO *rob_bcast;
  MemRespIO *cache2prf;
};

class Prf {
public:
  Prf(SimContext *ctx) { this->ctx = ctx; }
  SimContext *ctx;
  PrfIn in;
  PrfOut out;

  void comb_br_check();
  void comb_branch();
  void comb_complete();
  void comb_awake();
  void comb_read();
  void comb_flush();
  void comb_write();
  void comb_pipeline();
  void init();
  void seq();

#ifdef CONFIG_CACHE
  void comb_load();
  uint32_t load_data;
#endif

  reg<32> reg_file[PRF_NUM];
  InstEntry inst_r[ISSUE_WIDTH];

  wire<32> reg_file_1[PRF_NUM];
  InstEntry inst_r_1[ISSUE_WIDTH];
};
