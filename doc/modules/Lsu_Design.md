# Lsu (Load Store Unit) 设计文档

## 1. 概述
当前后端 LSU 实现为 `SimpleLsu`，核心目标是：
- 维护 Store/Load 的时序与可见性约束
- 支持 Store-to-Load Forwarding (STLF)
- 处理分支误预测/全局 flush 下的投机恢复
- 通过外部 `DCache` 的 Req/Resp 握手模拟访存延迟

当前版本重点变化：
- Load 追踪由旧 `inflight_loads(list)` 改为显式 `LDQ`（固定容量数组）
- 对错误路径已发出的 Load，采用 `killed` 标记并“等回包再释放”策略
- `LDQ` 在 Dispatch 阶段预分配（由 LSU 给出可分配槽位）

---

## 2. 主要接口

### 2.1 外部接口（模块边界）
- 输入：`dis2lsu`、`exe2lsu`、`rob_commit`、`rob_bcast`、`dec_bcast`、`csr_status`
- 输出：`lsu2dis`、`lsu2exe`、`lsu2rob`、`dcache_req`、`dcache_wreq`
- 输入补充：`dcache_resp`、`dcache_wready`

说明：
- `DCache` 已提升为 `BackTop` 内与 `LSU` 同级模块
- LSU 对 Dispatch 通过 `lsu2dis.stq_free/ldq_free` 提供反压
- LSU 对 DCache 通过 `MemReqIO/MemRespIO` 握手

握手流程（读）：
1. LSU 在 `comb_recv` 选择可发射 LDQ 项，驱动 `dcache_req`
2. DCache 入队请求并记录 `complete_time`
3. DCache 在请求完成时拉高 `dcache_resp.valid`
4. LSU 在 `comb_load_res` 消费回包并完成对应 LDQ 项

握手流程（写）：
1. LSU 在 `comb_recv` 基于 STQ head 产生 `dcache_wreq`
2. DCache 通过 `dcache_wready` 提供写接收能力
3. LSU 在 `seq` 仅在 `wreq && wready` 时释放 STQ head

---

## 3. STQ 设计

### 3.1 STQ 条目字段
`StqEntry` 主要字段：
- `valid`
- `addr_valid`
- `data_valid`
- `committed`
- `addr` / `p_addr`
- `data`
- `func3`
- `tag`
- `rob_idx` / `rob_flag`

### 3.2 三个指针语义
- `stq_tail`：分配指针（Dispatch 分配 Store 槽位）
- `stq_commit`：提交边界（ROB 提交 Store 后推进）
- `stq_head`：退休指针（真正写内存后推进）

### 3.3 Store 生命周期
1. Dispatch 申请 STQ 项
2. Exe 阶段 STA 写地址、STD 写数据（乱序到达，STQ 内合并）
3. ROB 提交后 `committed=true`
4. 当 `head` 项地址/数据均就绪时，发 `dcache_wreq`
5. 当拍 `dcache_wready=1` 时，STQ head 出队

---

## 4. LDQ 设计（显式数组）

### 4.1 LDQ 条目结构
`LdqEntry` 字段：
- `valid`：条目占用
- `killed`：错误路径标记
- `sent`：是否已向 Cache 发出请求
- `waiting_resp`：是否处于等待回包状态
- `uop`：Load 微操作（含地址/状态/结果）

### 4.2 分配与释放
- 分配：Dispatch 依据 `lsu2dis.ldq_alloc_idx[]` 选择槽位，LSU 在 `seq` 中 `reserve_ldq_entry()`
- 释放：`free_ldq_entry(idx)` 清空条目并递减 `ldq_count`
- `ldq_free = LDQ_SIZE - ldq_count`

### 4.3 Load 状态机（基于 `uop.cplt_time`）
- `REQ_WAIT_EXEC`：已分配 LDQ，但尚未收到 AGU 的 Load 请求
- `REQ_WAIT_RETRY`：STLF 需要重试（前序 Store 地址/数据未就绪）
- `REQ_WAIT_SEND`：可向 Cache 发请求
- `REQ_WAIT_RESP`：请求已发出，等待回包
- `<= sim_time`：完成并可写回

注：
- STLF 命中可直接完成，不走 Cache
- STLF miss 进入 `REQ_WAIT_SEND`

### 4.4 投机恢复与 `killed` 机制
在 `mispred/flush` 下：
- 未发请求（`sent=false`）的 LDQ 项：可立即释放
- 已发请求（`sent=true`）的 LDQ 项：只标记 `killed=true`，不立即释放

回包到达时：
- `killed=false`：正常写回并释放
- `killed=true`：丢弃数据（drain）并释放

该策略避免了“错误路径请求回包晚到，误匹配新分配条目”的问题。

---

## 5. STLF 机制
Load 执行时按 `stq_idx` 快照扫描 `[stq_head, stq_idx)`：
1. 若遇到更老 Store 且 `addr_valid=false`：返回 Retry
2. 地址重叠但 `data_valid=false`：返回 Retry
3. 地址重叠且数据就绪：执行 merge/forward
4. 无命中：返回 Miss，进入 Cache 路径

---

## 6. Cache 交互模型（当前实现）
`SimpleCache` 当前为“读请求队列 + 写请求直通”的阻塞式模型：
- 读路径：FIFO 请求队列（每拍最多接收 1 个读请求）
- 仅检查队头是否到 `complete_time`，到时返回 1 个回包
- 存在 HOL（队头阻塞）行为：前序慢读请求会阻塞后续读回包
- 写路径：`wreq/wready` 握手，当前 `SimpleCache` 默认 `wready=1`

这与“真实多 MSHR 非阻塞 cache”不同，属于保守功能模型。

---

## 7. 组合/时序关键点

### 7.1 `comb_lsu2dis_info`
- 输出 `stq_tail/stq_free/ldq_free`
- 扫描 LDQ 空槽并填充 `ldq_alloc_idx[]` 供 Dispatch 使用

### 7.2 `comb_recv`
- 先处理 STD，再 STA，再新到 Load
- 从 LDQ 挑选 `REQ_WAIT_SEND` 且未 `killed/sent` 的项发请求
- 生成 STQ head 对应的 `dcache_wreq`

### 7.3 `comb_load_res`
- 消费 `dcache_resp`
- 按 `ldq_idx`（当前复用在请求 `uop.rob_idx`）匹配条目
- live 项写回，killed 项仅 drain

### 7.4 `seq`
- 处理 flush/mispred 恢复
- 处理 STQ/LDQ 的 Dispatch 分配请求消费
- 处理 STQ 写握手成功后的出队、以及提交指针推进
- 处理 LDQ 的 retry 与完成释放
- 通过 helper 拆分，主流程更清晰：
  - `handle_global_flush`
  - `handle_mispred`
  - `retire_stq_head_if_ready`
  - `commit_stores_from_rob`
  - `progress_ldq_entries`

---

## 8. 已知限制与后续方向
- 当前用 `ldq_idx` 作为回包 token，前提是“sent 条目必须等回包释放”
- 当前 `SimpleCache` 无显式 cancel/flush 协议
- 当前读请求没有 `rready` 反压通道（写侧已有 `wready`）
- 后续可演进：
  1. 扩展多请求端口与非阻塞回包
  2. 引入独立 ITLB/DTLB + 共享 PTW 的完整内存层级
  3. 引入统一的请求仲裁（LSU/ITLB-PTW/DTLB-PTW）
