/*
 * MBOX test protocol header (双核通信测试)
 *
 * 固定 pattern: | magic(2B) | seq(4B) | channel(1B) | payload(递增) |
 * 每通道独立 seq，ISR 只记账不打 log（810 §4.1）。
 */

#ifndef MBOX_TEST_PROTOCOL_H
#define MBOX_TEST_PROTOCOL_H

#include <stdint.h>

#define MBOX_TEST_MAGIC      0xABCD
#define MBOX_TEST_MAX_SIZE   64

/* case_mbox_basic 的帧数契约（cpu2 发送侧与 cpu1 校验侧共用，避免两处硬编码
 * 各写一个数字而悄悄漂移）。判据见 COVERAGE.md §2。 */
#define MBOX_BASIC_CH4_COUNT 5
#define MBOX_BASIC_CH5_COUNT 3

/* cpu2 首帧前的让路延时：等 cpu1 完成回调注册 + set_enabled 之后才开始发，
 * 否则开机竞态会吞掉队首若干帧（各扩展 case 的 k_msleep(600) 同款）。 */
#define MBOX_BASIC_STARTUP_MS 600

/* 消息结构（4 字节对齐） */
struct mbox_test_msg {
	uint16_t magic;       /* 0xABCD */
	uint8_t  channel;     /* 通道号 (4 or 5) */
	uint8_t  _pad;
	uint32_t seq;         /* 每通道递增序列号 */
	uint8_t  payload[MBOX_TEST_MAX_SIZE - 8];  /* 递增 pattern */
} __packed;

/* 验证消息 pattern */
static inline int mbox_test_verify(const struct mbox_test_msg *msg, uint8_t ch)
{
	if (msg->magic != MBOX_TEST_MAGIC) return -1;
	if (msg->channel != ch) return -2;
	for (int i = 0; i < sizeof(msg->payload); i++) {
		if (msg->payload[i] != (uint8_t)(msg->seq + i)) return -3;
	}
	return 0;
}

/*
 * ===================== V2 扩展（P1c 追加） =====================
 *
 * 上面 V1 的结构体与字段语义/顺序保持不变（case_mbox_basic 继续使用）；
 * 以下仅追加扩展 case 所需的新命令/新字段，新 case 一律走 V2。
 */

#define MBOX_TEST_MAGIC_V2    0xABCE

/* 扩展 case 编号（与各工程 Kconfig 的 MBOX_TEST_CASE_ID 取值一一对应） */
#define MBOX_CASE_BIG_CRC     1
#define MBOX_CASE_STRESS      2
#define MBOX_CASE_CONSUMERS   3
#define MBOX_CASE_INVALID     4
#define MBOX_CASE_RTC_SYNC    5
#define MBOX_CASE_FIFO_FULL   6

/* V2 操作码（ASCII 助记符，避免与 CCC/其它协议码混淆） */
#define MBOX_V2_OP_ECHO   'P'   /* 回环请求 / 回环应答 */
#define MBOX_V2_OP_DATA   'D'   /* 大数据分片（seq = 分片序号） */
#define MBOX_V2_OP_ACK    'A'   /* 分片/回环确认 */
#define MBOX_V2_OP_DONE   'E'   /* 分片传输结束（aux = 发送端 CRC32） */
#define MBOX_V2_OP_STAT   'S'   /* 统计上报（aux = 结果码） */
#define MBOX_V2_OP_TS     'T'   /* RTC 时间戳请求/应答 */

/* V2 状态码（status 字段） */
#define MBOX_V2_ST_OK       0
#define MBOX_V2_ST_CRC      1   /* CRC32 与发送端不一致 */
#define MBOX_V2_ST_SEQ      2   /* 序号不连续/越界 */
#define MBOX_V2_ST_PATTERN  3   /* payload pattern 不符 */
#define MBOX_V2_ST_TIMEOUT  4   /* 等待超时 */
#define MBOX_V2_ST_ARG      5   /* 参数/设备不满足 */

/* V2 消息体：与 ch4/ch5 现有 MTU 严格等长（12 B 头部 + 52 B 载荷 = 64 B），
 * 发送时按通道 width 整帧发出，避免 FIFO item 尾部的陈旧字节参与比对。 */
#define MBOX_TEST_V2_HDR_LEN   12
#define MBOX_TEST_V2_DATA_LEN  52
#define MBOX_TEST_V2_MSG_LEN   (MBOX_TEST_V2_HDR_LEN + MBOX_TEST_V2_DATA_LEN)

struct mbox_test_msg_v2 {
	uint16_t magic;    /* MBOX_TEST_MAGIC_V2 */
	uint8_t  op;       /* MBOX_V2_OP_* */
	uint8_t  status;   /* MBOX_V2_ST_* */
	uint32_t seq;      /* 分片序号 / 消息序号 */
	uint32_t aux;      /* op 相关：CRC32 / 时间戳 / 计数 */
	uint8_t  payload[MBOX_TEST_V2_DATA_LEN];
} __packed;

_Static_assert(sizeof(struct mbox_test_msg_v2) == MBOX_TEST_V2_MSG_LEN,
	       "V2 帧长度必须与 ch4/ch5 MTU 一致");

/*
 * 最小探针帧（8 B）：V2 头部前 8 字节，供 MTU 只有 8 B 的 consumer
 * 通道（ch2/ch3）使用。
 */
struct mbox_test_probe {
	uint16_t magic;
	uint8_t  op;
	uint8_t  status;
	uint32_t seq;
} __packed;

/* 大数据分片 case 的总长度与分片数（ceil(4096/52) = 79 片，末片部分有效） */
#define MBOX_BIG_CRC_TOTAL   4096
#define MBOX_BIG_CRC_CHUNKS  ((MBOX_BIG_CRC_TOTAL + MBOX_TEST_V2_DATA_LEN - 1) / \
			      MBOX_TEST_V2_DATA_LEN)
/* 分片数据 pattern：第 n 字节 = (uint8_t)n */
#define MBOX_BIG_CRC_PATTERN(n)  ((uint8_t)(n))

/* 压力 case 的消息条数 */
#define MBOX_STRESS_COUNT    1000
/* FIFO 满 case：ch4/ch5 fifo 深度（板级 dts mboxes 第 3/5 个 cell） */
#define MBOX_FIFO_DEPTH      4

/*
 * CRC-32（IEEE 802.3，反射，poly 0xEDB88320）。
 * 双核同源实现，用于大数据完整性校验与压力测试的一致性判据。
 * crc 初值传 0。
 */
static inline uint32_t mbox_test_crc32(const uint8_t *buf, uint32_t len, uint32_t crc)
{
	crc = ~crc;
	for (uint32_t i = 0; i < len; i++) {
		crc ^= buf[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xEDB88320u * (crc & 1u));
		}
	}
	return ~crc;
}

/*
 * 公历日期 → Unix epoch 秒（Howard Hinnant days_from_civil 算法）。
 * RTC 双核时间同步 case 用：两侧把 struct rtc_time 折算成同一基准的
 * 整数秒后比对，避免依赖 libc/timeutil 的时区语义。
 */
static inline int32_t mbox_test_days_from_civil(int32_t y, int32_t m, int32_t d)
{
	int32_t era, yoe, doy, doe;

	y -= (m <= 2) ? 1 : 0;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;                                   /* [0, 399] */
	doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1; /* [0, 365] */
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           /* [0, 146096] */
	return era * 146097 + doe - 719468;
}

/* year/mon0(=tm_mon, 0-11)/mday/hour/min/sec → epoch 秒 */
static inline uint32_t mbox_test_epoch(int32_t year, int32_t mon0, int32_t mday,
				       int32_t hour, int32_t min, int32_t sec)
{
	int32_t days = mbox_test_days_from_civil(year, mon0 + 1, mday);

	return (uint32_t)((int64_t)days * 86400 + hour * 3600 + min * 60 + sec);
}

#endif /* MBOX_TEST_PROTOCOL_H */
