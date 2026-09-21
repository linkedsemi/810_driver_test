# QSPI flash 测试覆盖矩阵

- 工程：`projects/qspi_flash_test`（单核，board `lsqsh_evb@1os/lsqsh/cpu1`）
- 被测硬件：`qspi2@40040000` + 板载 32 MiB Puya NOR（JEDEC ID `85 23 19`），
  pinmux 板载默认（PC05=CSN / PC06-09=DAT0-3 / PC10=SCK，FUNC4）
- 被测驱动：
  - `zephyr/drivers/spi/qspi_ls.c`（`linkedsemi,ls-qspi`）
  - `zephyr/drivers/flash/spi_nor_multi_dev.c`（`jedec,spi-nor` 上层）
- 测试项总数：34（4 个 ztest 套件）
- 外部环境：**无**（板载器件，零外部接线；SPEC_REFERENCE §6.6.1「无」分支）
- **器件运行态（raw_op 用例必须对齐，否则驱动仍返回 0 但内容错位）**：
  - **SPI 单线**，**非 QPI 4-4-4**：`qspi2` 的 DT `spi-ctrl-caps-mask = <0x40000>`
    是 **disable 掩码**（binding `linkedsemi,ls-qspi.yaml`：bit18 = *disable* 4-4-4；
    驱动按 `cap_mask = ~(ctrl|nor)` 取反后判 `& SPI_NOR_MODE_4_4_4_CAP`，
    `spi_nor_multi_dev.c:2490/2498/2644`）⇒ `0x40000` 恰好**关闭** 4-4-4，
    `spi_nor_enter_qpi_mode()` 不会被调用。
  - **4 字节地址**：容量 32 MiB > 16 MiB ⇒ `spi_nor_multi_dev.c:2676` 使能 4 字节
    地址模式 ⇒ flash 层存储访问一律 `addr_len = 4`（`:1380`）。
  - ⇒ 所有 raw 存储读/写必须 `addr_len = 4`（TRIAGE_QSPI_SPIF_2026-09-18.md
    §1.1 Q1 的结论方向正确，但其"上电即进 QPI"的机制描述**有误**，已按上述更正）。
- 状态口径：`⬜` = 未实测（P2 阶段只做「生成 + 编译通过 + 合规自检」，
  烧录/采集/判定统一推迟到 P6，见 roadmap §4「执行模式」）；
  `⚠` = 钉住已知缺陷现状（断言的是**当前实现行为**，非「正确性已通过」）

## 覆盖矩阵

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| jedec_id_match | `spi_nor_read_jedec_id` → `spi_nor_op_exec` → `qspi_ls_nor_transceive` → `stg_reg_read`(RDID) | 功能通路 | ret==0 且 id==`85 23 19` | roadmap §5.1 已核实（`samples/spi_ls_flash_nor/README.rst`） | ⬜ |
| jedec_id_cached | `spi_nor_get_jedec_id`（读 `data->jedec_id`） | 功能通路 | ret==0 且 id==`85 23 19` | API 契约 + 同上 | ⬜ |
| jedec_id_null_arg | `spi_nor_read_jedec_id` / `spi_nor_get_jedec_id` 的 `id == NULL` 分支 | 异常参数 | `-EINVAL` | `spi_nor_multi_dev.c:1795-1797,1926-1928` | ⬜ |
| get_parameters | `flash_nor_get_parameters` | 功能通路 | `erase_value==0xff`、`write_block_size==4096` | `spi_nor_multi_dev.c:3015`（DT `write-block-size` 缺省 0x1000） | ⬜ |
| sfdp_header | `spi_nor_sfdp_read` → `read_sfdp` → `stg_mem_read`(RDSFDP) | 功能通路 | ret==0 且前 4 字节 == ASCII `"SFDP"` | JESD216 规范（SFDP 头签名） | ⬜ |
| size_is_32mib | `flash_get_page_info_by_offs` + `dev_flash_size()`（SFDP 运行时容量） | 功能通路/边界长度 | 偏移 32 MiB-1 命中；偏移 32 MiB 越界 | roadmap §5.1（32 MiB 板载 NOR） | ⬜ |
| line_width_set | `spi_nor_set_line_width` 的 `buswidth > 4` 分支 / 正常分支 | 异常参数 | 8 → `-EINVAL`；1 → 0 | `spi_nor_multi_dev.c:1904-1913` | ⬜ |
| four_byte_mode_unknown_mfr | `spi_nor_get_4byte_mode` 的 `switch (id[0])` default 分支 | 异常参数 | `-ENXIO`（Puya 0x85 不在白名单） | `spi_nor_multi_dev.c:1953-1990` **钉住驱动缺口** | ⚠ |
| erase_sector_all_ff | `flash_erase` → `spi_nor_erase`（SE 循环 + WIP 等待） | 功能通路 | ret==0；读出全 `0xFF` | API 契约 + `erase_value=0xff` | ⬜ |
| write_read_len1 | `spi_nor_write`（1 B，页内）→ `stg_mem_write` | 功能通路 | ret==0；回读逐字节一致 | API 契约 | ⬜ |
| write_read_len255 | 同上，255 B | 边界长度 | 同上 | API 契约（sample 实测长度） | ⬜ |
| write_read_len256 | 同上，256 B（典型 NOR 页上限） | 边界长度 | 同上 | API 契约 + `dev_page_size()` | ⬜ |
| write_read_len4096 | 同上，4096 B（整扇区，多页翻页写） | 边界长度 | 同上 | API 契约 + `QSPI_SECTOR_SIZE` | ⬜ |
| write_read_len8192 | 同上，8192 B；读侧同时覆盖 `stg_mem_read` 的 4096 切块拼接 | 边界长度 | 同上 | `qspi_ls.c:1160-1176`（`MIN(remain, 4096)` 循环） | ⬜ |
| erase_unaligned_addr_rejected | `spi_nor_erase` 的 `SPI_NOR_IS_SECTOR_ALIGNED(addr)` 检查 | 异常参数 | `-EINVAL` | `spi_nor_multi_dev.c:1577-1580` | ⬜ |
| erase_non_sector_multiple_rejected | `spi_nor_erase` 的 `size % 4096 != 0` 检查 | 异常参数 | `-EINVAL` | `spi_nor_multi_dev.c:1582-1585` | ⬜ |
| read_past_end_rejected | `spi_nor_read` 的 `addr + size > flash_size` 检查 | 异常参数 | `-EINVAL` | `spi_nor_multi_dev.c:1386-1389` | ⬜ |
| write_past_end_rejected | `spi_nor_write` 的 `size + addr > flash_size` 检查 | 异常参数 | `-EINVAL` | `spi_nor_multi_dev.c:1477-1480` | ⬜ |
| stg_cmd_wren_ok | `stg_transceive` → `addr_len==0 && data_len==0` → `stg_cmd` | 功能通路 | ret==0 | `qspi_ls.c:1208-1211` | ⬜ |
| raw_read_1_1_1_ok | `stg_wire_cfg_fill` `JESD216_MODE_111` 读分支 + `stg_mem_read` | 功能通路 | ret==0；回读内容一致（**raw 读须与运行态一致：`MODE_111` + `0x03` + `addr_len=4`**） | `qspi_ls.c:206-225`；`addr_len` 由器件 4 字节地址态决定（见顶部「器件运行态」），见 TRIAGE_QSPI_SPIF_2026-09-18.md §1.1 Q1 | ⬜ |
| raw_read_1_1_4_ok | `stg_wire_cfg_fill` `JESD216_MODE_114` 读分支（含 dummy） | 功能通路 | ret==0 | `qspi_ls.c:333-354` | ⬜ |
| raw_read_1_4_4_ok | `stg_wire_cfg_fill` `JESD216_MODE_144` 读分支（mode_clocks=2 + wait） | 功能通路 | ret==0 | `qspi_ls.c:356-401` | ⬜ |
| mode_222_rejected | `stg_wire_cfg_fill` 落 `default:`（MODE_222 未实现） | 异常参数 | `-EINVAL` | `qspi_ls.c:403-406` **钉住驱动缺口**（roadmap §5.2 qspi#1） | ⚠ |
| stg_write_len_4096_accepted | `stg_mem_write` → `stg_cfg_set_dat` 的 4096 上限**边界内** | 边界长度 | ret==0 | `qspi_ls.c:172-176`（`len > 4096` 才拒） | ⬜ |
| stg_write_len_4097_rejected | `stg_mem_write` → `stg_cfg_set_dat` 的 4096 上限**越界** | 边界长度 | `-EINVAL` | `qspi_ls.c:172-176` + `:1181-1195` | ⬜ |
| stg_reg_read_len_4097_rejected | `stg_reg_read` → `stg_cfg_set_dat` 同一上限（寄存器读路径） | 边界长度 | `-EINVAL` | `qspi_ls.c:172-176` + `:1093-1105` | ⬜ |
| stg_read_chunked_over_4096 | `stg_mem_read` 的 4096 切块循环 + 地址递增 | 边界长度 | ret==0；8192 B 逐字节一致（**同 `raw_read_1_1_1_ok`：`addr_len=4`**，切块逻辑本身与地址长度无关） | `qspi_ls.c:1147-1180`；见 TRIAGE_QSPI_SPIF_2026-09-18.md §1.1 Q2 | ⬜ |
| spi_transceive_notsup | `qspi_ls_transceive`（`spi_driver_api.transceive`） | 异常参数 | `-ENOTSUP` | `qspi_ls.c:1281-1292`（roadmap §5.2 qspi#3） | ⬜ |
| calib_status_null | `qspi_timing_calibration_status(NULL)` | 异常参数 | `-EINVAL` | `qspi_ls.c:1065-1075` | ⬜ |
| calib_status_expected | `qspi_ls_timing_calibration` 的 `timing_calibration_disabled` 早退 / 正常成功分支 | 功能通路 | case_default → `SUCCESS(1)`；case_no_calib → `UNUSED(0)` | `qspi_ls.c:952-967, 1030-1039` | ⬜ |
| set_freq_above_max_rejected | `spi_nor_set_freq`（不校验）+ `spi_nor_re_init`→`read_init`→`apply_clk` 的 `frequency > bus` 分支 | 异常参数 | `set_freq==0`；`re_init != 0` | `qspi_ls.c:828-832` | ⬜ |
| set_freq_below_min_rejected | `apply_clk` 的 `clk_cyc > 31` 分支（200MHz/6MHz-1 = 32） | 边界长度 | `set_freq==0`；`re_init != 0` | `qspi_ls.c:834-838` | ⬜ |
| set_freq_boundary_6m25_accepted | `apply_clk` 边界内沿（200MHz/6.25MHz-1 = **31**） | 边界长度 | `re_init == 0` | `qspi_ls.c:40-41, 834-838` | ⬜ |
| set_freq_zero_silent | `apply_clk` 的 `config->frequency == 0U` 早退分支 | 异常参数 | `re_init == 0`（**静默不改时钟**） | `qspi_ls.c:824-826` **钉住驱动缺陷**（roadmap §5.2 qspi#4） | ⚠ |

## 边界常量来源（qsh-test-validate §1.9h：不得凭猜）

| 常量 | 值 | 来源 |
|------|----|------|
| `QSPI_STG_DAT_MAX` | 4096 | `qspi_ls.c:35` `STG_DAT_PHASE_BYTES_MAX`（`stg_byt_num[15:4]`，12 bit，bytes = byt_num+1） |
| `QSPI_SECTOR_SIZE` | 4096 | `spi_nor_multi_dev.c` `SPI_NOR_SECTOR_SIZE` + DT `erase-block-size = <4096>` |
| `QSPI_BUS_HZ` | 200 MHz | `lsqsh.dtsi:16` `#define SDHCI_CLOCK DT_FREQ_M(200)` → `qspi2 { clock-frequency = <SDHCI_CLOCK> }` |
| `QSPI_FREQ_MIN_OK` | 6 250 000 | `bus/32`，`clk_cyc = bus/spi - 1 = 31`，恰好不超 31 |
| `QSPI_FREQ_BELOW_MIN` | 6 000 000 | `bus/33`（整除），`clk_cyc = 32 > 31` → `-EINVAL` |
| `QSPI_FREQ_ABOVE_MAX` | 200 000 001 | `> bus_hz` 分支 |
| `QSPI_FLASH_EXPECT_SIZE` | 0x2000000 | roadmap §5.1（板载 32 MiB Puya NOR） |

## 钉住缺陷清单（⚠ 项，断言的是当前实现行为，不是"正确性通过"）

| 测试项 | 缺陷 | 位置 | 若驱动修复后的表现 |
|--------|------|------|-------------------|
| `four_byte_mode_unknown_mfr` | `spi_nor_get_4byte_mode()` 厂商白名单仅 MXIC/Winbond/GD，Puya 落 default → `-ENXIO` | `spi_nor_multi_dev.c:1953-1990` | 新增厂商支持后该用例会失败，需同步更新 |
| `mode_222_rejected` | `stg_wire_cfg_fill()` 未实现 `JESD216_MODE_222`，但 binding 的 `spi-ctrl-caps-mask` 宣称 4-4-4/quad 能力 | `qspi_ls.c:403-406` | 补齐 MODE_222 后该用例会失败，需同步更新 |
| `set_freq_zero_silent` | `apply_clk()` 对 `frequency == 0` 静默返回 0、不改时钟也不报错（正确语义应为 `-EINVAL`） | `qspi_ls.c:824-826` | 改为报错后该用例会失败，需同步更新 |

## 跳过 / 未覆盖（明确"未覆盖"，非"验证过没问题"）

| 项 | 原因 | 判据类型 |
|----|------|---------|
| `JESD216_MODE_112 / 122`（1-1-2 / 1-2-2）wire 分支 | 板载 Puya NOR 的 SFDP 能力位决定 `spi_nor` 层选用的读命令；本板 `spi-ctrl-caps-mask = <0x40000>`（4-4-4）时不走双线分支，**无法在不改 overlay 的前提下确定性构造** | 未覆盖（构造不可达） |
| `JESD216_MODE_444`（QPI 4-4-4）读分支 | 同上：需 flash 先进入 QPI 模式（`flag_qpi_mode`）才由 `spi_nor` 层选用；驱动在 init 时会主动 Exit QPI，本工程未构造入 QPI 的路径 | 未覆盖（构造不可达） |
| `stg_reg_read` 恰好 4096 B 成功路径 | 对 1 字节寄存器（RDSR）读 4096 B 属未定义载荷，断言无意义；写侧 4096 已由 `stg_write_len_4096_accepted` 覆盖同一 `stg_cfg_set_dat` 检查点 | 未覆盖（无意义） |
| 复位后数据保持 / XIP 一致性 | `qspi2` 不做 XIP（非 memory-mapped），复位保持性需 XIP 固化流程（SPEC_REFERENCE §6.7），超出本工程范围 | 未覆盖（范围外） |
| 首次访问前 flash 残留 QPI 态（roadmap S-4） | 依赖上一次上电遗留状态，无法在编译期/用例内构造；驱动 `spi_nor` 层 init 会自行 Exit QPI，实测确认留到 P6 | 未覆盖（待 P6 实测观察） |

## 执行顺序与状态污染说明

ztest 套件按链接序（符号名字典序）执行，本工程顺序为：
`qspi_flash_calib` → `qspi_flash_id` → `qspi_flash_rw` → `qspi_flash_stg`。

- `qspi_flash_calib` 会修改 `data->spi.config.frequency` 并触发
  `spi_nor_re_init()`，其**用例级** `after` 钩子（`qspi_calib_after`）在每个
  用例后把频率恢复为设备树值并重跑 init，故不会污染后续套件。
- `set_freq_zero_silent` 之后 `data->calib_combo` 会被置为 1（"已校准"），
  后续不再重新校准——只影响校准是否重跑，不影响任何用例断言。
- `qspi_flash_rw` 与 `qspi_flash_stg` 共用 `QSPI_TEST_BASE + 4*4096` 起的
  8192 B 窗口，但两个用例都各自重新擦写，**不依赖兄弟用例的副作用**。

## 擦写窗口

所有写入/擦除均落在 flash 偏移 `0x00100000`（1 MiB）起的 32 KiB 窗口内，
全部地址 4096 对齐（`flash_parameters.write_block_size == 4096`），
远离 0 偏移，不与任何残留镜像/分区表冲突。
---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
> 来源标签：`handcalc`(手算) `spec`(外部规范/寄存器或 DT 定义) `golden`(黄金向量)
> `crosscheck`(跨来源交叉) `contract`(API 契约值) `driversrc`(转录自被测驱动实现，
> **只证"没变"不证"仍对"**) `samecode`(同源复算，**非独立**) `!none`(无确定预期)。

```expect
# 测试项 | 断言 | 来源标签:说明
spi_transceive_notsup | ret == -134 | contract:API 契约 -ENOTSUP（QSPI 无通用 transceive 通路；★无 INFO 通道 ⇒ NO_ACTUAL）
jedec_id_match | jedec_id == 0x852319 | spec:板载 NOR 厂商/型号 ID（roadmap §5.1 已核实；与驱动实现无关；★无 INFO 通道 ⇒ NO_ACTUAL）
jedec_id_cached | jedec_id == 0x852319 | spec:同上（★无 INFO 通道 ⇒ NO_ACTUAL）
stg_write_len_4097_rejected | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
stg_reg_read_len_4097_rejected | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
calib_status_null | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
calib_status_expected | calib_status == 1 | spec:case_default 的校准状态应为 SUCCESS(1)（enum 由上游定义；★无 INFO 通道 ⇒ NO_ACTUAL）
set_freq_above_max_rejected | set_freq == 0; re_init != 0 | contract:越界时钟必须拒绝且不得静默重配（★无 INFO 通道 ⇒ NO_ACTUAL）
set_freq_below_min_rejected | set_freq == 0; re_init != 0 | contract:同上（★无 INFO 通道 ⇒ NO_ACTUAL）
set_freq_zero_silent | re_init == 0 | contract:0 频率按现状"静默不改时钟"（★无 INFO 通道 ⇒ NO_ACTUAL）
jedec_id_null_arg | !none | !none:-EINVAL，键名未在 INFO 中固定 ⇒ 不硬凑
get_parameters | !none | !none:erase_value/write_block_size 两值合取
sfdp_header | !none | !none:前 4 字节 == ASCII "SFDP"（多字节比对），非单标量
size_is_32mib | !none | !none:边界命中/越界的合取
line_width_set | !none | !none:两个返回码的合取
four_byte_mode_unknown_mfr | !none | !none:-ENXIO（钉住驱动缺口），键名未固定
erase_sector_all_ff | !none | !none:读出全 0xFF，非标量
write_read_len1 | !none | !none:回读逐字节一致，非标量
write_read_len255 | !none | !none:同上
write_read_len256 | !none | !none:同上
write_read_len4096 | !none | !none:同上
write_read_len8192 | !none | !none:同上
erase_unaligned_addr_rejected | !none | !none:-EINVAL，键名未固定
erase_non_sector_multiple_rejected | !none | !none:同上
read_past_end_rejected | !none | !none:同上
write_past_end_rejected | !none | !none:同上
stg_cmd_wren_ok | !none | !none:ret==0，键名未固定
stg_write_len_4096_accepted | !none | !none:同上
stg_read_chunked_over_4096 | !none | !none:8192B 逐字节一致，非标量
raw_read_1_1_1_ok | !none | !none:返回码 + 内容一致的合取
raw_read_1_1_4_ok | !none | !none:返回码，键名未固定
raw_read_1_4_4_ok | !none | !none:同上
mode_222_rejected | !none | !none:-EINVAL（钉住驱动缺口），键名未固定
set_freq_boundary_6m25_accepted | !none | !none:`re_init == 0`，键名未固定
```
