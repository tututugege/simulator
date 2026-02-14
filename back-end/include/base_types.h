#pragma once
#include <cstdint>
#include <type_traits>

// Recursive template to find suitable container type
template <int N>
using AutoType = typename std::conditional<
    N == 1, bool,
    typename std::conditional<
        N <= 8, uint8_t,
        typename std::conditional<
            N <= 16, uint16_t,
            typename std::conditional<
                N <= 32, uint32_t,
                typename std::conditional<N <= 64, uint64_t, uint64_t>::type>::
                type>::type>::type>::type;

template <int N> using wire = AutoType<N>;
template <int N> using reg = AutoType<N>;

enum UopType {
  UOP_JUMP,
  UOP_ADD,
  UOP_BR,
  UOP_LOAD,
  UOP_STA,
  UOP_STD,
  UOP_CSR,
  UOP_ECALL,
  UOP_EBREAK,
  UOP_SFENCE_VMA,
  UOP_FENCE_I,
  UOP_MRET,
  UOP_SRET,
  UOP_MUL,
  UOP_DIV,
  UOP_WFI,
  UOP_FP,
  MAX_UOP_TYPE
};

enum IQType {
  IQ_INT,
  IQ_LD,
  IQ_STA,
  IQ_STD,
  IQ_BR,
  IQ_NUM,
};

struct IssuePortConfigInfo {
  int port_idx;          // 物理端口号 (Out.iss2prf 的下标)
  uint64_t support_mask; // 该端口支持的操作掩码 (Capability)
};

struct IQStaticConfig {
  int id;                 // IQ ID
  int size;               // 队列深度
  int dispatch_width;     // 入队宽度 (Dispatch 写端口数)
  uint64_t supported_ops; // IQ 整体接收什么指令 (用于 Dispatch 路由)
  int port_start_idx;
  int port_num;
};
