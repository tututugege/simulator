#pragma once

#include "config/frontend_diag_config.h"

#if FRONTEND_ENABLE_HOST_PROFILE

#include <cstdint>

namespace frontend_host_profile {

enum class Slot : uint32_t {
  FrontTop = 0,
  FrontSeqRead,
  FrontComb,
  FrontSeqWrite,
  FrontReadStage,
  FrontBpuStage,
  FrontIcacheStage,
  FrontPredecodeStage,
  FrontF2bStage,
  FrontRefreshStage,
  BpuSeqRead,
  BpuPreReadReq,
  BpuDataSeqRead,
  BpuPostReadReq,
  BpuSubmoduleSeqRead,
  BpuCoreComb,
  BpuTypeComb,
  BpuTageComb,
  BpuBtbComb,
  IcacheComb,
  SimCycle,
  SimCsrStatus,
  SimClearAxiInputs,
  SimFrontCycle,
  SimBackComb,
  SimMemLlcCombOutputs,
  SimAxiOutputs,
  SimBridgeAxiToMem,
  SimMemComb,
  SimBridgeMemToAxi,
  SimAxiInputs,
  SimBack2Front,
  SimBackSeq,
  SimMemSeq,
  SimMemLlcSeq,
  SimAxiSeq,
  Count,
};

void begin_sample(Slot slot, uint64_t &start_ns, bool &active);
void end_sample(Slot slot, uint64_t start_ns, bool active);
void print_summary();

class Scope final {
public:
  explicit Scope(Slot slot) : slot_(slot) {
    begin_sample(slot_, start_ns_, active_);
  }

  ~Scope() { end_sample(slot_, start_ns_, active_); }

private:
  Slot slot_;
  uint64_t start_ns_ = 0;
  bool active_ = false;
};

} // namespace frontend_host_profile

#define FRONTEND_HOST_PROFILE_SCOPE_IMPL(slot_name, line)                      \
  [[maybe_unused]] ::frontend_host_profile::Scope                              \
      frontend_host_profile_scope_##line(::frontend_host_profile::Slot::slot_name)

#define FRONTEND_HOST_PROFILE_SCOPE(slot_name)                                 \
  FRONTEND_HOST_PROFILE_SCOPE_IMPL(slot_name, __LINE__)

#else

namespace frontend_host_profile {
inline void print_summary() {}
} // namespace frontend_host_profile

#define FRONTEND_HOST_PROFILE_SCOPE(slot_name) ((void)0)

#endif
