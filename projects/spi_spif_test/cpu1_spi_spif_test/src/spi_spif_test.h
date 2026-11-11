/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spi_spif_test 公共定义（单核 cpu1）。
 *
 * 合并工程实例范围：
 *   - spi1 / spi2：`linkedsemi,ls-spi`（zephyr/drivers/spi/spi_ls.c）
 *   - spif1..spif4：`linkedsemi,spi-filter`（pfr_tpm spi_filter.c）
 * 覆盖矩阵见 tests/COVERAGE.md。
 */

#ifndef SPI_SPIF_TEST_H_
#define SPI_SPIF_TEST_H_

#include <zephyr/device.h>
#include <spi_filter.h>
#include <stdint.h>

/* spi_ls 被测实例数（spi1/spi2） */
#define SPI_INST_NUM 2

/* SPIF 被测实例数（spif1..spif4，overlay 全使能） */
#define SPIF_INST_NUM 4

/* idx 0..1 → spi1/spi2；越界返回 NULL */
const struct device *spi_dev(int idx);

/* idx 0..1 → spiN 寄存器基址（reg_spi_t）；越界返回 0 */
uintptr_t spi_reg_base(int idx);

/* idx 0..3 → spif1..spif4（DT index 属性同序）；越界返回 NULL */
const struct device *spif_dev(int idx);

/* idx 0..3 → spifN 寄存器基址；越界返回 0 */
uintptr_t spif_reg_base(int idx);

#endif /* SPI_SPIF_TEST_H_ */
