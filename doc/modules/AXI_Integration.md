# AXI 集成说明

## 当前生效的拓扑

当前仓库已经不再使用早期那种“只有 `Icache` 走 AXI，而其余访存路径仍停留在私有旧通路”的 split 设计。

在当前顶层 `SimCpu` 集成里：

- `DCache` 的读请求接到 `MASTER_DCACHE_R`
- `DCache` 的写回流量接到 `MASTER_DCACHE_W`
- 外设与 MMIO 流量接到 `MASTER_EXTRA_R/W`
- 当 `CONFIG_ICACHE_USE_AXI_MEM_PORT=1` 时，真实 `Icache` 使用 `MASTER_ICACHE`

这些 master 最终都连到同一套由 `SimCpu` 持有的顶层 AXI interconnect、router、DDR 模型和 MMIO 总线。

## 仍然存在但不是当前 SoC 活跃路径的部分

`MemSubsystem` 内部仍然保留了一套 internal AXI runtime 对象。这个路径还在代码里，主要用于本地集成与兼容，但顶层 SoC 流程会通过：

`mem_subsystem.set_internal_axi_runtime_active(false)`

把它关闭。

因此，当前 Linux/SoC 主线运行应理解为：

- 只有一套顶层 shared AXI fabric
- LLC 是否开启只影响这套 shared fabric 内部
- `MemSubsystem` 的 private/internal AXI runtime 不是活跃 SoC 路径

## LLC 开关语义

`CONFIG_AXI_LLC_ENABLE` 控制的是顶层 interconnect 是否启用 LLC。

- `CONFIG_AXI_LLC_ENABLE=1`
  请求走同一套 shared fabric，并在其中启用 LLC
- `CONFIG_AXI_LLC_ENABLE=0`
  请求仍然走同一套 shared fabric，但 LLC 被关闭，系统退化为只依赖一级 `I/D-cache`

这个开关不会把 `DCache/PTW/peripheral` 切换到另一套不同的 interconnect。

## ICache 路径语义

`CONFIG_ICACHE_USE_AXI_MEM_PORT` 控制真实 `Icache` miss 数据从哪里取回：

- `1`：使用顶层 AXI 的 `MASTER_ICACHE`
- `0`：使用 `ICacheTop` 中的固定延迟本地读适配路径

对 Linux 矩阵验证来说，更推荐让四象限 profile 保持 `CONFIG_ICACHE_USE_AXI_MEM_PORT=1`，这样真实 `Icache` 始终挂在同一套 SoC fabric 上，`llc0/llc1` 之间唯一的 cache 层级差异就是 `CONFIG_AXI_LLC_ENABLE`。

当 `CONFIG_BPU` 关闭时，前端运行在 Oracle 模式，真实 `Icache` 模型不会被 step。此时编译期仍可保留 `Icache AXI` 路径相关配置，以保持 build/拓扑一致性，但运行时不会真正走到这条路径。

## 初始化顺序

当前 shared fabric 的初始化顺序为：

1. `mem_subsystem.init()`
2. `axi_interconnect.init()`
3. `axi_router.init()`
4. `axi_ddr.init()`
5. `axi_mmio.init()`
6. `front.init()`

这样可以保证在前端开始发请求之前，shared AXI fabric 已经准备完成。
