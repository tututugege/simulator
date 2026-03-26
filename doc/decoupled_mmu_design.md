# 解耦 MMU 重构计划（094c925 重启版）

## 0. 背景
本计划基于提交 `094c925` 重新启动，目标是分阶段引入真实地址翻译与共享 PTW，避免一次性改动导致不可定位问题。

已确认经验：
- 必须先保证 `LSU` 路径正确，再扩展前端 `ITLB`
- 共享读端口必须有严格 `fire/ack` 语义，避免“假发送”
- 每阶段必须有独立可运行基线（至少 Linux 可启动）

当前进度（本分支）：
- 已完成 `Phase A`：`CONFIG_DTLB` 下 Linux 可启动
- 已完成 `Phase C` 预备：`TlbMmu` miss 路径拆为独立 `PtwWalker`，并采用 req/resp 状态机
- 已完成第一步层次调整：`DCache` 由 `BackTop` 直接持有改为 `MemSubsystem` 持有（LSU 仅通过 req/resp 端口访问）
- 已完成 `DTLB -> PTW -> MemSubsystem` 通路（`PtwMemPort::send_read_req/resp_valid/resp_data/consume_resp`）
- 已完成 `Phase D` 关键步骤：移除 PTW fallback 直读，改为 LSU/PTW 共享 DCache 读端口统一仲裁，基于 owner 队列做回包路由
- 已修复 `sfence` 广播粘连问题（`rob_bcast->fence/fence_i` 每拍清零）
- 已将 `DTLB miss` 升级为跨拍 `RETRY`：`TlbMmu` 不再同拍自旋 walker，LSU 负责 load/store 地址翻译重试队列
- 已完成前端阶段性切换：默认启用 `TrueICacheTop`，并已从旧 `cpu.mmu.io` 切换为 `AbstractMmu(TlbMmu)` 取指翻译路径
- 已完成共享 PTW 接入：ITLB 与 DTLB 通过 `MemSubsystem` 共享 PTW/DCache 通路（双客户端端口）
- 已完成 shared walk 上提（Phase A）：`MemSubsystem` 内新增 PTW walk 状态机，DTLB miss 主路径不再在 `TlbMmu` 内部推进 walker
- 已完成 ITLB 接入 shared walk（Phase B）：ITLB/DTLB 均通过 `*_walk_port` 访问同一 PTW walk 引擎
- 已完成 walk 客户端 RR 仲裁：ITLB/DTLB walk 请求采用轮转选主，降低单侧饥饿风险
- 当前阶段状态：Linux 长测可稳定运行，且 `run init` 已通过（TrueIcache + ITLB/DTLB shared PTW）
- 已完成层次收敛：`MemSubsystem` 从 `BackTop` 内部上提到 `SimCpu` 同级；`BackTop` 仅保留后端逻辑
- 已增加 `FrontTop` 封装类：内部复用 C 风格 `front_top(front_top_in, front_top_out)`，对外以 `in/out` 接口接入 `SimCpu`
- 已补充时序文档：`doc/modules/Memory_Subsystem_Design.md` 新增“TLB/Cache 场景矩阵与逐拍示例”，覆盖 ITLB/DTLB hit/miss + ICache/DCache hit/miss + 共享PTW竞争场景

---

## 1. 总体目标
1. 在 `LSU` 内先落地 `DTLB`（最小闭环）
2. 再把 `PTW` 接入 `DCache`，并上提读仲裁到内存子系统
3. 最后引入 `ITLB`，实现前后端共享 PTW

## 1.1 最终目标模块层次
```text
CPU
├── FrontEnd
│   ├── BPU
│   └── ITLB/ICache
├── BackEnd
│   ├── ROB EXU.....
│   └── LSU
│       ├── STQ/LDQ
│       └── DTLB
└── MemSubsystem
    ├── PTW (shared by ITLB/DTLB)
    ├── Arbiter (LSU 和 PTW 的仲裁)
    ├── DCache
    └── MemoryBus/Memory
```

说明：
- `DTLB` 作为 `LSU` 子模块保留在 BackEnd 内，负责数据访存翻译与异常属性。
- `PTW`、`DCache`、仲裁逻辑统一放在 `MemSubsystem`，由 `ITLB/DTLB` 共享。
- 当前阶段仍采用 `DTLB miss` 阻塞式完成；待 `MemSubsystem` 完整解耦后，再切换非阻塞 miss。

---

## 2. 分阶段计划

### Phase A：LSU 内部 DTLB（优先）
目标：
- 在 `LSU` 内引入 `AbstractMmu` 统一接口
- 支持 `SimpleMmu` 与 `TlbMmu` 两种实现切换
- 接口保留三态：`OK/FAULT/RETRY`，但当前实现仅启用 `OK/FAULT`（`miss` 保守阻塞）

范围：
- `back-end/Lsu/*` 为主
- 不改 `BackTop` 层级，不引入前端 ITLB

验收：
- `make -j8`
- `make -C baremetal/test run`
- Linux `../image/linux/linux.bin` 可稳定启动

### Phase B：LSU-Cache 握手与投机收敛
目标：
- 固化 `load req/resp` 与 `store wreq/wready`
- 明确 `LDQ/STQ` 生命周期与 flush/mispred 行为
- 回包匹配稳定（不丢包、不误匹配）

范围：
- `SimpleLsu`、`SimpleCache`

验收：
- `CONFIG_BPU` 下无回归
- 无 `LDQ wait_resp` 永久悬挂

### Phase C：PTW 接入 DCache（单 outstanding）
目标：
- `TlbMmu` miss 通过 `PtwWalker` 发起页表访问（已完成）
- `PtwWalker` 使用 req/resp 状态机（已完成）
- 先单 outstanding，先正确后性能（已完成）

范围：
- `memory/PtwWalker.*`
- `PtwMemPort` 及其实现

验收：
- Linux 启动稳定
- page fault 行为与 trap 信息正确

### Phase D：MemSubsystem 仲裁（LSU + PTW）
目标：
- 在共享读口实现严格 `fire/ack`
- 杜绝“LSU已标记sent但实际未被仲裁接收”
- 回包路由可验证、可观测

范围：
- `memory/MemSubsystem.*`
- 必要时扩展 `MemReq/MemResp` 元数据

验收：
- 长跑无 ROB deadlock
- 无 sent/wait_resp 永久悬挂
- PTW 不再存在 fallback 直读路径

当前实现结果：
- `MemSubsystem` 内部建立 DCache 私有端口，统一仲裁 LSU/PTW 读请求
- 使用 `read_owner_q` 记录读请求归属并在回包阶段路由到 LSU 或 PTW
- PTW 保持单 outstanding 约束：`pending/inflight` 状态严格区分
- `TlbMmu` 与 LSU 的 `RETRY` 流程已打通，不再同拍 busy-loop
- shared walk 状态机已位于 `MemSubsystem`（`IDLE/L1_REQ/L1_WAIT/L2_REQ/L2_WAIT`）
- ITLB/DTLB 通过 `PtwWalkPort` 共享同一 walk 引擎；旧 `PtwMemPort` 路径作为兼容保留

### Phase E：前端 ITLB（已切到 TrueIcache 主线）
目标：
- `TrueICacheTop` 接 `AbstractMmu::translate(type=Fetch)`，不再依赖旧集中式 `cpu.mmu.io`
- 明确 `OK/FAULT/RETRY` 到 ICache 输入语义映射：
  - `OK` -> `ppn_valid=1`
  - `FAULT` -> `ppn_valid=1 + page_fault=1`
  - `RETRY` -> 保持等待并重试
- 保留 `SimpleICacheTop` 作为回退模型（coherent 翻译模式）

范围：
- `front-end/icache/*`
- 不先改 true icache pipeline 细节

验收：
- Linux 可启动并完成长测
- 无 ROB deadlock、无固定点取指停摆

### Phase F：一致性与收尾
目标：
- `sfence.vma` 语义覆盖 ITLB/DTLB/PTW 在途状态
- 文档收敛，补齐模块边界与接口说明

下一阶段建议（当前主线）：
1. 对共享 PTW 增加长期稳定性回归（Linux 长测 + page fault 密集窗口）。
2. 增强 shared walk 的可观测性（按客户端统计 walk_req/walk_grant/walk_resp/fault/retry_wait）。
3. 在正确性稳定后，再评估 PTW 多 outstanding 的性能优化。
4. 收敛兼容路径：在确认无回归后逐步下线 `TlbMmu` 内部旧 walker fallback。

---

## 6. 当前交互方式（2026-02 阶段）

### 6.1 后端数据访存（已稳定）
`LSU -> DTLB(TlbMmu) -> PtwWalkPort -> MemSubsystem SharedPTW -> DCache -> Memory`

说明：
- `DTLB miss` 返回 `RETRY`，由 LSU 跨拍重试。
- PTW 采用单 outstanding 状态机。
- `MemSubsystem` 负责 LSU/PTW 读请求仲裁与回包路由。

### 6.2 前端取指访存（阶段性实现）
`FrontEnd(BPU/fetch FIFO) -> TrueICacheTop -> ITLB(TlbMmu) -> PtwWalkPort -> MemSubsystem SharedPTW -> DCache -> Memory`

说明：
- 该路径已改为 `AbstractMmu` 接口，不再走旧 `cpu.mmu.io`。
- ITLB/DTLB 已分别绑定 `MemSubsystem` 的独立 walk 客户端端口（避免回包混淆）。
- walk 客户端仲裁已升级为 RR；DCache 统一读口仍是 `LSU > PTW`。

---

## 3. 配置策略
- `CONFIG_TLB_MMU`：统一控制前后端 MMU 模型  
  - 定义：I/D 两侧都使用 `TlbMmu`（ITLB+DTLB+PTW）
  - 未定义：I/D 两侧都使用 `SimpleMmu`（理想翻译）
- `CONFIG_ITLB/CONFIG_DTLB`：保留为功能域标识，提升代码与文档可读性
- 默认建议：使用统一开关，避免 I/D 模型混搭带来的不可控行为差异

---

## 4. 回归策略
每阶段至少执行：

```bash
make -j8
make -C baremetal/test load-store
make -C baremetal/test run
./build/simulator ../image/linux/linux.bin
```

---

## 5. 里程碑判定
进入下一阶段前必须满足：
1. Linux 可稳定启动（至少过 OpenSBI + 内核 early boot）
2. 无死锁断言（ROB/LSU）
3. 无明显 difftest 系统性偏差
