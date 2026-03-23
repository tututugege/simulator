# PR #13 合并后审查报告

本文总结了 `Merge PR #13` 引入的、位于 DCache / LSU / MemSubsystem 主体实现之外的潜在风险改动。

本次审查范围：
- `diff/*`
- `back-end/*` 中除 LSU 核心逻辑外的部分
- `include/*` 与 simulator 顶层胶水代码
- 即使不属于内存子系统主体、但可能影响行为的运行时 / 配置变更

参考合并提交：
- `e7cc2a4`（`Merge PR #13`）

## 1. 非 BPU 模式下的 Difftest Page Fault 预期被硬编码关闭

当前状态：
- 已在本地修复。
- 修复方式：
  - 恢复 [include/config.h](/home/tututu/qimeng/simulator/include/config.h#L73) 中的 `CONFIG_BPU`
  - 将 [diff/diff.cpp](/home/tututu/qimeng/simulator/diff/diff.cpp#L225) 的 `difftest_step()` 改为直接传递真实的 `dut_cpu.page_fault_*`
  - 去掉易误用的四 `bool` setter，并在 [ref.cpp](/home/tututu/qimeng/simulator/diff/ref.cpp#L1708) 的 `va2pa_fix()` 中对 `ref_only` 路径做特殊处理
- 处理原则：
  - 这是恢复 `Merge PR #13` 之前主线的正确 difftest 语义，不是用来掩盖内存系统问题的临时绕过

相关文件：
- [diff/diff.cpp](/home/tututu/qimeng/simulator/diff/diff.cpp#L225)
- [include/config.h](/home/tututu/qimeng/simulator/include/config.h#L73)

原始问题：
- `CONFIG_BPU` 被注释掉了。
- 在 `difftest_step(bool check)` 中，非 BPU 分支曾经固定执行：
  - `ref_cpu.set_dut_page_fault_expect(true, false, false, false);`

风险原因：
- 在非 BPU 模式下，difftest 不再把 DUT 的 page-fault sideband 信息传给 REF 模型。
- 这会直接制造如下形式的报错：
  - `DUT has no page fault while REF has page fault`
- 即使 DUT 实际上真的产生了 page fault，REF 侧的比较路径也会被强制认为 DUT 没有 fault。

已观察到的影响：
- 这是当前 Linux page-fault difftest 失败的一级嫌疑点。

结论：
- 这一项应视为已修复，但仍需要结合 Linux / spec 运行结果确认：修复后若仍有 page fault 相关分叉，则根因应继续回到内存可见性、ROB 提交语义或 oracle 行为上。

## 2. Oracle 的 Refetch 路径现在会静默与 DUT 重新同步

当前状态：
- 已在本地修复。
- 修复方式：
  - 删除 [br_oracle.cpp](/home/tututu/qimeng/simulator/diff/br_oracle.cpp#L36) 中将 oracle 架构态整体同步到 DUT 的逻辑
  - refetch 时仅保留最小控制态同步：`pc`、`privilege`、以及前端/MMU 所需 CSR
  - 如果 refetch 后发现 GPR 与 DUT 不一致，恢复为直接报错，不再静默掩盖
- 处理原则：
  - oracle 不再尝试“追平”已经发生分叉的架构态，避免用错误同步掩盖真实 bug
  - 对于会合法导致前后端翻译视图分叉的程序，应视为 oracle 不适用的场景，而不是在 oracle 内部偷偷修正状态

相关文件：
- [diff/br_oracle.cpp](/home/tututu/qimeng/simulator/diff/br_oracle.cpp#L105)

原始问题：
- 在 refetch 时，oracle 状态不再被当作绝对可信。
- 如果 oracle 的 GPR 状态与 DUT 不一致，现在会调用 `sync_oracle_arch_state_from_dut(...)`，而不是直接断言失败。

风险原因：
- 这会掩盖真实的前端 / 架构态分叉。
- 以前可以直接暴露出来的不一致，现在可能会因为 oracle 被拉回 DUT 状态而消失。
- 这会降低 oracle 检查本身的诊断价值。

已观察到的影响：
- 可以解释为什么有些情况下“关掉某个检查后问题似乎消失了”。
- 问题可能仍然存在，只是 oracle 不再报告最早的分叉点。

结论：
- 这一项应视为已修复。
- 当前 oracle 的定位应收缩为“简单前端验证器”，不负责处理那些会合法产生页表/翻译分叉、且没有显式同步动作（如 `sfence.vma`）的程序。

## 3. Commit 时刻的 Store Mirror 被移除了

相关文件：
- [rv_simu_mmu_v2.cpp](/home/tututu/qimeng/simulator/rv_simu_mmu_v2.cpp#L214)

改动内容：
- 旧的 commit 时路径：
  - `mem_subsystem.on_commit_store(e.p_addr, e.data, e.func3);`
  已被整体注释掉。

风险原因：
- Store 的可见性现在完全依赖新的 DCache / WB / AXI 路径。
- 这条路径上的任何延迟或不一致，都不再会被 commit-side 的 memory mirror 掩盖。
- 对 Linux 下的页表写入尤其危险，因为翻译逻辑可能会在 store commit 后很短时间内就读取刚写入的 PTE。

已观察到的影响：
- 提高了 DUT / REF 在页表内容上出现短暂不一致的概率。
- 也让当前的 page-fault mismatch 以及之前的 AXI / writeback 问题显得更加合理。

建议：
- 先确认移除这条路径是不是有意且架构上必须如此。
- 如果是，就需要专门对页表可见性和 store ordering 做压力测试。

## 4. ROB 提交策略发生了会影响异常时序的变化

相关文件：
- [back-end/Rob.cpp](/home/tututu/qimeng/simulator/back-end/Rob.cpp#L142)
- [back-end/Rob.cpp](/home/tututu/qimeng/simulator/back-end/Rob.cpp#L227)

改动内容：
- 新增了 ROB 头部广播：
  - `head_valid`
  - `head_rob_idx`
  - `head_incomplete_valid`
  - `head_incomplete_rob_idx`
- 新增了 `progress_single_commit` fallback：
  - 当 group commit 被阻塞时，允许最老的 ready 且非 flush 指令单独提交

风险原因：
- 这不只是可观测性增强，而是实际改变了 retirement 行为。
- ROB 队头附近的异常、interrupt、MMIO、page-fault 时序都可能发生变化。
- 同一 ROB line 内的 faulting instruction 与年轻指令之间的相互作用也可能改变。

已观察到的影响：
- 可能导致“控制流看起来大体正确，但 fault sideband 对不上”这一类现象。

建议：
- 重点审查 `progress_single_commit` 在 page fault、interrupt、以及与 store replay 相关场景下是否安全。
- 重新检查 difftest 对提交顺序的假设是否仍然成立。

## 5. Difftest / Commit Sideband 在多个结构中被重复展开

相关文件：
- [back-end/include/types.h](/home/tututu/qimeng/simulator/back-end/include/types.h#L139)
- [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L102)
- [rv_simu_mmu_v2.cpp](/home/tututu/qimeng/simulator/rv_simu_mmu_v2.cpp#L277)

改动内容：
- 在已有 debug metadata 之外，又新增了一批平铺字段：
  - `instruction`
  - `pc`
  - `mem_align_mask`
  - `difftest_skip`
  - `inst_idx`
  - `is_cache_miss`
- 这些字段会经过多个 IO 转换辅助函数反复拷贝。

风险原因：
- 现在同一份逻辑信息存在多种表示。
- 某些路径可能读 `dbg.instruction`，另一些路径则读平铺的 `instruction`。
- 只要有一次拷贝遗漏或更新不完整，就可能在 commit / difftest 时产生静默不一致。

已观察到的影响：
- 增加了 page-fault sideband、commit PC、或 difftest skip 元数据陈旧或部分更新的风险。

建议：
- 能减少重复就尽量减少。
- 如果必须保留重复字段，就需要逐个审查所有 conversion helper 是否完整且一致。

## 6. Simulator 顶层被重新接到了外部 AXI 组件上

相关文件：
- [include/SimCpu.h](/home/tututu/qimeng/simulator/include/SimCpu.h#L36)
- [rv_simu_mmu_v2.cpp](/home/tututu/qimeng/simulator/rv_simu_mmu_v2.cpp#L302)
- [main.cpp](/home/tututu/qimeng/simulator/main.cpp#L60)

改动内容：
- 新增了显式的顶层 AXI interconnect / router / DDR / MMIO 对象。
- 在 `rv_simu_mmu_v2.cpp` 中增加了 `MemSubsystem` 与 AXI 之间的胶水逻辑。
- 在 `main.cpp` 中增加了基于 signal handler 的 debug dump 路径。

风险原因：
- simulator 现在依赖顶层跨模块握手时序。
- difftest、commit 语义、以及 memory visibility 不再只是 MemSubsystem 内部问题。
- 即使 DCache / LSU 内部逻辑正确，桥接代码本身也可能引入 bug。

已观察到的影响：
- 这与长时间 Linux / spec 运行中观察到的 AXI read timeout 和 writeback verification failure 是一致的。

建议：
- 应把顶层 AXI bridge 逻辑视为 correctness-critical 的 memory pipeline 一部分，而不是普通 simulator plumbing。

## 7. Commit / Perf / Debug 基础设施被大幅扩展

相关文件：
- [back-end/Ren.cpp](/home/tututu/qimeng/simulator/back-end/Ren.cpp#L255)
- [back-end/include/PerfCount.h](/home/tututu/qimeng/simulator/back-end/include/PerfCount.h#L11)
- [back-end/Exu/Csr.cpp](/home/tututu/qimeng/simulator/back-end/Exu/Csr.cpp#L343)
- [include/DeadlockDebug.h](/home/tututu/qimeng/simulator/include/DeadlockDebug.h#L1)
- [include/DebugPtwTrace.h](/home/tututu/qimeng/simulator/include/DebugPtwTrace.h#L1)
- [include/DiffMemTrace.h](/home/tututu/qimeng/simulator/include/DiffMemTrace.h#L1)
- [include/DeadlockReplayTrace.h](/home/tututu/qimeng/simulator/include/DeadlockReplayTrace.h#L1)

改动内容：
- 增加了大量 deadlock、PTW、replay、memory trace 基础设施。
- 增加了更多 perf counter 和跟踪路径。

风险原因：
- 这些大多是诊断功能，理论上应该是低风险的。
- 但如果出现下面几类情况，instrumentation 仍然可能影响行为：
  - 状态被错误复用
  - logging hook 与功能逻辑绑得过紧
  - 其他路径默认假设 trace metadata 一定有效

已观察到的影响：
- 这些 tracing 已经对定位问题有帮助。
- 目前没有直接证据表明它们是根因，但它们确实增加了脆弱路径周围的代码表面积。

建议：
- 回退优先级较低。
- 可以先保留，只有在核心功能路径被排除后，再把它们当作次级嫌疑点处理。

## 8. 核心配置改动不止内存子系统

相关文件：
- [include/config.h](/home/tututu/qimeng/simulator/include/config.h#L73)
- [include/config.h](/home/tututu/qimeng/simulator/include/config.h#L360)

改动内容：
- `CONFIG_BPU` 被关闭
- `PRF_NUM` 从 `160` 增加到 `256`
- `ROB_NUM` 从 `128` 增加到 `256`
- 新增了许多 debug / perf 宏
- `CONFIG_BE_IO_CLEAR_AT_COMB_BEGIN` 被强制开启

风险原因：
- `Merge PR #13` 并不是一次纯粹的 memory-only 变更。
- 前端模式、后端资源深度、组合逻辑清理策略都同时发生了变化。
- 这会显著增加回归归因的难度。

已观察到的影响：
- 即使不看显式 LSU 代码，这些改动也可能改变时序、队列压力、replay 压力和 commit 行为。

建议：
- 如果后续需要缩小排查范围，可以按以下维度做 bisect：
  - BPU 开 / 关
  - 旧 / 新 ROB 大小
  - 旧 / 新 PRF 大小
  - BE IO clear 开 / 关

## 9. BackTop / Backend 接线切换到了新的 LSU-DCache 总线类型

相关文件：
- [back-end/BackTop.cpp](/home/tututu/qimeng/simulator/back-end/BackTop.cpp#L28)
- [back-end/include/BackTop.h](/home/tututu/qimeng/simulator/back-end/include/BackTop.h#L101)
- [back-end/include/IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h#L1398)

改动内容：
- `SimpleLsu` 被替换成了 `RealLsu`。
- 旧的 request / response 结构被新的成组 LSU↔DCache 接口替换。
- 在共享头文件中增加了 MMIO / 外设相关的 IO 结构和 queue entry 类型。

风险原因：
- 这不只是“LSU 内部实现变化”，而是改动了 backend 可见的接口契约。
- fault、replay、memory-completion 的语义现在依赖更大的 IO 面。

已观察到的影响：
- 在审查 page fault / load / store 完成元数据如何传到 ROB 和 difftest 时，这一部分是直接相关的。

建议：
- 重点审查接口级不变量，尤其是：
  - page-fault 传播
  - replay 完成语义
  - ROB 队头的 MMIO gating

## 当前优先级排序

当前仍需优先关注的部分如下：

1. 移除 commit-time store mirror 暴露了 memory-visibility 分叉
2. ROB 在 single/group commit 与 head 语义附近的行为发生变化
3. debug / IO / types 路径中的 sideband 元数据重复展开

## 建议审查顺序

1. 重新评估移除 `on_commit_store` mirror 是否合理。
2. 审查 ROB single-commit fallback 与 page-fault / interrupt 顺序之间是否冲突。
3. 继续检查重复 sideband 字段在 commit / difftest 路径上的一致性。

## 附录：合并前 SimpleLsu 与当前 RealLsu 的 MMIO 处理差异

说明：
- 这一节超出本报告最初“排除 DCache/LSU/MemSubsystem 主体”的范围。
- 之所以补充，是因为当前 oracle 模式下的卡死 / refetch 缺失，已经高度怀疑与 MMIO 的 `flush/refetch` 语义变化有关。

### 合并前 SimpleLsu 的行为

相关文件：
- [SimpleLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/SimpleLsu.cpp)

MMIO load：
- 在地址翻译成功后，会判断 `is_mmio_addr(p_addr)`。
- 一旦是 MMIO，直接设置：
  - `task.flush_pipe = is_mmio;`
- 同时关闭 Store-to-Load Forwarding：
  - `is_mmio ? StoreForwardResult{} : check_store_forward(...)`
- 也就是说，MMIO load 在旧实现里天然被视为序列化事件，会要求后端刷流水线。

MMIO store：
- 在 store 地址翻译成功后，同样判断 `is_mmio_addr(pa)`。
- 一旦是 MMIO，直接设置：
  - `success_op.flush_pipe = is_mmio;`
- 然后把该 STA 完成项送入 `finished_sta_reqs`。
- 也就是说，MMIO store 在旧实现里同样会触发 `flush_pipe`。

旧实现的整体特征：
- MMIO load 和 MMIO store 都统一通过 `flush_pipe` 进入 ROB / 前端的 flush-refetch 机制。
- 语义简单粗暴，但对 oracle 很友好：遇到 MMIO 后，前端会停供并等待 refetch 重新对齐。

### PR #13 合并后的 RealLsu 原始行为

相关文件：
- [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp)

MMIO load：
- 仍然会判断 `is_mmio_addr(p_addr)`。
- 仍然会关闭 Store-to-Load Forwarding。
- 但原本旧实现里的：
  - `task.flush_pipe = is_mmio;`
  - `entry.uop.flush_pipe = is_mmio;`
  现在都被注释掉了。
- 取而代之的是：
  - `ldq[...].is_mmio_wait = is_mmio;`
- 然后 MMIO load 只有在“成为 ROB 队头行中最老未完成指令”时，才会通过 `peripheral_io` 发出请求，并等待响应。

MMIO store：
- 当前实现同样单独走 `peripheral_io` 外设通道，不再复用简单的 cache/store 完成路径。
- 更关键的是，代码显式写成了：
  - `success_op.flush_pipe = false;`
- 注释里也明确说明：
  - 这样做是为了避免 STA writeback 过早触发 ROB 全局 flush，导致 LSU 还没消费 `rob_commit` 就把 STQ commit 弄丢。

当前实现的整体特征：
- MMIO load/store 已经不再统一通过 `flush_pipe` 驱动前端 refetch。
- load 依赖 `is_mmio_wait + ROB 队头 gating + peripheral response` 完成。
- store 则显式禁止 `flush_pipe`。

### 关键差异与风险

1. 合并前 `SimpleLsu` 把 MMIO 统一视为 flush/refetch 事件；当前 `RealLsu` 不再这样做。

2. oracle 模式下前端是否 refetch，取决于：
   - `back.out.mispred || back.out.flush`
   - 而 `back.out.flush` 最终来自 ROB 是否看到了 `uop.flush_pipe`

3. 因此，在当前实现里：
   - 如果 MMIO 没有触发 `flush_pipe`
   - 但 oracle 因为碰到 `is_mmio_load/is_mmio_store` 进入 `stall`
   - 就可能出现“oracle 停止供指，但 DUT 没有给 refetch”的卡死

4. 这意味着：
   - 当前 oracle 卡死问题，极有可能不是 oracle 本身坏了
   - 而是 `SimpleLsu -> RealLsu` 切换后，MMIO 的 flush/refetch 语义发生了实质变化

### 已实施的临时修复

当前状态：
- 已在本地做一轮临时修复，目标是先恢复与合并前更接近的 oracle / MMIO load 交互语义。

本轮修复内容：
- 在 [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp) 中恢复了 MMIO load 的：
  - `task.flush_pipe = is_mmio;`
  - `entry.uop.flush_pipe = is_mmio;`
- 在 [br_oracle.cpp](/home/tututu/qimeng/simulator/diff/br_oracle.cpp) 中，将 oracle 的 stall 条件改成：
  - 只对 `exception / csr / mmio_load` 停供
  - 不再因为 `mmio_store` 进入 stall

本轮修复后的判断：
- `MMIO load` 不触发 `flush_pipe`，基本可以确认是一个问题，且已按旧语义修回。
- `MMIO store` 暂未恢复旧语义，仍保持“不触发 flush”，因为当前 `RealLsu` 里存在明确注释和时序约束，表明直接恢复旧行为可能打坏 `STQ / rob_commit`。

验证结果：
- 编译通过。
- oracle 模式下，之前怀疑的 “MMIO 触发 stall 但 DUT 不给 refetch” 这一类问题被部分缓解。
- 但 Linux 启动仍会在后续周期撞上：
  - `DUT has no page fault while REF has page fault`
- 说明 MMIO load/refetch 语义不是当前 page-fault 分叉的唯一根因，后续仍应继续排查后端 page fault 的产生与传递链路。

### 当前判断

- 如果目标是保持 oracle 模式与合并前行为一致，那么 `RealLsu` 至少在 MMIO load 上需要重新审视 `flush_pipe`。
- MMIO store 不能简单照抄旧实现，因为当前代码里已经明确暴露出一个新的约束：
  - 过早 flush 可能打断 STQ / `rob_commit` 的消费时序。
- 因此，MMIO load 和 MMIO store 应分开看：
  - load 更像“漏掉了旧语义”
  - store 更像“为了规避新时序问题而故意改变语义”
