# 访存子系统设计说明（Memory Subsystem）

## 1. 目标与范围
本文档描述当前版本中 `ICache`、`LSU`、`ITLB/DTLB`、`PTW`、`DCache`、`Memory` 的模块层次与交互细节，重点覆盖：

1. 前后端统一通过 `AbstractMmu` 接口进行地址翻译。
2. ITLB/DTLB 共享 `MemSubsystem` 中的 PTW/DCache 访存通路。
3. `MemSubsystem` 负责读请求仲裁与回包路由。

## 2. 当前模块层次
```text
CPU
├── FrontEnd
│   ├── BPU
│   └── TrueICache
│       └── ITLB (AbstractMmu 实例，当前可选 TlbMmu/SimpleMmu)
├── BackEnd
│   ├── ROB / EXU / ISU / ...
│   └── LSU
│       ├── STQ/LDQ
│       └── DTLB (AbstractMmu 实例，当前可选 TlbMmu/SimpleMmu)
└── MemSubsystem
    ├── SharedPTW Walk Engine
    │   ├── dtlb_walk_port
    │   └── itlb_walk_port
    ├── PTW 内存后端端口（兼容保留）
    │   ├── dtlb_ptw_port
    │   └── itlb_ptw_port
    ├── 读仲裁与回包路由（LSU/PTW）
    └── DCache
        └── Memory (p_memory / MMIO)
```

## 3. 统一配置策略
当前采用“统一模型开关”控制 I/D 两侧 MMU 模型：

1. `CONFIG_TLB_MMU` 定义时：
   - ICache 侧使用 `TlbMmu`（ITLB）
   - LSU 侧使用 `TlbMmu`（DTLB）
2. `CONFIG_TLB_MMU` 未定义时：
   - ICache 侧使用 `SimpleMmu`
   - LSU 侧使用 `SimpleMmu`
3. `CONFIG_ITLB/CONFIG_DTLB` 作为功能域标识保留，用于文档与路径可读性，不再单独决定模型类型。

## 4. 关键接口与连线

### 4.1 LSU <-> MemSubsystem <-> DCache
LSU 对外仍是标准端口：

1. 读请求：`dcache_req`
2. 写请求：`dcache_wreq`
3. 读回包：`dcache_resp`
4. 写反压：`dcache_wready`

`MemSubsystem` 内部再将 LSU 与 PTW 的读流量统一仲裁后驱动 DCache。

### 4.2 ITLB/DTLB PTW 端口
`SimCpu::init()` 中由同级模块连接：

1. `back.connect_mem_subsystem(&mem_subsystem)` 统一完成端口绑定：
   - `lsu->set_ptw_walk_port(mem_subsystem->dtlb_walk_port())`
   - `lsu->set_ptw_mem_port(mem_subsystem->dtlb_ptw_port())`
   - `icache_set_ptw_walk_port(mem_subsystem->itlb_walk_port())`
   - `icache_set_ptw_mem_port(mem_subsystem->itlb_ptw_port())`
   - `lsu_req/wreq/resp/wready` 线网连接

说明：
1. `*_walk_port` 是当前主路径：ITLB/DTLB miss 请求统一进入 `MemSubsystem` 的 shared PTW walk 状态机。
2. `*_ptw_port` 作为兼容保留路径（当前仍可被 `TlbMmu` fallback 使用）。

## 5. 详细数据通路

### 5.1 前端取指路径（I-side，当前主路径）
```text
FrontEnd fetch request
  -> TrueICacheTop
  -> ITLB(AbstractMmu::translate)
     -> (TLB hit) 直接返回 ppn/page_fault
     -> (TLB miss) 通过 itlb_walk_port 发起 walk 请求
        -> MemSubsystem SharedPTW 状态机 (L1/L2)
        -> MemSubsystem 仲裁
        -> DCache
        -> Memory
        -> 回包返回 ITLB refill
  -> ICache pipeline 输出取指结果给 FrontEnd
```

`TrueICacheTop` 对 `AbstractMmu::Result` 的映射：

1. `OK` -> `ppn_valid=1, page_fault=0`
2. `FAULT` -> `ppn_valid=1, page_fault=1`
3. `RETRY` -> 保持等待并重放翻译请求

说明：`icache_module` 只有在 `ppn_valid=1` 时消费翻译结果，因此 fault 也必须同时拉起 `ppn_valid`。

### 5.2 后端访存路径（D-side，当前主路径）
```text
EXU(AGU/SDU) -> LSU(STQ/LDQ)
  -> DTLB(AbstractMmu::translate)
     -> hit: 直接得到 PA
     -> miss: 通过 dtlb_walk_port 发起 walk 请求 (RETRY 跨拍)
  -> LSU 向 dcache_req/dcache_wreq 发请求
  -> MemSubsystem 仲裁/转发
  -> DCache -> Memory
  -> 回包 LSU -> WB -> ROB commit
```

LSU 当前语义：

1. Load 使用 req/resp 模型，支持等待与回包匹配。
2. Store 使用写请求 + `wready` 反压。
3. `sfence.vma` 提交门控通过 `lsu2rob` IO 信号（`committed_store_pending`）实现，ROB 不再直接持有 LSU 指针。

## 6. MemSubsystem 仲裁与回包路由

### 6.1 仲裁策略
当前有两层仲裁：

1. PTW walk 客户端仲裁（在 MemSubsystem 内）  
   - `DTLB walk` 与 `ITLB walk` 采用 RR（轮转）选主。
2. DCache 读口仲裁（统一读端口）  
   - 每拍最多发一个读请求，优先级：`LSU读 > SharedPTW读 > 兼容PTW端口读`。

写请求路径当前仅 LSU 使用，不参与 PTW 读仲裁。

### 6.2 回包路由
`MemSubsystem` 对每个已发读请求记录 owner 队列：

1. `LSU`
2. `PTW_WALK`（shared walk 状态机）
3. `PTW_DTLB`（兼容端口）
4. `PTW_ITLB`（兼容端口）

收到 DCache 回包后按 owner 出队路由到目标端口。

### 6.3 PTW 状态机与客户端状态
SharedPTW walk 状态机（单 outstanding）：

1. `IDLE`
2. `L1_REQ -> L1_WAIT_RESP`
3. `L2_REQ -> L2_WAIT_RESP`
4. 完成后向对应客户端发布 `walk_resp(fault/leaf_pte/leaf_level)`

每个 walk 客户端（ITLB/DTLB）独立维护：

1. `req_pending`
2. `req_inflight`
3. `resp_valid/resp`

此外兼容 `PtwMemPort` 客户端仍维护：

1. `pending`：请求已被 client 发起，等待仲裁发出
2. `inflight`：请求已发出，等待 DCache 回包
3. `resp_valid/resp_data`：回包槽

当前每个客户端为单 outstanding。

## 7. 性能观测（已接入）
为评估共享 PTW 瓶颈，`PerfCount` 已增加：

1. `ptw_dtlb_req/grant/resp/blocked/wait_cycle`
2. `ptw_itlb_req/grant/resp/blocked/wait_cycle`

解释建议：

1. `req - grant` 对应被拒绝/需重试次数（blocked）。
2. `wait_cycle` 反映从请求挂起到完成期间的总体占用周期。
3. 对比 `DTLB` 与 `ITLB` 可评估是否存在取指侧饥饿风险。

## 8. 当前已知特性与限制

1. SharedPTW 仍为单 outstanding，优先保证正确性与可调试性。
2. DCache 统一读口仍是单发射，LSU 高压时会挤压 PTW 发射窗口。
3. `SimpleMmu` 模式可作为性能/功能对照基线，便于隔离 TLB/PTW 影响。

## 9. 下一步建议

1. 在现有 RR 基础上评估配额/权重策略（如 LSU 压力高时的 PTW 最小服务保证）。
2. 增加 `max_wait_cycle` 与连续 blocked 长度统计，辅助判断是否存在短时饥饿。
3. 在正确性稳定后评估 PTW 多 outstanding。

## 10. 关键场景矩阵与时序（TLB/Cache）

### 10.1 I-side（取指）矩阵

说明：
1. I-side 分为两个阶段：先地址翻译（ITLB），再取指缓存访问（ICache）。
2. 当前实现里，`ITLB miss` 的页表读取走 `MemSubsystem SharedPTW -> DCache`。
3. 当前实现里，`ICache miss` 的 cache line 填充仍走 ICache 自身内存端口（`icache_hw.mem_req/mem_resp`），不经过 MemSubsystem DCache。

| 场景 | ITLB | ICache | 结果 |
|---|---|---|---|
| I-1 | hit | hit | 正常取指，延迟最小 |
| I-2 | hit | miss | 无翻译重试，等待 ICache miss latency 后完成 |
| I-3 | miss | hit | 先 PTW 完成并 refill ITLB，再按 hit 路径取指 |
| I-4 | miss | miss | 先 PTW，再进入 ICache miss 流程，尾延迟最大 |
| I-5 | fault | - | `page_fault_inst=1`，该 fetch group 产生 fault 行为 |

### 10.2 D-side（数据访存）矩阵

说明：
1. D-side 分为：先 DTLB 翻译，再 DCache req/resp。
2. LSU Load 必经 `WAIT_RESP`，不允许同拍绕过回包完成。

| 场景 | DTLB | DCache | LSU行为 |
|---|---|---|---|
| D-1 | hit | hit | 发 `dcache_req`，回包后 WB/提交 |
| D-2 | hit | miss | 发 `dcache_req`，等待 miss 回包后 WB |
| D-3 | miss | hit | 先 PTW refill，随后按 hit 路径发读并回包 |
| D-4 | miss | miss | 先 PTW refill，再等待 DCache miss 回包 |
| D-5 | fault | - | 标记 `page_fault_load/store`，走异常提交路径 |

### 10.3 SharedPTW 竞争场景（ITLB/DTLB 同时 miss）

| 场景 | 请求方 | 仲裁策略 | 结果 |
|---|---|---|---|
| P-1 | ITLB miss only | RR（单方） | ITLB walk 前进 |
| P-2 | DTLB miss only | RR（单方） | DTLB walk 前进 |
| P-3 | ITLB + DTLB 同时 miss | RR 轮转选主 | 两侧交替获得 walk 服务，避免固定优先级饥饿 |
| P-4 | LSU 读高压 + PTW miss | DCache读口优先级 `LSU > PTW` | PTW 可能排队，`wait_cycle` 增加 |

### 10.4 逐拍示例（简化）

#### A. DTLB hit + DCache hit（Load）
1. `T0`：AGU 发 load，`DTLB translate -> OK`，LSU 进入 `WAIT_SEND`。
2. `T1`：LSU 发 `dcache_req`，LDQ 项进入 `WAIT_RESP`。
3. `T2`：DCache 回 `dcache_resp.valid`，LSU 消费回包并写回队列。
4. `T3`：WB 到 EXU/ROB，随后提交。

#### B. DTLB miss + DCache hit（Load）
1. `T0`：`translate -> RETRY`，LSU 保留该请求并重试。
2. `T1..Tn`：DTLB 通过 `dtlb_walk_port` 发起 walk，SharedPTW 完成 L1/L2 PTE 读取。
3. `Tn+1`：DTLB refill，下一次 `translate -> OK`。
4. `Tn+2`：LSU 发 `dcache_req`。
5. `Tn+3`：收到 hit 回包并 WB。

#### C. ITLB miss + ICache miss（取指最慢路径）
1. `T0`：fetch 地址进入 ITLB，`translate -> RETRY`。
2. `T1..Tn`：ITLB 通过 `itlb_walk_port` 完成 PTW walk 并 refill。
3. `Tn+1`：ITLB 命中，给出 `ppn_valid`。
4. `Tn+2`：ICache 查询未命中，发 line fill 请求。
5. `Tn+3..Tm`：等待 ICache miss latency。
6. `Tm+1`：返回 fetch group，前端继续推进。

#### D. ITLB/DTLB 同时 miss
1. 双方分别提交 walk 请求到 MemSubsystem。
2. SharedPTW 采用 RR 选主推进一个 walk（单 outstanding）。
3. 完成一侧后切换到另一侧，双方最终都收到 `walk_resp`。
4. 两侧 TLB refill 后分别恢复取指/访存流水。
