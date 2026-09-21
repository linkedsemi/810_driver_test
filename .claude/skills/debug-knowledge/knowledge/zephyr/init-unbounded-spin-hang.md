---
name: init-unbounded-spin-hang
description: 初始化阶段无超时轮询/自旋（等器件就绪）导致固件永久挂死、串口 0 行——与烧录/配置问题同症状的判别法
tags: [init-hang, spin-wait, no-timeout, phy, serial-silent, post-kernel]
date: 2026-09-17
source: phy_realtek_rtl8211f.c 的 phy_rt_rtl8211f_reset（eth_test 实机暴露）+ KNOWN_ISSUES #71
---

# 初始化期无超时自旋 → 永久挂死（非异常，串口 0 行）

## 现象

固件 load/烧录成功，**串口 0 行**（无 banner、无 `\x00`、无 trap 打印），与"烧录失败 /
入口地址设错 / 配置缺失"完全同形。只在串口层排查会白查一轮。

## 根因

初始化路径（Zephyr 的 `POST_KERNEL` 等）里有**无上限**的轮询/自旋：等外部器件进入某状态
（PHY 复位完成、器件 ID 可读、ready 标志、晶振起振）。器件不应答或返回**意外但合法**的值时
循环永不退出 → CPU 卡在**正常代码路径**，**不产生异常**，故没有 trap/mcause 输出。

关键区分：`read` 返回**错误码**时驱动往往 `return ret`（有日志、能定位）；真正致命的是
**read 成功但值 ≠ 期望** —— 此时连错误日志都没有，表现为纯静默。

## 排查方法

1. gdb attach 读 PC / `bt`（qsh-debug §6 命令模板）。
2. PC 落在驱动源码的等待循环（非异常入口、非调试期 `while(1)` 陷阱）→ 即本类。
3. 与"异常入口挂死"区分：异常入口挂死可读 `mcause`/`mepc`（riscv 域 [[trap-hang-csr-scene]]）；
   本类 `mcause` 无意义，PC 在正常 `.text` 段循环。
4. 与"链路/烧录问题"区分：用**早标记分界**（embedded-debug §2.2）——load 正常却连最早的 printk
   都没有，直接抓 PC，不要在串口/配置层反复试。

## 修复模式

给自旋加超时并走可诊断的失败路径：

```c
k_timepoint_t end = sys_timepoint_calc(K_MSEC(100));

do {
        ret = drv_read(dev, REG, &val);
        if (ret) {
                return ret;
        }
} while ((val != EXPECTED) && !sys_timepoint_expired(end));

if (val != EXPECTED) {
        return -ETIMEDOUT;      /* 而不是无限等 */
}
```

## 实际案例

`zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c` 的 `phy_rt_rtl8211f_reset()`：
`do { read(MII_PHYID1R, &v); } while (v != REALTEK_OUI_MSB);`（**无超时**）。PHY 不应答时在
POST_KERNEL **永久挂死** → 固件不启动、串口 0 行（KNOWN_ISSUES #71）。带 eth1 的工程遇
"load 后串口 0 行"应**优先**怀疑此项，而不是先走 #38/#39/#40 那组（固件没跑/崩溃/配置）。

## 链路
> **相关**→ [[trap-hang-csr-scene]]（异常入口挂死，有 mcause 可读）· embedded-debug §2.1/§2.2 ·
> **skill**→ qsh-debug §0/§7
