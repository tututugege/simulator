#pragma once

#include <cstdint>

// ============================================================
// 物理内存统一接口
//
// p_memory 在 PhysMemory.cpp 中定义；通过下列接口访问。
// 字级别读写（pmem_read / pmem_write）是推荐接口；
// pmem_ptr() 仅供批量操作（checkpoint 加载、memcpy 等）使用。
// ============================================================

extern uint32_t *p_memory;

/// 读取物理字节地址 paddr 处的 32-bit word。
inline uint32_t pmem_read(uint32_t paddr) {
  return p_memory[paddr >> 2];
}

/// 向物理字节地址 paddr 处写入 32-bit word data。
inline void pmem_write(uint32_t paddr, uint32_t data) {
  p_memory[paddr >> 2] = data;
}

/// 返回底层原始指针，仅供需要连续内存地址的批量操作使用。
inline uint32_t *pmem_ptr() {
  return p_memory;
}
