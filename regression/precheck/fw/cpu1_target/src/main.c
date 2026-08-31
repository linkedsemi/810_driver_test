/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 环境预检固件（target 核）：最小 boot，纯链路验证。
 * 只打角色 banner + ZT_BOOT 后挂起；不依赖任何外设。
 * 串口：uart1（COM16）。
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

void main(void)
{
	printk("[PRE CK TGT] precheck target boot\n");
	printk("ZT_BOOT TGT\n");
	while (1) {
		k_sleep(K_FOREVER);
	}
}
