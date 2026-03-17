# 后端模块存储态瘦身计划

## 背景

当前 `back-end` 各模块的“传输接口”和“内部存储”大量复用了 `InstInfo` / `MicroOp`。
这对功能开发方便，但会带来两个问题：

- 模块内部存储宽度明显大于真实需求
- 接口已经逐步硬件化后，模块内部状态仍然保留了过多无关字段，和 RTL 风格不一致

本轮目标不是一次性删除 `InstInfo` / `MicroOp`，而是将“模块内部存储态”逐步改成模块专用 entry，只保留本模块真正会消费或更新的字段。

## 总体原则

1. 传输结构和存储结构分离。
   - 模块边界继续使用现有 IO payload
   - 模块内部状态改为专用 stored entry

2. 优先改深度大、生命周期长、收益高的存储结构。
   - 先改 `ROB`
   - 再改 `IssueQueue`
   - 再改 `LSU`
   - `FU` pipeline 最后评估

3. 先保证语义不变，再继续压缩字段。
   - 第一阶段允许保留少量“未来可能继续收缩”的字段
   - 不为了追求绝对最小而一次性扩大回归范围

## 第一阶段：ROB

### 当前问题

`ROB` 当前直接存 `InstEntry/InstInfo`：

- 文件：`back-end/include/Rob.h`
- 现状：`entry[ROB_BANK_NUM][ROB_LINE_NUM]` 和 `entry_1` 都是 `InstEntry`

但 `ROB` 实际只使用了其中一部分字段：

- 提交释放 RAT：`dest_en`、`dest_areg`、`dest_preg`、`old_dest_preg`
- 提交控制：`type`、`uop_num`、`cplt_num`、`ftq_idx`、`ftq_offset`、`ftq_is_last`
- 异常/flush：`diag_val`、`page_fault_*`、`illegal_inst`、`flush_pipe`
- 分支提交结果：`mispred`、`br_taken`
- store / perf / debug：`stq_idx`、`dbg`
- 少量软件侧提交语义：`src1_areg`、`imm`

明显不需要继续存整包 `InstInfo` 的字段包括：

- `src2_areg`
- `src1_preg/src2_preg`
- `src1_en/src2_en`
- `src1_busy/src2_busy`
- `src1_is_pc/src2_is_imm`
- `br_id/br_mask`
- `csr_idx`
- `ldq_idx`
- `rob_flag`

### 执行方案

引入 `RobStoredInst` / `RobStoredEntry`：

- `RobStoredInst::from_dis_rob_inst()`
- `RobStoredInst::to_commit_inst(rob_idx)`

`ROB` 内部状态改为仅存 `RobStoredEntry`，但外部 `RobCommitIO` 暂时不改。

### 验证重点

- 提交释放 RAT 正常
- CSR / 异常 / flush 正常
- branch mispred 提交信息正常
- load/store 提交及对齐检查正常
- difftest 提交路径不回退

## 后续阶段

### 第二阶段：IssueQueue

目标：不再在 IQ 内部存整包 `MicroOp`，只保留调度、唤醒和发射所需字段。

### 第三阶段：LSU

目标：`LDQ`、完成队列、重试队列改为 load/store 专用 entry，不再存整包 `MicroOp`。

### 第四阶段：FU Pipeline

目标：评估固定延迟流水线和迭代单元是否值得拆成执行结果专用 entry。

## 本轮范围

本轮只执行第一阶段：

- 新增文档
- 完成 `ROB` 存储态从 `InstInfo` 到专用 entry 的迁移
- 编译并跑基础回归
