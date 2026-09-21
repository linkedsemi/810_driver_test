# spi_test 预期数据

本工程 **无外部黄金向量文件**：判定所需的预期值全部内联在固件断言里，
来源逐项记录在 `tests/COVERAGE.md` 的「预期数据 / 预期来源」两列。

| 类别 | 预期值 | 来源 |
|---|---|---|
| API 错误码契约 | `0` / `-ENOTSUP` / `-EINVAL` / `-ETIMEDOUT` | 驱动源码分支 + `spi_context.h` 超时语义（行号见 COVERAGE.md） |
| 寄存器字段回读 | `SPI_CR1`/`SPI_CR2` 目标字段 == 写入值 | 器件寄存器定义（`reg_spi_type.h` 结构体/枚举位宽） |
| 分频档编码 | `/8../256 → BR = 2..7` | HAL `ls_hal_spi_i2s.h:182-187`（与驱动 `scaler[]` 同序） |
| 频率约束 | 上限 `clock/8`、下限安全点 `clock>>8` | `spi_ls.c:118-121,123-129`，本板 clock = 75 MHz |
| **回环比对序列**（P4.6，套件 `spi_loopback`） | 由固件自生成的确定性 pattern：`buf[i] = seed + i`，`seed` 按用例取值（见 `src/spi_loopback.c`）；判据为 `master_rx == slave_tx` 且 `slave_rx == master_tx` | 固件内联，无需外部向量文件 |
| **从设备返回值** | `slave_ret == len`（已收帧数） | `spi_ls.c:362-366` + `spi_context.h:174-178` |

**分区的外部条件**：

- 套件 `spi`（原有 18 项）：`外部: 无` —— `spi1`/`spi2` 总线上没有 DT 声明的从设备，
  全部判据来自驱动源码/寄存器语义，不预设任何外部器件值（见 COVERAGE.md「跳过项」）。
- 套件 `spi_loopback`（P4.6 新增 8 项）：**`外部: SPI1↔SPI3 跳线`** —— 对端是**本芯片自己的
  `spi1`**（作从设备），不是外部器件，故预期值仍是固件自生成的 pattern，
  不需要 NIST/官方一类外部黄金向量。跳线细节见 COVERAGE.md「回环可行性结论」。
