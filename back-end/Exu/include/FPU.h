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
class FPUSoftfloat : public IterativeFU {
    static constexpr int FADD = 0b00000;
    static constexpr int FSUB = 0b00001;
    static constexpr int FMUL = 0b00010;
    static constexpr int FDIV = 0b00011;
    static constexpr int FCVT_W_S = 0x60;
    static constexpr int FCVT_S_W = 0x68;
    static constexpr int LAT_FADD = 5;
    static constexpr int LAT_FMUL = 3;
    static constexpr int LAT_FDIV = 10;
    static constexpr int LAT_FCVT = 3;

public:
  FPUSoftfloat(std::string name = "FPUSoftfloat", int port_idx = 0, int max_lat = LAT_FDIV)
      : IterativeFU(name, port_idx, max_lat) {}

protected:
  void impl_compute(ExuInst &inst) override {
    float32_t a, b;
    a.v = inst.src1_rdata;
    b.v = inst.src2_rdata;
    // rm=7(DYN) is currently treated as RNE.
    softfloat_roundingMode = (inst.func3 == 7) ? 0 : inst.func3;

    switch (inst.op) {
    case UOP_FP:{
        switch (inst.func7) {
        case FCVT_W_S: {
            uint32_t rs2_sel = inst.imm & 0x1F;
            if (rs2_sel == 0) {
              inst.result = static_cast<uint32_t>(f32_to_i32(a, softfloat_roundingMode, true));
            } else if (rs2_sel == 1) {
              inst.result = f32_to_ui32(a, softfloat_roundingMode, true);
            } else {
              assert(0);
            }
            break;
        }
        case FCVT_S_W: {
            uint32_t rs2_sel = inst.imm & 0x1F;
            if (rs2_sel == 0) {
              inst.result = i32_to_f32(static_cast<int32_t>(inst.src1_rdata)).v;
            } else if (rs2_sel == 1) {
              inst.result = ui32_to_f32(inst.src1_rdata).v;
            } else {
              assert(0);
            }
            break;
        }
        default:
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
        case FDIV:
            inst.result = f32_div(a,b).v;
            break;
        default:
            assert(0);
          }
        }
        break;
    }
    default: {
        inst.result = f32_add(a,b).v;
        break;
    }
    }
  }

  int calculate_latency(const ExuInst &inst) override {
    if (inst.op != UOP_FP) {
      return LAT_FADD;
    }
    switch (inst.func7 >> 2) {
    case FMUL:
      return LAT_FMUL;
    case FDIV:
      return LAT_FDIV;
    case FADD:
    case FSUB:
      return LAT_FADD;
    default:
      if (inst.func7 == FCVT_W_S || inst.func7 == FCVT_S_W) {
        return LAT_FCVT;
      }
      return LAT_FADD;
    }
  }
};

// ==========================================
// FPURtl
// ==========================================
class FPURtl : public IterativeFU {
    static constexpr int FADD = 0b00000;
    static constexpr int FSUB = 0b00001;
    static constexpr int FMUL = 0b00010;
    static constexpr int LAT_FADD = 5;
    static constexpr int LAT_FMUL = 3;

public:
    FPURtl(std::string name = "FPURtl", int port_idx = 0, int max_lat = LAT_FADD)
        : IterativeFU(name, port_idx, max_lat) {}

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

    void impl_compute(ExuInst &inst) override {
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

    int calculate_latency(const ExuInst &inst) override {
        if (inst.op != UOP_FP) {
            return LAT_FADD;
        }
        switch (inst.func7 >> 2) {
        case FMUL:
            return LAT_FMUL;
        case FADD:
        case FSUB:
            return LAT_FADD;
        default:
            return LAT_FADD;
        }
    }
};
