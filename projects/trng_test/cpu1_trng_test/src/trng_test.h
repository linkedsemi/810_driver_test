/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TRNG_TEST_H
#define TRNG_TEST_H

/* 被测 TRNG 设备（app.overlay 指定 chosen zephyr,entropy，此处直接取 node） */
extern const struct device *trng_dev;

/* 跑完全部 ZTEST 套件 + 汇总 + DONE token（test_main 入口调用） */
void trng_test_run(void);

#endif /* TRNG_TEST_H */
