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

/* 内容判据辅助：全等于 val ?（flash_ls_read 恒 return 0，内容断言才是判别力所在） */
static bool buf_is_const(const uint8_t *buf, size_t len, uint8_t val)
{
	for (size_t i = 0; i < len; i++) {
		if (buf[i] != val) {
			return false;
		}
	}
	return true;
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

/* TC-2: erase -> read back all 0xFF（先写入非 0xFF 数据，使"擦除"可证伪）
 *
 * 审计 A 组 P0-5：旧版擦的是 setup 刚擦过的**全 FF** 区（本项跑在第 3 位，
 * 前两项不写 flash）⇒ no-op 擦除照样 PASS；且只断言前 256B，"擦 256B"与
 * "擦 4096B"不可区分。现改为：先写 0x00 512B → 回读确认写入生效 →
 * 擦 4096B → 跨扇区（16×256B）逐字节断言全 0xFF。 */
ZTEST(flash, test_flash_erase_all_ff)
{
	int ret;

	/* 前置：把待擦区首 512B 写成 0x00，制造"非 FF"起点 */
	memset(test_buf, 0x00, sizeof(test_buf));
	ret = flash_area_write(fa, 0, test_buf, sizeof(test_buf));
	zassert_equal(ret, 0, "pre-write 0x00 @0 failed: %d", ret);
	ret = flash_area_write(fa, 256, test_buf, sizeof(test_buf));
	zassert_equal(ret, 0, "pre-write 0x00 @256 failed: %d", ret);

	/* 回读确认写入真的落地：否则若"写"也静默失败，区域仍是 FF，
	 * 后面的擦除断言会退化成恒真（正是旧版的假覆盖形态）。 */
	memset(read_buf, 0xAA, sizeof(read_buf));
	ret = flash_area_read(fa, 0, read_buf, sizeof(read_buf));
	zassert_equal(ret, 0, "pre-read failed: %d", ret);
	zassert_true(buf_is_const(read_buf, sizeof(read_buf), 0x00),
		     "pre-write not effective: 首 256B 回读非全 0x00");

	/* 擦除整个 4K 扇区 */
	ret = flash_area_erase(fa, 0, 4096);
	zassert_equal(ret, 0, "flash_area_erase failed: %d", ret);

	/* 跨扇区回读 4096B（16 × 256B），逐字节必须 0xFF */
	for (off_t off = 0; off < 4096; off += (off_t)sizeof(read_buf)) {
		memset(read_buf, 0x00, sizeof(read_buf));
		ret = flash_area_read(fa, off, read_buf, sizeof(read_buf));
		zassert_equal(ret, 0, "read @%ld failed: %d", (long)off, ret);

		for (size_t i = 0; i < sizeof(read_buf); i++) {
			zassert_equal(read_buf[i], 0xFF,
				"byte @%ld = 0x%02x, expected 0xFF (erase not effective?)",
				(long)(off + (off_t)i), read_buf[i]);
		}
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

/* ==================================================================
 * P1a 新增用例（2026-09-17）
 *
 * 地址约束（用户裁定 S-5）：**boot flash 只有 4 MiB**，所以新增用例的地址
 * 一律以 4 MiB 为界。4 MiB 内的分区表（sbl / sbl_env / a_sec_image /
 * a_app_image）**没有空闲可写区**，唯一可写分区 rwfs 在 15 MiB（超出芯片）
 * ⇒ 新增用例只做**只读 / 元数据**断言，不做写擦。
 * ================================================================== */

#include <zephyr/drivers/flash.h>
#include "ztest_glue.h"   /* ZT_INFO 约定行（§13.4；P1a 新增项使用） */

#define FLASH_BOOT_BYTES (4 * 1024 * 1024)   /* boot flash = 4 MiB（用户裁定 S-5） */

/* TC-8: raw QSPI 读 JEDEC ID 与容量 + SFDP 头（覆盖 flash_ls_read_jedec_id /
 *   flash_ls_sfdp_read，此前零覆盖；需 CONFIG_FLASH_JESD216_API=y）。 */
ZTEST(flash, test_flash_jedec_id)
{
	uint8_t id[3] = { 0 };
	const struct device *dev = fa->fa_dev;

	int ret = flash_read_jedec_id(dev, id);

	zassert_equal(ret, 0, "flash_read_jedec_id ret %d", ret);
	ZT_INFO("test_flash_jedec_id", "id=%02x %02x %02x,cap_byte=0x%02x",
		id[0], id[1], id[2], id[2]);

	/* 非退化：全 0x00 / 全 0xFF 说明 QSPI 通路没真正取到 ID */
	zassert_false(id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00,
		      "JEDEC ID read back as all-00");
	zassert_false(id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF,
		      "JEDEC ID read back as all-FF");

	/* SFDP 头（JESD216）：前 4 字节固定为 ASCII "SFDP" */
	uint8_t sfdp[4] = { 0 };

	ret = flash_sfdp_read(dev, 0, sfdp, sizeof(sfdp));
	zassert_equal(ret, 0, "flash_sfdp_read ret %d", ret);
	ZT_INFO("test_flash_jedec_id", "sfdp=%02x %02x %02x %02x",
		sfdp[0], sfdp[1], sfdp[2], sfdp[3]);
	zassert_mem_equal(sfdp, "SFDP", 4,
			  "SFDP header signature mismatch (JESD216: 'SFDP')");
}

/* TC-9: 器件几何与 4 MiB 芯片边界（边界长度）。
 *   不断言 page size 的具体数值（由 HAL FLASH_SECTOR_SIZE 决定），
 *   而是断言**最后一页正好在 4 MiB 结束**——直接等效于"器件容量 = 4 MiB"。 */
ZTEST(flash, test_flash_device_bounds)
{
	const struct device *dev = fa->fa_dev;
	struct flash_pages_info info = { 0 };
	const struct flash_parameters *params = flash_get_parameters(dev);

	zassert_not_null(params, "flash_get_parameters returned NULL");

	/* 写粒度 = DT write-block-size（flash1: 4） */
	zassert_equal(params->write_block_size, 4,
		      "write_block_size %zu != 4", params->write_block_size);

	/* 首字节所在页 */
	int ret = flash_get_page_info_by_offs(dev, 0, &info);

	zassert_equal(ret, 0, "page_info(0) ret %d", ret);
	zassert_equal(info.start_offset, 0, "first page start_offset %ld != 0",
		      (long)info.start_offset);
	zassert_true(info.size > 0, "page size %u is 0", info.size);

	/* 4 MiB 的最后一个字节仍在器件内，且其所在页正好以 4 MiB 结束 */
	ret = flash_get_page_info_by_offs(dev, (off_t)(FLASH_BOOT_BYTES - 1), &info);
	zassert_equal(ret, 0, "page_info(4MiB-1) ret %d", ret);
	ZT_INFO("test_flash_device_bounds", "page_size=%u,last_page_end=%ld",
		info.size, (long)(info.start_offset + info.size));
	zassert_equal((int64_t)(info.start_offset + info.size),
		      (int64_t)FLASH_BOOT_BYTES,
		      "device does not end at 4 MiB: last page ends at %ld",
		      (long)(info.start_offset + info.size));

	/* 4 MiB 处必须越界（器件只有 4 MiB） */
	ret = flash_get_page_info_by_offs(dev, (off_t)FLASH_BOOT_BYTES, &info);
	zassert_equal(ret, -EINVAL, "page_info(4MiB) ret %d != -EINVAL", ret);
}

/* TC-10: 4 MiB 内分区边界只读校验（分区几何 + 首/末 16B 可读）。
 *   4 MiB 内的分区依次为 sbl / sbl_env / a_sec_image；都是固件镜像区，
 *   本项**只读**，不做任何写/擦。 */
ZTEST(flash, test_flash_partition_bounds)
{
	static const struct {
		uint32_t id;
		uint32_t size;
		const char *name;
	} parts[] = {
		{ FIXED_PARTITION_ID(sbl_partition),       FIXED_PARTITION_SIZE(sbl_partition),       "sbl" },
		{ FIXED_PARTITION_ID(sbl_env_partition),   FIXED_PARTITION_SIZE(sbl_env_partition),   "sbl_env" },
		{ FIXED_PARTITION_ID(a_sec_image_partition), FIXED_PARTITION_SIZE(a_sec_image_partition), "a_sec_image" },
	};

	/* 逐分区 head/tail 采样（每段 16B），收集后统一做判别力断言 */
	uint8_t head[ARRAY_SIZE(parts)][16];
	uint8_t tail[ARRAY_SIZE(parts)][16];
	bool all_zero = true;   /* 6 段 16B 是否全为 0x00（读通路恒 0 的签名） */

	for (size_t i = 0; i < ARRAY_SIZE(parts); i++) {
		const struct flash_area *pfa = NULL;
		int ret = flash_area_open(parts[i].id, &pfa);

		zassert_equal(ret, 0, "flash_area_open(%s) ret %d", parts[i].name, ret);
		zassert_not_null(pfa, "flash_area_open(%s) NULL", parts[i].name);
		zassert_equal(pfa->fa_size, parts[i].size,
			      "%s fa_size %u != DT %u", parts[i].name,
			      (unsigned)pfa->fa_size, (unsigned)parts[i].size);

		/* 全部落在 4 MiB 芯片内（用户裁定：超出部分属陈旧信息） */
		zassert_true(pfa->fa_off + pfa->fa_size <= FLASH_BOOT_BYTES,
			     "%s end 0x%x beyond boot flash 4 MiB",
			     parts[i].name, (unsigned)(pfa->fa_off + pfa->fa_size));

		memset(head[i], 0, sizeof(head[i]));
		memset(tail[i], 0, sizeof(tail[i]));

		ret = flash_area_read(pfa, 0, head[i], sizeof(head[i]));
		zassert_equal(ret, 0, "%s read head ret %d", parts[i].name, ret);

		/* 末 16B：offset = size-16（边界读，不越出分区） */
		ret = flash_area_read(pfa, (off_t)(pfa->fa_size - sizeof(tail[i])),
				      tail[i], sizeof(tail[i]));
		zassert_equal(ret, 0, "%s read tail ret %d", parts[i].name, ret);

		ZT_INFO("test_flash_partition_bounds",
			"%s:off=0x%x,size=%u,head0=%02x,tail0=%02x",
			parts[i].name, (unsigned)pfa->fa_off, (unsigned)pfa->fa_size,
			head[i][0], tail[i][0]);

		flash_area_close(pfa);
	}

	/* ★ 读通路可判别性断言（审计 A 组 P0-5）：
	 *   flash_ls_read() 丢弃 HAL 返回值、恒 return 0（soc_flash_ls.c:597-617），
	 *   旧版 head0/tail0 只经 ZT_INFO 打印、**从不断言** ⇒ QSPI 读通路全坏时
	 *   本项仍 PASS。此处断言 6 段采样不能全为 0x00（"读通路恒 0"的典型签名）。
	 *
	 * 说明（为何不断言"≠全 FF"）：sbl/sbl_env/a_sec_image 的**烧录状态未知**
	 * （板卡走 GDB RAM-load，flash 内容是否已编程未经硬件侧确认，见 S-5）。
	 * 若三个分区恰好是空白/已擦除，非 FF 断言会造出**假 FAIL**，比空壳更糟。
	 * 读通路的非退化已由 `test_flash_jedec_id`（SFDP 头 == "SFDP" + JEDEC ID
	 * 非全 0/全 F）独立覆盖，二者互补。 */
	for (size_t i = 0; i < ARRAY_SIZE(parts); i++) {
		for (size_t b = 0; b < sizeof(head[i]); b++) {
			if (head[i][b] != 0x00 || tail[i][b] != 0x00) {
				all_zero = false;
			}
		}
	}
	zassert_false(all_zero,
		"partition head/tail all 0x00 -> QSPI 读通路恒 0（flash_ls_read 恒 return 0 掩盖）");
}

/* ZTEST_SUITE(name, predicate, setup, before, after, teardown)：
 * setup/teardown 为套件级（整个 suite 各调一次），before/after 为用例级。
 * fa 在 setup 打开、teardown 关闭，二者必须成对落在同一层级，
 * 否则会在用例间出现 fa==NULL 的空指针解引用。 */
ZTEST_SUITE(flash, NULL, flash_test_setup, NULL, NULL, flash_test_teardown);
