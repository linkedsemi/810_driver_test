/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 纯 I2C 波形质量测试 —— cpu2 master 侧（i2c5 @400K，从机地址 0x58）。
 *
 * 被测对象：i2c5（i2c@40084000）I2C 主机，400K 总线速率，引脚
 * I2C5_scl_PJ10 / I2C5_sda_PJ11（board 级 pinctrl 宏生成，init 自动应用）。
 * 对端：cpu1 侧 i2c6 从机。
 *
 * i2c_wave：100 轮 × 16B 写（'W' <len> <0..15>）+ 16B 读（0xAA..0xB9），
 * 轮间 100ms 静默——波形每 ~100ms 一个突发，示波器单次触发即可抓拍，
 * 总时长 ~10s。套件 after 钩子无条件发 'P'（成败路径均下发）让 slave
 * 判定并结束其套件。
 *
 * 速率两态（CONFIG_I2C_PURE_WAVE_ALT_RATE，由 case 的 controller.conf 选）：
 *   - 默认 n（case_pure_i2c_400k）：固定 400K；
 *   - =y（case_pure_i2c_alt_100k_400k）：逐轮交替 100K/400K，并分组计时
 *     上报 100K/400K 平均传输耗时与比值（自证运行时速率切换生效）。
 *
 * ztest 原生框架版：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），套件内 ZTEST(i2c_pure_ctrl, i2c_wave) 为唯一
 * 测试项；结果记账由 ztest_glue 的 zt_run_all 按 ztest stats 自动完成，
 * 数值上报走 ZT_INFO 约定行（§13.4）。
 */

#include "i2c_pure_test_protocol.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/time_units.h>	/* k_cyc_to_us_floor32 */
#include <zephyr/ztest.h>

#include "ztest_glue.h"
#include "rlog.h"
#include "test_results.h"

LOG_MODULE_REGISTER(i2c_pure_master_test, LOG_LEVEL_INF);

const struct device *master_i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c5));

/* ztest 测试线程栈由 CONFIG_ZTEST_STACK_SIZE 单独给出，wbuf/rbuf 放 .bss
 * 不占测试线程栈（trng 样板同款约定）。 */
static uint8_t ctrl_wbuf[2 + I2C_WAVE_LEN];
static uint8_t ctrl_rbuf[I2C_WAVE_LEN];

/* after 钩子：无论测试体成败（zassert 失败长跳转后框架仍执行
 * suite->after）都无条件发 'P' 通知 slave 判定收尾——与旧固件
 * "循环后无条件发 'P'"语义逐点对齐，保证 slave 侧等待循环必被唤醒。
 * 返回错误忽略（钩子里不 zassert），slave 侧判定结构自带兜底。 */
static void ctrl_suite_after(void *fixture)
{
	const uint8_t cmd = CMD_PRINT_SUMMARY;

	ARG_UNUSED(fixture);

	(void)i2c_write(master_i2c_dev, &cmd, 1, I2C_PURE_SLAVE_ADDR);
	/* 发 'P' 后留 20ms 让 slave 先打印判定（旧固件同款时序） */
	k_sleep(K_MSEC(20));
}

ZTEST_SUITE(i2c_pure_ctrl, NULL, NULL, NULL, ctrl_suite_after, NULL);

/* 纯 I2C 波形：多轮 16B 写读循环（i2c_wave）。
 * 断言失败即测试失败（zassert 打印 Assertion failed 详情，harness 归因
 * 进报告 reason 列）；'P' 命令由套件 after 钩子下发（成败路径均执行）。 */
ZTEST(i2c_pure_ctrl, i2c_wave)
{
	int ret;

	/* 传输前显式配置速率/模式（官方样例做法：ls-i2c 按 dev_data->config
	 * 的速率档计算总线分频，应用层必须调用 i2c_configure）。
	 * 注意 i2c_runtime_configure 只记账（i2c_ls.c），真正的 TIMINGR 在
	 * 每次 i2c_ls_transfer 的 i2c_reenable(dev, true) 里按该速率档刷写
	 * ⇒ i2c_configure 是延迟生效，"每轮改一次速率"在机制上成立。 */
	ret = i2c_configure(master_i2c_dev,
			    I2C_SPEED_SET(I2C_SPEED_FAST) | I2C_MODE_CONTROLLER);
	zassert_equal(ret, 0, "i2c_configure rc=%d", ret);

#if defined(CONFIG_I2C_PURE_WAVE_ALT_RATE)
	/* 逐轮交替 100K/400K（偶数轮 100K、奇数轮 400K），并逐轮掐
	 * i2c_write+i2c_read 的传输周期数，按速率分组累计 —— 比值是
	 * "运行时速率切换是否真正生效"的自证（只调 i2c_configure 不算证据）。
	 * k_cycle_get_32 在 600MHz 下回绕周期 7.16s，单轮传输 < 10ms ⇒
	 * 32 位差值足够。 */
	uint32_t cyc_100k = 0, cyc_400k = 0;
	uint32_t n_100k = 0, n_400k = 0;
#endif

	for (int round = 0; round < I2C_WAVE_ROUNDS; round++) {
#if defined(CONFIG_I2C_PURE_WAVE_ALT_RATE)
		/* 每轮先把速率档切到本轮目标值（下一次 transfer 生效）。 */
		uint32_t speed = (round & 1) ? I2C_SPEED_FAST
					     : I2C_SPEED_STANDARD;
		uint32_t t0;

		ret = i2c_configure(master_i2c_dev,
				    I2C_SPEED_SET(speed) | I2C_MODE_CONTROLLER);
		zassert_equal(ret, 0, "i2c_configure round=%d rc=%d",
			      round, ret);
		t0 = k_cycle_get_32();
#endif
		ctrl_wbuf[0] = CMD_WRITE_PATTERN;
		ctrl_wbuf[1] = I2C_WAVE_LEN;
		for (size_t i = 0; i < I2C_WAVE_LEN; i++) {
			ctrl_wbuf[2 + i] = (uint8_t)i;
		}

		ret = i2c_write(master_i2c_dev, ctrl_wbuf, sizeof(ctrl_wbuf),
				I2C_PURE_SLAVE_ADDR);
		zassert_equal(ret, 0, "write round=%d rc=%d", round, ret);

		ret = i2c_read(master_i2c_dev, ctrl_rbuf, sizeof(ctrl_rbuf),
			       I2C_PURE_SLAVE_ADDR);
		zassert_equal(ret, 0, "read round=%d rc=%d", round, ret);

		for (size_t i = 0; i < I2C_WAVE_LEN; i++) {
			zassert_equal(ctrl_rbuf[i],
				      (uint8_t)(TGT_READ_PATTERN_BASE + i),
				      "byte[%u]:0x%02X!=0x%02X",
				      (unsigned)i, ctrl_rbuf[i],
				      TGT_READ_PATTERN_BASE + i);
		}

#if defined(CONFIG_I2C_PURE_WAVE_ALT_RATE)
		{
			uint32_t dt = k_cycle_get_32() - t0;

			if (speed == I2C_SPEED_STANDARD) {
				cyc_100k += dt;
				n_100k++;
			} else {
				cyc_400k += dt;
				n_400k++;
			}
		}
#endif
		k_sleep(K_MSEC(I2C_WAVE_ROUND_DELAY_MS));
	}

#if defined(CONFIG_I2C_PURE_WAVE_ALT_RATE)
	/* 分组平均传输耗时（us）与 100K/400K 比值上报；ZT_INFO 第一参必须是
	 * 字符串字面量（qsh-test-gen §1.4）。 */
	uint32_t avg_us_100k = n_100k ? k_cyc_to_us_floor32(cyc_100k / n_100k) : 0;
	uint32_t avg_us_400k = n_400k ? k_cyc_to_us_floor32(cyc_400k / n_400k) : 0;
	uint32_t ratio_x100 = avg_us_400k ?
		(uint32_t)(((uint64_t)avg_us_100k * 100U) / avg_us_400k) : 0;

	ZT_INFO("i2c_wave",
		"rounds=%d,delay_ms=%d,rate_khz=100alt400,n_100k=%u,n_400k=%u,"
		"avg_us_100k=%u,avg_us_400k=%u,ratio_100k_over_400k=%u.%02u",
		I2C_WAVE_ROUNDS, I2C_WAVE_ROUND_DELAY_MS,
		n_100k, n_400k, avg_us_100k, avg_us_400k,
		ratio_x100 / 100U, ratio_x100 % 100U);

	/* 自证：100K 组传输耗时必须明显大于 400K 组，比值落在 [2.5, 6.0]
	 * （理论 ~4；每笔传输的固定软件开销会稀释比值，下界 2.5 已放宽）。
	 * 超界 = 运行时速率切换未真正生效，须查根因，不放松本判据。 */
	zassert_true(avg_us_100k > avg_us_400k,
		     "100K avg_us=%u not > 400K avg_us=%u",
		     avg_us_100k, avg_us_400k);
	zassert_true(ratio_x100 >= 250U && ratio_x100 <= 600U,
		     "100K/400K transfer-time ratio %u.%02u out of [2.50,6.00]",
		     ratio_x100 / 100U, ratio_x100 % 100U);
#else
	ZT_INFO("i2c_wave", "rounds=%d,delay_ms=%d,rate_khz=400",
		I2C_WAVE_ROUNDS, I2C_WAVE_ROUND_DELAY_MS);
#endif
}

void test_main(void)
{
	zt_init("CTRL");

	/* 单出口（SPEC_REFERENCE §6.9.3）：本核是**主核**（唯一串口出口），收下
	 * 从核经 RLOG 送来的 token 行，加**核信封**原样转发。信封是分拣凭据
	 * （§6.9.4）：采集侧按 `<前缀>:TGT:` 把它们搬回 serial_TGT.log，重放输入
	 * 与"两核各自本地输出"同形（信封加在中继侧 ⇒ 不占从核帧长，见 rlog.h）。 */
	rlog_primary_init();
	rlog_set_relay_envelope(I2C_PURE_TEST_PREFIX, "TGT");

	LOG_INF("[CPU2 I2C_PURE_CTRL] master test started");   /* 角色 banner（harness 依赖） */

	zt_run_all();

	/* 等从核 DONE（期间反复 drain，给从核 FIFO 腾空间）。fail-closed（§6.9.3）：
	 * 没等到 DONE、或接收侧跳号/坏帧/缓冲满 ⇒ 判失败 —— 绝不把"没收到从核
	 * 结果"当成"通过"。 */
	bool sec_ok = rlog_wait_secondary_done(RLOG_TX_TIMEOUT_MS);
	bool any_fail = !sec_ok || (rlog_lost() != 0U);

	if (!sec_ok) {
		/* 非 token 形态的诊断行（不承载判定，只为报告可归因） */
		printk("[CTRL] secondary check FAILED: %s lost=%u\n",
		       rlog_secondary_silent() ? "SECONDARY_SILENT"
					       : "SECONDARY_INCOMPLETE",
		       (unsigned)rlog_lost());
	}
	/* 本核判据与 ztest 同源：zt_run_all 已按 stats 记入 test_results[] */
	for (int i = 0; i < TR_MAX_COUNT; i++) {
		if (test_results[i] == TR_FAIL) {
			any_fail = true;
			break;
		}
	}

	printk("PROJECT EXECUTION %s\n", any_fail ? "FAILED" : "SUCCESSFUL");

	/* test_main 不得返回（CLAUDE.md §4.1）——理由见 §6.9.3：返回会进 ztest 强
	 * main() 的 `TC_END_REPORT`，在本核补打一条**晚于**从核 DONE 的收尾标记，
	 * "完成标记是整条流最后一个"不再成立。 */
	k_sleep(K_FOREVER);
}
