/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * i2cf_enum —— I2CF 状态枚举矩阵回归用例（单核 cpu1）。
 *
 * 覆盖「白名单内/外 × 读/写 × 有/无器件 × 滤波器开/关 → 反馈状态
 *       （ret / 是否上报 / ERR 日志 / SMBF_NONWHITELIST）」的完整枚举表。
 *
 * 拓扑（DT 定，见 app.overlay）：
 *   上游 master = i2c2（alias i2cfupstream）→ i2cf4 输入 SCLI/SDAI(PE03/PB02)
 *   → 被测滤波器 i2cf4 → 输出 SCLO/SDAO(PE02/PB00) → i2c3 片内 target eeprom@33。
 *   （i2c4 = 旁路 master：switch_to_master() 时直驱 SCLO/SDAO，绕过滤波器。）
 *
 * 地址选择依据（实测，见 tests/COVERAGE.md）：
 *   0x33 = 片内 target（链路里有器件，读回真数据）；
 *   0x2A = 下游实测**无**器件（旁路 slave_addr_scan 应答清单 = 33 50；
 *          0x50 为板载实体器件，历史撞址，不用）。
 *   ⇒ 0x33/0x2A 是"有/无器件"这一维的实测依据（C8/C9 旁路对照独立复核）。
 *
 * 单变量 + 组间复位：每格前 cold_reset（**不清** WHITELIST_ADDRESS_* /
 *   SMBF_ADDRESS_ENABLE）→ **显式关全部 16 通道**（⇒ 空白名单）→ 注册回调
 *   → 交管脚 → 按格配白名单/开关。清 ISR：写 INTR_CLR=0x3 并把计数归零。
 *
 * 逐格判据（全部来自 reports/2026-10-15-3 实测，22 轮零方差）：
 *   每格读、写两方向**分别**断言 ret / ISR 增量合计 / SMBF_NONWHITELIST 锁存。
 *   核心结论 = **"上报只由白名单内外决定，与读写方向 / 器件应答无关"**：
 *     C2（白名单内 + 无器件）isr=0 与 C4（白名单外 + 无器件）isr=1 构成对照；
 *     C1（内 + 有器件）isr=0、C3（外 + 有器件）isr=1 各配"有器件"一侧。
 *   filter off 各格（C5/C6/C7）实测 ret=-5 且无上报（**实测锁定**，C7 与 C5 逐项
 *     相同 ⇒ filter off 下白名单内容无关）；C8（旁路 0x33）ret=0 + 真数据证明器件可达。
 *
 * 归因设计：每格成对 LOG_INF marker（EM_BEGIN / EM_END）包住 3 笔事务；驱动 ISR 内打的
 *   "address beyond whitelist" ERR 行与 marker 同走 deferred 日志缓冲，落在两 marker
 *   之间 ⇒ 串口可逐格数 ERR 行（人读）。断言侧不数日志，只断 ISR 计数 / ret / 锁存
 *   （日志会因 ring 溢出丢行，ISR 计数不会）。
 *
 * ★ 本文件由 CONFIG_I2CF_ENUM_MATRIX 保护（默认 n）：不置位时不编入，
 *   case_default 构建逐字节不变。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ztest_glue.h"
#include "i2cf_test.h"

#include <i2c_filter.h>
#include <reg_i2c_filter.h>

LOG_MODULE_REGISTER(i2cf_enum, LOG_LEVEL_INF);

/* 被测实例下标（i2cf4；见 i2cf_test.c 的 i2cf_devs[]） */
#define EM_INST            3

/* 每格重复笔数（对冲"紧接被拦之后一笔会丢"，取多数） */
#define EM_REPEAT          3
/* 每笔事务后等待 ISR 落定 + 其 ERR 日志入队的静默（ms） */
#define EM_TXN_SETTLE_MS   10

/* 片内 target 地址（app.overlay 的 &i2c3 / eeprom@33）—— 总线上**有**器件 */
#define EM_ADDR_DEV        0x33u
/* 下游**无**器件地址（实测应答清单 = 33 50；0x50 板载有实体器件，历史撞址，不用） */
#define EM_ADDR_NODEV      0x2Au

/* P0 自检写入片内 target 的已知 pattern（C1/C8 读回比对用） */
static const uint8_t em_pat[4] = { 0xA5u, 0x5Au, 0x11u, 0x22u };

/* 通路模式 */
enum em_mode {
	EM_FILTER_ON,      /* switch_to_filter + filter on（BLOCK_DISABLE=0,FILTER_DISABLE=0） */
	EM_FILTER_OFF,     /* switch_to_filter + filter off（BLOCK_DISABLE=1,FILTER_DISABLE=1） */
	EM_BYPASS_MASTER,  /* switch_to_master：i2c4 直驱输出侧，绕过滤波器（对照用） */
};

struct em_cfg {
	const char *tag;
	uint8_t     mode;
	bool        wl_on;       /* true = 把 wl_addr 授权进通道 0；false = 空白名单 */
	uint8_t     wl_addr;
	uint8_t     addr;        /* 事务目标地址 */
	int         exp_ret;     /* 期望 ret（读/写相同，实测） */
	int         exp_isr;     /* 期望 ISR 增量**合计**（= 每笔期望 × EM_REPEAT） */
	bool        exp_nw;      /* 期望 SMBF_NONWHITELIST 锁存非零 */
	bool        exp_rd_pat;  /* read 方向是否期望读回 em_pat */
	const char *note;        /* 该格语义（矩阵维度） */
};

/*
 * 枚举格清单（白名单内/外 × 器件有/无 × 滤波器开/关）。
 * "外" = 该地址不在白名单（空白名单即可）；"内" = 该地址被授权。
 * exp_* 全部来自 reports/2026-10-15-3 的实测表（22 轮零方差）。
 */
static const struct em_cfg em_cfgs[] = {
	{ "C1_wlin_devy",      EM_FILTER_ON,      true,  EM_ADDR_DEV,   EM_ADDR_DEV,   0,    0,
	  false, true,  "白名单内/有器件 → ret=0 + 真数据, isr=0" },
	{ "C2_wlin_devn",      EM_FILTER_ON,      true,  EM_ADDR_NODEV, EM_ADDR_NODEV, -EIO, 0,
	  false, false, "白名单内/无器件 → ret=-5, isr=0（靶心：不因无应答而上报）" },
	{ "C3_wlout_devy",     EM_FILTER_ON,      false, 0,             EM_ADDR_DEV,   -EIO, EM_REPEAT,
	  true,  false, "白名单外/有器件 → ret=-5, isr=1, 锁存违规地址" },
	{ "C4_wlout_devn",     EM_FILTER_ON,      false, 0,             EM_ADDR_NODEV, -EIO, EM_REPEAT,
	  true,  false, "白名单外/无器件 → ret=-5, isr=1, 锁存违规地址" },
	{ "C5_foff_devy",      EM_FILTER_OFF,     false, 0,             EM_ADDR_DEV,   -EIO, 0,
	  false, false, "filter off/有器件 → ret=-5（不转发）, isr=0" },
	{ "C6_foff_devn",      EM_FILTER_OFF,     false, 0,             EM_ADDR_NODEV, -EIO, 0,
	  false, false, "filter off/无器件 → ret=-5, isr=0" },
	{ "C7_foff_wlin_devy", EM_FILTER_OFF,     true,  EM_ADDR_DEV,   EM_ADDR_DEV,   -EIO, 0,
	  false, false, "filter off + 白名单内/有器件 → 与 C5 逐项相同（白名单内容无关）" },
	{ "C8_bypass_devy",    EM_BYPASS_MASTER,  false, 0,             EM_ADDR_DEV,   0,    0,
	  false, true,  "旁路 i2c4 直驱/有器件 → ret=0 + 真数据（证明器件可达）" },
	{ "C9_bypass_devn",    EM_BYPASS_MASTER,  false, 0,             EM_ADDR_NODEV, -EIO, 0,
	  false, false, "旁路 i2c4 直驱/无器件 → ret=-5" },
};

/* ISR 观测：回调在中断上下文执行，只计数、不打日志（SPEC_REFERENCE §6.1.2） */
static volatile uint32_t em_isr_count;

static void em_isr_cb(const struct device *dev, uint32_t idx, void *ud, void *dd)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(idx);
	ARG_UNUSED(ud);
	ARG_UNUSED(dd);

	em_isr_count++;
}

static void em_arm(const struct device *dev, int inst)
{
	/* 清 pending 中断（ADDRESS|COMMAND beyond whitelist）+ 计数归零 */
	sys_write32(0x3, i2cf_reg_base(inst) + INTR_CLR);
	em_isr_count = 0;
}

/*
 * 复位到已知态 + 按格配置：
 *   ① cold_reset（= 收尾 filter off + clr_tbl + 交管脚给滤波器）
 *   ② 显式关全部 16 通道（cold_reset 不清 SMBF_ADDRESS_ENABLE）⇒ 空白名单
 *   ③ 注册 ISR 回调
 *   ④ 按 mode 配通路/开关；wl_on 时授权通道 0
 */
static void em_setup(const struct device *dev, const struct em_cfg *c)
{
	uint32_t bm[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];

	(void)linkedsemi_i2c_filter_cold_reset(dev);
	for (uint8_t i = 0; i < I2CF_CHANNEL_NUM; i++) {
		(void)linkedsemi_i2c_filter_enable_channel(dev, i, false);
	}
	(void)linkedsemi_i2c_filter_register_callback(dev, 0, em_isr_cb, NULL);

	if (c->mode == EM_BYPASS_MASTER) {
		(void)linkedsemi_i2c_filter_switch_to_master(dev);
		return;
	}

	(void)linkedsemi_i2c_filter_switch_to_filter(dev);
	if (c->wl_on) {
		for (int i = 0; i < LINKEDSEMI_I2C_F_REMAP_SIZE_U32; i++) {
			bm[i] = 0xffffffffu;   /* 全命令码放行（避免掺入 command 违规） */
		}
		(void)linkedsemi_i2c_filter_fill_bitmap(dev, 0, c->wl_addr, bm);
	}
	if (c->mode == EM_FILTER_ON) {
		(void)linkedsemi_i2c_filter_en(dev, true, true, false);
	} else {
		(void)linkedsemi_i2c_filter_en(dev, false, false, false);
	}
}

/* 收尾：关滤波器 + 管脚交回滤波器 + 关全部通道（用例间零残留） */
static void em_teardown(const struct device *dev)
{
	(void)linkedsemi_i2c_filter_en(dev, false, false, false);
	(void)linkedsemi_i2c_filter_switch_to_filter(dev);
	for (uint8_t i = 0; i < I2CF_CHANNEL_NUM; i++) {
		(void)linkedsemi_i2c_filter_enable_channel(dev, i, false);
	}
}

/*
 * 跑一格（一个方向）：重置 → mark → 3 笔 → mark → 逐格断言。
 */
static void em_run_cell(const struct device *dev, const struct device *up,
			const struct device *down, const struct em_cfg *c, bool read_dir)
{
	const uintptr_t base = i2cf_reg_base(EM_INST);
	/* 旁路格由 i2c4（down）发波；其余格走上游 i2c2（up）经滤波器 */
	const struct device *mst = (c->mode == EM_BYPASS_MASTER) ? down : up;
	uint32_t nw_pre, nw_post, isr_tot = 0;
	int rets[EM_REPEAT];
	uint8_t rbuf[EM_REPEAT][4];

	em_setup(dev, c);
	em_arm(dev, EM_INST);
	nw_pre = sys_read32(base + SMBF_NONWHITELIST);

	LOG_INF("EM_BEGIN tag=%s dir=%s addr=%#x wl=%s mode=%s",
		c->tag, read_dir ? "RD" : "WR", c->addr,
		c->wl_on ? "in" : "out",
		c->mode == EM_FILTER_ON ? "on" :
		(c->mode == EM_FILTER_OFF ? "off" : "bypass"));

	for (int k = 0; k < EM_REPEAT; k++) {
		uint32_t before = em_isr_count;
		uint8_t w = 0xA5u;

		memset(rbuf[k], 0, sizeof(rbuf[k]));
		if (read_dir) {
			rets[k] = i2c_burst_read(mst, c->addr, 0x00, rbuf[k], sizeof(rbuf[k]));
		} else {
			rets[k] = i2c_write(mst, &w, 1, c->addr);
		}
		k_msleep(EM_TXN_SETTLE_MS);     /* 等 ISR 落定 + 其 ERR 日志入队 */
		isr_tot += em_isr_count - before;
	}

	nw_post = sys_read32(base + SMBF_NONWHITELIST);

	LOG_INF("EM_END tag=%s dir=%s ret=%d,%d,%d isr_tot=%u nw=%#x->%#x "
		"rd0=%02x%02x%02x%02x",
		c->tag, read_dir ? "RD" : "WR",
		rets[0], rets[1], rets[2], isr_tot, nw_pre, nw_post,
		rbuf[0][0], rbuf[0][1], rbuf[0][2], rbuf[0][3]);

	/* ---------- 逐格断言（判据全部实测来源） ---------- */
	for (int k = 0; k < EM_REPEAT; k++) {
		zassert_equal(rets[k], c->exp_ret,
			      "%s dir=%s 第%d笔 ret=%d，期望 %d",
			      c->tag, read_dir ? "RD" : "WR", k, rets[k], c->exp_ret);
	}
	zassert_true(isr_tot == (uint32_t)c->exp_isr,
		     "%s dir=%s ISR 增量合计=%u，期望 %d（白名单外每笔上报一次）",
		     c->tag, read_dir ? "RD" : "WR", isr_tot, c->exp_isr);
	zassert_true((nw_post != 0u) == c->exp_nw,
		     "%s dir=%s SMBF_NONWHITELIST=%#x，期望%s锁存",
		     c->tag, read_dir ? "RD" : "WR", nw_post,
		     c->exp_nw ? "" : "不");
	if (c->exp_nw) {
		/* 实测：锁存形 = 0x<addr>00（ERROR_ADDRESS=[15:8]） */
		zassert_equal((nw_post >> 8) & 0xffu, (uint32_t)c->addr,
			      "%s 锁存的 ERROR_ADDRESS 应 = 被拦地址 %#x",
			      c->tag, c->addr);
	}
	if (c->exp_rd_pat && read_dir) {
		zassert_mem_equal(rbuf[0], em_pat, sizeof(em_pat),
				  "%s read 应读回 P0 pattern（器件在位 + 通路真）", c->tag);
	}
}

/* 下游器件清单（旁路通路扫描，不经滤波器）—— 复核"0x2A 无器件 / 0x33 有器件" */
static void em_prescan(const struct device *dev, const struct device *down)
{
	uint8_t found[I2CF_SCAN_MAX];
	char list[I2CF_SCAN_MAX * 3 + 2];
	int n = 0, pos = 0;
	uint8_t v = 0;

	(void)linkedsemi_i2c_filter_switch_to_master(dev);
	k_msleep(5);
	for (uint16_t a = I2CF_SCAN_ADDR_MIN; a <= I2CF_SCAN_ADDR_MAX; a++) {
		if (i2c_read(down, &v, 1, (uint8_t)a) == 0) {
			if (n < I2CF_SCAN_MAX) {
				found[n++] = (uint8_t)a;
			}
		}
	}
	(void)linkedsemi_i2c_filter_switch_to_filter(dev);

	for (int i = 0; i < n; i++) {
		pos += snprintf(list + pos, sizeof(list) - (size_t)pos, "%02x ", found[i]);
	}
	if (pos == 0) {
		(void)snprintf(list, sizeof(list), "(none)");
	}
	LOG_INF("EM_PRESCAN downstream_count=%d addrs=%s", n, list);
}

/* P0：旁路往片内 target 0x33 写已知 pattern 并回读 —— 证"器件在位 + 数据通路真" */
static void em_p0_check(const struct device *dev, const struct device *down)
{
	uint8_t chk[4] = { 0 };
	int wr, rd;

	(void)linkedsemi_i2c_filter_switch_to_master(dev);
	k_msleep(5);
	wr = i2c_burst_write(down, EM_ADDR_DEV, 0x00, em_pat, sizeof(em_pat));
	k_msleep(20);                     /* target EEPROM 写入落定 */
	rd = i2c_burst_read(down, EM_ADDR_DEV, 0x00, chk, sizeof(chk));
	(void)linkedsemi_i2c_filter_switch_to_filter(dev);

	LOG_INF("EM_P0 addr=%#x pat=%02x%02x%02x%02x wr=%d rd=%d chk=%02x%02x%02x%02x",
		EM_ADDR_DEV, em_pat[0], em_pat[1], em_pat[2], em_pat[3], wr, rd,
		chk[0], chk[1], chk[2], chk[3]);

	/* 器件在位 + 旁路数据通路真（C1/C8 读回真数据的前提） */
	zassert_ok(wr, "P0 bypass write to %#x failed (%d)", EM_ADDR_DEV, wr);
	zassert_ok(rd, "P0 bypass read from %#x failed (%d)", EM_ADDR_DEV, rd);
	zassert_mem_equal(chk, em_pat, sizeof(em_pat),
			  "P0 回读 != 写入 pattern ⇒ 片内 target 不在位或旁路通路坏");
}

/*
 * 枚举矩阵回归用例：跑满 9 格 × 2 方向，逐格断言。
 * 核心结论 =「滤波器是否上报只由『地址是否在白名单内』决定，
 *           与总线是否应答(ACK/NACK)无关、与读/写方向无关」。
 */
ZTEST(i2cf, enum_matrix)
{
	const struct device *dev = i2cf_dev(EM_INST);                    /* i2cf4：被测 */
	const struct device *up = DEVICE_DT_GET(DT_ALIAS(i2cfupstream)); /* i2c2：上游 master */
	const struct device *down = i2cf_i2c_dev(dev);                   /* i2c4：旁路 master */

	zassert_not_null(dev, "i2cf4 device missing");
	zassert_true(device_is_ready(dev), "i2cf4 not ready");
	zassert_true(device_is_ready(up), "upstream master (i2cfupstream) not ready");
	zassert_true(device_is_ready(down), "bypass master (i2c4) not ready");

	em_prescan(dev, down);
	em_p0_check(dev, down);

	for (int i = 0; i < (int)ARRAY_SIZE(em_cfgs); i++) {
		em_run_cell(dev, up, down, &em_cfgs[i], true);    /* 读方向 */
		em_run_cell(dev, up, down, &em_cfgs[i], false);   /* 写方向 */
	}

	em_teardown(dev);

	ZT_INFO("enum_matrix", "cells=%u,dirs=2,repeat=%d",
		(unsigned)ARRAY_SIZE(em_cfgs), EM_REPEAT);

	/* 核心对照（人读）：C2（内/无器件）isr=0 ↔ C4（外/无器件）isr=1 */
	LOG_INF("EM_DONE 上报只由白名单内外决定：C1=isr0 C2=isr0(内) | C3=isr1 C4=isr1(外)");
}
