/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * QSPI flash 测试共享常量（cpu1 固件 + 用例）。
 *
 * 被测硬件：qspi2@40040000 + 板载 32 MiB Puya NOR（JEDEC ID 85 23 19），
 *           pinmux 板载默认，零外部接线。
 * 被测驱动：zephyr/drivers/spi/qspi_ls.c（linkedsemi,ls-qspi）
 *           + zephyr/drivers/flash/spi_nor_multi_dev.c（jedec,spi-nor 上层）
 */
#ifndef QSPI_FLASH_TEST_H
#define QSPI_FLASH_TEST_H

#include <zephyr/devicetree.h>

/* 设备树节点 */
#define QSPI_FLASH_NODE DT_NODELABEL(flash2)
#define QSPI_CTRL_NODE  DT_NODELABEL(qspi2)

/* 板载 NOR：文档值 32 MiB / JEDEC ID 85 23 19（roadmap §5.1，
 * 来源 = `samples/spi_ls_flash_nor/README.rst` 2026-06-26 的运行日志）。
 *
 * ⚠️ 该组常量与 2026-09-22 回归的串口实测**不一致**（同一份日志里）：
 *   `<inf> spi_nor: flash2@0: 64 MiBy flash` + `jedec id 20 40 20`
 *   （ID 字节 2 = 0x20 ⇒ 512 Mbit；64 MiB）。
 * 两者的 SFDP 版本/PH 数也不同（实测 `SFDP v 1.6 … PH0 rev 1.6: 16 DW / PH1 rev 1.0: 4 DW`，
 * 文档日志为 `SFDP v 1.10 … PH0 rev 1.8: 23 DW / PH1 rev 1.1: 2 DW`）⇒ 器件已换型。
 * ⇒ 依赖"容量/ID"的用例不得写死本组常量：`read/write_past_end_rejected` 已改为
 *   运行期推导器件容量（`qspi_flash_test.c::flash_device_size()`）。
 *   本组常量仅供 `size_is_32mib` / `qspi_flash_id` 套件使用 —— 该套件**当前未注册**
 *   （KNOWN_ISSUES #96），启用前必须先上板复核这两个值。 */
#define QSPI_FLASH_EXPECT_SIZE 0x2000000U
#define QSPI_FLASH_ID0         0x85U
#define QSPI_FLASH_ID1         0x23U
#define QSPI_FLASH_ID2         0x19U

/* 驱动内部常量（抄自驱动源码，见 COVERAGE.md「预期来源」列） */
/* qspi_ls.c:35  STG_DAT_PHASE_BYTES_MAX —— STIG 单次数据相字节上限 */
#define QSPI_STG_DAT_MAX 4096U
/* spi_nor_multi_dev.c  spi_nor_erase() —— 扇区粒度固定 4096 */
#define QSPI_SECTOR_SIZE 4096U

/* 总线时钟：lsqsh.dtsi  qspi2 { clock-frequency = <SDHCI_CLOCK>; }，SDHCI_CLOCK = 200 MHz。
 * 由 qspi_ls.c apply_clk()：
 *   clk_cyc = bus_hz / spi_hz - 1，clk_cyc > 31 → -EINVAL
 *   ⇒ 可接受区间为 spi_hz >= bus_hz / 32 = 6.25 MHz，且 spi_hz <= bus_hz = 200 MHz
 *   ⇒ 6.25 MHz 恰好可接受（clk_cyc == 31），6 MHz 被拒（clk_cyc == 32） */
#define QSPI_BUS_HZ           DT_PROP(QSPI_CTRL_NODE, clock_frequency)
#define QSPI_FREQ_MIN_OK      6250000U  /* bus/32，clk_cyc = 31，边界可接受 */
#define QSPI_FREQ_BELOW_MIN   6000000U  /* bus/33，clk_cyc = 32 > 31 → -EINVAL */
#define QSPI_FREQ_ABOVE_MAX   200000001U /* > bus_hz → -EINVAL */

/* 用例读写窗口：flash 偏移 1 MiB 起（远离 0 偏移，避开任何残留镜像）。
 * 所有写入地址均 4096 对齐（flash_parameters.write_block_size == 4096）。 */
#define QSPI_TEST_BASE 0x00100000U

#endif /* QSPI_FLASH_TEST_H */
