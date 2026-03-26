// LsuUtils.h
#include <cstdint>

class LsuUtils {
public:
  // 解析 func3 得到字节大小 (用于 Store 或者 Debug)
  static int get_size_in_bytes(int func3) {
    // func3 & 0x3 得到 0,1,2,3
    // 对应 1, 2, 4, 8 字节
    return 1 << (func3 & 0b11);
  }

  // 通用的数据处理函数
  // 输入：raw_data (从 Cache 读回来的原始 64位/32位 数据)
  // 输出：写入寄存器的最终值 (已移位、掩码、符号扩展)
  static uint64_t align_and_sign_extend(uint64_t raw_data, uint64_t addr,
                                        int func3) {
    int offset_bits = (addr & 0b111) * 8; // 计算位偏移
    int size_code = func3 & 0b11;         // 0=B, 1=H, 2=W, 3=D
    bool is_unsigned = (func3 & 0b100);   // Bit 2 是 unsigned 标志

    // 1. 移位 (Alignment)
    // 将目标数据移到最低位
    uint64_t shifted_data = raw_data >> offset_bits;

    // 2. 掩码生成 (Masking)
    // 技巧：利用 size_code 生成掩码
    // size=0(1B) -> mask=0xFF
    // size=1(2B) -> mask=0xFFFF
    // size=2(4B) -> mask=0xFFFFFFFF
    // size=3(8B) -> mask=0xFF..FF
    uint64_t mask = (uint64_t)-1;
    if (size_code < 3) {
      mask = (1ULL << (8 * (1 << size_code))) - 1;
    }

    uint64_t val = shifted_data & mask;

    // 3. 符号扩展 (Sign Extension)
    // 只有 Load 且不是 Unsigned 时才做
    if (!is_unsigned && size_code < 3) {
      // 检查符号位
      uint64_t sign_bit_pos = (8 * (1 << size_code)) - 1;
      if ((val >> sign_bit_pos) & 1) {
        // 如果符号位是1，把高位全部置1
        // 技巧：利用按位取反的掩码
        val |= ~mask;
      }
    }

    return val;
  }
};
