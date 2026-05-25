# Dcache

### 基本功能

```c++
uint32_t tag_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM] = {};
uint32_t data_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM][DCACHE_WORD_NUM] = {};
bool valid_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM] = {};
bool dirty_array[DCACHE_SETS_NUM][DCACHE_WAYS_NUM] = {};
bool plru_tree_state[DCACHE_SETS_NUM][DCACHE_PLRU_TREE_BITS] = {};
```

MemSubSystem中的DcacheConfig.cpp文件中的Dcache_Read,Dcache_Write函数

### 基本信号

`Dcache_Read` 和 `Dcache_Write` 是当前模板里直接访问 DCache 存储数组的边界：

```c++
Dcache_Read(dcache_line_read_req_,
            dcache_line_read_resp_,
            fill_out_,
            fill_in_);

Dcache_Write(pending_writes_,
             lru_updates_,
             fill_writes_);
```

#### 读信号

| 信号 | 来源/用途 | 读取的数组 | 输出到 |
| --- | --- | --- | --- |
| `out.dcachereadreq[i].set_idx` | 每个 load/store 端口在 stage1 根据请求地址 `decode(addr).set_idx` 发起 set 读取。`i < LSU_LDU_COUNT` 是 load 端口，`i >= LSU_LDU_COUNT` 是 store 端口。 | `valid_array[set_idx][way]`、`tag_array[set_idx][way]`、`dirty_array[set_idx][way]`、`data_array[set_idx][way][word]` | `in.dcachelinereadresp[i].valid[way]`、`tag[way]`、`dirty[way]`、`data[way][word]` |
| `out.fillout.valid/set_idx` | MSHR fill 到来时，stage1 用 fill 地址的 `set_idx` 请求被替换 set 的快照，供 stage2 选 victim 和判断是否需要写回 dirty line。 | `valid_array[set_idx][way]`、`tag_array[set_idx][way]`、`dirty_array[set_idx][way]`、`data_array[set_idx][way][word]`、`plru_tree_state[set_idx][node]` | `in.fillin.valid_snap[way]`、`tag_snap[way]`、`dirty_snap[way]`、`data_snap[way][word]`、`plru_tree_state[node]` |

说明：

- `Dcache_Read` 不修改任何 DCache 数组，只把指定 `set_idx` 下所有 way 的 tag/data/valid/dirty 或 PLRU 快照拷贝出来。
- 当 `out.fillout.valid == false` 时，`Dcache_Read` 会把整个 `in.fillin` 清零，避免 stage2 使用上一拍的 fill 快照。
- `in.dcachelinereadresp[i].data[hit_way][word_off]` 是 load hit 返回数据的来源；store hit 只用 `valid/tag` 找到 `hit_way`，真正的数据写入在 `pendingwrite` 阶段完成。

#### 写信号

| 信号 | 触发场景 | 具体数组修改 |
| --- | --- | --- |
| `out.pendingwrite[i]` | store hit 后生成，字段包含 `set_idx/way_idx/word_off/data/strb`。 | 只修改命中的一个 word：`apply_strobe(data_array[set_idx][way_idx][word_off], data, strb)`；然后置脏：`dirty_array[set_idx][way_idx] = true`。不会修改 `tag_array`、`valid_array`、`plru_tree_state`。 |
| `out.lru_updates[i]` | load hit 或 store hit 后生成，字段包含 `set_idx/way`。 | 调用 `plru_touch_way(set_idx, way)`，只更新 `plru_tree_state[set_idx][node]`，表示该 way 刚被访问。不会修改 tag/data/valid/dirty。 |
| `out.fill_write` | MSHR fill line 被 stage2 接收并选好 victim way 后生成，字段包含 `set_idx/tag/way_idx/data[]/dirty`。 | 写入整条 cache line。若同一个 set 内已有 `valid_array[set_idx][w] && tag_array[set_idx][w] == tag`，实际写入 `w`；否则写入 `way_idx`。随后修改：`valid_array[set_idx][target_way] = true`、`tag_array[set_idx][target_way] = tag`、`data_array[set_idx][target_way][word] = data[word]`、`plru_tree_state[set_idx]` 被 touch；`dirty_array[set_idx][target_way]` 先清零，如果 `fill_write.dirty == true` 再置为 true。 |

#### 数组修改路径汇总

| 数组 | 修改来源 | 修改粒度 |
| --- | --- | --- |
| `tag_array[set][way]` | 仅 `out.fill_write` / `write_dcache_line` | 整行 fill 时写入 `fill_write.tag` |
| `data_array[set][way][word]` | `out.pendingwrite[i]`、`out.fill_write` | store hit 按 `strb` 改一个 word；fill 写入整条 line 的全部 word |
| `valid_array[set][way]` | 仅 `out.fill_write` / `write_dcache_line` | fill 后把目标 way 置为 valid |
| `dirty_array[set][way]` | `out.pendingwrite[i]`、`out.fill_write` | store hit 置脏；fill 默认清脏，若 fill 合并了 store 或请求本身带 dirty，则再置脏 |
| `plru_tree_state[set][node]` | `out.lru_updates[i]`、`out.fill_write` | hit 或 fill 后 touch 对应 way，更新 tree-PLRU 状态 |

