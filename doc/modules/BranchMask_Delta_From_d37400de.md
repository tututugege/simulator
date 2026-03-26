# Branch Mask 重构对比（相对 `d37400de`）

## 1. 对比范围
- 基线提交：`d37400de`
- 对比目标：当前工作区（含未提交改动）
- 目标：从 `tag` 驱动分支恢复，迁移到 `br_id + br_mask + clear_mask` 驱动

## 2. 变更文件总览
- 后端核心模块：`back-end/Idu.cpp`、`back-end/Ren.cpp`、`back-end/Dispatch.cpp`、`back-end/Isu.cpp`、`back-end/Prf.cpp`、`back-end/Exu/Exu.cpp`、`back-end/Lsu/SimpleLsu.cpp`
- 头文件与IO：`back-end/include/types.h`、`back-end/include/IO.h`、`back-end/include/ModuleIOs.h`、`back-end/include/Idu.h`、`back-end/include/Ren.h`、`back-end/include/Isu.h`、`back-end/include/IssueQueue.h`、`back-end/Lsu/include/AbstractLsu.h`、`back-end/Lsu/include/SimpleLsu.h`
- 其他：`back-end/Exu/include/AbstractFU.h`、`back-end/Exu/include/Fu.h`、`back-end/Rob.cpp`、`back-end/include/PerfCount.h`

## 3. 数据结构与接口层改动

### 3.1 `types.h`：指令元数据从 `tag` 迁移到掩码语义
- `InstInfo` / `MicroOp` 增加：
  - `br_id`
  - `br_mask`
- `MicroOp(const InstInfo&)` 拷贝逻辑同步改为复制 `br_id/br_mask`。

### 3.2 `IO.h`：广播协议改造
- `DecBroadcastIO`：
  - `br_tag` -> `br_id`
  - 新增 `clear_mask`
- `ExuIdIO`：
  - `br_tag` -> `br_id`
  - 新增 `clear_mask`
- `DisLsuIO`：
  - `tag[]` -> `br_mask[]`
  - `ldq_tag[]` -> `ldq_br_mask[]`

### 3.3 `ModuleIOs.h`：硬件可视化 slim 字段映射
- 各类 `filter()` 中保留 `slim.tag` 字段，但数据源改为 `full.br_id`（兼容过渡）。
- `IduIO` 的可视化输入输出字段由 `br_tag` 改为 `br_id`。

## 4. 模块行为改动（按流水级）

### 4.1 IDU：从 `tag_list` 恢复改为 `br_mask` 恢复
文件：`back-end/Idu.cpp`、`back-end/include/Idu.h`

- 状态重构：
  - 删除：`tag_list / enq_ptr / now_tag`
  - 新增：`now_br_mask / br_mask_cp[]`
- `comb_decode`：
  - 给每条指令直接生成 `uop.br_id` 与 `uop.br_mask`
  - 分支自身不带 self-bit，后续更年轻指令才看到该 bit
  - 引入 ID 阶段旁路清理：`running_mask = now_br_mask & ~clear_mask`
- `comb_branch`：
  - mispred 广播改为 `br_id` + `br_mask = (1 << br_id)`
  - 用 `br_mask_cp[br_id]` 计算并释放更年轻分支占用
  - 广播 `clear_mask`
- `comb_release_tag`：
  - 不再在 commit 阶段释放分支ID（释放前移到 branch resolve）

### 4.2 Rename：检查点索引改为 `br_id`
文件：`back-end/Ren.cpp`、`back-end/include/Ren.h`

- 检查点数组更名：
  - `RAT_checkpoint` -> `RAT_checkpoint`
  - `alloc_checkpoint` -> `alloc_checkpoint`
- mispred 恢复索引：
  - `in.dec_bcast->br_tag` -> `in.dec_bcast->br_id`
- pipeline 增加 `clear_mask` 清位，保证留存/新进入条目都不会保留已解析分支位。

### 4.3 Dispatch：LSU 分配口改为显式 `br_mask`
文件：`back-end/Dispatch.cpp`

- STQ/LDQ 分配接口改为携带 `br_mask`，不再传 `tag`。
- 三个关键点补齐清位：
  - `comb_alloc`
  - `comb_fire`
  - `comb_pipeline`
- 入队口新增“强制清理”逻辑，避免 stale 分支位进入后端缓冲结构。

### 4.4 ISU / IssueQueue：flush 与 clear 分离
文件：`back-end/Isu.cpp`、`back-end/include/IssueQueue.h`、`back-end/include/Isu.h`

- `LatencyEntry` 从 `tag` 改为：
  - `br_mask`
  - `rob_idx/rob_flag`（用于更稳健调试与定位）
- `IssueQueue::flush_br()` 逻辑改为按 `uop.br_mask & br_mask` 判定。
- 新增 `IssueQueue::clear_br(clear_mask)`，对存活条目清已解析位。
- `Isu::comb_flush()` 在 mispred flush 后，增加 clear 流程处理 IQ 和 latency pipe。

### 4.5 PRF：kill 判定切到 `br_mask`
文件：`back-end/Prf.cpp`

- 增加统一 `is_killed(uop, dec_bcast)`。
- 原 `1 << tag` 判定全部替换为 `uop.br_mask & dec_bcast->br_mask`。
- pipeline 阶段补齐 `clear_mask` 清位。

### 4.6 EXU/FU：统一掩码判定并提供 `clear_br`
文件：`back-end/Exu/Exu.cpp`、`back-end/Exu/include/AbstractFU.h`

- 新增统一 kill 判定函数 `is_br_killed`，替换旧 `1 << tag` 判定。
- `comb_pipeline` 顺序明确为：
  1. global flush
  2. mispred flush
  3. clear resolved bits
- `AbstractFU` 新增 `clear_br(clear_mask)` 抽象接口；`FixedLatencyFU/IterativeFU` 落实实现。
- 分支仲裁输出：
  - `exu2id->br_id`
  - `exu2id->clear_mask`
- 当前版本已去掉 EXU 本地“同拍额外 clear”的补丁路径，统一通过 IDU 广播的 clear 机制收敛。

### 4.7 LSU：队列元数据全面 `mask` 化
文件：`back-end/Lsu/SimpleLsu.cpp`、`back-end/Lsu/include/AbstractLsu.h`、`back-end/Lsu/include/SimpleLsu.h`

- `StqEntry` 字段 `tag` -> `br_mask`。
- `reserve_stq_entry/reserve_ldq_entry` 参数改为 `br_mask`。
- mispred 杀伤与恢复逻辑改为 `uop.br_mask & mask` 判定。
- `seq()` 中补齐 clear 阶段，对 LDQ/STQ/finished 队列统一清位。

## 5. 其他改动
- `back-end/Rob.cpp`：仅有格式性空行变化（无行为差异）。
- `back-end/include/PerfCount.h`：恢复打印 `idu tag stall` 计数。
- `back-end/Exu/include/Fu.h`：主要是注释/包含顺序清理，功能影响较小。

## 6. 与“纯 br_id + mask”目标的距离

### 6.1 已完成
- 主数据通路已经不依赖 `uop.tag` 做 flush/kill 判定。
- `clear_mask` 已形成跨模块闭环（IDU/REN/DIS/ISU/PRF/EXU/LSU）。
- 分支恢复主索引已迁移到 `br_id`。

### 6.2 仍属过渡态（可继续收敛）
- `ModuleIOs.h` 的硬件展示结构还保留 `slim.tag` 命名（虽已映射 `br_id`）。
- `PerfCount` 中仍使用 `idu tag stall` 命名，可后续统一改名为 `idu br_id stall` 或 `idu branch-id stall`。

## 7. 建议的下一步收敛
1. 清理 `ModuleIOs.h` 中所有“`tag`命名但承载`br_id`语义”的字段命名。
2. 全局检索 `uop.tag`，逐处确认是否仍参与功能逻辑（目标：只保留兼容层，不参与判定）。
3. 补一份“小规模回归矩阵”（`dhrystone.bin` + `462.libquantum fast`）作为该重构阶段的验收基线。
