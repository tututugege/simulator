# New Front-End 接入计划（独立于 TLB 重构）

## 1. 背景与范围

本计划仅用于将 `new_front-end/` 迁移并接入当前模拟器主线，目标是替换现有前端取指/预测链路中的屎山时序问题。  
该任务与 ITLB/DTLB/PTW 重构解耦，不在本计划中引入或修改 TLB 机制。

### 1.1 明确不做

1. 不在本阶段引入 ITLB 或修改 `AbstractMmu/TlbMmu` 行为。
2. 不修改后端 LSU/ROB 的语义。
3. 不把本阶段问题归因到 TLB；优先修复前端握手与时序配对。

## 2. 目标

1. 让 `new_front-end` 在当前工程中可编译、可链接、可运行。
2. 保持当前后端接口不破坏：`front_top_in/front_top_out` 对接现有 `rv_simu_mmu_v2.cpp`。
3. 先使用现有 ICache/Simple VA2PA 语义跑通 Linux（冒烟+长跑）。
4. 清晰分层：前端接入完成后，再单独开启 ITLB 任务。

## 3. 交付物

1. 代码：`new_front-end` 相关模块接入主构建并替换旧 `front-end` 路径。
2. 配置：统一的编译宏与 include 依赖（去除 `TOP.h/cvt.h` 老依赖）。
3. 文档：本计划 + 最终接入结果记录（风险与回归结论）。

## 4. 分阶段实施

## Phase 0: 冻结基线

1. 固定当前可运行 commit，打标签或记录 SHA。
2. 记录当前 Linux 冒烟行为与失败点（若有）。
3. 新建独立开发分支（仅用于前端接入）。

**验收**
1. 能随时回到基线。
2. 基线构建与运行结果可复现。

## Phase 1: 编译兼容层（不切流量）

1. 为 `new_front-end` 补齐当前工程需要的头文件与类型映射。
2. 移除/替换 `cvt.h` 依赖：
   1. ICache 走当前新版 `va2pa(..., CsrStatusIO*, ...)` 接口。
3. 处理 `TOP.h` 依赖：
   1. 若仅用于旧 true-icache 绑定，改为当前工程可用接口或条件编译隔离。
4. 对齐 `front_IO.h` 字段：
   1. 补 `tage_tag`、2-ahead 相关字段，当前阶段不要启用2-ahead。
   2. 保持旧字段兼容，避免后端改动扩散。

**验收**
1. `new_front-end` 单独编译通过。
2. 主工程全量链接通过。

## Phase 2: 最小流量切换（先不改 ICache 语义）

1. 将 `Makefile` 的 `front-end` 源切换到 `new_front-end`。
2. 仅接入 `front_top/BPU/fifo` 新流程。
3. ICache 暂时保持当前语义：
   1. 不引入 ITLB。
   2. 不改变现有 MMU 宏行为。
4. 保持 `front_top_out` 到后端的数据格式不变。

**验收**
1. 可启动到 OpenSBI 与 Linux early boot。
2. 无立即性 ROB deadlock。

## Phase 3: 时序稳定化与行为对齐

1. 检查并修复请求/回包配对：
   1. `fetch_address_FIFO` 入队与 ICache 回包的一致性。
   2. `PTAB` 与 `instruction_FIFO` 的同步消费。
2. 修复 flush/refetch 角落行为：
   1. predecode flush。
   2. mispred/rob flush。
3. 处理 2-ahead 逻辑的最小可用策略：
   1. 可先保留框架，默认关闭复杂分支。

**验收**
1. Linux 可稳定跑过历史卡点周期窗口。
2. difftest 不出现固定点可复现分歧（若开启）。

## Phase 4: 清理与文档

1. 删除临时 debug log 与临时保护代码。
2. 补充模块文档：
   1. `new_front-end` 流程图（BPU -> fetch_addr_fifo -> icache -> inst_fifo -> PTAB -> predecode -> front2back）。
3. 输出“后续与 ITLB 对接接口点”列表，但不在本任务内实现。

**验收**
1. 代码可读性回到可维护状态。
2. 文档可支持下一会话直接接续实现。

## 5. 风险与应对

1. 风险：前端字段扩展影响后端接口。
   1. 应对：仅新增字段，不删除旧字段；保持默认值安全。
2. 风险：`USE_IDEAL_ICACHE` 宏切换引入额外变量。
   1. 应对：先锁定当前可运行宏组合，统一在 `frontend.h` 管控。
3. 风险：2-ahead/mini-flush 造成额外错拍。
   1. 应对：先保守关闭复杂路径，逐步打开。
4. 风险：日志过多影响定位。
   1. 应对：统一使用周期窗口日志开关，提交前清理。

## 6. 回归与验收清单

1. `make -j8` 编译通过。
2. Linux 冒烟：可进入内核 early boot 并稳定推进。
3. 长跑：可跑过历史失败窗口（记录绝对 cycle）。
4. （若启用）difftest：无确定性复现分歧。
5. 无新增前端死锁（ROB deadlock/取指停摆）。

## 7. 建议的提交粒度

1. `feat(frontend): add new_front-end compatibility layer`
2. `refactor(frontend): switch build to new_front-end pipeline`
3. `fix(frontend): stabilize fetch/PTAB/inst_fifo handshake`
4. `docs(frontend): update front-end integration notes`

## 8. 下一会话启动提示（可直接复制）

1. 目标：执行 `doc/new_frontend_integration_plan.md` 的 Phase 1 与 Phase 2。
2. 约束：不改 TLB/PTW/LSU，仅改前端与必要接口。
3. 验收：编译通过 + Linux 冒烟通过 + 无 ROB deadlock。
