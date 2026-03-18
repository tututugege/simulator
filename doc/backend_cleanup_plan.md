# Back-end 统一化与去冗余计划

目标：在当前主干接口已经基本收敛的基础上，继续统一 back-end 的接口风格，并清理剩余明显冗余的存储/传输字段。

## 当前状态

已经基本完成：
- `IO.h` 中主干流水线接口已集中管理
- `InstType/UopType` 在接口层改为显式位宽编码
- `ROB commit payload` 已明显收缩
- `IssueQueue` 内部存储已改成专用 `IqStoredUop/IqStoredEntry`
- `IQ -> PRF -> EXU` 链路已去掉一批冗余字段：
  - `uop_num`
  - `page_fault_inst`
  - `illegal_inst`
  - `ftq_is_last`
  - `diag_val`

仍未完全统一的部分：
- 模块 `In/Out` 壳子仍大量使用 `class`
- `BackTop.h`、`FTQ.h`、`InstructionBuffer.h` 仍保持软件风格结构
- `MemReqIO/MemRespIO` 仍整包携带 `MicroOp`
- `types.h` 仍保留较多 `typedef struct` 风格定义

## 阶段 1：收缩 memory path

优先级最高。

目标：
- 检查 `MemReqIO/MemRespIO` 在 dcache / PTW / LSU 路径中真实需要的字段
- 去掉 `MicroOp` 整包透传
- 替换为 memory path 专用 payload

重点文件：
- `back-end/include/IO.h`
- `back-end/Lsu/SimpleLsu.cpp`
- `MemSubSystem/*`

原因：
- 这是当前最明显的大块冗余
- 相比主流水接口，收益更高

## 阶段 2：统一模块壳子风格

目标：
- 将模块 `In/Out` 壳子从 `class` 统一为 `struct`
- 统一命名风格

涉及文件：
- `Ren.h`
- `Prf.h`
- `Isu.h`
- `Rob.h`
- `Dispatch.h`
- `Exu/include/Exu.h`

说明：
- 这一阶段主要是格式统一
- 原则上不应改行为

## 阶段 3：整理外层边界和内部存储风格

目标：
- 评估 `BackTop.h` 的 `Back_in/Back_out`
- 评估 `FTQEntry`
- 评估 `InstructionBufferEntry`

说明：
- 这些结构不一定都要完全硬件化
- 但需要明确哪些是模块边界，哪些是内部状态，哪些仍保留为软件侧结构

## 阶段 4：收尾基础类型风格

目标：
- 评估 `types.h` 中 `typedef struct` 是否统一为 `struct`
- 保持 `dbg/tma` 为有意保留的 sideband

说明：
- 这一步主要是代码风格一致性
- 不应影响当前已经稳定的主干语义

## 推荐执行顺序

1. `MemReqIO/MemRespIO`
2. 模块 `In/Out` 壳子
3. `BackTop/FTQ/InstructionBuffer`
4. `types.h` 风格收尾
