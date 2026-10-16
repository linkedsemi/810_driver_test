/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * i2cf_test —— "read 方向在线上到底长什么样" 的**循环波形发生器**（示波器抓取用）。
 *
 * 目的：让总线以固定节奏反复出现 **一笔写 + 一笔读** 的事务，供示波器/逻辑分析仪
 *       稳定触发与译码；标记 GPIO 给出每笔事务的包络(TRIG)与方向(DIR)，用于一眼
 *       分辨 START + (addr<<1|1) 的 LSB 与 DIR 是否一致（"read 在线上是不是读"）。
 *
 * 拓扑（DT 已定，见 app.overlay）：
 *   上游 master = i2c2（alias i2cfupstream），跳线到 i2cf4 输入 SCLI/SDAI(PE03/PB02)
 *   → 被测滤波器 i2cf4 → 输出 SCLO/SDAO(PE02/PB00) → i2c3（片内 target @0x33）。
 *   两相位**都 filter ON**，只切「0x33 是否在白名单」：
 *     偶数轮 A = 授权 0x33（放行/ACK，无中断）；奇数轮 B = 关 ch0=空白名单（拦+NACK+上报）。
 *   PHASE 引脚标记当前相位（低=A / 高=B），一次烧录即可同时观察两种状态。
 *
 * 周期（实测见运行日志，标称 ~420 ms）：
 *   seg1  写 0x33          TRIG 脉冲, DIR=低
 *   seg2  静默 ~50 ms
 *   seg3  读 0x33          TRIG 脉冲, DIR=高
 *   seg4  静默 ~350 ms
 *   相位逐轮交替：偶数轮 A = 授权 0x33（ACK）；奇数轮 B = 不授权（NACK 对照）；
 *   PHASE 引脚标记当前相位（低=A / 高=B），一次烧录即可同时观察两种状态。
 *
 * 标记引脚（PORT A；避开公共 gpio_toggle 占用的 PA05/PA07，且本工程无外设 pinctrl
 * 占用这三脚 —— pwm/cap/spi1/uart4 均 disabled）：
 *   TRIG  = PA06   事务包络：事务前建立、事务后撤销（超前/滞后 10us，脉宽 >> 10us）
 *   DIR   = PA04   写=低 / 读=高
 *   PHASE = PA05   A=低 / 高=B（PA05 由用户指定可用；本工程已摘掉框架的 PA05 toggle）
 *
 * ★ 本文件由 CONFIG_I2CF_WAVE_GEN 保护（默认 n）：不置位时不编入，case_default
 *   构建逐字节不变。本发生器**只发波形、不含任何 zassert 判据**（不判 PASS/FAIL）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "i2cf_test.h"
#include "i2cf_wave.h"

#include <i2c_filter.h>
#include <reg_i2c_filter.h>

LOG_MODULE_REGISTER(i2cf_wave, LOG_LEVEL_INF);

/* 事务目标地址 = 0x33 = 片内 target（overlay 的 `&i2c3 / eeprom@33`，由 main.c 经
 * i2c_target_driver_register() 注册），位于 i2cf4 **输出侧（下游）** —— 对端 ACK
 * 会经滤波器返回本上游 master；因此本地址能造出「从机 ACK」这一条件（相位 B 尤其）。
 *
 * 用途：观察「read + 从机 ACK」之后，这一次 I2CF 是否触发滤波异常（IP/硬件方的预期）。
 * ⚠ 旧值 0x2A 是总线上**无器件**的地址（必然 NACK）⇒ 测不到"ACK 之后"的条件，已弃用。
 * ⚠ 不用 0x50：板上有实体器件（身份不明），且历史同址撞过（KNOWN_ISSUES #110/#111）。 */
#define I2CF_WAVE_ADDR     0x33u

/* 标记引脚（见文件头说明；单点可改） */
#define WAVE_TRIG_PIN      6       /* PA06 */
#define WAVE_DIR_PIN       4       /* PA04 */
#define WAVE_PHASE_PIN     5       /* PA05（用户指定：确定可用；本工程已把框架的 PA05 toggle 摘掉） */

/* 标记电平超前/滞后量（us）：保证标记沿与 START/STOP 沿不重叠 */
#define WAVE_MARK_LEAD_US  10

/* 段间静默与 ISR 记账落定时间（ms） */
#define WAVE_GAP_TXN_MS    30      /* 写、读之间（另加 settle 共 ~50ms） */
#define WAVE_GAP_FRAME_MS  350     /* 帧间 */
#define WAVE_ISR_SETTLE_MS 20      /* 事务后等中断计数落定 */

static const struct device *wave_gpio = DEVICE_DT_GET(DT_NODELABEL(gpioa));

/* ISR 观测：回调在中断上下文执行，只计数、不打日志（SPEC_REFERENCE §6.1.2） */
static volatile uint32_t wave_isr_count;

/*
 * ★ 串口兜底取证：把最近 WAVE_REC_N 轮的结果留在 RAM，供 GDB 直接读回。
 *   本环境 UART 可能被外部进程独占（实测 COM16 被某进程以独占方式打开 ⇒ 串口
 *   采集拿不到），此时靠这段内存取证（gdb `x/` 读 wave_recs）。理由同本族先例：
 *   "串口无输出时可用 GDB 读内存取结果"（qsh-test-compare §3）。
 *   字段全为 32 位、无 padding；索引 (cyc % WAVE_REC_N)。
 *   wave_rec_idx = 已写入的总轮数（单调递增，判断固件确实跑了多少轮）。
 */
#define WAVE_REC_N 32
struct wave_rec {
	uint32_t cyc;
	int32_t  ret_w;
	uint32_t isr_w;
	int32_t  ret_r;
	uint32_t isr_r;
	uint32_t rd;          /* 读回 4 字节（大端拼字）—— 判"真 ACK vs 假 ACK(0xFF)" */
	uint32_t idle_w;      /* 帧前静默期总线电平 (scl<<1)|sda —— 判"总线是否被拉住" */
	uint32_t idle_r;      /* 读前静默期总线电平 (scl<<1)|sda */
	uint32_t period_ms;
};
struct wave_rec wave_recs[WAVE_REC_N];
uint32_t wave_rec_idx;

static void wave_isr_cb(const struct device *dev, uint32_t idx, void *ud, void *dd)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(idx);
	ARG_UNUSED(ud);
	ARG_UNUSED(dd);
	wave_isr_count++;
}

static inline void trig_set(int v)
{
	gpio_pin_set(wave_gpio, WAVE_TRIG_PIN, v);
}

static inline void dir_set(int v)
{
	gpio_pin_set(wave_gpio, WAVE_DIR_PIN, v);
}

static inline void phase_set(int v)
{
	gpio_pin_set(wave_gpio, WAVE_PHASE_PIN, v);
}

/*
 * 总线 pad 电平观测（判"总线被谁拉住"）：读 i2c2 的 scl/sda gpios
 * （DT `scl-gpios`/`sda-gpios` = PB04/PB01）——与 i2c_ls_bus_status() 读的是**同一对 pad**，
 * 故本观测与驱动"总线是否空闲"的判据同源。返回值 = (scl<<1)|sda。
 */
static const struct gpio_dt_spec wave_bus_scl = GPIO_DT_SPEC_GET(DT_NODELABEL(i2c2), scl_gpios);
static const struct gpio_dt_spec wave_bus_sda = GPIO_DT_SPEC_GET(DT_NODELABEL(i2c2), sda_gpios);

static uint32_t wave_bus_lines(void)
{
	int scl = gpio_pin_get_dt(&wave_bus_scl);
	int sda = gpio_pin_get_dt(&wave_bus_sda);

	return ((uint32_t)(scl > 0) << 1) | (uint32_t)(sda > 0);
}

/*
 * 空白名单 + 全通道关闭 + 注册 ISR + 交管脚给滤波器。
 * 直接调驱动 API、不 zassert（波形模式不是 ztest 上下文）。
 */
static void wave_setup(const struct device *dev)
{
	uint32_t bm[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];

	(void)linkedsemi_i2c_filter_cold_reset(dev);
	for (uint8_t i = 0; i < I2CF_CHANNEL_NUM; i++) {
		(void)linkedsemi_i2c_filter_enable_channel(dev, i, false);
	}
	(void)linkedsemi_i2c_filter_register_callback(dev, 0, wave_isr_cb, NULL);
	(void)linkedsemi_i2c_filter_switch_to_filter(dev);

	/*
	 * ★ 本轮唯一变化：把 I2CF_WAVE_ADDR 授权进白名单（filter 仍 ON）。
	 *   做法同 i2cf_test.c::filter_passthrough_whitelist —— 全命令位 bitmap
	 *   + fill_bitmap(dev, 0, addr, bm)（fill_bitmap 自动置该通道 enable）。
	 *   目的：让 filter ON 相位下 0x33 被**放行**（下游 target ACK），
	 *   以对照空白名单时的"拦 + 报"（isr=1/ret=-5）。
	 */
	for (int i = 0; i < LINKEDSEMI_I2C_F_REMAP_SIZE_U32; i++) {
		bm[i] = 0xffffffffu;
	}
	(void)linkedsemi_i2c_filter_fill_bitmap(dev, 0, I2CF_WAVE_ADDR, bm);
}

void i2cf_wave_gen_run(void)
{
	const struct device *dev = i2cf_dev(3);            /* i2cf4：被测滤波器 */
	const struct device *up = DEVICE_DT_GET(DT_ALIAS(i2cfupstream));   /* i2c2：上游 master */
	uint8_t v = 0;
	uint32_t cyc = 0;

	gpio_pin_configure(wave_gpio, WAVE_TRIG_PIN, GPIO_OUTPUT_LOW);
	gpio_pin_configure(wave_gpio, WAVE_DIR_PIN, GPIO_OUTPUT_LOW);
	gpio_pin_configure(wave_gpio, WAVE_PHASE_PIN, GPIO_OUTPUT_LOW);

	wave_setup(dev);

	LOG_INF("[WAVE] start addr=%#x TRIG=PA%d DIR=PA%d PHASE=PA%d",
		I2CF_WAVE_ADDR, WAVE_TRIG_PIN, WAVE_DIR_PIN, WAVE_PHASE_PIN);

	for (;;) {
		const bool phaseB = (cyc & 1u);
		int64_t t_cyc = k_uptime_get();
		int64_t t0, dt_w, dt_r;
		uint32_t isr_w, isr_r;
		int ret_w, ret_r;
		uint8_t rd[4];
		uint32_t idle_w, idle_r;

		/*
		 * 两相位**都 filter ON**，只切「0x33 是否在白名单」：
		 *   A（PA05 低）= 授权 0x33 ⇒ 应放行/ACK，无中断
		 *   B（PA05 高）= 关 ch0（等价空白名单）⇒ 拦 + NACK + 上报
		 * 不掺入"关滤波器"这一 810 未解怪象（i2c_failure_triage.md §9.4）。
		 */
		(void)linkedsemi_i2c_filter_en(dev, true, true, false);
		if (phaseB) {
			(void)linkedsemi_i2c_filter_enable_channel(dev, 0, false);
		} else {
			uint32_t bm[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];

			for (int i = 0; i < LINKEDSEMI_I2C_F_REMAP_SIZE_U32; i++) {
				bm[i] = 0xffffffffu;
			}
			(void)linkedsemi_i2c_filter_fill_bitmap(dev, 0, I2CF_WAVE_ADDR, bm);
		}
		phase_set(phaseB ? 1 : 0);
		wave_isr_count = 0;
		idle_w = wave_bus_lines();     /* 帧前静默期：总线 pad 电平（判"被拉住"） */

		/* ---- seg1：写方向 ---- */
		trig_set(1);
		dir_set(0);
		k_busy_wait(WAVE_MARK_LEAD_US);
		t0 = k_uptime_get();
		ret_w = i2c_write(up, &v, 1, I2CF_WAVE_ADDR);
		dt_w = k_uptime_get() - t0;
		k_busy_wait(WAVE_MARK_LEAD_US);
		trig_set(0);
		k_msleep(WAVE_ISR_SETTLE_MS);
		isr_w = wave_isr_count;
		wave_isr_count = 0;
		k_msleep(WAVE_GAP_TXN_MS);
		idle_r = wave_bus_lines();     /* 读前静默期：总线 pad 电平 */

		/* ---- seg3：读方向（读 4 字节，记录内容以辨真/假 ACK） ---- */
		trig_set(1);
		dir_set(1);
		k_busy_wait(WAVE_MARK_LEAD_US);
		t0 = k_uptime_get();
		rd[0] = rd[1] = rd[2] = rd[3] = 0;   /* 失败时保持 0，避免读到脏值 */
		ret_r = i2c_read(up, rd, sizeof(rd), I2CF_WAVE_ADDR);
		dt_r = k_uptime_get() - t0;
		k_busy_wait(WAVE_MARK_LEAD_US);
		trig_set(0);
		k_msleep(WAVE_ISR_SETTLE_MS);
		isr_r = wave_isr_count;

		/* ---- seg4：帧间隔 ---- */
		k_msleep(WAVE_GAP_FRAME_MS);

		LOG_INF("[WAVE] cyc=%u ph=%c | W ret=%d dt=%dms isr=%u | R ret=%d dt=%dms isr=%u rd=%02x%02x%02x%02x | bus(scl,sda)=%u,%u/%u,%u | period=%dms",
			(unsigned)cyc, phaseB ? 'B' : 'A',
			ret_w, (int)dt_w, (unsigned)isr_w,
			ret_r, (int)dt_r, (unsigned)isr_r,
			rd[0], rd[1], rd[2], rd[3],
			(unsigned)(idle_w >> 1) & 1u, (unsigned)idle_w & 1u,
			(unsigned)(idle_r >> 1) & 1u, (unsigned)idle_r & 1u,
			(int)(k_uptime_get() - t_cyc));

		/* 内存留证（串口不可用时靠 gdb 读回；见文件头 wave_rec 说明） */
		{
			struct wave_rec *r = &wave_recs[cyc % WAVE_REC_N];

			r->cyc = cyc;
			r->ret_w = ret_w;
			r->isr_w = isr_w;
			r->ret_r = ret_r;
			r->isr_r = isr_r;
			r->rd = ((uint32_t)rd[0] << 24) | ((uint32_t)rd[1] << 16) |
				((uint32_t)rd[2] << 8) | (uint32_t)rd[3];
			r->idle_w = idle_w;
			r->idle_r = idle_r;
			r->period_ms = (uint32_t)(k_uptime_get() - t_cyc);
			wave_rec_idx = cyc + 1u;
		}
		cyc++;
	}
}
