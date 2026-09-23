/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * QSPI flash 测试共享常量（cpu1 固件 + 用例）。
 *
 * 被测硬件：qspi2@40040000 + 板载 NOR（容量/JEDEC 运行时探测），
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

/* 板载 NOR：容量由 SFDP/驱动运行时探测，用例勿写死；
 * JEDEC ID 也不写死厂商号（见 qspi_flash_id.jedec_id_*）。 */

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
