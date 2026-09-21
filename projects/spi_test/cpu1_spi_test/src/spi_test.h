/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spi_test 公共定义（单核 cpu1 安全域）。
 * 覆盖矩阵见 tests/COVERAGE.md。
 */

#ifndef SPI_TEST_H_
#define SPI_TEST_H_

#include <zephyr/device.h>
#include <stdint.h>

/*
 * 被测实例数。
 *
 * ⚠️ 只有 spi1 / spi2 的 compatible 是 `linkedsemi,ls-spi`（= 被测驱动
 *    zephyr/drivers/spi/spi_ls.c）。spi3 / spi4 是 `snps,designware-spi`
 *    （spi_dw.c），**不归本工程**，见 tests/COVERAGE.md「实例范围」。
 */
#define SPI_INST_NUM 2

/* idx 0..1 → spi1/spi2；越界返回 NULL */
const struct device *spi_dev(int idx);

/* idx 0..1 → spiN 寄存器基址（reg_spi_t）；越界返回 0 */
uintptr_t spi_reg_base(int idx);

#endif /* SPI_TEST_H_ */
