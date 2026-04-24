#pragma once
#include <cstdint>
#include <type_traits>

#define SIMULATOR_HAS_PARENT_WIRE_REG 1

template <typename T>
using RemoveCvRefT = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
inline constexpr bool IsWireScalarV =
    std::is_integral_v<RemoveCvRefT<T>> ||
    std::is_same_v<RemoveCvRefT<T>, unsigned __int128> ||
    std::is_same_v<RemoveCvRefT<T>, __int128>;

template <int Bits>
struct WideBits {
  static_assert(Bits > 0, "wire/reg bit width must be positive");

  static constexpr int kBits = Bits;
  static constexpr int kByteCount = (Bits + 7) / 8;

  uint8_t bytes[kByteCount] = {};

  constexpr WideBits() = default;

  template <typename T, typename = std::enable_if_t<IsWireScalarV<T>>>
  constexpr WideBits(T value) {
    *this = value;
  }

  constexpr void clear() {
    for (int i = 0; i < kByteCount; ++i) {
      bytes[i] = 0;
    }
  }

  constexpr void trim_unused_bits() {
    if constexpr ((Bits % 8) != 0) {
      bytes[kByteCount - 1] &= static_cast<uint8_t>((1u << (Bits % 8)) - 1u);
    }
  }

  template <typename T, typename = std::enable_if_t<IsWireScalarV<T>>>
  constexpr WideBits &operator=(T value) {
    clear();
    if constexpr (std::is_same_v<RemoveCvRefT<T>, bool>) {
      bytes[0] = value ? 1u : 0u;
    } else {
      const unsigned __int128 raw = static_cast<unsigned __int128>(value);
      constexpr int kCopyBytes =
          (kByteCount < static_cast<int>(sizeof(RemoveCvRefT<T>)))
              ? kByteCount
              : static_cast<int>(sizeof(RemoveCvRefT<T>));
      for (int byte = 0; byte < kCopyBytes; ++byte) {
        bytes[byte] = static_cast<uint8_t>((raw >> (byte * 8)) & 0xFFu);
      }
    }
    trim_unused_bits();
    return *this;
  }

  constexpr WideBits &operator|=(const WideBits &other) {
    for (int i = 0; i < kByteCount; ++i) {
      bytes[i] |= other.bytes[i];
    }
    trim_unused_bits();
    return *this;
  }

  template <typename T, typename = std::enable_if_t<IsWireScalarV<T>>>
  constexpr WideBits &operator|=(T value) {
    WideBits other;
    other = value;
    return (*this |= other);
  }

  constexpr bool operator==(const WideBits &other) const {
    for (int i = 0; i < kByteCount; ++i) {
      if (bytes[i] != other.bytes[i]) {
        return false;
      }
    }
    return true;
  }

  constexpr bool operator!=(const WideBits &other) const {
    return !(*this == other);
  }

  template <typename T, typename = std::enable_if_t<IsWireScalarV<T>>>
  constexpr bool operator==(T value) const {
    WideBits other;
    other = value;
    return *this == other;
  }

  template <typename T, typename = std::enable_if_t<IsWireScalarV<T>>>
  constexpr bool operator!=(T value) const {
    return !(*this == value);
  }
};

template <int Bits, typename T,
          typename = std::enable_if_t<IsWireScalarV<T>>>
constexpr bool operator==(T value, const WideBits<Bits> &rhs) {
  return rhs == value;
}

template <int Bits, typename T,
          typename = std::enable_if_t<IsWireScalarV<T>>>
constexpr bool operator!=(T value, const WideBits<Bits> &rhs) {
  return rhs != value;
}

template <int Bits>
constexpr WideBits<Bits> operator|(WideBits<Bits> lhs,
                                   const WideBits<Bits> &rhs) {
  lhs |= rhs;
  return lhs;
}

template <int Bits, typename T,
          typename = std::enable_if_t<IsWireScalarV<T>>>
constexpr WideBits<Bits> operator|(WideBits<Bits> lhs, T value) {
  lhs |= value;
  return lhs;
}

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
                typename std::conditional<
                    N <= 64, uint64_t,
                    typename std::conditional<N <= 128, unsigned __int128,
                                              WideBits<N>>::type>::type>::
                type>::type>::type>::type;

template <int N>
using wire = AutoType<N>;
template <int N>
using reg = AutoType<N>;

static_assert(sizeof(wire<128>) == 16,
              "wire<128> must remain a real 128-bit carrier");
static_assert(sizeof(wire<256>) == 32,
              "wire<256> must remain a real 256-bit carrier");

constexpr int bit_width_for_count(uint32_t count) {
  int width = 0;
  uint32_t max_value = count > 0 ? (count - 1) : 0;
  do {
    width++;
    max_value >>= 1;
  } while (max_value != 0);
  return width;
}

enum UopType {
  UOP_JUMP,
  UOP_ADD,
  UOP_BR,
  UOP_LOAD,
  UOP_STA,
  UOP_STD,
  UOP_CSR,
  UOP_FENCE,
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

constexpr int UOP_TYPE_WIDTH = bit_width_for_count(MAX_UOP_TYPE);

enum IQType {
  IQ_INT,
  IQ_LD,
  IQ_STA,
  IQ_STD,
  IQ_BR,
  IQ_NUM,
};

constexpr int ROB_CPLT_MASK_WIDTH = 3;
constexpr wire<ROB_CPLT_MASK_WIDTH> ROB_CPLT_G0 = 0x1;
constexpr wire<ROB_CPLT_MASK_WIDTH> ROB_CPLT_G1 = 0x2;
constexpr wire<ROB_CPLT_MASK_WIDTH> ROB_CPLT_G2 = 0x4;

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
