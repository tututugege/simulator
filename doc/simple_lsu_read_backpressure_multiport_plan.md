# SimpleLsu 改进计划：读反压 + 多端口

## 1. 目标与范围

### 1.1 目标
- 给 `SimpleLsu` 增加读请求反压能力，避免 `SimpleCache::pending_reqs` 溢出时依赖断言失败。
- 把 `SimpleLsu` 从当前单读端口发射改为多读端口发射。
- 与 `MemSubsystem/SimpleCache` 的请求仲裁与回包路由保持一致，保证功能正确。

### 1.2 当前已知现状
- `MemReqIO` 读通道无 `ready`，只有 `en/wen/...`。
- `SimpleCache` 读请求入 `pending_reqs`，队列满会 `Assert`，无读反压。
- LSU 每拍最多发 1 个读请求。
- `SimpleCache` 当前按队首回包（FIFO），miss 在队首会阻塞后续 hit 回包。

## 2. 实施原则
- 先做协议闭环，再做吞吐优化。
- 先保证正确性（可观测握手、无丢包/重包），再提升并行度。
- 第一版多端口允许“多发单回”（多请求发射、每拍最多 1 回包），降低风险。

## 3. 分阶段计划

## 阶段 A：读反压闭环（单端口）

### A1. 扩展读通道握手接口
- 改动文件：
  - `back-end/include/IO.h`
  - `back-end/Lsu/include/AbstractLsu.h`
  - `MemSubSystem/include/AbstractDcache.h`
  - `back-end/include/BackTop.h`
  - `back-end/BackTop.cpp`
  - `MemSubSystem/include/MemSubsystem.h`
  - `MemSubSystem/MemSubsystem.cpp`
  - `rv_simu_mmu_v2.cpp`
- 主要改动：
  - 为 LSU->DCache 读请求新增 `read_ready` 通道（类型可复用 `MemReadyIO`）。
  - 打通顶层连线：`BackTop -> MemSubsystem -> DCache`。
- 验收：
  - 工程编译通过。
  - 默认行为与旧版一致（端口数仍为 1）。

### A2. DCache 提供真实读反压
- 改动文件：
  - `MemSubSystem/include/SimpleCache.h`
  - `MemSubSystem/SimpleCache.cpp`
- 主要改动：
  - `read_ready = (pending_reqs.size() < MAX_PENDING_REQS)`。
  - 仅在 `req.en && read_ready` 时 `accept_req()` 入队。
- 验收：
  - 压力场景下不再触发 pending queue overflow assert。
  - 当队列接近满时 LSU 可观察到 backpressure。

### A3. LSU 侧按握手更新状态
- 改动文件：
  - `back-end/Lsu/SimpleLsu.cpp`
- 主要改动：
  - 仅在 `dcache_req.en && dcache_read_ready.ready` 时，把 LDQ 项置 `sent/waiting_resp`。
  - 未握手成功时保持可重发状态，不提前进入 `REQ_WAIT_RESP`。
- 验收：
  - 无“请求未被接收却已标记 sent”的状态错误。
  - load 能在 backpressure 解除后继续前进。

## 阶段 B：多端口发射（先多发单回）

### B1. 参数化端口数
- 改动文件：
  - `include/config.h`
- 主要改动：
  - 增加配置项：`LSU_DCACHE_RD_PORTS`（建议初始 `2`）。
- 验收：
  - `LSU_DCACHE_RD_PORTS=1` 时与阶段 A 行为一致。

### B2. LSU 多端口请求生成
- 改动文件：
  - `back-end/Lsu/include/AbstractLsu.h`
  - `back-end/Lsu/include/SimpleLsu.h`
  - `back-end/Lsu/SimpleLsu.cpp`
- 主要改动：
  - 把 `dcache_req` 扩为数组端口。
  - 每拍从 LDQ 选最多 `LSU_DCACHE_RD_PORTS` 个可发项。
  - 每端口独立 valid/ready 握手，防止同一 LDQ 项重复被多个端口领取。
- 验收：
  - 波形/日志可见同拍发出多个读请求。
  - 无重复发射、无漏发。

### B3. MemSubsystem 多端口读仲裁
- 改动文件：
  - `MemSubSystem/include/MemReadArbBlock.h`
  - `MemSubSystem/include/MemSubsystem.h`
  - `MemSubSystem/MemSubsystem.cpp`
- 主要改动：
  - 输入改为 LSU 多读端口 + PTW 请求统一仲裁。
  - 读反压按端口返回（每端口 `ready`）。
  - 若 DCache 内部暂仍单入口，则仲裁保证每拍最多 1 路进入 DCache，同时正确回传谁被 grant。
- 验收：
  - 多端口并发请求时，grant 行为符合优先级/公平性设计。
  - 未 grant 的端口保持等待，不丢请求。

### B4. DCache 多端口接入（可先逻辑多端口、物理单发）
- 改动文件：
  - `MemSubSystem/include/AbstractDcache.h`
  - `MemSubSystem/include/SimpleCache.h`
  - `MemSubSystem/SimpleCache.cpp`
- 主要改动：
  - 支持多读端口输入。
  - 第一版可仍保持单回包（每拍 1 resp），降低复杂度。
- 验收：
  - 功能正确，且请求吞吐优于单端口。

## 阶段 C：多端口回包（可选增强）

### C1. 响应路由携带端口信息
- 改动文件：
  - `MemSubSystem/include/MemRespRouteBlock.h`
  - `MemSubSystem/MemSubsystem.cpp`
- 主要改动：
  - owner tag 扩展为 `owner + lsu_port_id`。
  - 回包精确投递到对应 LSU 响应口。

### C2. LSU 多端口回包消费
- 改动文件：
  - `back-end/Lsu/include/AbstractLsu.h`
  - `back-end/Lsu/SimpleLsu.cpp`
- 主要改动：
  - 同拍消费多个 `dcache_resp`。
  - 与 `LSU_LOAD_WB_WIDTH` 协同，防止完成队列拥塞。
- 验收：
  - 同拍多回包可被正确处理并写回。

## 4. 关键设计决策（实现前需确认）
- 是否要求“同拍多回包”进入首版。
  - 建议：否，首版先多发单回。
- 是否允许读回包乱序。
  - 建议：否，先保持 FIFO；后续做 bank/MSHR 再放开。
- 多端口仲裁策略。
  - 建议：LSU 端口内部轮询；LSU 与 PTW 保持现有优先级策略。

## 5. 风险点与规避
- 风险：接口改动面广，容易遗漏连线。
  - 规避：先端口数固定为 1 打通再放开到 2。
- 风险：握手时序错误导致 LDQ 状态机卡死。
  - 规避：增加 `sent/waiting_resp` 相关断言与 trace。
- 风险：多端口+flush/kill 下出现幽灵回包。
  - 规避：保留 rob_idx token 校验，回包时二次检查 `entry.valid/sent/waiting_resp`。

## 6. 验证计划

### 6.1 Directed
- `queue_full_backpressure`：构造长 miss，压满 pending 队列，验证读 ready 拉低且无断言。
- `single_port_regression`：`LSU_DCACHE_RD_PORTS=1` 与基线行为一致。
- `dual_port_issue`：`LSU_DCACHE_RD_PORTS=2` 同拍发双请求，统计吞吐提升。
- `flush_kill_inflight`：分支回滚/异常时在途 load 回包不污染状态。

### 6.2 Regression
- 现有 baremetal 测试集全跑。
- Linux 长测（若当前流程已具备）至少回归一次。

## 7. 交付里程碑
- M1：阶段 A 完成（读反压单端口），可合入。
- M2：阶段 B 完成（多端口多发单回），可默认开启 `RD_PORTS=2`。
- M3：阶段 C 完成（多回包，可选），按性能收益决定是否默认开启。

## 8. 时间预估
- 阶段 A：0.5 ~ 1 天
- 阶段 B：2 ~ 4 天
- 阶段 C（可选）：1 ~ 2 天
