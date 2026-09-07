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

#endif /* MBOX_TEST_PROTOCOL_H */
