 # 🚀 RISC-V Out-of-Order Pipeline Interface Definition (v2.0)

## 1. Uop Structures (Pipeline Flow)

这里定义了指令/微操作在流水线各级流动时携带的数据包结构。

| 结构体 (Struct) | 流水线阶段 (Stage) | 字段 (Field) | 位宽 (Width) | 说明 (Description) |
| :--- | :--- | :--- | :--- | :--- |
| **`DecRenUop`** | **IDU → Rename** | `type` | Enum | 译码指令类型 |
| | | `pc` | 32 bits | 程序计数器 |
| | | `imm` | 32 bits | 扩展后的立即数 |
| | | `csr_idx` | 12 bits | CSR 寄存器索引 |
| | | `func3` / `func7` | 3 / 7 bits | **辅助功能码** (用于 ALU 控制) |
| | | `dest_areg` | 6 bits | 目标架构寄存器 (rd) |
| | | `src1_areg` / `src2_areg` | 6 bits | 源架构寄存器 (rs1, rs2) |
| | | `tag` | 4 bits | 分支预测 Tag |
| | | `uop_num` | 2 bits | Uop 数量 |
| | | `*_en` | 1 bit | 寄存器读写使能 |
| | | `src1_is_pc` | 1 bit | **操作数1选择信号** (PC vs Reg) |
| | | `src2_is_imm` | 1 bit | **操作数2选择信号** (Imm vs Reg) |
| | | `*_inst` | 1 bit | 异常标记 (Illegal, PageFault) |
| **`RenDisUop`** | **Rename → Dispatch** | `pc` | 32 bits | 程序计数器 |
| | | `base` | Struct | **包含 `DecRenUop` 所有字段** (含 func/sel) |
| | | `dest_preg` | 7 bits | 目标物理寄存器 |
| | | `src1_preg` / `src2_preg` | 7 bits | 源物理寄存器 |
| | | `old_dest_preg` | 7 bits | 旧物理寄存器 |
| | | `src1_busy` / `src2_busy` | 1 bit | 源操作数 Busy 状态 |
| **`DisIssUop`** | **Dispatch → Issue** | `pc` | 32 bits | 程序计数器 |
| | | `op` | Enum | 微操作类型 (UopType) |
| | | `imm` | 32 bits | 立即数 |
| | | `dest_preg` | 7 bits | 目标物理寄存器 |
| | | `src1_preg` / `src2_preg` | 7 bits | 源物理寄存器 |
| | | `rob_idx` | 7 bits | ROB 条目索引 |
| | | `stq_idx` | 4 bits | Store Queue 索引 |
| | | `csr_idx` | 12 bits | CSR 索引 |
| | | `tag` | 4 bits | Branch Tag |
| | | `func3` / `func7` | 3 / 7 bits | **辅助功能码** (Explicitly Passed) |
| | | `src1_is_pc` / `src2_is_imm`| 1 bit | **操作数选择信号** (Explicitly Passed) |
| | | `src*_busy` | 1 bit | 忙状态位 |
| **`IssExeUop`** | **Issue → Execute** | `pc` | 32 bits | 程序计数器 |
| | | `op` | Enum | 具体微操作码 |
| | | `imm` | 32 bits | 立即数 |
| | | `dest_preg` | 7 bits | 写回目标 |
| | | `rob_idx` | 7 bits | ROB 索引 |
| | | `csr_idx` | 12 bits | CSR 索引 |
| | | `tag` | 4 bits | Branch Tag |
| | | `func3` / `func7` | 3 / 7 bits | **辅助功能码** (传递给 ALU) |
| | | `src1_is_pc` | 1 bit | **Src1 Mux 选择** (传递给 ALU) |
| | | `src2_is_imm` | 1 bit | **Src2 Mux 选择** (传递给 ALU) |
| | | `illegal_inst` | 1 bit | 异常透传 |
| **`ExeWbUop`** | **Execute → Writeback**| `op` | Enum | 微操作码 |
| | | `result` | 32 bits | 执行结果 / 访存地址 |
| | | `dest_preg` | 7 bits | 写回目标物理寄存器 |
| | | `rob_idx` | 7 bits | ROB 完成通知 |
| | | `tag` | 4 bits | Branch Tag |
| | | `page_fault_*` | 1 bit | 访存异常 |
| **`RobUop`** | **ROB Entry** | `type` | Enum | 指令类型 |
| | | `pc`, `instruction` | 32 bits | 调试与恢复 |
| | | `dest_preg` / `old_dest_preg`| 7 bits | 物理寄存器管理 |
| | | `dest_areg` | 6 bits | 架构寄存器恢复 |
| | | `tag` | 4 bits | Branch Tag |
| | | `pc_next`, `br_taken` | 32/1 bit | 分支更新 |
| | | `mispred` | 1 bit | 误预测标记 |
| | | `uop_num` / `cplt_num` | 2 bits | Uop 完成计数 |

---

## 2. Module Interface Definitions (Top-Level Ports)

模块间的物理接口定义。

| 模块接口 (Interface) | 端口方向 (Dir) | 信号名 (Signal) | 数据包/说明 (Payload) |
| :--- | :--- | :--- | :--- |
| **`IduIO`** | Input | `from_front` | `inst` [32], `valid` |
| (Fetch & Decode) | Input | `from_ren` | `ready` |
| | Input | `from_back` | `flush`, `mispred`, `br_tag` |
| | Output | `to_ren` | **`DecRenUop`** |
| | Output | `to_back` | `br_mask`, `br_tag`, `mispred` |
| **`RenIO`** | Input | `from_dec` | `DecRenUop` |
| (Rename) | Input | `from_rob` | `commit_areg` [6], `commit_preg` [7] |
| | Input | `from_back` | `wake_preg` [7] |
| | Output | `to_dis` | **`RenDisUop`** |
| **`DispatchIO`** | Input | `from_ren` | `RenDisUop` |
| (Dispatch) | Input | `from_iss` | `ready_num` (Credits) |
| | Input | `from_rob` | `full`, `ready` |
| | Output | `to_rob` | **`RobUop`** |
| | Output | `to_iss` | **`DisIssUop`** |
| **`IssueIO`** | Input | `from_dis` | `DisIssUop` |
| (Issue Queue) | Output | `to_exe` | **`IssExeUop`** |
| | Output | `awake_bus` | `wake_preg` [7] |
| **`ExuIO`** | Input | `from_iss` | `IssExeUop` + **`src_data` [32]** |
| (Execution) | Output | `to_back` | **`ExeWbUop`** |
| **`RobIO`** | Input | `from_dis` | `RobUop` |
| (Reorder Buffer) | Input | `from_exe` | `ExeWbUop` |
| | Output | `to_ren` | Commit Info |
| | Output | `to_all` | `flush` |