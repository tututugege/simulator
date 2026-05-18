#pragma once

#include "Csr.h"
#include "PhysMemory.h"
#include "config.h"
#include <cstdint>
#include <iostream>

class PeripheralModel {
public:
  Csr *csr = nullptr;
  uint32_t *memory = nullptr;

  void bind(Csr *csr_ptr, uint32_t *memory_ptr) {
    csr = csr_ptr;
    memory = memory_ptr;
  }

  static bool is_modeled_mmio(uint32_t paddr) {
    const auto in_range = [](uint32_t addr, uint32_t base, uint32_t size) {
      if (addr < base) {
        return false;
      }
      return static_cast<uint64_t>(addr - base) < static_cast<uint64_t>(size);
    };
    return in_range(paddr, UART_ADDR_BASE, UART_MMIO_SIZE) ||
           in_range(paddr, PLIC_ADDR_BASE, PLIC_MMIO_SIZE);
  }

  uint32_t read_load(uint32_t paddr, uint8_t func3) const {
    if (memory == nullptr || !is_modeled_mmio(paddr)) {
      return 0;
    }

    const uint32_t shift = (paddr & 0x3u) * 8u;
    uint32_t data = pmem_read(paddr) >> shift;

    const uint32_t size = func3 & 0x3u;
    uint32_t sign = 0;
    uint32_t mask = 0xFFFFFFFFu;
    if (size == 0u) {
      mask = 0xFFu;
      if (data & 0x80u) {
        sign = 0xFFFFFF00u;
      }
    } else if (size == 1u) {
      mask = 0xFFFFu;
      if (data & 0x8000u) {
        sign = 0xFFFF0000u;
      }
    }

    data &= mask;
    if ((func3 & 0x4u) == 0) {
      data |= sign;
    }
    return data;
  }

  void on_commit_store(uint32_t paddr, uint32_t data, uint8_t func3) {
    if (csr == nullptr || memory == nullptr || !is_modeled_mmio(paddr)) {
      return;
    }

    const uint32_t shift = (paddr & 0x3u) * 8u;
    const uint32_t store_data = mask_store_data(data, func3);
    const uint32_t mask = mask_store_mask(func3) << shift;
    const uint32_t wdata = store_data << shift;
    uint32_t old_data = pmem_read(paddr);
    pmem_write(paddr, (old_data & ~mask) | (wdata & mask));

    if (paddr == UART_ADDR_BASE) {
      pmem_write(UART_ADDR_BASE, pmem_read(UART_ADDR_BASE) & 0xFFFFFF00u);
      char temp = static_cast<char>(data & 0xFFu);
      std::cout << temp << std::flush;
      return;
    }

    if (paddr == (UART_ADDR_BASE + 1u)) {
      const uint8_t cmd = static_cast<uint8_t>(store_data & 0xFFu);
      if (cmd == 7u) {
        pmem_write(PLIC_CLAIM_ADDR, 0x0000000Au);
        pmem_write(UART_ADDR_BASE, pmem_read(UART_ADDR_BASE) & 0xFFF0FFFFu);
        csr->CSR_RegFile_1[csr_mip] = csr->CSR_RegFile[csr_mip] | (1 << 9);
        csr->CSR_RegFile_1[csr_sip] = csr->CSR_RegFile[csr_sip] | (1 << 9);
      } else if (cmd == 5u) {
        pmem_write(UART_ADDR_BASE,
                   (pmem_read(UART_ADDR_BASE) & 0xFFF0FFFFu) | 0x00030000u);
      }
      return;
    }

    if (paddr == PLIC_CLAIM_ADDR && ((store_data & 0xFFu) == 0x0Au)) {
      pmem_write(PLIC_CLAIM_ADDR, 0x00000000u);
      csr->CSR_RegFile_1[csr_mip] = csr->CSR_RegFile[csr_mip] & ~(1 << 9);
      csr->CSR_RegFile_1[csr_sip] = csr->CSR_RegFile[csr_sip] & ~(1 << 9);
      return;
    }
  }

private:
  static uint32_t mask_store_data(uint32_t data, uint8_t func3) {
    if ((func3 & 0x3u) == 0u) {
      return data & 0xFFu;
    }
    if ((func3 & 0x3u) == 1u) {
      return data & 0xFFFFu;
    }
    return data;
  }

  static uint32_t mask_store_mask(uint8_t func3) {
    if ((func3 & 0x3u) == 0u) {
      return 0xFFu;
    }
    if ((func3 & 0x3u) == 1u) {
      return 0xFFFFu;
    }
    return 0xFFFFFFFFu;
  }
};
