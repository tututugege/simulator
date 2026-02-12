# FTQ (Fetch Target Queue) 文档

## 1. 概述
FTQ (Fetch Target Queue) 是连接处理器前端（Frontend）与后端（Backend）的关键微架构结构。它主要用于存储前端的分支预测信息，并将这些信息传递给后端执行单元以进行验证。通过解耦前端预测与后端执行，FTQ 支持了高性能的推测执行和精确的误预测恢复。

## 2. 数据结构 (`FTQEntry`)

FTQ 是一个循环队列，每个条目对应一个取指块（Fetch Block）。`back-end/include/FTQ.h` 定义了 `FTQEntry` 结构：

- **基础信息**:
  - `start_pc`: 取指块的起始 PC。
  - `next_pc`: 取指块的预测后继 PC（Target）。
  - `valid`: 条目是否有效。

- **预测信息**:
  - `pred_taken_mask`: 位掩码，指示块内每条指令的预测方向（Taken/Not Taken）。
  - `tage_idx`: TAGE 分支预测器的索引（用于更新预测器状态）。
  - `mid_pred`, `alt_pred`: 辅助预测位。
  - `pcpn`, `altpcpn`: 路径历史相关信息。

## 3. 核心机制

### 3.1 分配 (Allocation)
- **位置**: `Idu::seq` (Decode Stage)
- **逻辑**: 当指令包从前端传递到后端时，Idu 会在 FTQ 的 `tail` 处申请一个新的条目。
- **内容**: 将前端传递过来的预测信息（`predict_dir`, `predict_next_fetch_address` 等）存入该条目。
- **关联**: 每条指令在重命名（Rename）阶段都会被标记它所属的 `ftq_idx` 和块内偏移 `ftq_offset`。

### 3.2 验证 (Verification)
- **位置**: `BruUnit::impl_compute` (Execution Stage)
- **逻辑**: 
  1. 分支指令执行时，通过其携带的 `ftq_idx` 索引 FTQ。
  2. 获取前端的预测结果 (`pred_taken_mask`)。
  3. 将执行结果（实际跳转方向）与预测结果进行比对。
  4. 如果不一致，触发误预测（`inst.mispred = true`）。

### 3.3 提交与回收 (Commit & Reclamation)
- **位置**: `BackTop::seq` (Commit Stage)
- **逻辑**: 当一条标记为 `ftq_is_last`（块内最后一条有效指令）的指令提交时，FTQ 的 `head` 指针向前移动（`pop`），释放该条目。这保证了只有当包含该预测块的所有指令都退休后，预测信息才被丢弃。

### 3.4 恢复 (Recovery)
- **位置**: `Idu::comb_flush`
- **逻辑**: 
  - 当发生分支误预测时，处理器流水线会被冲刷。
  - FTQ 的 `tail` 指针需要回滚到误预测分支所在的条目之后（`new_tail = (mispred_ftq + 1) % FTQ_SIZE`）。
  - 这样保留了误预测点之前的正确路径预测信息，丢弃了之后的错误路径信息。

## 4. 模块交互

1. **Frontend (Idu)**: 
   - 负责填充 FTQ。
   - 在分派指令时，将 FTQ索引 附加到 `MicroOp` 中。

2. **Backend (Exu/Bru)**:
   - 读取 FTQ 进行方向预测验证。
   - 计算正确的目标地址（如果预测错误）。

3. **Backend (Rob/Commit)**:
   - 监控指令提交，驱动 FTQ 的资源回收。
   - 在误预测时触发全局恢复信号，通知 Idu 重置 FTQ 指针。

4. **PerfCount**:
   - 利用 FTQ 中的信息区分 `Branch Direction Misprediction`（方向错误）和 `Branch Target Misprediction`（目标地址错误）。
