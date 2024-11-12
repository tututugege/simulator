#pragma once
#include <config.h>

typedef struct {
  bool valid;
  uint32_t br_pc;
} PTAB_entry;

typedef struct {
  PTAB_entry entry[ALU_NUM];
} PTAB_out;

typedef struct {
  uint32_t br_idx[ALU_NUM];
} PTAB_in;

class PTAB {
  PTAB_entry entry[MAX_BR_NUM];

public:
  PTAB_in in;
  PTAB_out out;
  void read();
  void init();
};
