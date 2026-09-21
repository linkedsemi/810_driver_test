/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethernet 测试 —— B 组数据面②：UDP 报文收发 + 载荷校验
 *
 * 覆盖对象：
 *   zephyr/drivers/ethernet/eth_dwmac.c  真实收发路径（dwmac_send / dwmac_receive
 *                                        + RX refill 线程 + 描述符环形队列）
 *   zephyr/subsys/net/ip/                  UDP 封装/IPv4 输出（含 ARP 解析）
 *   zephyr/subsys/net/lib/sockets/         zsock UDP socket 数据面
 *
 * 外部路径（SPEC_REFERENCE §6.6）：板载 RJ45 <-> PC 网口（网线）。
 *   对端要求：PC 网口激活 + 同网段静态 IPv4 + **PC 侧运行 UDP 回声程序**，
 *   把收到的报文原样送回板卡（详见 tests/COVERAGE.md「PC 侧前置条件」）。
 *
 * 判据设计（防 flaky）：
 *   - UDP 无连接，**首包 sendto 因 ARP 未解析而返回 EHOSTUNREACH 属正常**，
 *     故 sendto 失败只计数不硬判；用「N 次重试 + 每次独立超时」兜住。
 *   - 每次载荷带**序号**，回包必须与本次发出的字节**逐字节相等**才计入成功
 *     ⇒ 上一轮的迟到回包会被 memcmp 拒绝，不会造成假成功。
 *   - 命中 1 次即 PASS；统计值（发送/发送失败/接收/校验失败/丢包率/RTT）用
 *     ZT_INFO 上报，不做精确值断言。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/ztest.h>
#include <string.h>

#include "ztest_glue.h"   /* ZT_INFO 约定行（SPEC_REFERENCE §6.5.4） */

/* 约定值（P6 按实际网络调整时只改 prj.conf 的 CONFIG_NET_CONFIG_* 两行） */
#define LOCAL_IPV4   CONFIG_NET_CONFIG_MY_IPV4_ADDR
#define PEER_IPV4    CONFIG_NET_CONFIG_PEER_IPV4_ADDR

/* UDP 回声端口：必须与 PC 侧回声程序监听/回送的端口一致 */
#define UDP_ECHO_PORT   5001U

/* 重试窗口：5 次 × 2 s 上限 = 10 s，远小于 case_timeout */
#define UDP_ATTEMPTS     5U
#define UDP_TIMEOUT_MS   2000
#define UDP_PAYLOAD_LEN  32U

/* 载荷构造：magic + 序号 + 滚动字节。发送与校验共用，保证可逐字节比对。 */
static void build_payload(uint8_t *buf, uint16_t seq)
{
	buf[0] = 0x45U;                       /* 'E' */
	buf[1] = 0x54U;                       /* 'T' */
	buf[2] = (uint8_t)(seq & 0xFFU);
	buf[3] = (uint8_t)(seq >> 8);
	for (uint32_t i = 4; i < UDP_PAYLOAD_LEN; i++) {
		buf[i] = (uint8_t)(0xA5U + i);
	}
}

ZTEST(eth_udp_echo, eth_udp_echo)
{
	const struct device *eth = DEVICE_DT_GET(DT_NODELABEL(eth1));
	struct net_if *iface;
	struct sockaddr_in peer = { 0 };
	struct in_addr local;
	uint8_t tx[UDP_PAYLOAD_LEN];
	uint8_t rx[UDP_PAYLOAD_LEN * 2];      /* 稍大，便于识别异常长度 */
	uint32_t sent = 0;
	uint32_t send_fail = 0;
	uint32_t rx_total = 0;
	uint32_t rx_bad = 0;
	uint32_t verified = 0;
	int64_t rtt_last_us = 0;
	int64_t rtt_min_us = 0;
	int64_t rtt_max_us = 0;
	int ret;
	int sock;

	iface = net_if_lookup_by_dev(eth);
	zassert_not_null(iface, "eth1 未绑定 net_if");

	/* 1) 静态 IPv4 配置生效（CONFIG_NET_CONFIG_MY_IPV4_ADDR 由 net_config 写入） */
	ret = net_addr_pton(AF_INET, LOCAL_IPV4, &local);
	zassert_equal(ret, 0, "本机地址 \"%s\" 解析失败", LOCAL_IPV4);

	if (net_if_ipv4_addr_lookup(&local, NULL) == NULL) {
		struct net_if_addr *added =
			net_if_ipv4_addr_add(iface, &local, NET_ADDR_MANUAL, 0);

		zassert_not_null(added, "静态 IPv4 \"%s\" 既未配置也无法添加", LOCAL_IPV4);
	}

	/* 2) 对端地址（约定值） */
	peer.sin_family = AF_INET;
	peer.sin_port = htons(UDP_ECHO_PORT);
	ret = net_addr_pton(AF_INET, PEER_IPV4, &peer.sin_addr);
	zassert_equal(ret, 0, "对端地址 \"%s\" 解析失败", PEER_IPV4);

	/* 3) UDP socket */
	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	zassert_true(sock >= 0, "zsock_socket ret=%d", sock);

	/* 4) N 次重试：每次独立超时；命中 1 次逐字节相符的回包即通过 */
	for (uint32_t i = 0; i < UDP_ATTEMPTS; i++) {
		int64_t deadline;
		int64_t t0;
		bool ok = false;

		build_payload(tx, (uint16_t)i);
		t0 = k_uptime_get();

		ret = zsock_sendto(sock, tx, sizeof(tx), 0,
				   (struct sockaddr *)&peer, sizeof(peer));
		if (ret < 0) {
			/* ARP 尚未解析 / 无路由等：只计数，继续下一轮 */
			send_fail++;
			continue;
		}
		sent++;

		deadline = t0 + UDP_TIMEOUT_MS;
		while (k_uptime_get() < deadline) {
			struct zsock_pollfd pfd = { .fd = sock, .events = ZSOCK_POLLIN };
			int64_t left = deadline - k_uptime_get();
			struct sockaddr_in from = { 0 };
			socklen_t from_len = sizeof(from);
			int pr;
			int n;

			if (left <= 0) {
				break;
			}

			pr = zsock_poll(&pfd, 1, (int)left);
			if (pr <= 0) {
				break;
			}

			n = zsock_recvfrom(sock, rx, sizeof(rx), 0,
					   (struct sockaddr *)&from, &from_len);
			if (n < 0) {
				break;
			}

			rx_total++;

			if ((uint32_t)n == sizeof(tx) &&
			    memcmp(rx, tx, sizeof(tx)) == 0) {
				int64_t rtt = (k_uptime_get() - t0) * 1000;   /* ms -> us */

				verified++;
				ok = true;
				rtt_last_us = rtt;
				if (rtt_min_us == 0 || rtt < rtt_min_us) {
					rtt_min_us = rtt;
				}
				if (rtt > rtt_max_us) {
					rtt_max_us = rtt;
				}
				break;
			}

			/* 长度或内容不符（可能是上一轮迟到回包）：继续等本轮 */
			rx_bad++;
		}

		if (ok) {
			break;                /* 一次成功即证明收发通路 */
		}
	}

	(void)zsock_close(sock);

	ZT_INFO("eth_udp_echo",
		"peer=%s:%u,local=%s,sent=%u,send_fail=%u,rx_total=%u,rx_bad=%u,"
		"verified=%u,rtt_us=%ld,rtt_min_us=%ld,rtt_max_us=%ld",
		PEER_IPV4, UDP_ECHO_PORT, LOCAL_IPV4,
		sent, send_fail, rx_total, rx_bad, verified,
		(long)rtt_last_us, (long)rtt_min_us, (long)rtt_max_us);

	/* 5) 判据：至少 1 次「逐字节相符」的回包 */
	zassert_true(verified > 0,
		     "对端 %s:%u 在 %u 次尝试（每次 %d ms）内无相符回包 "
		     "(sent=%u,send_fail=%u,rx_total=%u,rx_bad=%u)："
		     "需确认 PC 侧 UDP 回声程序已运行且端口一致、PC 与板卡同网段",
		     PEER_IPV4, UDP_ECHO_PORT, UDP_ATTEMPTS, UDP_TIMEOUT_MS,
		     sent, send_fail, rx_total, rx_bad);
}

ZTEST_SUITE(eth_udp_echo, NULL, NULL, NULL, NULL, NULL);
