# Local Host-Side Perf Summary

本文件只记录当前目录下已经完成的 host-side 执行时间优化、量化结果，以及后续是否还有显著优化空间。

## Scope

- 工作对象：`CONFIG_BPU=1` + `CONFIG_AXI_LLC_ENABLE=1`
- 优化目标：在不改变功能、不改变 commit/cycle/IPC 结果的前提下，缩短相同 workload 的 C++ 执行时间
- 基准 workload：
  - `./baremetal/linux.bin`
  - `--max-commit 2000000` 用于 profiling
  - `--max-commit 5000000` 用于 wall-clock 对比

## What Was Optimized

### 已完成的主要优化方向

1. 去掉 `front_top` helper/wrapper 的按值传参
2. 去掉 `BPU` 外围 `OutputPayload/ReadData` 大 bundle 的中间搬运
3. 去掉 `TypePredictor/TAGE/BTB` 入口 `ReadData` 复制
4. 去掉 `bpu_predict_main / nlp / hist / queue / post_read_req / pre_read_req` 的按值 wrapper
5. 去掉前端若干 FIFO/表模型内部冗余 `std::queue` 状态，只保留必要时序状态
6. 去掉 `refresh_stage` 里四个 FIFO/表模型的 no-op `comb_calc`

### 主要改动文件

- `front-end/front_top.cpp`
- `front-end/BPU/BPU.h`
- `front-end/BPU/type_predictor/TypePredictor.h`
- `front-end/BPU/dir_predictor/TAGE_top.h`
- `front-end/BPU/target_predictor/BTB_top.h`
- `front-end/fifo/fetch_address_FIFO.cpp`
- `front-end/fifo/instruction_FIFO.cpp`
- `front-end/fifo/PTAB.cpp`
- `front-end/fifo/front2bank_FIFO.cpp`

## Quantified Effect

### Profiling 累计收益

当前相对 `stage2` 基线的累计收益：

- `front_top.total`: `-23.88%`
- `front_top.bpu_stage`: `-27.34%`
- `bpu.core_comb`: `-25.53%`

最近一轮（`pre_read_req` wrapper 去按值）单独收益：

- `front_top.total`: `-2.90%`
- `front_top.comb`: `-3.13%`
- `front_top.bpu_stage`: `-6.24%`
- `bpu.core_comb`: `-2.10%`

再下一轮（`bpu_nlp_comb` 去掉 `OutputPayload` seed/copy-back）单独收益：

- `front_top.total`: `-0.14%`
- `front_top.comb`: `-0.37%`
- `front_top.bpu_stage`: `-0.32%`
- `bpu.core_comb`: `-0.40%`

对应 profile 日志：

- `local_logs/real_bpu_llc/profile_run_llc1_bpu1_2m_s256_stage2.log`
- `local_logs/real_bpu_llc/profile_run_llc1_bpu1_2m_s256_nlp_hist_queue_refs.log`
- `local_logs/real_bpu_llc/profile_run_llc1_bpu1_2m_s256_preread_refs.log`

### Wall-Clock 对比

对比 workload：

- `./baremetal/linux.bin`
- `--max-commit 5000000`
- `--progress-interval 1000000`

Baseline：

- 二进制：`./build_bpu_llc1/simulator`
- 日志：`local_logs/real_bpu_llc/run_baseline_bpu1_llc1_5m.log`
- 结果：
  - `5000000 commit`
  - `cycle=3377680`
  - `ipc_total=1.48031`
  - `[host] elapsed=265.86 user=265.13 sys=0.10`

优化后：

- 二进制：`./build_bpu_llc1_perfopt/simulator`
- 日志：`local_logs/real_bpu_llc/run_perfopt_nlp_inplace_bpu1_llc1_5m.log`
- 结果：
  - `5000000 commit`
  - `cycle=3377680`
  - `ipc_total=1.48031`
  - `[host] elapsed=174.73 user=174.20 sys=0.10`

结论：

- `elapsed` 降低 `34.28%`
- `user` 降低 `34.30%`
- 对应速度提升约 `1.52x`

这说明当前这些优化不是只改善了采样统计，而是对执行相同 workload 的 wall-clock/CPU 时间有实质收益。

相对上一版 `pre_read_req` 优化后的 release 二进制：

- `elapsed`: `176.26 -> 174.73`，额外改善 `0.87%`
- `user`: `175.68 -> 174.20`，额外改善 `0.84%`

这与最新 profiling 里 `0.1% ~ 0.4%` 的小幅收益方向一致，说明该方向仍有正收益，但已经接近收敛。

## Current Hotspots

当前最新 profile 热点：

- `front_top.total`: `63582.460`
- `front_top.comb`: `51024.102`
- `front_top.bpu_stage`: `22784.657`
- `bpu.core_comb`: `19219.620`
- `bpu.tage_comb_total`: `14270.415`
- `front_top.read_stage`: `12324.882`
- `front_top.f2b_stage`: `4227.620`

解读：

- `icache` 不是当前最大热点
- `BTB/type predictor` 也不是当前最大热点
- 主要成本仍在 `front_top + BPU` 外围组合逻辑和胶水层对象搬运

## Quick Scope Timing Test

代表性快测配置：

- 构建：`build_bpu_llc1_scopeprof`
- 宏：
  - `CONFIG_BPU=1`
  - `CONFIG_AXI_LLC_ENABLE=1`
  - `FRONTEND_ENABLE_HOST_PROFILE=1`
  - `FRONTEND_HOST_PROFILE_SAMPLE_SHIFT=8`
- workload：
  - `./baremetal/linux.bin`
  - `--max-commit 2000000`
  - `--progress-interval 1000000`
- 日志：
  - `local_logs/real_bpu_llc/profile_scope_run_llc1_bpu1_2m_s256.log`

结果：

- `2000000 commit`
- `cycle=1474962`
- `ipc_total=1.35597`

Top-level host-side 时间占比（相对 `sim.cycle_total`）：

- `frontend`: `78.45%`
- `backend`: `16.33%`
- `memsubsystem`: `2.61%`
- `submodule_glue`: `1.88%`
- `uncategorized`: `0.74%`

这里的 `submodule_glue` 定义为：

- `csr_status`
- `clear_axi_inputs`
- `axi_outputs`
- `bridge_axi_to_mem`
- `bridge_mem_to_axi`
- `axi_inputs`
- `back2front`
- `axi_seq`

对应子项占比：

- `axi_outputs`: `0.83%`
- `axi_seq`: `0.54%`
- `axi_inputs`: `0.16%`
- `back2front`: `0.14%`
- `csr_status`: `0.06%`
- `clear_axi_inputs`: `0.06%`
- `bridge_axi_to_mem`: `0.04%`
- `bridge_mem_to_axi`: `0.04%`

补充细项：

- `sim.front_cycle`: `64088.940 ms`
- `sim.back_comb + sim.back_seq`: `13341.125 ms`
- `sim.mem.comb + sim.mem.seq + sim.mem.llc_*`: `2129.775 ms`
- `front_top.total`: `63812.755 ms`
- `bpu.core_comb`: `19232.836 ms`
- `bpu.tage_comb_total`: `14320.865 ms`
- `front_top.read_stage`: `12245.053 ms`

结论：

- 当前 wall-clock 主瓶颈非常明确地在 `Frontend`
- `MemSubsystem` 在当前 host-side 执行时间里占比很低，不是首要 wall-clock 问题
- 因此前面“先做 `front_top/BPU` host-side 优化、暂不优先做 LLC micro-arch”这个顺序是对的

## Frontend Internal Share Before/After

对比口径：

- `before`:
  - `local_logs/real_bpu_llc/profile_run_llc1_bpu1_2m_s256_stage2.log`
- `after`:
  - `local_logs/real_bpu_llc/profile_scope_run_llc1_bpu1_2m_s256.log`
- 分母统一取 `front_top.total`
- 所有百分比都表示“该模块/函数在 frontend 总时间中的占比”

说明：

- `Stage breakdown` 基本按前端阶段拆分，适合看 frontend 时间花在哪
- `Function/module hotspot` 是 inclusive 占比，函数项之间允许重叠，不要求和为 `100%`

### Stage Breakdown (% of frontend total)

- `seq_read`: `4.42% -> 6.04%`，`+1.61 pct`
- `read_stage`: `14.34% -> 19.19%`，`+4.85 pct`
- `bpu_stage`: `38.88% -> 35.79%`，`-3.09 pct`
- `icache_stage`: `0.50% -> 0.71%`，`+0.21 pct`
- `predecode_stage`: `0.81% -> 1.05%`，`+0.25 pct`
- `f2b_stage`: `5.05% -> 6.63%`，`+1.58 pct`
- `refresh_stage`: `17.62% -> 5.69%`，`-11.93 pct`
- `seq_write`: `4.64% -> 6.43%`，`+1.80 pct`
- `frontend_other`: `13.74% -> 18.46%`，`+4.72 pct`

### Hotspot Function/Module (% of frontend total)

- `bpu.core_comb`: `30.65% -> 30.14%`，`-0.51 pct`
- `bpu.tage_comb_total`: `18.06% -> 22.44%`，`+4.38 pct`
- `bpu.btb_comb_total`: `3.50% -> 3.02%`，`-0.49 pct`
- `bpu.submodule_seq_read`: `1.25% -> 1.89%`，`+0.64 pct`
- `icache.comb`: `0.77% -> 1.06%`，`+0.28 pct`
- `bpu.seq_read`: `0.66% -> 0.84%`，`+0.18 pct`
- `bpu.type_comb`: `0.53% -> 0.63%`，`+0.10 pct`
- `bpu.post_read_req`: `0.46% -> 0.49%`，`+0.03 pct`
- `bpu.pre_read_req`: `0.20% -> 0.32%`，`+0.12 pct`
- `bpu.data_seq_read`: `0.18% -> 0.19%`，`+0.01 pct`

结论：

- 前端内部最明显的改善来自 `refresh_stage` 大幅下降
- `bpu_stage` 作为 frontend 总量中的占比也下降了
- `bpu.core_comb` 的 frontend 占比基本持平，说明这一块虽然绝对时间已降，但仍然是前端里最重的核心热点
- `bpu.tage_comb_total` 的相对占比上升，不代表它绝对时间变差，而是其他胶水成本下降更快后，它在 frontend 内部更“暴露”了

## Is There Still Significant Space?

有，但已经从“低风险高回报”进入“中等风险中等回报”阶段。

### 仍然值得继续做的方向

1. 把 `BPU` 内部剩余临时 `*_CombOut` 和 copy-back 进一步收缩
2. 压 `front_top.read_stage` 的保存态结构拷贝
3. 在收益开始收敛后，再用 `perf` 或短窗口 `gprof` 做整程序交叉验证

当前判断：

- 上一轮 `pre_read_req` 仍有明确收益
- 这一轮 `nlp` 原位输出已经只剩 `0.1% ~ 0.4%` 级别
- release `5M` 上也只剩 `0.84% ~ 0.87%` 级别
- 因此“继续沿同类小 wrapper/copy 点往下抠”已经不再属于显著优化空间
- 真要再继续，下一步应该切到更结构化的点：
  - `BPU` 内剩余大的 `*_CombOut` 临时对象与 copy-back
  - `front_top.read_stage` 保存态对象
  - 或切到 `perf/gprof` 做新的热点确认

### Frontend Phase-1 Exit Decision

结论：

- 前端“简单实现、低风险、符合现有模拟器开发规范”的优化点，基本已经挖完
- 剩余还看得见的点并不是没有，而是已经进入“中等风险、收益不确定、需要改接口/改状态流”的阶段
- 因此这些剩余点不应该继续阻塞下一阶段工作

当前保留的 frontend backlog：

1. `BpuPredictMainCombOut` 改为直接写 `out/req`，减少一次大对象 copy-back
2. `BpuHistCombOut` 改为直接写 `req` 或拆小对象，减少大块 `memcpy`
3. `front_top.cpp` 中 `saved_fifo_out / saved_ptab_out` 的整结构复制改成更窄的数据流

判断：

- 上面三项都仍然遵循当前代码风格和模拟器开发约束
- 但它们已经不属于“简单点”，更像小型结构重排
- 预期收益不会再像当前 phase-1 这批优化那样显著

执行决策：

- frontend phase-1 到此结束
- 后续主线切回 correctness / IPC / LLC
- frontend backlog 保留为非阻塞项，只有在后续再次证明 host-side 时间仍明显卡在 frontend 时再回头处理

### 暂时不应优先做的方向

1. `LLC 多 outstanding`
2. `LLC micro-arch` 深改

原因：

- 当前 host-side 主要热点还不在 LLC
- 现在直接改 LLC，风险更高，但不一定先命中“给同事快速跑分”最需要的 wall-clock 问题

## Recommended Next Step

1. 保持 correctness 长跑不打断
2. 继续做一到两轮 `BPU/front_top` 胶水层压缩
3. 每轮都回归同一 `2M` profile workload 和同一 `5M` wall-clock workload
4. 如果单轮收益掉到约 `1%` 级别，再切换到更重的 profiling 工具
