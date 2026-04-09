# Front-end Comb/Seq 书写规范（BSD 训练友好统一版）

适用范围：
- 目录：`front-end/`
- 重点：除 `icache/` 子目录内部实现和 `FrontTop.cpp` 外的 front-end 代码
- `icache` 模块通过其声明的接口（`icache_seq_read`, `icache_comb_calc`, `icache_seq_write`）被顶层调用，其内部实现不在本规范范围内
- 目标对象：所有参与前端重构、维护、训练数据提取、训练清单统计的开发者/Codex

关联手册：
- `FRONTEND_COMB_SEQ_REQUIREMENTS_MANUAL.md`
- `FRONTEND_TOPLEVEL_TRAINING_MANUAL.md`

本手册是上述两份手册与后续讨论结论的统一落地版本；若出现冲突，以本手册为准。

---

## 0. 术语定义

- **BSD**：本项目对”神奇的黑盒函数训练”机制的正式命名。所有待训练的纯组合函数最终由 BSD 训练流程消费。
- **训练函数列表**：交付给 BSD 训练人员的正式函数清单，记录在 `TRAINING_FUNCTION_LIST.md` 中。
- **清单 comb**：出现在训练函数列表中的 `*_comb` 函数。
- **非清单 comb**：合法的 `*_comb` 函数，但因被上层清单 comb 包含而不单独列入清单。
- **Falcon**：前端性能统计分析器。所有 Falcon 相关代码不纳入 BSD 规范约束（详见 Section 15）。

---

## 1. 终极目标：把 front-end 整理成”BSD 训练友好”的形式

本项目是**周期精确 CPU 模拟器**。front-end 重构的首要目标，不是代码表面整洁，而是让前端逻辑可以被**稳定、清晰、可验证地交付给 BSD 训练流程**。

最终交付物 = **BSD 训练友好代码** + **训练函数列表**。

这里的”BSD 训练友好”具体指：

1. 我们能明确指出：
   - 哪些函数是待训练函数（`*_comb`）；
   - 它们的输入/输出结构体在哪里；
   - 每个字段的逻辑位宽是多少；
   - 位宽是否最终落实到 `wireX_t` / `*_BITS` 体系。
2. 所有 `*_comb` 函数都必须**严格满足纯组合逻辑要求**：
   - 只依赖输入、常量、局部变量；
   - 不读取或写入持久状态；
   - 不依赖全局可变状态、静态跨调用状态；
   - **此规则具有传递性**：无论 comb 是直接读还是通过调用链间接读持久状态，均属违规。
3. 面对大表项/大状态时，训练边界必须合理拆开：
   - 先由地址/索引类 `*_comb` 算出读请求；
   - 再由 `*_seq_read` 按请求读取表项；
   - 最后由后级 `*_comb` 仅基于”读出的表项”完成决策。
4. 最终交付给训练人员的是**训练函数列表**：
   - 清单根据代码实际状态导出，是代码状况决定清单状况；
   - 选取口径：**功能完全覆盖** + **功能最上层覆盖**（详见 Section 5）。

一句话概括：

- 我们追求的不是”所有逻辑都拆得越碎越好”；
- 而是”训练边界清晰、纯组合严格、时序壳透明、必要时可继续细拆”。

---

## 2. 设计原则与优先级

### 2.1 总原则

- `comb` 负责“算什么”。
- `seq_read` / `seq_write` 负责“读什么 / 写什么”。
- `comb_calc` 负责“在必要时组织 `comb -> seq_read -> comb` 胶水流程”。

### 2.2 优先级顺序

代码评审时，判断优先级如下：

1. 是否形成了**清晰的待训练 `*_comb` 边界**。
2. 这些 `*_comb` 是否**真的纯组合**。
3. 大状态/大表项是否做了**地址计算 / 表读取 / 决策计算**的合理拆分。
4. `seq_read` / `seq_write` 是否严格守边界。
5. `comb_calc` 是否只在必要处出现，且只承担胶水职责。

注意：
- `comb_calc` **不是刚需**；
- 不应为了凑三段式而机械制造 `comb_calc`；
- 但凡确实需要胶水组织，就必须显式、透明、守规矩。

---

## 3. 三类函数的硬性定义

### 3.0 全局类型约束

本项目**禁止使用 signed 类型**作为信号/状态/IO 字段类型。包括但不限于 `int`、`int8_t`、`int16_t`、`int32_t`。

- 所有信号、状态、IO 字段必须使用 `wireX_t`（unsigned）或由其构成的语义 alias；
- 如发现代码中仍残留 signed 类型，应即时汇报并修正；
- 此约束适用于所有 comb、seq_read、seq_write 中的信号/状态字段，不仅限于清单 comb。

## 3.1 `*_seq_read`

允许：
- 读取本模块持久状态：
  - 寄存器
  - 队列状态
  - 表项
  - SRAM 模型状态
- 根据**外部已算好的**读地址 / 读 bank / 读使能读取表项，并把结果放入 `ReadData`
- 在确有需要时保留 SRAM delay 行为模拟
- 形成状态快照、表项快照、可供后续组合逻辑消费的 `ReadData`

禁止：
- 任何策略计算、命中选择、victim 选择、更新决策
- 任何 index/tag/address/bank 生成逻辑，即使逻辑很简单也不行
- 在 `seq_read` 内调用任何 `*_comb`，或调用本质等价于 `*_comb` 的 helper
- 修改持久状态（包括 hidden state / shadow state / pending state）
- 偷偷决定“读哪张表/读哪个 bank/走哪个 fallback”

特别注意：
- 即使底层存储体被当作 0 延迟 Reg，也必须先在 `seq_read` 中显式读出，再传给 `comb`
- 不允许在 `comb` 中直接摸底层数组做“隐式读表”
- `seq_read` 输入如果需要地址、bank、使能，这些必须由上一级 `*_comb` 或 `comb_calc` 事先算好并显式传入

合法链条：

`*_comb -> *_seq_read -> *_comb`

不合法链条示例：

- `*_seq_read` 内部先算地址，再读表
- `*_seq_read -> helper_comb -> 表读取`
- `*_comb` 里直接 `table[idx]`

---

## 3.2 `*_comb`（纯组合函数）

`*_comb` 分为两类：**清单 comb**（列入训练函数列表）和**非清单 comb**（被上层清单 comb 内部调用的子级 helper）。

### 3.2.1 所有 `*_comb` 的通用强制要求

以下规则对**所有** `*_comb` 生效，无论是否在训练清单上：

- 命名必须为 `*_comb`
- **函数开头必须先把 `output` 置默认值**（清零或默认结构体），不允许依赖调用者预先 `memset`
- 计算只能依赖：
  - `input`（函数参数）
  - 常量
  - 局部临时变量
- **禁止读写模块持久状态**（寄存器、表项、SRAM 等）
- **此规则具有传递性**：无论是 comb 自身直接读，还是通过调用链中的子 comb/helper 间接读，均属违规
- 禁止依赖全局可变状态、静态跨调用状态
- 禁止调用任何 `*_comb_calc`
- `*_comb` 内部允许调用其他 `*_comb`（子级纯组合函数）

### 3.2.2 清单 comb 的额外强制要求

以下规则**仅对训练函数列表中的 `*_comb`** 生效：

- 函数参数必须**严格只有两个**：`const XxxCombIn& input` + `XxxCombOut& output`
- `CombIn` / `CombOut` 结构体中的**所有叶子字段**必须是 `wireX_t` 或由 `wireX_t` 构成的语义 alias
- 禁止裸 `bool`（应使用 `wire1_t`）、裸 `int`/`uint32_t`/`uint8_t`（应使用对应语义 alias）出现在清单 comb 的 IO 叶子位置
- 输入/输出字段应可被稳定抽取并统计位宽，最终能追溯到 `*_BITS`

### 3.2.3 非清单 comb

- 不要求严格二参签名
- 不要求 IO 叶子字段必须是 `wireX_t`
- 但仍必须满足 3.2.1 中的所有通用规则（纯组合、不摸时序、开头清零 output）

### 3.2.4 训练函数列表的选取

清单不是预先规定的，而是**根据代码实际状态导出**：

1. 首先确认候选函数是合法的 `*_comb`（满足 3.2.1）
2. **功能完全覆盖**：清单中的 comb + comb_calc 胶水 + seq_read/write = 模拟器范围内全部功能逻辑，不允许有功能逻辑遗漏在清单之外
3. **功能最上层覆盖**：若 `A_comb` 内部包含 `A1_comb` + `A2_comb` 且 `A_comb` 已完整覆盖对应功能，只列 `A_comb`
4. 代码变更后应同步更新训练函数列表

---

## 3.3 `*_seq_write`

允许：
- 根据 `comb` 产出的 `UpdateRequest` / `Delta` / `NextReadData` 提交写入
- reset 时执行模块状态复位
- 把 `comb` 已经完全决定好的 next-state 写回持久状态

禁止：
- 在 `seq_write` 再推导 next-state 策略
- 再做“到底写不写、写哪一路、写哪项、写多少”的二次判定
- 借 `seq_write` 补做本应放在 `comb` 的命中、victim、更新策略逻辑

---

## 4. `comb_calc` 的定位：可选，但一旦出现就要规范

### 4.1 什么时候需要 `comb_calc`

`comb_calc` 只在以下场景需要：

- 单一模块内部需要组织多段：
  - `comb -> seq_read -> comb`
  - 或 `comb -> seq_read -> comb -> seq_read -> comb`
- 需要把多个下层模块的 `seq_read` / `comb` 拼接起来
- 需要显式承载”胶水逻辑”，但又不能把它塞回 `seq_read` 或 `seq_write`

### 4.2 什么时候不需要 `comb_calc`

如果一个模块天然就是：

- `seq_read`
- `comb`
- `seq_write`

且中间不存在额外胶水组织需求，那么**可以没有 `comb_calc`**。

换句话说：
- “没有 `comb_calc`”本身不是违规；
- “需要胶水却用不透明 wrapper 乱包一层”才是问题。

### 4.3 `comb_calc` 的硬要求

`comb_calc` 内部：
- 以组织流程为主，**永远不进入训练函数列表**，BSD 不消费 `comb_calc`
- `comb_calc` 的参数不要求严格二参，其 IO 不强制 `wireX_t`
- 允许调用：
  - `*_comb`
  - `*_seq_read`
  - 必要时允许调用下层 `*_comb_calc`
- 不允许调用：
  - `prepare_*`
  - `helper_*`
  - `finalize_*`
  - `misc_*`
  - `step()`
  - `run_*()`
  - `comb_calc_only()`
  - `xxx_instruction()`
  - 或其他语义不透明的 legacy wrapper

目标是：让调用链一眼就能看出训练边界和时序边界。

补充说明：

- 若某个函数内部需要调用下层 `*_comb_calc`，则它自身就**不能**继续命名为 `*_comb`；
- 这类函数必须归类为 `*_comb_calc`；
- `*_comb_calc` 可以组织：
  - 上层 `*_comb`
  - `*_seq_read`
  - 下层 `*_comb`
  - 必要下层 `*_comb_calc`
- 但无论是否允许调用下层 `*_comb_calc`，`*_comb_calc` 的本质都仍然是**胶水流程函数**，不是训练主体。

进一步明确：

- 理想状态下，`comb_calc` 应尽量收敛为”函数调用 + 少量显式连接”；
- 但在工程上，如果强行继续拆分只会制造大量薄包装、明显增加等价改写风险，允许 `comb_calc` 保留**少量顶层胶水逻辑**；
- 这里的”少量顶层胶水逻辑”通常包括：
  - 局部临时变量承接；
  - 子模块输入输出的简单搬运、打包、解包；
  - 少量与调用顺序强绑定的 enable / valid / ready / mux 连接；
  - 顶层 orchestration 中难以再独立命名、训练价值很低的零散连接。
- 但以下内容仍然**不应**继续残留在 `comb_calc`：
  - 明显成块、可独立命名的大段功能逻辑；
  - 具有独立语义的主决策逻辑；
  - 可单独说明输入输出语义的 next-state 计算；
  - 训练覆盖意义上的”大头”组合功能。

换句话说：

- 规范目标不是把 `comb_calc` 机械压成”绝对零逻辑”；
- 而是确保**大头功能 logic 必须提纯为纯 `*_comb`**；
- `comb_calc` 中若有残留，也只能是少量、透明、不可再有效拆分的顶层胶水。

---

## 5. 训练函数列表：清单 comb vs 非清单 comb

这是本项目 BSD 训练交付的关键口径。

### 5.1 核心原则：清单跟着代码走

训练函数列表不是预先设计的，而是**根据代码实际状态导出**的：

1. 先审视代码中所有合法的 `*_comb` 函数
2. 按**功能完全覆盖** + **功能最上层覆盖**两条口径筛选
3. 筛选结果即为训练函数列表
4. 代码变更后必须同步更新列表

### 5.2 两条筛选口径

#### 功能完全覆盖

清单中的 comb + comb_calc 胶水 + seq_read/write = 模拟器范围内**全部功能逻辑**。

- 不允许有功能逻辑遗漏在清单之外（Falcon 统计逻辑豁免，见 Section 15）
- 如果某段功能逻辑目前既不在某个清单 comb 中，也不在胶水/时序壳中，则说明拆分不完整

#### 功能最上层覆盖

若 `A_comb` 内部调用了 `A1_comb` + `A2_comb` 且 `A_comb` 已完整覆盖对应功能：
- 只列 `A_comb`
- `A1_comb`、`A2_comb` 作为非清单 comb 保留，不进入正式清单

### 5.3 清单 comb 与非清单 comb 的要求差异

| 要求 | 清单 comb | 非清单 comb（子级/helper） |
|------|----------|--------------------------|
| 纯组合（不摸时序，传递性） | **必须** | **必须** |
| 函数开头 output 清零 | **必须** | **必须** |
| 严格二参签名 `(const In&, Out&)` | **必须** | 不要求 |
| IO 叶子字段全部 `wireX_t` | **必须** | 不要求 |
| 禁止 signed 类型 | **必须** | **必须**（全局规则） |
| 进入训练函数列表 | 是 | 否 |

### 5.4 评审时的判断方式

当你看到若干 `*_comb` 时，不要只问”它纯不纯”，还要问：

1. 它是不是**真正的训练边界**（清单 comb 候选）
2. 它的功能是否已能完整覆盖一个有意义的逻辑单元
3. 它是否仍依赖未展开的时序访问或不透明 wrapper

如果答案是：
- “纯组合，但只是地址生成/局部子步骤”
  - 则它通常属于非清单 comb
- “纯组合，且能完成模块级核心决策”
  - 则它更可能是清单 comb

---

## 6. 模块接口与层次组织规范

### 6.1 推荐对外接口

模块对外优先提供：
- `*_seq_read`
- `*_comb_calc`（若该模块确有胶水需求）
- `*_seq_write`

若模块无胶水需求，也可对外只保留：
- `*_seq_read`
- `*_comb`
- `*_seq_write`

### 6.2 顶层入口

- `front_top` 是唯一后端调用入口
- `front_top` 内部主路径必须固定为：

`front_seq_read -> front_comb_calc -> front_seq_write`

说明：
- 这里的 `front_comb_calc` 是顶层总调度层，保留是合理的
- 但顶层之外不应随意保留第二套行为入口

### 6.3 legacy 路径处理原则

非运行必需的历史路径应清理，包括但不限于：
- `step()`
- `step_pipeline()`
- `legacy_top()`
- `run_*()`
- `*_seq()` 这类仅转发到 `*_seq_write` 的包装函数
- 直接暴露内部状态的 `peek_*()` / `get_*()` 旁路接口

若暂时保留兼容层，必须满足：
- 最终仍复用主三段式路径
- 不引入第二套行为分叉
- 不破坏训练边界和状态观测边界

---

## 7. 大表项/大状态的标准拆法

面对大寄存器堆、表项数组、FIFO、预测表时，标准拆法如下：

### 7.1 推荐拆分模式

1. `addr/index/tag/..._comb`
   - 输入：请求、状态快照、必要上下文
   - 输出：读地址、bank、tag、使能等
2. `*_seq_read`
   - 只根据上一步算出的请求读表
3. `core/..._comb`
   - 只基于“读出的表项 + 输入上下文”做选择、输出、更新请求生成

### 7.2 为什么必须这样拆

因为训练人员不希望拿到一个超大黑盒，输入里塞满整个表：
- 这会让 IO 维度暴涨
- 让训练边界模糊
- 让训练数据抽取极不稳定

所以：
- 地址计算属于组合逻辑
- 表读取属于时序外壳
- 决策属于组合逻辑

### 7.3 禁止写法

- `seq_read` 里先算 idx/tag/bank 再读表
- `comb` 里直接访问底层数组
- `seq_write` 里再根据旧状态决定实际写法

---

## 8. FIFO / 多次组合调用的特别约束

front-end 顶层同一拍内，可能对同一个 FIFO/队列多次观察“虚拟下一状态”。

硬约束：
- `comb_calc` 不能修改模块隐藏持久状态
- 不能通过模块私有成员暂存“pending delta”，再让下一次 `comb_calc` 隐式读到
- 必须通过显式参数/显式 `next_read_data` 传递虚拟下一状态

正确做法：
- `comb` 只输出 `delta/request/next_rd`
- 上层或模块内显式聚合
- `seq_write` 统一提交

换句话说：
- 允许显式的“虚拟下一拍视图”
- 不允许隐式的“模块偷偷记住了上一轮组合结果”

---

## 9. 位宽语义与训练 IO 规范

### 9.1 位宽口径

- 统计与训练口径必须使用**逻辑位宽**
- 不能使用 C++ 类型 padding 位宽
- 统一以 `wire_types.h` 中 `*_BITS` 为准

### 9.2 训练 IO 字段要求

清单 comb 的 `CombIn` / `CombOut` 结构体中：
- **所有叶子字段**必须是 `wireX_t` 或由 `wireX_t` 构成的语义 alias
- 禁止裸 `bool`（应使用 `wire1_t`）
- 禁止裸 `int`、`uint32_t`、`uint8_t` 等无法追溯逻辑位宽的类型
- 嵌套子结构体中的叶子字段同样必须满足此要求

非清单 comb 和 comb_calc 的 IO 不强制此要求，但推荐遵循。

全局约束（适用于所有代码）：
- 禁止 signed 类型（`int`、`int8_t`、`int16_t`、`int32_t`）作为信号/状态/IO 字段
- 发现残留 signed 类型应即时汇报并修正

### 9.3 跨模块 / FIFO 的窄位宽信号

窄位宽信号跨模块传递、进出 FIFO、进出队列表项时：
- 写入要 mask
- 读出要 mask

典型例子包括但不限于：
- `pcpn`
- `tage_idx`
- `tage_tag`
- 各类 set/way/tag/index 类型

### 9.4 训练方查看路径

推荐路径：
1. 先看 `train_IO.h`
2. 再追到具体模块结构体
3. 最后在 `wire_types.h` 中确认 `wireX_t` 与 `*_BITS`

---

## 10. 面积统计与资源分类口径

训练交付除函数 IO 外，还需要资源规模统计。

规则：
- 除 TAGE / BTB 中显式做了 SRAM delay 建模的部分外，其余按 Reg 统计
- 面积统计使用逻辑位宽
- 统计文档必须明确使用的是 `*_BITS` 口径，而不是 `sizeof(type) * 8`

至少应能输出：
- 每个待训练函数的 input bits / output bits
- 每个模块的 Reg bits / SRAM bits / Total bits

---

## 11. 推荐实现模板

### 11.1 无额外胶水需求的模块

```cpp
void module_seq_read(const ModuleSeqReadIn& in, ModuleReadData& rd) {
  memset(&rd, 0, sizeof(rd));
  // 只抓状态快照 / 读表
}

void module_comb(const ModuleCombIn& input, ModuleCombOut& output) {
  memset(&output, 0, sizeof(output));
  // 只依赖 input / 常量 / 局部变量
}

void module_seq_write(const ModuleSeqWriteReq& req, bool reset) {
  if (reset) { reset_all(); return; }
  // 只提交 req
}
```

### 11.2 需要胶水流程的模块

```cpp
void module_seq_read(const ModuleSeqReadIn& in, ModuleReadData& rd) {
  memset(&rd, 0, sizeof(rd));
  // 只抓状态快照
}

void module_comb_calc(const ModuleInput& inp,
                      const ModuleReadData& rd,
                      ModuleOutput& out,
                      ModuleUpdateReq& req) {
  memset(&out, 0, sizeof(out));
  memset(&req, 0, sizeof(req));

  ReadAddrCombOut addr_out{};
  module_addr_comb(ReadAddrCombIn{inp, rd}, addr_out);

  ModuleReadData working_rd = rd;
  module_data_seq_read(ModuleDataSeqReadIn{addr_out}, working_rd);

  module_core_comb(ModuleCoreCombIn{inp, working_rd, addr_out}, core_out);
}

void module_seq_write(const ModuleUpdateReq& req, bool reset) {
  if (reset) { reset_all(); return; }
  // 只提交 req
}
```

---

## 12. 典型反例（禁止）

- `seq_read` 里先算 `idx/tag/bank`，再读表
- `seq_read` 里调用 `prepare_*()` / `helper_*()`，而 helper 本质上在做组合策略
- `comb` 读取模块成员数组、全局变量、静态跨调用状态
- `comb` 依赖调用者提前把输出清零，自己不做默认化
- `seq_write` 根据旧状态再次决定最终写法
- `comb_calc` 内部塞入一堆不透明 wrapper，而不是直接组织 `*_comb + *_seq_read`
- 暴露 `peek_*()` / `get_*()` 之类旁路状态读取接口，绕过 `seq_read`
- 保留第二套可运行主路径，导致三段式主路径不再唯一

---

## 13. 代码评审检查清单（提交前）

### 13.1 训练边界

1. 新增或修改逻辑后，能否明确指出最上层待训练函数是谁？
2. 该函数是否真的是 `*_comb`，而不是 `comb_calc` / wrapper / mixed function？
3. 若内部还有小 `*_comb`，是否已明确它们属于 fallback 子级训练单元还是主统计对象？

### 13.2 纯组合要求

4. 所有待训练函数是否严格 `*_comb`、二参、开头默认清零？
5. `comb` 内是否存在持久状态读写、全局状态依赖、静态状态依赖？
6. `comb` 是否存在直接摸底层数组的隐式读表？

### 13.3 时序边界

7. `seq_read` 是否只读不写？
8. `seq_read` 的读地址 / bank / 使能是否都来自更早的 `*_comb` 或显式输入？
9. `seq_write` 是否纯提交、无策略推导？

### 13.4 训练 IO 与位宽

10. 输入输出结构体是否可稳定抽取？
11. 字段位宽是否能追溯到 `wireX_t` / `*_BITS`？
12. 窄位宽信号跨模块 / FIFO 时是否按 `*_BITS` 做 mask？

### 13.5 顶层路径

13. `front_top` 是否仍是唯一后端入口，且流程固定三段式？
14. 是否残留不必要的 `step/run/peek/get` 旁路入口？
15. 新增或改名的 `*_comb` 是否已同步进入训练清单和 IO 统计文档？

---

## 14. 最后强调：形式服从 BSD 训练目标

评审和重构时，始终记住以下判断顺序：

第一问：
- 这段代码是否让训练边界更清楚了？

第二问：
- 这段 `*_comb` 是否比之前更纯组合了？

第三问：
- 大表项是否被拆成”地址计算 / 读表 / 决策”三段了？

第四问：
- `seq_read` / `seq_write` 是否更透明、更标准化了？

只有在这几项都成立时，”三段式写法”才算真正达成目标。

本项目不追求教条式拆分；
本项目追求的是：

- 训练边界清晰
- 组合逻辑纯净
- 时序逻辑透明
- 顶层交付稳定

---

## 15. Falcon / 性能统计豁免声明

**Falcon** 是前端性能统计分析器。以下内容**不纳入 BSD 规范约束**：

- `FrontRuntimeStats` 结构体及其所有计数器字段
- `falcon_*` 系列变量（`falcon_window_active`、`falcon_warmup_cycles`、`falcon_recovery_pending`、`falcon_recovery_src` 等）
- `front_stats` 的所有更新逻辑
- 统计打印函数（`falcon_print_summary`、`front_stats_print_summary` 等）
- 死锁快照相关逻辑（`FrontDeadlockSnapshot`、`front_deadlock_snapshot`）
- `SIM_DEBUG_PRINT_ACTIVE`、`DEBUG_LOG_*` 相关调试打印

具体豁免规则：

1. 这些代码可以存在于 `comb_calc` 内部，不影响三段式合规性判定
2. 这些代码中的变量类型不受 `wireX_t` / 禁止 signed 等约束
3. 评审时应跳过统计/调试相关逻辑，聚焦功能主路径
4. 训练函数列表的”功能完全覆盖”口径中，统计/调试逻辑不算”功能”
