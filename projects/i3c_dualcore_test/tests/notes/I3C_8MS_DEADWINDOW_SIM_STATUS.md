# I3C 双核 8ms 死窗仿真固件 — 状态记录

生成时间：2026-08-19

## 1. 当前 GPIO 调试定义（从机侧）

- **PJ06**：`ls_i3c_target_isr()` 执行区间  
  - 入口 `io_set_pin(PJ06)`
  - 出口 `io_clr_pin(PJ06)`
- **PJ07**：异常触发 LOG 前后翻转  
  - `z_log_msg_post_finalize()` 入口 `io_toggle_pin(PJ07)`
  - `z_log_msg_post_finalize()` 出口 `io_toggle_pin(PJ07)`
- **PJ04/PJ05**：未使用（硬件未接）

## 2. 关键代码改动

- `zephyr/subsys/logging/log_core.c`
  - `z_log_msg_post_finalize()` 内保留 g0-g7 缝隙打点、`LOG_DBG`/`GAP_DBG`/`TO_DBG` 数据输出
  - PJ07 翻转标记由 `log_dbg_gpio_mark(2, 0)` 触发
- `zephyr/drivers/i3c/i3c_linkedsemi.c`
  - 从机 ISR 入口/出口添加 PJ06 set/clr
  - 主机/从机超时压缩为 3ms：
    - `I3C_IBI_COMPLETE_TIMEOUT = K_MSEC(3)`
    - `LS_I3C_TRANSFER_TIMEOUT = K_MSEC(3)`
- `810_driver_test/i3c_dualcore_test/cpu1_i3c_target/src/i3c_target_test.c`
  - 保留 dispatcher 22ms 心跳死窗采样
  - 保留 `TGT_DBG`/`LOG_DBG`/`GAP_DBG`/`E906_DBG`/`HB`/`PROBE`/`ISRLOG`/`IRQLOG` 输出
  - 删除 PJ04/PJ05 相关代码

## 3. 复现结果（case_pure_i3c_12m5）

```text
controller: I3C_TEST:CTRL:FAIL:write_stop_read_32:read actual 0 != 32;
target:     logspan=8115us, postfin=8107us, mtime=8107us, mcycle=8107us
```

- 死窗期间 timer 中断（irq=7）持续进入，I3C 中断（irq=117）缺失
- `GAP_DBG: g0=0us g1=0us ... g7=0us`，8ms 不在 `post_finalize` 任何可见代码路径内
- 证据指向：IBI 完成后 cpu1 第一次访问 SRAM 时被总线/硬件级事件 stall 约 8ms

## 4. 最终定论（2026-08-21 GDB 实锤 + 修复已验证）

**根因**：ztest `main()`（[ztest.c:1478](zephyr/subsys/testsuite/ztest/src/ztest.c#L1478)）无条件执行 `LOG_PANIC()` →
log_core.c:103 的 `panic_mode=true`。target 的 `test_main()` 只 `k_thread_create`
创建 dispatcher 线程就立即返回，毫秒级走到 LOG_PANIC；之后 dispatcher 线程
（K_PRIO_COOP(5)）跑 I3C 业务期间每一条 LOG/printk 都进 `z_log_msg_post_finalize`
panic 分支：`k_spin_lock`（关中断）+ `log_process()` 同步全量吐 UART → 阻塞 ~8ms。

**GDB 证据（条件断点）**：
- `hbreak z_impl_log_panic` 命中 → bt：`main() @ ztest.c:1478` 调用（非 fatal handler）
- `hbreak z_log_msg_post_finalize if panic_mode == 1` 修复前立即命中 → `panic_mode=true`
  （此时 dispatcher 线程正在跑 ztest suite，`$mstatus=0x22088`，MIE=1）
- shell_rtt.c:34 的 panic_mode 是 RTT 模块私有 static，与 log_core 无关（证伪）；
  后端实为 shell_uart（CONFIG_SHELL_BACKEND_SERIAL=y）

**修复**：`target_dispatcher_run()` 创建线程后 `k_thread_join(&dispatcher_thread, K_FOREVER)`，
test_main() 永不返回 → ztest main 走不到 LOG_PANIC → panic_mode 保持 false。
- 验证 1：修复后条件断点 `if panic_mode == 1` 全程 90s 零命中（修复前秒级命中）
- 验证 2：`logspan=8115us → 11us`，8ms 死窗消失
- 验证 3：`write_stop_read_32 PASS`，I3C_TEST:DONE:PASS，功能无损

## 5. 产物路径

- Patch：
  - `tests/i3c_8ms_deadwindow_zephyr.patch`（zephyr 仓库改动）
  - `tests/i3c_8ms_deadwindow_app.patch`（app 仓库改动）
- 固件（case_pure_i3c_12m5）：
  - target ELF：`tests/case_pure_i3c_12m5/build/target/zephyr/zephyr.elf`
  - controller BIN：`tests/case_pure_i3c_12m5/build/controller/zephyr/zephyr.bin`
  - target 烧录 BIN：`tests/case_pure_i3c_12m5/build/target/zephyr/zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin`
  - controller 烧录 BIN：`tests/case_pure_i3c_12m5/build/controller/zephyr/zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu2.bin`

## 5. 运行方式

```bash
cd 810_driver_test/i3c_dualcore_test/tests
python dbg_run.py --case case_pure_i3c_12m5 --timeout 20 --raw
```

要求：GDB server 在 `localhost:1025`，串口 COM15（controller）/COM16（target）。
