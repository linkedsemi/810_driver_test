# I3C 双核测试 8ms 死窗问题总结（ztest LOG_PANIC / panic_mode）

> 2026-08-21 定位并修复，GDB 实锤 + 硬件验证通过。

## 1. 问题现象

I3C 双核测试（target = cpu1，controller = cpu2）中，**IBI 之后的 8ms 死窗**：

- IBI 事件后所有 I3C 帧（tx/rx）都不再触发中断，约 8ms 后才恢复
- 拉 GPIO 实测：死窗期间**没有进入中断入口**（中断未被关，CPU 忙在别处）
- 复现数据（case_pure_i3c_12m5）：`logspan=8115us, postfin=8107us, mtime=8107us`

## 2. 根因链路（最终定论）

```
target 启动
→ test_main() 只 k_thread_create 创建 dispatcher 线程，毫秒级立即返回
→ ztest main() 执行 LOG_PANIC()                    (ztest.c:1478)
→ log_core.c:103 panic_mode = true                 （永不复位）
→ dispatcher 线程（K_PRIO_COOP(5)）继续跑 I3C 业务
→ 之后每一条 LOG/printk 进 z_log_msg_post_finalize 的 panic 分支：
    k_spin_lock(&process_lock)  +  log_process() 同步全量输出
→ UART 队列满时 log_process() 阻塞 → 8ms 死窗
```

本质：**ztest 的契约是 `test_main()` 返回 = 测试已完成**。普通测试同步跑 suite，返回时测试确实结束，`LOG_PANIC()` 只是"结束后冲洗日志"，无副作用。而 target 的测试是**被动驱动型**（测试进度由 controller 通过 I3C 命令推进），必须用事件循环线程，`test_main()` 建线程即返回 → 违反契约 → ztest 在测试**进行中**就置位了 panic_mode。

## 3. 调试证据（GDB 条件断点，两遍烧录法）

```text
# 断点 1：LOG_PANIC 的调用者
hbreak z_impl_log_panic  → 命中
bt:
  #0 z_impl_log_panic            log_core.c:529
  #1 log_panic                   syscalls/log_ctrl.h:35
  #2 main                        ztest.c:1478   ← ztest main，不是 fatal handler

# 断点 2：panic 后第一条日志 post
hbreak z_log_msg_post_finalize if panic_mode == 1  → 立即命中
  panic_mode = true            （$1 = true）
  bt: dispatcher_entry → z_ztest_run_test_suite_ptr → printk → vprintk → z_log_msg_post_finalize
  $mstatus = 0x22088           （MIE=1：中断是开的，印证"没进中断入口"的实测）
```

**排除项**：
- shell_rtt.c:34 的 `panic_mode` 是 RTT 模块**私有 static**，与 log_core.c:103 无关（证伪）；后端实为 shell_uart（CONFIG_SHELL_BACKEND_SERIAL=y）
- fatal handler（e906/fatal.c）调用链排除（bt 已实锤是 ztest main）
- "SRAM 原子访问被 stall"模型（Variant 4 实验）被 panic 分支理论覆盖：无关 atomic_inc 也复现 8ms，是因为它同样触发后续 log 的 panic 同步输出

## 4. 修复方案

让 `test_main()` 永不返回，ztest 永远走不到 `LOG_PANIC()`：

```c
/* cpu1_i3c_target/src/i3c_target_test.c */
void target_dispatcher_run(void)
{
	k_thread_create(&dispatcher_thread, dispatcher_stack,
			K_THREAD_STACK_SIZEOF(dispatcher_stack),
			dispatcher_entry, NULL, NULL, NULL,
			K_PRIO_COOP(5), 0, K_NO_WAIT);

	/* dispatcher_entry 是无限事件循环；test_main() 不能返回，
	 * 否则 ztest main() 会执行 LOG_PANIC() 置 panic_mode=1，
	 * 测试期间所有日志走 panic 同步分支（k_spin_lock + log_process()）
	 * → 8ms 死窗。 */
	k_thread_join(&dispatcher_thread, K_FOREVER);
}
```

## 5. 验证结果

| 项目 | 修复前 | 修复后 |
|---|---|---|
| 条件断点 `panic_mode==1` 命中 | 秒级命中 | 90s 全程零命中 |
| `logspan`（TGT_DBG） | 8115us | **11us** |
| 测试结果 | FAIL（read actual 0） | **PASS**（I3C_TEST:DONE:PASS） |

## 6. 通用教训

- **任何 `test_main()` 里建线程后不等待就返回的 ztest 测试都会踩这个坑**——不是 I3C 特有
- 被动驱动型测试（硬件命令推进）不要依赖 ztest 默认 main 流程；要么 `test_main()` 不返回（join/sleep 永久阻塞），要么不用 ztest 的 main（自建 main 流程）
- `LOG_PANIC()` 之后 `panic_mode` 永不复位，后续所有日志都走同步 panic 分支——"测试结束后"才允许
- 判断"测试是否结束"以 `test_main()` 是否返回为准，与业务实际进度无关

## 7. 涉及文件

| 文件 | 说明 |
|---|---|
| zephyr/subsys/testsuite/ztest/src/ztest.c:1478 | `LOG_PANIC()` 无条件调用（契约点） |
| zephyr/subsys/logging/log_core.c:103, 209-219 | `panic_mode` 及 panic 分支（k_spin_lock + log_process） |
| 810_driver_test/i3c_dualcore_test/cpu1_i3c_target/src/i3c_target_test.c:970-989 | 修复位置（k_thread_join） |
| 810_driver_test/i3c_dualcore_test/cpu1_i3c_target/src/main.c:21-31 | `test_main()` 建线程即返回（违规点） |
