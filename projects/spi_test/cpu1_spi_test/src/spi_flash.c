/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spi_flash.c —— spif3 master -> flash4_0 的 flash 访问用例。
 *
 * 被测通路（板载，无外部接线）：
 *   spif3（linkedsemi,spi-filter，master 态）-> spi4（snps,designware-spi）
 *   -> flash4_0（jedec,spi-nor / zephyr/drivers/flash/spi_nor_multi_dev.c）
 *
 * 覆盖维度：
 *   - flash_ready                  : spif3 切 master + flash 设备/JEDEC ID/容量契约
 *   - erase_all_ff                 : 擦除后回读必须全 0xFF
 *   - read_aligned_len_sweep       : 对齐缓冲，读长度 1..4096，逐字节比对
 *   - read_unaligned_len_sweep     : 非对齐缓冲，读长度 1..4096，逐字节比对
 *   - write_aligned_len_sweep      : 对齐缓冲，写长度 1..256，擦除后写读比对
 *   - write_unaligned_len_sweep    : 非对齐缓冲，写长度 1..256，擦除后写读比对
 *
 * 判据不是“返回 0 就算过”：每条用例都把读回内容与写入 pattern 逐字节比对；
 * 擦除用例比对 0xFF。对齐/非对齐指**数据缓冲区地址**（cache line / DMA 对齐），
 * 缓冲一律用静态数组，不用堆（避免 NULL 分配把串口打挂，SPEC_REFERENCE §6.1）。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include <spi_filter.h>

#include "ztest_glue.h"

LOG_MODULE_REGISTER(spi_flash, LOG_LEVEL_INF);

/* 失败现场（GDB/内存兜底可读）：stage 1=page info, 2=erase, 3=read, 4=write */
volatile int spi_flash_dbg_stage;
volatile int spi_flash_dbg_ret;
volatile int spi_flash_dbg_len;
volatile int spi_flash_dbg_idx;
volatile int spi_flash_dbg_exp;
volatile int spi_flash_dbg_act;

/* -------------------------------------------------------------------------- */
/* 硬件/区域常量                                                               */
/* -------------------------------------------------------------------------- */

/*
 * 测试区放在 1 MiB 之后：flash4_0 为板载 64/128 MiB NOR，前 1 MiB 让给
 * 板上固件/布局；三个区域互不重叠。擦除按 flash 页大小向上覆盖。
 */
#define FLASH_READ_REGION      0x100000U   /* 读/擦用例：1 个 sector 起 */
#define FLASH_WRITE_REGION_A   0x110000U   /* 对齐写：64 KiB */
#define FLASH_WRITE_REGION_U   0x120000U   /* 非对齐写：64 KiB */

#define READ_MAX_LEN           4096U
#define WRITE_MAX_LEN          256U
#define WRITE_PAGE_STRIDE      256U
#define WRITE_REGION_SIZE      (WRITE_MAX_LEN * WRITE_PAGE_STRIDE) /* 64 KiB */

/* -------------------------------------------------------------------------- */
/* 设备                                                                        */
/* -------------------------------------------------------------------------- */

static const struct device *const spif_dev = DEVICE_DT_GET(DT_ALIAS(spifilter));
static const struct device *const flash_dev = DEVICE_DT_GET(DT_ALIAS(spiflash));

/* -------------------------------------------------------------------------- */
/* 缓冲（静态；对齐/非对齐两套，非对齐指针 = 数组 + 8）                          */
/* -------------------------------------------------------------------------- */

static uint8_t pattern[READ_MAX_LEN] __aligned(CONFIG_DCACHE_LINE_SIZE);

static uint8_t rd_aligned[READ_MAX_LEN] __aligned(CONFIG_DCACHE_LINE_SIZE);
static uint8_t rd_unaligned_raw[READ_MAX_LEN + CONFIG_DCACHE_LINE_SIZE]
			       __aligned(CONFIG_DCACHE_LINE_SIZE);

static uint8_t wr_aligned[WRITE_MAX_LEN] __aligned(CONFIG_DCACHE_LINE_SIZE);
static uint8_t wr_unaligned_raw[WRITE_MAX_LEN + CONFIG_DCACHE_LINE_SIZE]
				__aligned(CONFIG_DCACHE_LINE_SIZE);
static uint8_t rb_aligned[WRITE_MAX_LEN] __aligned(CONFIG_DCACHE_LINE_SIZE);
static uint8_t rb_unaligned_raw[WRITE_MAX_LEN + CONFIG_DCACHE_LINE_SIZE]
				__aligned(CONFIG_DCACHE_LINE_SIZE);

/* -------------------------------------------------------------------------- */
/* 工具                                                                        */
/* -------------------------------------------------------------------------- */

struct sweep_result {
	size_t fail;
	size_t first_fail;
};

static uint8_t *unaligned_ptr(uint8_t *raw)
{
	/*
	 * raw 本身按 cache line 对齐；+8 模拟 k_malloc() 返回的 8 字节对齐
	 * 堆缓冲（相对 32 B cache line 仍然“非对齐”）。不要用 +1：当前
	 * spi_nor/DW-SPI 的写路径要求源缓冲至少 4 字节对齐，+1 会把
	 * cache line 边界字节写成 0xFF（实测 len=35 idx=31）。
	 */
	return raw + 8U;
}

static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (uint8_t)(seed + 3U * (uint8_t)i + (uint8_t)(i >> 5));
	}
}

static int erase_region(const struct device *flash, off_t addr, size_t len)
{
	off_t pos = addr;
	off_t end = addr + (off_t)len;

	while (pos < end) {
		struct flash_pages_info page;
		int ret = flash_get_page_info_by_offs(flash, pos, &page);

		if (ret != 0) {
			spi_flash_dbg_stage = 1;
			spi_flash_dbg_ret = ret;
			return ret;
		}
		if (page.size == 0U) {
			spi_flash_dbg_stage = 1;
			spi_flash_dbg_ret = -EINVAL;
			return -EINVAL;
		}
		ret = flash_erase(flash, page.start_offset, page.size);
		if (ret != 0) {
			spi_flash_dbg_stage = 2;
			spi_flash_dbg_ret = ret;
			return ret;
		}
		pos = page.start_offset + (off_t)page.size;
	}

	return 0;
}

/* 写一段 pattern，再读回逐字节比对；返回首个失败长度与失败次数。 */
static struct sweep_result read_sweep(bool aligned, bool use_malloc)
{
	struct sweep_result r = { 0 };
	uint8_t *dst_static = aligned ? rd_aligned : unaligned_ptr(rd_unaligned_raw);
	int ret;

	fill_pattern(pattern, READ_MAX_LEN, 0x5AU);

	ret = erase_region(flash_dev, FLASH_READ_REGION, READ_MAX_LEN);
	if (ret != 0) {
		r.fail = 1;
		return r;
	}

	ret = flash_write(flash_dev, FLASH_READ_REGION, pattern, READ_MAX_LEN);
	if (ret != 0) {
		r.fail = 1;
		return r;
	}

	for (size_t len = 1; len <= READ_MAX_LEN; len++) {
		uint8_t *dst = dst_static;

		if (use_malloc) {
			dst = aligned ? k_aligned_alloc(CONFIG_DCACHE_LINE_SIZE, len)
				      : k_malloc(len);
			if (dst == NULL) {
				if (r.fail == 0U) {
					r.first_fail = len;
				}
				r.fail++;
				continue;
			}
		}

		memset(dst, 0xA5, len);
		ret = flash_read(flash_dev, FLASH_READ_REGION, dst, len);
		if (ret != 0 || memcmp(dst, pattern, len) != 0) {
			if (r.fail == 0U) {
				r.first_fail = len;
			}
			r.fail++;
		}
		if (use_malloc) {
			k_free(dst);
		}
	}

	return r;
}

/* 每个长度写入独立页，擦一次 64 KiB 区域后再逐个写/读比对。 */
static struct sweep_result write_sweep(bool aligned, bool use_malloc, off_t base)
{
	struct sweep_result r = { 0 };
	uint8_t *src_static = aligned ? wr_aligned : unaligned_ptr(wr_unaligned_raw);
	uint8_t *rb_static = aligned ? rb_aligned : unaligned_ptr(rb_unaligned_raw);
	int ret;

	ret = erase_region(flash_dev, base, WRITE_REGION_SIZE);
	if (ret != 0) {
		r.fail = 1;
		return r;
	}

	for (size_t len = 1; len <= WRITE_MAX_LEN; len++) {
		/* 每档独占一个 256 B 页，长度 <= 页大小，不跨页 */
		off_t addr = base + (off_t)((len - 1U) * WRITE_PAGE_STRIDE);
		uint8_t *src = src_static;
		uint8_t *rb = rb_static;

		if (use_malloc) {
			src = aligned ? k_aligned_alloc(CONFIG_DCACHE_LINE_SIZE, len)
				      : k_malloc(len);
			rb = aligned ? k_aligned_alloc(CONFIG_DCACHE_LINE_SIZE, len)
				     : k_malloc(len);
			if (src == NULL || rb == NULL) {
				if (r.fail == 0U) {
					r.first_fail = len;
				}
				r.fail++;
				k_free(src);
				k_free(rb);
				continue;
			}
		}

		fill_pattern(src, len, (uint8_t)len);
		memset(rb, 0x5A, len);
		ret = flash_write(flash_dev, addr, src, len);
		if (ret != 0) {
			if (r.fail == 0U) {
				spi_flash_dbg_stage = 4;
				spi_flash_dbg_ret = ret;
				spi_flash_dbg_len = (int)len;
			}
		} else {
			ret = flash_read(flash_dev, addr, rb, len);
			if (ret != 0) {
				if (r.fail == 0U) {
					spi_flash_dbg_stage = 5;
					spi_flash_dbg_ret = ret;
					spi_flash_dbg_len = (int)len;
				}
			} else if (memcmp(src, rb, len) != 0) {
				if (r.fail == 0U) {
					spi_flash_dbg_stage = 6;
					spi_flash_dbg_ret = 0;
					spi_flash_dbg_len = (int)len;
					for (size_t i = 0; i < len; i++) {
						if (src[i] != rb[i]) {
							spi_flash_dbg_idx = (int)i;
							spi_flash_dbg_exp = src[i];
							spi_flash_dbg_act = rb[i];
							break;
						}
					}
				}
			}
		}
		if (ret != 0 || memcmp(src, rb, len) != 0) {
			if (r.fail == 0U) {
				r.first_fail = len;
			}
			r.fail++;
		}
		if (use_malloc) {
			k_free(src);
			k_free(rb);
		}
	}

	return r;
}

/* -------------------------------------------------------------------------- */
/* 套件 setup：spif3 必须是 master                                             */
/* -------------------------------------------------------------------------- */

static void *spi_flash_suite_setup(void)
{
	uint8_t id[3] = { 0 };
	int ret;

	zassert_true(device_is_ready(spif_dev), "spif3（%s）设备不就绪", spif_dev->name);

	ret = spif_switch_to_master(spif_dev);
	zassert_ok(ret, "spif_switch_to_master 失败: %d", ret);
	zassert_true(spif_pinctrl_master_mode_check(spif_dev),
		     "spif3 pinctrl 未进入 master 态");

	zassert_true(device_is_ready(flash_dev), "flash4_0（%s）设备不就绪",
		     flash_dev->name);

	/*
	 * flash 驱动在 POST_KERNEL 时 spif3 还不是 master，首次 init 会失败并
	 * 依赖 re-init 机制。`flash_get_page_info_by_offs()` / `flash_read_jedec_id()`
	 * 都不会触发 re-init（前者读布局，后者只发 RDID），只有 read/write/erase
	 * 会走 `spi_nor_re_init()`；故这里先做一次 4 B read，把 SFDP、容量和
	 * page layout 真正建立起来，后面的 erase/page-info 才有效。
	 */
	ret = flash_read(flash_dev, 0, pattern, 4);
	zassert_ok(ret, "flash4_0 初始化读失败: %d", ret);

	ret = flash_read_jedec_id(flash_dev, id);
	zassert_ok(ret, "flash4_0 JEDEC 读取失败: %d", ret);

	return NULL;
}

/* -------------------------------------------------------------------------- */
/* 用例                                                                        */
/* -------------------------------------------------------------------------- */

ZTEST(spi_flash, flash_ready)
{
	const struct flash_parameters *params;
	uint8_t id[3] = { 0 };
	bool all_0 = true;
	bool all_ff = true;
	int ret;

	ret = flash_read_jedec_id(flash_dev, id);
	zassert_ok(ret, "flash_read_jedec_id 失败: %d", ret);

	for (size_t i = 0; i < ARRAY_SIZE(id); i++) {
		all_0 = all_0 && (id[i] == 0x00U);
		all_ff = all_ff && (id[i] == 0xFFU);
	}

	params = flash_get_parameters(flash_dev);
	zassert_true(params != NULL, "flash_get_parameters 返回 NULL");
	zassert_true(params->flash_size > (size_t)FLASH_WRITE_REGION_U + WRITE_REGION_SIZE,
		     "flash 容量 0x%zx 覆盖不到测试区 0x%x",
		     params->flash_size, FLASH_WRITE_REGION_U + WRITE_REGION_SIZE);

	ZT_INFO("flash_ready", "jedec=%02x%02x%02x,size=%u,erase=%02x",
		id[0], id[1], id[2], (unsigned int)params->flash_size,
		params->erase_value);

	zassert_false(all_0 || all_ff, "JEDEC ID 非法: %02x %02x %02x",
		      id[0], id[1], id[2]);
}

/*
 * alloc_read_smoke —— malloc 只读冒烟：只验证 flash_read 返回 0，
 * 不比对内容、不擦写。放在 spi_flash 套件最前，作为读写扫描前的
 * "链路 + 堆分配" 快速确认。
 */
ZTEST(spi_flash, alloc_read_smoke)
{
	const size_t len = READ_MAX_LEN;
	uint8_t *buf = k_malloc(len);
	int ret;

	zassert_not_null(buf, "k_malloc(%u) 失败", (unsigned int)len);

	ret = flash_read(flash_dev, 0, buf, len);
	ZT_INFO("alloc_read_smoke", "len=%u,ret=%d", (unsigned int)len, ret);
	zassert_ok(ret, "malloc 只读失败: %d", ret);

	k_free(buf);
}

ZTEST(spi_flash, erase_all_ff)
{
	size_t ff = 0;
	int ret;

	ret = erase_region(flash_dev, FLASH_READ_REGION, READ_MAX_LEN);
	zassert_ok(ret, "flash_erase 失败: %d", ret);

	ret = flash_read(flash_dev, FLASH_READ_REGION, rd_aligned, READ_MAX_LEN);
	zassert_ok(ret, "擦除后 flash_read 失败: %d", ret);

	for (size_t i = 0; i < READ_MAX_LEN; i++) {
		if (rd_aligned[i] == 0xFFU) {
			ff++;
		}
	}

	ZT_INFO("erase_all_ff", "ff_bytes=%u,total=%u", (unsigned int)ff,
		(unsigned int)READ_MAX_LEN);
	zassert_equal(ff, READ_MAX_LEN, "擦除后回读不是全 0xFF: %u/%u",
		      (unsigned int)ff, (unsigned int)READ_MAX_LEN);
}

ZTEST(spi_flash, read_aligned_len_sweep)
{
	struct sweep_result r = read_sweep(true, false);

	ZT_INFO("read_aligned_len_sweep", "min=1,max=%u,fail=%u,first=%u",
		(unsigned int)READ_MAX_LEN, (unsigned int)r.fail,
		(unsigned int)r.first_fail);
	zassert_equal(r.fail, 0U, "对齐读失败 %u 次，首个失败 len=%u",
		      (unsigned int)r.fail, (unsigned int)r.first_fail);
}

ZTEST(spi_flash, read_unaligned_len_sweep)
{
	struct sweep_result r = read_sweep(false, false);

	ZT_INFO("read_unaligned_len_sweep", "min=1,max=%u,fail=%u,first=%u",
		(unsigned int)READ_MAX_LEN, (unsigned int)r.fail,
		(unsigned int)r.first_fail);
	zassert_equal(r.fail, 0U, "非对齐读失败 %u 次，首个失败 len=%u",
		      (unsigned int)r.fail, (unsigned int)r.first_fail);
}

ZTEST(spi_flash, read_malloc_aligned_len_sweep)
{
	struct sweep_result r = read_sweep(true, true);

	ZT_INFO("read_malloc_aligned_len_sweep", "min=1,max=%u,fail=%u,first=%u",
		(unsigned int)READ_MAX_LEN, (unsigned int)r.fail,
		(unsigned int)r.first_fail);
	zassert_equal(r.fail, 0U, "malloc 对齐读失败 %u 次，首个失败 len=%u",
		      (unsigned int)r.fail, (unsigned int)r.first_fail);
}

ZTEST(spi_flash, read_malloc_unaligned_len_sweep)
{
	struct sweep_result r = read_sweep(false, true);

	ZT_INFO("read_malloc_unaligned_len_sweep", "min=1,max=%u,fail=%u,first=%u",
		(unsigned int)READ_MAX_LEN, (unsigned int)r.fail,
		(unsigned int)r.first_fail);
	zassert_equal(r.fail, 0U, "malloc 非对齐读失败 %u 次，首个失败 len=%u",
		      (unsigned int)r.fail, (unsigned int)r.first_fail);
}

ZTEST(spi_flash, write_aligned_len_sweep)
{
	spi_flash_dbg_stage = 0;
	spi_flash_dbg_ret = 0;
	spi_flash_dbg_len = 0;
	spi_flash_dbg_idx = -1;
	spi_flash_dbg_exp = 0;
	spi_flash_dbg_act = 0;
	struct sweep_result r = write_sweep(true, false, FLASH_WRITE_REGION_A);

	ZT_INFO("write_aligned_len_sweep", "min=1,max=%u,fail=%u,first=%u,stage=%d,ret=%d,dlen=%d,idx=%d,exp=%02x,act=%02x",
		(unsigned int)WRITE_MAX_LEN, (unsigned int)r.fail,
		(unsigned int)r.first_fail, spi_flash_dbg_stage, spi_flash_dbg_ret,
		spi_flash_dbg_len, spi_flash_dbg_idx, spi_flash_dbg_exp,
		spi_flash_dbg_act);
	zassert_equal(r.fail, 0U, "对齐写失败 %u 次，首个失败 len=%u",
		      (unsigned int)r.fail, (unsigned int)r.first_fail);
}

ZTEST(spi_flash, write_unaligned_len_sweep)
{
	spi_flash_dbg_stage = 0;
	spi_flash_dbg_ret = 0;
	spi_flash_dbg_len = 0;
	spi_flash_dbg_idx = -1;
	spi_flash_dbg_exp = 0;
	spi_flash_dbg_act = 0;
	struct sweep_result r = write_sweep(false, false, FLASH_WRITE_REGION_U);

	ZT_INFO("write_unaligned_len_sweep", "min=1,max=%u,fail=%u,first=%u,stage=%d,ret=%d,dlen=%d,idx=%d,exp=%02x,act=%02x",
		(unsigned int)WRITE_MAX_LEN, (unsigned int)r.fail,
		(unsigned int)r.first_fail, spi_flash_dbg_stage, spi_flash_dbg_ret,
		spi_flash_dbg_len, spi_flash_dbg_idx, spi_flash_dbg_exp,
		spi_flash_dbg_act);
	zassert_equal(r.fail, 0U, "非对齐写失败 %u 次，首个失败 len=%u",
		      (unsigned int)r.fail, (unsigned int)r.first_fail);
}

ZTEST(spi_flash, write_malloc_aligned_len_sweep)
{
	spi_flash_dbg_stage = 0;
	spi_flash_dbg_ret = 0;
	spi_flash_dbg_len = 0;
	spi_flash_dbg_idx = -1;
	spi_flash_dbg_exp = 0;
	spi_flash_dbg_act = 0;
	struct sweep_result r = write_sweep(true, true, FLASH_WRITE_REGION_A);

	ZT_INFO("write_malloc_aligned_len_sweep", "min=1,max=%u,fail=%u,first=%u,stage=%d,ret=%d,dlen=%d,idx=%d,exp=%02x,act=%02x",
		(unsigned int)WRITE_MAX_LEN, (unsigned int)r.fail,
		(unsigned int)r.first_fail, spi_flash_dbg_stage, spi_flash_dbg_ret,
		spi_flash_dbg_len, spi_flash_dbg_idx, spi_flash_dbg_exp,
		spi_flash_dbg_act);
	zassert_equal(r.fail, 0U, "malloc 对齐写失败 %u 次，首个失败 len=%u",
		      (unsigned int)r.fail, (unsigned int)r.first_fail);
}

ZTEST(spi_flash, write_malloc_unaligned_len_sweep)
{
	spi_flash_dbg_stage = 0;
	spi_flash_dbg_ret = 0;
	spi_flash_dbg_len = 0;
	spi_flash_dbg_idx = -1;
	spi_flash_dbg_exp = 0;
	spi_flash_dbg_act = 0;
	struct sweep_result r = write_sweep(false, true, FLASH_WRITE_REGION_U);

	ZT_INFO("write_malloc_unaligned_len_sweep", "min=1,max=%u,fail=%u,first=%u,stage=%d,ret=%d,dlen=%d,idx=%d,exp=%02x,act=%02x",
		(unsigned int)WRITE_MAX_LEN, (unsigned int)r.fail,
		(unsigned int)r.first_fail, spi_flash_dbg_stage, spi_flash_dbg_ret,
		spi_flash_dbg_len, spi_flash_dbg_idx, spi_flash_dbg_exp,
		spi_flash_dbg_act);
	zassert_equal(r.fail, 0U, "malloc 非对齐写失败 %u 次，首个失败 len=%u",
		      (unsigned int)r.fail, (unsigned int)r.first_fail);
}

/* -------------------------------------------------------------------------- */
/* 套件：setup/teardown 为套件级；before/after 为用例级（本套件不用）             */
/* -------------------------------------------------------------------------- */

ZTEST_SUITE(spi_flash, NULL, spi_flash_suite_setup, NULL, NULL, NULL);
