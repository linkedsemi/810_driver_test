/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ztest_glue.h"

#include <zephyr/ztest.h>
#include <zephyr/ztest_test.h>
#include "test_results.h"

void zt_init(const char *core)
{
	printk("ZT_BOOT %s\n", core);
}

void zt_run_all(void)
{
	/* 槽位全量置 NOT_RUN；GDB 兜底（dump_results.py）按注册顺序读 */
	tr_init(TR_MAX_COUNT);

	ztest_run_all(NULL, false, 1, 1);

	/* 套件结束后按 ztest 原生 stats 批量记账。索引 = 测试在链接段
	 * 中的注册顺序（构建内稳定）；zassert 失败文本由 zassert 自身
	 * printk 输出，此处只记统计性 reason（串口可用时以串口为准）。 */
	for (struct ztest_unit_test *t = _ztest_unit_test_list_start;
	     t < _ztest_unit_test_list_end; t++) {
		int idx = (int)(t - _ztest_unit_test_list_start);

		if (idx >= TR_MAX_COUNT) {
			continue;
		}
		if (t->stats->fail_count > 0) {
			tr_record(idx, TR_FAIL, "ztest fail_count=%u",
				  t->stats->fail_count);
		} else if (t->stats->pass_count > 0) {
			tr_record(idx, TR_PASS, "");
		} else if (t->stats->skip_count > 0) {
			tr_record(idx, TR_NOT_RUN, "skipped");
		} else {
			tr_record(idx, TR_NOT_RUN, "not run");
		}
	}
}
