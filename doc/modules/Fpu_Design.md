# Fpu (Floating-Point Unit) 设计文档

## 1. 概述 (Overview)
当前后端 FPU 采用“Zfinx 风格编码 + 整数寄存器堆承载数据”的实现路径：

1. 译码入口按 Zfinx 使用 `OP-FP (opcode=0x53)`。
2. 浮点操作数来自通用整数寄存器（PRF 读出的 `src1_rdata/src2_rdata`）。
3. FPU 结果回写到整数寄存器（不使用独立 FPR）。

当前主流水线不支持三操作数 `rs3` 数据通路，因此仅覆盖两操作数/单操作数 FP 指令。

---

## 2. 译码与流水线接入

### 2.1 译码策略
IDU 将 `opcode=0x53` 译为 `InstType::FP`，并统一下发为 `UOP_FP`：

1. `OP-FP` 入口：见 [Idu.cpp](/home/tututu11037/work/personal/simulator/back-end/Idu.cpp:520)。
2. `FCVT/FSQRT/FCLASS` 这类单输入指令通过 `src2_en=false` 表达，同时保留 `rs2` 到 `uop.imm`（用于选择子类型）：见 [Idu.cpp](/home/tututu11037/work/personal/simulator/back-end/Idu.cpp:525)。
3. Dispatch 将 `InstType::FP` 映射到 `UOP_FP`：见 [Dispatch.cpp](/home/tututu11037/work/personal/simulator/back-end/Dispatch.cpp:819)。

### 2.2 FMA opcode 当前状态
ISA 常量里保留了 `0x43/0x47/0x4b/0x4f`（`fmadd/fmsub/fnmsub/fnmadd`）枚举：见 [RISCV.h](/home/tututu11037/work/personal/simulator/include/RISCV.h:39)。
但当前 IDU 未建立对应解码路径，主流水线不支持三操作数 FMA。

---

## 3. FPU 实现（两套并列模型）
FPU 统一定义于 `back-end/Exu/include/FPU.h`，当前有两种并列实现：`FPUSoftfloat` 与 `FPURtl`。

### 3.1 `FPUSoftfloat`（主用实现）
实现入口： [FPU.h](/home/tututu11037/work/personal/simulator/back-end/Exu/include/FPU.h:18)  
EXU 默认实例化： [Exu.cpp](/home/tututu11037/work/personal/simulator/back-end/Exu/Exu.cpp:101)

#### 3.1.1 实现方式
1. 基于 Berkeley SoftFloat API（`f32_add/f32_mul/f32_div/f32_sqrt/...`）实现语义。
2. 在 FPU 内实现了 `fclass` 与 `fmin/fmax` 的 RISC-V 语义辅助函数（含 NaN/±0 处理）。
3. `rm=111(DYN)` 当前临时按 `RNE` 处理，未从 `frm` 读取。

#### 3.1.2 当前已支持指令（主标准）
1. 算术：`fadd.s` `fsub.s` `fmul.s` `fdiv.s`
2. 开方：`fsqrt.s`
3. 符号注入：`fsgnj.s` `fsgnjn.s` `fsgnjx.s`
4. 最值：`fmin.s` `fmax.s`
5. 比较：`feq.s` `flt.s` `fle.s`
6. 分类：`fclass.s`
7. 转换：`fcvt.w.s` `fcvt.wu.s` `fcvt.s.w` `fcvt.s.wu`

核心分支见 [FPU.h](/home/tututu11037/work/personal/simulator/back-end/Exu/include/FPU.h:137)。

#### 3.1.3 延迟模型
1. `fadd/fsub`: 5 周期
2. `fmul`: 3 周期
3. `fdiv/fsqrt`: 10 周期
4. `fcvt`: 3 周期
5. 其余已实现 FP 指令默认 5 周期

FU 侧延迟：见 [FPU.h](/home/tututu11037/work/personal/simulator/back-end/Exu/include/FPU.h:248)  
ISU 侧镜像：见 [Isu.cpp](/home/tututu11037/work/personal/simulator/back-end/Isu.cpp:93)

### 3.2 `FPURtl`（AIG 格式电路）
实现入口： [FPU.h](/home/tututu11037/work/personal/simulator/back-end/Exu/include/FPU.h:275)

#### 3.2.1 实现方式
1. 通过 `fadd_io_generator/fmul_io_generator` 调用 AIG 电路计算逻辑。
2. 位级接口：
   - `pi[67] = {a[31:0], b[31:0], rm[2:0]}`
   - `po[37] = {result[31:0], fflags[4:0]}`
3. 当前只回写 `result`，`fflags` 已解出但未接入架构状态。

对应代码见 [FPU.h](/home/tututu11037/work/personal/simulator/back-end/Exu/include/FPU.h:305)。

#### 3.2.2 当前支持范围
1. `fadd.s`
2. `fsub.s`（通过翻转 `src2` 符号位后复用 `fadd` 路径）
3. `fmul.s`

其余 FP 指令在该模型中未实现。

#### 3.2.3 延迟模型
1. `fadd/fsub`: 5 周期
2. `fmul`: 3 周期

见 [FPU.h](/home/tututu11037/work/personal/simulator/back-end/Exu/include/FPU.h:366)。

---

## 4. 独占阻塞式执行模型（当前行为）

### 4.1 核心语义
两种 FPU 都继承 `IterativeFU`，是“非全流水化、单在飞”模型：

1. `can_accept()` 仅在 `!busy` 时为真。
2. `accept()` 后进入 busy，直到 `done_cycle<=sim_time` 且 `pop_finished()` 才释放。
3. 因此同一 FPU 实例任一时刻只执行一条 FP 指令。

见 [AbstractFU.h](/home/tututu11037/work/personal/simulator/back-end/Exu/include/AbstractFU.h:139)。

### 4.2 对发射的影响
EXU 发射阶段若目标 FPU 不可接收，则该 issue 端口置 `issue_stall=true`：

1. 路由与准入检查： [Exu.cpp](/home/tututu11037/work/personal/simulator/back-end/Exu/Exu.cpp:323)
2. 阻塞标记： [Exu.cpp](/home/tututu11037/work/personal/simulator/back-end/Exu/Exu.cpp:337)

这就是当前“全能 FPU 但独占阻塞式”的微架构语义。

---

## 5. 以 FPUSoftfloat 为标准，距离完整 Zfinx 的缺口

### 5.1 指令覆盖缺口
当前仍缺完整 Zfinx 中的三操作数 FMA 组：

1. `fmadd.s`
2. `fmsub.s`
3. `fnmsub.s`
4. `fnmadd.s`

根因是主流水线暂无 `rs3/src3` 承载与依赖跟踪通路（Ren/Dispatch/IQ/PRF/EXU 均为双源模型）。

### 5.2 架构状态与控制缺口
1. `fcsr/frm/fflags` 未完整接入 CSR 体系。
2. `rm=111(DYN)` 未读取 `frm`，当前临时固定为 `RNE`。
3. 浮点异常标志（`NV/DZ/OF/UF/NX`）未以架构状态形式累计维护。

### 5.3 语义完整性缺口（工程化）
1. 当前不支持编码多数通过 `assert` 处理，后续可收敛为统一非法指令异常路径。
2. 需要与 `ref` 的 Zfinx 全集（含 FMA）继续对齐回归。

---

## 6. 后续演进建议
1. 若目标是“完整 Zfinx”，优先补三源通路以支持 FMA 四指令。
2. 接入 `fcsr/frm/fflags` 后再处理 `DYN rm` 与异常标志的提交时累积。
3. 若目标是提高吞吐，可将 FPU 从 `IterativeFU` 升级为可流水化模型，或配置多 FPU 实例。
