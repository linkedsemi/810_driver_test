# spi_loopback 用例归属决定（暂不迁移）

> **2026-10-29 更新**：`spif_test` 的回环用例已按方案 A 落地并上板通过
> （`spif1` master + `spif3` filter → `flash3_0`，`loopback_filter_*` 四组合 /
> `loopback_jedec` / `loopback_read_addr`，`case_default` 24/24 PASS）。实现与证据见
> [`spif_loopback_result.md`](spif_loopback_result.md)。
> 下文保留 2026-10-27 的原始决定作为历史。

- **日期**：2026-10-27
- **状态**：**已删除并归档（2026-10-27）**；原 8 项不再运行，设计见附录。
- **相关工程**：`projects/spi_test`、`projects/spif_test`

## 决定

用户认为 `spi_test` 的 `spi_loopback`（尤其 `link_probe`）**放到 spif 上更合适**；
本轮**已删除回环用例**（2026-10-27），设计归档到本文件附录；`spi_test` 现为
**18 项 `spi` + 10 项 `spi_flash`**。

## 背景

- 2026-10-27 单独跑 `spi_test`（1os，RAM）：
  - 套件 `spi` 18 项全 PASS；
  - 套件 `spi_loopback` 的 `link_probe` FAIL：
    `master_ret=0, slave_ret=-119, timed_out=1, mrx0=ff, srx0=00`；
    其余 7 项按设计 SKIP。
- `spi_loopback` 需要 **SPI1↔SPI3 四线跳线**（SCK PA00↔PG14、MOSI PA01↔PG15、
  MISO PA02↔PG03、CS PA03↔PG13）；COVERAGE.md R-5 已注明 CS 一路未核实，
  缺它会直接导致 `link_probe` FAIL。
- `spif_test` 目前**没有回环用例**，只有 SPIF 主路径读板上 NOR
  （`master_read_jedec_id` / `master_read_with_addr`，无外部跳线）。

## 后续可选方案（未拍板）

| 方案 | 内容 | 代价 |
|---|---|---|
| A. 迁到 `spif_test` | 把 8 项 `spi_loopback` 移到 `spif_test`，基于 SPIF 主路径（spi3/spi4 + 板上 NOR）重写 | 不再覆盖 `spi_ls` 的**从设备**路径 |
| B. 在 `spif_test` 新增 | `spi_test` 现有 8 项保留；`spif_test` 另加一个 SPIF 主路径链路/读校验用例 | 两套用例并存，需维护两份 |
| C. 维持现状 | 继续用 `spi_test` + 跳线；`link_probe` 作为已知基线 | 依赖跳线在位 |

## 相关文档

- `projects/spi_test/tests/COVERAGE.md`「回环可行性结论」§6 R-1/R-5
- `OPEN_ITEMS.md` O-31（`spif_test` 的 `cmd_idx_accessors` 已知基线，同为"保持现状"裁定）
- 本次运行日志：`810_driver_test/wsl_environment/wsl_new/logs/spi_test_uart_COM24_20261027_1729.log`

---

## 附录：原 spi_test COVERAGE 回环设计（2026-10-27 归档）

## 回环可行性结论（P4.6 Step-0 验证，2026-09-17）

> 用户裁定原文："**SPI 和 SPIF 应该是已经跳线回环了，好像是 SPI1 和 SPI3**""**两路 SPI 相互接了**"。
> 本节记录**为什么采信**、**采信到什么程度**、以及**哪些部分仍未确认**。铁律："不要把'用户说跳了线'当既成事实"。

### 1. 引脚级事实（源码可查，已核实）

| 实例 | 驱动 | SCK | MOSI(DAT0) | MISO(DAT1) | 其它 | CS |
|---|---|---|---|---|---|---|
| `spi1` @0x4008a000 | `spi_ls.c` | PA00 | PA01 | PA02 | — | **GPIOA 3** |
| `spi2` @0x4008a400 | `spi_ls.c` | PJ12 | PJ13 | PJ14 | — | GPIOJ 15 |
| `spi3` @0x4006e000 | `spi_dw.c` | PG14 | PG15 | PG03 | PE01(DAT2) / PE00(DAT3) | GPIOG 13 / 12（板级默认） |
| `spi4` @0x4006e400 | `spi_dw.c` | PQ00 | PQ03 | PQ04 | PQ05 / PQ06 | GPIOQ 2 / 1 |

证据：`zephyr/dts/riscv/linkedsemi/lsqsh.dtsi:1179-1235`（节点）、`zephyr/boards/linkedsemi/lsqsh_evb/lsqsh_evb_pinctrl.dtsi:148-212`（pinmux 生产者）、`:381-415`（消费者 + cs-gpios）。
**`spi1` 与 `spi3` 的引脚组完全不相交（Port A ↔ Port G/E），不存在片内 pinmux 复用通路 ⇒ 任何连接都必须是物理跳线**，与用户"跳线"的说法一致。

### 2. 四线 + CS 交叉是否成立

成立。两侧各自具备完整的 **SCK / MOSI / MISO + 一个 GPIO 片选**，主从之间按标准 SPI 做**直连**（SCK↔SCK、MOSI↔MOSI、MISO↔MISO、CS↔CS）即为合法主从对连。
（注：只有"单器件自环"才需要 MOSI↔MISO 交叉；两器件主从对连是**直连**。）

### 3. 哪一侧能当 slave —— 唯一有证据支持的方向是 `spi3=master / spi1=slave`

| 判据 | `spi1`（LS-SPI） | `spi3`（DW SPI） |
|---|---|---|
| 从模式代码路径 | 有 —— `spi_ls_data_exchange_slave()`（`spi_ls.c:207-236`）、`MSTR=SLAVE` 写入（`:89-91`） | 有 —— `spi_dw.c` 的 `spi_dw_is_slave()`（`:233-237`）分支 |
| 硬件 NSS/片选**输入**复用宏 | **有**：`SPI1_NSS_FUNC1_IDX19_PINMUX_PA(XX)`（`lsqsh-pinctrl_func_1.h:29`）、`..._IDX20_PINMUX_PC/PI`（`:92,278`） | ~~没有~~ **有（C1 更正，2026-09-18）**：`SPI3_CSN0_FUNC2_PG13_PINMUX` / `SPI3_CSN1_FUNC2_PG12_PINMUX`（全树 grep `SPI3_` 即得）。<br>⚠️ 本节原写「spi3 没有任何 NSS 复用宏」是**错误结论**（当时 grep 范围/关键字漏了 `CSN`），据此推出的方向认定需降权 |
| HAL 侧从设备 NSS 初始化 | 声明存在（`soc/rv32/qsh/include/ls_soc_pinmux.h`） | 声明存在但**函数体被整段注释**（`soc/rv32/qsh/ls_soc_gpio.c` 的 `pinmux_spi3_slave_nss_init` 区） |
| 驱动侧从模式门槛 | 无额外门槛（`spi_ls.c:118-121` 的频率判据不区分主从） | **DT 属性 `serial-target` 必须为真**，否则 `-ENOTSUP`（`spi_dw.c:407-411`）；而该属性描述的是**硬件固定角色**——驱动从不写 master/slave 位（`spi_dw_init` 只做 reset/clock/pinctrl/IRQ） |
| 全树实际用法 | 从模式**从未被启用过**（上轮因无主控时钟跳过） | `serial-target` 在全树 `.dts/.dtsi/.overlay` 中**出现 0 次**（仅注释）；所有用例（`spif_test` P4.7、`pfr_tpm` 的 `spi_master_communication` / `spi_filter` / `spi_monitor_*`）都把 spi3/spi4 当 **master** 用 |

⇒ **结论：`spi3` 作从设备既没有 NSS 输入通路、也没有任何在用证据；`spi1` 作从设备有驱动路径 + SoC 的 NSS 复用宏支持。** 故本工程采用 `spi3 = master / spi1 = slave`。

### 4. 电气安全约束（已在实现中落实）

1. **CS 只由主机一侧驱动**（C1 修复，2026-09-18；原「两侧都不接」已作废）。
   主机 `spi3`：`lb_cfg_m.cs.gpio` = 板级 `&spi3 cs-gpios` 第 0 项（**GPIOG 13** = `SPI3_CSN0` 焊盘）⇒ `spi_dw.c:793-794` 传输前拉低 / `:1026-1030` 传输后放开。
   从机 `spi1`：`cs.gpio.port == NULL` ⇒ `spi_cs_is_gpio()` 为假 ⇒ `spi_ls.c:330-332` **不会**驱动 CS GPIO；片选改由**硬件 NSS 输入脚 PA03** 承担（overlay 的 `spi1_nss_pa03`，`input-enable` + `bias-pull-up`）。
   依据（vendor 自己的从设备例程，不是推测）：`modules/hal/linkedsemi/examples/peripheral/spi_i2s/spi_it_slave/main.c:71-79` **逐个 pin 显式初始化**且**必须含 NSS**（`pinmux_spi2_slave_nss_init(SPI_CS_PIN)`），其 `spi_slave_nss_io_cfg()` = 上拉 + **输入** ⇒ 从机不推 NSS，与主机 CS 无对推。
   ⚠️ 原「spi3 根本没有 NSS 复用宏」是错误结论（见上 §3 更正）：`SPI3_CSN0_FUNC2_PG13` / `SPI3_CSN1_FUNC2_PG12` 均存在，且与板级 `cs-gpios` 逐脚对应。
2. **主机现在会驱动 PG13**（原「不驱动任何 CS」已作废）⇒ 挂在 spi3 net 上的板载 NOR（若存在，板级默认片选 gpiog12/13）在主机传输期间**会跟随选中**。风险由「跳线只连 spi1↔spi3、且 spi3 侧无 DT 声明的 flash 子节点」兜底；若实机 MISO 恒为固定值，第一动作是核对 spi3 net 上是否真挂了 NOR（见 §6 R-2）。
3. **从模式无超时**：`spi_context.h:153-154` 在 `CONFIG_SPI_SLAVE` 下把从设备等待设为 `K_FOREVER`。故从设备一律跑在独立线程、主线程用**有界 `k_thread_join()`**（500 ms）+ `k_thread_abort()` + `spi_release()` 兜底；链路一次性探测，不通则后续用例 `ztest_test_skip()`。
4. overlay 给 `spi3` 加 `timing-calibration-disabled`，规避 `spi_dw.c:467-469` 对空数组 `timing_calibration_delay_arr[]` 的越界读；并使能 `&dmac2` 以满足 `spi3` 自带 `dmas` 属性带来的 `DEVICE_DT_GET` / `CONFIG_DMA_DW_LLI_POOL_SIZE` 编入依赖（同 `spif_test` 处理）。

### 5. 与 `spif_test`（P4.7）的关键差异

`spif_test` 把 `&spi3 { pinctrl-0 = <>; }` **清空**，把 spi3 的 net 让给 SPIF 的 master 态引脚；**本工程不启用 SPIF**，故保留 spi3 自己的板级 pinctrl（PG14/PG15/PG03/PE01/PE00），由 spi3 自己驱动 SCK/MOSI。

### 6. 未决项（★ 需用户确认）

| # | 未决 | 影响 |
|---|---|---|
| R-1 | **跳线是否真的存在**，以及**逐 pin 对应**（是否含 CS 线） | 决定本套件是 PASS 还是 FAIL/SKIP；用户原话为"应该是""好像是"，属推测语气，DT/电路图均无法证实 |
| R-2 | `spi3` 的 net 上是否挂有板载 NOR（vendor 的 `spi_master_communication` overlay 在 `&spi3` 下声明过 `flash3_0/flash3_1`，但 alias 里被注释掉） | 若有且其 CS 被拉低，会与从设备争 MISO。**C1 后主机已会驱动 PG13**，故该风险从"默认不触发"变为"值得实测核对" |
| R-3 | ~~`spi1` 作从设备是否需要硬件 NSS~~ **已定论：需要**（vendor 从设备例程 `spi_it_slave/main.c:71-79` 必须配 NSS）。本工程已在 overlay 补 `spi1_nss_pa03`（复用宏 `SPI1_NSS_FUNC1_IDX19`，`input-enable` + `bias-pull-up`） | 已解决（引脚号 PA03 仍是推断，见 R-4） |
| R-4 | **`SPI1_NSS` 的物理焊盘是否为 PA03**：`SPI1_NSS_FUNC1_IDX19_PINMUX_PA(XX)` 的 `XX` 是模板参数（宏不校验脚号）。选 PA03 的依据是「板级 `&spi1 cs-gpios = <&gpioa 3>`」+「同板 `&spi3 cs-gpios = gpiog13/12` 与 `SPI3_CSN0/CSN1` 逐脚对应」⇒ cs-gpios 指向该实例的片选焊盘 | 若 PA03 不是 idx19 焊盘：NSS 恒为高（上拉）⇒ 从机永不被选中 ⇒ `roundtrip_*` 全部超时。实测判据：主机 CS 拉低时量 PA03 电平（应被主机拉低） |
| R-5 | **物理跳线必须含 CS 一路：`spi3 PG13 ↔ spi1 PA03`**（C1 后主机 GP 片选从 PG13 出、从机 NSS 从 PA03 入）。上一轮记录只有"四线直连"的口述，**逐 pin 对应从未核实**，且此前的 `spi` 套件会把 PA03 当 GPIO 片选驱动 —— 若跳线早已连上 PA03↔PG13，两轮之间就存在过对推 | 缺这一路 ⇒ `link_probe` 仍 FAIL（`slave_ret` 会从 0 变为 -115 有界超时）。**不要假设它已连着** |

---

## 回环用例（套件 `spi_loopback`，8 项，2026-09-17 新增）

- **外部条件：`外部: SPI1↔SPI3 跳线`** —— 四线直连（SCK↔SCK / MOSI↔MOSI / MISO↔MISO / CS↔CS），**C1 后逐 pin 对应明确为**：
  | 信号 | spi1（slave, `spi_ls.c`） | spi3（master, `spi_dw.c`） |
  |---|---|---|
  | SCK  | PA00 | PG14 |
  | MOSI | PA01 | PG15 |
  | MISO | PA02 | PG03 |
  | **CS** | **PA03**（SPI1_NSS 复用，overlay 新增） | **PG13**（板级 `cs-gpios[0]`，`SPI3_CSN0` 焊盘） |
  ⇒ **CS 一路（PA03↔PG13）是 C1 新增的必要条件，尚未经核实**（见 §6 R-5）。
- 角色：`spi3`（`spi_dw.c`）= **master**；`spi1`（`spi_ls.c`）= **slave**（依据见上节 §3）。
- **判据形态**：**逐字节比对** —— `master_rx[] == slave_tx[]` 且 `slave_rx[] == master_tx[]`；另加驱动层判据 `slave_ret == len`（`spi_ls.c:362-366` 在 `CONFIG_SPI_SLAVE` 下返回 `recv_frames`）。
- 全部用例以 `lb_link_ok()` 为前置：链路一次性探测不通 ⇒ 除 `link_probe` 外**一律 `ztest_test_skip()`**（不产生级联 FAIL，也不会有用例永久挂死）。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| link_probe | `spi_dw_init()`（`spi_dw.c:2024-2090`）+ `spi_ls_init()`（`spi_ls.c:415-472`）；`spi_dw_configure()` 的 master 分支（`:407-411`）与 `spi_ls_configure()` 的 `MSTR=SLAVE`（`:89-91`） | 功能通路 | 两设备 `device_is_ready` 真；8 B 双向逐字节相等；`master_ret==0`；`slave_ret==8` | DT 契约 + **外部跳线** | ⏳ |
| roundtrip_length_sweep | 主 `spi_dw.c` ISR `push_data`/`pull_data` 状态机（`:277/:338`）；从 `spi_ls_data_exchange_slave()`（`:207-236`）+ `spi_ls_isr()`（`:277-299`）+ `spi_ls_complete()`（`:251-275`） | 边界长度 | len ∈ {1,2,3,4,5,8,16,32,63,64,127,128,255,256} 每档双向逐字节相等、`slave_ret==len` | 驱动源码 + 外部跳线 | ⏳ |
| roundtrip_fifo_boundary | 主 `spi_dw.c` 的 `fifo_depth`(板级 =4) / `max_xfer_size`(=32) 相关分支（`:285-291`、`:364`、`:427-434`）；从 `spi_ls.c` 的 `CR2.TXFTH=4`（`:138`） | 边界长度 | len ∈ {3,4,5,31,32,33} 双向逐字节相等、`slave_ret==len` | 驱动源码 + DT 属性 | ⏳ |
| roundtrip_pattern_matrix | 主 `spi_dw.c` 8 bit 收发通路；从 `spi_ls.c` 的 `spi_context_update_tx/rx`（`:215/:229`） | 功能通路 | 6 组 seed（含 0x00/0xFF/0xAA/0x55），16 B 双向逐字节相等 | 驱动源码 | ⏳ |
| roundtrip_max_length_256 | 同上，长度取单次上限 256 B（主侧 FIFO 反复灌/抽，从侧 ISR 逐字节搬运） | 边界长度 | 256 B 双向逐字节相等；`slave_ret==256` | 驱动源码 + 外部跳线 | ⏳ |
| roundtrip_master_tx_slave_rx | 主 `spi_dw.c` 在 `rx_bufs==NULL` 时的 IMR 选择（`:784-788`）；从 `spi_ls_data_exchange_slave()` 的 `spi_context_rx_buf_on()` 分支（`:223-235`，tx 关闭时只收不发） | 功能通路 | 24 B：`slave_rx[] == master_tx[]`；`master_ret==0`；`slave_ret==24` | 驱动源码 | ⏳ |
| slave_ret_recv_frames | `spi_ls_transceive()` `:362-366`（`CONFIG_SPI_SLAVE` 下从模式返回 `ctx->recv_frames`）；来源 `spi_context.h:174-178` | 功能通路（**上轮跳过项解禁**） | len ∈ {1,7,64} 时 `slave_ret` 分别 == 1/7/64 | 驱动源码 | ⏳ |
| repeat_roundtrips_state_isolation | `spi_context_lock/release`（`spi_context.h:89-130`）的轮间取放锁；`spi_ls_complete()` 的 SPE 复位（`spi_ls.c:269-272`） | 并发/状态隔离 | 8 轮连续回环全部双向逐字节相等（长度在 {1,2,3,8,17,64} 间循环） | 驱动 + spi_context 契约 | ⏳ |

> 状态标记：`⏳` = **P4.6 新增，仅编译通过（`west build` rc=0），待首次实测**。

## 跳过项

| 跳过的用例/分支 | 判据类型 | 原因 |
|---|---|---|
| 数据回环/回显类（把 TX 数据经从设备或 MOSI→MISO 短接读回并逐字节比对） | ~~缺硬件条件~~ **P4.6 已解禁（有条件下）** | 上轮缺对端；本轮依用户口述 `外部: SPI1↔SPI3 跳线` 补入套件 `spi_loopback`（多长度/多模式逐字节比对）。**前提是该跳线确实存在**，见「回环可行性结论 §6 未决项 R-1」 |
| `jedec,spi-nor` 器件层（JEDEC ID / 读写擦） | **缺硬件条件** | 唯一声称 `spi2` 挂 NOR 的 vendor sample（`spi_flash_communication`）证据不足，见上「结论 §3」四条反证。**不因本轮跳线而改变**：本工程不打 NOR，且刻意不驱动任何 CS（见「回环可行性结论 §4.2」），避免选中 spi3 net 上可能存在的板载 flash |
| `spi_ls_configure()` 的**从设备模式**分支（`MSTR=0`，`spi_ls_data_exchange_slave()` `:207-236`） | ~~缺硬件条件 + 危险不可控~~ **P4.6 已解禁（有条件下）** | 上轮无主控时钟 ⇒ `spi_context_wait_for_completion()` 走 `K_FOREVER`（`spi_context.h:153-154`）⇒ 永久挂死。本轮由 spi3(DW) 提供时钟；`K_FOREVER` 风险由「从设备独立线程 + 500 ms 有界 join + abort/release 兜底 + 链路一次性探测」消解（`src/spi_loopback.c` 文件头 §安全性设计） |
| `spi_ls_configure()` 分频搜索的**下限越界窗口**（`frequency ∈ [146484, 292968)`） | **缺陷规避（不可断言）** | 见「上游缺陷观察」#3：会越界读 `scaler[6]` 并写入不可预测的 BR 值，无确定预期值。故分频表用例的下界取 `292968`（`clock>>8`） |
| `spi_ls_complete()` 的 `SR.BSY==1` 分支（`spi_ls_release()` 的 `-EBUSY` 路径 `:384-386`） | **不可确定性注入** | `BSY` 是传输中的瞬态；同步 API（`spi_transceive`）返回时 `spi_ls_complete()` 已自旋等到 `BSY==0`，外部无法在同步语义下把 `BSY` 置 1 |
| `spi_ls_transceive()` 的**首次调用即配置失败**路径（`ctx->config==NULL` 时的 `spi_context_release()`） | **缺陷规避（不可断言）** | 见「上游缺陷观察」#1：会解引用 NULL → load access fault → 固件挂死、整轮套件中断。故错误路径用例一律先 `spi_arm_instance()` |
| `spi1`/`spi2` 与 `spif1..4` / `tpm_spis1-2` 的**跨 IP 连通** | **缺硬件条件** | `spif` 的配对 spi 是 `spi3`/`spi4`（`lsqsh_pinctrl_pfr_tpm.dtsi:961,1012,1063,1114`），与 `spi1`/`spi2` 无片内通路；`tpm_spis1` 在 PI04-PI07，与 `spi1` 的 PA00-PA02 **无同管脚复用**（P4 `spid_test` 已登记需外部跳线） |
| `spi_ls_isr()` 的错误中断分支（`MODFFM`/`OVRFM`/`FREFM` → `-EIO`，`spi_ls_get_err()` `:146-166`） | **缺硬件条件（引脚不可达，已定论）** | P4.6 重新核查后的精确原因：<br>① **MODF**（主模式 NSS 被外部拉低）需要主/从**任一**侧有可驱动的 NSS 线 —— `spi3` **无任何 NSS 复用宏**（HAL 的 `pinmux_spi3_slave_nss_init` 函数体被注释），`spi1` 虽有 `SPI1_NSS_FUNC1_IDX19` 复用宏但**板级 pinctrl 未布线**，且本工程 CS 一律不接（§4.1）；<br>② **OVR**（FIFO 溢出）需要"ISR 未及时读取 RXNE"——`spi_ls_isr()` 收到 RXNE 即读，应用层无法注入；<br>③ **FR**（帧格式错）由 TI 模式/NSS 脉冲引起，而驱动 `:77-80` 直接拒绝 TI 模式。<br>⇒ 三者在本硬件配置下均无可确定性注入路径 |
| **双 master 线程并发**对同一从设备（真并发，非状态隔离） | **不适用（本回环只有一路从设备）** | 跳线只连了 spi1 一路从设备，第二个 master 线程并发时没有对端可服务，构造出来测的是"没有从设备的传输"，无判据价值。真正的并发/状态隔离维度由 `repeat_roundtrips_state_isolation` 覆盖（轮间锁与缓冲不串扰） |
| LDMA / DMA 通路 | **不适用** | `linkedsemi,ls-spi` 绑定（`zephyr/dts/bindings/spi/linkedsemi,ls-spi.yaml`）**无 `dmas` 属性**，`spi_ls.c` 全文无 DMA 代码 |


## 附录：原 P4.6 生成说明 / C1 修复说明（2026-10-27 归档）

## P4.6 生成说明（2026-09-17，只生成 + 编译通过，未烧录）

- 新增 **8 项**，套件 `spi_loopback`（与原有 `spi` 套件分离，**原有 18 项零改动**）。
- 编译通过：`west build -p auto -d <build/target> -b lsqsh_evb@1os/lsqsh/cpu1` rc=0。
- 链接序核对（`riscv64-unknown-elf-nm --numeric-sort zephyr.elf | grep _ztest_unit_test__`）：
  `spi` 套件 18 项在前（索引 0..17），`spi_loopback` 8 项在后（索引 18..25，字母序）；
  `dump_results.py` 的 `TEST_NAMES` 已按此序扩到 26 项。
  **`spi` 套件先于 `spi_loopback` 执行** ⇒ 即使回环从设备线程卡死，也不会污染原有 18 项。
- 每用例自带前置，不依赖其它用例的副作用或执行顺序：全部以 `lb_link_ok()` 起手，
  链路不通则 `ztest_test_skip()`；链路探测结果缓存，最多只吃一次 500 ms 超时。
- 新增/修改文件：`src/spi_loopback.c`（新）、`app.overlay`（+`&spi3`（含
  `timing-calibration-disabled`）、+`&dmac2`）、`prj.conf`（+`CONFIG_SPI_DW=y` /
  `CONFIG_SPI_SLAVE=y`）、`CMakeLists.txt`（+`src/spi_loopback.c`）、
  `tests/scripts/dump_results.py`（TEST_NAMES 18→26）。

## C1 修复说明（2026-09-18，只改码 + 编译通过，未烧录）

> 触发：上一轮回归 `spi_test_case_default` 中 `link_probe` FAIL，
> `master_ret = -116 (-ETIMEDOUT)` / `slave_ret = 0` / `state = 0`，
> 其余 7 项回环用例被 `link_probe` 正确判 SKIP（保护机制生效，无级联 FAIL）。

### 1. 真因（**不在跳线/片选**，在用例侧的配置对象）

`src/spi_loopback.c` 上一轮声明的是 `static struct spi_config lb_cfg_m;` / `lb_cfg_s;`
（**全零**），而文件里为它们准备的 `LB_FREQ_HZ` / `LB_OP_M` / `LB_OP_S` 三个宏
**从未被使用**。于是：

| 现象 | 机理 |
|---|---|
| `master_ret = -116` | `operation = 0` ⇒ `SPI_WORD_SIZE_GET(op) == 0` ⇒ `spi_dw.c:435/437` 把 `CTRLR0.DFS` 写成 0（DW SSI 合法帧宽要求 DFS≥3）⇒ 帧宽非法、发送状态机不推进 ⇒ 主机等满自己的完成超时 |
| `slave_ret = 0` | `operation` 里**没有** `SPI_OP_MODE_SLAVE` 位 ⇒ `spi_context_is_slave()`（`spi_context.h:84-87`）为假 ⇒ `spi_ls_configure()` 走 `spi_ls.c:89-91` 的 `else` 分支写 `MSTR=1` ⇒ spi1 实际按**主模式**跑：自己发时钟、自己收完 8 字节（MISO 悬空读 0xFF）⇒ 正常返回 0 |

⇒ 「从设备从未被选中」的推论（线索 1/2）方向对但**不是本轮观测现象的原因**；
CS/NSS 是修完配置之后必须补齐的**第二个**条件。

### 2. 配置修好后才会暴露的第二个阻塞：驱动 P0（见「上游缺陷观察」#12）

`spi_ls.c:359-360` 同步路径重复 complete ⇒ spi_ls 实例每完成一次传输就在
`ctx->sync` 残留 1 个令牌 ⇒ 第 2 次起 `spi_transceive()` 立刻返回不等待。
主设备侧因 ISR 饿死线程而表象正常；**从设备侧会在返回后立刻 `SPE=0`**，永远等不到时钟。
本工程不改驱动，故在 `spi_loopback.c` 增加 `lb_drain_stray_sync()`：发起从设备传输前
清掉该残留令牌；带运行时自检（镜像 `struct spi_context` 前缀 + 校验
`config/cs_gpios/num_cs_gpios/lock.count/sync.limit`），自检不过就**不动**驱动状态。
**驱动若被修，该函数与镜像结构体应整体删除**（清一个本就是 0 的信号量，无副作用）。

### 3. CS/NSS 补齐（第二个条件）

- 主机 `spi3`：`lb_cfg_m.cs.gpio` = 板级 `&spi3 cs-gpios` 第 0 项 = **GPIOG 13**
  （`SPI3_CSN0` 焊盘）；`spi_dw.c:793-794 / :1026-1030` 负责拉/放。
- 从机 `spi1`：overlay 新增 pinctrl 节点 `spi1_nss_pa03`
  （`SPI1_NSS_FUNC1_IDX19_PINMUX_PA(03)` + `input-enable` + `bias-pull-up`），
  并整体重写 `&spi1` 的 `pinctrl-0` 把它带上（板级原本没有 NSS 节点）。
- 从机依旧**不驱动** CS GPIO（`cs.gpio.port == NULL`），只让主机一侧推。

### 4. 未决（★ 见 §6 R-4/R-5）

`PA03` 是否就是 SPI1_NSS 焊盘（推断）；以及**物理跳线必须含 CS 一路
`spi3 PG13 ↔ spi1 PA03`** —— 逐 pin 对应从未核实，**不得假设它已连着**。

### 5. 编译与链接序（本次实测）

- `west build -p auto -d <build/target> -b lsqsh_evb@1os/lsqsh/cpu1
  -DDTC_OVERLAY_FILE=<abs app.overlay>`，改 overlay 后已删 build 全量重建，rc=0，无 warning。
- DTS 核对：`spi1_nss_pa03.pinmux = <0xcc03>` → 解出 pin=3 / alt=19 / func=1 / valid=1 ✓；
  `&spi1 pinctrl-0 = <&spi1_nss_pa03 &spi1_clk_pa00 &spi1_miso_pa02 &spi1_mosi_pa01>` ✓。
- 配置对象核对（`.data` 实际字节）：`lb_cfg_m` = {freq 1e6, op 0x0100(MASTER|8bit|MSB), slave 0,
  cs={port≠NULL, pin=13, GPIO_ACTIVE_LOW}}；`lb_cfg_s` = {freq 1e6, op 0x0101(SLAVE|8bit|MSB), cs 全零} ✓。
- 链接序（`nm --numeric-sort | grep _ztest_unit_test__`）**未变**：`spi` 18 项（0..17）+
  `spi_loopback` 8 项（18..25）⇒ `dump_results.py` 的 `TEST_NAMES` 无需改动。
- 本次**未烧录**（硬件被其它任务占用），实机验证待后续。

---

