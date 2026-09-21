/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PHY 链路状态公共辅助（工程内复用；case_mdio 与 case_link 两个源集共享）。
 *
 * 均为 static inline，随各 case 的源文件编入本 case 的镜像；
 * 不产生跨编译单元符号，也不引入链接依赖。
 */

#ifndef ETH_PHY_COMMON_H
#define ETH_PHY_COMMON_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/net/phy.h>

/* 板载 RTL8211F PHY（mdio1 下的 ethernet-phy@0） */
static inline const struct device *eth_phy_dev(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(eth1_phy));
}

/* RTL8211F 的链路速率码 -> Mbps（PHYSR 的 LINKDUPLEX/LINKSPEED 位域解码） */
static inline unsigned int link_speed_mbps(enum phy_link_speed speed)
{
	if (PHY_LINK_IS_SPEED_1000M(speed)) {
		return 1000U;
	}
	if (PHY_LINK_IS_SPEED_100M(speed)) {
		return 100U;
	}
	return 10U;
}

static inline const char *link_duplex_str(enum phy_link_speed speed)
{
	return PHY_LINK_IS_FULL_DUPLEX(speed) ? "full" : "half";
}

/* 速率码是否为 802.3 合法组合（10/100/1000 Mbps × half/full） */
static inline bool link_speed_is_valid(enum phy_link_speed speed)
{
	return speed == LINK_HALF_10BASE_T || speed == LINK_FULL_10BASE_T ||
	       speed == LINK_HALF_100BASE_T || speed == LINK_FULL_100BASE_T ||
	       speed == LINK_HALF_1000BASE_T || speed == LINK_FULL_1000BASE_T;
}

#endif /* ETH_PHY_COMMON_H */
