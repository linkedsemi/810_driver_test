# spi_spif_test 预期数据

本工程 **无外部黄金向量文件**：判定所需的预期值全部内联在固件断言里，
来源逐项记录在 `tests/COVERAGE.md` 的「预期数据 / 预期来源」两列。

| 类别 | 预期值 | 来源 |
|---|---|---|
| `spi` 套件 API 错误码 | `0` / `-ENOTSUP` / `-EINVAL` / `-ETIMEDOUT` | `spi_ls.c` + `spi_context.h`（行号见 COVERAGE.md） |
| `spi` 套件寄存器回读 | `SPI_CR1`/`SPI_CR2` 目标字段 == 写入值 | `reg_spi_type.h` 位域 |
| `spi` 套件分频档 | `/8../256 → BR = 2..7` | HAL `ls_hal_spi_i2s.h:182-187`（与驱动 `scaler[]` 同序） |
| `spi_flash` 套件 | 固件自生成 pattern：`buf[i] = seed + 3*i + (i >> 5)`；写后读回逐字节比对；擦除后回读全 0xFF | 固件内联，无需外部向量文件 |
| `spif` 套件 API 错误码 | `0` / `-EINVAL` / `-ENOSR` / `-EBUSY` | `spi_filter.c` 分支（行号见 COVERAGE.md） |
| `spif` 套件寄存器回读 | 按 `reg_spi_filter.h` 位域声明取字段 | 寄存器定义（`spif_cfg_t` / `spif_cmd_t` 等） |
| `spif_switch_to_mode` 状态 | `spif_single_passthrough` / `spif_multi_passthrough` / `spif_no_passthrough` / `spif_pinctrl_master` | `spi_filter.c` 的 `spif_pinctrl_state_get()` + 模块 dtsi 四态 pinctrl |

**分区的外部条件**：

- 套件 `spi`（18 项）：`外部: 无` —— `spi1`/`spi2` 总线上没有 DT 声明的从设备，
  全部判据来自驱动源码/寄存器语义，不预设任何外部器件值。
- 套件 `spi_flash`（11 项）：`外部: 无` —— 板载 spif3 master -> spi4 -> flash4_0。
- 套件 `spif`（25 项）：寄存器级用例与 master 直驱板载 flash 零外部条件；
  `loopback_*`（6 项）需 `spif1`(master) ↔ `spif3`(filter in) 跳线
  （与 `projects/spif_test/tests/COVERAGE.md`「参考资产出处」记录的接线相同）。
