#ifndef GENERIC_TABLE_H
#define GENERIC_TABLE_H

#include "base_types.h"
#include <array>
#include <cstdint>

struct RegfileTablePolicy {};
struct SramTablePolicy {};

struct GenericTableTimingConfig {
  uint32_t fixed_latency = 1;
  bool random_delay = false;
  uint32_t random_min = 1;
  uint32_t random_max = 1;
};

template <int Chunks, int ChunkBits> struct GenericTablePayload {
  std::array<wire<ChunkBits>, Chunks> chunks{};
};

template <int AddrBits> struct GenericTableReadReq {
  wire<1> enable = false;
  wire<AddrBits> address = 0;
};

template <int Chunks, int ChunkBits> struct GenericTableReadResp {
  wire<1> valid = false;
  GenericTablePayload<Chunks, ChunkBits> payload{};
};

template <int AddrBits, int Chunks, int ChunkBits> struct GenericTableWriteReq {
  wire<1> enable = false;
  wire<AddrBits> address = 0;
  GenericTablePayload<Chunks, ChunkBits> payload{};
  std::array<wire<1>, Chunks> chunk_enable{};
};

template <int Rows, int Chunks, int ChunkBits, typename Policy> class GenericTable;

template <int Rows, int Chunks, int ChunkBits>
class GenericTable<Rows, Chunks, ChunkBits, RegfileTablePolicy> {
public:
  static constexpr int kAddrBits = (Rows <= 1) ? 1 : __builtin_ctz(Rows);
  using ReadReq = GenericTableReadReq<kAddrBits>;
  using ReadResp = GenericTableReadResp<Chunks, ChunkBits>;
  using WriteReq = GenericTableWriteReq<kAddrBits, Chunks, ChunkBits>;
  using Payload = GenericTablePayload<Chunks, ChunkBits>;

  explicit GenericTable(const GenericTableTimingConfig & = {}) {}

  void reset() {
    for (auto &row : storage_) {
      for (auto &chunk : row.chunks) {
        chunk = 0;
      }
    }
  }

  void comb(const ReadReq &req, ReadResp &resp) const {
    resp = {};
    if (!req.enable) {
      return;
    }
    resp.valid = true;
    resp.payload = storage_[req.address];
  }

  void seq(const ReadReq &, const WriteReq &write_req) {
    if (!write_req.enable) {
      return;
    }
    for (int chunk = 0; chunk < Chunks; ++chunk) {
      if (write_req.chunk_enable[chunk]) {
        storage_[write_req.address].chunks[chunk] =
            write_req.payload.chunks[chunk];
      }
    }
  }

  const Payload &peek_row(uint32_t address) const { return storage_[address]; }

private:
  std::array<Payload, Rows> storage_{};
};

template <int Rows, int Chunks, int ChunkBits>
class GenericTable<Rows, Chunks, ChunkBits, SramTablePolicy> {
public:
  static constexpr int kAddrBits = (Rows <= 1) ? 1 : __builtin_ctz(Rows);
  using ReadReq = GenericTableReadReq<kAddrBits>;
  using ReadResp = GenericTableReadResp<Chunks, ChunkBits>;
  using WriteReq = GenericTableWriteReq<kAddrBits, Chunks, ChunkBits>;
  using Payload = GenericTablePayload<Chunks, ChunkBits>;

  explicit GenericTable(const GenericTableTimingConfig &config = {})
      : config_(config) {}

  void set_config(const GenericTableTimingConfig &config) { config_ = config; }

  void reset() {
    pending_valid_ = false;
    pending_addr_ = 0;
    delay_left_ = 0;
    seed_ = 1;
    for (auto &row : storage_) {
      for (auto &chunk : row.chunks) {
        chunk = 0;
      }
    }
  }

  void comb(const ReadReq &, ReadResp &resp) const {
    resp = {};
    if (pending_valid_ && delay_left_ == 0) {
      resp.valid = true;
      resp.payload = storage_[pending_addr_];
    }
  }

  void seq(const ReadReq &read_req, const WriteReq &write_req) {
    if (write_req.enable) {
      for (int chunk = 0; chunk < Chunks; ++chunk) {
        if (write_req.chunk_enable[chunk]) {
          storage_[write_req.address].chunks[chunk] =
              write_req.payload.chunks[chunk];
        }
      }
    }

    if (read_req.enable && !pending_valid_) {
      pending_valid_ = true;
      pending_addr_ = read_req.address;
      delay_left_ = choose_latency() - 1u;
    } else if (!read_req.enable && pending_valid_ && delay_left_ == 0) {
      pending_valid_ = false;
      pending_addr_ = 0;
      delay_left_ = 0;
    } else if (pending_valid_ && delay_left_ > 0) {
      delay_left_--;
    }
  }

  const Payload &peek_row(uint32_t address) const { return storage_[address]; }

private:
  std::array<Payload, Rows> storage_{};
  bool pending_valid_ = false;
  uint32_t pending_addr_ = 0;
  uint32_t delay_left_ = 0;
  uint32_t seed_ = 1;
  GenericTableTimingConfig config_{};

  static uint32_t clamp_latency(uint32_t value) { return value < 1u ? 1u : value; }

  static uint32_t advance_seed(uint32_t x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
  }

  uint32_t choose_latency() {
    uint32_t latency = clamp_latency(config_.fixed_latency);
    if (config_.random_delay) {
      uint32_t min_lat = clamp_latency(config_.random_min);
      uint32_t max_lat = config_.random_max;
      if (max_lat < min_lat) {
        max_lat = min_lat;
      }
      seed_ = advance_seed(seed_);
      latency = min_lat + (seed_ % (max_lat - min_lat + 1u));
    }
    return clamp_latency(latency);
  }
};

#endif
