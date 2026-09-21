/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spif_test 公共定义（单核 cpu1）。
 * 覆盖矩阵见 tests/COVERAGE.md。
 */

#ifndef SPIF_TEST_H_
#define SPIF_TEST_H_

#include <zephyr/device.h>
#include <stdint.h>

/* 被测实例数（spif1..spif4，overlay 全使能） */
#define SPIF_INST_NUM 4

/* idx 0..3 → spif1..spif4（DT index 属性同序）；越界返回 NULL */
const struct device *spif_dev(int idx);

/* idx 0..3 → spifN 寄存器基址；越界返回 0 */
uintptr_t spif_reg_base(int idx);

#endif /* SPIF_TEST_H_ */
