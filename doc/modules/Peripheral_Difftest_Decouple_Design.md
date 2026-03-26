# 外设模型解耦设计与实现说明

## 1. 文档目的
本文档给出当前模拟器中外设模型（UART/PLIC）与 Difftest 的解耦方案、实际连线、时序语义和验证结论。

适用范围：
1. `store` 提交后触发的外设行为。
2. `mip/sip` 中断位与 MMIO 副作用的建模。
3. Difftest 准备路径与 DUT 行为路径分离。

## 2. 设计目标
1. 去除“外设行为依赖 Difftest 执行”的耦合。
2. 固定硬连线风格，不使用通用 hook/opaque 回调框架。
3. 保持与参考模型对齐的关键语义：
- 外设中断位（`mip/sip`）在提交语义上生效。
- MMIO 寄存器与 UART 输出行为保持现有测试可通过。
4. 将外设入口收敛到访存子系统边界（`MemSubsystem`）。

## 3. 总体架构
当前外设链路分为两条并行语义路径：

1. 访存生效路径（MMIO side-effect）
`LSU store write -> MemSubsystem -> SimpleCache -> PeripheralModel::on_mem_store_effective`

2. 提交生效路径（架构可见 side-effect）
`Ren commit -> SimContext::run_commit_inst -> simcpu_commit_sync -> MemSubsystem::on_commit_store -> PeripheralModel::on_commit_store`

对应职责：
1. `on_mem_store_effective`：处理 UART 打印、PLIC claim 内存值更新等“写已发生”行为。
2. `on_commit_store`：处理 `mip/sip` 位更新等“提交后可见”行为。

## 4. 关键模块与接口

### 4.1 `SimContext`
1. 固定持有 `SimCpu* cpu`。
2. 提供：
- `run_commit_inst(InstEntry*)`
- `run_difftest_inst(InstEntry*)`

说明：
- `run_commit_inst` 只走 DUT 行为路径。
- `run_difftest_inst` 只走对拍准备与 step/skip。

### 4.2 `PeripheralModel`
接口：
1. `on_mem_store_effective(uint32_t paddr, uint32_t new_val)`
2. `on_commit_store(uint32_t paddr, uint32_t data)`

状态连线：
1. `csr`（用于 `mip/sip` 更新）
2. `memory`（用于 MMIO 寄存器映射值）

### 4.3 `MemSubsystem`
新增外设入口：
1. 内部子模块：`PeripheralModel peripheral`
2. `void on_commit_store(uint32_t paddr, uint32_t data)`
3. 顶层连线输入：`Csr *csr`、`uint32_t *memory`

作用：
- 对外承接提交侧外设事件。
- 对内在 `init()` 时完成 `peripheral` 初始化，并连到 DCache 抽象层。

### 4.4 `AbstractDcache / SimpleCache`
1. `AbstractDcache` 增加 `peripheral_model` 指针。
2. `SimpleCache::handle_write_req` 在写内存后调用 `on_mem_store_effective`。

## 5. 时序与语义细化

### 5.1 UART + PLIC 命令语义
1. 写 `UART_ADDR_BASE`：输出字符，并清数据字节。
2. 写 `UART_ADDR_BASE + 1`：解析命令字节。
- `cmd == 7`：置 `PLIC_CLAIM_ADDR=0xa`，并在提交路径置 `mip/sip` 对应位。
- `cmd == 5`：更新 UART 状态位。
3. 写 `PLIC_CLAIM_ADDR` 且低字节为 `0xa`：claim 清零；提交路径清 `mip/sip` 对应位。

注意：
- 命令字节按地址偏移从 `new_val` 中提取（修复了 byte write 到 `+1` 时取值错误问题）。

### 5.2 Difftest 边界
1. Difftest 不再直接执行外设副作用。
2. Difftest 仅做：
- DUT 架构状态打包（GPR/CSR/PC/store 信息）
- `difftest_skip` 或 `difftest_step`

## 6. 已完成重构项
1. 提交路径与 Difftest 路径拆分。
2. 去除回调式 hook/opaque，改为固定硬连线风格。
3. `PeripheralModel` 独立并引入两阶段接口。
4. 外设入口从 `SimCpu` 收敛到 `MemSubsystem`。
5. `SimpleCache` 中 MMIO 逻辑迁移为调用外设模型，不再散落硬编码。
6. 修复 UART 命令字节提取错误，相关回归已通过。
7. `PeripheralModel` 已下沉为 `MemSubsystem` 内部子模块。

## 7. 当前约束与取舍
1. 当前实现中 store 写请求本身发生在提交相关路径，因此不引入额外事件队列也能满足现有语义。
2. 外设模型仍是“功能模拟”而非完整总线时序模型。
3. 目前仅覆盖 UART/PLIC 相关规则，CLINT 等外设可按同一接口扩展。
4. 当前先采用“MMIO store 提交触发流水线 flush”来收敛可见性窗口，优先保证与参考模型一致。
5. `MMIO load` 暂未做“禁止推测执行”的完整收敛（遗留项，见后续建议）。

## 8. 后续演进建议
1. 将 `PeripheralModel` 从 header 内联实现拆为 `.cpp`，减少编译耦合。
2. 为 `PeripheralModel` 增加统一设备分发表（按地址窗口分派）。
3. 若未来引入“写生效早于提交确认”的实现，再增加可选事件队列（effective->commit 对账）。
4. 补充针对 UART 命令字节偏移、PLIC claim、`mip/sip` 时序的定向回归用例。
5. 增加“MMIO load 禁止推测执行”机制（例如在 LSU 对 MMIO load 加入保守阻塞），并补充对应对拍用例。

## 9. 验证结论
当前版本已通过用户现有测试，关键结论：
1. 先前由外设路径引起的 Difftest 寄存器偏差已消除。
2. 断言触发问题与 `diag_val` 时机误用已修正（仅在 load 完成且非异常时检查对齐）。
3. 外设与 Difftest 已实现逻辑解耦，且边界清晰可维护。
