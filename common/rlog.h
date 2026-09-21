/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RLOG —— 双核测试的**单出口**日志通道（SPEC_REFERENCE §6.9.3 / §6.9.4）。
 *
 * 角色
 * ----
 *   SECONDARY（从核）：本核不往自己的串口打 token，改把每一行经 mbox 交给主核。
 *   PRIMARY（主核）  ：收下从核的行，**原样**打到自己的串口；从核行因此与主核行
 *                      汇成一路串口流（"单出口"）。
 *
 * 为什么原样转发而不重新格式化
 * ---------------------------
 * 从核的行本身就是 D2 同构 token（`<PREFIX>:<core>:…`，见 §6.9.1），带 core 段
 * ⇒ 采集侧（L1）按 `<PREFIX>:<TGT>:` 前缀把它们分拣回 `serial_TGT.log`，重放输入
 * 形态与"两核各自本地输出"同形。重新格式化只会多一份可能与 token 文法漂移的代码。
 *
 * 现场保留
 * --------
 * 本模块**只搬 token 行**：从核的 LOG / panic / assert 文本仍走本地串口
 * （没调 rlog_emit 的东西一概不动）⇒ 从核崩溃时现场仍在它的串口上，不丢。
 *
 * 失败可见（fail-closed 的输入侧）
 * ------------------------------
 * 发送失败（FIFO 满且重试耗尽）、接收侧跳号 / 帧非法 / 环形缓冲溢出，都计入
 * `rlog_lost()`；主核在收不到 DONE 时判 `rlog_secondary_ok() == false`。调用方据
 * 此把 case 判 FAIL，而不是把"没收到"当"通过"。
 *
 * 传输
 * ----
 * 专用 mbox 消费者通道（overlay 里追加的 `vnd,mbox-consumer` 子节点）。`channel`
 * 单元格必须等于该节点在 `&mbox` 子节点中的**序号**（驱动用 `DT_INST_FOREACH_
 * CHILD_STATUS_OKAY` 的遍历序索引 FIFO，spec 的 channel_id 直接当数组下标用），
 * 两核必须声明在相同序号上。
 */
#ifndef RLOG_H
#define RLOG_H

#include <stdbool.h>
#include <stdint.h>

/* 单帧可承载的文本上限（含结尾省略号）：帧 = 8B 头 + (RLOG_TEXT_MAX+1)B 文本，
 * 合计 RLOG_TEXT_MAX+9 B。**必须 ≤ 通道 width**（设备树里 `mboxes` 的第 2/4
 * 个单元格），且**两核取同一值**（帧结构是双方的 ABI）。
 *
 * 默认 54 ⇒ 帧 63B，配板级/既有工程的 width 64（`i3c_i2c_switch_test` 用它）。
 * 值可被工程覆盖：ztest 型从核的逐项 D1 行可能很长
 * （` PASS - test_cpu1_does_not_own_app_iwdg in 0.001 seconds` ≈ 56B），
 * 54 会在 `rlog_emitf` 的 vsnprintf 处被**静默截断**，把结尾的 `seconds`
 * 吃掉 ⇒ `_RESULT_RE` 认不出该行 ⇒ 逐项证据凭空消失（判成"零证据 FAIL"）。
 * 这类工程在 CMakeLists 里 `target_compile_definitions(app PRIVATE
 * RLOG_TEXT_MAX=118)` 并把 overlay 的 width 抬到 128（帧 127B）。 */
#ifndef RLOG_TEXT_MAX
#define RLOG_TEXT_MAX 54
#endif

/* 主核等从核 DONE 的上限（毫秒）。实测校准值，见 SPEC_REFERENCE §6.9.3。 */
#define RLOG_TX_TIMEOUT_MS 2000

/* ---- PRIMARY（有串口出口的一侧） ---- */

/* 注册接收回调并开通道。回调在 ISR 只入环形缓冲，不打印。 */
void rlog_primary_init(void);

/* 转发信封（SPEC_REFERENCE §6.9.4 分拣）：非空 ⇒ drain 时按
 * `<prefix>:<core>:<行>` 打印，L1 的 split_relay_lines 据此把该行分拣回从核
 * 归档。**默认 NULL = 原样打印**（`i3c_i2c_switch_test` 的行为逐字不变）。
 *
 * 为什么信封加在**中继侧**而不在从核侧：从核发的是纯文本，帧长上限
 * （RLOG_TEXT_MAX）不被信封挤占 —— 否则 14B 的信封会把 INFO 行挤到截断
 * （ztest 的 `INFO <name>: k=v,k=v` 常达 40+ 字节）。信封在唯一的出口处加，
 * 也正是"单出口"发生的地方。 */
void rlog_set_relay_envelope(const char *prefix, const char *core);

/* 把已收到的从核行按到达顺序 printk 出去（thread 上下文调用；可反复调）。
 * 返回本次打印的行数。 */
int rlog_relay_drain(void);

/* 等从核 DONE 帧：期间反复 drain（保持从核 FIFO 有空间），到点或收到即返回。
 * 返回 true = 收到 DONE。 */
bool rlog_wait_secondary_done(uint32_t timeout_ms);

/* 从核自报的结论：DONE 帧显示 PASS 且无丢失 ⇒ true。 */
bool rlog_secondary_ok(void);

/* 从核一个帧都没收到 ⇒ true（判失败理由用）。 */
bool rlog_secondary_silent(void);

/* 丢失/非法帧计数（发送侧重试耗尽 + 接收侧跳号/坏帧/缓冲满）。 */
uint32_t rlog_lost(void);

/* ---- 背压量化（SPEC_REFERENCE §6.11.5：单出口对从核的阻塞必须可测） ----
 *
 * 从核的打印路径**不得阻塞测试**：FIFO 满时先让路重试，重试耗尽则丢弃并
 * 计入 lost（主核侧 fail-closed 判 FAIL，绝不静默）。这几个量回答"到底阻
 * 了多久、丢没丢"——也是 §6.11.5 上板实测的数据源。
 *
 * 全部是**非 static 全局符号**：GDB 可直接按符号名读（不依赖调试信息），
 * 便于上板后从从核 .bss 取证（`x/wx &rlog_stat_tx_max_block_ms`）。 */
extern volatile uint32_t rlog_stat_tx_lines;      /* 送出的行数（含 DONE） */
extern volatile uint32_t rlog_stat_tx_retries;    /* 累计重试次数 */
extern volatile uint32_t rlog_stat_tx_max_retry;  /* 单行最大重试次数 */
extern volatile uint32_t rlog_stat_tx_max_block_ms; /* 单行最长阻塞(ms) */
extern volatile uint32_t rlog_stat_tx_lost;       /* 重试耗尽丢弃的行数 */
extern volatile uint32_t rlog_stat_rx_hwm;        /* 接收环最大占用(行) */

/* ---- SECONDARY（无 token 出口的一侧） ---- */

/* 标记本核角色并准备发送通道（不注册接收回调）。 */
void rlog_secondary_init(void);

/* 单出口下的 BOOT 标记（§6.9.1 引导类，文法 `ZT_BOOT <core>`）。
 *
 * 为什么不用 `ztest_glue.c::zt_init()`：它走 `printk`，是**本核串口**出口 ——
 * 单出口的从核不能用它（BOOT 会留在本核）。这里复用同一文法，只是换出口
 * （`zt_init` 的格式串若变更，本宏要同步）。 */
#define RLOG_ZT_BOOT(core) rlog_emit("ZT_BOOT " core)

/* 单出口下的 INFO 约定行（§6.9.1 INFO 类，文法 `INFO <name>: k=v,k=v`）。
 * 与 `ztest_glue.h::ZT_INFO` 同一文法、不同出口（理由同 RLOG_ZT_BOOT）。 */
#define RLOG_ZT_INFO(name, fmt, ...) \
	rlog_emitf("INFO %s: " fmt, name, ##__VA_ARGS__)

/* 发一行（自动截断）。发送失败按 FIFO 空间重试，重试耗尽计入 lost。 */
void rlog_emit(const char *text);

/* 格式化后发一行（内部缓冲 RLOG_TEXT_MAX+1，超长截断）。 */
void rlog_emitf(const char *fmt, ...);

/* 发 DONE 帧（整条流的最后一条）：verdict_ok = 本核结论，text = 该帧要打印的收尾行。 */
void rlog_emit_done(bool verdict_ok, const char *text);

#endif /* RLOG_H */
