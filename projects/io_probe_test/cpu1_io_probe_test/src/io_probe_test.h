/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * io_probe_test —— 实测 IO 互联矩阵（单核 cpu1 安全域）公共接口。
 *
 * 目的：用户手工接了线，但"不确定接对了没有"。本工程不靠 DT 推导，
 * 而是**实测**哪些引脚之间真的导通，产出互联矩阵，用来判定
 * i2c2/i2c3/i2c4、spi1/spi3/spi4 与 spif1..4 的跳线是否成立。
 *
 * 方法（只驱动 LOW，永不驱动 HIGH，理由见 io_probe_test.c 头注释）：
 *   1. 全部候选脚 = 输入 + 内部上拉 → 读基线（基线为低 = 被外部拉低）
 *   2. 逐个把候选脚 i 配成"推挽输出低"，其余保持输入 + 上拉，稳定后采样
 *        读到 LOW  <=> 与 i 连通
 *        读到 HIGH <=> 不连通
 *   3. 跑完得矩阵，检查对称性
 *
 * 「同引脚（DT 固有）」与「跳线连通（实测）」必须分开看：
 *   i2cfN 与 i2cN 在 DT 上是同一根脚（i2cfN.i2c = <&i2cN> 且 SCLO/SDAO 同脚），
 *   矩阵里它们天然"连通" —— 那是同一根脚，**不是**用户接的跳线。
 *   本模块把这种关系单独标成 IO_PROBE_SAME_PIN，绝不混进连线清单。
 */
#ifndef IO_PROBE_TEST_H
#define IO_PROBE_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

/* 候选物理引脚上限。本工程实测 75 个（见 tests/COVERAGE.md §2 引脚表），
 * 取 80 留余量。矩阵为 count x count 字节，整块放 .bss。 */
#define IO_PROBE_MAX_PINS 80

/* 连线清单最大打印条数（矩阵本身不截断；清单只是便于阅读的摘要） */
#define IO_PROBE_MAX_LINKS 300

/* 不对称对／异常行的**打印**上限。首跑实测 2624 对不对称，逐条打印会把
 * deferred log ring 撑爆（丢消息）。计数不截断，只是不逐条刷屏。 */
#define IO_PROBE_MAX_ASYM_PRINT 16

/* 一个候选**物理**引脚（端口 + 脚号），附带全部占用它的逻辑信号名。
 * names 多个用 '|' 分隔 —— 用于在报告里区分「同引脚」与「跳线」。 */
struct io_probe_pin {
	const struct device *dev;
	uint8_t pin;
	const char *label;   /* "PB04"：端口字母 + 两位十进制脚号，行/列标签 */
	const char *names;   /* "i2c2.SCL|i2cf2.SCLO" */
};

enum io_probe_state {
	IO_PROBE_UNKNOWN = 0,
	IO_PROBE_NO_LINK,    /* 驱动 i 为低时 j 仍为高 —— 不连通 */
	IO_PROBE_LINK,       /* j 随 i 拉低，且 j 基线为高 —— 实测连通 */
	IO_PROBE_AMBIGUOUS,  /* j 随 i 拉低，但 j 基线已是低 —— 读数无法归因 */
	IO_PROBE_SAME_PIN,   /* i、j 是同一根物理脚（DT 固有，非跳线） */
	IO_PROBE_SELF,       /* i == j（对角） */
};

struct io_probe_result {
	uint8_t count;
	const struct io_probe_pin *pins;

	/* state[driven][read] —— 行列索引都对应 pins[] 下标 */
	uint8_t state[IO_PROBE_MAX_PINS][IO_PROBE_MAX_PINS];

	bool cfg_ok[IO_PROBE_MAX_PINS];       /* 全部 gpio_pin_configure 返回 0 */
	bool controlled[IO_PROBE_MAX_PINS];   /* 驱动低后自身读回为低 => 该脚确受控 */
	bool baseline_low[IO_PROBE_MAX_PINS]; /* 输入 + 内部上拉仍读低 => 外部拉低 */
	bool baseline_ok[IO_PROBE_MAX_PINS];  /* 基线读成功（读回非负） */

	/* 还原验证（补缺陷 A）：驱动脚还原成"高阻输入 + 上拉"后**读回必须为高**。
	 * 基线为高却没还原回高 => not_restored，该脚从可驱动集剔除、且不产生连线证据。 */
	bool restored_high[IO_PROBE_MAX_PINS];
	bool not_restored[IO_PROBE_MAX_PINS];
	uint16_t restore_us[IO_PROBE_MAX_PINS];  /* 还原实测耗时（含轮询） */
	uint16_t settle_us[IO_PROBE_MAX_PINS];   /* 本行采样稳定耗时（轮询直到两次一致） */

	uint16_t settle_us_max;
	uint16_t restore_us_max;

	uint8_t baseline_low_count;
	uint8_t not_restored_count;
	uint8_t not_controlled_count;
	uint8_t uncontrolled[IO_PROBE_MAX_PINS]; /* 不可作为驱动脚：未受控/未还原 */

	uint16_t link_count;        /* state == LINK 的单元数 */
	uint16_t ambiguous_count;   /* state == AMBIGUOUS 的单元数 */
	uint16_t asymmetric_count;  /* (i,j) 与 (j,i) 的 LINK/NO_LINK 判定不一致的对数 */
	uint16_t same_pin_pairs;    /* 同物理脚的 (i,j) 对数（应为偶数对） */

	/* 矩阵自检（防"测量本身坏掉"）：无向对的上限 = N*(N-1)/2。
	 * links 超过上限在数学上不可能，出现即证明存在假阳性 => 自检失败。 */
	uint16_t link_upper_bound;
	bool link_over_bound;
};

/* 执行一次全量邻接扫描，结果写入 res（调用方持有）。返回 0；不抛异常。 */
int io_probe_scan(struct io_probe_result *res);

/* 按 label 查下标；找不到返回 -1 */
int io_probe_index_of(const struct io_probe_result *res, const char *label);

/* 由 label_a 驱动时 label_b 的实测状态（取矩阵 [a][b] 单元） */
enum io_probe_state io_probe_state_of(const struct io_probe_result *res,
				      const char *label_a, const char *label_b);

/* 报告打印：引脚图例 + 邻接矩阵 + 显式连线清单 + 期望/实测对照 */
void io_probe_dump_all(const struct io_probe_result *res);

/* 在两组 label 之间统计实测连通对数（LINK 才算），返回条数；
 * pairs 非空时把命中的 "A <-> B" 追加进 out（容量 cap 对）。 */
int io_probe_cross_links(const struct io_probe_result *res,
			 const char *const *group_a, int na,
			 const char *const *group_b, int nb,
			 char out[][24], int cap);

#endif /* IO_PROBE_TEST_H */
