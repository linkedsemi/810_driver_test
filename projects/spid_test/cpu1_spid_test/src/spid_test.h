/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spid_test 公共定义（单核 cpu1）。
 * 覆盖矩阵见 tests/COVERAGE.md。
 */

#ifndef SPID_TEST_H_
#define SPID_TEST_H_

#include <zephyr/device.h>
#include <stdint.h>

/* TPM SPI slave 实例数（tpm_spis1 / tpm_spis2） */
#define SPID_INST_NUM 2

/* idx 0 → tpm_spis1（0x4000c000）；idx 1 → tpm_spis2（0x4000e000） */
const struct device *spid_dev(int idx);

/* idx → tpm_spisN 寄存器基址（取自 DT，**不硬编码**）；越界返回 0 */
uintptr_t spid_reg_base(int idx);

#endif /* SPID_TEST_H_ */
