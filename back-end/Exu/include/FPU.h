#pragma once
#include "AbstractFU.h"  
#include "IO.h"
#include "config.h"
#include <cassert>
#include <climits>


extern "C" {
    #include "softfloat.h"
    #include "fadd.h"
    #include "fmul.h"
}

// ==========================================
// FPUSoftfloat
// ==========================================
class FPUSoftfloat : public FixedLatencyFU {
    static constexpr int FADD = 0b00000;
    static constexpr int FSUB = 0b00001;
    static constexpr int FMUL = 0b00010;

public:
  FPUSoftfloat(std::string name = "FPUSoftfloat", int port_idx = 0, int lat = 1)
      : FixedLatencyFU(name, port_idx, lat) {}

protected:
  void impl_compute(MicroOp &inst) override {
    float32_t a,b;
    a.v = inst.src1_rdata;
    b.v = inst.src2_rdata;
    softfloat_roundingMode = inst.func3; // rm

    switch (inst.op) {
    case UOP_FP:{
        switch (inst.func7 >> 2) {
        case FADD:
            inst.result = f32_add(a,b).v;
            break;
        case FSUB:
            b.v ^= 0x80000000;
            inst.result = f32_add(a,b).v;
            break;
        case FMUL:
            inst.result = f32_mul(a,b).v;
            break;
        default:
            assert(0);
        }
        break;
    }
    default: {
        inst.result = f32_add(a,b).v;
        break;
    }
    }
  }
};

// ==========================================
// FPURtl
// ==========================================
class FPURtl : public FixedLatencyFU {
    static constexpr int FADD = 0b00000;
    static constexpr int FSUB = 0b00001;
    static constexpr int FMUL = 0b00010;

public:
    FPURtl(std::string name = "FPURtl", int port_idx = 0, int lat = 1)
        : FixedLatencyFU(name, port_idx, lat) {}

protected:
    // 将整数转换为布尔数组
    void uintToBoolArray(uint32_t value, bool* arr, int size) {
        for (int i = 0; i < size; ++i) {
            arr[i] = (value >> i) & 1;
        }
    }

    // 将布尔数组转换为整数
    uint32_t boolArrayToUInt(const bool* arr, int size) {
        uint32_t value = 0;
        for (int i = 0; i < size; ++i) {
            if (arr[i]) {
                value |= (1u << i);
            }
        }
        return value;
    }

    uint32_t rtlFADD32(uint32_t a, uint32_t b, uint32_t rm) {
        bool pi[67] = {false};  
        bool po[37] = {false};  
        
        uintToBoolArray(a, &pi[0], 32);
        uintToBoolArray(b, &pi[32], 32);
        uintToBoolArray(rm, &pi[64], 3);
        
        fadd_io_generator(pi, po);
        
        uint32_t result = boolArrayToUInt(&po[0], 32);
        uint32_t fflags = boolArrayToUInt(&po[32], 5);
        
        return result;
    }

    uint32_t rtlFMUL32(uint32_t a, uint32_t b, uint32_t rm) {
        bool pi[67] = {false};  
        bool po[37] = {false};  
        
        uintToBoolArray(a, &pi[0], 32);
        uintToBoolArray(b, &pi[32], 32);
        uintToBoolArray(rm, &pi[64], 3);
        
        fmul_io_generator(pi, po);
        
        uint32_t result = boolArrayToUInt(&po[0], 32);
        uint32_t fflags = boolArrayToUInt(&po[32], 5);
        
        return result;
    }

    void impl_compute(MicroOp &inst) override {
        uint32_t a = inst.src1_rdata;
        uint32_t b = inst.src2_rdata;
        uint32_t rm = inst.func3;

        switch (inst.op) {
        case UOP_FP: {
            switch (inst.func7 >> 2) {
            case FADD:
                inst.result = rtlFADD32(a, b, rm);
                break;
            case FSUB:
                inst.result = rtlFADD32(a, b ^ 0x80000000, rm);
                break;
            case FMUL:
                inst.result = rtlFMUL32(a, b, rm);
                break;
            default:
                assert(0);
            }
            break;
        }
        default: {
            inst.result = rtlFADD32(a, b, rm);
            break;
        }
        }
    }
};

