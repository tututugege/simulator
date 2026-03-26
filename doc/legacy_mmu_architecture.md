# 旧版 MMU 架构调研报告

## 概述

旧版 MMU 子系统（位于 `legacy/mmu/` 目录下）实现了一个支持 RISC-V Sv32 分页机制的虚实地址转换单元。其当前采用**统一 TLB** 设计，即由单个 `TLB` 实例同时服务于前端（指令取指）和后端（Load/Store）请求，并由单个 `PTW`（页表漫游器）提供缺失后的回填支持。

## 模块层级

- **MMU** (`legacy/mmu/MMU.cpp`)
  - 顶层封装模块。
  - 管理前端（`mmu_ifu_req/resp`）和后端（`mmu_lsu_req/resp`）的 IO 接口。
  - 包含：
    - **TLB** (`tlb` 实例)
    - **PTW** (`ptw` 实例)
  - 实现前端和后端 TLB 缺失请求到 PTW 之间的仲裁。

- **TLB** (`legacy/mmu/TLB.cpp`)
  - **统一 TLB**：单一结构 `entries[TLB_SIZE]`（默认 32 项）。
  - **端口**：
    - `ifu_io`：指令取指读端口。
    - `lsu_io[MAX_LSU_REQ_NUM]`：访存单元读端口数组（默认 4 个）。
    - `write_io`：用于 PTW 回填和冲刷 (`SFENCE.VMA`) 的写端口。
  - **仲裁**：在 `comb_arbiter` 中采用固定优先级（`LSU[0] > LSU[1] > ... > IFU`）来处理更新（PLRU/LRU）和命中的上报。
  - **逻辑**：
    - `lookup()`：用于检查 VPN 匹配的组合逻辑。
    - `comb_replacement()`：更新替换策略状态（PLRU/Random）。
    - `comb_write()`：处理来自 PTW 的回填。

- **PTW** (`legacy/mmu/PTW.cpp`)
  - **状态机**：实现硬件页表漫游（Sv32 二级页表查询）。
  - **状态**：`IDLE` -> `CACHE_1` -> `MEM_1` -> `CACHE_2` -> `MEM_2`。
  - **接口**：
    - `tlb2ptw`：接收来自 TLB 的缺失请求（由 MMU 仲裁）。
    - `ptw2tlb`：向 TLB 发送回填数据。
    - `dcache_req/resp`：与 D-Cache/内存交互以读取页表项（PTE）。
  - **当前行为**：
    - 模拟 D-Cache/内存延迟（当前代码中已注释或设为常量）。
    - 在 `MEM_1/MEM_2` 状态下直接访问 `p_memory`（模拟器内存）读取 PTE，在当前逻辑中实际上绕过了真实的 D-Cache 层级（尽管接口已存在）。

## 关键信号流

1.  **请求**：
    - 前端发送 `mmu_ifu_req`。
    - 后端发送 `mmu_lsu_req`。
    - `MMU::comb_frontend/backend` 调用 `tlb.lookup()`。

2.  **命中**：
    - 如果 `lookup` 匹配成功：
        - 生成带有 `paddr`（物理地址）的 `mmu_resp`。
        - `tlb.comb_arbiter` 在多个并发命中中选择一个来更新 PLRU 状态。
    - `tlb2ptw` 设置为“无缺失”。

3.  **缺失**：
    - 如果 `lookup` 失败：
        - `mmu_resp` 标识为 `miss`。
        - `MMU` 产生 `tlb_miss` 信号。
    - **仲裁**：`MMU::comb_arbiter` 选择一个缺失请求（优先级：`Backend[i] > Frontend`）通过 `tlb2ptw` 转发给 `PTW`。

4.  **页表漫游**：
    - `PTW` 检测到 `tlb2ptw.tlb_miss` 并转换为 `CACHE_1` 状态。
    - 执行二级页表查询（读取内存）。
    - 成功后，通过 `ptw2tlb.write_valid` 提交新条目。

5.  **回填**：
    - `TLB::comb_write` 检测到 `ptw2tlb.write_valid`。
    - 选择替换对象（无效项或 PLRU 替换项）。
    - 将新条目写入 `entries[]`。

## 重构计划：ITLB 与 DTLB 分离

为了支持高带宽的取指和访存而不产生冲突，需要将统一 TLB 进行拆分。

### 目标
将 **ITLB**（指令 TLB）和 **DTLB**（数据 TLB）分离，同时共享同一个 **PTW**。

### 建议架构

1.  **分拆 `TLB` 类使用**：
    - 在 `MMU` 内部实例化两个 `TLB` 对象：
        - `itlb`：连接到 `ifu_io`，大小可根据需要优化。
        - `dtlb`：连接到 `lsu_io`，大小可根据需要优化。

2.  **带仲裁的共享 PTW**：
    - `MMU` 必须维持仲裁器以序列化对单个 `PTW` 的访问。
    - `MMU::comb_arbiter` 的输入源更改：
        - 原本：`tlb.miss`（统一 TLB）。
        - 现状：`itlb.miss` vs `dtlb.miss`。
    - **回填逻辑**：
        - PTW 需要知道应该回填哪一个 TLB。
        - **方案 A**：`PTW` 返回一个 Tag/ID 标识请求源。
        - **方案 B**：`MMU` 追踪当前正在处理的请求来源。
        - **方案 C（最简）**：`PTW` 仅输出 PTE，`MMU` 根据 `PTW` 中存储的 `op_type`（指令 vs 数据）将 `ptw2tlb` 信号路由到正确的 TLB。

3.  **回填路由（MMU 逻辑）**：
    - `PTW` 已经存储了 `op_type` (`OP_FETCH`, `OP_LOAD`, `OP_STORE`)。
    - 如果 `ptw.out.op_type == OP_FETCH`：驱动 `itlb.write_io`。
    - 如果 `ptw.out.op_type == OP_LOAD/STORE`：驱动 `dtlb.write_io`。

4.  **TLB 类调整**：
    - `TLB` 类目前同时拥有 `ifu_io` 和 `lsu_io` 端口。
    - **清理建议**：可以从接口中移除未使用的端口，或者在实例化时简单地将未使用端口连接到 0 或设置 `valid=false`。
    - 理想情况下，`TLB` 类应该是通用的（例如通过模板设置端口数），但为了最小化代码变更，在 `MMU` 中仅连接所需端口的子集也是可以接受的。

### 任务清单
1.  修改 `MMU.h`，添加 `TLB itlb;` 和 `TLB dtlb;`。
2.  更新 `MMU::comb_frontend` 以使用 `itlb`。
3.  更新 `MMU::comb_backend` 以使用 `dtlb`。
4.  更新 `MMU::comb_arbiter`，在 `itlb` 和 `dtlb` 的缺失请求之间进行仲裁。
5.  更新 `MMU::comb_ptw`，根据 `op_type` 将 `ptw2tlb` 回填信号路由到正确的 TLB。
6.  确保 `SFENCE.VMA` 同时冲刷两个 TLB。
