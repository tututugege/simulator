#pragma once

#include "IO.h"
#include "MSHR.h"
#include "MemPtwBlock.h"
#include "PeripheralAxi.h"
#include "MemReadArbBlock.h"
#include "MemRespRouteBlock.h"
#include "PtwMemPort.h"
#include "PtwWalkPort.h"
#include "RealDcache.h"
using MemDcacheImpl = RealDcache;
#include "WriteBuffer.h"
#include <array>
#include <cstdint>
#include <memory>

class SimContext;
class Csr;
class MemSubsystemPtwMemPortAdapter;
class MemSubsystemPtwWalkPortAdapter;
struct AxiKitRuntime;
namespace axi_interconnect {
struct ReadMasterPort_t;
}
class MemSubsystem {
public:
  enum class PtwClient : uint8_t {
    DTLB = 0,
    ITLB = 1,
    NUM_CLIENTS = 2,
  };

  explicit MemSubsystem(SimContext *ctx);
  ~MemSubsystem();

  // External ports — LSU <-> DCache (RealDcache multi-port interface)
  LsuDcacheIO  *lsu2dcache  = nullptr;  // LSU → DCache requests
  DcacheLsuIO  *dcache2lsu  = nullptr;  // DCache → LSU responses

  // AXI interfaces exposed for the memory interconnect to drive/read each cycle.
  MshrAxiIn   mshr_axi_in{};   // IC read-channel  → MSHR  (set by caller)
  MshrAxiOut  mshr_axi_out{};  // MSHR → IC read-channel   (read by caller)
  WbAxiIn     wb_axi_in{};     // IC write-channel → WB    (set by caller)
  WbAxiOut    wb_axi_out{};    // WB → IC write-channel    (read by caller)
  PeripheralAxiReadIn peripheral_axi_read_in{};
  PeripheralAxiReadOut peripheral_axi_read_out{};
  PeripheralAxiWriteIn peripheral_axi_write_in{};
  PeripheralAxiWriteOut peripheral_axi_write_out{};

  // PTW 端口连线（对外直接赋值/读取）
  PtwMemPort  *dtlb_ptw_port  = nullptr;
  PtwMemPort  *itlb_ptw_port  = nullptr;
  PtwWalkPort *dtlb_walk_port = nullptr;
  PtwWalkPort *itlb_walk_port = nullptr;
  PeripheralIO *peripheral_io = nullptr;
  Csr         *csr            = nullptr;
  uint32_t    *memory         = nullptr;

  void init();
  void comb();
  void seq();
  void dump_debug_state() const;
  void on_commit_store(uint32_t paddr, uint32_t data, uint8_t func3);
  axi_interconnect::ReadMasterPort_t *icache_read_port();

  // Accessors for sub-modules (e.g., for debug/stats).
  MemDcacheImpl  &get_dcache()  { return dcache_; }
  MSHR        &get_mshr()    { return mshr_; }
  WriteBuffer &get_wb()      { return wb_; }
  PeripheralAxi &get_peripheral_axi() { return peripheral_axi_; }
  const PeripheralAxi &get_peripheral_axi() const { return peripheral_axi_; }

private:
  enum class DebugEventKind : uint8_t {
    PTW_MEM_REQ = 0,
    PTW_MEM_GRANT,
    PTW_MEM_RESP,
    PTW_WALK_REQ,
    PTW_WALK_GRANT,
    PTW_WALK_RESP,
    REPLAY_WAKEUP,
    LSU_PORT0_PREEMPT,
    LSU_LOAD_RESP,
    PTW_ROUTE_EVENT,
  };

  struct DebugEvent {
    uint64_t cycle = 0;
    DebugEventKind kind = DebugEventKind::PTW_MEM_REQ;
    uint8_t owner = 0;
    uint8_t port = 0;
    uint8_t replay = 0;
    uint8_t extra = 0;
    uint32_t addr = 0;
    uint32_t data = 0;
    size_t req_id = 0;
  };

  SimContext *ctx;

  // Sub-modules
  MemDcacheImpl dcache_;
  MSHR          mshr_;
  WriteBuffer   wb_;
  PeripheralAxi peripheral_axi_;
  MemPtwBlock   ptw_block;
  MemReadArbBlock read_arb_block;
  MemRespRouteBlock resp_route_block;
  LsuDcacheIO dcache_req_mux_{};
  DcacheLsuIO dcache_resp_raw_{};

  std::unique_ptr<AxiKitRuntime> axi_kit_runtime;

  static constexpr size_t kPtwClientCount =
      static_cast<size_t>(PtwClient::NUM_CLIENTS);
  static size_t ptw_client_idx(PtwClient c) { return static_cast<size_t>(c); }
  static MemPtwBlock::Client to_block_client(PtwClient c);
  void refresh_ptw_client_outputs();
  bool ptw_mem_send_read_req(PtwClient client, uint32_t paddr);
  bool ptw_mem_resp_valid(PtwClient client) const;
  uint32_t ptw_mem_resp_data(PtwClient client) const;
  void ptw_mem_consume_resp(PtwClient client);
  bool ptw_walk_send_req(PtwClient client, const PtwWalkReq &req);
  bool ptw_walk_resp_valid(PtwClient client) const;
  PtwWalkResp ptw_walk_resp(PtwClient client) const;
  void ptw_walk_consume_resp(PtwClient client);
  void ptw_walk_flush(PtwClient client);

  std::array<PtwMemRespIO, kPtwClientCount>  ptw_mem_resp_ios{};
  std::array<PtwWalkRespIO, kPtwClientCount> ptw_walk_resp_ios{};
  static constexpr size_t kDebugEventCapacity = 64;
  std::array<DebugEvent, kDebugEventCapacity> debug_events_{};
  size_t debug_event_head_ = 0;
  size_t debug_event_count_ = 0;

  friend class MemSubsystemPtwMemPortAdapter;
  friend class MemSubsystemPtwWalkPortAdapter;

  std::unique_ptr<PtwMemPort>  dtlb_ptw_port_inst;
  std::unique_ptr<PtwMemPort>  itlb_ptw_port_inst;
  std::unique_ptr<PtwWalkPort> dtlb_walk_port_inst;
  std::unique_ptr<PtwWalkPort> itlb_walk_port_inst;

  void record_debug_event(DebugEventKind kind, uint8_t owner, uint32_t addr,
                          uint32_t data = 0, uint8_t replay = 0,
                          uint8_t extra = 0, size_t req_id = 0,
                          uint8_t port = 0);
  void dump_recent_debug_events() const;
  void dump_failure_analysis() const;
  void dump_key_cache_lines() const;
  void dump_cache_line_for_addr(const char *reason, uint32_t addr) const;
};
