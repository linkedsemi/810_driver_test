/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethernet 测试 —— B 组：网口链路建立（需网线对端）
 *
 * 覆盖对象：
 *   zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c
 *     （compatible "realtek,rtl8211f"，实例 = mdio1 下 ethernet-phy@0；
 *        链路状态取 PHY 专有状态寄存器 PHYSR(0x1A)，不是标准 BMSR）
 *   zephyr/drivers/ethernet/eth_dwmac_linkedsemi.c（net_if / L2 绑定）
 *
 * 外部路径（SPEC_REFERENCE §6.6）：**网线 + PC 对端**
 *   板载 RJ45 <--> PC 网口，对端网口须上电并开启自协商。
 *
 * 说明：同族弱判据项 `eth_phy_link_state`（只读 PHY 状态、不断言 up、无需对端）
 *   已按父会话裁定挪入 A 组 `case_mdio`（套件 `eth_mdio`），本 case 只留强判据。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/net/phy.h>
#include <zephyr/ztest.h>

#include "ztest_glue.h"       /* ZT_INFO 约定行（SPEC_REFERENCE §6.5.4） */
#include "eth_phy_common.h"   /* eth_phy_dev / link_speed_mbps / link_duplex_str */

/* 链路协商是毫秒~秒级过程：轮询 + 超时窗口，不做单次采样判定。
 * 窗口取 10 s —— 对端网卡处于省电模式或被固定速率时，自协商可能超过 5 s。 */
#define LINK_UP_TIMEOUT_MS    10000U
#define LINK_POLL_INTERVAL_MS 100U

static bool poll_link_up(struct phy_link_state *state, uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + (int64_t)timeout_ms;

	do {
		if (phy_get_link_state(eth_phy_dev(), state) == 0 && state->is_up) {
			return true;
		}
		k_msleep(LINK_POLL_INTERVAL_MS);
	} while (k_uptime_get() < deadline);

	return false;
}

/* ==================================================================
 * eth_link_up：网口链路建立（功能通路）—— 需对端
 *   外部: 板载 RJ45 <-> PC 网口（网线），对端网口须上电并在自协商。
 *   10000 ms 轮询窗口内 PHY 报 link up 即 PASS；超时仍 down 判 FAIL，
 *   提示先查对端网口状态（不是单次采样的时序抖动）。
 * ================================================================== */
ZTEST(eth_link, eth_link_up)
{
	struct phy_link_state st = { 0 };
	bool up = poll_link_up(&st, LINK_UP_TIMEOUT_MS);

	ZT_INFO("eth_link_up", "link=%s,speed_mbps=%u,duplex=%s,window_ms=%u",
		up ? "up" : "down", link_speed_mbps(st.speed),
		link_duplex_str(st.speed), LINK_UP_TIMEOUT_MS);

	zassert_true(up,
		     "网线对端 %u ms 内未建立链路（PHY 报 link down）："
		     "需确认 PC 网口已上电/激活且自协商开启",
		     LINK_UP_TIMEOUT_MS);
}

ZTEST_SUITE(eth_link, NULL, NULL, NULL, NULL, NULL);
