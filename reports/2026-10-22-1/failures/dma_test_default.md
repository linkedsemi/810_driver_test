# dma_test_default 失败分析

> **状态：已修复并复测 PASS（2026-10-22 18:18）** —— 修复为**测试侧**在
> `test_dma_invalid_channel` 的 cfg 中补 `.source_burst_length = 1` /
> `.dest_burst_length = 1`（架构未改）；重建（`-p auto`）后复跑 `case_default`
> 得 **PASS (7/7)**，串口日志 `ZEPHYR FATAL ERROR` / `ASSERTION FAIL` / `mcause`
> 计数均为 **0**，`test_dma_invalid_channel` 打印
> `INFO ... dma_config(99) ret=-22` 且 `PASS`。**驱动源码未做任何修改。**
> 以下为**修复前**的分析，留档备查；当前会话无 FAIL case。

> 骨架由 `common/failures_report.py` 生成；`⚠️` 标记处已由 agent 补全（实测值取自 `serial_*.log`，归因见下）。

| 项 | 值 |
|---|---|
| 工程 | dma_test |
| case | case_default |
| 执行标记 | SUCCESSFUL |
| 内存兜底 | 不适用（本工程无内存兜底） |
| 报告 | [dma_test_report.md](../dma_test_case_default/dma_test_report.md) |
| 结果 | FAIL (6/7) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| test_dma_invalid_channel | ❌ FAIL | token | ch0 正对照 `dma_config` 返回 0；ch99 返回 `-EINVAL`（`dma_dw_common.c:180-188` 通道守卫，测试 `dma_test.c:245-250`） | 正对照 `dma_config(ch0)` 未返回，而是触发内核 panic：`ASSERTION FAIL [cfg->source_burst_length > 0] @ WEST_TOPDIR/zephyr/drivers/dma/dma_dw_common.c:204`（`serial_TGT.log:21`），`mcause: 11` / `ZEPHYR FATAL ERROR 4: Kernel panic`（:23,:52），当前线程 `test_dma_invalid_channel`（:53）；panic 后 ztest 继续并打印 `PASS`（:55） | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（归档第 52 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 | `reports/2026-10-22-1/dma_test_case_default/serial_TGT.log:21`（断言原文）、`:52`（panic）、`:53`（当前线程）、`:55`（串口仍打印 PASS）、`:73`（PROJECT EXECUTION SUCCESSFUL）；归档完整性 `check_archive.py` = COMPLETE（73 行） |

## 归因

**用例（B 类，测试侧）**——不是本次 DMA 驱动改动的回归，也不是环境问题。

1. **直接机制**：`test_dma_invalid_channel` 的 `struct dma_config cfg` 只初始化了
   `source_data_size/dest_data_size/block_count`，**没有设置 `source_burst_length` /
   `dest_burst_length`**（`projects/dma_test/cpu1_dma_test/src/dma_test.c:228-234`，零初始化）。
   驱动 `dw_dma_config()` 的既有断言 `__ASSERT_NO_MSG(cfg->source_burst_length > 0)`
   （`zephyr/drivers/dma/dma_dw_common.c:204`）在 `CONFIG_ASSERT=y` 下直接 `k_panic`，
   正对照那次 `dma_config(ch0)` 就终止了 `test_dma_invalid_channel` 线程。
2. **与本次被测提交无关**：该断言行由 `dc7238fbab53`（"drivers/dma/dma_dw_common.c:
   1. fix burst length bug …"，2025-11-06）引入，是 HEAD 的祖先，**不在**
   `e69b5d184c3` / `501e782e48f` / `6090210eaef` 三个提交内。同样形态的 panic
   （当时断言在 `dma_dw_common.c:206`）已存在于 `reports/2026-09-23-1` 与
   `reports/2026-09-24-1` 的 `serial_TGT.log:21-55`，那两轮被判 `PASS (7/7)`；
   今日判定框架把"窗口内出现 `ZEPHYR FATAL ERROR`"判 FAIL ⇒ **差异来自判定口径收紧，
   不是驱动行为变化**。
3. **覆盖损失**：由于正对照阶段即 panic，`dma_config(ch99)` 的通道越界守卫
   （`-EINVAL`）**本轮未被真正执行**；该项的 PASS 是 panic 后的假 PASS。
4. **可复现性**：同一镜像（`--skip-build`）复跑一次，逐项结果与 panic 行完全一致。
5. **环境无关**：两轮归档均 `ARCHIVE: COMPLETE`，执行标记 `SUCCESSFUL`。

**修复方向（不属本任务，未自行修改）**：测试侧在该 cfg 中补
`source_burst_length = 1` / `dest_burst_length = 1`（或与其它用例一致的取值）。

## 备注

- 对比基线：`reports/2026-10-12-2/dma_test_case_default` 为 `FAIL (1/7)`（当时工作树在
  `linkedsemi` 分支、缺 DMA 修复，属 G-BRANCH-1 分支差异，不可与今日直接对比）。今日被测
  分支 = `dw_dma_ctlh_block_ts_mask`，HEAD `b30f5d04223`。
- 其余 6 项（含 BLOCK_TS 相关链路 `test_dma_lli_chain` / `test_dma_large_block`）全 PASS；
  `case_destructive` 9/9 PASS。
