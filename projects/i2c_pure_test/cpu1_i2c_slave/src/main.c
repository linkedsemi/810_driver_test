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

LOG_MODULE_REGISTER(i2c_pure_slave_test, LOG_LEVEL_INF);

const struct device *slave_i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c6));

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

	/* 等 'P'（master 100 轮后发送）：ISR 置标志，本测试体轮询 */
	while (!tgt_summary_evt) {
		k_sleep(K_MSEC(20));
	}

	/* 判定：写过且写内容校验全过 → PASS（读内容由 master 校验） */
	zassert_true(tgt_write_seen, "no_write_seen");
	zassert_false(tgt_data_mismatch, "write_data_mismatch");

	ZT_INFO("i2c_wave", "writes=%u,bytes=%u,reads=%u",
		(unsigned)tgt_write_cnt, (unsigned)tgt_write_bytes,
		(unsigned)tgt_read_cnt);
}

void test_main(void)
{
	LOG_INF("[CPU1 I2C_PURE_TGT] slave test started");   /* 角色 banner（harness 依赖） */

	/* 读返回缓冲预填：0xAA + i（master 侧校验） */
	for (int i = 0; i < (int)sizeof(tgt_tx_buf); i++) {
		tgt_tx_buf[i] = (uint8_t)(TGT_READ_PATTERN_BASE + i);
	}

	tgt_ps = PS_OPCODE;
	tgt_tx_idx = 0;

	zt_init("TGT");
	zt_run_all();
}
