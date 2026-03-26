#ifndef GENERIC_TABLE_H
#define GENERIC_TABLE_H

#include "base_types.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

struct RegfileTablePolicy {};
struct SramTablePolicy {};

struct GenericTableTimingConfig {
  uint32_t fixed_latency = 1;
  bool random_delay = false;
  uint32_t random_min = 1;
  uint32_t random_max = 1;
};

struct DynamicTableConfig {
  uint32_t rows = 0;
  uint32_t chunks = 0;
  uint32_t chunk_bits = 0;
  GenericTableTimingConfig timing{};
};

struct DynamicTablePayload {
  std::vector<uint8_t> bytes{};

  void reset(size_t size) { bytes.assign(size, 0); }
  size_t size() const { return bytes.size(); }
  bool empty() const { return bytes.empty(); }
  uint8_t *data() { return bytes.data(); }
  const uint8_t *data() const { return bytes.data(); }
};

struct DynamicTableReadReq {
  bool enable = false;
  uint32_t address = 0;
};

struct DynamicTableReadResp {
  bool valid = false;
  DynamicTablePayload payload{};
};

struct DynamicTableWriteReq {
  bool enable = false;
  uint32_t address = 0;
  DynamicTablePayload payload{};
  std::vector<uint8_t> chunk_enable{};
};

namespace generic_table_detail {

inline uint32_t clamp_latency(uint32_t value) { return value < 1u ? 1u : value; }

inline uint32_t advance_seed(uint32_t x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

class DynamicStorage {
public:
  void configure(const DynamicTableConfig &config) {
    config_ = config;
    storage_.assign(total_storage_bytes(), 0);
  }

  void reset_contents() { std::fill(storage_.begin(), storage_.end(), 0); }

  const DynamicTableConfig &config() const { return config_; }
  uint32_t chunk_bytes() const { return (config_.chunk_bits + 7u) / 8u; }
  uint32_t row_bytes() const { return config_.chunks * chunk_bytes(); }
  size_t total_storage_bytes() const {
    return static_cast<size_t>(config_.rows) * static_cast<size_t>(row_bytes());
  }

  bool valid_address(uint32_t address) const { return address < config_.rows; }
  size_t payload_bytes() const { return static_cast<size_t>(row_bytes()); }

  void copy_row_to_payload(uint32_t address, DynamicTablePayload &payload) const {
    payload.reset(payload_bytes());
    if (!valid_address(address) || payload_bytes() == 0) {
      return;
    }
    std::memcpy(payload.data(), row_ptr(address), payload_bytes());
  }

  void write_chunks(uint32_t address, const DynamicTableWriteReq &write_req) {
    if (!write_req.enable || !valid_address(address)) {
      return;
    }
    const uint32_t cbytes = chunk_bytes();
    if (cbytes == 0 || config_.chunks == 0) {
      return;
    }
    if (write_req.payload.size() < payload_bytes()) {
      return;
    }
    if (write_req.chunk_enable.size() < config_.chunks) {
      return;
    }
    uint8_t *dst = row_ptr(address);
    const uint8_t *src = write_req.payload.data();
    for (uint32_t chunk = 0; chunk < config_.chunks; ++chunk) {
      if (!write_req.chunk_enable[chunk]) {
        continue;
      }
      std::memcpy(dst + static_cast<size_t>(chunk) * cbytes,
                  src + static_cast<size_t>(chunk) * cbytes, cbytes);
    }
  }

private:
  uint8_t *row_ptr(uint32_t address) {
    return storage_.data() + static_cast<size_t>(address) * row_bytes();
  }
  const uint8_t *row_ptr(uint32_t address) const {
    return storage_.data() + static_cast<size_t>(address) * row_bytes();
  }

  DynamicTableConfig config_{};
  std::vector<uint8_t> storage_{};
};

} // namespace generic_table_detail

template <typename Policy> class DynamicGenericTable;

template <> class DynamicGenericTable<RegfileTablePolicy> {
public:
  explicit DynamicGenericTable(const DynamicTableConfig &config = {}) {
    configure(config);
  }

  void configure(const DynamicTableConfig &config) { storage_.configure(config); }
  void set_config(const DynamicTableConfig &config) { configure(config); }

  void reset() { storage_.reset_contents(); }

  void comb(const DynamicTableReadReq &req, DynamicTableReadResp &resp) const {
    resp.valid = false;
    resp.payload.reset(storage_.payload_bytes());
    if (!req.enable || !storage_.valid_address(req.address)) {
      return;
    }
    resp.valid = true;
    storage_.copy_row_to_payload(req.address, resp.payload);
  }

  void seq(const DynamicTableReadReq &, const DynamicTableWriteReq &write_req) {
    storage_.write_chunks(write_req.address, write_req);
  }

  bool debug_read_row(uint32_t address, DynamicTablePayload &payload) const {
    payload.reset(storage_.payload_bytes());
    if (!storage_.valid_address(address)) {
      return false;
    }
    storage_.copy_row_to_payload(address, payload);
    return true;
  }

  size_t payload_bytes() const { return storage_.payload_bytes(); }
  uint32_t row_bytes() const { return storage_.row_bytes(); }
  uint32_t chunk_bytes() const { return storage_.chunk_bytes(); }
  const DynamicTableConfig &config() const { return storage_.config(); }

private:
  generic_table_detail::DynamicStorage storage_{};
};

template <> class DynamicGenericTable<SramTablePolicy> {
public:
  explicit DynamicGenericTable(const DynamicTableConfig &config = {}) {
    configure(config);
  }

  void configure(const DynamicTableConfig &config) {
    storage_.configure(config);
    timing_ = config.timing;
  }

  void set_config(const DynamicTableConfig &config) { configure(config); }
  void set_timing(const GenericTableTimingConfig &timing) { timing_ = timing; }

  void reset() {
    pending_valid_ = false;
    pending_addr_ = 0;
    delay_left_ = 0;
    seed_ = 1;
    storage_.reset_contents();
  }

  void comb(const DynamicTableReadReq &, DynamicTableReadResp &resp) const {
    resp.valid = false;
    resp.payload.reset(storage_.payload_bytes());
    if (!pending_valid_ || delay_left_ != 0 ||
        !storage_.valid_address(pending_addr_)) {
      return;
    }
    resp.valid = true;
    storage_.copy_row_to_payload(pending_addr_, resp.payload);
  }

  void seq(const DynamicTableReadReq &read_req,
           const DynamicTableWriteReq &write_req) {
    storage_.write_chunks(write_req.address, write_req);

    if (read_req.enable && !pending_valid_ &&
        storage_.valid_address(read_req.address)) {
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

  bool debug_read_row(uint32_t address, DynamicTablePayload &payload) const {
    payload.reset(storage_.payload_bytes());
    if (!storage_.valid_address(address)) {
      return false;
    }
    storage_.copy_row_to_payload(address, payload);
    return true;
  }

  size_t payload_bytes() const { return storage_.payload_bytes(); }
  uint32_t row_bytes() const { return storage_.row_bytes(); }
  uint32_t chunk_bytes() const { return storage_.chunk_bytes(); }
  const DynamicTableConfig &config() const { return storage_.config(); }

private:
  uint32_t choose_latency() {
    uint32_t latency = generic_table_detail::clamp_latency(timing_.fixed_latency);
    if (timing_.random_delay) {
      uint32_t min_lat = generic_table_detail::clamp_latency(timing_.random_min);
      uint32_t max_lat = timing_.random_max;
      if (max_lat < min_lat) {
        max_lat = min_lat;
      }
      seed_ = generic_table_detail::advance_seed(seed_);
      latency = min_lat + (seed_ % (max_lat - min_lat + 1u));
    }
    return generic_table_detail::clamp_latency(latency);
  }

  generic_table_detail::DynamicStorage storage_{};
  GenericTableTimingConfig timing_{};
  bool pending_valid_ = false;
  uint32_t pending_addr_ = 0;
  uint32_t delay_left_ = 0;
  uint32_t seed_ = 1;
};

template <int Chunks, int ChunkBits> struct GenericTablePayload {
  static constexpr int kWordBits = 32;
  static constexpr int kWordsPerChunk =
      (ChunkBits + kWordBits - 1) / kWordBits;
  std::array<std::array<uint32_t, kWordsPerChunk>, Chunks> chunks{};
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
        chunk.fill(0);
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
        chunk.fill(0);
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
