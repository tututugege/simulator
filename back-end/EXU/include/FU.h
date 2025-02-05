#pragma once

#include "config.h"

class FU {
public:
  void (*comb)(Inst_info *, FU &);
  void (*seq)(Inst_info *, FU &);
  int latency = 0;
  int cycle = 0;
  FU_TYPE type;
  int state;
  bool complete = false;
};
