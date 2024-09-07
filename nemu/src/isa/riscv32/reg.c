/***************************************************************************************
 * Copyright (c) 2014-2022 Zihao Yu, Nanjing University
 *
 * NEMU is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan
 *PSL v2. You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 *KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 *NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 ***************************************************************************************/

#include "local-include/reg.h"
#include <isa.h>
#include <sys/select.h>

const char *regs[] = {"zero", "ra", "sp",  "gp",  "tp", "t0", "t1", "t2",
                      "s0",   "s1", "a0",  "a1",  "a2", "a3", "a4", "a5",
                      "a6",   "a7", "s2",  "s3",  "s4", "s5", "s6", "s7",
                      "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

const char *csrs[] = {"mtvec", "mepc", "mcause", "mstatus"};

void isa_reg_display() {
  for (int i = 0; i < GPR_NUM; i++) {
    printf("%s: %08x ", regs[i], gpr(i));
    if (i % 8 == 7)
      putchar('\n');
  }
  printf("PC: %08x\n", cpu.pc);
}

word_t isa_reg_str2val(const char *s, bool *success) {
  assert(s[0] == '$');
  s++;
  int i;
  for (i = 0; i < 32; i++) {
    if (strcmp(s, regs[i]) == 0) {
      break;
    }
  }

  if (i != 32) {
    return gpr(i);
  }

  if (strcmp(s, "pc") == 0)
    return cpu.pc;

  *success = false;
  return 0;
}
