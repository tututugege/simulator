#pragma once
#include "AXI_Interconnect.h"
#include "AXI_Router_AXI4.h"
#include "BackTop.h"
#include "FrontTop.h"
#include "MMIO_Bus_AXI4.h"
#include "MemSubsystem.h"
#include "SimDDR.h"
using AxiInterconnectImpl = axi_interconnect::AXI_Interconnect;
using AxiRouterImpl = axi_interconnect::AXI_Router_AXI4;
using AxiDdrImpl = sim_ddr::SimDDR;
using AxiMmioImpl = mmio::MMIO_Bus_AXI4;
#include "UART16550_Device.h"
#include "axi_mmio_map.h"
#include "front_IO.h"

class SimCpu {
  // 性能计数器
public:
  ICacheMemPortReq icache_req{}; // 供 FrontTop 直接驱动的 ICache 请求端口（可选，取决于 CONFIG_ICACHE_USE_AXI_MEM_PORT）
  ICacheMemPortResp icache_resp{}; // 供 FrontTop 直接驱动的 ICache 响应端口（可选，取决于 CONFIG_ICACHE_USE
  SimCpu() : back(&this->ctx), mem_subsystem(&this->ctx) {};
  SimContext ctx;
  BackTop back;
  FrontTop front;
  MemSubsystem mem_subsystem;
  AxiInterconnectImpl axi_interconnect;
  AxiRouterImpl axi_router;
  AxiDdrImpl axi_ddr;
  AxiMmioImpl axi_mmio;
  mmio::UART16550_Device axi_uart{MMIO_RANGE_BASE};
  // Oracle 模式下的一拍保留寄存，避免“后端当拍阻塞”导致前端指令丢失。
  bool oracle_pending_valid = false;
  front_top_out oracle_pending_out = {};

  void init();
  void sync_mmio_devices_from_backing();
  void reinit_frontend_after_restore();
  void restore_pc(uint32_t pc);
  void cycle();
  void front_cycle();
  void back2front_comb();
  bool ready_to_exit() const;
  uint32_t get_reg(uint8_t arch_idx) { return back.get_reg(arch_idx); }
  // 由 SimContext 在提交路径调用的本地辅助逻辑。
  void commit_sync(InstInfo *inst);
  void difftest_prepare(InstEntry *inst_entry, bool *skip);
};
