/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethernet 测试 —— B 组数据面①：ARP 解析 + ICMP echo 收发校验
 *
 * 覆盖对象：
 *   zephyr/subsys/net/lib/config/init.c     静态 IPv4 地址写入（net_config 库）
 *   zephyr/subsys/net/l2/ethernet/arp.c     IPv4 发送路径内的 ARP 解析/缓存
 *   zephyr/subsys/net/ip/icmp.c, icmpv4.c   ICMPv4 Echo-Request / Echo-Reply
 *   zephyr/drivers/ethernet/eth_dwmac.c     真实收发路径 dwmac_send / dwmac_receive
 *                                           （eth_dev_ready 只碰设备契约，不碰数据面）
 *
 * 外部路径（SPEC_REFERENCE §6.6）：板载 RJ45 <-> PC 网口（网线）。
 *   对端要求：PC 网口上电且自协商成功 + PC 侧同网段静态 IPv4 + 放行 ICMPv4 回显请求
 *   （详见 tests/COVERAGE.md「PC 侧前置条件」）。
 *
 * 判据设计（防 flaky）：ARP 首包解析失败、ICMP 首包丢失是常态，
 *   故用「N 次重试 + 每次独立超时」；命中 1 次即 PASS —— 一次成功的
 *   请求/应答同时证明「ARP 解析到 PC」与「ICMP 往返通」两件事。
 *   统计值（发送/接收/丢包/RTT 极值）用 ZT_INFO 上报，不做精确值断言。
 *
 * 注意：ARP 表在本版本 Zephyr 无公开查询 API（无 net/arp.h），故「ARP 是否解析成功」
 *   以**回包的源 IPv4 地址等于对端地址**为证据（ARP 不成功则收不到该对端的回包）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/icmp.h>
#include <zephyr/net/socket.h>
#include <zephyr/ztest.h>
#include <string.h>

#include "ztest_glue.h"   /* ZT_INFO 约定行（SPEC_REFERENCE §6.5.4） */

/* 约定值（P6 按实际网络调整时只改 prj.conf 的 CONFIG_NET_CONFIG_* 两行） */
#define LOCAL_IPV4       CONFIG_NET_CONFIG_MY_IPV4_ADDR
#define PEER_IPV4        CONFIG_NET_CONFIG_PEER_IPV4_ADDR

/* 重试窗口：5 次 × 2 s 上限 = 10 s，远小于 case_timeout */
#define PING_ATTEMPTS    5U
#define PING_TIMEOUT_MS  2000
#define PING_PAYLOAD_LEN 32U

static K_SEM_DEFINE(reply_sem, 0, 1);

/* handler 在 net rx 线程执行：只记账 + give 信号量，禁 log/禁阻塞（§6.1.2 同款纪律） */
static volatile int64_t last_send_uptime_ms;
static volatile uint32_t reply_total;
static volatile uint32_t reply_from_peer;
static volatile int64_t rtt_last_us;
static volatile int64_t rtt_min_us;
static volatile int64_t rtt_max_us;

static int echo_reply_handler(struct net_icmp_ctx *ctx, struct net_pkt *pkt,
			      struct net_icmp_ip_hdr *hdr,
			      struct net_icmp_hdr *icmp_hdr, void *user_data)
{
	int64_t now;
	int64_t sent;
	struct in_addr peer;

	ARG_UNUSED(ctx);
	ARG_UNUSED(pkt);
	ARG_UNUSED(icmp_hdr);
	ARG_UNUSED(user_data);

	if (hdr == NULL || hdr->family != AF_INET) {
		return 0;
	}

	reply_total++;

	/* 回包源地址 == 对端地址 ⇒ ARP 解析到的就是 PC 本机 */
	if (net_addr_pton(AF_INET, PEER_IPV4, &peer) == 0 &&
	    memcmp(&hdr->ipv4->src, &peer, sizeof(peer)) == 0) {
		reply_from_peer++;
	}

	now = k_uptime_get();
	sent = last_send_uptime_ms;
	if (sent != 0) {
		int64_t rtt = (now - sent) * 1000;   /* ms -> us */

		rtt_last_us = rtt;
		if (rtt_min_us == 0 || rtt < rtt_min_us) {
			rtt_min_us = rtt;
		}
		if (rtt > rtt_max_us) {
			rtt_max_us = rtt;
		}
	}

	k_sem_give(&reply_sem);

	return 0;
}

/* ==================================================================
 * eth_arp_icmp：静态 IP -> ARP 解析 PC -> ICMP echo 收发校验
 * ================================================================== */
ZTEST(eth_arp_icmp, eth_arp_icmp)
{
	const struct device *eth = DEVICE_DT_GET(DT_NODELABEL(eth1));
	struct net_if *iface;
	struct sockaddr_in dst = { 0 };
	struct in_addr local;
	struct net_icmp_ping_params params = { 0 };
	struct net_icmp_ctx ctx;
	static uint8_t payload[PING_PAYLOAD_LEN];
	uint32_t sent = 0;
	uint32_t got = 0;
	bool src_ok = false;
	int ret;

	iface = net_if_lookup_by_dev(eth);
	zassert_not_null(iface, "eth1 未绑定 net_if");

	/* 1) 静态 IPv4 配置生效（CONFIG_NET_CONFIG_MY_IPV4_ADDR 由 net_config 写入） */
	ret = net_addr_pton(AF_INET, LOCAL_IPV4, &local);
	zassert_equal(ret, 0, "本机地址 \"%s\" 解析失败", LOCAL_IPV4);

	if (net_if_ipv4_addr_lookup(&local, NULL) == NULL) {
		/* 地址未由 net_config 写入时自补一次，避免判定被启动时序左右 */
		struct net_if_addr *added =
			net_if_ipv4_addr_add(iface, &local, NET_ADDR_MANUAL, 0);

		zassert_not_null(added, "静态 IPv4 \"%s\" 既未配置也无法添加", LOCAL_IPV4);
	}

	/* 2) 对端地址（约定值） */
	dst.sin_family = AF_INET;
	ret = net_addr_pton(AF_INET, PEER_IPV4, &dst.sin_addr);
	zassert_equal(ret, 0, "对端地址 \"%s\" 解析失败", PEER_IPV4);

	/* 3) 注册 ICMP Echo-Reply 处理（handler 在 net rx 线程，禁阻塞） */
	memset(&ctx, 0, sizeof(ctx));
	ret = net_icmp_init_ctx(&ctx, NET_ICMPV4_ECHO_REPLY, 0, echo_reply_handler);
	zassert_equal(ret, 0, "net_icmp_init_ctx ret=%d", ret);

	for (uint32_t i = 0; i < PING_PAYLOAD_LEN; i++) {
		payload[i] = (uint8_t)(0xE0U + i);
	}

	params.identifier = 0x4554U;      /* 'ET' */
	params.data = payload;
	params.data_size = sizeof(payload);

	/* 4) N 次重试：每次独立超时；命中 1 次即通过 */
	for (uint32_t i = 0; i < PING_ATTEMPTS; i++) {
		/* 清掉上一轮迟到/超时后到达的回包，避免串轮 */
		while (k_sem_take(&reply_sem, K_NO_WAIT) == 0) {
			/* drain */
		}

		params.sequence = (uint16_t)i;
		last_send_uptime_ms = k_uptime_get();
		rtt_last_us = 0;

		ret = net_icmp_send_echo_request(&ctx, iface, (struct sockaddr *)&dst,
						 &params, NULL);
		zassert_equal(ret, 0, "第 %u 次 net_icmp_send_echo_request ret=%d", i, ret);
		sent++;

		if (k_sem_take(&reply_sem, K_MSEC(PING_TIMEOUT_MS)) == 0) {
			got++;
			src_ok = (reply_from_peer > 0);
			break;                /* 一次成功即证明通路 */
		}
	}

	ret = net_icmp_cleanup_ctx(&ctx);
	zassert_equal(ret, 0, "net_icmp_cleanup_ctx ret=%d", ret);

	ZT_INFO("eth_arp_icmp",
		"peer=%s,local=%s,sent=%u,recv=%u,loss_pct=%u,reply_total=%u,"
		"rtt_us=%ld,rtt_min_us=%ld,rtt_max_us=%ld",
		PEER_IPV4, LOCAL_IPV4, sent, got,
		sent ? (unsigned)((sent - got) * 100U / sent) : 0U,
		(unsigned)reply_total,
		(long)rtt_last_us, (long)rtt_min_us, (long)rtt_max_us);

	/* 5) 判据：至少 1 次「源地址 = 对端」的回包（同时证明 ARP 与 ICMP 往返） */
	zassert_true(got > 0,
		     "对端 %s 在 %u 次尝试（每次 %d ms）内无 ICMP 回包："
		     "需确认 PC 网口已激活、同网段静态 IP 已配、ICMPv4 回显请求未被防火墙拦截",
		     PEER_IPV4, PING_ATTEMPTS, PING_TIMEOUT_MS);
	zassert_true(src_ok,
		     "收到 %u 个回包但源地址不是对端 %s（ARP 解析到的不是 PC？）",
		     (unsigned)reply_total, PEER_IPV4);
}

ZTEST_SUITE(eth_arp_icmp, NULL, NULL, NULL, NULL, NULL);
