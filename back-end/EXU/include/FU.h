#pragma once

#include "config.h"

class FU {
public:
  void (*fu_exec)(Inst_info *);
  int latency = 0;
  FU_TYPE type;
};
