# MemSubsystem 层次化重构规划（MemPtwBlock 合并版）

## 1. 背景
当前 `MemSubsystem` 同时承载了以下多类职责：
1. LSU/PTW 读请求仲裁。
2. Shared PTW walk 状态机。
3. PTW 客户端请求/回包状态。
4. DCache 回包 owner 路由。
5. 外设提交入口与 side effect 转发。

这些逻辑集中在单一类中，导致：
1. 类体积过大，可读性下降。
2. 状态归属不清晰，修改易相互影响。
3. 缺少硬件层次化结构，不利于后续扩展。

## 2. 重构目标
1. 将 `MemSubsystem` 改为“总装顶层”，只做连线与调度。
2. 将 PTW 客户端与 walk FSM 合并为统一 PTW 子模块（`MemPtwBlock`）。
3. 将仲裁与回包路由保持为独立子模块，形成清晰硬件边界。
4. 保持行为等价，不引入时序语义变化。
5. 支持分阶段迁移：每一步都可编译、可回归。

## 3. 顶层结构（目标）

### 3.1 顶层模块：`MemSubsystem`
职责：
1. 持有外部端口（LSU、ITLB/DTLB PTW 端口）。
2. 持有子模块实例。
3. 在 `init/comb/seq` 中按阶段调度子模块。
4. 负责顶层参数和连线完整性断言。

非职责：
1. 不直接维护 PTW 客户端状态。
2. 不直接维护 walk 细节状态。
3. 不直接维护 owner 队列细节。

### 3.2 子模块划分（合并后）
1. `MemPtwBlock`（合并 `MemPtwClientBlock + MemWalkBlock`）
2. `MemReadArbBlock`
3. `MemRespRouteBlock`
4. `PeripheralModel`（已存在，保持为子模块）

## 4. 子模块权责

### 4.1 `MemPtwBlock`
职责：
1. 管理 DTLB/ITLB PTW 客户端请求寄存。
2. 管理 shared walk FSM（L1/L2）。
3. 产生 PTW 访存请求（普通 PTW 读 + walk 页表读）。
4. 消费来自回包路由的 PTW/Walk 回包。
5. 对外提供 ITLB/DTLB 的响应可见状态。
6. 处理 flush 行为（清 pending/inflight/resp 与 drop credit）。

输入：
1. ITLB/DTLB client 请求（read/walk/consume/flush）。
2. 来自 `MemRespRouteBlock` 的 PTW 回包与 walk 回包。

输出：
1. 发往 `MemReadArbBlock` 的候选请求：
- `ptw_mem_read_req`（DTLB/ITLB 普通 PTW 读）
- `walk_mem_read_req`（shared walk 当前拍页表读）
2. 对 ITLB/DTLB 端口的 resp_valid/resp_data。

状态归属：
1. `ptw_clients[]`
2. `walk_clients[]`
3. `walk_state`
4. `walk_active`
5. `walk_owner`
6. `walk_l1_pte`
7. `walk_drop_resp_credit`

### 4.2 `MemReadArbBlock`
职责：
1. 对 LSU/PTW/walk 的读请求进行优先级仲裁。
2. 生成发往 DCache 的单一读请求通道。
3. 生成 owner tag enqueue 信号。

输入：
1. LSU 读请求。
2. `MemPtwBlock` 的普通 PTW 读请求。
3. `MemPtwBlock` 的 walk 读请求。

输出：
1. DCache read req。
2. owner enqueue 控制。
3. 对 `MemPtwBlock` 的 grant/accepted 反馈。

建议策略（与现状一致）：
1. LSU-first
2. shared-walk
3. PTW_DTLB
4. PTW_ITLB

### 4.3 `MemRespRouteBlock`
职责：
1. 维护 owner 队列。
2. 将 DCache 回包按 owner 路由到 LSU/PTW/walk。
3. 处理 flush 后回包丢弃语义（依据 `MemPtwBlock` 提供的控制）。

输入：
1. DCache 回包。
2. owner enqueue。

输出：
1. LSU `MemRespIO`。
2. PTW 回包到 `MemPtwBlock`。
3. Walk 回包到 `MemPtwBlock`。

状态归属：
1. `read_owner_q`

### 4.4 `PeripheralModel`
职责：
1. 访存生效路径 `on_mem_store_effective`。
2. 提交生效路径 `on_commit_store`。

保持不变：
1. 作为 `MemSubsystem` 子模块。
2. 通过 DCache 抽象层触发 effective。
3. 通过 `MemSubsystem::on_commit_store` 触发 commit。

## 5. 内部接口规划（当前实现）
当前实现采用“直连函数接口 + 顶层显式赋值”的硬件化风格，不再保留额外的中间 IO 壳层（如 `MemSubsystemIO.h`）。

规则：
1. 顶层 `MemSubsystem` 负责显式连线与调用顺序。
2. 子模块仅暴露最小必要接口（请求可见、仲裁、回包路由）。
3. 避免为了抽象而抽象，保持数据通路直观可追踪。

## 6. 时序组织建议

### 6.1 `MemSubsystem::comb()`
顺序建议：
1. 采集输入并清默认输出。
2. `MemPtwBlock::comb_req()`
3. `MemReadArbBlock::comb()`
4. 驱动 DCache `comb()`
5. `MemRespRouteBlock::comb_route()`
6. `MemPtwBlock::comb_resp()`（消费 route 回包）
7. 将内部输出回写外部端口。

### 6.2 `MemSubsystem::seq()`
顺序建议：
1. `dcache->seq()`
2. 各子模块 `seq()`（如有时序态）。

## 7. 迁移步骤（渐进）

### Phase A：子模块壳体落地（不搬逻辑）
1. 新增 `MemPtwBlock / MemReadArbBlock / MemRespRouteBlock` 类壳。
2. `MemSubsystem` 中实例化并接线。
3. 行为保持完全不变。

当前状态：
1. Phase A 已完成并已收口，不再保留仅用于过渡的中间 IO 壳。
2. 主数据通路已完成后续迁移（见 Phase B/C/D）。

### Phase B：搬运 PTW 逻辑到 `MemPtwBlock`
1. 将 `ptw_clients[]/walk_clients[]/walk_*` 逻辑迁入。
2. 校验 PTW miss/flush 场景。

当前状态：
1. Phase B 已完成：PTW client 状态与 shared walk FSM 已迁入 `MemPtwBlock`。
2. `MemSubsystem` 仅通过 `MemPtwBlock` 接口访问 PTW 状态与行为。

### Phase C：搬运 RespRoute
1. 将 owner queue 与回包分发迁入 `MemRespRouteBlock`。
2. 校验 LSU/PTW 回包归属正确。

当前状态：
1. Phase C 已完成：owner 队列与回包路由逻辑已迁入 `MemRespRouteBlock`。
2. `MemSubsystem` 不再直接维护 `read_owner_q`，改为调用 `resp_route_block` 接口。

### Phase D：搬运 ReadArb
1. 将仲裁策略迁入 `MemReadArbBlock`。
2. 校验 grant/blocked/perf 统计一致。

当前状态：
1. Phase D 已完成：LSU/PTW/walk 读仲裁策略已迁入 `MemReadArbBlock`。
2. `MemSubsystem` 改为消费 `arbitrate(...)` 结果并触发后续动作。

### Phase E：冗余收口与接口瘦身
1. 删除未使用的中间 IO 壳（`MemSubsystemIO.h` 及相关 `in/out` 成员）。
2. 删除空壳生命周期函数（无状态模块的 `init/comb/seq`）。
3. 保留必要的有状态边界（如 `MemRespRouteBlock::init()` 清空 owner 队列）。

当前状态：
1. Phase E 已完成：`MemSubsystem`、`MemPtwBlock`、`MemReadArbBlock`、`MemRespRouteBlock` 的接口已最小化。
2. 代码形态更接近“顶层连线 + 子模块功能块”的硬件层次化结构。

## 8. 验证策略
每一 Phase 完成后执行：
1. 编译：`make -j4`
2. 基础回归（含 MMIO/UART/PLIC 场景）
3. Difftest 对比
4. 性能计数关键项对比：
- `ptw_* req/grant/resp/blocked/wait_cycle`

## 9. 代码风格约束
1. 模块内状态最小化，禁止跨模块直接写对方私有状态。
2. 注释统一中文，说明“时序语义/边界条件”。
3. 顶层只保留调度与连线，不承载复杂算法。

## 10. 预期收益
1. 层次清晰，符合硬件模块化思维。
2. PTW 相关状态统一归口，避免 client/walk 分裂带来的边界复杂度。
3. 后续扩展（新仲裁策略/新外设/新缓存模型）成本可控。
