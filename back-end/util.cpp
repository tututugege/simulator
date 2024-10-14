#include "config.h"

bool is_branch(Inst_info inst) {

  return (inst.op == BR || inst.op == JALR || inst.op == JAL);
}

bool orR(bool *in, int num) {
  bool out = false;
  for (int i = 0; i < num; i++)
    out = out || in[i];
  return out;
}

bool andR(bool *in, int num) {
  bool out = true;
  for (int i = 0; i < num; i++)
    out = out && in[i];
  return out;
}
