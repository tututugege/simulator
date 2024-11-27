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

#include "common.h"
#include <assert.h>
#include <cpu/cpu.h>
#include <difftest-def.h>
#include <isa.h>
#include <memory/paddr.h>
#include <stdint.h>

__EXPORT void difftest_memcpy(paddr_t addr, void *buf, size_t n,
                              bool direction) {
  int align = n & 0x3;
  int i;

  if (direction == DIFFTEST_TO_REF) {
    for (i = 0; i < align; i += 4) {
      paddr_write(addr + i, 4, *(word_t *)(buf + i));
    }
    for (; i < n; i++) {
      paddr_write(addr + i, 1, *(uint8_t *)(buf + i));
    }
  } else {
    for (i = 0; i < align; i += 4) {
      *(word_t *)(buf + i) = paddr_read(addr + i, 4);
    }
    for (; i < n; i++) {
      *(uint8_t *)(buf + i) = paddr_read(addr + i, 1);
    }
  }

  // printf("*******************\n");
  // printf("%x\n", ((uint32_t *)(buf))[0x18bc0 / 4]);
  // printf("%x\n", paddr_read(0x80018bc0, 4));
}

__EXPORT void difftest_regcpy(void *dut, bool direction) {
  if (direction == DIFFTEST_TO_REF) {
    memcpy(&cpu, dut, sizeof(uint32_t) * 33);
  } else {
    memcpy(dut, &cpu, sizeof(uint32_t) * 33);
  }
}

__EXPORT void difftest_exec(uint64_t n) { cpu_exec(n); }

__EXPORT void difftest_raise_intr(word_t NO) { assert(0); }

__EXPORT void difftest_init() {
  void init_mem();
  init_mem();
  /* Perform ISA dependent initialization. */
  init_isa();
}
