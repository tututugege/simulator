# RISC-V 原子指令 (LR/SC/AMO) 重构方案

本文给出一个可以直接落地的 `RV32A` 重构计划，目标是优先修复正确性问题（`SC` 误成功），并保证可逐步回归。

## 1. 问题定义

当前核心问题是：
- 在乱序流水里，`SC` 可能在语义上尚未判定时就被“提前成功”写回。
- `LR/SC` reservation 生命周期与 `SC` 成败判定没有在同一个模块统一管理。
- `SC` 失败场景下，可能仍然出现错误 store 副作用。

已复现用例：
- `./build/simulator --mode fast -f 186000000 ../image/mem/spec_mem/rv32imab_test/429.mcf_test.bin`

## 2. 设计原则

1. `SC` 成败判定统一放在 LSU。
2. `SC` 无论成败都清 reservation。
3. `SC` 的寄存器结果 (`rd=0/1`) 必须由 LSU 最终产生，不能提前写回。
4. 失败 `SC` 不允许产生内存写副作用。
5. 为了避免乱序超车，采用 `ROB-empty` 串行化分派原子指令（性能换正确）。

## 3. 总体重构路径

采用四阶段改造，每一步都可单独提交并回归。

### Step 0: 基线冻结

- 目的：在改动前固定可复现输入，避免回归标准漂移。
- 操作：
  - 保留当前复现命令。
  - 记录失败点：`pc=0x0007b524, inst=0x1ac525af (sc.w)`。
- 验收：
  - 现网行为仍能稳定复现 difftest mismatch。

### Step 1: Dispatch 串行化原子指令

- 修改点：
  - `Dispatch` 检测到 `AMO/LR/SC` 时：
    - 若 `rob_empty == false`，阻塞该条及后续指令发包。
    - 仅当 `rob_empty == true` 时允许原子指令进入后端。
  - 增加 `rob_empty` 信号从 ROB 到 Dispatch 的路径（若现有路径不完整）。
- 目标：
  - 彻底去掉“LR 与后续 SC 在不同队列并发竞争发射”的超车来源。
- 验收：
  - 原子指令前后不再与普通指令交错发射。
  - 性能下降可接受（正确性优先）。

### Step 2: LSU 接管 LR/SC reservation

- 修改点：
  - 在 LSU 中集中维护：
    - `reserve_valid`
    - `reserve_addr`
  - `LR` 执行后：
    - 写入 `reserve_valid=true`
    - 写入 `reserve_addr=lr_paddr`
  - `SC` 执行时：
    - 成功条件严格为 `reserve_valid && reserve_addr == sc_paddr`
    - 计算 `sc_success`
    - 无论成功失败，`reserve_valid=false`
- 目标：
  - reservation 生命周期只有一个真实来源，避免多模块状态不一致。
- 验收：
  - `SC` 成败与 ref 对齐。

### Step 3: SC 写回语义收敛（去掉提前写回）

- 修改点：
  - 取消 `SC` 在 INT placeholder uop 的提前 `rd` 写回路径。
  - `SC` 的 `rd` 结果由 LSU 最终产生并回写：
    - 成功 `rd=0`
    - 失败 `rd=1`
  - `SC fail` 必须显式抑制 store 请求（`store=0`）。
- 目标：
  - 统一“判定-写回-副作用”时序，避免出现“rd 成功但 store 失败”或反向错配。
- 验收：
  - difftest 中寄存器和 store sideband 同时对齐。

### Step 4: AMO 单体化（可选但建议）

- 修改点：
  - 在 `ROB-empty` 串行化前提下，逐步将 AMO RMW 简化为 LSU 内部单体执行（减少拆分链路）。
  - 对 `amoadd/amoxor/...` 保持与 ref 等价的读改写顺序。
- 目标：
  - 缩减跨模块交互复杂度，降低未来维护成本。
- 验收：
  - `rv32ua` 相关测试通过。

## 4. 每一步的回归清单

每完成一步，至少执行：

1. 关键复现：
   - `./build/simulator --mode fast -f 186000000 ../image/mem/spec_mem/rv32imab_test/429.mcf_test.bin`
2. 原子 ISA：
   - `baremetal/riscv-tests/isa/rv32ua/lrsc`（以及主要 AMO 用例）
3. 冒烟：
   - `./build/simulator ./baremetal/dhrystone/build/dhrystone.bin`
   - `./build/simulator ./baremetal/coremark/build/coremark.bin`

## 5. 风险与兜底

- 风险 1：`ROB-empty` 策略导致性能明显下降。
  - 兜底：先保证 correctness，后续再做 relaxed 版本。

- 风险 2：`SC` 写回路径改造后影响 PRF 唤醒。
  - 兜底：保持 `SC` 结果走现有 LSU->EXU->PRF 写回通道，不新开旁路协议。

- 风险 3：异常/flush 与 reservation 清理竞态。
  - 兜底：在 LSU flush path 中统一清 reservation，并增加断言。

## 6. 完成标准 (Definition of Done)

当满足以下条件时视为重构完成：

1. `mcf` 复现命令不再触发 `SC` 误成功类 difftest。
2. `rv32ua/lrsc` 与主要 AMO 测例稳定通过。
3. `SC` 的 `rd` 回写、store 副作用、reservation 清理三者时序一致且可解释。
4. 文档与实现一致，后续维护者可按此文追踪行为。
