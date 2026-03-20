#pragma once

#include "config.h"
#include <cstdint>
#include <cstring>

#ifndef DCACHE_OFFSET_BITS
#define DCACHE_OFFSET_BITS 6
#endif

#ifndef DCACHE_LINE_BYTES
#define DCACHE_LINE_BYTES 64
#endif

#ifndef DCACHE_LINE_WORDS
#define DCACHE_LINE_WORDS 16
#endif

#ifndef WB_ENTRIES
#define WB_ENTRIES 8
#endif

class SimContext;

struct WriteBufferEntry {
  bool valid = false;
  bool send = false;
  uint32_t addr = 0;
  uint32_t data[DCACHE_LINE_WORDS] = {};
};

extern WriteBufferEntry write_buffer[WB_ENTRIES];
extern WriteBufferEntry write_buffer_nxt[WB_ENTRIES];

struct WBMSHRIO {
  bool ready = false;
};

struct MSHRWBIO {
  bool valid = false;
  uint32_t addr = 0;
  uint32_t data[DCACHE_LINE_WORDS] = {};
};

struct BypassReq {
  bool valid = false;
  uint32_t addr = 0;
};

struct BypassResp {
  bool valid = false;
  uint32_t data = 0;
};

struct MergeReq {
  bool valid = false;
  uint32_t addr = 0;
  uint32_t data = 0;
  uint8_t strb = 0;
};

struct MergeResp {
  bool valid = false;
  bool busy = false;
};

struct DcacheWBIO {
  BypassReq bypass_req[LSU_LDU_COUNT];
  MergeReq merge_req[LSU_STA_COUNT];
};

struct WBDcacheIO {
  BypassResp bypass_resp[LSU_LDU_COUNT];
  MergeResp merge_resp[LSU_STA_COUNT];
};

struct WBState {
  uint32_t count = 0;
  uint32_t head = 0;
  uint32_t tail = 0;
  uint32_t send = 0;

  uint32_t bypassdata[LSU_LDU_COUNT] = {};
  bool bypassvalid[LSU_LDU_COUNT] = {};

  bool mergevalid[LSU_STA_COUNT] = {};
  bool mergebusy[LSU_STA_COUNT] = {};
};

struct WbDeferredCheck {
  bool valid = false;
  uint32_t addr = 0;
  uint32_t data[DCACHE_LINE_WORDS] = {};
};

struct WbIssueTrace {
  bool valid = false;
  uint32_t addr = 0;
  uint32_t data[DCACHE_LINE_WORDS] = {};
  uint64_t issue_cycle = 0;
};

struct WbRespTrace {
  bool valid = false;
  uint32_t addr = 0;
  uint32_t data[DCACHE_LINE_WORDS] = {};
};

struct WbAxiIn {
  bool req_ready = false;
  bool resp_valid = false;
};

struct WbAxiOut {
  bool req_valid = false;
  uint32_t req_addr = 0;
  uint8_t req_total_size = 0;
  uint8_t req_id = 0;
  uint64_t req_wstrb = 0;
  uint32_t req_wdata[DCACHE_LINE_WORDS] = {};
  bool resp_ready = false;
};

struct WBIn {
  MSHRWBIO mshrwb;
  DcacheWBIO dcachewb;
  WbAxiIn axi_in;

  void clear() { *this = {}; }
};

struct WBOut {
  WBMSHRIO wbmshr;
  WBDcacheIO wbdcache;
  WbAxiOut axi_out;

  void clear() { *this = {}; }
};

class WriteBuffer {
public:
  WriteBuffer() = default;
  void bind_context(SimContext *c) { ctx = c; }
  int find_wb_entry(uint32_t addr);
  void init();
  void comb_outputs();
  void comb_inputs();
  void seq();

  WBIn in;
  WBOut out;

  WBState cur, nxt;
  SimContext *ctx = nullptr;
};
