#pragma once

#include "DcacheConfig.h"
#include "IO.h"
#include <cstdint>
#include <cstring>

class PeripheralModel;

struct PeripheralAxiReadIn {
  bool req_ready = false;
  bool req_accepted = false;
  bool resp_valid = false;
  uint32_t resp_data[DCACHE_WORD_NUM] = {};
  uint8_t resp_id = 0;
};

struct PeripheralAxiReadOut {
  bool req_valid = false;
  uint32_t req_addr = 0;
  uint8_t req_total_size = 0;
  uint8_t req_id = 0;
  bool resp_ready = false;
};

struct PeripheralAxiWriteIn {
  bool req_ready = false;
  bool req_accepted = false;
  bool resp_valid = false;
  uint8_t resp_id = 0;
};

struct PeripheralAxiWriteOut {
  bool req_valid = false;
  uint32_t req_addr = 0;
  uint8_t req_total_size = 0;
  uint8_t req_id = 0;
  uint64_t req_wstrb = 0;
  uint32_t req_wdata[DCACHE_WORD_NUM] = {};
  bool resp_ready = false;
};

struct PeripheralAxiIn {
  PeripheralAxiReadIn read;
  PeripheralAxiWriteIn write;
};

struct PeripheralAxiOut {
  PeripheralAxiReadOut read;
  PeripheralAxiWriteOut write;
};

struct PeripheralAxiState {
  bool busy = false;
  bool write = false;
  bool req_accepted = false;
  bool resp_valid = false;
  uint32_t addr = 0;
  uint32_t wdata = 0;
  uint8_t func3 = 0;
  uint32_t rdata = 0;
  uint8_t req_id = 0;
};

class PeripheralAxi {
public:
  PeripheralReqIO *peripheral_req = nullptr;
  PeripheralRespIO *peripheral_resp = nullptr;
  PeripheralModel *peripheral_model = nullptr;

  void init();
  void comb_outputs();
  void comb_inputs();
  void seq();

  PeripheralAxiIn in;
  PeripheralAxiOut out;
  PeripheralAxiState cur;
  PeripheralAxiState nxt;

private:
  static uint8_t access_size_bytes(uint8_t func3);
  static uint32_t pack_write_data(uint32_t data, uint8_t func3);
  static uint64_t pack_write_strb(uint8_t func3);
  static uint32_t extract_read_data(uint32_t raw, uint8_t func3);
};
