/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ztest 原生框架公共 glue（810_driver_test 公共模块）。
 * 测试固件回归纯 ztest 后由本模块提供：
 *   1. zt_init(core)：test_main 第一行调用，printk "ZT_BOOT <core>"（§6.2 BOOT 判据）
 *   2. zt_run_all()：ztest_run_all 后按 test->stats 批量记账到 test_results[]（GDB 兜底数据源）
 *   3. ZT_INFO：INFO 约定行（§13.4 数值上报，报告「实际」列）
 */
#ifndef ZTEST_GLUE_H
#define ZTEST_GLUE_H

#include <zephyr/sys/printk.h>

/* test_main 第一行调用；core 为 "CTRL" 或 "TGT" */
void zt_init(const char *core);

/* test_main 末尾调用；返回后套件已结束（与 ztest_run_all 相同语义） */
void zt_run_all(void);

/* INFO 约定行：name 必须与 ZTEST(suite, name) 注册名一致 */
#define ZT_INFO(name, fmt, ...) \
	printk("INFO %s: " fmt "\n", name, ##__VA_ARGS__)

#endif /* ZTEST_GLUE_H */
