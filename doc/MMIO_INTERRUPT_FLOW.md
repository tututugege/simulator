# 当前 MMIO 与中断处理流程说明

本文描述的是当前仓库里的实际实现，不是理想设计。

## 1. MMIO 总体结构

当前 MMIO 有两条路径：

1. 本地截断路径
   - 由 `PeripheralModel` 处理。
   - 当前覆盖 `UART/PLIC` 地址段，以及 OpenSBI timer 特殊读。
   - 这部分请求不会发到外部 AXI。

2. 外部 AXI 路径
   - 由 `PeripheralAxi` 把请求转成 AXI read/write。
   - 只用于“未被本地截断”的 MMIO 请求。

对应模块关系：

- LSU 侧接口定义在 [IO.h](/home/tututu/qimeng/simulator/back-end/include/IO.h)
  - `PeripheralInIO`
  - `PeripheralOutIO`
  - `PeripheralIO`
- LSU 发起请求：
  - [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp)
- MemSubsystem 连接本地模型和外设 AXI bridge：
  - [MemSubsystem.cpp](/home/tututu/qimeng/simulator/MemSubSystem/MemSubsystem.cpp)
  - [PeripheralAxi.cpp](/home/tututu/qimeng/simulator/MemSubSystem/PeripheralAxi.cpp)
  - [PeripheralModel.h](/home/tututu/qimeng/simulator/MemSubSystem/include/PeripheralModel.h)

## 2. MMIO 地址何时被识别

### 2.1 MMIO load

Load 在地址翻译完成后，由 LSU 判断物理地址是否为 MMIO：

- [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp)
  - `handle_load_req()`
  - `progress_ldq_entries()`
  - `is_mmio_addr()`

识别为 MMIO 后：

- `ldq[idx].is_mmio_wait = true`
- 不做 store-to-load forwarding
- 不走普通 DCache load 请求
- 等待后续专门的 MMIO 发射条件满足

### 2.2 MMIO store

Store 在 STA 地址翻译成功后识别：

- [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp)
  - `finish_store_addr_once()`

识别为 MMIO 后：

- `stq[idx].is_mmio = true`
- 不触发 `flush_pipe`
- 后续由 STQ 单独走 MMIO store 发射逻辑

## 3. MMIO load 处理流程

### 3.1 LSU 侧发射条件

MMIO load 不是地址一出来就发，而是要等到比较严格的条件：

- [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp)

当前条件包括：

1. 该 load 已经完成地址翻译，且 `is_mmio_wait == true`
2. 当前没有其他 MMIO inflight
3. `peripheral_io.out.ready == true`
4. 这条 load 之前没有更老的 store 仍阻塞它
5. 这条 load 必须是 ROB 当前最老未提交指令
   - 使用 `in.rob_bcast->head_rob_idx`

满足后，LSU 生成 `PeripheralInIO`：

- `is_mmio = 1`
- `wen = 0`
- `mmio_addr = paddr`
- `uop = load uop`

然后放入 `pending_mmio_req`，由 `comb_recv()` 在 `peripheral_io.out.ready` 时真正送给 `PeripheralAxi`。

### 3.2 PeripheralAxi 如何决定是否发 AXI

逻辑在 [PeripheralAxi.cpp](/home/tututu/qimeng/simulator/MemSubSystem/PeripheralAxi.cpp) 的 `comb_inputs()`：

#### 情况 A：OpenSBI timer 特殊读

地址：

- `OPENSBI_TIMER_LOW_ADDR`
- `OPENSBI_TIMER_HIGH_ADDR`

处理方式：

- 直接本地返回
- 不发 AXI
- `uop.dbg.difftest_skip = true`

返回数据来源：

- `CONFIG_BPU` 打开时返回 `sim_time`
- 否则返回 `get_oracle_timer()`

#### 情况 B：被 `PeripheralModel` 覆盖的 MMIO

判定函数：

- [PeripheralModel.h](/home/tututu/qimeng/simulator/MemSubSystem/include/PeripheralModel.h)
  - `PeripheralModel::is_modeled_mmio()`

当前覆盖：

- `UART`
- `PLIC`

处理方式：

- 直接本地完成
- 不发 AXI
- load 通过 `peripheral_model->read_load(paddr, func3)` 返回数据

#### 情况 C：其他 MMIO

如果不属于上面两类：

- `PeripheralAxi` 进入 `busy`
- 通过 `out.read.req_*` 发 AXI read 请求
- 等待 AXI read response

### 3.3 MMIO load 如何回到 LSU

无论是本地返回还是 AXI 返回，最后都通过 `PeripheralOutIO` 回 LSU：

- [PeripheralAxi.cpp](/home/tututu/qimeng/simulator/MemSubSystem/PeripheralAxi.cpp)
  - `peripheral_io->out.is_mmio`
  - `peripheral_io->out.mmio_rdata`
  - `peripheral_io->out.uop`

LSU 在这里消费：

- [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp)
  - `if (peripheral_io.out.is_mmio && peripheral_io.out.uop.op == UOP_LOAD)`

处理方式：

1. 用 `uop.rob_idx` 找回对应 LDQ 槽位
   - 这里的 `rob_idx` 实际上被临时改成了 `ldq_idx`
2. 把 `mmio_rdata` 写入 `entry.uop.result`
3. 把 `difftest_skip` 带回 load uop
4. 将该 load 放入 `finished_loads`
5. 回收 LDQ 项

## 4. MMIO store 处理流程

### 4.1 store 什么时候能发

MMIO store 从 STQ 侧发：

- [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp)

当前要求：

1. `entry.valid`
2. `addr_valid`
3. `data_valid`
4. `committed == true`
5. `done == false`
6. `send == false`
7. 当前没有其他 MMIO inflight
8. `peripheral_io.out.ready == true`

这里最关键的一点是：

- MMIO store 必须先经过 ROB 提交
- LSU 在 `seq()` 里通过 `commit_stores_from_rob()` 把对应 STQ 项标成 `committed`
- 只有 `committed` 之后才允许真正发给 `PeripheralAxi`

相关代码：

- [RealLsu.cpp](/home/tututu/qimeng/simulator/back-end/Lsu/RealLsu.cpp)
  - `commit_stores_from_rob()`
  - `seq()`

### 4.2 PeripheralAxi 对 MMIO store 的处理

LSU 发出的请求内容：

- `is_mmio = 1`
- `wen = 1`
- `mmio_addr = entry.p_addr`
- `mmio_wdata = entry.data`
- `uop.op = UOP_STA`
- `uop.rob_idx = stq_idx`
- `uop.func3 = entry.func3`

然后 `PeripheralAxi::comb_inputs()` 分三种情况：

#### 情况 A：被 `PeripheralModel` 覆盖的 MMIO

处理方式：

- 本地直接返回 write 完成
- 不发 AXI
- 这里只返回响应，不在这里真正修改外设状态

#### 情况 B：未覆盖的 MMIO

处理方式：

- 走 AXI write 请求
- 等待 AXI write response

### 4.3 MMIO store 的副作用何时真正生效

这一点和 load 不同，当前实现是“响应”和“提交副作用”分开的。

真正的副作用不是在 `PeripheralAxi` 接收请求时做，而是在 commit 阶段做：

- [rv_simu_mmu_v2.cpp](/home/tututu/qimeng/simulator/rv_simu_mmu_v2.cpp)
  - `SimCpu::commit_sync()`
- [MemSubsystem.cpp](/home/tututu/qimeng/simulator/MemSubSystem/MemSubsystem.cpp)
  - `MemSubsystem::on_commit_store()`
- [PeripheralModel.h](/home/tututu/qimeng/simulator/MemSubSystem/include/PeripheralModel.h)
  - `PeripheralModel::on_commit_store()`

当前语义是：

1. STQ 中的 MMIO store 在 LSU/PeripheralAxi 层拿到“完成响应”
2. 当该 store 真正 commit，`SimCpu::commit_sync()` 才调用
   - `mem_subsystem.on_commit_store(e.p_addr, e.data, e.func3)`
3. `PeripheralModel::on_commit_store()` 再去更新：
   - `memory[]` 里的 MMIO 镜像
   - `mip/sip`
   - `PLIC claim`
   - `UART` 命令副作用

也就是说：

- 对本地 model 覆盖的 MMIO，是否发 AXI 和“最终副作用何时生效”是两回事
- 当前副作用边界是 commit，而不是 issue

## 5. 当前哪些 MMIO 会发 AXI

### 不发 AXI

1. OpenSBI timer 特殊读
2. `PeripheralModel` 覆盖的地址
   - `UART`
   - `PLIC`

### 会发 AXI

1. 未被 `PeripheralModel` 覆盖的 MMIO load/store
2. 普通 cacheable 内存访问本来就走自己的 AXI 路径

需要注意：

- 顶层仍然保留外部 AXI `mmio/router/ddr` 运行时
- 但被本地截断的 MMIO 不会再走到那一层

## 6. 中断请求是怎么产生的

中断请求来自 CSR 模块：

- [Csr.cpp](/home/tututu/qimeng/simulator/back-end/Exu/Csr.cpp)
  - `Csr::comb_interrupt()`

输入：

- `mstatus`
- `mie`
- `mip`
- `mideleg`
- 当前 `privilege`

输出：

- `out.csr2rob->interrupt_req`

也就是：

- CSR 只负责判断“现在是否有可响应的 interrupt”
- 不直接改流水线
- 由 ROB 决定何时把它变成“真正响应的 interrupt”

## 7. 当前 interrupt 在 ROB 中如何处理

主逻辑在：

- [Rob.cpp](/home/tututu/qimeng/simulator/back-end/Rob.cpp)
  - `comb_commit()`

### 7.1 什么时候进入 single commit

只要满足以下任一条件，ROB 当前出队行就转成 single-commit：

1. 该行里有 flush 类指令
2. `interrupt_pending = in.csr2rob->interrupt_req`

### 7.2 当前 interrupt 的优先级

当前实现里，interrupt 优先级高于该指令自己的异常/CSR/页故障语义。

并且当前刚改成：

- interrupt 触发时，不再要求队头指令先完成

也就是说：

- 只要队头有 valid 指令，并且 `interrupt_req = 1`
- ROB 就可以把这条最老指令按 interrupt 单提交掉

### 7.3 ROB 发出哪些信号

当 `interrupt_fire` 成立时：

- `out.rob2csr->interrupt_resp = true`
- `out.rob_bcast->interrupt = true`

同时走 single-commit 路径：

- 只提交队头最老那一条
- `out.rob_commit->commit_entry[single_idx].valid = true`
- 该 ROB 项被出队

如果该拍是 flush/exception/interrupt 提交，还会设置：

- `out.rob_bcast->flush = true`
- `out.rob_bcast->exception = true`
- `out.rob_bcast->pc = single_pc`

其中 interrupt 分支本身不再继续下钻到 CSR/page fault/ecall 等语义，等 CSR 模块统一处理 trap。

## 8. CSR 如何把 interrupt 变成 trap

主逻辑在：

- [Csr.cpp](/home/tututu/qimeng/simulator/back-end/Exu/Csr.cpp)
  - `comb_exception()`

这里会再次用当前 CSR 状态计算 IRQ 类型，并结合：

- `in.rob2csr->interrupt_resp`
- `in.rob_bcast->ecall/page_fault/illegal_inst`

生成：

- `MTrap`
- `STrap`

如果是 interrupt trap：

1. 写 `mepc/sepc = in.rob_bcast->pc`
2. 写 `mcause/scause`
3. 计算 `trap_pc = mtvec/stvec`
4. 更新 `mstatus/sstatus`
5. 关闭 `MIE/SIE`
6. 更新 `privilege`

## 9. front-end 如何得到重定向 PC

在 [BackTop.cpp](/home/tututu/qimeng/simulator/back-end/BackTop.cpp)：

- 如果 `rob_bcast->flush == false`
  - 正常走分支预测/后端 redirect
- 如果 `rob_bcast->flush == true`
  - `mret/sret`：跳 `csr->out.csr2front->epc`
  - `exception/interrupt`：跳 `csr->out.csr2front->trap_pc`
  - 其他 flush：跳 `rob_bcast->pc + 4`

所以 interrupt 的 redirect 来源是：

- `CSR trap_pc`

## 10. interrupt 提交时，这条指令当前被当成什么语义

当前实现不是“先执行指令，再进中断”，而是更接近：

- 这条指令在 ROB 层被退休
- 但架构副作用按“被 interrupt 截断”处理

主要体现在三处：

### 10.1 Rename 不提交架构寄存器映射

- [Ren.cpp](/home/tututu/qimeng/simulator/back-end/Ren.cpp)

当 `in.rob_bcast->interrupt == true` 时：

- 不更新 `arch_RAT`
- 不按普通提交路径释放旧映射

这条指令的寄存器结果不会表现为“已经执行”

### 10.2 commit_sync 不做 store side-effect

- [rv_simu_mmu_v2.cpp](/home/tututu/qimeng/simulator/rv_simu_mmu_v2.cpp)
  - `SimCpu::commit_sync()`

当 `interrupt_commit == true` 时：

- 不调用 `mem_subsystem.on_commit_store()`

所以 interrupt 截断的指令不会在 commit 阶段产生 MMIO store 副作用。

### 10.3 difftest 不暴露 store/page-fault

- [rv_simu_mmu_v2.cpp](/home/tututu/qimeng/simulator/rv_simu_mmu_v2.cpp)
  - `SimCpu::difftest_prepare()`

当 `interrupt_commit == true` 时：

- 不向 difftest 暴露 store sideband
- 强制 `page_fault_inst/load/store = false`

因此当前 interrupt 语义更接近：

- “这条指令被退休用于推进机器状态和触发 trap”
- “但其自身架构副作用尽量按 NOP 处理”

## 11. 当前实现的两个关键点

### 11.1 MMIO load 与 MMIO store 的根本区别

- MMIO load：结果在 LSU/PeripheralAxi 返回时就产生
- MMIO store：真正 side-effect 在 commit 时才生效

### 11.2 interrupt 当前已经不要求队头指令完成

这是当前实现的一个强语义选择：

- ROB 在 `interrupt_pending` 时，不再要求 `head_uop.cplt_num == head_uop.uop_num`
- 也不再让 `SFENCE.VMA + committed_store_pending` 阻塞 interrupt

因此当前 interrupt 的优先级确实高于“该指令完成”“该指令异常”“该指令 CSR/flush 语义”。
