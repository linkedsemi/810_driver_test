# SDHCI (linkedsemi) — case_block_driver 失败分析

| 项 | 值 |
|---|---|
| 工程 | emmc_test |
| case | emmc_test_case_block_driver（报告内 case 名 `case_block_driver`） |
| 被测驱动 | `zephyr/drivers/sdhc/sdhci-of-linkedsemi.c` |
| 报告 | [emmc_test_report.md](../emmc_test_case_block_driver/emmc_test_report.md) |
| 结果 | FAIL (8/10) |
| 执行标记 | CTRL=不适用（1os 单核，仅 TGT 一个核；replay_meta.json 的 `cores` 只有 TGT/COM16）, TGT=FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |

> 本 case 使能 SDMA（`CONFIG_SDHCI_SDMA_ENABLE` 默认 y）→ 数据通路走 DMA。

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| request_mutex_not_leaked | 并发 | 探针线程内连发两次非法请求，第二次 `sdhc_request()` 在 2000 ms 内返回（`k_thread_join` 成功，`jret==0`；`probe_ret==probe_ret2==-ENOTSUP`）。来源：COVERAGE.md:43「第二次 sdhc_request() 在 2000 ms 内返回」 | `join=-11`（-EAGAIN，2000 ms 超时）、`probe_ret=2147483647`(0x7fffffff，从未被赋值)、`probe_ret2=2147483647`、`done=0` | 报告原文：`emmc_block_test.c:460: ... (jret is non-zero)`；日志附注「second in-probe sdhc_request() did not return within 2000 ms: access_mutex leaked by a -ENOTSUP early return taken while holding the lock」 | `serial_TGT.log:71`（INFO 实测值）、`serial_TGT.log:73`（断言）、`serial_TGT.log:75`（FAIL） |
| set_io_clock_out_of_range_not_silent | 异常参数 | 越界时钟 `clock=1000`（< `f_min=50000`）应返回非 0（`ret != 0`）。来源：COVERAGE.md:44「越界时钟（1000 < f_min 50000）应返回非 0」 | `ret=0`（`clock=1000`、`f_min=50000` 均在 INFO 行给出） | 报告原文：`emmc_block_test.c:496: ... (ret equal to 0)`；日志附注「out-of-range clock 1000 (< f_min 50000) was silently accepted ... 仍返回 0」 | `serial_TGT.log:78`（INFO 实测值）、`serial_TGT.log:80`（断言）、`serial_TGT.log:82`（FAIL） |

## 归因

两项均为**驱动缺陷**（非用例问题、非采集问题），且与 COVERAGE.md 预期状态一致或相反：

1. **request_mutex_not_leaked**：COVERAGE.md:43 与 :52 记录该失败应为「✅ PASS（2026-09-18 驱动修复后改判）」——即期望钉缺陷 #1（`k_mutex_lock`(:422) 与 `unlock:`(:505) 之间的 `default: return -ENOTSUP`）已修复。但本次实测 `join=-11` + `done=0`（探针线程第二次调用 `sdhc_request()` 未在 2000 ms 内返回）⇒ 旧的**持锁早退导致 `access_mutex` 泄漏**的症状**复现**。归因指向驱动 `sdhci-of-linkedsemi.c` 的 `linkedsemi_sdhci_request` 加锁/解锁配对：`-ENOTSUP` 早退路径未汇入 `unlock:`（与 COVERAGE.md:52 描述的修复前形态一致）。**疑似回归或修复未落到本次构建**，需核对当前源码该分支形态。

2. **set_io_clock_out_of_range_not_silent**：COVERAGE.md:44 与 :55 明确标注此项为 `❌预期FAIL`（缺陷靶点 #4：`sdhci-of-linkedsemi.c:228-232` 时钟越界分支不报错、不改频、仍返回 0）。本次实测 `ret=0` 与预期 FAIL **一致**，属**已知未修复缺陷**，非新问题。

3. **执行标记说明**：TGT 标记为 FAILED，但日志完整（`serial_TGT.log:86-105` 含完整 TESTSUITE SUMMARY START/END 与 `PROJECT EXECUTION FAILED`）⇒ 是真实断言失败（`zz_emmc_block_edge` 2 fail），**非采集截断**。

## 备注

- 与同源 case `case_block_pio` 的对比见 `emmc_test_block_pio.md`：两 case 固件与用例集相同，仅 `prj.conf` 差异（本 case SDMA 使能走 DMA 通路；pio case `CONFIG_SDHCI_SDMA_ENABLE=n` 走 ISR 轮询搬运，见 `case_block_pio/test.sh:5-9`）。这两项失败属驱动的**非数据通路**分支（请求加锁/解锁、set_io 时钟越界），与 DMA/PIO 传输方式无关，因此两 case 失败项相同属预期。
- COVERAGE.md:42 的 `request_data_invalid_opcode_notsup` 本次 PASS，与 :43 的 mutex 项失败形成对照：白名单/参数校验路径通过，但探针线程中的行为未通过，说明问题集中在加锁区间内的出口处理。
