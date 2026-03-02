#pragma once

#include "AbstractDcache.h"
#include "IO.h"
#include "MemPtwBlock.h"
#include "MemReadArbBlock.h"
#include "MemRespRouteBlock.h"
#include "PeripheralModel.h"
#include "PtwMemPort.h"
#include "PtwWalkPort.h"
#include <array>
#include <memory>

class SimContext;
class MemSubsystemPtwMemPortAdapter;
class MemSubsystemPtwWalkPortAdapter;
struct AxiMemBackend;
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

  // External ports (current stage: LSU <-> DCache)
  MemReqIO *lsu_req_io = nullptr;
  MemReqIO *lsu_wreq_io = nullptr;
  MemRespIO *lsu_resp_io = nullptr;
  MemReadyIO *lsu_wready_io = nullptr;

  // PTW 端口连线（对外直接赋值/读取，避免 getter 包装）
  PtwMemPort *dtlb_ptw_port = nullptr;
  PtwMemPort *itlb_ptw_port = nullptr;
  PtwWalkPort *dtlb_walk_port = nullptr;
  PtwWalkPort *itlb_walk_port = nullptr;
  Csr *csr = nullptr;
  uint32_t *memory = nullptr;

  void init();
  void comb();
  void seq();
  void on_commit_store(uint32_t paddr, uint32_t data, uint8_t func3);
  axi_interconnect::ReadMasterPort_t *icache_read_port();

private:
  SimContext *ctx;
  std::unique_ptr<AbstractDcache> dcache;
  std::unique_ptr<AxiMemBackend> axi_backend;
  PeripheralModel peripheral;
  MemPtwBlock ptw_block;
  MemReadArbBlock read_arb_block;
  MemRespRouteBlock resp_route_block;

  // Internal ports: MemSubsystem arbitrates LSU/PTW then drives DCache.
  MemReqIO dcache_req_mux;
  MemReqIO dcache_wreq_mux;
  MemRespIO dcache_resp_raw;
  MemReadyIO dcache_wready_raw;

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

  // PTW 对外端口显式 IO（定义在 IO.h）。
  std::array<PtwMemRespIO, kPtwClientCount> ptw_mem_resp_ios{};
  std::array<PtwWalkRespIO, kPtwClientCount> ptw_walk_resp_ios{};

  friend class MemSubsystemPtwMemPortAdapter;
  friend class MemSubsystemPtwWalkPortAdapter;

  // Dedicated PTW client ports for DTLB/ITLB shared PTW access.
  std::unique_ptr<PtwMemPort> dtlb_ptw_port_inst;
  std::unique_ptr<PtwMemPort> itlb_ptw_port_inst;
  std::unique_ptr<PtwWalkPort> dtlb_walk_port_inst;
  std::unique_ptr<PtwWalkPort> itlb_walk_port_inst;
};
