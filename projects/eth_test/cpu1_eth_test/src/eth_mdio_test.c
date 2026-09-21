/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethernet / MDIO 测试 —— A 组（零对端，不依赖网线状态）
 *
 * 覆盖对象：
 *   zephyr/drivers/mdio/mdio_dwmac.c
 *     （compatible "snps,dwmac-mdio"，实例 mdio1@0x40064000；
 *       对外 API：mdio_read / mdio_write，经 mdio_dwmac_transfer()）
 *   zephyr/drivers/ethernet/eth_dwmac_linkedsemi.c + eth_dwmac.c
 *     （compatible "linkedsemi,ethernet"，实例 eth1@0x40064000）
 *   zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c
 *     （仅**弱判据** eth_phy_link_state：读 PHYSR(0x1A) 解码链路状态，
 *       不断言 link 必须 up ⇒ 无对端也能确定判定，故归 A 组）
 *
 * 硬件：板载 RTL8211F PHY，挂 mdio1 下，phy addr = 0
 *       （zephyr/boards/linkedsemi/lsqsh_evb/lsqsh_evb_cpu1.dts:268-276）。
 *
 * 外部路径：无 —— 只与板载 PHY 通话，网线插拔不影响本组判定。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mdio.h>
#include <zephyr/net/mii.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/ztest.h>
#include <string.h>

#include "ztest_glue.h"       /* ZT_INFO 约定行（SPEC_REFERENCE §6.5.4） */
#include "eth_phy_common.h"   /* eth_phy_dev / link_speed_mbps / link_duplex_str */

/* 板载 RTL8211F：mdio1 下的 ethernet-phy@0 */
#define ETH_PHY_ADDR      0U
/* Realtek OUI = 0x001CC9（PHYID1R[15:0] + PHYID2R[15:8]）。
 * PHYID2R 低字节是 model/revision，随 RTL8211F 具体型号而异，故只断言 OUI。 */
#define RTL8211F_PHYID1   0x001CU
#define RTL8211F_OUI_HI   0xC900U

/* mdio_dwmac.c 的地址寄存器把 PHY addr / reg 直接裁进 5 位字段
 * （mdio_address_t.PA[4:0] / .RDA[4:0]），驱动无范围校验 ⇒ 越界索引
 * 静默按 5 位回绕。本组把该行为钉住。 */
#define MDIO_ADDR_WRAP    32U

static const struct device *eth_mdio_dev(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(mdio1));
}

/* ==================================================================
 * TC-1: MDIO 读 PHY 标识（功能通路）
 *   读 mdio_dwmac_read() -> mdio_dwmac_transfer() 的 C22 读路径。
 * ================================================================== */
ZTEST(eth_mdio, mdio_phy_id)
{
	const struct device *mdio = eth_mdio_dev();
	uint16_t id1 = 0;
	uint16_t id2 = 0;
	int ret;

	zassert_true(device_is_ready(mdio), "mdio1 device is not ready");

	ret = mdio_read(mdio, ETH_PHY_ADDR, MII_PHYID1R, &id1);
	zassert_equal(ret, 0, "mdio_read(PHYID1R) ret=%d", ret);

	ret = mdio_read(mdio, ETH_PHY_ADDR, MII_PHYID2R, &id2);
	zassert_equal(ret, 0, "mdio_read(PHYID2R) ret=%d", ret);

	ZT_INFO("mdio_phy_id", "addr=%u,id1=0x%04x,id2=0x%04x,oui=0x%06x",
		ETH_PHY_ADDR, id1, id2,
		((uint32_t)id1 << 8) | (uint32_t)(id2 >> 8));

	/* 非退化：全 0/全 1 说明 MDIO 通路没真正取到数据 */
	zassert_false(id1 == 0x0000 && id2 == 0x0000,
		      "PHY ID 读回全 0x0000（MDIO 通路未取到数据）");
	zassert_false(id1 == 0xFFFF && id2 == 0xFFFF,
		      "PHY ID 读回全 0xFFFF（MDIO 通路未取到数据）");

	/* 归因顺序（★ 排障提示）：读到 0x0000 / 0xFFFF / 非 Realtek OUI 时，
	 * **先查 PHY 复位是否成功**，不要先怀疑下面的期望值（0x001C / 0xC9 已核实正确）：
	 *   mdio_dwmac.c:275 调用 mdio_dwmac_ethphy_reset() 的返回值**已修**为
	 *   被上报（:276-279 `if (ret) { DEV_ERR("PHY reset failed"); goto done; }`）
	 *   ⇒ 复位失败现在会使 mdio_dwmac_init() 返回非 0，
	 *     `device_is_ready(mdio1)` 随之先失败，不会再静默走到本项。
	 * （历史：修前该返回值被丢弃，复位失败不报错、PHY 可能仍处复位态，
	 *   MDIO 读回垃圾值 —— 这条排障提示即源于那次缺陷。） */
	zassert_equal(id1, RTL8211F_PHYID1,
		      "PHYID1R=0x%04x 不是 Realtek OUI 高 16 位 0x%04x",
		      id1, RTL8211F_PHYID1);
	zassert_equal(id2 & 0xFF00U, RTL8211F_OUI_HI,
		      "PHYID2R=0x%04x 的 OUI 低 8 位 != 0x%02x（非 Realtek PHY）",
		      id2, RTL8211F_OUI_HI >> 8);
}

/* ==================================================================
 * TC-2: MDIO 寄存器读写回环（功能通路）
 *   走 mdio_dwmac_write() 的写分支（is_write -> 先写 MDIO_DATA 再写
 *   MDIO_ADDRESS）与随后的读回。
 *
 *   选 ANAR(0x04) 作为读写靶：802.3 clause 22 规定其 16 位全 RW，且
 *   **修改 ANAR 不重启自协商**（只有写 BMCR 才会），故对在跑的链路无副作用；
 *   写完立即恢复原值。
 * ================================================================== */
ZTEST(eth_mdio, mdio_reg_rw)
{
	const struct device *mdio = eth_mdio_dev();
	uint16_t anar_orig = 0;
	uint16_t anar_probe = 0;
	uint16_t anar_back = 0;
	uint16_t bmcr_a = 0;
	uint16_t bmcr_b = 0;
	uint16_t modified;
	int ret;

	/* 读通路可重复性：同寄存器连续两次读必须一致 */
	ret = mdio_read(mdio, ETH_PHY_ADDR, MII_BMCR, &bmcr_a);
	zassert_equal(ret, 0, "mdio_read(BMCR) ret=%d", ret);
	ret = mdio_read(mdio, ETH_PHY_ADDR, MII_BMCR, &bmcr_b);
	zassert_equal(ret, 0, "mdio_read(BMCR) ret=%d", ret);
	zassert_equal(bmcr_a, bmcr_b,
		      "BMCR 连续两次读不一致: 0x%04x / 0x%04x", bmcr_a, bmcr_b);
	/* 复位位自清：读到 1 说明复位未完成 */
	zassert_equal(bmcr_a & MII_BMCR_RESET, 0,
		      "BMCR.RESET 仍为 1（PHY 复位未完成）: 0x%04x", bmcr_a);

	/* 写通路：改一位（ANAR bit5 = 100BASE-TX half duplex）后读回 */
	ret = mdio_read(mdio, ETH_PHY_ADDR, MII_ANAR, &anar_orig);
	zassert_equal(ret, 0, "mdio_read(ANAR) ret=%d", ret);
	ZT_INFO("mdio_reg_rw", "anar_orig=0x%04x", anar_orig);

	modified = (uint16_t)(anar_orig ^ 0x0020U);

	ret = mdio_write(mdio, ETH_PHY_ADDR, MII_ANAR, modified);
	zassert_equal(ret, 0, "mdio_write(ANAR, 0x%04x) ret=%d", modified, ret);

	ret = mdio_read(mdio, ETH_PHY_ADDR, MII_ANAR, &anar_probe);
	zassert_equal(ret, 0, "mdio_read(ANAR) ret=%d", ret);

	/* 恢复原值 */
	ret = mdio_write(mdio, ETH_PHY_ADDR, MII_ANAR, anar_orig);
	zassert_equal(ret, 0, "mdio_write(ANAR, 0x%04x) ret=%d", anar_orig, ret);

	ret = mdio_read(mdio, ETH_PHY_ADDR, MII_ANAR, &anar_back);
	zassert_equal(ret, 0, "mdio_read(ANAR) ret=%d", ret);

	ZT_INFO("mdio_reg_rw", "anar_w=0x%04x,anar_rb=0x%04x,anar_restored=0x%04x",
		modified, anar_probe, anar_back);

	zassert_equal(anar_probe, modified,
		      "ANAR 写回读不相等: 写 0x%04x 读回 0x%04x", modified, anar_probe);
	zassert_equal(anar_back, anar_orig,
		      "ANAR 恢复失败: 原值 0x%04x 恢复后读回 0x%04x",
		      anar_orig, anar_back);
}

/* ==================================================================
 * TC-3: MDIO 越界参数（异常参数 / 边界长度）
 *   驱动对 phy addr / reg 无范围校验，两者都被裁进 5 位字段：
 *   越界索引静默回绕到 addr 0 / reg 0。
 *
 *   注意：**不测“无 PHY 的地址必须报错”** —— DW MAC 的 MDIO 事务不区分
 *   “从设备无响应”，GB 位照样清零并返回上一次的数据，因此“读到错误码”
 *   这一判据在本平台上不存在（见 COVERAGE.md 跳过项）。
 * ================================================================== */
ZTEST(eth_mdio, mdio_invalid_args)
{
	const struct device *mdio = eth_mdio_dev();
	uint16_t reg0 = 0;
	uint16_t wrap_addr = 0;
	uint16_t wrap_reg = 0;
	uint16_t absent = 0;
	int ret;

	ret = mdio_read(mdio, ETH_PHY_ADDR, 0, &reg0);
	zassert_equal(ret, 0, "mdio_read(addr=0, reg=0) ret=%d", ret);

	/* phy addr 越界：32 -> PA[4:0] = 0 */
	ret = mdio_read(mdio, ETH_PHY_ADDR + MDIO_ADDR_WRAP, 0, &wrap_addr);
	zassert_equal(ret, 0, "mdio_read(addr=%u) ret=%d",
		      ETH_PHY_ADDR + MDIO_ADDR_WRAP, ret);
	zassert_equal(wrap_addr, reg0,
		      "phy addr %u 未按 5 位回绕到 0: 0x%04x vs 0x%04x",
		      ETH_PHY_ADDR + MDIO_ADDR_WRAP, wrap_addr, reg0);

	/* reg 越界：32 -> RDA[4:0] = 0 */
	ret = mdio_read(mdio, ETH_PHY_ADDR, MDIO_ADDR_WRAP, &wrap_reg);
	zassert_equal(ret, 0, "mdio_read(reg=%u) ret=%d", MDIO_ADDR_WRAP, ret);
	zassert_equal(wrap_reg, reg0,
		      "reg %u 未按 5 位回绕到 0: 0x%04x vs 0x%04x",
		      MDIO_ADDR_WRAP, wrap_reg, reg0);

	/* 只观测、不断言：addr 31 无 PHY，但 DW MAC 不区分无响应 */
	ret = mdio_read(mdio, 31, 0, &absent);
	ZT_INFO("mdio_invalid_args",
		"wrap_addr_val=0x%04x,wrap_reg_val=0x%04x,absent_phy31:ret=%d,val=0x%04x",
		wrap_addr, wrap_reg, ret, absent);
}

/* ==================================================================
 * TC-4: 以太网设备 / net_if 契约（功能通路）
 *   覆盖 eth_dwmac_linkedsemi.c 的 ETH_NET_DEVICE_DT_INST_DEFINE 产物：
 *   设备就绪、net_if 绑定、L2 类型、MTU、链路层地址。
 * ================================================================== */
ZTEST(eth_mdio, eth_dev_ready)
{
	const struct device *eth = DEVICE_DT_GET(DT_NODELABEL(eth1));
	struct net_if *iface;
	struct net_linkaddr *link_addr;
	uint8_t or_all = 0;
	uint8_t and_all = 0xFF;
	int i;

	zassert_true(device_is_ready(eth), "eth1 device is not ready");

	iface = net_if_lookup_by_dev(eth);
	zassert_not_null(iface, "eth1 未绑定 net_if");
	zassert_equal(net_if_get_device(iface), eth,
		      "net_if 绑定的设备不是 eth1");
	zassert_equal(net_if_get_mtu(iface), NET_ETH_MTU,
		      "net_if MTU=%u != %u", net_if_get_mtu(iface), NET_ETH_MTU);

	link_addr = net_if_get_link_addr(iface);
	zassert_not_null(link_addr, "net_if link addr 为 NULL");
	zassert_not_null(link_addr->addr, "net_if link addr 缓冲区为 NULL");
	zassert_equal(link_addr->len, NET_ETH_ADDR_LEN,
		      "MAC 长度 %u != %u", link_addr->len, NET_ETH_ADDR_LEN);

	for (i = 0; i < NET_ETH_ADDR_LEN; i++) {
		or_all |= link_addr->addr[i];
		and_all &= link_addr->addr[i];
	}

	ZT_INFO("eth_dev_ready", "mac=%02x:%02x:%02x:%02x:%02x:%02x,mtu=%u,admin_up=%d",
		link_addr->addr[0], link_addr->addr[1], link_addr->addr[2],
		link_addr->addr[3], link_addr->addr[4], link_addr->addr[5],
		net_if_get_mtu(iface), (int)net_if_is_admin_up(iface));

	zassert_not_equal(or_all, 0x00, "MAC 地址全 0（未初始化）");
	zassert_not_equal(and_all, 0xFF, "MAC 地址全 0xFF（未初始化）");
	zassert_equal(link_addr->addr[0] & 0x01U, 0,
		      "MAC 首字节 0x%02x 的组播位置起（应为单播地址）",
		      link_addr->addr[0]);
}

/* ==================================================================
 * TC-5: PHY 链路状态可观测（功能通路，**弱判据** —— A 组，无需对端）
 *   走 phy_rt_rtl8211f_get_link()：加锁 -> 读 PHYSR(0x1A) -> 解码
 *   link/speed/duplex。**不断言 link 必须 up**（取决于对端是否激活），
 *   只断言读路径成功 + 若 up 则解码出的速率码合法 ⇒ 无对端也能确定判定。
 *   （原在 case_link 的 `eth_link` 套件内，按父会话裁定挪入 A 组。）
 * ================================================================== */
ZTEST(eth_mdio, eth_phy_link_state)
{
	const struct device *phy = eth_phy_dev();
	struct phy_link_state st = { 0 };
	int ret;

	zassert_true(device_is_ready(phy), "eth1_phy device is not ready");

	ret = phy_get_link_state(phy, &st);
	zassert_equal(ret, 0, "phy_get_link_state ret=%d", ret);

	/* mdio_reset_*：mdio1 的复位脉冲配置（DT 属性），用于**区分本变体**——
	 * case_mdio 为 0/0（脉冲子块不发脉冲），case_phy_reset 为 10/30（真发脉冲）。
	 * 见 mdio_dwmac.c:231/:252 的门 `reset_hold_ms && circuits_set_ms`。 */
	ZT_INFO("eth_phy_link_state",
		"link=%s,speed_mbps=%u,duplex=%s,speed_code=0x%02x,"
		"mdio_reset_hold_ms=%u,mdio_circuits_set_ms=%u",
		st.is_up ? "up" : "down", link_speed_mbps(st.speed),
		link_duplex_str(st.speed), (unsigned int)st.speed,
		(unsigned int)DT_PROP_OR(DT_NODELABEL(mdio1), reset_hold_ms, 0),
		(unsigned int)DT_PROP_OR(DT_NODELABEL(mdio1), circuits_set_ms, 0));

	if (st.is_up) {
		/* 链路已建立时，速率/双工必须能解码到 802.3 合法组合
		 * （10/100/1000 Mbps × half/full）。 */
		zassert_true(link_speed_is_valid(st.speed),
			     "link up 但解码出的速率码非法: 0x%02x", (unsigned int)st.speed);
	}
}

ZTEST_SUITE(eth_mdio, NULL, NULL, NULL, NULL, NULL);
