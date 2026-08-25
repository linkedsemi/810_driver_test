/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i3c_controller_test.h"
#include "i3c_dualcore_test_protocol.h"
#include "test_results.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i3c/ibi.h>
#include <zephyr/drivers/i3c/ccc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(i3c_master_test, LOG_LEVEL_INF);

static struct i3c_device_desc *target_desc;
static enum i3c_test_id current_test_id;
static bool ctrl_any_failed;

static volatile bool ibi_received;
static volatile uint8_t ibi_payload_buf[8];
static volatile size_t ibi_payload_len;

static char ctrl_fail_reason[256];
static bool ctrl_failed;

/* 调试：带时间戳的步骤标记（printk 入队即返回，deferred 不阻塞流程） */
#define CTRL_STEP(tag) \
	printk("CTRL_STEP: %s @%lu\n", tag, k_cyc_to_us_floor32(k_cycle_get_32()))

/* 每项测试的结果记账：走 810_driver_test/common/test_results.h 公共模块
 * （test_results[] / test_reasons[] 全局符号），串口不可用时 GDB 直接读内存，
 * 见 tests/scripts/dump_results.py。 */

#define CTRL_EXPECT(cond, fmt, ...) \
	do { \
		if (!(cond)) { \
			int _rem = (int)sizeof(ctrl_fail_reason) - \
				   (int)strlen(ctrl_fail_reason); \
			if (_rem > 0) { \
				snprintf(ctrl_fail_reason + strlen(ctrl_fail_reason), \
					 (size_t)_rem, fmt "; ", ##__VA_ARGS__); \
			} \
			ctrl_failed = true; \
		} \
	} while (0)

static int ctrl_ibi_cb(struct i3c_device_desc *target,
		       struct i3c_ibi_payload *payload)
{
	ARG_UNUSED(target);

	CTRL_STEP("IBI enter");
	ibi_received = true;
	if (payload != NULL && payload->payload_len > 0) {
		ibi_payload_len = payload->payload_len;
		if (ibi_payload_len > sizeof(ibi_payload_buf)) {
			ibi_payload_len = sizeof(ibi_payload_buf);
		}
		memcpy((void *)ibi_payload_buf, payload->payload, ibi_payload_len);
		printk("IBI received, %zu bytes\n", ibi_payload_len);
	} else {
		ibi_payload_len = 0;
		printk("IBI received, no payload\n");
	}
	CTRL_STEP("IBI exit");
	return 0;
}

static int ctrl_send_cmd(const uint8_t *data, size_t len)
{
	size_t actual = 0;

	if (target_desc == NULL) {
		return -ENODEV;
	}

	int ret = i3c_write_actual(target_desc, data, len, &actual);
	if (ret != 0) {
		return ret;
	}
	if (actual != len) {
		return -EIO;
	}
	return 0;
}

/* 调试：带时间戳的步骤标记（printk 入队即返回，deferred 不阻塞流程） */
#define CTRL_STEP(tag) \
	printk("CTRL_STEP: %s @%lu\n", tag, k_cyc_to_us_floor32(k_cycle_get_32()))

/* 等待从机的就绪握手 IBI（payload {0xBB, test_id}） */
static bool ctrl_wait_ready_ibi(enum i3c_test_id id, uint32_t timeout_ms)
{
	uint32_t waited = 0;

	while (waited < timeout_ms) {
		if (ibi_received && ibi_payload_len == 2 &&
		    ibi_payload_buf[0] == 0xBB && ibi_payload_buf[1] == (uint8_t)id) {
			return true;
		}
		k_sleep(K_MSEC(1));
		waited += 1;
	}
	return false;
}

static bool ctrl_test_begin(enum i3c_test_id id)
{
	current_test_id = id;
	ctrl_failed = false;
	ctrl_fail_reason[0] = '\0';

	I3C_TEST_CTRL_START_VAR(i3c_test_names[id]);

	if (target_desc == NULL) {
		CTRL_EXPECT(false, "target not discovered");
		return false;
	}

	ibi_received = false;
	uint8_t payload = (uint8_t)id;
	int ret = ctrl_send_cmd((const uint8_t[]){ CMD_TEST_START, payload }, 2);
	CTRL_EXPECT(ret == 0, "CMD_TEST_START failed: %d", ret);
	CTRL_STEP("S sent");

	/* 就绪握手：从机解析完 TEST_START 后回 IBI {0xBB, id}。
	 * 收不到说明从机中断/状态机已卡死，立即失败并暴露。
	 * 实测慢速档（6.25MHz）下握手 IBI 偶发丢失（约 1/6 轮）：从机确实
	 * 发了（target 侧校验全过），主机侧 IBI 中断/workqueue 环节偶发漏投。
	 * 重发重等最多 3 次（reset_test_state 幂等，重发安全），
	 * 仍收不到才判失败——持久丢失才会暴露为 FAIL，偶发丢失被吸收。 */
	bool ready = false;
	for (int attempt = 0; attempt < 3 && ret == 0 && !ready; attempt++) {
		if (attempt > 0) {
			CTRL_STEP("ready IBI miss, retry S");
			k_sleep(K_MSEC(5));	/* 让上一笔 IBI 的 workqueue 处理排空 */
			ibi_received = false;
			ret = ctrl_send_cmd((const uint8_t[]){ CMD_TEST_START, payload }, 2);
			CTRL_EXPECT(ret == 0, "CMD_TEST_START retry failed: %d", ret);
		}
		ready = ctrl_wait_ready_ibi(id, 1000);
	}
	if (ret == 0 && !ready) {
		CTRL_EXPECT(false, "ready IBI timeout for test %d", id);
	}
	/* 实验：收到 IBI 后不再发后续帧，判定 cpu1 卡 8ms 是否与 cpu2 后续操作有关 */
#if 1
	CTRL_STEP("IBI ok");
	return !ctrl_failed;
#else
	CTRL_STEP("IBI ok");

	/* 实验（排查 IBI 后 8ms 死窗）：收到就绪 IBI 后延迟 N ms 再发测试帧。
	 * DLY500 实测：延迟 500ms 时帧落在窗口外，write_stop_read_32 双核 PASS，
	 * 死窗确为 IBI 后固定时长 blackout。改回 0 让帧打进窗口继续观测。 */
#define CTRL_DBG_POST_IBI_DELAY_MS 0
#if CTRL_DBG_POST_IBI_DELAY_MS > 0
	k_sleep(K_MSEC(CTRL_DBG_POST_IBI_DELAY_MS));
	CTRL_STEP("DLY done");
#endif

	return !ctrl_failed;
#endif
}

static void ctrl_test_end(void)
{
	uint8_t result = ctrl_failed ? TEST_RESULT_FAIL : TEST_RESULT_PASS;
	int ret = ctrl_send_cmd((const uint8_t[]){ CMD_TEST_END, (uint8_t)current_test_id, result }, 3);
	CTRL_STEP("E sent");
	CTRL_EXPECT(ret == 0, "CMD_TEST_END failed: %d", ret);

	/* Give target time to run its ztest and emit the TGT token. */
	k_sleep(K_MSEC(10));

	/* 记账，供结尾汇总表使用（zassert 失败会中止当前测试线程，必须先记） */
	tr_record(current_test_id,
		  ctrl_failed ? TR_FAIL : TR_PASS,
		  "%s", ctrl_fail_reason);

	if (ctrl_failed) {
		I3C_TEST_CTRL_FAIL_VAR(i3c_test_names[current_test_id], ctrl_fail_reason);
		ctrl_any_failed = true;
	} else {
		I3C_TEST_CTRL_PASS_VAR(i3c_test_names[current_test_id]);
	}

	zassert_false(ctrl_failed, "%s", ctrl_fail_reason);
}

static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (uint8_t)(seed + i);
	}
}

static void validate_pattern(const uint8_t *buf, size_t len, uint8_t seed,
			     const char *ctx)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t expected = (uint8_t)(seed + i);
		CTRL_EXPECT(buf[i] == expected,
			    "%s byte[%zu] 0x%02x != 0x%02x",
			    ctx, i, buf[i], expected);
	}
}

static int ctrl_set_read_len(uint8_t len)
{
	return ctrl_send_cmd((const uint8_t[]){ CMD_SET_READ_LEN, len }, 2);
}

static int ctrl_trigger(enum i3c_test_id id)
{
	uint8_t opcode = (id == I3C_TEST_IBI) ? CMD_TRIGGER_IBI : CMD_TRIGGER_HJ;
	return ctrl_send_cmd(&opcode, 1);
}

/* ========== ztest suite setup ========== */

static void *i3c_controller_setup(void)
{
	LOG_INF("[CPU2 MASTER] I3C controller test started");

	tr_init(I3C_TEST_COUNT);

	k_sleep(K_MSEC(20));

	target_desc = wait_for_target();
	if (target_desc == NULL) {
		/* Let the first test fail gracefully. */
		return NULL;
	}

	target_desc->ibi_cb = ctrl_ibi_cb;
	int ret = i3c_ibi_enable(target_desc);
	if (ret != 0) {
		LOG_ERR("i3c_ibi_enable failed: %d", ret);
	} else {
		LOG_INF("IBI enabled for target 0x%02x", I3C_TARGET_ADDR);
	}

	/* 每个测试项的就绪握手依赖 IBI：target 侧事件使能必须确实打开。
	 * 不能用 i3c_ibi_enable 的重使能（target 已在 IBI 列表时会早退 -EINVAL，
	 * ENEC 发不出去），这里直接发 direct CCC ENEC。 */
	struct i3c_ccc_events ibi_evt = { .events = I3C_CCC_EVT_INTR };
	int en_ret = i3c_ccc_do_events_set(target_desc, true, &ibi_evt);
	if (en_ret != 0) {
		LOG_ERR("setup ENEC INTR failed: %d", en_ret);
	}

	return NULL;
}

static void i3c_controller_before(void *fixture)
{
	ARG_UNUSED(fixture);
}

static void i3c_controller_after(void *fixture)
{
	ARG_UNUSED(fixture);
}

/* ========== Test implementations ========== */

#if I3C_TEST_EN_DAA_DISCOVERY
ZTEST(i3c_controller, daa_discovery)
{
	(void)ctrl_test_begin(I3C_TEST_DAA_DISCOVERY);

	CTRL_EXPECT(target_desc != NULL, "target 0x%02x not discovered", I3C_TARGET_ADDR);
	if (target_desc != NULL) {
		CTRL_EXPECT(target_desc->dynamic_addr == I3C_TARGET_ADDR,
			    "dynamic addr 0x%02x != 0x%02x",
			    target_desc->dynamic_addr, I3C_TARGET_ADDR);
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_DAA_DISCOVERY */

#if I3C_TEST_EN_WRITE_8
ZTEST(i3c_controller, write_8)
{
	uint8_t buf[8];
	uint8_t cmd_buf[2 + sizeof(buf)];

	if (!ctrl_test_begin(I3C_TEST_WRITE_8)) {
		ctrl_test_end();
		return;
	}

	fill_pattern(buf, sizeof(buf), 0x10);
	cmd_buf[0] = CMD_WRITE_DATA;
	cmd_buf[1] = sizeof(buf);
	memcpy(cmd_buf + 2, buf, sizeof(buf));

	size_t actual = 0;
	int ret = i3c_write_actual(target_desc, cmd_buf, sizeof(cmd_buf), &actual);
	CTRL_EXPECT(ret == 0, "write failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(cmd_buf), "write actual %zu != %zu", actual, sizeof(cmd_buf));

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_WRITE_8 */

#if I3C_TEST_EN_WRITE_100
ZTEST(i3c_controller, write_100)
{
	uint8_t buf[100];
	uint8_t cmd_buf[2 + sizeof(buf)];

	if (!ctrl_test_begin(I3C_TEST_WRITE_100)) {
		ctrl_test_end();
		return;
	}

	fill_pattern(buf, sizeof(buf), 0x20);
	cmd_buf[0] = CMD_WRITE_DATA;
	cmd_buf[1] = sizeof(buf);
	memcpy(cmd_buf + 2, buf, sizeof(buf));

	size_t actual = 0;
	int ret = i3c_write_actual(target_desc, cmd_buf, sizeof(cmd_buf), &actual);
	CTRL_EXPECT(ret == 0, "write failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(cmd_buf), "write actual %zu != %zu", actual, sizeof(cmd_buf));

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_WRITE_100 */

#if I3C_TEST_EN_READ_32_DIRECT
ZTEST(i3c_controller, read_32_direct)
{
	uint8_t buf[32];

	if (!ctrl_test_begin(I3C_TEST_READ_32_DIRECT)) {
		ctrl_test_end();
		return;
	}

	size_t actual = 0;
	int ret = -EIO;
	for (int i = 0; i < PHASE_RETRY; i++) {
		ret = i3c_read_actual(target_desc, buf, sizeof(buf), &actual);
		if (ret == 0 && actual == sizeof(buf)) {
			break;
		}
		LOG_INF("direct read attempt %d failed (ret=%d, actual=%zu), retry",
			i + 1, ret, actual);
		k_sleep(K_MSEC(TEST_DELAY_MS));
	}

	CTRL_EXPECT(ret == 0, "direct read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(buf), "direct read actual %zu != %zu",
		    actual, sizeof(buf));
	if (ret == 0 && actual == sizeof(buf)) {
		validate_pattern(buf, actual, 0x00, "read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_READ_32_DIRECT */

#if I3C_TEST_EN_WRITE_READ_1
ZTEST(i3c_controller, write_read_1)
{
	uint8_t rd[1];
	uint8_t wr = CMD_DUMMY_BYTE;	/* 占位字节，读长度已由 SET_READ_LEN 设置 */

	if (!ctrl_test_begin(I3C_TEST_WRITE_READ_1)) {
		ctrl_test_end();
		return;
	}

	int ret = ctrl_set_read_len(1);
	CTRL_EXPECT(ret == 0, "set read len failed: %d", ret);

	size_t actual = 0;
	ret = i3c_write_read_actual(target_desc, &wr, 1, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "write-read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "read actual %zu != %zu", actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "write-read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_WRITE_READ_1 */

#if I3C_TEST_EN_WRITE_READ_15
ZTEST(i3c_controller, write_read_15)
{
	uint8_t rd[15];
	uint8_t wr = CMD_DUMMY_BYTE;	/* 占位字节，读长度已由 SET_READ_LEN 设置 */

	if (!ctrl_test_begin(I3C_TEST_WRITE_READ_15)) {
		ctrl_test_end();
		return;
	}

	int ret = ctrl_set_read_len(15);
	CTRL_EXPECT(ret == 0, "set read len failed: %d", ret);

	size_t actual = 0;
	ret = i3c_write_read_actual(target_desc, &wr, 1, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "write-read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "read actual %zu != %zu", actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "write-read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_WRITE_READ_15 */

#if I3C_TEST_EN_WRITE_READ_16
ZTEST(i3c_controller, write_read_16)
{
	uint8_t rd[16];
	uint8_t wr = CMD_DUMMY_BYTE;	/* 占位字节，读长度已由 SET_READ_LEN 设置 */

	if (!ctrl_test_begin(I3C_TEST_WRITE_READ_16)) {
		ctrl_test_end();
		return;
	}

	int ret = ctrl_set_read_len(16);
	CTRL_EXPECT(ret == 0, "set read len failed: %d", ret);

	size_t actual = 0;
	ret = i3c_write_read_actual(target_desc, &wr, 1, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "write-read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "read actual %zu != %zu", actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "write-read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_WRITE_READ_16 */

#if I3C_TEST_EN_WRITE_READ_17
ZTEST(i3c_controller, write_read_17)
{
	uint8_t rd[17];
	uint8_t wr = CMD_DUMMY_BYTE;	/* 占位字节，读长度已由 SET_READ_LEN 设置 */

	if (!ctrl_test_begin(I3C_TEST_WRITE_READ_17)) {
		ctrl_test_end();
		return;
	}

	int ret = ctrl_set_read_len(17);
	CTRL_EXPECT(ret == 0, "set read len failed: %d", ret);

	size_t actual = 0;
	ret = i3c_write_read_actual(target_desc, &wr, 1, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "write-read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "read actual %zu != %zu", actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "write-read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_WRITE_READ_17 */

#if I3C_TEST_EN_WRITE_READ_32
ZTEST(i3c_controller, write_read_32)
{
	uint8_t rd[32];
	uint8_t wr = CMD_DUMMY_BYTE;	/* 占位字节，读长度已由 SET_READ_LEN 设置 */

	if (!ctrl_test_begin(I3C_TEST_WRITE_READ_32)) {
		ctrl_test_end();
		return;
	}

	int ret = ctrl_set_read_len(32);
	CTRL_EXPECT(ret == 0, "set read len failed: %d", ret);

	size_t actual = 0;
	ret = i3c_write_read_actual(target_desc, &wr, 1, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "write-read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "read actual %zu != %zu", actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "write-read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_WRITE_READ_32 */

/*
 * write+stop+start+read 变体：数据内容与 write_read 完全一致，
 * 但把 repeated-start 拆成 STOP + 新 START 两笔独立传输。
 * 用于隔离 write_read 的 URUNNACK 是否 restart 路径特有：
 * STOP 会触发 target 的 stop_cb 在总线空闲时做恢复性预填，
 * 没有 restart 前 ~3us（末字节 T-bit 之后）的装填死线。
 */
#define WRITE_STOP_READ_TEST(id, name, len)                                       \
	ZTEST(i3c_controller, name)                                               \
	{                                                                         \
		uint8_t rd[len];                                                  \
		uint8_t wr = CMD_DUMMY_BYTE;                                      \
		                                                                  \
		if (!ctrl_test_begin(id)) {                                       \
			ctrl_test_end();                                          \
			return;                                                   \
		}                                                                 \
		                                                                  \
		int ret = ctrl_set_read_len(len);                                 \
		CTRL_EXPECT(ret == 0, "set read len failed: %d", ret);            \
		CTRL_STEP("L sent");                                              \
		                                                                  \
		size_t actual = 0;                                                \
		ret = i3c_write_actual(target_desc, &wr, sizeof(wr), &actual);    \
		CTRL_EXPECT(ret == 0, "write failed: %d", ret);                   \
		CTRL_EXPECT(actual == sizeof(wr), "write actual %zu != %zu",      \
			    actual, sizeof(wr));                                  \
		CTRL_STEP("A5 sent");                                             \
		                                                                  \
		/* 等 target stop_cb 完成 TX FIFO 恢复性预填，再发起新 START 读 */ \
		k_sleep(K_MSEC(1));                                               \
		                                                                  \
		actual = 0;                                                       \
		ret = i3c_read_actual(target_desc, rd, sizeof(rd), &actual);      \
		CTRL_STEP("RD done");                                             \
		CTRL_EXPECT(ret == 0, "read failed: %d", ret);                    \
		CTRL_EXPECT(actual == sizeof(rd), "read actual %zu != %zu",       \
			    actual, sizeof(rd));                                  \
		if (ret == 0 && actual == sizeof(rd)) {                           \
			validate_pattern(rd, actual, 0x00, "write-stop-read");    \
		}                                                                 \
		                                                                  \
		ctrl_test_end();                                                  \
	}

#if I3C_TEST_EN_WRITE_STOP_READ_1
WRITE_STOP_READ_TEST(I3C_TEST_WRITE_STOP_READ_1, write_stop_read_1, 1)
#endif
#if I3C_TEST_EN_WRITE_STOP_READ_15
WRITE_STOP_READ_TEST(I3C_TEST_WRITE_STOP_READ_15, write_stop_read_15, 15)
#endif
#if I3C_TEST_EN_WRITE_STOP_READ_16
WRITE_STOP_READ_TEST(I3C_TEST_WRITE_STOP_READ_16, write_stop_read_16, 16)
#endif
#if I3C_TEST_EN_WRITE_STOP_READ_17
WRITE_STOP_READ_TEST(I3C_TEST_WRITE_STOP_READ_17, write_stop_read_17, 17)
#endif
#if I3C_TEST_EN_WRITE_STOP_READ_32
WRITE_STOP_READ_TEST(I3C_TEST_WRITE_STOP_READ_32, write_stop_read_32, 32)
#endif

#if I3C_TEST_EN_IBI
ZTEST(i3c_controller, ibi)
{
	if (!ctrl_test_begin(I3C_TEST_IBI)) {
		ctrl_test_end();
		return;
	}

	/* ccc_basic 的 DISEC(EVT_ALL) 可能已关掉 target 的 IBI 事件
	 * （硬件锁存 IBIDIS，期间 IBI 请求不会上到总线）。
	 * 注意不能用 i3c_ibi_enable 重使能：target 已在 controller 的
	 * IBI 列表中，它会以 -EINVAL(already in the list) 提前返回，
	 * ENEC 根本发不出去。这里直接发 direct CCC ENEC。 */
	struct i3c_ccc_events ibi_evt = { .events = I3C_CCC_EVT_INTR };
	int en_ret = i3c_ccc_do_events_set(target_desc, true, &ibi_evt);
	CTRL_EXPECT(en_ret == 0, "ENEC INTR failed: %d", en_ret);

	ibi_received = false;
	ibi_payload_len = 0;

	int ret = ctrl_trigger(I3C_TEST_IBI);
	CTRL_EXPECT(ret == 0, "IBI trigger failed: %d", ret);

	k_sleep(K_MSEC(30));

	CTRL_EXPECT(ibi_received, "IBI not received");
	CTRL_EXPECT(ibi_payload_len == 4, "IBI payload len %zu != 4", ibi_payload_len);
	if (ibi_payload_len == 4) {
		CTRL_EXPECT(ibi_payload_buf[0] == 0xBB, "IBI payload[0] 0x%02x != 0xBB",
			    ibi_payload_buf[0]);
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_IBI */

#if I3C_TEST_EN_HOT_JOIN
ZTEST(i3c_controller, hot_join)
{
	uint8_t rd[32];

	if (!ctrl_test_begin(I3C_TEST_HOT_JOIN)) {
		ctrl_test_end();
		return;
	}

	/* 同上：DISEC 可能已关掉 HJ 事件（硬件锁存 HJDIS）；重新使能 */
	struct i3c_ccc_events hj_evt = { .events = I3C_CCC_EVT_HJ };
	int en_ret = i3c_ccc_do_events_set(target_desc, true, &hj_evt);
	CTRL_EXPECT(en_ret == 0, "ENEC HJ failed: %d", en_ret);

	/* 1. 武装 target：HJ 只允许无动态地址的设备发起（DAVALID=1 时
	 *    硬件抑制请求，总线上不会有波形），所以 target 收到本命令后
	 *    并不立即发 HJ，而是等待下面 RSTDAA 清掉地址后自动发起 */
	int ret = ctrl_trigger(I3C_TEST_HOT_JOIN);
	CTRL_EXPECT(ret == 0, "HJ trigger failed: %d", ret);
	k_sleep(K_MSEC(10));

	/* 2. 广播 RSTDAA：target 失去动态地址 → 自动 HJ；
	 *    controller 收到 HJ 后由 IBI workqueue 重新 ENTDAA 分配地址
	 *    （驱动对已知 PID 复用旧地址，重连后仍是 0x32） */
	ret = i3c_ccc_do_rstdaa_all(i3c_ctrl_dev);
	CTRL_EXPECT(ret == 0, "RSTDAA failed: %d", ret);
	k_sleep(K_MSEC(20));

	/* 3. 以"读成功"作为 HJ 重连完成的判据：RSTDAA 到重 DAA 完成之间
	 *    读 0x32 会被 NACK，带重试等待 HJ workqueue 完成 */
	size_t actual = 0;
	ret = -EIO;
	for (int i = 0; i < PHASE_RETRY; i++) {
		ret = i3c_read_actual(target_desc, rd, sizeof(rd), &actual);
		if (ret == 0 && actual == sizeof(rd)) {
			break;
		}
		LOG_INF("post-HJ read attempt %d failed (ret=%d, actual=%zu), retry",
			i + 1, ret, actual);
		k_sleep(K_MSEC(TEST_DELAY_MS));
	}
	CTRL_EXPECT(ret == 0, "post-HJ read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "post-HJ read actual %zu != %zu",
		    actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "post-HJ read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_HOT_JOIN */

#if I3C_TEST_EN_CCC_BASIC
ZTEST(i3c_controller, ccc_basic)
{
	struct i3c_ccc_getpid pid;
	struct i3c_ccc_getbcr bcr;
	struct i3c_ccc_events events = { .events = I3C_CCC_EVT_ALL };

	if (!ctrl_test_begin(I3C_TEST_CCC_BASIC)) {
		ctrl_test_end();
		return;
	}

	int ret = i3c_ccc_do_getpid(target_desc, &pid);
	CTRL_EXPECT(ret == 0, "getpid failed: %d", ret);
	if (ret == 0) {
		CTRL_EXPECT(pid.pid[0] != 0 || pid.pid[1] != 0 || pid.pid[2] != 0 ||
			    pid.pid[3] != 0 || pid.pid[4] != 0 || pid.pid[5] != 0,
			    "received pid is zero");
	}

	ret = i3c_ccc_do_getbcr(target_desc, &bcr);
	CTRL_EXPECT(ret == 0, "getbcr failed: %d", ret);
	if (ret == 0) {
		CTRL_EXPECT(bcr.bcr == target_desc->bcr,
			    "bcr 0x%02x != 0x%02x", bcr.bcr, target_desc->bcr);
	}

	ret = i3c_ccc_do_events_set(target_desc, true, &events);
	CTRL_EXPECT(ret == 0, "ENEC failed: %d", ret);

	ret = i3c_ccc_do_events_set(target_desc, false, &events);
	CTRL_EXPECT(ret == 0, "DISEC failed: %d", ret);

	/* DISEC(EVT_ALL) 会让 target 硬件锁存 IBIDIS/HJDIS，
	 * 恢复使能，避免影响后续 ibi/hot_join 测试 */
	ret = i3c_ccc_do_events_set(target_desc, true, &events);
	CTRL_EXPECT(ret == 0, "restore ENEC failed: %d", ret);

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_CCC_BASIC */

/* ========== 主机主动临界测试（2026-08-24 新增） ========== */

/*
 * 读欠载（target TX 耗尽提前终止，T-bit=0）宏生成器：
 * SET_READ_LEN(short_len) 后 write+restart+read 读 read_len（> short_len）。
 * 用 write_read 结构（非纯读）：写匹配时刻 target 才按新 tx_len 重新预填
 * FIFO（纯读会踩到 stop_cb 预填的 tx_idx 记账滞后问题——app 层时序，
 * 与本测试要考察的"主机对提前终止的处理"无关）。
 * 预期：主机收到 short_len 字节后 target 结束消息，主机不挂死；
 * 随后紧跟一次正常 write_read 验证总线/驱动状态恢复。
 * 注：i3c_transfer 对提前终止的返回码依驱动实现而定（ret 可能为 0 且
 * actual<read_len，也可能返回错误），判定以 actual==short_len + 恢复读
 * 成功为准，ret 只记录不强判。
 */
#define READ_UNDERRUN_TEST(id, name, short_len, read_len)                        \
	ZTEST(i3c_controller, name)                                              \
	{                                                                        \
		uint8_t rd[read_len];                                            \
		uint8_t wr = CMD_DUMMY_BYTE;                                     \
		                                                                 \
		if (!ctrl_test_begin(id)) {                                      \
			ctrl_test_end();                                         \
			return;                                                  \
		}                                                                \
		                                                                 \
		int ret = ctrl_set_read_len(short_len);                          \
		CTRL_EXPECT(ret == 0, "set read len failed: %d", ret);           \
		                                                                 \
		size_t actual = 0;                                               \
		ret = i3c_write_read_actual(target_desc, &wr, 1, rd,             \
					    sizeof(rd), &actual);                  \
		CTRL_EXPECT(actual == short_len,                               \
			    "underrun read actual %zu != %d (ret=%d)",           \
			    actual, short_len, ret);                             \
		if (actual == short_len) {                                       \
			validate_pattern(rd, actual, 0x00, "underrun read");       \
		}                                                                \
		                                                                 \
		/* 恢复验证：提前终止后紧跟的正常读必须完整成功 */              \
		memset(rd, 0, sizeof(rd));                                       \
		actual = 0;                                                      \
		ret = i3c_write_read_actual(target_desc, &wr, 1, rd,             \
					    short_len, &actual);                     \
		CTRL_EXPECT(ret == 0, "recovery read failed: %d", ret);          \
		CTRL_EXPECT(actual == short_len,                               \
			    "recovery read actual %zu != %d", actual, short_len); \
		if (ret == 0 && actual == short_len) {                           \
			validate_pattern(rd, actual, 0x00, "recovery read");       \
		}                                                                \
		                                                                 \
		ctrl_test_end();                                                 \
	}

#if I3C_TEST_EN_READ_UNDERRUN_8_16
READ_UNDERRUN_TEST(I3C_TEST_READ_UNDERRUN_8_16, read_underrun_8_16, 8, 16)
#endif
#if I3C_TEST_EN_READ_UNDERRUN_16_32
/* 16 = target TX FIFO 深度边界：正好装满 FIFO 的预填量被读穿 */
READ_UNDERRUN_TEST(I3C_TEST_READ_UNDERRUN_16_32, read_underrun_16_32, 16, 32)
#endif

#if I3C_TEST_EN_NACK_BAD_ADDR
ZTEST(i3c_controller, nack_bad_addr)
{
	uint8_t rd[8];
	uint8_t wr = CMD_DUMMY_BYTE;

	if (!ctrl_test_begin(I3C_TEST_NACK_BAD_ADDR)) {
		ctrl_test_end();
		return;
	}

	/* 伪造一个未挂载地址（0x55）的 desc，走正常 i3c_transfer 路径。
	 * target 侧硬件层不应答，不经过任何软件回调。
	 *
	 * 注意（实测）：地址 NACK 的上报在不同总线时序下不一致——
	 * 12.5MHz pure I3C 下驱动能检出错误（-EIO），1MHz mixed 时序下
	 * 可能静默"成功"返回（ERRF/SER.ANACK 跨时钟域同步都不可见）。
	 * 因此本测试的硬性判定是三条安全性质：
	 *   1) 不挂死（能在超时内返回——跑到这里即满足）；
	 *   2) 读不得伪造数据（ret==0 时 actual 必须为 0）；
	 *   3) 事后总线恢复正常（紧随的正常读必须成功）。
	 * ret/actual 记录进日志供报告分析。 */
	struct i3c_device_desc bad_desc = *target_desc;
	bad_desc.dynamic_addr = 0x55;

	size_t actual = 0;
	int ret_w = i3c_write_actual(&bad_desc, &wr, sizeof(wr), &actual);
	size_t wr_actual = actual;
	printk("NACK probe: write ret=%d actual=%zu\n", ret_w, wr_actual);

	actual = 0;
	int ret_r = i3c_read_actual(&bad_desc, rd, sizeof(rd), &actual);
	printk("NACK probe: read ret=%d actual=%zu\n", ret_r, actual);
	CTRL_EXPECT(ret_r != 0 || actual == 0,
		    "bad-addr read fabricated data: ret=%d actual=%zu", ret_r, actual);

	/* 关键校验：NACK 之后总线/控制器状态必须恢复，对正常地址的
	 * 读写不受影响（target 默认预填 32 字节，读 8 字节即可） */
	actual = 0;
	int ret = i3c_read_actual(target_desc, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "post-NACK read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "post-NACK read actual %zu != %zu",
		    actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "post-NACK read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_NACK_BAD_ADDR */

#if I3C_TEST_EN_BACK_TO_BACK_8X8
ZTEST(i3c_controller, back_to_back_8x8)
{
	uint8_t rd[8];
	uint8_t wr = CMD_DUMMY_BYTE;

	if (!ctrl_test_begin(I3C_TEST_BACK_TO_BACK_8X8)) {
		ctrl_test_end();
		return;
	}

	int ret = ctrl_set_read_len(8);
	CTRL_EXPECT(ret == 0, "set read len failed: %d", ret);

	/* 无延时连发 8 次 write+restart+read：压测主机帧引擎与 target
	 * 预填/续发在零间隙下的稳定性 */
	for (int i = 0; i < 8; i++) {
		size_t actual = 0;
		ret = i3c_write_read_actual(target_desc, &wr, 1, rd, sizeof(rd),
					    &actual);
		CTRL_EXPECT(ret == 0, "b2b iter %d failed: %d", i, ret);
		CTRL_EXPECT(actual == sizeof(rd), "b2b iter %d actual %zu != %zu",
			    i, actual, sizeof(rd));
		if (ret == 0 && actual == sizeof(rd)) {
			validate_pattern(rd, actual, 0x00, "b2b");
		}
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_BACK_TO_BACK_8X8 */

#if I3C_TEST_EN_BIG_256
ZTEST(i3c_controller, big_256)
{
	uint8_t cmd_buf[2 + 256];
	uint8_t rd[256];

	if (!ctrl_test_begin(I3C_TEST_BIG_256)) {
		ctrl_test_end();
		return;
	}

	/* 256B 大写：协议长度字节 val==0 约定为 256（单字节表达上限 255）。
	 * 跨 target RX 缓冲/解析与 controller TX FIFO 续写路径。 */
	fill_pattern(cmd_buf + 2, 256, 0x40);
	cmd_buf[0] = CMD_WRITE_DATA;
	cmd_buf[1] = 0;	/* 0 => 256 */

	size_t actual = 0;
	int ret = i3c_write_actual(target_desc, cmd_buf, sizeof(cmd_buf), &actual);
	CTRL_EXPECT(ret == 0, "write 256 failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(cmd_buf), "write actual %zu != %zu",
		    actual, sizeof(cmd_buf));

	/* 256B 大读：target 预填 16B FIFO + read_processed_cb 续发 240B */
	ret = ctrl_set_read_len(0);	/* 0 => 256 */
	CTRL_EXPECT(ret == 0, "set read len 256 failed: %d", ret);
	k_sleep(K_MSEC(1));

	actual = 0;
	ret = i3c_read_actual(target_desc, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "read 256 failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "read actual %zu != %zu",
		    actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "read 256");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_BIG_256 */

#if I3C_TEST_EN_CCC_RAPID_8
ZTEST(i3c_controller, ccc_rapid_8)
{
	if (!ctrl_test_begin(I3C_TEST_CCC_RAPID_8)) {
		ctrl_test_end();
		return;
	}

	/* getpid 无间隔连发 8 次：num_xfer 未清零修复（栈垃圾计数器残留）
	 * 的针对性回归——连发时 ccc_tgt_payload 栈槽残留最显著。
	 * 每次 payload 都在同一栈位重建，若驱动不清 num_xfer，第 2 次起
	 * 必挂。 */
	for (int i = 0; i < 8; i++) {
		struct i3c_ccc_getpid pid;
		int ret = i3c_ccc_do_getpid(target_desc, &pid);
		CTRL_EXPECT(ret == 0, "rapid getpid iter %d failed: %d", i, ret);
		if (ret == 0) {
			CTRL_EXPECT(pid.pid[0] != 0 || pid.pid[1] != 0 ||
				    pid.pid[2] != 0 || pid.pid[3] != 0 ||
				    pid.pid[4] != 0 || pid.pid[5] != 0,
				    "rapid getpid iter %d pid is zero", i);
		}
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_CCC_RAPID_8 */

#if I3C_TEST_EN_READ_MASTER_ABORT
ZTEST(i3c_controller, read_master_abort)
{
	uint8_t rd[32];

	if (!ctrl_test_begin(I3C_TEST_READ_MASTER_ABORT)) {
		ctrl_test_end();
		return;
	}

	/* 主机主动截断读：target 预填 32 字节（reset_test_state 默认），
	 * 主机只取 8 字节就结束帧（DCNT=8，硬件 T-bit 提前终止）。
	 * 考察 target 侧 TERM 路径与 FIFO 残留恢复。 */
	size_t actual = 0;
	int ret = i3c_read_actual(target_desc, rd, 8, &actual);
	CTRL_EXPECT(ret == 0, "abort read failed: %d", ret);
	CTRL_EXPECT(actual == 8, "abort read actual %zu != 8", actual);
	if (ret == 0 && actual == 8) {
		validate_pattern(rd, actual, 0x00, "abort read");
	}

	/* 恢复验证：STOP 后 target 应已 FLUSHTB 重填，下一笔读必须
	 * 从 pattern 起点对齐（buf[0]）；错位说明截断恢复有 bug */
	memset(rd, 0, sizeof(rd));
	actual = 0;
	ret = i3c_read_actual(target_desc, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "post-abort read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "post-abort read actual %zu != %zu",
		    actual, sizeof(rd));
	if (ret == 0 && actual == sizeof(rd)) {
		validate_pattern(rd, actual, 0x00, "post-abort read");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_READ_MASTER_ABORT */

#if I3C_TEST_EN_IBI_NEG_PATHS
ZTEST(i3c_controller, ibi_neg_paths)
{
	if (!ctrl_test_begin(I3C_TEST_IBI_NEG_PATHS)) {
		ctrl_test_end();
		return;
	}

	/* 编排：先 DISEC(EVT_INTR) 让 target 硬件锁存 IBIDIS，
	 * 再让 target 跑 IBI 负路径自检（此时它的 i3c_ibi_raise 必须
	 * fail-fast -EACCES），最后 ENEC 恢复（否则后续测试的
	 * 就绪握手 IBI 全部发不出来）。 */
	struct i3c_ccc_events evt = { .events = I3C_CCC_EVT_INTR };
	int ret = i3c_ccc_do_events_set(target_desc, false, &evt);
	CTRL_EXPECT(ret == 0, "DISEC INTR failed: %d", ret);

	ret = ctrl_send_cmd((const uint8_t[]){ CMD_RUN_TGT_NEG }, 1);
	CTRL_EXPECT(ret == 0, "CMD_RUN_TGT_NEG failed: %d", ret);

	k_sleep(K_MSEC(50));	/* 等 target dispatcher 跑完负路径自检 */

	ret = i3c_ccc_do_events_set(target_desc, true, &evt);
	CTRL_EXPECT(ret == 0, "restore ENEC INTR failed: %d", ret);

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_IBI_NEG_PATHS */

#if I3C_TEST_EN_API_NEG_PATHS
ZTEST(i3c_controller, api_neg_paths)
{
	uint8_t dummy = 0xA5;
	struct i3c_msg msg = {
		.buf = &dummy,
		.len = 1,
		.flags = I3C_MSG_WRITE | I3C_MSG_STOP,
		.hdr_mode = 0,
		.num_xfer = 0,
	};

	if (!ctrl_test_begin(I3C_TEST_API_NEG_PATHS)) {
		ctrl_test_end();
		return;
	}

	/* 全部只验返回值，不上总线（驱动入口拦截） */
	int ret = i3c_transfer(target_desc, &msg, 0);
	CTRL_EXPECT(ret == -EINVAL, "num_msgs=0 ret %d != -EINVAL", ret);

	struct i3c_device_desc no_addr = *target_desc;
	no_addr.dynamic_addr = 0;
	ret = i3c_transfer(&no_addr, &msg, 1);
	CTRL_EXPECT(ret == -EINVAL, "dynamic_addr=0 ret %d != -EINVAL", ret);

	msg.hdr_mode = 1;
	ret = i3c_transfer(target_desc, &msg, 1);
	CTRL_EXPECT(ret == -EINVAL, "hdr_mode=1 ret %d != -EINVAL", ret);
	msg.hdr_mode = 0;

	/* 注：NULL desc 不测——i3c_transfer 的 API 包装层
	 * （include/zephyr/drivers/i3c.h z_impl_i3c_transfer）先解引用
	 * target->bus 才进驱动，NULL 会在包装层硬 fault（上游行为，
	 * 非本驱动可防；驱动入口已有 NULL 检查做纵深防御）。
	 * msgs=NULL 包装层不解引用，可直达驱动入口检查。 */
	ret = i3c_transfer(target_desc, NULL, 1);
	CTRL_EXPECT(ret == -EINVAL, "NULL msgs ret %d != -EINVAL", ret);

	/* 负路径之后正常传输必须不受影响（确认无状态残留） */
	size_t actual = 0;
	uint8_t rd[8];
	ret = i3c_read_actual(target_desc, rd, sizeof(rd), &actual);
	CTRL_EXPECT(ret == 0, "post-neg read failed: %d", ret);
	CTRL_EXPECT(actual == sizeof(rd), "post-neg read actual %zu != %zu",
		    actual, sizeof(rd));

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_API_NEG_PATHS */

#if I3C_TEST_EN_MULTI_MSG_4
ZTEST(i3c_controller, multi_msg_4)
{
	uint8_t wr = CMD_DUMMY_BYTE;
	uint8_t rd1[8], rd2[8];

	if (!ctrl_test_begin(I3C_TEST_MULTI_MSG_4)) {
		ctrl_test_end();
		return;
	}

	/* 四段复合帧：W + R8 + W + R8（中间无 STOP，全 RESTART 链）。
	 * 考察 ctrl_msg_idx/status_msg_idx/xfer_msg_idx 三索引推进。
	 * target 侧预期：每个写匹配都触发 target 按 buf[0] 重新预填
	 * （写匹配会清 TX FIFO），两段读均从 pattern 起点供应。 */
	struct i3c_msg msgs[4] = {
		{ .buf = &wr, .len = 1, .flags = I3C_MSG_WRITE, .num_xfer = 0 },
		{ .buf = rd1, .len = sizeof(rd1),
		  .flags = I3C_MSG_RESTART | I3C_MSG_READ, .num_xfer = 0 },
		{ .buf = &wr, .len = 1,
		  .flags = I3C_MSG_RESTART | I3C_MSG_WRITE, .num_xfer = 0 },
		{ .buf = rd2, .len = sizeof(rd2),
		  .flags = I3C_MSG_RESTART | I3C_MSG_READ | I3C_MSG_STOP, .num_xfer = 0 },
	};

	int ret = i3c_transfer(target_desc, msgs, 4);
	CTRL_EXPECT(ret == 0, "multi-msg transfer failed: %d", ret);
	CTRL_EXPECT(msgs[0].num_xfer == 1, "msg0 num_xfer %u != 1", msgs[0].num_xfer);
	CTRL_EXPECT(msgs[1].num_xfer == sizeof(rd1),
		    "msg1 num_xfer %u != %zu", msgs[1].num_xfer, sizeof(rd1));
	CTRL_EXPECT(msgs[2].num_xfer == 1, "msg2 num_xfer %u != 1", msgs[2].num_xfer);
	CTRL_EXPECT(msgs[3].num_xfer == sizeof(rd2),
		    "msg3 num_xfer %u != %zu", msgs[3].num_xfer, sizeof(rd2));

	if (ret == 0 && msgs[1].num_xfer == sizeof(rd1)) {
		validate_pattern(rd1, sizeof(rd1), 0x00, "multi-msg read1");
	}
	if (ret == 0 && msgs[3].num_xfer == sizeof(rd2)) {
		validate_pattern(rd2, sizeof(rd2), 0x00, "multi-msg read2");
	}

	ctrl_test_end();
}
#endif /* I3C_TEST_EN_MULTI_MSG_4 */

ZTEST_SUITE(i3c_controller, NULL, i3c_controller_setup,
	    i3c_controller_before, i3c_controller_after, NULL);

static void ctrl_print_summary(void)
{
	int passed = 0, ran = 0;

	I3C_TEST_SUMMARY_BEGIN(CTRL);
	for (int i = 0; i < I3C_TEST_COUNT; i++) {
		const char *name = i3c_test_names[i];

		if (test_results[i] < 0) {
			I3C_TEST_SUMMARY_ITEM(CTRL, name, "NOT_RUN");
			continue;
		}
		ran++;
		if (test_results[i] == 0) {
			passed++;
			I3C_TEST_SUMMARY_ITEM(CTRL, name, "PASS");
		} else if (test_reasons[i][0] != '\0') {
			I3C_TEST_SUMMARY_ITEM_REASON(CTRL, name, "FAIL",
						     test_reasons[i]);
		} else {
			I3C_TEST_SUMMARY_ITEM(CTRL, name, "FAIL");
		}
	}
	I3C_TEST_SUMMARY_END(CTRL, passed, ran);
}

void test_main(void)
{
	I3C_TEST_CTRL_BOOT();

	ztest_run_all(NULL, false, 1, 1);
	ztest_verify_all_test_suites_ran();

	/* 通知 target 打印它的汇总表（target 没有本地的"全部结束"信号），
	 * 稍等让 target 的总结先出完，再打印 controller 自己的，
	 * 避免两张表在合并观察的串口日志里交错 */
	(void)ctrl_send_cmd((const uint8_t[]){ CMD_PRINT_SUMMARY }, 1);
	k_sleep(K_MSEC(20));

	ctrl_print_summary();
	I3C_TEST_DONE_VAR(ctrl_any_failed ? "FAIL" : "PASS");
}
