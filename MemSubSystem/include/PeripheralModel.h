#pragma once

#include "IO.h"
#include "PhysMemory.h"
#include "config.h"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>
#include "debug_config.h"

class PeripheralModel {
public:
  static constexpr uint32_t kDmaCtrlStart = 1u << 0;
  static constexpr uint32_t kDmaCtrlIrqEn = 1u << 1;
  static constexpr uint32_t kDmaStatusBusy = 1u << 0;
  static constexpr uint32_t kDmaStatusDone = 1u << 1;
  static constexpr uint32_t kDmaStatusError = 1u << 2;

  CsrInterruptInjectIO *csr_interrupt_inject = nullptr;
  uint32_t *memory = nullptr;
  uint64_t timer_offset_ = 0;
  uint64_t timer_cmp_ = 0;
  uint32_t dma_src_ = 0;
  uint32_t dma_dst_ = 0;
  uint32_t dma_len_ = 0;
  uint32_t dma_ctrl_ = 0;
  uint32_t dma_status_ = 0;
  bool uart_irq_pending_ = false;
  bool dma_irq_pending_ = false;
  void (*ram_sync_hook_)(uint32_t paddr, size_t size_bytes) = nullptr;

  void bind(CsrInterruptInjectIO *csr_interrupt_inject_ptr,
            uint32_t *memory_ptr) {
    csr_interrupt_inject = csr_interrupt_inject_ptr;
    memory = memory_ptr;
  }

  void set_ram_sync_hook(void (*hook)(uint32_t paddr, size_t size_bytes)) {
    ram_sync_hook_ = hook;
  }

  static bool is_modeled_mmio(uint32_t paddr) {
    const auto in_range = [](uint32_t addr, uint32_t base, uint32_t size) {
      if (addr < base) {
        return false;
      }
      return static_cast<uint64_t>(addr - base) < static_cast<uint64_t>(size);
    };
    return in_range(paddr, UART_ADDR_BASE, UART_MMIO_SIZE) ||
           in_range(paddr, PLIC_ADDR_BASE, PLIC_MMIO_SIZE) ||
           in_range(paddr, DMA_ADDR_BASE, DMA_MMIO_SIZE) ||
           in_range(paddr, OPENSBI_TIMER_BASE, OPENSBI_TIMER_MMIO_SIZE);
  }

  uint32_t read_load(uint32_t paddr, uint8_t func3) const {
    if (memory == nullptr || !is_modeled_mmio(paddr)) {
      return 0;
    }

    if (paddr == OPENSBI_TIMER_LOW_ADDR) {
      return static_cast<uint32_t>(effective_timer_value());
    }
    if (paddr == OPENSBI_TIMER_HIGH_ADDR) {
      return static_cast<uint32_t>(effective_timer_value() >> 32);
    }
    if (paddr == OPENSBI_TIMERCMP_LOW_ADDR) {
      return static_cast<uint32_t>(timer_cmp_);
    }
    if (paddr == OPENSBI_TIMERCMP_HIGH_ADDR) {
      return static_cast<uint32_t>(timer_cmp_ >> 32);
    }
    if (paddr == DMA_SRC_ADDR) {
      return dma_src_;
    }
    if (paddr == DMA_DST_ADDR) {
      return dma_dst_;
    }
    if (paddr == DMA_LEN_ADDR) {
      return dma_len_;
    }
    if (paddr == DMA_CTRL_ADDR) {
      return dma_ctrl_;
    }
    if (paddr == DMA_STATUS_ADDR) {
      return dma_status_;
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
    if (csr_interrupt_inject == nullptr || memory == nullptr ||
        !is_modeled_mmio(paddr)) {
      return;
    }

    if (paddr == OPENSBI_TIMER_LOW_ADDR || paddr == OPENSBI_TIMER_HIGH_ADDR) {
      const uint64_t current_timer = effective_timer_value();
      uint64_t new_timer = current_timer;
      if (paddr == OPENSBI_TIMER_LOW_ADDR) {
        new_timer = (current_timer & 0xFFFFFFFF00000000ull) |
                    static_cast<uint64_t>(data);
      } else {
        new_timer = (static_cast<uint64_t>(data) << 32) |
                    (current_timer & 0x00000000FFFFFFFFull);
      }
      timer_offset_ = new_timer - current_sim_time_u64();
      update_timer_irq();
      return;
    }

    if (paddr == OPENSBI_TIMERCMP_LOW_ADDR) {
      timer_cmp_ =
          (timer_cmp_ & 0xFFFFFFFF00000000ull) | static_cast<uint64_t>(data);
      update_timer_irq();
      return;
    }

    if (paddr == OPENSBI_TIMERCMP_HIGH_ADDR) {
      timer_cmp_ = (static_cast<uint64_t>(data) << 32) |
                   (timer_cmp_ & 0x00000000FFFFFFFFull);
      update_timer_irq();
      return;
    }

    if (paddr == DMA_SRC_ADDR) {
      dma_src_ = data;
      return;
    }
    if (paddr == DMA_DST_ADDR) {
      dma_dst_ = data;
      return;
    }
    if (paddr == DMA_LEN_ADDR) {
      dma_len_ = data;
      return;
    }
    if (paddr == DMA_CTRL_ADDR) {
      dma_ctrl_ = data & kDmaCtrlIrqEn;
      if (data & kDmaCtrlStart) {
        start_dma_transfer();
      }
      return;
    }
    if (paddr == DMA_STATUS_ADDR) {
      dma_status_ &= ~data;
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
        pmem_write(UART_ADDR_BASE, pmem_read(UART_ADDR_BASE) & 0xFFF0FFFFu);
        uart_irq_pending_ = true;
        update_external_irq();
      } else if (cmd == 5u) {
        pmem_write(UART_ADDR_BASE,
                   (pmem_read(UART_ADDR_BASE) & 0xFFF0FFFFu) | 0x00030000u);
      }
      return;
    }

    if (paddr == PLIC_CLAIM_ADDR) {
      const uint8_t claim = static_cast<uint8_t>(store_data & 0xFFu);
      if (claim == UART_PLIC_IRQ_ID) {
        uart_irq_pending_ = false;
      } else if (claim == DMA_PLIC_IRQ_ID) {
        dma_irq_pending_ = false;
      }
      update_external_irq();
      return;
    }
  }

  void tick() {
    update_timer_irq();
    update_external_irq();
  }
  bool timer_irq_level() const { return effective_timer_value() >= timer_cmp_; }

private:
  static uint64_t current_sim_time_u64() {
    return sim_time >= 0 ? static_cast<uint64_t>(sim_time) : 0ull;
  }

  uint64_t effective_timer_value() const {
    return current_sim_time_u64() + timer_offset_;
  }

  void start_dma_transfer() {
    dma_status_ = kDmaStatusBusy;
    const bool valid_range =
        dma_len_ > 0u && pmem_is_ram_addr(dma_src_, dma_len_) &&
        pmem_is_ram_addr(dma_dst_, dma_len_);
    if (!valid_range) {
      dma_status_ = kDmaStatusError;
      return;
    }

    std::vector<uint8_t> temp(dma_len_);
    pmem_memcpy_from_ram(temp.data(), dma_src_, dma_len_);
    pmem_memcpy_to_ram(dma_dst_, temp.data(), dma_len_);
    if (ram_sync_hook_ != nullptr) {
      ram_sync_hook_(dma_dst_, dma_len_);
    }

    dma_status_ = kDmaStatusDone;
    if ((dma_ctrl_ & kDmaCtrlIrqEn) != 0u) {
      dma_irq_pending_ = true;
      update_external_irq();
    }
  }

  void update_timer_irq() {
    if (csr_interrupt_inject == nullptr) {
      return;
    }
    csr_interrupt_inject->timer_irq_pending_valid = true;
    csr_interrupt_inject->timer_irq_pending =
        effective_timer_value() >= timer_cmp_;
  }

  void update_external_irq() {
    if (csr_interrupt_inject == nullptr) {
      return;
    }
    uint32_t claim_id = 0u;
    if (dma_irq_pending_) {
      claim_id = DMA_PLIC_IRQ_ID;
    } else if (uart_irq_pending_) {
      claim_id = UART_PLIC_IRQ_ID;
    }
    pmem_write(PLIC_CLAIM_ADDR, claim_id);
    csr_interrupt_inject->external_irq_pending_valid = true;
    csr_interrupt_inject->external_irq_pending = claim_id != 0u;
  }

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
