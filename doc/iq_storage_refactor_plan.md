# IQ 存储收缩计划

目标：将 `IssueQueue` 的内部存储从通用 `UopEntry/MicroOp` 改为 IQ 专用最小条目，避免在 IQ 中保存执行后、提交后才会使用的信息。

测试标准：
- `make -j4` 通过
- `timeout 30s ./build/simulator ../image/mem/spec_mem/rv32imab_test/462.libquantum_test.bin` 在 30 秒内不出现断言、difftest error 或异常退出

## 阶段 1：分离传输结构和存储结构

目标：
- 新增 `IqStoredUop/IqStoredEntry`
- `IssueQueue` 改为存储专用条目，不再直接存 `UopEntry`
- 保持 `DisIssIO` 和 `IssPrfIO` 接口不变

做法：
- `IqStoredUop` 先对齐当前 IQ 实际需要的字段
- 入队时从 `DisIssUop` 转换为 `IqStoredUop`
- 发射时从 `IqStoredUop` 转换为 `IssPrfUop`

预期收益：
- 把 IQ 存储和流水线通用 `MicroOp` 解耦
- 为下一阶段按字段继续收缩打基础

## 阶段 2：收缩仅 IQ 使用的字段

检查维度：
- 调度排序需要：`op/rob_idx/rob_flag`
- 就绪判断和唤醒矩阵需要：`src1_en/src2_en/src1_busy/src2_busy/src1_preg/src2_preg`
- 发射后续需要：`IssPrfUop` 真正消费的字段

候选删除对象：
- `diag_val` 是否需要等到发射后才保留
- `ftq_*` 是否能延后到更后一级
- `page_fault_inst/illegal_inst` 是否必须在 IQ 中保存
- `dbg` 是否可以拆成更小 sideband

说明：
- 阶段 2 每次只删一类字段，并回归 `462.libquantum`

## 阶段 3：检查 IssueQueue 外部连带结构

范围：
- `LatencyEntry`
- `IssPrfUop`
- `PrfExeUop`

目标：
- 避免 IQ 收小后，后续一级仍然保留明显冗余字段
- 明确哪些字段是 IQ 阶段必须，哪些字段只是历史透传
