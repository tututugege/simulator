#include "PhysMemory.h"
#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

uint32_t *p_memory = nullptr;

namespace {
std::unordered_map<uint32_t, uint32_t> g_io_words;

[[noreturn]] void pmem_fatal(const char *op, uint32_t addr, uint64_t size) {
  std::fprintf(stderr,
               "[PhysMemory] %s failed: addr=0x%08x size=%llu\n", op, addr,
               static_cast<unsigned long long>(size));
  std::abort();
}

inline void pmem_require_ready(const char *op) {
  if (p_memory != nullptr) {
    return;
  }
  std::fprintf(stderr,
               "[PhysMemory] %s failed: RAM backend not initialized\n", op);
  std::abort();
}
} // namespace

bool pmem_init() {
  if (p_memory != nullptr) {
    pmem_clear_all();
    return true;
  }
  p_memory =
      static_cast<uint32_t *>(std::calloc(PHYSICAL_MEMORY_LENGTH, sizeof(uint32_t)));
  if (p_memory == nullptr) {
    return false;
  }
  g_io_words.clear();
  return true;
}

void pmem_release() {
  if (p_memory != nullptr) {
    std::free(p_memory);
    p_memory = nullptr;
  }
  g_io_words.clear();
}

void pmem_clear_all() {
  if (p_memory != nullptr) {
    std::memset(p_memory, 0, RAM_SIZE);
  }
  g_io_words.clear();
}

bool pmem_is_ram_addr(uint32_t paddr, uint32_t size) {
  if (size == 0 || paddr < PMEM_RAM_BASE) {
    return false;
  }
  const uint64_t end =
      static_cast<uint64_t>(paddr) + static_cast<uint64_t>(size) - 1u;
  return end < static_cast<uint64_t>(PMEM_RAM_BASE) + RAM_SIZE;
}

uint32_t pmem_read(uint32_t paddr) {
  pmem_require_ready("read");
  const uint32_t word_addr = paddr & ~0x3u;
  if (pmem_is_ram_addr(word_addr, 4u)) {
    return p_memory[(word_addr - PMEM_RAM_BASE) >> 2];
  }
  auto it = g_io_words.find(word_addr);
  return (it == g_io_words.end()) ? 0u : it->second;
}

void pmem_write(uint32_t paddr, uint32_t data) {
  pmem_require_ready("write");
  const uint32_t word_addr = paddr & ~0x3u;
  if (pmem_is_ram_addr(word_addr, 4u)) {
    p_memory[(word_addr - PMEM_RAM_BASE) >> 2] = data;
    return;
  }
  if (data == 0u) {
    g_io_words.erase(word_addr);
  } else {
    g_io_words[word_addr] = data;
  }
}

void pmem_memcpy_to_ram(uint32_t ram_paddr, const void *src, size_t len) {
  pmem_require_ready("memcpy_to_ram");
  if (len == 0) {
    return;
  }
  if (len > RAM_SIZE) {
    pmem_fatal("memcpy_to_ram", ram_paddr, len);
  }
  if (!pmem_is_ram_addr(ram_paddr, static_cast<uint32_t>(len))) {
    pmem_fatal("memcpy_to_ram", ram_paddr, len);
  }
  const size_t off = static_cast<size_t>(ram_paddr - PMEM_RAM_BASE);
  std::memcpy(reinterpret_cast<uint8_t *>(p_memory) + off, src, len);
}

void pmem_memcpy_from_ram(void *dst, uint32_t ram_paddr, size_t len) {
  pmem_require_ready("memcpy_from_ram");
  if (len == 0) {
    return;
  }
  if (len > RAM_SIZE) {
    pmem_fatal("memcpy_from_ram", ram_paddr, len);
  }
  if (!pmem_is_ram_addr(ram_paddr, static_cast<uint32_t>(len))) {
    pmem_fatal("memcpy_from_ram", ram_paddr, len);
  }
  const size_t off = static_cast<size_t>(ram_paddr - PMEM_RAM_BASE);
  std::memcpy(dst, reinterpret_cast<const uint8_t *>(p_memory) + off, len);
}

uint32_t *pmem_ram_ptr() {
  return p_memory;
}
