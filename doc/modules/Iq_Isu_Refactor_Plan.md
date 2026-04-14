# IQ/ISU IO 化重构计划

## 1. 目标
- 为 `IssueQueue` 明确 `in/out` 接口。
- `Isu` 与 `IssueQueue` 只通过 IO 交互，不再直接访问 `entry/count` 等内部状态。
- 保持行为不变（发射、唤醒、flush/mispred/clear 顺序不变）。

## 2. 验证约束
- 每一步都执行：
  1. `make clean`
  2. `make small -j`
  3. `./build/simulator ./dhrystone.bin`
- 校验 `instruction num` 和 `cycle num` 与基线一致。

## 3. 分步实施

### Step 1：给 IssueQueue 增加 IO 壳层（不改核心算法）
- 新增 `IssueQueueIn/IssueQueueOut`。
- 新增 `comb_begin/comb_enq/comb_wakeup/comb_issue/comb_flush/seq`。
- 内部仍复用原有 `enqueue/wakeup/schedule/commit_issue/flush_xxx` 逻辑。

### Step 2：Isu 切换为 IO 交互
- `comb_begin` 改为调用 `q.comb_begin()`。
- `comb_enq` 通过 `q.in.enq_reqs` 驱动入队。
- `comb_issue` 通过 `q.in.{issue_block,port_ready,port_fu_ready_mask}` 驱动并读取 `q.out.issue_grants`。
- `comb_awake/comb_flush` 通过 `q.in` 驱动，调用对应 `q.comb_xxx()`。

### Step 3：收口与清理
- 删除 `Isu` 中对 IQ 内部实现的残余依赖。
- 视情况将 `IssueQueue` 内部状态改为 `private`（若无外部依赖）。
- 同步 `Isu_Design.md` 文档到“IO 交互”语义。

## 4. 风险点
- 同拍顺序风险：`comb_begin -> comb_enq/comb_awake/comb_issue -> comb_flush -> seq` 不能改变。
- 发射筛选风险：`port_ready` 与 `fu_ready_mask` 必须在 IQ 发射确认时保留原条件。
- 清理优先级风险：`flush > mispred > clear_mask` 必须保持。

## 5. 当前进度
- [x] Step 1
- [x] Step 2（首版）
- [x] Step 3
