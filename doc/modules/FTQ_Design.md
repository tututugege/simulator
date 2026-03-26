# FTQ (Fetch Target Queue) 模块设计文档

## 1. 概述
FTQ (Fetch Target Queue) 用于保存前端每个 Fetch Block 的分支预测元数据，并在后端执行与提交阶段提供查询与回收能力。

当前实现中，FTQ **内聚在 IDU 内部**，作为 IDU 的私有子模块，不再由 BackTop/Exu 等模块直接持有指针。跨模块访问均通过显式 IO 完成。

## 2. 当前实现形态

### 2.1 层次关系
- `Idu` 内部持有 `FTQ ftq`。
- `BackTop` 不再持有 `FTQ*`。
- `Exu/BRU` 通过 `IduOut.ftq_lookup` 读取 FTQ 条目。
- 前端训练路径（`rv_simu_mmu_v2.cpp`）也通过 `idu->out.ftq_lookup` 读取 FTQ 条目。

### 2.2 时序模型
FTQ 采用与后端其他模块一致的两阶段模型：
- 组合逻辑：`comb_begin/comb_alloc_req/comb_ctrl/comb_commit_reclaim`
- 时序提交：`seq`

其中：
- `comb_begin()`：建立 next-state（`entries_1/head_1/tail_1/count_1`）。
- `comb_alloc_req()`：处理分配请求，返回 `alloc_resp`。
- `comb_ctrl()`：处理 flush/recover 控制。
- `comb_commit_reclaim()`：根据 commit 中 `ftq_is_last` 统计回收数量并统一 `comb_pop(pop_cnt)`。
- `seq()`：提交 next-state。

## 3. IO 定义

### 3.1 FTQ 输入 (`FTQIn`)
- `rob_commit`：提交信息，用于回收判断。
- `alloc_req`：分配请求（由 IDU 组合阶段产生）。
- `flush_req`：流水线 flush 控制。
- `recover_req`：分支误预测恢复控制。
- `recover_tail`：恢复后的 tail 目标位置。

### 3.2 FTQ 输出 (`FTQOut`)
- `alloc_resp`：分配结果（是否成功 + 分配索引）。
- `status`：队列状态（`full/empty`）。
- `lookup`：完整 FTQ 条目数组镜像，供 EXU/前端训练读取。

## 4. 核心工作流
1. IDU 在 `comb_fire` 中根据前端输入构造 `FTQEntry` 并通过 `alloc_req` 发起分配。
2. FTQ 在 `comb_alloc_req` 返回 `alloc_resp.idx`，IDU 在 `seq` 中将该 `ftq_idx/offset/is_last` 写入 ibuf 指令。
3. 指令携带 `ftq_idx/ftq_offset` 进入后端，BRU 通过 `ftq_lookup` 读取预测元数据进行校验。
4. 提交阶段 FTQ 根据 `commit_entry[].uop.ftq_is_last` 统一回收多个条目。
5. flush/误预测时，通过 `flush_req/recover_req/recover_tail` 控制 FTQ 状态恢复。

## 5. 关键约束
- FTQ 条目回收时不清空 payload（仅移动指针/计数），避免在同拍或紧邻拍读取 `ftq_idx` 时丢失元数据。
- 误预测恢复语义：`tail <- (mispred_ftq_idx + 1) % FTQ_SIZE`。
- `count` 作为当前稳定的占用状态表示；判空判满由 `count` 派生。

## 6. 与硬件对齐说明
当前软件模型已经按“显式 IO + comb/seq”风格实现，便于后续映射到 RTL：
- 控制路径统一通过 IO 信号传递（不跨模块直接改状态）。
- 数据查询通过 `lookup` 端口完成（不使用跨模块函数调用）。

## 7. 验证状态
回归使用：
- `./build/simulator ../image/linux/linux.bin`

当前版本可稳定通过快速回归窗口（例如 `timeout 3s` 持续运行）。
