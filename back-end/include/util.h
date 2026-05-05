#pragma once
#include "types.h"
#include <cstdio>

#define LOOP_INC(idx, length) idx = (idx + 1) % (length)
#define LOOP_DEC(idx, length) idx = (idx + (length) - 1) % (length)
extern long long sim_time;

// Unified backend/memory logging helpers.
// Prefer these macros over scattered direct domain checks.
#define BE_LOG(fmt, ...)                                                       \
  do {                                                                         \
    if (BACKEND_LOG) {                                                         \
      std::printf("[BE][t=%lld] " fmt "\n", (long long)sim_time,              \
                  ##__VA_ARGS__);                                              \
    }                                                                          \
  } while (0)

#define MEM_LOGF(fmt, ...)                                                     \
  do {                                                                         \
    if (MEM_LOG) {                                                             \
      std::printf("[MEM][t=%lld] " fmt "\n", (long long)sim_time,             \
                  ##__VA_ARGS__);                                              \
    }                                                                          \
  } while (0)

#define DCACHE_LOGF(fmt, ...)                                                  \
  do {                                                                         \
    if (DCACHE_LOG) {                                                          \
      std::printf("[DCACHE][t=%lld] " fmt "\n", (long long)sim_time,          \
                  ##__VA_ARGS__);                                              \
    }                                                                          \
  } while (0)

#define MMU_LOGF(fmt, ...)                                                     \
  do {                                                                         \
    if (MMU_LOG) {                                                             \
      std::printf("[MMU][t=%lld] " fmt "\n", (long long)sim_time,             \
                  ##__VA_ARGS__);                                              \
    }                                                                          \
  } while (0)

// Custom Assert Macro to avoid WSL2 issues.
// Respect NDEBUG so release builds do not pay hot-path assertion cost.
#ifdef NDEBUG
#define Assert(cond)                                                           \
  do {                                                                         \
    (void)sizeof(cond);                                                        \
  } while (0)
#else
#define Assert(cond)                                                           \
  do {                                                                         \
    if (__builtin_expect(!(cond), 0)) {                                        \
      printf("\033[1;31mAssertion failed: %s, file %s, line %d, cycle "        \
             "%lld\033[0m\n",                                                  \
             #cond, __FILE__, __LINE__, sim_time);                             \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#endif

inline int get_rob_line(uint32_t rob_idx) { return rob_idx / ROB_BANK_NUM; }
inline int get_rob_bank(uint32_t rob_idx) { return rob_idx % ROB_BANK_NUM; }
inline uint32_t make_rob_idx(uint32_t line, uint32_t bank) {
  return (line * ROB_BANK_NUM) | bank;
}

inline uint8_t rob_cplt_popcount(wire<ROB_CPLT_MASK_WIDTH> mask) {
  return static_cast<uint8_t>(__builtin_popcount(static_cast<unsigned>(mask)));
}

inline wire<ROB_CPLT_MASK_WIDTH> rob_cplt_mask_from_iq(int iq_id) {
  switch (iq_id) {
  case IQ_INT:
  case IQ_LD:
    return ROB_CPLT_G0;
  case IQ_BR:
  case IQ_STA:
    return ROB_CPLT_G1;
  case IQ_STD:
    return ROB_CPLT_G2;
  default:
    Assert(0 && "Unknown IQ id for ROB completion mask");
    return 0;
  }
}

inline wire<ROB_CPLT_MASK_WIDTH> rob_cplt_mask_from_issue_port(int port_idx) {
  if (port_idx >= IQ_ALU_PORT_BASE && port_idx < IQ_ALU_PORT_BASE + ALU_NUM) {
    return ROB_CPLT_G0;
  }
  if (port_idx >= IQ_LD_PORT_BASE &&
      port_idx < IQ_LD_PORT_BASE + LSU_LOAD_WB_WIDTH) {
    return ROB_CPLT_G0;
  }
  if (port_idx >= IQ_BR_PORT_BASE && port_idx < IQ_BR_PORT_BASE + BRU_NUM) {
    return ROB_CPLT_G1;
  }
  if (port_idx >= IQ_STA_PORT_BASE &&
      port_idx < IQ_STA_PORT_BASE + LSU_STA_COUNT) {
    return ROB_CPLT_G1;
  }
  if (port_idx >= IQ_STD_PORT_BASE &&
      port_idx < IQ_STD_PORT_BASE + GLOBAL_IQ_CONFIG[IQ_STD].port_num) {
    return ROB_CPLT_G2;
  }
  Assert(0 && "Unknown issue port for ROB completion mask");
  return 0;
}

inline bool is_branch(InstType type) { return type == BR || type == JALR; }

constexpr wire<INST_TYPE_WIDTH> encode_inst_type(InstType type) {
  return static_cast<wire<INST_TYPE_WIDTH>>(type);
}

inline InstType decode_inst_type(wire<INST_TYPE_WIDTH> bits) {
  Assert((uint32_t)bits < INST_TYPE_COUNT);
  return static_cast<InstType>((uint32_t)bits);
}

constexpr wire<UOP_TYPE_WIDTH> encode_uop_type(UopType op) {
  return static_cast<wire<UOP_TYPE_WIDTH>>(op);
}

inline UopType decode_uop_type(wire<UOP_TYPE_WIDTH> bits) {
  Assert((uint32_t)bits < MAX_UOP_TYPE);
  return static_cast<UopType>((uint32_t)bits);
}

inline bool is_store(InstInfo uop) {
  return uop.type == STORE ||
         (uop.type == AMO && (uop.func7 >> 2) != AmoOp::LR);
}

inline bool is_store(MicroOp uop) {
  return uop.op == UOP_STA || uop.op == UOP_STD;
}

inline bool is_load(InstInfo uop) {
  return uop.type == LOAD || (uop.type == AMO && (uop.func7 >> 2) != AmoOp::SC);
}

inline bool is_load(MicroOp uop) { return uop.op == UOP_LOAD; }

inline bool is_amo_sc_inst(InstType type,uint32_t func7) {
  return type == AMO && ((func7 >> 2) == AmoOp::SC);
}
inline bool is_amo_lr_inst(InstType type,uint32_t func7) {
  return type == AMO && ((func7 >> 2) == AmoOp::LR);
}
static inline bool is_amo_sc_uop(const MicroOp &uop) {
  return uop.is_atomic && ((uop.func7 >> 2) == AmoOp::SC);
}
static inline bool is_amo_lr_uop(const MicroOp &uop) {
  return uop.is_atomic && ((uop.func7 >> 2) == AmoOp::LR);
}


inline bool is_CSR(InstType type) {
  return (type == CSR || type == MRET || type == ECALL || type == EBREAK);
}
inline bool is_branch(wire<INST_TYPE_WIDTH> type) {
  return is_branch(decode_inst_type(type));
}
inline bool is_CSR(wire<INST_TYPE_WIDTH> type) {
  return is_CSR(decode_inst_type(type));
}

inline bool is_branch_uop(UopType op) { return op == UOP_BR || op == UOP_JUMP; }
inline bool is_branch_uop(wire<UOP_TYPE_WIDTH> op) {
  return is_branch_uop(decode_uop_type(op));
}

inline bool is_CSR_uop(UopType op) {
  return (op == UOP_CSR || op == UOP_MRET || op == UOP_ECALL ||
          op == UOP_EBREAK);
}
inline bool is_CSR_uop(wire<UOP_TYPE_WIDTH> op) {
  return is_CSR_uop(decode_uop_type(op));
}

inline bool cmp_inst_age(InstInfo inst1, InstInfo inst2) {
  if (inst1.rob_flag == inst2.rob_flag) {
    return inst1.rob_idx > inst2.rob_idx;
  } else {
    return inst1.rob_idx < inst2.rob_idx;
  }
}

inline bool cmp_inst_age(MicroOp inst1, MicroOp inst2) {
  if (inst1.rob_flag == inst2.rob_flag) {
    return inst1.rob_idx > inst2.rob_idx;
  } else {
    return inst1.rob_idx < inst2.rob_idx;
  }
}

inline bool is_sta_uop(UopType op) { return op == UOP_STA; }
inline bool is_sta_uop(wire<UOP_TYPE_WIDTH> op) {
  return is_sta_uop(decode_uop_type(op));
}

inline bool is_std_uop(UopType op) { return op == UOP_STD; }
inline bool is_std_uop(wire<UOP_TYPE_WIDTH> op) {
  return is_std_uop(decode_uop_type(op));
}

inline bool is_load_uop(UopType op) { return op == UOP_LOAD; }
inline bool is_load_uop(wire<UOP_TYPE_WIDTH> op) {
  return is_load_uop(decode_uop_type(op));
}

inline bool is_page_fault(InstInfo uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store;
}

inline bool is_page_fault(MicroOp uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store;
}

inline bool is_exception(InstInfo uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store ||
         uop.illegal_inst || uop.type == ECALL;
}

inline bool is_exception(MicroOp uop) {
  return uop.page_fault_inst || uop.page_fault_load || uop.page_fault_store ||
         uop.illegal_inst || is_CSR_uop(uop.op);
}

inline bool is_flush_inst(InstInfo uop) {
  return uop.type == CSR || uop.type == ECALL || uop.type == MRET ||
         uop.type == SRET || uop.type == SFENCE_VMA || uop.type == FENCE_I ||
         is_exception(uop) || uop.type == EBREAK || uop.flush_pipe;
}

inline bool is_flush_inst(MicroOp uop) {
  return is_CSR_uop(uop.op) || uop.op == UOP_FENCE_I || is_exception(uop) ||
         uop.flush_pipe;
}
