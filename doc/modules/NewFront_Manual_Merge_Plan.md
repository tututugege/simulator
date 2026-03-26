# 新前端手工合并计划（front-end + new_front）

## 目标
- 将 `new_front/front-end` 的 BPU 与流水实现并入当前 `front-end`。
- 保留当前已验证可跑路径（尤其是 ITLB、性能计数、Back/Front 接口时序）。
- 采用分阶段、小步可回归策略，每步都可编译、可运行 `dhrystone.bin`。

## 当前状态
- 已完成：`new_front` 新增文件并入现有 `front-end`：
  - `front-end/config/frontend_feature_config.h`
  - `front-end/config/frontend_diag_config.h`
  - `front-end/config/CONFIG_SWITCH_MANUAL.md`
  - `front-end/wire_types.h`
  - `front-end/frontend_stats.h`
  - `front-end/frontend_stats.cpp`
- 已完成：`front-end/frontend.h` 接入新配置头，并保持旧行为默认值。

## 文件合并策略
- `front-end/front_IO.h`
  - 策略：以当前版本为主，分批引入 `wire_types` 类型别名与静态检查。
  - 风险：会牵引 back-end 的 IO 字段类型连锁修改。
- `front-end/front_module.h`
  - 策略：保留当前顶层函数接口（含 `set_context/set_ptw_port`），逐步增加 seq/comb 拆分接口，先不删除旧入口。
- `front-end/front_top.cpp`
  - 策略：先保留当前稳定实现；再移植 new_front 的统计/旁路优化，最后再考虑 BPU 内部时序拆分。
- `front-end/icache/*`
  - 策略：优先保留当前 ITLB/PTW 路径；按需吸收 new_front 的局部优化，避免直接切到 `cpu.mmu` 强耦合实现。
- `front-end/fifo/*`
  - 策略：先迁移无语义变化重构，再迁移行为改动（mini flush / two-ahead 相关）。
- `front-end/BPU/*`、`front-end/predecode*`
  - 策略：按模块替换，但必须通过 `dhrystone.bin` 回归后再推进到 Linux 场景。

## Phase 计划
1. Phase 1（接口基线）
- 保持当前可跑前端行为不变。
- 引入新配置与统计框架，不改变时序。

2. Phase 2（类型系统并轨）
- 在 `front_IO.h` 引入 `wire_types.h` 的类型别名。
- 仅做“等宽替换”，不改语义，不删旧字段。

3. Phase 3（模块入口双轨）
- 在 `front_module.h` 增加 seq/comb 新入口，同时保留旧 `*_top` 入口作为兼容层。
- 先让编译通过，再逐模块切换调用点。

4. Phase 4（BPU/FIFO 迁移）
- 逐文件替换 `BPU/*` 与 `fifo/*`，每替换一组跑 `dhrystone.bin`。
- 出现行为偏差立即止损回退当前 phase。

5. Phase 5（front_top 合流）
- 合并 `front_top.cpp` 主流程。
- 再做 Linux/462 回归，确认 `run init` 不卡死。

## 回归基准
- 基础：`./build/simulator ../image/mem/dhrystone.bin`
- 进阶：`./build/simulator ../image/mem/spec_mem/test/462.libquantum`
- 若启用 fast：需要确认 oracle/new-front 切换语义一致后再作为主回归。

