# SPI 测试覆盖矩阵

> **两个部分**：
> ① **原有 18 项**（套件 `spi`，`spi_ls.c` 驱动内部路径，`外部: 无`）；
> ② 2026-10-27 新增 11 项（套件 `spi_flash`，spif3 master -> spi4 -> flash4_0，
>    板载无外部接线；静态缓冲 + malloc 缓冲，覆盖对齐/非对齐）；原 `spi_loopback` 8 项已删除并归档到
>    `810_driver_test/request/skill/spi_test/spif_loopback_decision.md`。
> —— 原「回环用例」「回环可行性结论」两节已删除，设计归档见上。**原有 18 项零改动。**

- 驱动: `zephyr/drivers/spi/spi_ls.c`（compatible `linkedsemi,ls-spi`）
  ＋（2026-10-27 起）`zephyr/drivers/spi/spi_dw.c`（`snps,designware-spi`，spif3 master 的底层 SPI 控制器，flash4_0 通路）
- 接口: `zephyr/drivers/spi.h`（`spi_transceive` / `spi_release`）；底层上下文 `zephyr/drivers/spi/spi_context.h`
- 寄存器: `modules/hal/linkedsemi/hal_driver/inc/reg_spi_type.h`（`reg_spi_t` / 字段掩码）；HAL 编码常量 `hal_driver/api/ls_hal_spi_i2s.h`
- 外部路径（**分区**）：
  - **套件 `spi`（原有 18 项）：`外部: 无`** —— `spi1`/`spi2` 的 SCK/MOSI/MISO + CS 全部由**板级 pinctrl** 指定（`zephyr/boards/linkedsemi/lsqsh_evb/lsqsh_evb_pinctrl.dtsi:381-391`），片内引脚组；总线上**没有任何 DT 声明的从设备** ⇒ 判据全部来自驱动源码分支与寄存器语义（依据见下「实例范围与 Step-0 可行性结论」）。
  - **套件 `spi_flash`（2026-10-27 新增 10 项）：`外部: 无`** ——
    spif3 master -> spi4 -> flash4_0（板载 NOR），读/写/擦除全部通过板载器件验证。

## 实例范围与 Step-0 可行性结论（P4.5，2026-09-17）

### 1. `spi_ls.c` 只绑定 `spi1` / `spi2`

| 实例 | 地址 | compatible | 归属驱动 |
|---|---|---|---|
| `spi1` | `0x4008a000` | `linkedsemi,ls-spi` | **`spi_ls.c`（本工程）** |
| `spi2` | `0x4008a400` | `linkedsemi,ls-spi` | **`spi_ls.c`（本工程）** |
| `spi3` | `0x4006e000` | `snps,designware-spi` | `spi_dw.c`（**本工程 flash 通路**） |
| `spi4` | `0x4006e400` | `snps,designware-spi` | `spi_dw.c`（**本工程 flash 通路**） |

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
- 器件确实存在：本工程实测 `jedec-id = [ef 40 21]`、`flash4@0: 128 MiBy flash`（2026-10-27）；
  sample README 的旧记录为 `85 23 19` / 32 MiB，以实测为准。

⇒ **该通路现由本工程 `spi_flash`（2026-10-27 起）覆盖；`spif_test` 另覆盖 filter 语义与寄存器路径。**

### 3. `spi1` / `spi2` 上没有可确定性到达的从设备

- `lsqsh.dtsi:1179-1205`：两节点 `status = "disabled"`，节点内**无 `cs-gpios`、无子设备**。
- 板级 dts 里 `&spi1..&spi4` 的 flash 子节点**全部被注释掉**（`lsqsh_evb_cpu1.dts:426-461`）。
- **全树**（`zephyr/`、`modules/lib/pfr_tpm/`、`linkedsemi_zephyr_project/`）搜 `&spi1` / `&spi2`：
  除本工程与 P4 的 `spid_test`（把 `spi1` 当 SPI master 用、需**外部跳线**到 `tpm_spis1`）外，
  只有 `linkedsemi_zephyr_project/samples/spi_flash_communication/boards/lsqsh_evb_cpu1.overlay`
  在 `&spi2` 下声明了 `jedec,spi-nor`。该 sample **不足以作为本板有器件的证据**：
  - 同目录 `README.rst` 首行即 `NOTICE: The SPI and external flash pins need to be consistent with the hardware`；
  - README 记录的实测输出标注为「if board is **qsh610**」，而全树不存在 `qsh610` 这个板名；
  - 其记录的 JEDEC `85 63 19` 与本板实测 `spif3→spi4` 通路 NOR（`ef 40 21`，128 MiB）**不符**；
  - 该 overlay 与 `ls101x_evb.overlay` 是"按驱动类型对齐"的移植（ls-spi↔ls-spi、DW↔DW），
    属把 sample 跑通用的适配，不构成硬件事实。
- 板级 pinctrl 确实为两实例分配了完整引脚组 + 专用 CS GPIO
  （`spi1`: PA00/PA01/PA02 + `gpioa 3`；`spi2`: PJ12/PJ13/PJ14 + `gpioj 15`），
  但**引脚被分配 ≠ 器件在板**；本阶段（只生成不烧录）无法在硬件上确证。

### 4. 结论：**没有可确定性到达的从设备，但驱动内部路径可确定判定 ⇒ 工程可建（有条件）**

- **18 项 `spi` 不建**依赖从设备的用例（数据回环/回显）；`spi_flash` 单独覆盖板载 flash 的 JEDEC/读写/擦除，不涉及外部从设备。
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

## spi_flash 用例（套件 `spi_flash`，11 项，2026-10-27 新增）

被测通路（板载，无外部接线）：
`spif3`（`linkedsemi,spi-filter`，master 态）→ `spi4`（`snps,designware-spi`）
→ `flash4_0`（`jedec,spi-nor`，128 MiB）。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|
| `alloc_read_smoke` | `k_malloc(4096)` 只读 4096 B | 功能通路 | ret=0（不比对内容） | driversrc | ✅ |
| `flash_ready` | `spi_nor_multi_dev.c` init / `flash_read_jedec_id()` | 功能通路 | JEDEC 非 0/FF；容量覆盖测试区 | spec:JEDEC/SFDP | ✅ |
| `erase_all_ff` | `spi_nor_multi_dev.c` erase path | 擦除语义 | 擦除后回读全 0xFF | driversrc | ✅ |
| `read_aligned_len_sweep` | `spi_nor_multi_dev.c` read + DW SPI RX DMA | 边界长度 | 每档 ret=0 且逐字节等于写入 pattern | driversrc | ✅ |
| `read_malloc_aligned_len_sweep` | `k_aligned_alloc(32,len)` 读 1..4096 | 边界长度 | 每档 ret=0 且逐字节相等 | driversrc | ✅ |
| `read_malloc_unaligned_len_sweep` | `k_malloc(len)` 读 1..4096 | 边界长度 | 同上 | driversrc | ✅ |
| `read_unaligned_len_sweep` | 同上（缓冲区 +8） | 边界长度 | 同上 | driversrc | ✅ |
| `write_aligned_len_sweep` | `spi_nor_multi_dev.c` write + DW SPI TX | 边界长度 | 每档 ret=0 且逐字节相等 | driversrc | ✅ |
| `write_malloc_aligned_len_sweep` | `k_aligned_alloc(32,len)` 写 1..256 | 边界长度 | 每档 ret=0 且逐字节相等 | driversrc | ✅ |
| `write_malloc_unaligned_len_sweep` | `k_malloc(len)` 写 1..256 | 边界长度 | 同上 | driversrc | ✅ |
| `write_unaligned_len_sweep` | 同上（缓冲区 +8） | 边界长度 | 同上 | driversrc | ✅ |

测试区：`0x100000`（读/擦）、`0x110000`（对齐写）、`0x120000`（非对齐写）。

> 原 `spi_loopback` 8 项（SPI1↔SPI3 外部跳线）已于 2026-10-27 删除，
> 设计/接线/失败现象归档到
> `810_driver_test/request/skill/spi_test/spif_loopback_decision.md`，
> 供后续迁移到 `spif_test` 使用。

## 跳过项（2026-10-27 更新）

- 原 `spi_loopback`（SPI1↔SPI3 外部跳线）已删除，不再作为本工程用例；设计归档见上。
- `spi1`/`spi2` 总线上仍无可确定性到达的从设备；数据回环/回显类不在本工程覆盖。

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
| 12 | `spi_ls.c:359-360` | **同步路径重复 complete（P0，C1 新发现）**：`spi_ls_transceive()` 先 `spi_context_wait_for_completion()`，紧接着又调 `spi_ls_complete()` —— 后者 `:274` 会 `spi_context_complete()`，非 ASYNC 下该函数无条件 `k_sem_give(&ctx->sync)`（`spi_context.h:209-212`）。对照正确写法 `spi_gd32.c:400-406`（同为 STM32-like SPI）：同步模式**不再** complete，只在 `#ifdef CONFIG_SPI_ASYNC` 下调 | **每完成一次传输，`ctx->sync` 就残留 1 个令牌**（ISR 给一次、线程再给一次，limit=1 故稳定为 1）。对 spi_ls 实例的第 2 次及以后 `spi_transceive()` 会**立刻返回、根本不等待**。主设备侧因 ISR 把线程饿死到传输结束而表象正常；**从设备侧**返回后 `spi_ls_complete()` 立刻 `SPE=0`（`:269-272`），从机永远等不到主机时钟 ⇒ 这是从设备路径上出现异常超时/提前返回的来源。**本工程不得改驱动**；本工程 18 项用例通过 `spi_arm_instance()` / 静态 config 规避该路径。 |

## 预期数据说明

- 错误码契约：`0` / `-ENOTSUP` / `-EINVAL` / `-ETIMEDOUT`（驱动与 `spi_context` 源码分支，行号见上表）。
- 寄存器回读：按 `reg_spi_type.h` 的**位域声明**取字段（`SPI_CR1_*` / `SPI_CR2_*` / `SPI_SR_*`），**不用猜位宽**。
- 分频档编码来自 HAL `ls_hal_spi_i2s.h:182-187`，与驱动 `scaler[]`（`spi_ls.c:55-62`）同序；本工程在测试内重述该编码（`SPI_LS_BR_DIV*`），不用"看起来像"的字面值。
- 数值实测经 `ZT_INFO`（§6.5.4）上报，报告"实际"列对比；套件 `spi`（原有 18 项）**不断言**任何依赖总线对端的数据内容；套件 `spi_flash`（2026-10-27 新增）**以逐字节比对为判据**（写后读回 / 擦除后 0xFF）。
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

## 历史说明（spi_loopback，已删除）

原 `spi_loopback` 8 项（2026-09-17 新增，2026-10-27 删除）的接线、用例设计、
失败现象与 C1 修复记录已归档到
`810_driver_test/request/skill/spi_test/spif_loopback_decision.md`，供以后迁移到
`spif_test` 时复用。本工程不再包含任何回环用例。

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
flash_ready | jedec != 0; size > 0x130000 | spec:JEDEC ID / SFDP 容量（板载 NOR）
erase_all_ff | ff_bytes == total | driversrc:擦除后回读必须全 0xFF
read_aligned_len_sweep | fail == 0; first == 0 | driversrc:对齐读 1..4096 逐字节比对
read_unaligned_len_sweep | fail == 0; first == 0 | driversrc:非对齐读 1..4096 逐字节比对
write_aligned_len_sweep | fail == 0; first == 0 | driversrc:对齐写 1..256 擦写读比对
write_unaligned_len_sweep | fail == 0; first == 0 | driversrc:非对齐写 1..256 擦写读比对
```
