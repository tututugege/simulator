# Front-end FIFO RTL Template Mapping

适用范围：`front-end/fifo/`

---

## 1. 总体结论

当前 front-end 共有 4 个 FIFO：

- `fetch_address_FIFO`
- `instruction_FIFO`
- `PTAB`
- `front2back_FIFO`

它们在代码中的真实行为高度一致，可抽象为同一类 FIFO template：

- 单时钟
- 单读口 / 单写口（1R1W）
- 同拍支持 read + write
- 支持 empty 时 write-through / bypass read
- 支持 flush / clear
- 读侧输出为队首 entry
- 顺序队列语义（push 到尾，pop 从头）

其中只有 `PTAB` 在代码里还保留了“额外 dummy push”路径，但该路径依赖 2-Ahead / mini-flush 语义；在当前永久关闭 `ENABLE_2AHEAD` 的项目配置下，这条语义路径应视为**死路径**，不应进入当前 RTL 主映射需求。

---

## 2. 各 FIFO 规格

| FIFO | Entry logical width | Depth | 当前有效端口语义 | 备注 |
|------|---------------------|-------|------------------|------|
| `fetch_address_FIFO` | 32 bits | 8 | 1R1W | full 判定采用 `size >= DEPTH-1` 预留 1 格 |
| `instruction_FIFO` | 486 bits | 8 | 1R1W | 存 1 个 fetch group + predecode 结果 |
| `PTAB` | 1141 bits | 8 | 当前主路径 1R1W | 代码保留 dummy second-push，但当前配置下失效 |
| `front2back_FIFO` | 1277 bits | 8 | 1R1W | 最终送后端的宽 entry |

---

## 3. 通用行为需求

### 3.1 基本接口

建议统一 template 至少具备：

- `reset / clear`
- `read_enable`
- `write_enable`
- `write_data`
- `read_valid`
- `read_data`
- `full`
- `empty`

### 3.2 同拍读写语义

四个 FIFO 都满足以下统一语义：

- 若队列非空，`read_enable=1` 时读出当前队首
- 若同拍也有写入，则队列执行“先保留读出结果，再更新 next state”
- 若队列为空但同拍 `write_enable=1 && read_enable=1`，则读口直接返回当拍写入数据（write-through）

因此 RTL template 必须支持：

- empty + read + write 的旁路返回
- 同拍 push/pop 的 size 正确抵消

### 3.3 clear / refetch 语义

四个 FIFO 都支持由 `reset` 或 `refetch` 触发清空：

- `fetch_address_FIFO`
- `instruction_FIFO`
- `PTAB`
- `front2back_FIFO`

因此 template 需要支持：

- 同步 clear（逻辑等效即可）
- clear 后 size=0
- clear 优先级高于普通保留状态

---

## 4. full / empty 判定差异

### 4.1 `fetch_address_FIFO`

- `empty = (next_size == 0)`
- `full = (next_size >= DEPTH - 1)`

这是**预留 1 格 headroom** 的设计，不是标准“写满到 DEPTH”。

### 4.2 `instruction_FIFO`

- `empty = (next_size == 0)`
- `full = (next_size == DEPTH)`

标准满判定。

### 4.3 `PTAB`

- `empty = (next_size == 0)`
- `full = (next_size >= DEPTH - 1)`

同样预留 1 格。

### 4.4 `front2back_FIFO`

- `empty = (next_size == 0)`
- `full = (next_size == DEPTH)`

标准满判定。

### 结论

FIFO template 需要参数化：

- `DEPTH`
- `FULL_THRESHOLD`

不应把 full 判定硬编码成统一一种。

---

## 5. 结构差异与是否需要特化

### 5.1 `fetch_address_FIFO`

最简单：

- entry = `fetch_addr_t`
- 无额外字段打包
- 最适合作为基础模板参考

### 5.2 `instruction_FIFO`

特点：

- entry 是 fetch group 打包
- 写入前 predecode 已经完成
- FIFO 本身不做 predecode 计算，只搬运 entry

因此对 template 来说仍只是宽 entry FIFO。

### 5.3 `PTAB`

特点：

- entry 很宽，包含 TAGE / SC / loop metadata
- 代码中存在 `push_write_en` + `push_dummy_en` 双 push 形式
- 但 `push_dummy_en` 的功能语义来自 2-Ahead mini-flush

在当前项目冻结口径下：

- RTL 主模板按 **单写入口 1R1W** 建模即可
- `dummy_entry` 可保留为 entry payload 中的普通 bit
- 不需要为当前主实现支持双写口

若未来重新启用 2-Ahead，再单独扩展为：

- 1R2W（或内部两次 push 仲裁）

### 5.4 `front2back_FIFO`

特点：

- entry 很宽
- 仅承接前级整包数据
- 没有额外特殊控制路径

因此直接适配宽 entry FIFO template 即可。

---

## 6. 推荐模板参数

建议统一 FIFO template 参数化为：

- `ENTRY_WIDTH`
- `DEPTH`
- `FULL_THRESHOLD`
- `ALLOW_EMPTY_READ_THROUGH`（当前四者都应为 true）
- `ENABLE_CLEAR`

可选：

- `REGISTERED_OUTPUT` / `COMB_OUTPUT`
- `SUPPORT_MULTI_PUSH`（当前主路径默认 false）

---

## 7. 当前映射建议

### 当前可直接归一到同一 template 的

- `fetch_address_FIFO`
- `instruction_FIFO`
- `front2back_FIFO`
- `PTAB`（按当前冻结配置，仅主路径 1R1W）

### 当前不建议纳入模板主需求的

- `PTAB` dummy second-push 语义
- 所有 2-Ahead / mini-flush 驱动出的额外 push 行为

理由：

- 这些路径在当前项目配置下是永久关闭 / 不启用路径
- 若为其扩 RTL 模板，会把当前主需求复杂化

---

## 8. 最终结论

在当前冻结配置下，front-end FIFO 的 RTL 映射需求可以统一概括为：

- **4 个单时钟、单读单写、支持 clear、支持空队列写透读出的参数化 FIFO**
- 主要参数差异只有：
  - `ENTRY_WIDTH`
  - `DEPTH`
  - `FULL_THRESHOLD`
- `PTAB` 的 dummy / 2-Ahead 特殊路径当前应按死路径处理，不纳入主模板需求
