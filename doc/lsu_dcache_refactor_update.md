# LSU/DCache 重构更新说明（新旧机制对比）

## 1. 文档目的
这份文档用于快速回答两个问题：
1. 这次大重构到底改了什么。
2. 新机制相比旧机制，行为和约束发生了哪些关键变化。

---

## 2. 旧机制概览（重构前）

### 2.1 模块层级
- `SimpleCache` 作为 `SimpleLsu` 内部成员。
- LSU 直接调用 cache 访问函数，耦合较强。

### 2.2 Load 路径
- 以 `inflight_loads` 列表追踪 load。
- 部分路径存在“发请求/完成”耦合，投机清理和回包匹配边界不清晰。

### 2.3 Store 路径
- STQ 提交阶段在 `LSU::seq()` 内直接写 `p_memory`。
- 没有独立的 cache 写握手机制。

### 2.4 风险点
- 分支误预测后，已发出的错误路径 load 回包容易造成管理复杂度上升。
- Cache 与 LSU 强耦合，不利于后续 ITLB/DTLB + 共享 PTW 接入。

---

## 3. 新机制概览（当前基线）

### 3.1 模块层级
- `DCache` 上提到 `BackTop`，与 `LSU` 同级。
- `LSU <-> DCache` 使用显式 IO 连接。

### 3.2 读通路（Load）
- LSU 读请求口：`dcache_req`
- DCache 读回包口：`dcache_resp`
- DCache 读侧为 FIFO pending queue，按 `complete_time` 出回包。

### 3.3 写通路（Store）
- LSU 写请求口：`dcache_wreq`
- DCache 写就绪口：`dcache_wready`
- STQ head 仅在 `wreq && wready` 时退休。

### 3.4 LDQ 机制
- 显式 `LDQ` 数组替代旧 `inflight_loads`。
- 条目字段：`valid/killed/sent/waiting_resp/uop`。
- Dispatch 阶段预分配 `ldq_idx`，执行阶段按 `uop.ldq_idx` 命中。

### 3.5 投机恢复
- 对已发请求的错误路径 load：标记 `killed`，等回包后 drain+free。
- 对未发请求的错误路径 load：可直接释放。

---

## 4. 新旧机制差异（重点）

### 4.1 架构边界
- 旧：Cache 内嵌 LSU。
- 新：Cache 与 LSU 解耦，便于引入 PTW 多源仲裁。

### 4.2 Load 标识与回包匹配
- 旧：列表管理 + 动态状态，边界不够硬。
- 新：`ldq_idx` 作为稳定 token，回包匹配清晰。

### 4.3 Store 提交语义
- 旧：LSU 内部直接写内存。
- 新：通过 `wreq/wready` 与 DCache 写端口握手，时序更接近真实实现。

### 4.4 时序控制点
- 旧：`seq()` 逻辑集中且分支复杂。
- 新：`seq()` 拆分为 helper，主流程更清晰：
  - `handle_global_flush`
  - `handle_mispred`
  - `retire_stq_head_if_ready`
  - `commit_stores_from_rob`
  - `progress_ldq_entries`

### 4.5 压测能力
- 新增写侧压力模式：
  - `SIM_DCACHE_STRESS=1`
  - `SIM_DCACHE_WREADY_PCT=<1..100>`
- 用于提前验证写反压下 LSU/STQ 的健壮性。

---

## 5. 当前仍然简化的点
- 读请求还没有 `rready`/读侧反压握手；当前默认读请求必接收。
- DCache 仍是保守模型（单端口读队列 + 简化写处理），不是完整 MSHR 非阻塞实现。
- 仍未引入 PTW 多源仲裁（ITLB/DTLB）到 DCache 前端。

---

## 6. 建议的下一步
1. 增加读侧 ready 握手（或完整 req-ready/resp-valid 协议）。
2. 在 DCache 前加入 LSU/ITLB-PTW/DTLB-PTW 仲裁层。
3. 再推进非阻塞能力（多请求接入 + 回包路由 token 体系）。
