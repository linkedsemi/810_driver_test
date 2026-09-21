/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * io_probe_test —— 实测 IO 互联矩阵。
 *
 * ============================================================================
 * 为什么不用 DT 推导
 * ============================================================================
 * 用户手工接了线，但不确定接对了没有。DT 只能告诉我们"某信号**本该**在哪根脚"
 * 和"哪些信号**天生**共用一根脚"；它无法告诉我们两根线之间**实际**有没有跳线。
 * 本工程只信实测：把候选脚逐个拉低，看别的脚是否跟着变低。
 *
 * ============================================================================
 * 方法：只驱动 LOW，永不驱动 HIGH
 * ============================================================================
 * 逐脚邻接扫描（adjacency scan）：
 *   全部候选脚 = 输入 + 内部上拉；把第 i 根配成**推挽输出低**，其余不动；
 *   稳定后采样 -> 读到 LOW 即与 i 连通，读到 HIGH 即不连通。
 *
 * 为什么低有效、为什么只拉低：
 *   1. I2C 总线上有外部上拉。只有"拉低"能赢过外部上拉，"推高"赢不了；
 *   2. "拉低对着外部上拉"是 0.7mA 量级的无害状态；
 *      "推高撞上别的芯片输出"才是危险方向（会产生大电流对灌）。
 *   => 本文件**没有任何** GPIO_OUTPUT_HIGH / INIT_HIGH 的调用路径。
 *      改这个文件时请保持该不变量。
 *
 * ============================================================================
 * 自检（防"方法本身坏掉"）
 * ============================================================================
 *   a) 逐脚受控自检：驱动低后读回自身，必须为低（否则该脚根本没被我们控制，
 *      例如仍被复用功能占着）-> 记 controlled=false，用例大声 FAIL。
 *   b) 基线门：全输入 + 内部上拉时读回，若为低说明外部有东西把它压住，
 *      该脚的一切读数都不可归因 -> 标 AMBIGUOUS，**不参与连线计数**，
 *      且单列一节 BASELINE_LOW。
 *   c) 还原验证（★ 首跑暴露的缺陷 A）：驱动脚还原成"高阻输入 + 上拉"后
 *      **必须读回为高**。读不回高 => 该脚会被此后每一行误读成低（级联污染，
 *      首跑实测出"完美下三角"）。=> 记 not_restored，把它从可驱动集剔除，
 *      并在连线计数中视为"无证据"。
 *   d) 阳性对照：I3C5_SCL(PJ10) <-> I3C6_SCL(PJ08)、I3C5_SDA(PJ11) <-> I3C6_SDA(PJ09)
 *      是**已确认接好的跨核对连**（810 README「外部接线记录」）。
 *      扫描**必须**报出这两对；报不出 => 方法失效 => 用例 FAIL。
 *   e) 对称性：i->j 与 j->i 判定不一致的对单独列出。
 *   f) 上限自检：无向对的上限 = N*(N-1)/2。links 超过上限在数学上不可能，
 *      出现即证明测量有假阳性 => 用例 FAIL。
 *
 * ============================================================================
 * 关于还原：为什么不能只调 gpio_pin_configure(GPIO_INPUT|GPIO_PULL_UP)
 * ============================================================================
 * 首跑（2026-09-18）实测：任何脚一旦被驱动低就再也回不到高，矩阵呈完美下三角
 * （第 i 行对全部 j<i 都是 '#'），links=2862 超过理论上限 2775。
 * 根因（HAL 层，只读复核，未改）：modules/hal/linkedsemi/soc/rv32/qsh/
 * ls_soc_gpio.c 的 io_cfg_input() 清的是 OE_DIN 的**低半区**（DIN 回读位）：
 *     APP_PMU->IO_VAL[port].OE_DIN &= ~(1<<num);
 * 而输出使能位在**高半区** —— io_cfg_output() 置 1<<num<<16、
 * io_cfg_disable_output() 清 1<<num<<16、io_is_output_enabled() 读 1<<num<<16。
 * 所以 OUTPUT->INPUT 的迁移**不会**撤掉输出驱动，pad 保持驱动在上一次的值（低）。
 * 驱动读回 gpio_ls_port_get_raw() = OE_DIN & 0xffff（pad 值）=> 永远读到低。
 *
 * 本文件的对策（不碰驱动/HAL）：还原走 GPIO_DISCONNECTED（内部 io_cfg_disable()
 * -> io_cfg_disable_output()，正确清 OE 高半区）再配 GPIO_INPUT|GPIO_PULL_UP，
 * 然后**读回验证 + 轮询**。该缺陷已上报 INBOX，不在此处修 HAL。
 *
 * ============================================================================
 * 「同引脚（DT 固有）」与「跳线连通（实测）」
 * ============================================================================
 *   i2cfN 与 i2cN 在 DT 上同引脚（i2cfN.i2c = <&i2cN>，且 SCLO/SDAO 复用同脚），
 *   spif1.CSNO 与 spi3.CS0 也同脚，spif2 的 IO2 与 spi3 的 CLK/IO0 也同脚。
 *   矩阵里这些格子"天然导通"，但那是**同一根脚**，不是用户接的跳线。
 *   本模块把它们标成 IO_PROBE_SAME_PIN（矩阵里显示 '='），
 *   **不计入连线清单**，避免误读。
 */
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "io_probe_test.h"
#include "ztest_glue.h"

LOG_MODULE_REGISTER(io_probe, LOG_LEVEL_INF);

/* settle 策略（★ 首跑暴露的缺陷 C）：不再用一个猜死的常数，而是**轮询到稳定**
 * ——反复整行采样，直到连续两次读数完全一致（或到上限）。
 *
 * 为什么原 300us 不够（至少不能假定够）：内部上拉 50-100kΩ，跳线电容 + 板上
 * 负载可能把 RC 时间常数抬到数十 us；300us 只有个位数倍余量，且没有回读确认。
 * 轮询法把真实稳定耗时**实测出来**并作为 INFO 上报。 */
#define IO_PROBE_SETTLE_POLL_US 20u    /* 轮询步长 */
#define IO_PROBE_SETTLE_MAX_US 5000u   /* 稳定等待上限，超时按当前读数采信并告警 */

/* 还原验证（★ 首跑暴露的缺陷 A）：还原后读回必须为高，轮询到超时。 */
#define IO_PROBE_RESTORE_POLL_US 20u
#define IO_PROBE_RESTORE_MAX_US 5000u

/* 每行日志之间的间隔：LOG_MODE_DEFERRED 的 log 线程要时间把 ring 排空
 * （115200 下 ~95 字节的行约 8ms）。首跑实测：350+ 行不对称逐条刷屏把 ring
 * 撑爆，丢了 "total measured links" 整行并造成多处行合并/乱码。
 * => 现在只打紧凑行（每行 ~60 字符）+ 行间让出 + 不对称只打前 N 条。 */
#define IO_PROBE_LINE_GAP_MS 20
#define IO_PROBE_LEGEND_GAP_MS 10

#define GPIOA_DEV DEVICE_DT_GET(DT_NODELABEL(gpioa))
#define GPIOB_DEV DEVICE_DT_GET(DT_NODELABEL(gpiob))
#define GPIOE_DEV DEVICE_DT_GET(DT_NODELABEL(gpioe))
#define GPIOF_DEV DEVICE_DT_GET(DT_NODELABEL(gpiof))
#define GPIOG_DEV DEVICE_DT_GET(DT_NODELABEL(gpiog))
#define GPIOH_DEV DEVICE_DT_GET(DT_NODELABEL(gpioh))
#define GPIOI_DEV DEVICE_DT_GET(DT_NODELABEL(gpioi))
#define GPIOJ_DEV DEVICE_DT_GET(DT_NODELABEL(gpioj))
#define GPIOQ_DEV DEVICE_DT_GET(DT_NODELABEL(gpioq))

/*
 * ============================================================================
 * 候选引脚表
 * ============================================================================
 * 来源：生效 DTS（板级 lsqsh_evb_pinctrl.dtsi + pfr_tpm 模块
 * lsqsh_pinctrl_pfr_tpm.dtsi + 已构建工程的 zephyr.dts）—— 逐脚核对，
 * 逐条 pinmux 宏解码回 端口+脚号 做过一致性校验，详见 tests/COVERAGE.md §2。
 *
 * names 里多个信号用 '|' 分隔 = 这些信号**天生共用这一根物理脚**（DT 固有）。
 *
 * 明确**不在**表内（排除理由见 tests/COVERAGE.md §3）：
 *   - PA05/PA07        公共层 gpio_toggle.c 每 500ms 翻转，会污染矩阵
 *   - PN03/PN04        uart1 控制台（拉低即丢日志通道）
 *   - PV00..PV05       qspi1 启动 flash，运行中在用
 *   - PD10..PD15       qspi1 测试焊盘（同上，不碰）
 *   - 其余外设脚       （eth PHY / SD / USB / 未列出的 spi2 等）不在本次请求范围，
 *                      不做"全网扫描"——避免碰到 strap 与带外部驱动的脚
 */
static const struct io_probe_pin probe_pins[] = {
	/* ---------- SPI1：linkedsemi,ls-spi（spi@4008a000，CS 是 GPIO） ---------- */
	{ GPIOA_DEV,  0, "PA00", "spi1.SCK" },
	{ GPIOA_DEV,  1, "PA01", "spi1.MOSI" },
	{ GPIOA_DEV,  2, "PA02", "spi1.MISO" },
	{ GPIOA_DEV,  3, "PA03", "spi1.CS(cs-gpios)" },

	/* ---------- I2C2 / I2CF2（PB04/PB01 为 i2cf2 的 O 侧，与 i2c2 同脚） ---------- */
	{ GPIOB_DEV,  4, "PB04", "i2c2.SCL|i2cf2.SCLO" },
	{ GPIOB_DEV,  1, "PB01", "i2c2.SDA|i2cf2.SDAO" },
	{ GPIOB_DEV,  5, "PB05", "i2cf2.SCLI" },
	{ GPIOB_DEV,  3, "PB03", "i2cf2.SDAI" },

	/* ---------- I2C3 / I2CF3 ---------- */
	{ GPIOB_DEV, 15, "PB15", "i2c3.SCL|i2cf3.SCLO" },
	{ GPIOB_DEV, 14, "PB14", "i2c3.SDA|i2cf3.SDAO" },
	{ GPIOB_DEV, 13, "PB13", "i2cf3.SCLI" },
	{ GPIOB_DEV, 12, "PB12", "i2cf3.SDAI" },

	/* ---------- I2C4 / I2CF4 ---------- */
	{ GPIOE_DEV,  2, "PE02", "i2c4.SCL|i2cf4.SCLO" },
	{ GPIOB_DEV,  0, "PB00", "i2c4.SDA|i2cf4.SDAO" },
	{ GPIOE_DEV,  3, "PE03", "i2cf4.SCLI" },
	{ GPIOB_DEV,  2, "PB02", "i2cf4.SDAI" },

	/* ---------- SPI3：snps,designware-spi（spi@4006e000） ---------- */
	{ GPIOG_DEV, 14, "PG14", "spi3.SCK|spif2.IO2_IN" },
	{ GPIOG_DEV, 15, "PG15", "spi3.IO0|spif2.IO2_OUT" },
	{ GPIOG_DEV,  3, "PG03", "spi3.IO1|spif2.CSNI" },
	{ GPIOE_DEV,  1, "PE01", "spi3.IO2" },
	{ GPIOE_DEV,  0, "PE00", "spi3.IO3" },
	{ GPIOI_DEV,  8, "PI08", "spi3.CS0|spif1.CSNO" },
	{ GPIOG_DEV,  2, "PG02", "spi3.CS1|spif2.CSNO" },

	/* ---------- SPI4：snps,designware-spi（spi@4006e400，CS 是 GPIO PQ02/PQ01） ---------- */
	{ GPIOQ_DEV,  0, "PQ00", "spi4.SCK|spif4.CSNO" },
	{ GPIOQ_DEV,  3, "PQ03", "spi4.IO0" },
	{ GPIOQ_DEV,  4, "PQ04", "spi4.IO1" },
	{ GPIOQ_DEV,  5, "PQ05", "spi4.IO2|spif4.CSNI" },
	{ GPIOQ_DEV,  6, "PQ06", "spi4.IO3" },
	{ GPIOQ_DEV,  2, "PQ02", "spi4.CS0(cs-gpios)" },
	{ GPIOQ_DEV,  1, "PQ01", "spi4.CS1(cs-gpios)" },
	{ GPIOI_DEV,  0, "PI00", "spi4.CS0|spif3.CSNO" },

	/* ---------- SPIF1（spi = <&spi3>，CSN 与 spi3.CS0 同脚 PI08） ---------- */
	{ GPIOI_DEV, 10, "PI10", "spif1.CSNI" },
	{ GPIOG_DEV,  4, "PG04", "spif1.SCKI" },
	{ GPIOF_DEV, 14, "PF14", "spif1.SI" },
	{ GPIOG_DEV,  6, "PG06", "spif1.IO1_IN" },
	{ GPIOH_DEV,  0, "PH00", "spif1.IO2_IN" },
	{ GPIOH_DEV,  2, "PH02", "spif1.IO3_IN" },
	{ GPIOG_DEV,  0, "PG00", "spif1.SCKO" },
	{ GPIOF_DEV, 15, "PF15", "spif1.SO" },
	{ GPIOF_DEV, 13, "PF13", "spif1.IO1_OUT" },
	{ GPIOH_DEV,  1, "PH01", "spif1.IO2_OUT" },
	{ GPIOH_DEV,  3, "PH03", "spif1.IO3_OUT" },

	/* ---------- SPIF2（spi = <&spi3>，IO2/CSN 与 spi3 同脚） ---------- */
	{ GPIOG_DEV,  8, "PG08", "spif2.SCKI" },
	{ GPIOH_DEV,  4, "PH04", "spif2.SI" },
	{ GPIOH_DEV,  6, "PH06", "spif2.IO1_IN" },
	{ GPIOG_DEV, 12, "PG12", "spif2.IO3_IN|spi3.CS1(board)" },
	{ GPIOG_DEV,  9, "PG09", "spif2.SCKO" },
	{ GPIOH_DEV,  5, "PH05", "spif2.SO" },
	{ GPIOH_DEV,  7, "PH07", "spif2.IO1_OUT" },
	{ GPIOG_DEV, 13, "PG13", "spif2.IO3_OUT|spi3.CS0(board)" },

	/* ---------- SPIF3（spi = <&spi4>，CSN 与 spi4.CS0 同脚 PI00） ---------- */
	{ GPIOI_DEV,  2, "PI02", "spif3.CSNI" },
	{ GPIOF_DEV,  8, "PF08", "spif3.SCKI" },
	{ GPIOF_DEV,  4, "PF04", "spif3.SI" },
	{ GPIOF_DEV,  2, "PF02", "spif3.IO1_IN" },
	{ GPIOF_DEV, 10, "PF10", "spif3.IO2_IN" },
	{ GPIOF_DEV,  6, "PF06", "spif3.IO3_IN" },
	{ GPIOF_DEV,  9, "PF09", "spif3.SCKO" },
	{ GPIOF_DEV,  5, "PF05", "spif3.SO" },
	{ GPIOF_DEV,  3, "PF03", "spif3.IO1_OUT" },
	{ GPIOF_DEV, 11, "PF11", "spif3.IO2_OUT" },
	{ GPIOF_DEV,  7, "PF07", "spif3.IO3_OUT" },

	/* ---------- SPIF4（spi = <&spi4>，CSN 与 spi4.SCK 同脚 PQ00） ---------- */
	{ GPIOH_DEV, 12, "PH12", "spif4.SCKI" },
	{ GPIOH_DEV, 10, "PH10", "spif4.SI" },
	{ GPIOH_DEV,  8, "PH08", "spif4.IO1_IN" },
	{ GPIOF_DEV, 12, "PF12", "spif4.IO2_IN" },
	{ GPIOG_DEV,  7, "PG07", "spif4.IO3_IN" },
	{ GPIOH_DEV, 13, "PH13", "spif4.SCKO" },
	{ GPIOH_DEV, 11, "PH11", "spif4.SO" },
	{ GPIOH_DEV,  9, "PH09", "spif4.IO1_OUT" },
	{ GPIOG_DEV,  1, "PG01", "spif4.IO2_OUT" },
	{ GPIOG_DEV,  5, "PG05", "spif4.IO3_OUT" },

	/* ---------- I3C5 / I3C6：阳性对照用的**已确认**跨核跳线 ---------- */
	{ GPIOJ_DEV, 10, "PJ10", "i3c5.SCL" },
	{ GPIOJ_DEV, 11, "PJ11", "i3c5.SDA" },
	{ GPIOJ_DEV,  8, "PJ08", "i3c6.SCL" },
	{ GPIOJ_DEV,  9, "PJ09", "i3c6.SDA" },
};

#define PROBE_N ((int)ARRAY_SIZE(probe_pins))

/*
 * DT 固有「同一块焊盘」清单（**事实声明，不是测量结果**）。
 *
 * 这些逻辑信号在 DT 里复用同一根物理脚 —— i2cfN 的 SCLO/SDAO 就是 i2cN 的
 * SCL/SDA（`i2cfN.i2c = <&i2cN>` 且 SCLO/SDAO 走同一 pinmux），spif 的若干
 * in/out 与 spi3/spi4 也是同脚。**任何**测量都必然看到它们"导通"，
 * 但那是同一块焊盘，**不是**用户接的跳线。
 *
 * 本工程的引脚表按**物理脚**建行（一根脚一行），所以这些关系在矩阵里
 * 根本不会以两个格子出现 —— 但正因为如此，更要把它们**显式列出来**，
 * 否则用户看到 `i2cf4.SCLI` 与 `i2c4.SCL` 的名字会以为需要自己接线。
 */
static const char *const same_pin_groups[][2] = {
	{ "i2c2.SCL(PB04)",          "i2cf2.SCLO(PB04)" },
	{ "i2c2.SDA(PB01)",          "i2cf2.SDAO(PB01)" },
	{ "i2c3.SCL(PB15)",          "i2cf3.SCLO(PB15)" },
	{ "i2c3.SDA(PB14)",          "i2cf3.SDAO(PB14)" },
	{ "i2c4.SCL(PE02)",          "i2cf4.SCLO(PE02)" },
	{ "i2c4.SDA(PB00)",          "i2cf4.SDAO(PB00)" },
	{ "spi3.SCK(PG14)",          "spif2.IO2_IN(PG14)" },
	{ "spi3.IO0(PG15)",          "spif2.IO2_OUT(PG15)" },
	{ "spi3.IO1(PG03)",          "spif2.CSNI(PG03)" },
	{ "spi3.CS1(PG02)",          "spif2.CSNO(PG02)" },
	{ "spi3.CS0(board)(PG13)",   "spif2.IO3_OUT(PG13)" },
	{ "spi3.CS1(board)(PG12)",   "spif2.IO3_IN(PG12)" },
	{ "spi4.SCK(PQ00)",          "spif4.CSNO(PQ00)" },
	{ "spi4.IO2(PQ05)",          "spif4.CSNI(PQ05)" },
	{ "spi3.CS0(spif ctx)(PI08)", "spif1.CSNO(PI08)" },
	{ "spi4.CS0(spif ctx)(PI00)", "spif3.CSNO(PI00)" },
};

/* 结果放 .bss：矩阵 80x80 字节 + 标志位，总量约 7KB，不占 ztest 线程栈 */
static struct io_probe_result g_res;

/* 报告用的临时缓冲（static，避免占用线程栈） */
static char g_rowbuf[200];
static char g_hdrbuf[220];

/* ============================ 低层 GPIO 助手 ============================ */

static uint32_t cycles_to_us(uint32_t cyc)
{
	return k_cyc_to_us_floor32(cyc);
}

/* 只驱动 LOW。GPIO_OUTPUT_LOW 内含 INIT_LOW，保证输出值与方向一致。
 * 本文件**不出现** GPIO_OUTPUT_HIGH / GPIO_OUTPUT_INIT_HIGH（见头注释不变量）。 */
static void pin_drive_low(const struct io_probe_pin *p)
{
	(void)gpio_pin_configure(p->dev, p->pin, GPIO_OUTPUT_LOW);
}

/*
 * 驱动一根脚为低，并**轮询到自身读回稳定为低**（或超时）—— 与采样侧同样的
 * "连续两次一致" 判据。**不能只等一个固定短时间**：高容性焊盘（长跳线 /
 * 多负载）从高阻上拉态被拉低需要时间；等太短会把"慢"误判成"不受控"。
 *
 * 返回 true = 稳定读到低（该脚确受 GPIO 控制）；false = 超时仍读到高。
 * *settle_us 回填实测耗时（上报真实值，取代原来的猜死常数）。
 */
static bool pin_drive_low_verified(const struct io_probe_pin *p, uint32_t *settle_us)
{
	uint32_t t0 = k_cycle_get_32();
	uint32_t waited = 0;
	int prev = -2;
	bool ok = false;

	pin_drive_low(p);

	for (;;) {
		int v = gpio_pin_get_raw(p->dev, p->pin);

		if (v == 0 && prev == 0) {   /* 连续两次读到低 => 稳定 */
			ok = true;
			break;
		}
		prev = v;
		if (waited >= IO_PROBE_SETTLE_MAX_US) {
			break;
		}
		k_busy_wait(IO_PROBE_SETTLE_POLL_US);
		waited += IO_PROBE_SETTLE_POLL_US;
	}
	*settle_us = cycles_to_us(k_cycle_get_32() - t0);
	return ok;
}

/*
 * 把一根脚还原成"高阻输入 + 内部上拉"，并**读回验证**为高。
 *
 * 为什么先 GPIO_DISCONNECTED：HAL 的 io_cfg_input() 有缺陷（见文件头"关于还原"），
 * OUTPUT->INPUT 不会撤掉输出驱动。GPIO_DISCONNECTED 内部走 io_cfg_disable()
 * -> io_cfg_disable_output()，那一条是**正确**的（清 OE_DIN 高半区）。之后再配
 * INPUT|PULL_UP，pad 才能真正回到高阻 + 上拉。
 *
 * 返回 true = 读回已是高；false = 超时仍为低（该脚不可还原）。
 * *settle_us 回填实测耗时（含轮询），用于上报真实稳定时间。
 */
static bool pin_restore_verified(const struct io_probe_pin *p, uint32_t *settle_us)
{
	uint32_t t0 = k_cycle_get_32();
	uint32_t waited = 0;
	bool ok = false;

	(void)gpio_pin_configure(p->dev, p->pin, GPIO_DISCONNECTED);
	(void)gpio_pin_configure(p->dev, p->pin, GPIO_INPUT | GPIO_PULL_UP);

	for (;;) {
		if (gpio_pin_get_raw(p->dev, p->pin) > 0) {
			ok = true;
			break;
		}
		if (waited >= IO_PROBE_RESTORE_MAX_US) {
			break;
		}
		k_busy_wait(IO_PROBE_RESTORE_POLL_US);
		waited += IO_PROBE_RESTORE_POLL_US;
	}

	*settle_us = cycles_to_us(k_cycle_get_32() - t0);
	return ok;
}

/* 矩阵单元的紧凑编码（2 bit/格）：
 *   0 = open（NO_LINK / SELF / SAME_PIN，对角天然 0）
 *   1 = LINK（实测连通）
 *   2 = AMBIGUOUS（基线为低，无法归因）
 *   3 = UNKNOWN（无证据：读失败 / 列不可用） */
static int cell_code(uint8_t st)
{
	switch (st) {
	case IO_PROBE_LINK:      return 1;
	case IO_PROBE_AMBIGUOUS: return 2;
	case IO_PROBE_UNKNOWN:   return 3;
	default:                 return 0;
	}
}

int io_probe_scan(struct io_probe_result *res)
{
	/* 采样用的双缓冲（整行 75 个读数），放 static 不占线程栈 */
	static int8_t cur[IO_PROBE_MAX_PINS];
	static int8_t prev[IO_PROBE_MAX_PINS];

	memset(res, 0, sizeof(*res));
	res->count = (uint8_t)PROBE_N;
	res->pins = probe_pins;
	res->link_upper_bound =
		(uint16_t)((PROBE_N * (PROBE_N - 1)) / 2);

	/* ---- Phase 0：全部候选脚 = 高阻输入 + 内部上拉（安全初态）----
	 * 复位后 OE 本就是 0，但仍走已验证的还原序列 —— 这样"初态"与"还原态"
	 * 是同一条语义，且覆盖"复位态并非 GPIO"的情形。 */
	for (int i = 0; i < PROBE_N; i++) {
		uint32_t sut = 0;
		bool up = pin_restore_verified(&probe_pins[i], &sut);

		res->cfg_ok[i] = true;
		res->restored_high[i] = up;
		res->restore_us[i] = (uint16_t)MIN(sut, 0xFFFFu);
	}
	k_msleep(2);

	/* ---- Phase 1：基线（全输入 + 上拉）---- */
	for (int i = 0; i < PROBE_N; i++) {
		int v = gpio_pin_get_raw(probe_pins[i].dev, probe_pins[i].pin);

		res->baseline_ok[i] = (v >= 0);
		res->baseline_low[i] = (v == 0);
		if (res->baseline_low[i]) {
			res->baseline_low_count++;
		}
	}

	/* ---- Phase 2：能力探测（受控 + 可还原），先于采样跑完 ----
	 * 必须在采样前把"哪些脚不可用"全部定下来：否则列 j 的可用性要等扫到
	 * 第 j 行才知道，前面行已经产生了假证据。 */
	for (int i = 0; i < PROBE_N; i++) {
		const struct io_probe_pin *pi = &probe_pins[i];
		uint32_t sut = 0;
		bool up;

		/* 驱动 + **轮询到自身稳定为低**（不是等一个猜死的短常数：
		 * 高容性焊盘需要时间，等太短会把"慢"误判成"不受控"）。 */
		res->controlled[i] = pin_drive_low_verified(pi, &sut);

		up = pin_restore_verified(pi, &sut);
		res->restored_high[i] = up;
		res->restore_us[i] = (uint16_t)MIN(sut, 0xFFFFu);
		/* 还原耗时的统计只用"基线为高、理应能还原"的脚 —— 基线为低的脚
		 * （外部压住）必然轮询到超时，把它算进 max 会把真实值淹没。 */
		if (!res->baseline_low[i] && res->restore_us[i] > res->restore_us_max) {
			res->restore_us_max = res->restore_us[i];
		}

		/* 还原门：基线为高、却还原不回高 => 这个脚一旦驱动就会永久污染
		 * 后面的行（缺陷 A）。剔出可驱动集，且不作为证据来源。 */
		if (!res->baseline_low[i] && !up) {
			res->not_restored[i] = true;
			res->not_restored_count++;
		}

		/* 不可用作驱动脚/证据源：基线为低（外部压住）、未受控、
		 * 或还原失败，三者任一。 */
		res->uncontrolled[i] = (res->baseline_low[i] ||
					!res->controlled[i] ||
					res->not_restored[i]);
		if (!res->controlled[i]) {
			res->not_controlled_count++;
		}
	}

	/* ---- Phase 3：逐脚驱动 LOW，**轮询到稳定**后采样 ----
	 * 只对可用脚驱动；不可用脚整行留 UNKNOWN（不给证据，也不浪费一整个
	 * 采样周期）。 */
	for (int i = 0; i < PROBE_N; i++) {
		const struct io_probe_pin *pi = &probe_pins[i];
		uint32_t t0;
		bool have_prev = false;

		if (res->uncontrolled[i]) {
			for (int j = 0; j < PROBE_N; j++) {
				res->state[i][j] = (j == i) ? IO_PROBE_SELF
							    : IO_PROBE_UNKNOWN;
			}
			continue;
		}

		pin_drive_low(pi);
		t0 = k_cycle_get_32();

		/* 轮询到稳定：连续两次整行读数完全一致即可采信。 */
		for (;;) {
			for (int j = 0; j < PROBE_N; j++) {
				cur[j] = (int8_t)gpio_pin_get_raw(probe_pins[j].dev,
								  probe_pins[j].pin);
			}
			if (have_prev && memcmp(cur, prev, PROBE_N) == 0) {
				break;
			}
			memcpy(prev, cur, PROBE_N);
			have_prev = true;
			if (cycles_to_us(k_cycle_get_32() - t0) >=
			    IO_PROBE_SETTLE_MAX_US) {
				break;
			}
			k_busy_wait(IO_PROBE_SETTLE_POLL_US);
		}
		res->settle_us[i] =
			(uint16_t)MIN(cycles_to_us(k_cycle_get_32() - t0), 0xFFFFu);
		if (res->settle_us[i] > res->settle_us_max) {
			res->settle_us_max = res->settle_us[i];
		}

		/* 受控自检读数取采样缓冲里的自身值（同一稳定窗口）。 */
		res->controlled[i] = (cur[i] == 0);

		for (int j = 0; j < PROBE_N; j++) {
			const struct io_probe_pin *pj = &probe_pins[j];
			int v = cur[j];

			if (j == i) {
				res->state[i][j] = IO_PROBE_SELF;
				continue;
			}
			if (pj->dev == pi->dev && pj->pin == pi->pin) {
				res->state[i][j] = IO_PROBE_SAME_PIN;
				res->same_pin_pairs++;
				continue;
			}
			/* 证据门 1：j 基线为低 => 被外部压住，读数无法归因 */
			if (res->baseline_low[j]) {
				res->state[i][j] = IO_PROBE_AMBIGUOUS;
				res->ambiguous_count++;
				continue;
			}
			/* 证据门 2：j 不可用（未受控 / 未还原）=> 读数不可信 */
			if (res->uncontrolled[j]) {
				res->state[i][j] = IO_PROBE_UNKNOWN;
				continue;
			}
			if (v < 0) {
				res->state[i][j] = IO_PROBE_UNKNOWN;
			} else if (v == 0) {
				res->state[i][j] = IO_PROBE_LINK;
				res->link_count++;
			} else {
				res->state[i][j] = IO_PROBE_NO_LINK;
			}
		}

		/* 还原 + 验证。运行期才失败（瞬时）也要处理：撤回它已被计入的
		 * 列证据，之后的行也不再驱它。 */
		{
			uint32_t sut = 0;
			bool up = pin_restore_verified(pi, &sut);
			int self_after = gpio_pin_get_raw(pi->dev, pi->pin);

			if (!res->baseline_low[i] && !up) {
				res->not_restored[i] = true;
				res->not_restored_count++;
				res->uncontrolled[i] = true;
				for (int k = 0; k < i; k++) {
					if (res->state[k][i] == IO_PROBE_LINK) {
						res->state[k][i] = IO_PROBE_UNKNOWN;
						res->link_count--;
					}
				}
				(void)self_after;
			}
		}
	}

	/* ---- Phase 4：对称性 ---- */
	for (int i = 0; i < PROBE_N; i++) {
		for (int j = i + 1; j < PROBE_N; j++) {
			bool a = (res->state[i][j] == IO_PROBE_LINK);
			bool b = (res->state[j][i] == IO_PROBE_LINK);

			if (a != b) {
				res->asymmetric_count++;
			}
		}
	}

	/* ---- Phase 5：矩阵自检（上限校验）----
	 * 无向对上限 = N*(N-1)/2。links 超过上限在数学上不可能 => 必有假阳性。 */
	res->link_over_bound = (res->link_count > res->link_upper_bound);
	if (res->link_over_bound) {
		LOG_ERR("IOPROBE SELFCHECK FAIL links=%u > upper_bound=%u "
			"(N=%d) -> measurement has false positives",
			res->link_count, res->link_upper_bound, PROBE_N);
	}

	LOG_INF("IOPROBE scan done: pins=%d links=%u bound=%u ambiguous=%u "
		"asym=%u same_pin_pairs=%u baseline_low=%u not_restored=%u "
		"not_controlled=%u settle_us_max=%u restore_us_max=%u",
		PROBE_N, res->link_count, res->link_upper_bound,
		res->ambiguous_count, res->asymmetric_count, res->same_pin_pairs,
		res->baseline_low_count, res->not_restored_count,
		res->not_controlled_count, res->settle_us_max,
		res->restore_us_max);

	return 0;
}

int io_probe_index_of(const struct io_probe_result *res, const char *label)
{
	for (int i = 0; i < res->count; i++) {
		if (strcmp(res->pins[i].label, label) == 0) {
			return i;
		}
	}
	return -1;
}

enum io_probe_state io_probe_state_of(const struct io_probe_result *res,
				      const char *label_a, const char *label_b)
{
	int a = io_probe_index_of(res, label_a);
	int b = io_probe_index_of(res, label_b);

	if (a < 0 || b < 0) {
		return IO_PROBE_UNKNOWN;
	}
	return (enum io_probe_state)res->state[a][b];
}

int io_probe_cross_links(const struct io_probe_result *res,
			 const char *const *group_a, int na,
			 const char *const *group_b, int nb,
			 char out[][24], int cap)
{
	int hits = 0;

	for (int i = 0; i < na; i++) {
		int ai = io_probe_index_of(res, group_a[i]);

		if (ai < 0) {
			continue;
		}
		for (int j = 0; j < nb; j++) {
			int bj = io_probe_index_of(res, group_b[j]);

			if (bj < 0) {
				continue;
			}
			if (res->state[ai][bj] != IO_PROBE_LINK) {
				continue;
			}
			if (out != NULL && hits < cap) {
				snprintf(out[hits], 24, "%s<->%s",
					 group_a[i], group_b[j]);
			}
			hits++;
		}
	}
	return hits;
}

/* ============================ 报告打印 ============================ */
/* 全部 ASCII：首跑（2026-09-18）实测中文在 deferred log ring 溢出时乱码、
 * 且行被合并。关键数据一律 ASCII，中文只留在源码注释里。 */

static const char HEXC[] = "0123456789abcdef";

static void dump_legend(const struct io_probe_result *res)
{
	LOG_INF("IOPROBE [LEGEND] idx label ctl base rest restore_us names");
	for (int i = 0; i < res->count; i++) {
		LOG_INF("IOPROBE L%02d %-5s c%d b%c r%c %-4u %s",
			i, res->pins[i].label,
			res->controlled[i] ? 1 : 0,
			res->baseline_low[i] ? 'L' : 'H',
			res->not_restored[i] ? '!'
					     : (res->restored_high[i] ? 'H' : 'L'),
			res->restore_us[i], res->pins[i].names);
		if ((i & 7) == 7) {
			k_msleep(IO_PROBE_LEGEND_GAP_MS);
		}
	}
}

static void dump_matrix(const struct io_probe_result *res)
{
	const int n = res->count;

	LOG_INF("IOPROBE [MATRIX] compact: 2 bits per cell, low 2 bits = first "
		"column of the pair");
	LOG_INF("IOPROBE KEY 0=open(self/same-pin) 1=LINK 2=AMBIG 3=UNKNOWN");

	/* 列序 = LEGEND 的 idx 序，分段打印便于对照 */
	for (int base = 0; base < n; base += 15) {
		int end = MIN(base + 15, n);
		size_t off = 0;

		off += (size_t)snprintf(g_hdrbuf, sizeof(g_hdrbuf), "IOPROBE COLS");
		for (int j = base; j < end; j++) {
			off += (size_t)snprintf(g_hdrbuf + off,
						sizeof(g_hdrbuf) - off,
						" %d:%s", j, res->pins[j].label);
		}
		LOG_INF("%s", g_hdrbuf);
		k_msleep(IO_PROBE_LINE_GAP_MS);
	}

	LOG_INF("IOPROBE MATRIX rows follow: M <row_idx>/<total> <label> <hex>");
	k_msleep(IO_PROBE_LINE_GAP_MS);

	for (int i = 0; i < n; i++) {
		size_t off = 0;

		off += (size_t)snprintf(g_rowbuf, sizeof(g_rowbuf),
					"IOPROBE M %02d/%02d %-5s ",
					i, n, res->pins[i].label);
		for (int j = 0; j < n; j += 2) {
			int c0 = cell_code(res->state[i][j]);
			int c1 = (j + 1 < n) ? cell_code(res->state[i][j + 1]) : 0;

			g_rowbuf[off++] = HEXC[(c1 << 2) | c0];
		}
		g_rowbuf[off] = '\0';
		LOG_INF("%s", g_rowbuf);
		k_msleep(IO_PROBE_LINE_GAP_MS);
	}
	LOG_INF("IOPROBE MATRIX end: %d rows printed (= pins), idx/total on each row",
		n);
}

static void dump_links(const struct io_probe_result *res)
{
	int printed = 0;
	int total = 0;

	for (int i = 0; i < res->count; i++) {
		for (int j = i + 1; j < res->count; j++) {
			if (res->state[i][j] != IO_PROBE_LINK) {
				continue;
			}
			total++;
			if (printed < IO_PROBE_MAX_LINKS) {
				LOG_INF("IOPROBE LINK %s(%s) <-> %s(%s)",
					res->pins[i].names, res->pins[i].label,
					res->pins[j].names, res->pins[j].label);
				printed++;
				k_msleep(IO_PROBE_LINE_GAP_MS);
			}
		}
	}
	if (total > printed) {
		LOG_WRN("IOPROBE LINK list truncated: %d of %d shown",
			printed, total);
	}
	/* 关键结论行：前后留空 + 放在本节最后，避免被相邻行合并。 */
	LOG_INF("IOPROBE");
	LOG_INF("IOPROBE total measured links (undirected) = %d (upper_bound=%u)",
		total, res->link_upper_bound);
	LOG_INF("IOPROBE");
	k_msleep(IO_PROBE_LINE_GAP_MS);
}

static void dump_baseline_low(const struct io_probe_result *res)
{
	if (res->baseline_low_count == 0) {
		LOG_INF("IOPROBE [BASELINE_LOW] none: all %d candidate pins read HIGH "
			"at baseline", res->count);
		return;
	}
	LOG_INF("IOPROBE [BASELINE_LOW] %u pin(s) held LOW externally at baseline -> "
		"they produce NO link evidence anywhere in the matrix:",
		res->baseline_low_count);
	for (int i = 0; i < res->count; i++) {
		if (!res->baseline_low[i]) {
			continue;
		}
		LOG_INF("IOPROBE BASELINE_LOW %s (%s)", res->pins[i].label,
			res->pins[i].names);
		k_msleep(IO_PROBE_LINE_GAP_MS);
	}
}

static void dump_anomalies(const struct io_probe_result *res)
{
	int asym_shown = 0;

	for (int i = 0; i < res->count; i++) {
		if (!res->cfg_ok[i]) {
			LOG_ERR("IOPROBE ANOMALY cfg-failed %s", res->pins[i].label);
		}
		if (!res->controlled[i]) {
			LOG_ERR("IOPROBE ANOMALY pin-not-controlled %s (%s)",
				res->pins[i].label, res->pins[i].names);
		}
		if (res->not_restored[i]) {
			LOG_ERR("IOPROBE ANOMALY pin-not-restored %s (%s) "
				"restore_us=%u (=> excluded from driving and from "
				"link counting)",
				res->pins[i].label, res->pins[i].names,
				res->restore_us[i]);
		}
	}

	/* 不对称对：首跑实测 2624 条，逐条打印会把 deferred log ring 撑爆
	 * （丢整行 + 行合并）。这里只逐条打前 IO_PROBE_MAX_ASYM_PRINT 条，
	 * 总数一定打印。 */
	for (int i = 0; i < res->count; i++) {
		for (int j = i + 1; j < res->count; j++) {
			bool a = (res->state[i][j] == IO_PROBE_LINK);
			bool b = (res->state[j][i] == IO_PROBE_LINK);

			if (a == b) {
				continue;
			}
			if (asym_shown < IO_PROBE_MAX_ASYM_PRINT) {
				LOG_WRN("IOPROBE ASYM %s(%s) %s <-> %s(%s) %s",
					res->pins[i].label, res->pins[j].label,
					a ? "LINK" : "OPEN",
					res->pins[j].label, res->pins[i].label,
					b ? "LINK" : "OPEN");
				asym_shown++;
				k_msleep(IO_PROBE_LINE_GAP_MS);
			}
		}
	}
	LOG_WRN("IOPROBE asymmetric pairs = %u (first %d printed)",
		res->asymmetric_count, asym_shown);
}

static void dump_same_pin_decl(void)
{
	LOG_INF("IOPROBE [SAME_PIN] DT-inherent same-PAD pairs (declared, NOT measured)");
	LOG_INF("IOPROBE [SAME_PIN] each pair is ONE silicon pad; it never appears as two "
		"matrix cells and is NEVER the jumper you soldered:");
	for (size_t k = 0; k < ARRAY_SIZE(same_pin_groups); k++) {
		LOG_INF("IOPROBE SAME_PIN %s == %s",
			same_pin_groups[k][0], same_pin_groups[k][1]);
		k_msleep(IO_PROBE_LINE_GAP_MS);
	}
}

void io_probe_dump_all(const struct io_probe_result *res)
{
	LOG_INF("IOPROBE ================ IO INTERCONNECT PROBE REPORT ================");
	LOG_INF("IOPROBE method: drive-LOW adjacency scan (never drive HIGH); "
		"other pins high-Z input + internal pull-up");
	LOG_INF("IOPROBE pins=%d links=%u bound=%u ambiguous=%u asym=%u same_pin_pairs=%u",
		res->count, res->link_count, res->link_upper_bound,
		res->ambiguous_count, res->asymmetric_count, res->same_pin_pairs);
	LOG_INF("IOPROBE baseline_low=%u not_restored=%u not_controlled=%u "
		"settle_us_max=%u restore_us_max=%u",
		res->baseline_low_count, res->not_restored_count,
		res->not_controlled_count, res->settle_us_max, res->restore_us_max);
	LOG_INF("IOPROBE SELFCHECK links<=bound : %s",
		res->link_over_bound ? "FAIL" : "OK");

	dump_same_pin_decl();
	dump_legend(res);
	dump_matrix(res);
	dump_links(res);
	dump_baseline_low(res);
	dump_anomalies(res);

	LOG_INF("IOPROBE");
	LOG_INF("IOPROBE ===== SUMMARY: pins=%d links=%u bound=%u asym=%u "
		"baseline_low=%u not_restored=%u =====",
		res->count, res->link_count, res->link_upper_bound,
		res->asymmetric_count, res->baseline_low_count,
		res->not_restored_count);
	LOG_INF("IOPROBE");
	LOG_INF("IOPROBE ====================== END OF REPORT ======================");
}

/* ============================ ZTEST ============================ */

/* 候选引脚表里必须存在的几根脚（正对照 + 用户声称的两组） */
#define L_I3C5_SCL "PJ10"
#define L_I3C5_SDA "PJ11"
#define L_I3C6_SCL "PJ08"
#define L_I3C6_SDA "PJ09"

/* 扫描只做一次（套件级 setup），各用例读同一份缓存结果 —— 用例彼此独立，
 * 不依赖执行顺序（ztest 按链接序跑，本套件不假设任何顺序）。 */
static void *io_probe_suite_setup(void)
{
	io_probe_scan(&g_res);
	return NULL;
}

ZTEST_SUITE(io_probe, NULL, io_probe_suite_setup, NULL, NULL, NULL);

/* 1. 每根候选脚都必须真的受我们控制（拉低后自身读回为低）。
 *    不受控 => 方法在这根脚上无效 => 大声 FAIL。 */
ZTEST(io_probe, pin_controllable)
{
	int bad = 0;

	for (int i = 0; i < g_res.count; i++) {
		if (!g_res.controlled[i]) {
			LOG_ERR("IOPROBE pin NOT controlled: %s (%s)",
				g_res.pins[i].label, g_res.pins[i].names);
			bad++;
		}
		if (!g_res.cfg_ok[i]) {
			LOG_ERR("IOPROBE pin config FAILED: %s", g_res.pins[i].label);
			bad++;
		}
	}

	ZT_INFO("pin_controllable", "pins=%d not_controlled=%d",
		g_res.count, bad);
	zassert_equal(bad, 0,
		      "pin_controllable FAIL: %d candidate pin(s) not driven to LOW "
		      "(see IOPROBE ANOMALY pin-not-controlled)",
		      bad);
}

/* 1b. 还原判据（★ 首跑暴露的缺陷 A）：每根**基线为高**的候选脚，被驱动低
 *     之后必须能还原回高。还原不回高的脚会让此后的每一行都把它误读成低
 *     （级联污染，首跑实测出"完美下三角"、links 超过理论上限）。
 *     这是独立 FAIL 判据：只要有一个 not_restored，矩阵就不可采信。 */
ZTEST(io_probe, pin_restorable)
{
	int bad = 0;

	for (int i = 0; i < g_res.count; i++) {
		if (g_res.not_restored[i]) {
			LOG_ERR("IOPROBE pin NOT restorable: %s (%s) restore_us=%u",
				g_res.pins[i].label, g_res.pins[i].names,
				g_res.restore_us[i]);
			bad++;
		}
	}

	ZT_INFO("pin_restorable", "pins=%d not_restored=%d settle_us_max=%u "
		"restore_us_max=%u", g_res.count, bad,
		g_res.settle_us_max, g_res.restore_us_max);
	zassert_equal(bad, 0,
		      "pin_restorable FAIL: %d pin(s) driven LOW could not be "
		      "restored to HIGH (=> cascade pollution; matrix untrustworthy)",
		      bad);
}

/* 2. 阳性对照：I3C5 <-> I3C6 这对**已确认接好**的跨核跳线必须被扫出来。
 *    扫不出来 => 方法失效（或线掉了）=> 用例 FAIL，绝不吐个空矩阵装正常。 */
ZTEST(io_probe, positive_control_i3c_link)
{
	enum io_probe_state scl = io_probe_state_of(&g_res, L_I3C5_SCL, L_I3C6_SCL);
	enum io_probe_state sda = io_probe_state_of(&g_res, L_I3C5_SDA, L_I3C6_SDA);

	ZT_INFO("positive_control_i3c_link", "scl_%s_%s=%d sda_%s_%s=%d",
		L_I3C5_SCL, L_I3C6_SCL, (int)scl,
		L_I3C5_SDA, L_I3C6_SDA, (int)sda);

	if (scl != IO_PROBE_LINK || sda != IO_PROBE_LINK) {
		LOG_ERR("IOPROBE POSITIVE CONTROL FAILED: expected PJ10<->PJ08 and "
			"PJ11<->PJ09 to read LINK, got scl=%d sda=%d. The measurement "
			"method or the wiring itself is broken - do NOT trust this matrix.",
			(int)scl, (int)sda);
	}

	zassert_equal(scl, IO_PROBE_LINK,
		      "positive control FAILED: %s-%s not LINK (state=%d) - method broken",
		      L_I3C5_SCL, L_I3C6_SCL, (int)scl);
	zassert_equal(sda, IO_PROBE_LINK,
		      "positive control FAILED: %s-%s not LINK (state=%d) - method broken",
		      L_I3C5_SDA, L_I3C6_SDA, (int)sda);
}

/* 3. 对称性：i->j 与 j->i 判定应当一致。不一致单独报（可能中间有二极管/驱动器），
 *    不作为失败 —— 矩阵本身已经把两侧都记下来了。 */
ZTEST(io_probe, symmetry_report)
{
	ZT_INFO("symmetry_report", "asymmetric_pairs=%u links=%u bound=%u "
		"settle_us_max=%u restore_us_max=%u",
		g_res.asymmetric_count, g_res.link_count, g_res.link_upper_bound,
		g_res.settle_us_max, g_res.restore_us_max);
	LOG_INF("IOPROBE symmetry: asymmetric_pairs=%u (first %d printed above)",
		g_res.asymmetric_count, IO_PROBE_MAX_ASYM_PRINT);
	LOG_INF("IOPROBE note: asymmetry is reported, never failed (a diode or "
		"driver in between is possible); both directions are in the matrix");
	/* 矩阵口径自检：逐格都归类过，且不对称不可能多于连通（每个不对称对
	 * 至少贡献一个方向的 LINK）。这两条是"扫描真的跑完了"的廉价不变量。 */
	zassert_equal(g_res.count, PROBE_N,
		      "candidate pin table empty or setup did not run");
	zassert_true(g_res.asymmetric_count <= g_res.link_count,
		     "asymmetric pairs (%u) > links (%u): matrix self-contradictory",
		     g_res.asymmetric_count, g_res.link_count);
	/* ★ 上限自检：无向对上限 = N*(N-1)/2。超过 = 必有假阳性。 */
	zassert_false(g_res.link_over_bound,
		      "links (%u) exceed the undirected upper bound (%u): "
		      "the scan has false positives - the measurement itself is broken",
		      g_res.link_count, g_res.link_upper_bound);
}

/* 4+5. 用户声称的两组接线 —— 只**报告**结论（CONNECTED / PARTIAL / NOT_FOUND），
 *       不因"实际与声称不符"判失败：测出来不一样正是本工程要交付的信息。
 *       只有"测不出方法有效性"（第 1/2 项）才 FAIL。 */
ZTEST(io_probe, claim_i2c234_bus)
{
	static const char *const scl[3] = { "PB04", "PB15", "PE02" };
	static const char *const sda[3] = { "PB01", "PB14", "PB00" };
	int scl_pairs = 0, sda_pairs = 0, scl_hit = 0, sda_hit = 0;

	for (int a = 0; a < 3; a++) {
		for (int b = a + 1; b < 3; b++) {
			scl_pairs++;
			if (io_probe_state_of(&g_res, scl[a], scl[b]) == IO_PROBE_LINK) {
				scl_hit++;
				LOG_INF("IOPROBE   SCL link %s <-> %s", scl[a], scl[b]);
			}
			sda_pairs++;
			if (io_probe_state_of(&g_res, sda[a], sda[b]) == IO_PROBE_LINK) {
				sda_hit++;
				LOG_INF("IOPROBE   SDA link %s <-> %s", sda[a], sda[b]);
			}
		}
	}

	LOG_INF("IOPROBE CLAIM i2c2/i2c3/i2c4 tied together:");
	LOG_INF("IOPROBE   SCL {PB04(i2c2),PB15(i2c3),PE02(i2c4)} linked %d/%d  -> %s",
		scl_hit, scl_pairs,
		(scl_hit == scl_pairs) ? "CONNECTED" : (scl_hit ? "PARTIAL" : "NOT_FOUND"));
	LOG_INF("IOPROBE   SDA {PB01(i2c2),PB14(i2c3),PB00(i2c4)} linked %d/%d  -> %s",
		sda_hit, sda_pairs,
		(sda_hit == sda_pairs) ? "CONNECTED" : (sda_hit ? "PARTIAL" : "NOT_FOUND"));
	LOG_INF("IOPROBE   note: i2cfN.SCLO/SDAO is the SAME PAD as i2cN (DT-inherent, "
		"see SAME_PIN section); deduped by physical pin, not counted above");

	ZT_INFO("claim_i2c234_bus", "scl=%d/%d sda=%d/%d verdict=%s",
		scl_hit, scl_pairs, sda_hit, sda_pairs,
		(scl_hit == scl_pairs && sda_hit == sda_pairs) ? "CONNECTED" :
		((scl_hit || sda_hit) ? "PARTIAL" : "NOT_FOUND"));
}

ZTEST(io_probe, claim_spi_spif_interconnect)
{
	/* SPI 侧：spi1 + spi3 + spi4 的全部信号脚 */
	static const char *const spi[] = {
		"PA00", "PA01", "PA02", "PA03",
		"PG14", "PG15", "PG03", "PE01", "PE00", "PI08", "PG02",
		"PG13", "PG12",
		"PQ00", "PQ03", "PQ04", "PQ05", "PQ06", "PQ02", "PQ01", "PI00",
	};
	/* SPIF 侧：spif1..4 的 in/out 全部信号脚 */
	static const char *const spif[] = {
		"PI10", "PG04", "PF14", "PG06", "PH00", "PH02",
		"PG00", "PF15", "PF13", "PH01", "PH03",
		"PG08", "PH04", "PH06", "PG12", "PG09", "PH05", "PH07", "PG13",
		"PI02", "PF08", "PF04", "PF02", "PF10", "PF06",
		"PF09", "PF05", "PF03", "PF11", "PF07",
		"PH12", "PH10", "PH08", "PF12", "PG07",
		"PH13", "PH11", "PH09", "PG01", "PG05",
	};
	static char hits[24][24];
	int n = io_probe_cross_links(&g_res, spi, (int)ARRAY_SIZE(spi),
				     spif, (int)ARRAY_SIZE(spif), hits, 24);

	LOG_INF("IOPROBE CLAIM SPI <-> SPIF IOs jumpered together: %d measured cross-links -> %s",
		n, n ? "CONNECTED" : "NOT_FOUND");
	for (int k = 0; k < n && k < 24; k++) {
		LOG_INF("IOPROBE   SPI<->SPIF %s", hits[k]);
	}
	LOG_INF("IOPROBE   note: these signal pairs are the SAME PAD (DT-inherent, see "
		"SAME_PIN section) and are deduped by physical pin, so they are NOT "
		"counted as cross-links: spif1.CSNO=PI08(spi3.CS0), "
		"spif2.CSNI=PG03(spi3.IO1), spif2.IO2_IN=PG14(spi3.SCK), "
		"spif2.IO2_OUT=PG15(spi3.IO0), spif2.CSNO=PG02(spi3.CS1), "
		"spif2.IO3_OUT=PG13(spi3.CS0 board), spif2.IO3_IN=PG12(spi3.CS1 board), "
		"spif3.CSNO=PI00(spi4.CS0), spif4.CSNI=PQ05(spi4.IO2), "
		"spif4.CSNO=PQ00(spi4.SCK)");

	ZT_INFO("claim_spi_spif_interconnect", "cross_links=%d verdict=%s",
		n, n ? "CONNECTED" : "NOT_FOUND");
}

/* 6. DT 固有同脚关系（声明式，非测量）：i2cfN↔i2cN 等。必须与"跳线连通"
 *    分开呈现，否则用户会把"同一根脚"误当自己接的线。 */
ZTEST(io_probe, same_pin_groups)
{
	ZT_INFO("same_pin_groups", "dt_same_pad_pairs=%zu", ARRAY_SIZE(same_pin_groups));
	LOG_INF("IOPROBE DT-inherent same-pad pairs = %zu (declared, never counted as links)",
		ARRAY_SIZE(same_pin_groups));
	zassert_true(ARRAY_SIZE(same_pin_groups) > 0, "same-pin declaration list is empty");
}

/* 7. 全量报告：同脚声明 + 图例 + 矩阵 + 连线清单 + 异常/不对称。 */
ZTEST(io_probe, dump_full_matrix)
{
	io_probe_dump_all(&g_res);
	zassert_true(g_res.count > 0, "candidate pin table is empty");
}
