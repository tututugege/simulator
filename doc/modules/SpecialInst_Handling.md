# 特殊指令处理说明

## 1. 范围
本文描述后端对“控制类/序列化类”特殊指令的统一处理方式，重点覆盖：
- `IDU` 解码阶段如何分类
- `Dispatch` 如何映射到 `UOP` 与 `IQ`
- `ROB` 如何提交、是否 flush、是否需要门控
- 与前端/CSR/LSU 的交互信号

本文以当前主线实现为准（`back-end/*` + `front-end/*`），不覆盖 `demo CPU` 的独立实现。

## 2. 特殊指令总览
| ISA 指令类型 | `InstType` | Dispatch 映射 | IQ | ROB 提交形态 | 额外门控 | 提交后动作 |
| --- | --- | --- | --- | --- | --- | --- |
| `fence` | `FENCE` | `UOP_FENCE` | `IQ_INT` | 单提交 | `committed_store_pending == 0` | 仅序列化，不发 flush 广播 |
| `fence.i` | `FENCE_I` | `UOP_FENCE_I` | `IQ_INT` | 单提交 + flush 类 | `committed_store_pending == 0` | `rob_bcast.fence_i=1`，触发 ICache invalidate |
| `sfence.vma` | `SFENCE_VMA` | `UOP_SFENCE_VMA` | `IQ_INT` | 单提交 + flush 类 | `committed_store_pending == 0` | `rob_bcast.fence=1`，触发 ITLB context flush |
| `csr*` | `CSR` | `UOP_CSR` | `IQ_INT` | 单提交 + flush 类 | 无 | `rob2csr.commit=1` |
| `ecall` | `ECALL` | `UOP_ECALL` | `IQ_INT` | 单提交 + flush 类 | 无 | 异常路径 |
| `mret` | `MRET` | `UOP_MRET` | `IQ_INT` | 单提交 + flush 类 | 无 | 返回路径 |
| `sret` | `SRET` | `UOP_SRET` | `IQ_INT` | 单提交 + flush 类 | 无 | 返回路径 |
| `ebreak` | `EBREAK` | `UOP_EBREAK` | `IQ_INT` | 单提交 + flush 类 | 无 | 结束仿真 |
| `wfi` | `WFI` | `UOP_WFI` | `IQ_INT` | 单提交 + flush 类 | 无 | 结束仿真/等待 |

说明：
- “flush 类”定义见 `rob_is_flush_inst()`，会触发 ROB 精确点串行处理与后端重定向。
- fence 三兄弟统一由 `rob_is_fence_inst()` 进入“单提交 + STQ 清空门控”路径。

## 3. IDU 阶段
关键点：
- `opcode=fence`：
  - `funct3=001` -> `FENCE_I`
  - 其他 -> `FENCE`
- `SYSTEM` 指令中：
  - `ecall/ebreak/mret/sret/wfi` 直接识别
  - `sfence.vma`（`funct7=0001001 && funct3=000 && rd=x0`）识别为 `SFENCE_VMA`

实现位置：
- `back-end/Idu.cpp` 的 `decode()` 特殊分支。

## 4. Dispatch 阶段
特殊指令统一走 default 分支并投递到 `IQ_INT`，再映射到对应 `UOP_*`：
- `FENCE -> UOP_FENCE`
- `FENCE_I -> UOP_FENCE_I`
- `SFENCE_VMA -> UOP_SFENCE_VMA`
- 以及 `CSR/ECALL/MRET/SRET/EBREAK/WFI`

实现位置：
- `back-end/Dispatch.cpp` 的 `default` 分支 `switch(type)`。

补充：
- `UOP_FENCE` 已加入 ALU 可接收掩码（`OP_MASK_ALU`），因此可通过整数执行路径完成完成位回写。

## 5. ROB 提交与门控

### 5.1 单提交判定
以下任一条件会转为单提交路径：
- 队头行为 flush 类（`rob_is_flush_inst`）
- 队头行为 fence 类（`rob_is_fence_inst`）
- 有中断待响应

这样保证特殊指令在精确提交点处理，不与普通组提交混合。

### 5.2 fence 统一 STQ 门控
在队头是 `FENCE/FENCE_I/SFENCE_VMA` 时，ROB 会检查：
- `in.lsu2rob->committed_store_pending`

若为 1，则本拍禁止该 fence 提交（`single_commit=false, commit=false`），直到提交侧 STQ 清空。

`committed_store_pending` 的定义（LSU）：
- 只要 committed STQ 中仍有 `valid && committed && !suppress_write` 的 store，就返回 true。

这实现了“fence 三兄弟都等 STQ 空再提交”的统一行为。

### 5.3 front_stall 对提交的抑制
ROB 提交入口还增加前端门控输入：
- `front_stall == 1` 时，本拍 `commit` 直接为 false

即：
- `commit = !is_empty && !mispred && !front_stall`

用于在前端要求停顿时暂停 ROB 提交节奏。

## 6. 提交后的广播与前端效果

### 6.1 `fence.i`
- ROB 提交时置位：`rob_bcast.fence_i = 1`
- `BackTop` 转发到前端：`out.fence_i`
- ICache 侧动作：
  - 作为 cache invalidate 触发源
  - 取消当前 pending 请求
  - 触发恢复保持（hold for recovery）

### 6.2 `sfence.vma`
- ROB 提交时置位：`rob_bcast.fence = 1`
- `BackTop` 转发为 `out.itlb_flush`
- ICache/MMU 侧动作：
  - `translation_context_flush = itlb_flush`
  - 触发 ITLB/翻译上下文刷新

### 6.3 `fence`
- 当前实现不单独对前端/MMU 发广播信号
- 语义通过“ROB 单提交 + STQ 清空门控”保证顺序化

## 7. 与旧逻辑差异说明
- 主线后端已移除 `translation_pending` 对 ROB 的提交门控。
- fence 类指令当前仅依赖 `committed_store_pending` 进行提交前约束。

## 8. 代码索引
- 解码：`back-end/Idu.cpp`
- Dispatch 映射：`back-end/Dispatch.cpp`
- ROB 判定与提交：`back-end/Rob.cpp`
- 后端到前端广播：`back-end/BackTop.cpp`
- ICache/ITLB 响应：`front-end/icache/ICacheTop.cpp`
- STQ 门控来源：`back-end/Lsu/RealLsu.cpp`
