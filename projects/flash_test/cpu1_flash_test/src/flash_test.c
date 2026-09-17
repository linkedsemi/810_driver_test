/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash test cases (rwfs2 partition via flash_area API)
 *
 * 安全约束：只擦写 rwfs2 分区（label = "rwfs2"），
 * 绝不碰 sbl/sec/app 固件区。
 *
 * 注意：soc-nv-flash compatible 无对应驱动，改用 flash_area API。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <string.h>

/* 通过 partition label 获取 flash_area */
/* 注意：FIXED_PARTITION_ID 需要裸标识符，不能传字符串宏 */
static const struct flash_area *fa;
static uint8_t test_buf[256];
static uint8_t read_buf[256];

static void *flash_test_setup(void)
{
	/* 通过 partition label 获取 flash_area（勿硬编码 index，分区表会变）。
	 * rwfs_partition 是 qspi flash 上的可写分区（ID=5，0xf00000 起）。 */
	int ret = flash_area_open(FIXED_PARTITION_ID(rwfs_partition), &fa);
	zassert_equal(ret, 0, "flash_area_open(rwfs, id=%d) failed: %d",
		      FIXED_PARTITION_ID(rwfs_partition), ret);
	zassert_not_null(fa, "flash_area_open returned NULL");

	/* 擦除第一 page 作为测试起点 */
	flash_area_erase(fa, 0, 4096);
	return NULL;
}

static void flash_test_teardown(void *data)
{
	ARG_UNUSED(data);
	if (fa) {
		/* 清理：擦除测试区域 */
		flash_area_erase(fa, 0, 4096);
		flash_area_close(fa);
		fa = NULL;
	}
}

/* TC-1: write + read back + memcmp */
ZTEST(flash, test_flash_read_write_basic)
{
	for (int i = 0; i < 256; i++) {
		test_buf[i] = (uint8_t)i;
	}

	int ret = flash_area_write(fa, 0, test_buf, 256);
	zassert_equal(ret, 0, "flash_area_write failed: %d", ret);

	memset(read_buf, 0, sizeof(read_buf));
	ret = flash_area_read(fa, 0, read_buf, 256);
	zassert_equal(ret, 0, "flash_area_read failed: %d", ret);

	zassert_mem_equal(test_buf, read_buf, 256, "data mismatch");
}

/* TC-2: erase -> read back all 0xFF */
ZTEST(flash, test_flash_erase_all_ff)
{
	int ret = flash_area_erase(fa, 0, 4096);
	zassert_equal(ret, 0, "flash_area_erase failed: %d", ret);

	memset(read_buf, 0, sizeof(read_buf));
	ret = flash_area_read(fa, 0, read_buf, 256);
	zassert_equal(ret, 0, "flash_area_read failed: %d", ret);

	for (int i = 0; i < 256; i++) {
		zassert_equal(read_buf[i], 0xFF,
			"byte %d = 0x%02x, expected 0xFF", i, read_buf[i]);
	}
}

/* TC-3: five different patterns */
ZTEST(flash, test_flash_write_read_multiple_patterns)
{
	uint8_t patterns[5][256];

	for (int i = 0; i < 256; i++) { patterns[0][i] = (uint8_t)i; }
	memset(patterns[1], 0xAA, 256);
	memset(patterns[2], 0x55, 256);
	memset(patterns[3], 0x00, 256);
	memset(patterns[4], 0xFF, 256);

	for (int p = 0; p < 5; p++) {
		flash_area_erase(fa, 0, 4096);
		int ret = flash_area_write(fa, 0, patterns[p], 256);
		zassert_equal(ret, 0, "write pattern %d failed: %d", p, ret);

		memset(read_buf, 0, sizeof(read_buf));
		ret = flash_area_read(fa, 0, read_buf, 256);
		zassert_equal(ret, 0, "read pattern %d failed: %d", p, ret);

		zassert_mem_equal(patterns[p], read_buf, 256, "pattern %d mismatch", p);
	}
}

/* TC-4: cross-page-boundary write */
ZTEST(flash, test_flash_page_boundary)
{
	uint32_t addr = 4096 - 128;  /* page_size - 128 */
	uint8_t boundary_data[256];

	for (int i = 0; i < 256; i++) {
		boundary_data[i] = (uint8_t)(0xB0 + i);
	}

	flash_area_erase(fa, 0, 4096 * 2);

	int ret = flash_area_write(fa, addr, boundary_data, 256);
	zassert_equal(ret, 0, "boundary write failed: %d", ret);

	memset(read_buf, 0, sizeof(read_buf));
	ret = flash_area_read(fa, addr, read_buf, 256);
	zassert_equal(ret, 0, "boundary read failed: %d", ret);

	zassert_mem_equal(boundary_data, read_buf, 256, "boundary data mismatch");
}

/* TC-5: non-4K-aligned erase -> -EINVAL */
ZTEST(flash, test_flash_erase_size_boundary)
{
	int ret = flash_area_erase(fa, 0, 100);
	zassert_not_equal(ret, 0, "erase 100B should fail but returned %d", ret);
}

/* TC-6: flash_area_size returns valid value */
ZTEST(flash, test_flash_area_info)
{
	uint32_t size = fa->fa_size;
	zassert_true(size > 0, "flash_area size = %d", size);
	zassert_true(size >= 0x80000, "flash_area size %d < 512K", size);
}

/* TC-7: write offset beyond area -> error */
ZTEST(flash, test_flash_write_out_of_bounds)
{
	uint32_t size = fa->fa_size;
	int ret = flash_area_write(fa, size, test_buf, 256);
	zassert_not_equal(ret, 0, "write beyond area should fail");
}

/* ZTEST_SUITE(name, predicate, setup, before, after, teardown)：
 * setup/teardown 为套件级（整个 suite 各调一次），before/after 为用例级。
 * fa 在 setup 打开、teardown 关闭，二者必须成对落在同一层级，
 * 否则会在用例间出现 fa==NULL 的空指针解引用。 */
ZTEST_SUITE(flash, NULL, flash_test_setup, NULL, NULL, flash_test_teardown);
