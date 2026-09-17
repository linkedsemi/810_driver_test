# DW DMAC ERR 路径注入尝试（IOPMP 触发器）— 实测记录与结论

> 会话：2026-09-17（O-8「补测」）。目标：让 `dw_dma_isr()` 的 `status_err`
> 分支获得**实测**覆盖。结论：**未能打通**，且本轮给出了比上一轮更强的排除证据。
>
> 本文所有数据均为**串口实测**（ztest `INFO` 行），非推论。

## 1. 待检验的假设与通道

上一轮（O-8）已证：QSH 总线对**未解码地址**不返回错误响应（12 次注入全部静默成功），
DW DMAC 亦无软件置位 ERR 的寄存器。本轮改走**芯片自带的错误源 IOPMP**：

| IOPMP | 基址 | 守的总线主设备（`samples/iopmp_dt` overlay 注释） |
|---|---|---|
| iopmp1 | 0x40001000 | APP_CPUI / APP_CPUD |
| iopmp2 | 0x40001400 | APP_CPUS |
| **iopmp3** | **0x40001800** | **DMAC1 / ETH1 / EMMC1** |
| iopmp4 | 0x40001C00 | DMAC2 / ETH2 / EMMC2 |
| iopmp5 | 0x40002000 | USB2 / SHA512 / LTPI |

`SEC_IOPMP3_ADDR = 0x40001800`（`reg_base_addr.h`），DMAC1 基址 `APP_DWDMAC1_ADDR = 0x40041000`。
IOPMP3 在默认 lsqsh board **无 DT 节点**（`linkedsemi,iopmp` 只在 `iopmp_dt` 样例的
overlay 里），`CONFIG_IOPMP_LINKEDSEMI` 默认 n ⇒ 上电时 IOPMP3 处于**硬件复位态**，
实测 `MD_CFG = 0x0`（关闭）。

注入方法：直接写 IOPMP3 寄存器（不经 DT/soc 驱动），用 RISC-V PMP 风格条目
（`ADDRx` = NAPOT 编码，`CFG` 字节 = `A[4:3] | X | W | R`，8 条按 4 字节/寄存器
打包在 CFG0(slot0-3)/CFG1(slot4-7)）配一条**只拒绝 DMAC1 访问专用 scratch 页**的规则，
再跑 M2M 传输打该页。

> ⚠️ `samples/iopmp/src/main.c` **不能作为参考**：它调用的
> `iopmp_config_region_napot4()` 在整个仓库里**不存在**（`iopmp.h` 只有
> `iopmp_config_region_napot`），该样例是过时代码，未编译验证过。

## 2. 实测数据

配置均经**写-回读**确认生效，例如：
`md=0x3, cfg0=0x18, cfg1=0x1f000000, addr0=0x40201ff, addr7=0x1fffffff`。
`md` = IOPMP3 `MD_CFG`，`err_cb` = 收到 `DMA_STATUS_ERROR` 的回调次数，
`state` = 驱动 `chan_data->state`（0=IDLE,1=PREPARED,3=ACTIVE）。

### 2.1 IOPMP3 确实在 DMAC1 的通路上（拒绝生效）

| 实验 | 配置 | 结果 |
|---|---|---|
| E0 | md=0（关闭） | done=1 status=1 err_cb=0 state=0；目的缓冲区被写入 ✓ |
| E1a | slot0 拒绝 4KB 页 + slot7 全放行，md=3，**正常区**传输 | done=1 status=1 err_cb=0；`ok_dst[0]=0x5A` **写穿** ✓ |
| E1b | 同上，**目的 = 被拒页** | done=1 status=1 **err_cb=0**；`deny_page[0]=0xEE`（**未被写**）|
| E1c | 同上，**源 = 被拒页** | done=1 status=1 **err_cb=0** |

E1a vs E1b 对照证明：**拒绝规则有效**（被拒页的写确实没落地），但 DMA 依旧
报告**传输完成**（`status=1 = DMA_STATUS_BLOCK`），**没有 ERR 回调**。

### 2.2 拒绝整个 1MB SRAM（含数据缓冲 + 描述符池）：仍无 ERR

| md | 含义 | `ok_dst[0]` | err_cb |
|---|---|---|---|
| 0 | 关闭（对照） | 0x5A（写穿） | 0 |
| 1 | `MD_EN` | 0x5A（写穿） | 0 |
| 2 | `IOPMP_EN` | **0xCC（被拦）** | 0 |
| 3 | `MD_EN｜IOPMP_EN` | **0xCC（被拦）** | 0 |

两条结论：
1. **`IOPMP_EN`（bit1）单独即可使能过滤**；`MD_EN`（bit0）单独不使能滤波。
2. 即使**描述符池与全部数据都在被拒区内**，传输仍以 `status=1` 报"完成"，
   **err_cb 恒为 0** ⇒ 拦截是**静默丢弃**，不产生总线错误响应。

（`MD_CFG` bit4 在读回时为 1，是"曾发生违例"的粘滞状态位；写 `INTR_CLR(0x34)=1`
未能清除它，但不影响功能——写回 md=0 即恢复。）

### 2.3 候选②：非法传输配置（IOPMP 关闭）

| 实验 | 配置 | 结果 |
|---|---|---|
| E6a | 源地址 +1（非对齐） | done=1 err_cb=0 |
| E6b | 目的地址 +2（非对齐） | done=1 err_cb=0 |
| E6c | data_size=4, block_size=6（非整倍数） | done=1 err_cb=0 |
| E6d | data_size=8, burst=16 | **rc=-22（`dma_config` 软件拒绝）**，未触硬件 |
| E6e | 1KB 页内传输 | done=1 err_cb=0 |
| E6f | DMA **读**外设寄存器区（dmac2 0x40042000） | done=1 err_cb=0 |

全部无 ERR。E6d 是**参数校验阶段**被驱动拒绝，与硬件错误路径无关。

## 3. 结论

**本平台上 DW DMAC 的 ERR 中断路径无法用可用手段注入。** 具体地：

1. **IOPMP3 是"静默拦截器"，不是错误源**：被拒事务的响应不是总线 ERROR，
   而是被丢弃 + 正常响应（否则 DW DMAC 会置 `RawErr`/`StatusErr` 并中断）。
   实测 `DW_RAW_ERR = 0x0` 恒成立、`err_cb` 恒 0。
2. 未解码地址不报错（上一轮），IOPMP 拦截也不报错（本轮）⇒
   **该 SoC 互联对被拒/未解码访问一律回 OKAY**，DW DMAC 没有可感知的错误事件。
3. 非法传输配置要么被驱动在 `dma_config()` 阶段软件拒绝，要么硬件静默接受。

## 4. 要真正测得 ERR 路径，还需要什么

- **能返回 ERROR 响应（HRESP/BRESP error）的总线从设备**：例如把 DMA 指向一个
  会对非法访问主动回错误的外设，或由 FPGA/仿真模型在总线上注入错误响应；
- 或 **DW DMAC 侧的死信/超时错误源**：若该 IP 变体支持 descriptor fetch error
  检测，需让 LLP 指向会报错的地址（同样依赖上面的"会报错的从设备"）；
- 或 **芯片级错误注入手段**（如某些 SoC 的总线错误注入寄存器 / 调试域 force 位）——
  本仓库与 `reg_base_addr.h` 中未见此类寄存器；
- 或联系芯片工程师确认 IOPMP 是否存在"违例返回错误响应"的配置位（当前
  `MD_CFG` 只暴露 MD_EN/IOPMP_EN/INTR_REG 三位，未见此开关）。

在此之前，O-8 的 err 分支改动只能维持"**与 TFR 分支对称 + 状态机不变量**"的论证，
**不得**记为"已实测"。对应的加固项（`state = DW_DMA_IDLE`、显式清 `DW_DMA_CHAN_EN`）
依据 = 代理实测（TFR 完成后 ChEn 自清）+ 不变量论证（见 `OPEN_ITEMS.md` O-9）。
