# TMA (Top-Down Microarchitecture Analysis) Profiling 文档

## 1. 概述
TMA (Top-Down Microarchitecture Analysis) 是一种用于识别 CPU 性能瓶颈的方法论。它将 CPU 的总由于（Slots）划分为四个主要类别：
- **Retiring (提交)**: 有效执行并提交的指令。
- **Bad Speculation (错误预测)**:由于分支预测错误或异常导致被冲刷的指令。
- **Frontend Bound (前端受限)**: 前端无法向后端提供足够的指令。
- **Backend Bound (后端受限)**: 后端无法处理前端提供的指令（通常由于资源阻塞）。

本模拟器实现了 TMA Level 1 和部分 Level 2/3 的指标统计。

## 2. 核心组件
TMA 的核心统计逻辑位于 `back-end/include/PerfCount.h` 和 `back-end/Dispatch.cpp` 中。

### 2.1 统计计数器 (`PerfCount.h`)
`PerfCount` 类维护了以下关键计数器：
- `slots_issued`: 成功发射到后端执行单元的指令槽位数。
- `slots_backend_bound`: 后端阻塞导致的空闲槽位数。
- `slots_frontend_bound`: 前端未提供指令导致的空闲槽位数。
- `slots_bad_speculation`: 通过 `slots_issued - commit_num` 计算得出。

细分指标：
- **Frontend Bound**:
  - `slots_fetch_latency`: 取指延迟（主要由 I-Cache Miss 导致）。
  - `slots_fetch_bandwidth`: 取指带宽受限。
- **Backend Bound**:
  - `slots_mem_bound_lsu`: 访存相关阻塞。
    - `slots_mem_l1_bound`: L1 资源（STQ/LDQ）不足。
    - `slots_mem_ext_bound`: 外部存储器访问延迟（ROB 头指令为 Load/Store 且发生 Cache Miss）。
  - `slots_core_bound_*`: 核心计算资源阻塞。
    - `slots_core_bound_rob`: ROB 满。
    - `slots_core_bound_iq`: 发射队列（Issue Queue）满。

## 3. 实现与检测逻辑 (`Dispatch.cpp`)

TMA 的统计主要在分派（Dispatch）阶段的 `comb_fire` 函数中进行。系统逐个检查每个指令槽位（Slot）的状态：

### 3.1 槽位状态判断
对于每一个发射宽度内的槽位 `i` (0 到 FETCH_WIDTH-1)：
1. **Issued (Retiring + Bad Speculation)**:
   - 如果 `out.dis2rob->dis_fire[i]` 为真，表示指令成功发射。
   - 计数器：`slots_issued++`。

2. **Backend Bound**:
   - 如果指令有效 (`inst_r[i].valid`) 但未能发射 (`!dis_fire`)，则判定为后端受限。
   - 计数器：`slots_backend_bound++`。
   - **阻塞原因细分**:
     - **ROB 阻塞**: 如果 `!in.rob2dis->ready` (ROB 满)。
       - 如果 ROB 头指令是访存指令且未就绪 (`rob2dis->head_is_memory`)：
         - 若发生 Cache Miss (`head_is_miss`) -> **Memory Bound (Ext)**。
         - 否则 -> **Memory Bound (L1)**。
       - 否则 -> **Core Bound (ROB)**。
     - **资源阻塞 (IQ/LSU)**: 如果 `!dispatch_success_flags[i]`。
       - 如果是 Load/Store 指令且相应队列（LDQ/STQ）满 -> **Memory Bound (L1)**。
       - 否则（IQ 满） -> **Core Bound (IQ)**。

3. **Frontend Bound**:
   - 如果指令槽位无效 (`!inst_r[i].valid`)，则判定为前端受限。
   - 计数器：`slots_frontend_bound++`。
   - **阻塞原因细分**:
     - 如果 `icache_busy` 为真 -> **Fetch Latency**。
     - 否则 -> **Fetch Bandwidth**。

## 4. 输出与分析
在模拟结束时，调用 `perf_print_tma()` 函数打印统计报告：

```
*********Top-Down Analysis (Level 1)************
Total Slots      : [总槽位数]
Frontend Bound   : [百分比]
  - Fetch Latency  : [百分比] (Approx by ICache Miss)
  - Fetch Bandwidth: [百分比]
Backend Bound    : [百分比]
  - Memory Bound   : [百分比] (LSU Stall)
    - L1 Bound       : [百分比]
    - Ext Memory Bound: [百分比]
  - Core Bound     : [百分比] (IQ/ROB Stall)
Bad Speculation  : [百分比]
Retiring         : [百分比]
```

## 5. 与模拟器的交互
- **PerfCount 对象**: 嵌入在 `SimContext` 中，作为全局统计工具。
- **Dispatch 阶段**: 作为流水线中连接前端和后端的关键点，最适合进行 Slot 分类统计。
- **ROB 状态**: ROB 提供了关于队头阻塞原因的关键信息（如是否为长延迟 Load），辅助区分 Memory Bound 和 Core Bound。
