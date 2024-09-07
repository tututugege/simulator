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
#include "debug.h"
#include "utils.h"
#include <assert.h>
#include <device/mmio.h>
#include <isa.h>
#include <memory/host.h>
#include <memory/paddr.h>

#if defined(CONFIG_PMEM_MALLOC)
static uint8_t *mrom = NULL;
#else // CONFIG_PMEM_GARRAY
static uint8_t mrom[CONFIG_MROM_SIZE] PG_ALIGN = {};
static uint8_t sram[CONFIG_SRAM_SIZE] PG_ALIGN = {};
static uint8_t flash[CONFIG_FLASH_SIZE] PG_ALIGN = {};
static uint8_t psram[CONFIG_PSRAM_SIZE] PG_ALIGN = {};
static uint8_t sdram[CONFIG_SDRAM_SIZE] PG_ALIGN = {};
#endif

uint8_t *guest_to_host(paddr_t paddr) {
  uint8_t *ret;
  if (in_mrom(paddr)) {
    ret = mrom + paddr - CONFIG_MROM_BASE;
  } else if (in_sram(paddr)) {
    ret = sram + paddr - CONFIG_SRAM_BASE;
  } else if (in_flash(paddr)) {
    ret = flash + paddr - CONFIG_FLASH_BASE;
  } else if (in_psram(paddr)) {
    ret = psram + paddr - CONFIG_PSRAM_BASE;
  } else if (in_sdram(paddr)) {
    ret = sdram + paddr - CONFIG_SDRAM_BASE;
  } else {

    panic("error addr 0x%x\n", paddr);
  }
  return ret;
}
// paddr_t host_to_guest(uint8_t *haddr) { return haddr - mrom + CONFIG_MBASE;
// }

static word_t pmem_read(paddr_t addr, int len) {
  word_t ret = host_read(guest_to_host(addr), len);
  return ret;
}

static void pmem_write(paddr_t addr, int len, word_t data) {
  host_write(guest_to_host(addr), len, data);
}

static void out_of_bound(paddr_t addr) {
  panic("address = " FMT_PADDR " is out of bound of mrom [" FMT_PADDR
        ", " FMT_PADDR "] sram [" FMT_PADDR ", " FMT_PADDR
        "] at pc = " FMT_WORD,
        addr, MROM_LEFT, MROM_RIGHT, SRAM_LEFT, SRAM_RIGHT, cpu.pc);
}

void init_mem() {
#if defined(CONFIG_PMEM_MALLOC)
  mrom = malloc(CONFIG_MSIZE);
  assert(mrom);
#endif
#ifdef CONFIG_MEM_RANDOM
  uint32_t *p = (uint32_t *)mrom;
  int i;
  for (i = 0; i < (int)(CONFIG_MROM_SIZE / sizeof(p[0])); i++) {
    p[i] = rand();
  }

  p = (uint32_t *)sram;
  for (i = 0; i < (int)(CONFIG_SRAM_SIZE / sizeof(p[0])); i++) {
    p[i] = rand();
  }

#endif
  Log("physical memory area [" FMT_PADDR ", " FMT_PADDR "], [" FMT_PADDR
      ", " FMT_PADDR "]",
      MROM_LEFT, MROM_RIGHT, SRAM_LEFT, SRAM_RIGHT);
}

word_t paddr_read(paddr_t addr, int len) {

#ifdef CONFIG_MTRACE
  log_write(FMT_PADDR ": read memory address " FMT_PADDR ", len %d\n", cpu.pc,
            addr, len);

#endif

  if (likely(in_pmem(addr)))
    return pmem_read(addr, len);
  else if (in_uart(addr))
    return 0xFFFFFFFF;
  else if (in_clint(addr))
    return 0;
  IFDEF(CONFIG_DEVICE, return mmio_read(addr, len));
  out_of_bound(addr);
  return 0;
}

void paddr_write(paddr_t addr, int len, word_t data) {
#ifdef CONFIG_MTRACE
  log_write(FMT_PADDR ": write memory address " FMT_PADDR ", len %d\n", cpu.pc,
            addr, len);
#endif
  if (likely(in_pmem(addr))) {
    pmem_write(addr, len, data);
    return;
  } else if (in_uart(addr)) {
    printf("%c", (char)data);
    return;
  }
  IFDEF(CONFIG_DEVICE, mmio_write(addr, len, data); return);
  out_of_bound(addr);
}
