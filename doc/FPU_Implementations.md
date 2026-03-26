# 浮点运算单元 (FPU) 设计文档

本文档介绍了 `back-end/Exu/include/` 目录中新增的浮点运算单元 (FPU) 的实现细节。目前实现了两种不同的 FPU 模型，分别用于功能验证和硬件仿真。

## 1. 概述

FPU 位于执行单元 (Exu) 中，负责处理 RISC-V 浮点指令（如 FADD.S, FMUL.S 等）。代码结构如下：

- `FPU.h`: 定义了 FPU 的类接口和两种主要的实现模型。
- `fadd.h`: 自动生成的加法器组合逻辑（门级仿真模型）。
- `fmul.h`: 自动生成的乘法器组合逻辑（门级仿真模型）。

## 2. FPUSoftfloat (功能模型)

`FPUSoftfloat` 是一个基于 Berkeley SoftFloat 库的函数式模型。其主要特点是：

- **完备性**: 使用成熟的 C 语言浮点库 `softfloat`，支持 IEEE 754 标准的所有边缘情况。
- **快速性**: 适合高性能模拟。
- **配置**: 
  - 支持从指令 (`inst.rm`) 中读取舍入模式 (Rounding Mode)。
  - 目前实现了 `FADD` (Func5: 0x00) 和 `FMUL` (Func5: 0x02)。

## 3. FPURtl (RTL 仿真模型)

`FPURtl` 提供了一个结构化的仿真模型，旨在模拟真实的硬件门电路逻辑。

### 3.1 工作原理
该模型通过布尔数组（Boolean Array）模拟硬件连线：
1. **输入转换**: 将 32 位浮点操作数 A, B 以及 3 位舍入模式 RM 转换为布尔数组 (`pi[67]`)。
2. **逻辑生成**: 调用 `fadd_io_generator` 或 `fmul_io_generator`。这些函数内部是巨大的布尔表达式树，直接对应于硬件的逻辑门。
3. **输出转换**: 将输出布尔数组 (`po[37]`) 转换回 32 位结果和 5 位标志位 (fflags)。

### 3.2 自动生成组件 (`fadd.h` & `fmul.h`)
这两个头文件包含了极为庞大的 `fadd_io_generator` 和 `fmul_io_generator` 函数。它们是由硬件设计工具（如 Chisel 或 Verilog 综合工具）自动生成的 C++ 代码，通过逐门模拟 (Gate-level simulation) 确保模拟器的执行结果与实际生成的硬件电路完全一致。

## 4. 接口与集成

FPU 类继承自 `FixedLatencyFU`，这意味着它们在流水线中具有固定的执行延迟。

```cpp
void impl_compute(Inst_uop &inst) override {
    // 1. 获取源操作数和舍入模式
    // 2. 根据 inst.func5 选择操作类型
    // 3. 将计算结果写回 inst.result
}
```

### 关键数据对应关系
- **输入端口 (PI)**:
  - `pi[0:31]`: src1 (Operand A)
  - `pi[32:63]`: src2 (Operand B)
  - `pi[64:66]`: Rounding Mode
- **输出端口 (PO)**:
  - `po[0:31]`: 计算结果 (Result)
  - `po[32:36]`: 浮点异常标志 (fflags)

## 5. 后续计划
- 将 FPU 实例集成到 `Exu.cpp` 的执行端口映射中。
- 完善其他浮点指令（如 FMSUB, FDIV, FSQRT, FSGNJ 等）的 RTL 逻辑生成。
