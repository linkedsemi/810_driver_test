/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 纯 I2C 波形质量测试 —— cpu1 slave 侧（i2c6 @400K，从机地址 0x58）。
 *
 * 被测对象：i2c6（i2c@40084400）I2C 从机，引脚 I2C6_scl_PJ08 /
 * I2C6_sda_PJ09（board 级 pinctrl 宏生成，init 自动应用）。
 * 对端：cpu2 侧 i2c5 主机。
 *
 * 逐字节解析（i2c_target_callbacks 普通回调模式，无 buf_read_requested）：
 *   'W' <len> <data...>：校验递增序列 0,1,2,...（写内容校验，读由 master 校验）
 *   'P'：判定后打印并结束套件
 * ISR 回调只记账（CLAUDE.md §4.2），ZTEST 测试体轮询 'P' 标志后统一判定。
 *
 * ztest 原生框架版：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），套件内 ZTEST(i2c_pure_tgt, i2c_wave) 为唯一
 * 测试项；结果记账由 ztest_glue 的 zt_run_all 按 ztest stats 自动完成，
 * 数值上报走 ZT_INFO 约定行（§13.4）。
 */

#include "i2c_pure_test_protocol.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "ztest_glue.h"
#include "rlog.h"
#include "test_results.h"

LOG_MODULE_REGISTER(i2c_pure_slave_test, LOG_LEVEL_INF);

const struct device *slave_i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c6));

/* 'P' 收尾命令等待上限。正常路径 ≈ I2C_WAVE_ROUNDS × (单轮传输 ~0.7ms +
 * I2C_WAVE_ROUND_DELAY_MS 100ms) ≈ 10.1s；取 30s（3 倍余量），同时显著
 * 小于 harness 的 case 超时默认 60s（run_test.py I2C_CASE_TIMEOUT），
 * 保证 master 崩溃时失败落在可归因的 zassert 上而非 harness 强杀。 */
#define I2C_TGT_SUMMARY_TIMEOUT_MS 30000U

/* 读返回缓冲：0xAA + i（master 侧校验） */
uint8_t tgt_tx_buf[256];

/* 读序列索引：每次写匹配复位，read 回调逐字节推进 */
static volatile uint16_t tgt_tx_idx;

/* 逐字节解析状态机（ISR 上下文：只记账） */
enum parse_state {
	PS_OPCODE,		/* 等 opcode */
	PS_WRITE_LEN,		/* 已收 'W'，等长度 */
	PS_WRITE_DATA,		/* 已收 'W' <len>，收数据并校验 */
};

static volatile enum parse_state tgt_ps;
static volatile uint8_t tgt_wr_len;
static volatile uint16_t tgt_wr_idx;

/* 写内容校验：期望递增序列 0,1,2,... */
static volatile bool tgt_data_mismatch;
static volatile bool tgt_write_seen;	/* 至少一笔完整 'W' 写 */
static volatile uint32_t tgt_write_cnt;	/* 'W' 写笔数 */
static volatile uint32_t tgt_write_bytes; /* 'W' 写数据总字节 */
static volatile uint32_t tgt_read_cnt;	/* 读请求总字节 */
static volatile bool tgt_summary_evt;	/* 收到 'P' */

static void tgt_parse_byte(uint8_t byte)
{
	switch (tgt_ps) {
	case PS_OPCODE:
		switch (byte) {
		case CMD_WRITE_PATTERN:
			tgt_ps = PS_WRITE_LEN;
			break;
		case CMD_PRINT_SUMMARY:
			tgt_summary_evt = true;
			break;
		default:
			break;	/* 未知 opcode 忽略，保持等 opcode */
		}
		break;
	case PS_WRITE_LEN:
		tgt_wr_len = byte;
		tgt_wr_idx = 0;
		tgt_ps = (byte == 0) ? PS_OPCODE : PS_WRITE_DATA;
		break;
	case PS_WRITE_DATA:
		if (byte != (uint8_t)tgt_wr_idx) {
			tgt_data_mismatch = true;
		}
		tgt_wr_idx++;
		tgt_write_bytes++;
		if (tgt_wr_idx >= tgt_wr_len) {
			tgt_ps = PS_OPCODE;
			tgt_write_seen = true;
			tgt_write_cnt++;
		}
		break;
	}
}

/* ============ i2c6 slave 回调（逐字节） ============ */

static int tgt_i2c_read_byte(uint8_t *val)
{
	/* i2c 无 END0 概念：读长度由 master 的 STOP 决定，tx_buf[256] 足够 */
	*val = tgt_tx_buf[tgt_tx_idx++];
	tgt_read_cnt++;
	return 0;
}

static int tgt_i2c_write_requested_cb(struct i2c_target_config *config)
{
	ARG_UNUSED(config);

	tgt_ps = PS_OPCODE;
	tgt_tx_idx = 0;
	return 0;
}

static int tgt_i2c_write_received_cb(struct i2c_target_config *config, uint8_t val)
{
	ARG_UNUSED(config);

	tgt_parse_byte(val);
	return 0;	/* 不 NACK：数据内容由解析状态机校验 */
}

static int tgt_i2c_read_requested_cb(struct i2c_target_config *config, uint8_t *val)
{
	ARG_UNUSED(config);

	return tgt_i2c_read_byte(val);
}

static int tgt_i2c_read_processed_cb(struct i2c_target_config *config, uint8_t *val)
{
	ARG_UNUSED(config);

	return tgt_i2c_read_byte(val);
}

static int tgt_i2c_stop_cb(struct i2c_target_config *config)
{
	ARG_UNUSED(config);

	return 0;
}

ZTEST_SUITE(i2c_pure_tgt, NULL, NULL, NULL, NULL, NULL);

/* 纯 I2C 波形：从机侧写内容校验（i2c_wave）。
 * 等待 master 100 轮后的 'P' 命令（ISR 置标志，本测试体轮询），
 * 判定写已见且写内容全部匹配（读内容由 master 侧校验）。 */
ZTEST(i2c_pure_tgt, i2c_wave)
{
	/* 标准 Zephyr i2c_target_callbacks：不设 buf_read_requested（NULL），
	 * 驱动按函数指针非 NULL 判断走逐字节回调路径（i2c_ls.c）。 */
	static struct i2c_target_callbacks i2c_cb = {
		.write_requested = tgt_i2c_write_requested_cb,
		.write_received = tgt_i2c_write_received_cb,
		.read_requested = tgt_i2c_read_requested_cb,
		.read_processed = tgt_i2c_read_processed_cb,
		.stop = tgt_i2c_stop_cb,
	};
	static struct i2c_target_config i2c_cfg = {
		.address = I2C_PURE_SLAVE_ADDR,
		.callbacks = &i2c_cb,
	};
	int ret = i2c_target_register(slave_i2c_dev, &i2c_cfg);

	zassert_equal(ret, 0, "i2c_target_register rc=%d", ret);

	/* 等 'P'（master 100 轮后发送）：ISR 置标志，本测试体轮询。
	 * 带超时（原实现 `while (!tgt_summary_evt) k_sleep(20ms)` 无限等 ⇒
	 * master 崩溃时本测试体永久挂起，只能靠 harness case 超时强杀，
	 * 报告里归因不出"从机在等 master"，与"固件挂死"无法区分）。 */
	uint32_t waited_ms = 0;

	while (!tgt_summary_evt && waited_ms < I2C_TGT_SUMMARY_TIMEOUT_MS) {
		k_sleep(K_MSEC(20));
		waited_ms += 20;
	}

	zassert_true(tgt_summary_evt,
		     "timeout waiting for 'P' after %ums: master aborted or link "
		     "dead (writes=%u,bytes=%u,reads=%u)",
		     (unsigned)waited_ms, (unsigned)tgt_write_cnt,
		     (unsigned)tgt_write_bytes, (unsigned)tgt_read_cnt);

	/* 判定①：写过且写内容校验全过（原判据，保留） */
	zassert_true(tgt_write_seen, "no_write_seen");
	zassert_false(tgt_data_mismatch, "write_data_mismatch");

	/* 判定②（新增，堵空壳）：写**轮数/字节数**必须与 master 协议一致。
	 * 原实现只验"见过至少一笔写 + 无错字节" ⇒ 丢 99/100 轮仍 PASS，
	 * TGT 侧对"链路整段静默/大面积丢帧"零判别力（CTRL 侧会拦，但
	 * "双核双 PASS"里 TGT 这一半是弱证据）。
	 * 期望值来源（源码，非猜测）：协议头 I2C_WAVE_ROUNDS=100、
	 * I2C_WAVE_LEN=16 —— master 每轮发 1 帧 'W'<16><16B 数据>，
	 * 故完整跑完 = 100 帧 / 1600 数据字节。
	 *   - 驱动正确时必过：100 轮各 1 帧完整到达；
	 *   - 典型失效时必不过：master 中途崩/掉帧/写帧被截断（解析停在
	 *     PS_WRITE_DATA 不计帧）⇒ 计数偏小。 */
	zassert_equal(tgt_write_cnt, (uint32_t)I2C_WAVE_ROUNDS,
		      "write frames %u != %d (rounds lost or master aborted)",
		      (unsigned)tgt_write_cnt, I2C_WAVE_ROUNDS);
	zassert_equal(tgt_write_bytes,
		      (uint32_t)I2C_WAVE_ROUNDS * (uint32_t)I2C_WAVE_LEN,
		      "write bytes %u != %d",
		      (unsigned)tgt_write_bytes,
		      I2C_WAVE_ROUNDS * (int)I2C_WAVE_LEN);

	/* 单出口：INFO 是四类标记之一（§6.9.1）⇒ 不能留在本核串口 */
	RLOG_ZT_INFO("i2c_wave", "writes=%u,bytes=%u,reads=%u",
		     (unsigned)tgt_write_cnt, (unsigned)tgt_write_bytes,
		     (unsigned)tgt_read_cnt);
}

void test_main(void)
{
	/* 单出口（SPEC_REFERENCE §6.9.3/§6.11.5）：本核是**从核**（ztest 型）⇒
	 * 不往自己的串口打 token。ztest 的逐项输出（START/结果/====/断言行）经
	 * `tc_util_user_override.h` 把 PRINT_DATA 指向 RLOG 自动换出口；BOOT 与
	 * INFO 走的是公共宏的 printk 路径（不经 PRINT_DATA）⇒ 此处显式换出口。
	 * 本核的 LOG / panic / assert 现场仍走本核串口。 */
	rlog_secondary_init();

	LOG_INF("[CPU1 I2C_PURE_TGT] slave test started");   /* 角色 banner（harness 依赖） */

	/* 读返回缓冲预填：0xAA + i（master 侧校验） */
	for (int i = 0; i < (int)sizeof(tgt_tx_buf); i++) {
		tgt_tx_buf[i] = (uint8_t)(TGT_READ_PATTERN_BASE + i);
	}

	tgt_ps = PS_OPCODE;
	tgt_tx_idx = 0;

	RLOG_ZT_BOOT("TGT");
	zt_run_all();

	/* 本核结论：zt_run_all 已按 ztest stats 记入 test_results[] ⇒ 扫一遍即可
	 * （与主核同源，不另设判据）。DONE 帧是**本核发出的最后一条**：主核等到
	 * 它才打自己的 PROJECT EXECUTION（§6.9.3 硬约束：完成标记必须是整条流的
	 * 最后一个，否则 harness 提前杀 gdb、截断后续行）。 */
	bool any_fail = false;

	for (int i = 0; i < TR_MAX_COUNT; i++) {
		if (test_results[i] == TR_FAIL) {
			any_fail = true;
			break;
		}
	}
	rlog_emit_done(!any_fail, any_fail ? "PROJECT EXECUTION FAILED"
					   : "PROJECT EXECUTION SUCCESSFUL");

	/* test_main 不得返回（CLAUDE.md §4.1）：返回会进 ztest 强 main() 的
	 * `TC_END_REPORT`，在本核再补一条 `PROJECT EXECUTION` —— 此刻 PRINT_DATA
	 * 已被覆盖，它同样会被转发到主核流里、**排在主核收尾之后** ⇒ 破坏
	 * §6.9.3 的"完成标记是整条流最后一个"硬约束。 */
	k_sleep(K_FOREVER);
}
