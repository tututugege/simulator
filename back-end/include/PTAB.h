#pragma once
#include <config.h>
#include <cstdint>

typedef struct {
  bool valid;
  uint32_t br_pc;
} PTAB_entry;

typedef struct {
  PTAB_entry entry[ALU_NUM];
  bool ready[INST_WAY];
  bool ptab_idx[INST_WAY];
  bool full;
} PTAB_out;

typedef struct {
  uint32_t ptab_idx[ALU_NUM];
  uint32_t ptab_wdata[ALU_NUM];
  bool valid[INST_WAY];
} PTAB_in;

class PTAB {
public:
  PTAB_entry entry[MAX_BR_NUM];
  PTAB_entry entry_1[MAX_BR_NUM];
  PTAB_in in;
  PTAB_out out;
  void seq();
  void comb_alloc();
  void comb_read();
  void init();
};
