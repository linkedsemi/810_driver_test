/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * eMMC 块层测试共享常量（cpu1 固件 + 用例）。
 *
 * 被测硬件：板载 eMMC（非外插卡），7.28 GiB，8-bit，
 *           sdhci1@40049000（别名 sdhc0），零外部接线。
 * 被测驱动：zephyr/drivers/sdhc/sdhci-of-linkedsemi.c（linkedsemi,sdhci）
 *           + zephyr/drivers/disk/mmc_subsys.c（zephyr,mmc-disk）
 */
#ifndef EMMC_BLOCK_TEST_H
#define EMMC_BLOCK_TEST_H

#include <zephyr/devicetree.h>

/* sdhci1 控制器节点（板级 dts 已 okay） */
#define EMMC_SDHCI_NODE DT_NODELABEL(sdhci1)

/* FatFS / disk_access 盘名：必须与 DT mmc_disk1 的 disk-name 一致 */
#define EMMC_DISK_NAME "SD2"

/* 板载 eMMC 容量（roadmap §5.1 已核实）：
 *   TOTAL_SECTORS = 15269888（7.28 GiB）
 *   原生块大小 512 B
 * 用 ±2% 窗口断言，避免把"实测与 roadmap 的微小出入"当成假 FAIL。 */
#define EMMC_EXPECT_SECTORS 15269888U
#define EMMC_SECTORS_TOLERANCE (EMMC_EXPECT_SECTORS / 50U)
#define EMMC_SECTOR_SIZE 512U

/* 破坏性用例的 scratch 扇区（用户裁定允许格式化整片）：
 * 取 8 MiB 偏移处，远离 MBR（扇区 0）与 FAT 区，也远离盘尾。 */
#define EMMC_SCRATCH_SECTOR 0x00004000U

/* 不在 linkedsemi_sdhci_request() 的 data-opcode 白名单里的命令
 * （sdhci-of-linkedsemi.c:453-474）。用 SD SEND_STATUS = 13：
 * 该分支在持锁状态下 return -ENOTSUP（:475-478），是本工程的缺陷靶点。 */
#define EMMC_INVALID_DATA_OPCODE 13U

#endif /* EMMC_BLOCK_TEST_H */
