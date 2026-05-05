#pragma once

#include "IO.h"
#include "MSHR.h"
#include "MemPtwBlock.h"
#include "PeripheralAxi.h"
#include "PeripheralModel.h"
#include "MemRouteBlock.h"
#include "PtwMemPort.h"
#include "PtwWalkPort.h"
#include "RealDcache.h"
#include "WriteBuffer.h"
#include <array>
#include <cstdint>
#include <memory>
#include <FrontTop.h>


class SimContext;
class Csr;
class MemSubsystemPtwMemPortAdapter;
class MemSubsystemPtwWalkPortAdapter;
class RealLsu;
struct AxiKitRuntime;
namespace axi_interconnect {
struct ReadMasterPort_t;
struct AXI_LLCConfig;
struct AXI_LLC_LookupIn_t;
struct AXI_LLC_TableOut_t;
struct AXI_LLCPerfCounters_t;
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

  ICacheMemPortResp *icache_resp = nullptr;
  ICacheMemPortReq *icache_req = nullptr;

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
  PeripheralReqIO *peripheral_req = nullptr;
  PeripheralRespIO *peripheral_resp = nullptr;
  Csr         *csr            = nullptr;
  uint32_t    *memory         = nullptr;

  void init();
  void comb();
  void seq();
  void on_commit_store(uint32_t paddr, uint32_t data, uint8_t func3);
  void sync_mmio_devices_from_backing();
  void dump_debug_state(FILE *out) const;
  axi_interconnect::ReadMasterPort_t *icache_read_port();
  void set_internal_axi_runtime_active(bool active);
  void set_ptw_coherent_source(RealLsu *lsu) { ptw_coherent_source_ = lsu; }
  void set_llc_config(const axi_interconnect::AXI_LLCConfig &cfg);
  void llc_comb_outputs();
  const axi_interconnect::AXI_LLC_LookupIn_t &llc_lookup_in() const;
  void llc_seq(const axi_interconnect::AXI_LLC_TableOut_t &table_out,
               const axi_interconnect::AXI_LLCPerfCounters_t &perf);

  RealDcache  &get_dcache()  { return dcache_; }
  MSHR        &get_mshr()    { return mshr_; }
  WriteBuffer &get_wb()      { return wb_; }
  PeripheralAxi &get_peripheral_axi() { return peripheral_axi_; }
  const PeripheralAxi &get_peripheral_axi() const { return peripheral_axi_; }

private:
  SimContext *ctx;

  // Sub-modules
  RealDcache  dcache_;
  MSHR          mshr_;
  WriteBuffer   wb_;
  PeripheralAxi peripheral_axi_;
  PeripheralModel peripheral_model_;
  MemPtwBlock   ptw_block;
  MemRouteBlock mem_route_block;
  LsuDcacheIO dcache_req_mux_{};
  DcacheLsuIO dcache_resp_raw_{};

  MSHRDcacheIO mshr_dcache_io_{};
  DcacheMSHRIO dcache_mshr_io_{};

  WBDcacheIO wb_dcache_io_{};
  DcacheWBIO dcache_wb_io_{};

  DcacheLineReadResp dcache_line_read_resp_[LSU_LDU_COUNT + LSU_STA_COUNT]{};
  DcacheLineReadReq dcache_line_read_req_[LSU_LDU_COUNT + LSU_STA_COUNT]{};
  PendingWrite pending_writes_[LSU_LDU_COUNT + LSU_STA_COUNT]{};
  LruUpdate lru_updates_[LSU_LDU_COUNT + LSU_STA_COUNT]{};
  FILLWrite fill_writes_{};
  FillIn fill_in_{};
  FillOut fill_out_{};

  PtwReq ptw_walk_req;
  PtwReq ptw_dtlb_req;
  PtwReq ptw_itlb_req;

  PtwGrant ptw_grant;
  PtwEvent ptw_events;

  ReplayWakeup wakeup;

  std::unique_ptr<AxiKitRuntime> axi_kit_runtime;
  bool internal_axi_runtime_active_ = true;

  void sync_ptw_port_outputs();

  friend class MemSubsystemPtwMemPortAdapter;
  friend class MemSubsystemPtwWalkPortAdapter;

  std::unique_ptr<MemSubsystemPtwMemPortAdapter>  dtlb_ptw_port_inst;
  std::unique_ptr<MemSubsystemPtwMemPortAdapter>  itlb_ptw_port_inst;
  std::unique_ptr<MemSubsystemPtwWalkPortAdapter> dtlb_walk_port_inst;
  std::unique_ptr<MemSubsystemPtwWalkPortAdapter> itlb_walk_port_inst;

  struct LlcPerfShadow {
    uint64_t read_access = 0;
    uint64_t read_hit = 0;
    uint64_t read_miss = 0;
    uint64_t read_access_icache = 0;
    uint64_t read_hit_icache = 0;
    uint64_t read_miss_icache = 0;
    uint64_t read_access_dcache = 0;
    uint64_t read_hit_dcache = 0;
    uint64_t read_miss_dcache = 0;
    uint64_t bypass_read = 0;
    uint64_t write_passthrough = 0;
    uint64_t refill = 0;
    uint64_t mshr_alloc = 0;
    uint64_t mshr_merge = 0;
    uint64_t prefetch_issue = 0;
    uint64_t prefetch_hit = 0;
    uint64_t prefetch_drop_inflight = 0;
    uint64_t prefetch_drop_mshr_full = 0;
    uint64_t prefetch_drop_queue_full = 0;
    uint64_t prefetch_drop_table_hit = 0;
    uint64_t ddr_read_total_cycles = 0;
    uint64_t ddr_read_samples = 0;
    uint64_t ddr_write_total_cycles = 0;
    uint64_t ddr_write_samples = 0;
  };
  void sync_llc_perf();
  LlcPerfShadow llc_perf_shadow_{};
  bool llc_perf_shadow_valid_ = false;
  RealLsu *ptw_coherent_source_ = nullptr;
};
