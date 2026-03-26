# Branch Mask 重构阶段对比（相对 `ae83696a`）

## 1. 对比范围
- 基线提交：`ae83696a`
- 对比目标：当前工作区（含未提交改动）
- 统计范围：`back-end/` 与 `doc/`

## 2. 变更文件概览（back-end）
- `back-end/Idu.cpp`
- `back-end/include/Idu.h`
- `back-end/Ren.cpp`
- `back-end/include/Ren.h`
- `back-end/Dispatch.cpp`
- `back-end/Isu.cpp`
- `back-end/include/Isu.h`
- `back-end/include/IssueQueue.h`
- `back-end/Prf.cpp`
- `back-end/Exu/Exu.cpp`
- `back-end/Exu/include/AbstractFU.h`
- `back-end/Lsu/SimpleLsu.cpp`
- `back-end/Lsu/include/SimpleLsu.h`
- `back-end/include/IO.h`
- `back-end/include/types.h`
- `back-end/Rob.cpp`
- `back-end/include/PerfCount.h`

## 3. 核心功能变更

### 3.1 IDU：从 tag_list 恢复到 br_mask 语义
- 删除 `tag_list/enq_ptr/now_tag` 主恢复路径，改为：
  - `now_br_mask`
  - `br_mask_cp[]`
- 误预测恢复依据 `br_id + br_mask_cp` 完成。
- 新增 `clear_mask` 驱动的本地状态清理。
- ID 阶段新增旁路清理：新译码指令 `br_mask` 在生成时就 `& ~clear_mask`。

### 3.2 Exu：统一产出分支控制广播
- `clear_mask` 由本拍所有已解析分支 OR 得到并输出到 `exu2id`。
- 误预测分支输出 `br_id`，`br_tag` 保留为兼容镜像。
- 对 FU 与流水寄存条目增加 clear 后清位路径。
- kill 判定改为基于 `uop.br_mask & dec_bcast.br_mask`。

### 3.3 Ren：检查点索引切到 br_id
- 检查点存储改为 `*_brid`（`RAT_checkpoint/alloc_checkpoint`）。
- 恢复路径以 `dec_bcast.br_id` 为索引。
- pipeline 中新增 clear_mask 清位（包括新接收与保留条目）。

### 3.4 Dispatch：补齐清位闭环
- `comb_alloc`、`comb_fire`、`comb_pipeline` 三处补齐 `br_mask &= ~clear_mask`。
- LSU 分配通道的 `tag/ldq_tag` 数据源由 `tag` 切为 `br_id`。

### 3.5 Isu / IssueQueue：flush 接口与行为简化
- `IssueQueue::flush_br` 从带重定向参数的签名简化为仅 `mask`。
- `IssueQueue` flush 逻辑改为纯 `br_mask` 清除；`clear_br` 保留用于解析后清位。
- `Isu` 的 latency 管道误预测清除改为纯 `br_mask`。

### 3.6 LSU：去年龄补丁并保留 clear 路径
- `handle_mispred` 与 `find_recovery_tail` 改为纯 `br_mask`。
- 接口签名同步去除冗余重定向参数。
- 保留并使用 `clear_mask` 对 LDQ/STQ/完成队列进行清位。

### 3.7 Prf：去年龄补丁
- kill 判定改为纯 `br_mask`。
- pipeline 保留 clear_mask 清位。

## 4. IO 与数据结构调整
- `DecBroadcastIO` 新增/强化字段：
  - `br_id`
  - `clear_mask`
- `ExuIdIO` 新增/强化字段：
  - `br_id`
  - `clear_mask`
- `InstInfo/MicroOp` 新增 `br_id` 字段（与旧 `tag` 并存过渡）。

## 5. 冗余逻辑检查结果

### 5.1 已清理
- `Isu/Prf/Exu/Lsu` 中基于 `is_younger` 的年龄补丁逻辑已移除。
- `IssueQueue` 与 `Lsu` 的 flush/mispred 接口冗余重定向参数已删除。

### 5.2 仍保留（合理）
- `redirect_rob_idx` 在 ROB 端恢复定位仍需要，属于结构性控制信号，不是年龄补丁冗余。
- `tag` 字段仍在 `InstInfo/MicroOp` 保留，当前为过渡兼容字段，后续可在全链路确认后再彻底移除。

## 6. 当前阶段结论
- 从 `ae83696a` 到当前版本，分支恢复主语义已从“tag 驱动”转为“br_id + br_mask + clear_mask 驱动”。
- 清位路径已形成闭环（IDU/Ren/Dispatch/Isu/Exu/Lsu），并已支持去除主要年龄补丁。
- 后续可继续做的收敛工作：
  - 删除 `tag` 兼容字段及相关镜像赋值。
  - 进一步收敛 `IO.h` 中仅为兼容保留的字段。
