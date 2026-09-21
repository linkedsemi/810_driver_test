# SPI 测试覆盖矩阵

> **两个部分**：
> ① **原有 18 项**（套件 `spi`，`spi_ls.c` 驱动内部路径，`外部: 无`）；
> ② **P4.6 新增 8 项**（套件 `spi_loopback`，`外部: SPI1↔SPI3 跳线` 主从回环）
> —— 见「回环用例」与「回环可行性结论」两节。**原有 18 项零改动。**

- 驱动: `zephyr/drivers/spi/spi_ls.c`（compatible `linkedsemi,ls-spi`）
  ＋（P4.6 起）`zephyr/drivers/spi/spi_dw.c`（`snps,designware-spi`，仅作回环主设备）
- 接口: `zephyr/drivers/spi.h`（`spi_transceive` / `spi_release`）；底层上下文 `zephyr/drivers/spi/spi_context.h`
- 寄存器: `modules/hal/linkedsemi/hal_driver/inc/reg_spi_type.h`（`reg_spi_t` / 字段掩码）；HAL 编码常量 `hal_driver/api/ls_hal_spi_i2s.h`
- 外部路径（**分区**）：
  - **套件 `spi`（原有 18 项）：`外部: 无`** —— `spi1`/`spi2` 的 SCK/MOSI/MISO + CS 全部由**板级 pinctrl** 指定（`zephyr/boards/linkedsemi/lsqsh_evb/lsqsh_evb_pinctrl.dtsi:381-391`），片内引脚组；总线上**没有任何 DT 声明的从设备** ⇒ 判据全部来自驱动源码分支与寄存器语义（依据见下「实例范围与 Step-0 可行性结论」）。
  - **套件 `spi_loopback`（P4.6 新增 8 项）：`外部: SPI1↔SPI3 跳线`** —— spi1(LS-SPI,从) 与 spi3(DW SPI,主) 之间的四线直连（SCK↔SCK / MOSI↔MOSI / MISO↔MISO / CS↔CS）。**该跳线的存在与逐 pin 对应由用户口述，未经本项目实测确认**，见「回环可行性结论 §6 未决项」。

## 实例范围与 Step-0 可行性结论（P4.5，2026-09-17）

### 1. `spi_ls.c` 只绑定 `spi1` / `spi2`

| 实例 | 地址 | compatible | 归属驱动 |
|---|---|---|---|
| `spi1` | `0x4008a000` | `linkedsemi,ls-spi` | **`spi_ls.c`（本工程）** |
| `spi2` | `0x4008a400` | `linkedsemi,ls-spi` | **`spi_ls.c`（本工程）** |
| `spi3` | `0x4006e000` | `snps,designware-spi` | `spi_dw.c`（**不属本工程**） |
| `spi4` | `0x4006e400` | `snps,designware-spi` | `spi_dw.c`（**不属本工程**） |

证据：`zephyr/dts/riscv/linkedsemi/lsqsh.dtsi:1179-1234`；`DT_DRV_COMPAT = linkedsemi_ls_spi`（`spi_ls.c:7`）。
⇒ 任务描述里「裸 `spi_ls.c`（`spi1~4`）」中的 `spi3`/`spi4` **是笔误**：那两个实例走 `spi_dw.c`。

### 2. `spi_master_communication` 走的不是 `spi_ls.c`

该 sample 的器件链是 `spif3 → spi4`：

- `spif3.spi = <&spi4>`（`modules/lib/pfr_tpm/include/lsqsh_pinctrl_pfr_tpm.dtsi:1063`），
  sample 里 `spif_switch_to_master(spif3)` 之后用 `spif_spi_dev()` 取到的就是 **`spi4`（DW SPI）**；
- `flash4_0` 子节点**不在模块 dtsi 里**，是 sample 自己的板级 overlay 加的
  （`modules/lib/pfr_tpm/samples/spi_master_communication/boards/lsqsh_evb_cpu1.overlay:80-93`）；
- sample 把 `&spi4 { pinctrl-0 = <>; }` 显式清空 ⇒ 该通路依赖 **spif 的 "master" pinctrl**
  （CS=PI00 / CLK=PF09 / IO0-3=PF05,PF03,PF11,PF07），而不是 `spi4` 自己的板级 pinmux（PQ00/PQ03-06）。
- 器件确实存在：sample `README.rst` 记录的运行日志为 `jedec-id = [85 23 19]`、`flash4@0: 32 MiBy flash`、
  `FINAL BULK CHECK PASSED (32896 bytes)`。

⇒ **该通路属 `spif_test`（P4 已完成 15 项，含 `master_read_jedec_id`），与本工程无重叠。**

### 3. `spi1` / `spi2` 上没有可确定性到达的从设备

- `lsqsh.dtsi:1179-1205`：两节点 `status = "disabled"`，节点内**无 `cs-gpios`、无子设备**。
- 板级 dts 里 `&spi1..&spi4` 的 flash 子节点**全部被注释掉**（`lsqsh_evb_cpu1.dts:426-461`）。
- **全树**（`zephyr/`、`modules/lib/pfr_tpm/`、`linkedsemi_zephyr_project/`）搜 `&spi1` / `&spi2`：
  除本工程与 P4 的 `spid_test`（把 `spi1` 当 SPI master 用、需**外部跳线**到 `tpm_spis1`）外，
  只有 `linkedsemi_zephyr_project/samples/spi_flash_communication/boards/lsqsh_evb_cpu1.overlay`
  在 `&spi2` 下声明了 `jedec,spi-nor`。该 sample **不足以作为本板有器件的证据**：
  - 同目录 `README.rst` 首行即 `NOTICE: The SPI and external flash pins need to be consistent with the hardware`；
  - README 记录的实测输出标注为「if board is **qsh610**」，而全树不存在 `qsh610` 这个板名；
  - 其记录的 JEDEC `85 63 19` 与本板已知两处 NOR（`qspi2` 与 `spif3→spi4` 通路，均为 `85 23 19`）**不符**；
  - 该 overlay 与 `ls101x_evb.overlay` 是"按驱动类型对齐"的移植（ls-spi↔ls-spi、DW↔DW），
    属把 sample 跑通用的适配，不构成硬件事实。
- 板级 pinctrl 确实为两实例分配了完整引脚组 + 专用 CS GPIO
  （`spi1`: PA00/PA01/PA02 + `gpioa 3`；`spi2`: PJ12/PJ13/PJ14 + `gpioj 15`），
  但**引脚被分配 ≠ 器件在板**；本阶段（只生成不烧录）无法在硬件上确证。

### 4. 结论：**没有可确定性到达的从设备，但驱动内部路径可确定判定 ⇒ 工程可建（有条件）**

- **不建**任何依赖从设备的用例（数据回环/回显/JEDEC 读取）—— 见「跳过项」，且**不硬造 loopback 假设**（用户裁定 2026-09-17）。
- **可判定性来源**：SPI master 的 `spi_transceive()` 完成性只取决于自身 TX/RX FIFO 与中断状态机的推进，
  与总线上是否有从设备应答无关；且 `spi_context_wait_for_completion()` 在 **master 模式带超时**
  （`spi_context.h:146-170`：`max(tx,rx)*8*1000/frequency + CONFIG_SPI_COMPLETION_TIMEOUT_TOLERANCE(默认 200ms)`），
  超时返回 `-ETIMEDOUT` 而非永久阻塞 ⇒ "传输完成 / 未完成"是本工程最强的可判定事实。
- 因此全部 18 项的预期值来自：**驱动源码分支**（错误码、寄存器写入集合）+ **寄存器位域**（`reg_spi_type.h`）+
  **HAL 编码常量**（`ls_hal_spi_i2s.h`），不依赖任何外部器件。

### 5. 关键常量

| 常量 | 值 | 来源 |
|---|---|---|
| `PBUS4_CLOCK`（`spi1`/`spi2` 的 `clock-frequency`） | 75 MHz | `lsqsh.dtsi:15,1184,1196` |
| 频率上限 | `frequency <= clock/8 = 9375000` | `spi_ls.c:118-121` |
| 分频搜索最低安全点 | `clock>>8 = 292968` | `spi_ls.c:123-129`（再低即越界，见缺陷 3） |
| 分频档编码 | `/8,/16,/32,/64,/128,/256 → BR = 2..7` | `ls_hal_spi_i2s.h:182-187`、`spi_ls.c:55-62` |
| 字宽 | `DS = 0x7`(8bit) / `0xF`(16bit) | `ls_hal_spi_i2s.h:155-156` |
| `CR2.TXFTH` | 4 | `spi_ls.c:138` |
| CS | `spi1`→GPIOA 3；`spi2`→GPIOJ 15 | `lsqsh_evb_pinctrl.dtsi:382,388` |

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| device_ready | `LS_SPI_INIT`（`spi_ls.c:474-503`）→ `DEVICE_DT_INST_DEFINE`；`spi_ls_init()`（`:415-472`）→ `spi_context_cs_configure_all()`（`:462`，CS GPIO 端口不就绪 → `-ENODEV`） | 功能通路 | 两实例 `device_is_ready` 真；reg 基址 == `0x4008a000`/`0x4008a400`；`cs-gpios` 长度各 1；两个 CS GPIO 端口 ready | DT 绑定契约 + 驱动 init 源码 | ⏳ |
| configure_mode_bits | `spi_ls_configure()`：MSTR（`:84`）/ LSBFIRST（`:93-97`）/ DS（`:100-104`）/ CPOL（`:106-110`）/ CPHA（`:112-116`）/ SSOE（`:137`）/ TXFTH（`:138`）；`spi_ls_complete()` 的 `SPE=0`（`:269-272`） | 功能通路 | 一次合法传输后回读：`CR1.MSTR==1`、`CPOL==1`、`CPHA==1`、`LSBFIRST==0`、`BR==/16`、`SPE==0`；`CR2.DS==0x7`、`SSOE==1`、`TXFTH==4`；`ret==0` | 驱动源码写值 + `reg_spi_type.h` 位域 | ⏳ |
| configure_lsb_first | `spi_ls_configure()` `:93-97`（`SPI_TRANSFER_LSB` → `SPI_FIRSTBIT_LSB`；默认 → `SPI_FIRSTBIT_MSB`） | 功能通路 | `SPI_TRANSFER_LSB` → `CR1.LSBFIRST==1`；随后 MSB 配置 → `==0`；两次 `ret==0` | 驱动源码 | ⏳ |
| configure_datasize16 | `spi_ls_configure()` `:100-104`（非 8 bit → `SPI_DATASIZE_16BIT`）；`spi_ls_transceive()` `:324-328`（16 bit → `spi_context_buffers_setup(...,2)`） | 功能通路 | `SPI_WORD_SET(16)` → `CR2.DS==0xF`；4 字节（2 帧）传输 `ret==0` | 驱动源码 + HAL 编码 | ⏳ |
| configure_prescaler_table | `spi_ls_configure()` `:123-129` + `scaler[]`（`:55-62`） | 边界长度/时序 | 取 `clock>>(i+3) <= frequency` 的最小 `i`：`9375000→/8`、`5000000→/16`、`3000000→/32`、`1500000→/64`、`800000→/128`、`400000→/256`、`292968→/256`；七次 `ret==0` | 驱动源码公式（本板 clock=75 MHz） | ⏳ |
| configure_freq_boundary | `spi_ls_configure()` `:118-121`（`8*frequency > clock_frequency` → `-EINVAL`） | 边界长度/异常参数 | `frequency == 9375000` → `ret==0` 且 `BR==/8`；`== 9375001` → `ret==-EINVAL` 且 `SPE==0` | 驱动源码不等式。⚠️ 需先 `spi_arm_instance()`（缺陷 1） | ⏳ |
| transceive_null_bufs | `spi_ls_transceive()` `:312-314`（`!tx_bufs && !rx_bufs` → 直接 `return 0`，不经 lock/configure） | 异常参数 | `spi_transceive(dev,cfg,NULL,NULL) == 0`；随后 `spi_release(dev,cfg) == -EINVAL`（旁证未配置） | 驱动源码 | ⏳ |
| transceive_length_sweep | `spi_ls_transceive()` `:324-328` → ISR 逐字节搬运 `spi_ls_data_exchange_master()`（`:168-205`）→ `spi_context_update_tx/rx` | 边界长度 | len ∈ {1,2,3,7,8,15,16,31,32,63,64,127,128,255,256} 每次 `ret==0` 且传输后 `SPE==0` | 驱动源码 + `spi_context_wait_for_completion()` 超时语义 | ⏳ |
| transceive_duplex_pair | `spi_ls_transceive()` `:337-343`（rx_bufs 非空 → 先 `IER=RXNE` 再被 `IER=TXE` 覆盖）；`spi_ls_data_exchange_master()` 的 `spi_context_update_rx` | 功能通路 | 32 B 全双工 `ret==0`；RX 字节实测经 `ZT_INFO` 上报（**不断言内容**，MISO 悬空） | 驱动源码 | ⏳ |
| transceive_tx_only_rx_only | `spi_ls_data_exchange_master()` 的 `spi_context_tx_buf_on()` / `spi_context_rx_buf_on()` 两分支（`:175-204`） | 功能通路 | tx-only（`rx_bufs=NULL`）`ret==0`；rx-only（`tx_bufs=NULL`，`tx_frame=0`）`ret==0` | 驱动源码 | ⏳ |
| error_half_duplex | `spi_ls_configure()` `:72-75`（`SPI_HALF_DUPLEX` → `-ENOTSUP`） | 异常参数 | `ret == -ENOTSUP` | 驱动源码。⚠️ 需先 `spi_arm_instance()`（缺陷 1） | ⏳ |
| error_ti_mode | `spi_ls_configure()` `:77-80`（`SPI_FRAME_FORMAT_TI` → `-ENOTSUP`） | 异常参数 | `ret == -ENOTSUP` | 驱动源码。⚠️ 需先 `spi_arm_instance()`（缺陷 1） | ⏳ |
| release_semantics | `spi_ls_release()` `:373-393`：`!spi_context_configured()` → `-EINVAL`（`:380-382`）；已配置且 `SR.BSY==0` → `spi_context_unlock_unconditionally` + `SPE=0`（`:384-392`） | 异常参数/功能通路 | 未配置 cfg → `-EINVAL`；已配置 → `0` 且 `SPE==0` | 驱动源码 | ⏳ |
| reconfigure_same_cfg_prescaler | `spi_ls_configure()` `:64-70`：**先** `SPE=0` + `BR←scaler[0]`，**后** 才做 `spi_context_configured()` 早退 | 异常参数（缺陷固化） | 首次 400 kHz → `BR==/256`；同一 `spi_config` 指针二次调用 → `ret==0` 但 `BR==/8`（被静默重置） | 驱动源码（**刻意固化缺陷 2**） | ⏳ |
| hold_on_cs_spe | `spi_ls_complete()` `:269-272`（`SPI_HOLD_ON_CS` → 不关 SPE）；`_spi_context_cs_control()`（`spi_context.h:236-253`，HOLD_ON_CS 时 CS 保持） | 功能通路 | HOLD 传输后 `SPE==1`；随后普通传输 → `SPE==0`；`ret` 均 0；`cs_hold` 经 `ZT_INFO` 上报 | 驱动源码 + spi_context 源码 | ⏳ |
| lock_on_hold_release | `spi_context_lock()`（`spi_context.h:89-109`）的 `SPI_LOCK_ON` 短路分支 + `spi_context_release()`（`:111-130`）的持锁分支 + `spi_ls_release()` `:388` | 并发/时序 | LOCK_ON 连发两次 `ret` 均 0（第二次走 lock 短路）；`spi_release` → 0 | spi_context 源码契约 | ⏳ |
| concurrent_two_threads | `spi_ls_transceive()` `:316` `spi_context_lock()`（`k_sem`，`K_FOREVER`）→ `:369` `spi_context_release()` | 并发 | 两线程各 4 轮 × 16 B 全部 `ret==0`（无 `-ETIMEDOUT`、无交错破坏） | 驱动 + spi_context 锁契约 | ⏳ |
| post_reset_state | `spi_ls_init()` `:433-446` 的 reset 路径（`reset_line_toggle`）；`spi_ls_configure()` 的**全量**重配能力 | 复位/掉电 | `reset_line_toggle_dt()` == 0；复位后 `CR1.SPE==0`、`MSTR==0`、**`BR==/8`（CR1.BR 字段 0x10，即该 IP 的复位默认档，非 0）**；随后全新 cfg 传输 `ret==0` 且 `BR==/32` | `reg_spi_type.h` 位域 + 驱动 configure 写入集合；**BR 复位默认值 = 实测**（`cr1_after=0x10`；复位确已生效由 MSTR 1→0 佐证），见 TRIAGE_MISC_2026-09-18.md §2 | ⏳ |

> 状态标记：`⏳` = **P4.5 新增，仅编译通过，待首次实测**。

---

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

## 上游缺陷观察（只读记录，未修改 `zephyr/drivers/spi/spi_ls.c`）

> 依据 `qsh-driver-review` 的驱动审核维度逐条核对，**仅记录**；修复需上游裁决。

| # | 位置 | 问题 | 后果 |
|---|---|---|---|
| 1 | `spi_ls.c:318-321` → `:369`（`goto done`）→ `spi_context.h:111-130` | **首次调用即配置失败时解引用 NULL（P0）**：`spi_ls_configure()` 的 `-ENOTSUP`（半双工 `:72-75`、TI 模式 `:77-80`）与 `-EINVAL`（频率超限 `:118-121`）都发生在 `ctx->config = config`（`:140`）**之前**；随后 `spi_context_release(ctx, err)` 在 `CONFIG_SPI_ASYNC=n` 下执行 `ctx->config->operation`，而首调时 `ctx->config == NULL`（`spi_ls_dev_data_*` 零初始化） | **load access fault** → SoC 异常入口 `while(1)` → 串口静默，整轮套件中断。任何"首次就用非法配置调 `spi_transceive`"的调用者都会踩中（本工程用 `spi_arm_instance()` 规避） |
| 2 | `spi_ls.c:64-70` | `spi_ls_configure()` 在 `spi_context_configured()` 早退**之前**无条件 `SPE=0` + `BR ← scaler[0]`（=/8） | 用同一 `spi_config` 指针重复 `spi_transceive` 时，实际波特率被静默改为 `clock/8`（本板 9.375 MHz），与请求频率无关且无任何日志 |
| 3 | `spi_ls.c:123` | 分频搜索循环上界写错：`for (uint8_t i = 0U; i <= ARRAY_SIZE(scaler); i++)` —— `scaler[]` 长 6，`i` 可达 6 | `config->frequency < clock>>8`（本板 < 292968 Hz）时读 `scaler[6]`（越界，读到相邻 .rodata）并把垃圾值写进 `CR1.BR` ⇒ 实际分频不可预测；且驱动对"频率过低"从不返回错误 |
| 4 | `spi_ls.c:262`、`:172`、`:190` | `spi_ls_complete()` 的 `while (REG_FIELD_RD(spi->SR, SPI_SR_BSY) == 1U);` 与 `spi_ls_data_exchange_master()` 的 TXE/RXNE 自旋都**无超时**，且后者在 **ISR 上下文**内忙等 | 硬件异常（BSY 卡住 / 时钟未使能）时 ISR 内永久自旋 → 看门狗/整机挂死，无 `-ETIMEDOUT` 保护（上层 `spi_context_wait_for_completion()` 的超时只有在 ISR 能返回时才生效） |
| 5 | `spi_ls.c:117-121` | 频率上限检查排在 MSTR/LSBFIRST/DS/CPOL/CPHA 写入（`:83-116`）与 `SPE=0`（`:65`）**之后**，且不还原已写字段 | 返回 `-EINVAL` 时外设已被"半配置"（除 BR/FIFO 外全部生效）—— 非原子失败，调用方拿到错误码也无法假定寄存器未被改动 |
| 6 | `spi_ls.c:137` | 无条件 `REG_FIELD_WR(spi->CR2, SPI_CR2_SSOE, 1)`，覆盖了 `:86-88`「`cs-gpios` 存在时不启用硬件 CS」的意图 | CS-GPIO 场景下硬件 NSS 输出仍被使能；若该引脚被复用为其它功能会引入冲突 |
| 7 | `spi_ls.c:345-357` | 首字节预载优化被 `#if defined(CONFIG_SOC_SERIES_LE501X)` 包住 | 本 SoC（`lsqsh`）**不生效** ⇒ 主模式首个字节也必须靠 TXE 中断推进；若 TXE 中断未使能/未连接，传输根本不启动（表现为 `-ETIMEDOUT`，而非常见的"发得慢"） |
| 8 | `spi_ls.c:281-296` | `spi_ls_isr()` 内 `err` 在 `spi_context_tx_on||rx_on` 为假时未重新赋值即参与 `if (err \\|\\| ...)` 判断（当前依赖上一行 `spi_ls_get_err()` 的赋值，尚安全） | 逻辑脆弱：后续若在两次赋值之间插入 `continue`/分支即静默变成未初始化读 |
| 9 | `spi_ls.c:484-487` | `clock_frequency` 取值来自 `DT_INST_PROP_BY_PHANDLE(id, clocks, clock_frequency)`（即 `SPI1_CLOCK` 的 `clock_frequency`），而节点自身声明的 `clock-frequency = <PBUS4_CLOCK>` 被忽略 | 两者若不一致会静默取到另一个值；本板两者都是 75 MHz，暂未见差异 |
| 10 | `spi_ls.c:373-393` | `spi_ls_release()` 用**指针比较**判定"是否已配置"（`spi_context_configured`），而 `spi_context_unlock_unconditionally()` 又无条件 give 锁 | 调用方传等价但不同地址的 `spi_config` 会拿到 `-EINVAL` 且不解锁（锁泄漏）；API 契约的"等价配置"语义不可用 |
| 11 | `spi_ls.c:140` | `ctx->config = config` 保存的是**调用方指针**（`const struct spi_config *`），驱动不做拷贝 | 调用方传栈上/临时对象时，`ctx->config` 在后续调用里成为悬垂指针（`spi_context_release` / `_spi_context_cs_control` 都会解引用它）；本工程因此把全部 config 放在静态槽位 |
| 12 | `spi_ls.c:359-360` | **同步路径重复 complete（P0，C1 新发现）**：`spi_ls_transceive()` 先 `spi_context_wait_for_completion()`，紧接着又调 `spi_ls_complete()` —— 后者 `:274` 会 `spi_context_complete()`，非 ASYNC 下该函数无条件 `k_sem_give(&ctx->sync)`（`spi_context.h:209-212`）。对照正确写法 `spi_gd32.c:400-406`（同为 STM32-like SPI）：同步模式**不再** complete，只在 `#ifdef CONFIG_SPI_ASYNC` 下调 | **每完成一次传输，`ctx->sync` 就残留 1 个令牌**（ISR 给一次、线程再给一次，limit=1 故稳定为 1）。对 spi_ls 实例的第 2 次及以后 `spi_transceive()` 会**立刻返回、根本不等待**。主设备侧因 ISR 把线程饿死到传输结束而表象正常；**从设备侧**返回后 `spi_ls_complete()` 立刻 `SPE=0`（`:269-272`），从机永远等不到主机时钟 ⇒ 这正是上轮 `link_probe` 里 `slave_ret = 0`（而非设计预期的 -115 有界超时）的来源。**本工程不得改驱动**，只在 `spi_loopback.c` 的 `lb_drain_stray_sync()` 里做有自检的规避（发起从设备传输前清该令牌）；驱动修复后该规避可整体删除 |

## 预期数据说明

- 错误码契约：`0` / `-ENOTSUP` / `-EINVAL` / `-ETIMEDOUT`（驱动与 `spi_context` 源码分支，行号见上表）。
- 寄存器回读：按 `reg_spi_type.h` 的**位域声明**取字段（`SPI_CR1_*` / `SPI_CR2_*` / `SPI_SR_*`），**不用猜位宽**。
- 分频档编码来自 HAL `ls_hal_spi_i2s.h:182-187`，与驱动 `scaler[]`（`spi_ls.c:55-62`）同序；本工程在测试内重述该编码（`SPI_LS_BR_DIV*`），不用"看起来像"的字面值。
- 数值实测经 `ZT_INFO`（§6.5.4）上报，报告"实际"列对比；套件 `spi`（原有 18 项）**不断言**任何依赖总线对端的数据内容；套件 `spi_loopback`（P4.6 新增）则**以逐字节比对为判据**（对端 = 跳线连的 spi1）。
- 无外部黄金向量文件（见 `expected/README.md`）。

## P4.5 生成说明（2026-09-17，只生成 + 编译通过，未烧录）

- 新增 **18 项**，编译通过（`west build` rc=0，`lsqsh_evb@1os/lsqsh/cpu1`）。
- 分型：**单核**（只有 `cpu1_spi_test/` 一个固件目录，SPEC_REFERENCE §6.1.10）。
- 每个用例自带前置，不依赖其它用例的副作用或执行顺序：
  - 触发配置错误的用例（`configure_freq_boundary` / `error_half_duplex` / `error_ti_mode`）先调
    `spi_arm_instance()` 把 `ctx->config` 建起来，规避缺陷 1 的 NULL 解引用；
  - 每个用例使用**各自独立的 `spi_config` 静态对象**，避免命中缺陷 2 的 `spi_context_configured()` 早退；
  - `hold_on_cs_spe` 末尾用一次普通传输把实例恢复成常规态；
  - `lock_on_hold_release` 在**任何断言之前**调 `spi_release()`，避免断言跳出时锁泄漏。
- `spi_test.h` 的实例表按 `SPI_INST_NUM = 2`（只含 `spi1`/`spi2`），越界访问返回 NULL/0。
- `tests/scripts/dump_results.py` 的 `TEST_NAMES` 按**链接序**（`nm --numeric-sort`）与本工程测试项对齐。

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

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
>
> ★ 本工程「预期来源」列大量写的是 **`驱动源码`**。这类预期值是从**被测驱动自己的
> 实现**里读出来的（"驱动写 0x11f 到 CR1" ⇒ 断言 CR1==0x11f），判定上归
> `driversrc`：**有判别力但只算弱** —— 驱动写值一变就 DISAGREE，可**既有缺陷会被
> 一起抄进预期**，所以它证的是"没变"，不是"仍对"。只有 `device_ready` 的基址来自
> DT（`spec`），是真强预期。

```expect
# 测试项 | 断言 | 来源标签:说明
device_ready | spi1_base == 0x4008a000; spi2_base == 0x4008a400 | spec:DT `spi1`/`spi2` reg 基址（zephyr.dts 实读），与驱动实现无关
configure_mode_bits | ret == 0; cr1 == 0x11f; cr2 == 0x40704 | driversrc:CR1/CR2 整字转录自驱动写入集合（reg_spi_type.h 定位位域）
configure_lsb_first | ret_lsb == 0; lsb_bit == 128; ret_msb == 0; msb_bit == 0 | driversrc:CR1.LSBFIRST 位值
configure_datasize16 | ret == 0; ds == 0xf00 | driversrc:CR2.DS 字段移位后的整字值
configure_freq_boundary | ret_ok == 0; ret_bad == -22; spe_after_bad == 0 | driversrc:边界档 BR 值与拒绝路径；-22 = -EINVAL 属 API 契约
error_half_duplex | ret == -134 | contract:API 契约 -ENOTSUP（HAL 无半双工）
error_ti_mode | ret == -134 | contract:API 契约 -ENOTSUP（HAL 无 TI 模式）
transceive_null_bufs | ret_null == 0; ret_release == -22 | contract:API 契约（NULL 双向合法；未配置即 release ⇒ -EINVAL）
transceive_tx_only_rx_only | ret_tx == 0; ret_rx == 0 | contract:API 契约
transceive_length_sweep | n == 15; last_ret == 0 | contract:长度集共 15 档（原文列举），最后档仍 ret==0
release_semantics | ret_unused == -22; ret_tx == 0 | contract:API 契约（未配置 ⇒ -EINVAL；已配置 ⇒ 0）
lock_on_hold_release | ret1 == 0; ret2 == 0; ret_rel == 0 | contract:spi_context LOCK_ON 短路契约
hold_on_cs_spe | spe_hold != 0; spe_norm == 0 | driversrc:SPE 位在 HOLD 传输后保持、普通传输后被清
concurrent_two_threads | ret_a == 0; ret_b == 0; iter == 4 | contract:并发下两线程均无 -ETIMEDOUT
reconfigure_same_cfg_prescaler | ret1 == 0; ret2 == 0; br_first == 0x38; br_second == 0x10 | driversrc:**刻意固化缺陷 2**（同 cfg 二次调用 BR 被静默重置）；这是"现状基线"，不构成正确性证明
post_reset_state | ret_rst == 0; cr1_after == 0x10 | driversrc:复位后 CR1 字（原文明确给出 CR1 字段 0x10）；`br_after` 原文与实测档位表述不一致，**不登记**以免编预期
configure_prescaler_table | !none | !none:预期是"取 clock>>(i+3) <= f 的最小 i"的**选择规则**，实测上报的是 `频率/br=` 复合串；断言整串等于照抄，而规则本身不在 INFO 里 ⇒ 不硬凑
transceive_duplex_pair | !none | !none:RX 字节原表就声明"MISO 悬空，不断言内容"，只有 ret==0 可判但无独立价值
link_probe | !none | !none:需外部跳线；本次归档 master_ret=-116（-ETIMEDOUT）判 FAIL，期望 master_ret==0 的前置条件（跳线）未在证据中体现
roundtrip_length_sweep | !none | !none:需外部跳线回环；本次 SKIP
roundtrip_fifo_boundary | !none | !none:同上
roundtrip_pattern_matrix | !none | !none:同上
roundtrip_max_length_256 | !none | !none:同上
roundtrip_master_tx_slave_rx | !none | !none:同上
slave_ret_recv_frames | !none | !none:同上
repeat_roundtrips_state_isolation | !none | !none:同上
```
