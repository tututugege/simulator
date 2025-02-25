#pragma once

#include "config.h"

class FU {
public:
  void (*comb)(Inst_info *, FU &);
  void (*seq)(Inst_info *, FU &);
  FU_TYPE type;
  int latency = 0;
  int cycle = 0;
  int state;
  bool complete = false;
};
