#include <iostream>
#include <cstdint>
#include <iomanip>

constexpr int kByteCount = 19;
struct WideBits {
    uint8_t bytes[kByteCount];

    WideBits() {
        for (int i = 0; i < kByteCount; ++i) bytes[i] = 0;
    }

    WideBits(uint64_t val) {
        for (int i = 0; i < kByteCount; ++i) {
            bytes[i] = static_cast<uint8_t>(val & 0xFF);
            val >>= 8;
        }
    }

    void trim_unused_bits() {
        if (150 % 8 != 0) {
            bytes[kByteCount - 1] &= (1 << (150 % 8)) - 1;
        }
    }

    WideBits operator<<(int shift) const {
        WideBits res;
        if (shift < 0 || shift >= 150) return res;
        int byte_shift = shift / 8;
        int bit_shift = shift % 8;
        for (int i = kByteCount - 1; i >= byte_shift; --i) {
            res.bytes[i] = bytes[i - byte_shift] << bit_shift;
            if (bit_shift > 0 && i - byte_shift - 1 >= 0) {
                res.bytes[i] |= bytes[i - byte_shift - 1] >> (8 - bit_shift);
            }
        }
        res.trim_unused_bits();
        return res;
    }

    operator bool() const {
        for (int i = 0; i < kByteCount; ++i) {
            if (bytes[i] != 0) return true;
        }
        return false;
    }
    
    WideBits operator&(const WideBits& other) const {
        WideBits res;
        for(int i=0; i<kByteCount; ++i) res.bytes[i] = bytes[i] & other.bytes[i];
        return res;
    }

    WideBits operator~() const {
        WideBits res;
        for(int i=0; i<kByteCount; ++i) res.bytes[i] = ~bytes[i];
        res.trim_unused_bits();
        return res;
    }

    WideBits& operator&=(const WideBits& other) {
        for(int i=0; i<kByteCount; ++i) bytes[i] &= other.bytes[i];
        return *this;
    }
    
    WideBits& operator|=(const WideBits& other) {
        for(int i=0; i<kByteCount; ++i) bytes[i] |= other.bytes[i];
        trim_unused_bits();
        return *this;
    }
};

int main() {
    WideBits mask = WideBits(1) << 149;
    std::cout << "Mask has bit 149 set? " << (bool)mask << std::endl;
    WideBits uop_mask;
    uop_mask |= mask;
    std::cout << "uop_mask & mask != 0 ? " << (bool)(uop_mask & mask) << std::endl;
    
    WideBits clear_mask = WideBits(1) << 149;
    uop_mask &= ~clear_mask;
    std::cout << "After clear, uop_mask & mask != 0 ? " << (bool)(uop_mask & mask) << std::endl;
    return 0;
}
