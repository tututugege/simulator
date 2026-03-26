# LOG 精简候选清单（更新版）

更新时间：2026-03-26

## 范围

- 只看非 submodule 代码。
- 不包含 `axi-interconnect-kit/**`。

## 本轮已完成（已落地）

1. `MemSubSystem/RealDcache.cpp`
- 已删除 `kCoremark*`、`FOCUS`、`FAST DIFF TRACE` 相关日志和辅助函数。
- 当前仅保留 replay/异常路径日志（11 处 `LSU_MEM_DBG_PRINTF`）。

2. `rv_simu_mmu_v2.cpp`
- 已删除 AXI bridge 逐拍日志（此前的 `[AXI->MSHR AR ACCEPT]` / `[AXI->MSHR RESP]`）。
- 当前剩余 `printf` 主要是 debug dump / 退出信息，不是访存热路径刷屏项。

3. `MemSubSystem/MemSubsystem.cpp`
- 已删除 PTW 高频请求日志（`[MEM][PTW][MEM REQ]`、`[MEM][PTW][WALK REQ]`）。

## 当前残留日志分布（非 submodule）

说明：下面是源码匹配计数，包含部分辅助打印函数中的调用。

| 文件 | `LSU_MEM_DBG_PRINTF` | `LSU_MEM_DBG_FPRINTF` | `std::printf/printf` |
|---|---:|---:|---:|
| `MemSubSystem/RealDcache.cpp` | 11 | 0 | 0 |
| `MemSubSystem/MSHR.cpp` | 5 | 0 | 13 |
| `MemSubSystem/WriteBuffer.cpp` | 25 | 0 | 7 |
| `MemSubSystem/MemSubsystem.cpp` | 2 | 0 | 42 |
| `back-end/Lsu/RealLsu.cpp` | 12 | 4 | 7 |
| `rv_simu_mmu_v2.cpp` | 0 | 0 | 7 |

## 残留日志清单与建议

## A. 建议保留（故障定位价值高）

1. `MemSubSystem/RealDcache.cpp`（replay 原因）
- `:393` `replay=3(bank_conflict)`
- `:479` `replay=2(mshr_pending_guard)`
- `:558` `replay=2(mshr_hit)`
- `:581` `replay=1(mshr_full)`
- `:616` `replay=2(first_mshr_alloc)`
- `:662` `store replay=3(bank_conflict)`
- `:703` `store replay=3(hit_wb_busy)`
- `:731` `store replay=3(fill_replace_conflict)`
- `:796` `store replay=3(wb_busy)`
- `:818` `store replay=2(fill_wait)`
- `:859` `store replay=1(mshr_full)`

2. `MemSubSystem/MSHR.cpp`（协议异常/一致性问题）
- `:97` / `:105` `[MSHR RESP UNEXPECTED]`
- `:488` `[AXI READ MISMATCH]`
- `:530` / `:537` `[MSHR AR ACCEPT UNEXPECTED]`

3. `MemSubSystem/WriteBuffer.cpp`（一致性与协议保护）
- `:164` `[WB STATE CORRUPT]`
- `:276` `:280` `:292` `:298` `:304` `:318` `[AXI WRITE MISMATCH]` 系列
- `:463` `:469` `[AXI WRITE ISSUE WARN]`
- `:499` `req_total_size (>31) WARN`
- `:578` `:585` `[AXI WRITE RESP UNEXPECTED]`

4. `back-end/Lsu/RealLsu.cpp`（关键异常）
- `:295` `[LSU][KILLED LDQ GC]`
- `:316` `[LSU][LD RESP TIMEOUT]`
- `:733` `[LSU][LD RESP STALE]`
- `:1014` `:1022` `[LSU][STD_MISMATCH]`（stderr）
- `:1094` `:1102` `[LSU][STQ_ALLOC_TRACE]`（mismatch 诊断）
- `:1671` `[LSU][STQ UNDERFLOW PRECHECK]`

5. `MemSubSystem/MemSubsystem.cpp`（一次性配置）
- `:757` `[MEM][AXI CFG]`
- `:765` `[MEM][AXI CFG][WARN]`

## B. 可继续删除（若目标是进一步降噪）

1. `back-end/Lsu/RealLsu.cpp`
- `:441` `[LSU] Load replay triggered ...`
- `:464` `[LSU] Store replay triggered ...`
- `:519` `[LSU][MMIO][LD ISSUE]`
- `:680` `[LSU][MMIO][ST ISSUE]`
- `:834` `[LSU][MMIO][LD RESP]`
- `:894` `[LSU][MMIO][ST RESP]`

说明：
- 这些日志可观测性有价值，但在长跑中会产生明显噪音。
- 如果仅关注错误定位，可以考虑只留 timeout/stale/mismatch/underflow 类。

## C. 定向调试日志（默认不高频，但仍是残留）

1. `MemSubSystem/MSHR.cpp`（FOCUS 线/族触发）
- `:166` `[MSHR WB OUT][FOCUS]`
- `:245` `[MSHR ALLOC][FOCUS]`
- `:303` `[MSHR STORE MERGE][FOCUS]`
- `:345` `[MSHR VICTIM]`
- `:386` `[MSHR WB]`
- `:423` `[MSHR RESP RAW][FOCUS]`
- `:446` `[MSHR FILL]`

2. `MemSubSystem/WriteBuffer.cpp`（固定 fast-diff 线触发）
- `:404` `:410` `:425` `:437` `:444` `[WB BYPASS TRACE]`
- 仅在 `kFastDiffFocusLine` 命中时打印，但一旦命中会很密。

3. `MemSubSystem/MemSubsystem.cpp`（FOCUS 线触发）
- `:896` `[MEMSUBSYS][MSHRWB->WB][FOCUS]`
- `:900`-`:904` 数据行打印

## D. 仅在调试/dump路径打印（通常无需清理）

1. `MemSubSystem/MemSubsystem.cpp`
- 大量 `std::printf` 位于 `dump_debug_state*` / `DEADLOCK` 相关函数。
- 非常驻热路径，不建议作为“降噪”优先项。

2. `rv_simu_mmu_v2.cpp`
- `RECENT-COMMIT` / `DIFF[SOC]` / `PF-TRACE` / 退出信息。
- 常规运行不会持续刷屏。

## 建议的下一步删减顺序（非 submodule）

1. 先处理 `RealLsu` 的 replay/MMIO 过程日志（B 类），噪音收益最大。
2. 再处理 `WriteBuffer` 的 `[WB BYPASS TRACE]`（C 类）。
3. 若仍需极简，再处理 `MSHR` 的 FOCUS 系列（C 类）。

