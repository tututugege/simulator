#include "config.h"

#define LOOP_INC(idx, length) idx = (idx + 1) % (length)
#define LOOP_DEC(idx, length) idx = (idx + (length) - 1) % (length)

bool andR(bool *in, int num);
bool orR(bool *in, int num);
bool is_branch(Inst_info);
bool is_CSR(Inst_info);
