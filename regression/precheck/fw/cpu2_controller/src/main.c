/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 环境预检固件（controller 核）：最小 boot，纯链路验证。
 * 只打角色 banner + ZT_BOOT 后挂起；不依赖任何外设。
 * 串口：uart5（COM15）。
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

void main(void)
{
	printk("[PRE CK CTRL] precheck controller boot\n");
	printk("ZT_BOOT CTRL\n");
	while (1) {
		k_sleep(K_FOREVER);
	}
}
