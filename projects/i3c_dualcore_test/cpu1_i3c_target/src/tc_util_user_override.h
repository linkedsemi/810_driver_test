/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ztest 输出的**单出口**后端（SPEC_REFERENCE §6.9.3 / §6.11.5）。
 *
 * 启用方式（CMakeLists.txt）：
 *   zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
 * + prj.conf：CONFIG_ZTEST_TC_UTIL_USER_OVERRIDE=y
 *
 * 原理
 * ----
 * ztest 的全部逐项输出（`START - <name>` / ` PASS - <name> in X.XXX seconds` /
 * `====…` / `INFO` / `PROJECT EXECUTION` / 断言行）都经 `tc_util.h` 的
 * `PRINT_DATA` 发射。上游为此留了覆盖点：`CONFIG_ZTEST_TC_UTIL_USER_OVERRIDE`
 * ⇒ `tc_util.h` 在定义 `PRINT_DATA` **之前** include 本头文件，本头的
 * `#define` 抢先命中，ztest 源码零改动即完成换后端。
 *
 * 覆盖点必须对**所有** TU 可见（`ztest.c` 自身也用 PRINT_DATA）⇒ 只能用
 * `zephyr_include_directories()`（挂在 zephyr_interface 上），用
 * `target_include_directories(app …)` 会让 ztest 库编不过。
 *
 * 谁是"单出口"
 * ------------
 * 本核是**从核**（TGT）：不往自己的串口打 token，全部经 RLOG 交主核原样转发。
 * 现场保留不受影响——`__ASSERT` / `ZEPHYR FATAL ERROR` / 内核 oops 的寄存器
 * 与调用栈走内核自己的 printk，**不经 PRINT_DATA**，仍留在本核串口上。
 * 本核的 LOG 子系统输出（角色 banner 等）也仍走本核串口（不是 token）。
 *
 * 为什么只声明、不 include "rlog.h"
 * ---------------------------------
 * 本头被 ztest 库的 TU 也包含，而 `common/` 只挂在 app target 的 include 路径
 * 上（common_sources.cmake）⇒ 这里直接声明所需原型，避免扩大全局 include 面。
 */
#ifndef TC_UTIL_USER_OVERRIDE_H
#define TC_UTIL_USER_OVERRIDE_H

/* `common/rlog.h` 的同名声明（签名必须逐字一致）。 */
void rlog_emitf(const char *fmt, ...);

#ifndef PRINT_DATA
#if defined(RLOG_LOCAL_ECHO)
/* Integrated dual set: keep the cpu1 console informative while still relaying
 * the token to the controller. Echoing every START/PASS line with blocking
 * printk slowed the target enough to break tight read-path timing (run10c:
 * 24/35 on the controller). Echo only FAIL and SUMMARY locally; the full line
 * set still reaches the controller over RLOG. */
#include <zephyr/kernel.h>
#include <string.h>
#define PRINT_DATA(fmt, ...)                                                   \
	do {                                                                   \
		rlog_emitf(fmt, ##__VA_ARGS__);                                \
		if (strstr(fmt, "SUMMARY") != NULL ||                          \
		    strstr(fmt, "FAIL") != NULL) {                             \
			printk(fmt, ##__VA_ARGS__);                            \
		}                                                              \
	} while (0)
#else
#define PRINT_DATA(fmt, ...) rlog_emitf(fmt, ##__VA_ARGS__)
#endif
#endif

#endif /* TC_UTIL_USER_OVERRIDE_H */
