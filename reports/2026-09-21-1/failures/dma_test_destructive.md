# dma_test — case_destructive 失败分析

| 项 | 值 |
|---|---|
| 工程 | dma_test |
| case | case_destructive（套件 `dma_destructive`，9 项；构建开关 `-DDMA_DESTRUCTIVE=1`） |
| 被测驱动 | `zephyr/drivers/dma/dma_dw_common.c` / `dma_dw_common.h`（dmac1 `0x40041000`） |
| 报告 | [dma_test_report.md](../dma_test_case_destructive/dma_test_report.md) |
| 结果 | FAIL (4/9) |
| 执行标记 | CTRL=不适用（单核工程，目录内无 `serial_CTRL.log`）, TGT=FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| test_dma_large_block | 边界（单块上限） | 2047B（`BLOCK_TS[10:0]` 上限）首尾字节正确（`0x55`）+ 回调触发（`COVERAGE.md:55`；源码 `dma_test.c` 对应 `dma_destructive_test.c:270-279`） | `dma_config(block_size=2047)` = **-16**（`-EBUSY`） | 报告原文 `:271: (ret not equal to 0)`；串口原文 `dma_config failed for 2047-byte block: -16` | `serial_TGT.log:30`、`serial_TGT.log:33` |
| test_dma_lli_pool_exhaust | 异常参数（边界长度） | `block_count=3` > pool 2 ⇒ 返回**精确 `-EINVAL`(-22)**，且拒绝发生在任何描述符改写前（`COVERAGE.md:56`、`COVERAGE.md:158`；源码 `dma_destructive_test.c:329-333`） | ret = **-16**（`-EBUSY`）；实测 INFO 原文 `block_count=3,pool=2,ret=-16` | 报告原文 `:332: (ret not equal to -EINVAL)`；串口原文 `block_count=3 > pool 2 should be -EINVAL, got -16` | `serial_TGT.log:38`（INFO）、`serial_TGT.log:41` |
| test_dma_one_byte | 边界（最小传输） | 1 字节 `0xAB` 送达 + 回调触发（`COVERAGE.md:54`；源码 `dma_destructive_test.c:229-236`） | `dma_config` = **-16** | 报告原文 `:230: (ret not equal to 0)`；串口原文 `dma_config failed: -16` | `serial_TGT.log:45`、`serial_TGT.log:48` |
| test_dma_self_copy_rejected | 异常参数（同址退化配置） | `dma_config(src == dst)` 返回 `-EINVAL`，且缓冲区内容未被改动（`COVERAGE.md:53`、`COVERAGE.md:159`；源码 `dma_destructive_test.c:192-198`） | ret = **-16**（`-EBUSY`） | 报告原文 `:193: (ret not equal to -EINVAL)`；串口原文 `dma_config src==dst should be rejected: -16` | `serial_TGT.log:52`、`serial_TGT.log:55` |
| test_dma_suspend_resume_guards | 中断/时序（状态守卫） | 非 ACTIVE 时 `dma_suspend(ch0)` ⇒ `-EINVAL`(-22)（`COVERAGE.md:58`；源码 `dma_destructive_test.c:441-445`） | `dma_suspend(ch0)` = **0**（未拒绝）；实测 INFO 原文 `suspend_idle_ret=0` | 报告原文 `:444: (ret not equal to -EINVAL)`；串口原文 `suspend while IDLE ret 0 != -EINVAL` | `serial_TGT.log:59`（INFO）、`serial_TGT.log:62` |

> 未失败项（4 项 PASS）：`test_dma_abort_mid_transfer`（INFO `stop_ret=0`，`serial_TGT.log:12`）、`test_dma_block_too_large`、`test_dma_data_size_zero`、`test_dma_invalid_data_size`。
> **注意这 3 项是"假 PASS"**：它们只断言 `zassert_not_equal(ret, 0)`（源码 `dma_destructive_test.c:100/131/162`），而实测每项 config 都因通道残留 ACTIVE 返回 `-EBUSY`（`serial_TGT.log:18/22/26` 的 err 行，状态 = 3）——**非 0 满足，但被测的参数校验分支从未被触发**，"data_size / block_size 被拒"这一维实际未验证。
> `test_dma_suspend_resume_guards` 的 `ZT_INFO` 只有第 1 条被执行（`serial_TGT.log:59`），第 2 条 `resume_idle_ret`（源码 `:448`）因断言致命中断 ⇒ **未采集到**。

## 归因

**驱动缺陷（与 case_default 同一根因），不是用例、环境或数据采集问题。**

根因链（5 个失败项指向同一处）：

1. `dw_dma_config()` 在 `complete_callback_en=1` 时只解除 **BLOCK / DST_TRAN** 屏蔽（`dma_dw_common.c:502-508`），**TFR 屏蔽未解除**；`DW_MASK_TFR` 的解除只写在 `else` 分支（`dma_dw_common.c:512`）。
2. `dw_dma_isr()` **只在 TFR 分支**执行 `chan_data->state = DW_DMA_IDLE`（`dma_dw_common.c:130`），BLOCK/DST_TRAN 分支不复位状态。
3. ⇒ 一次成功传输后 `chan_data->state` 停在 **3 = `DW_DMA_ACTIVE`**（枚举 `dma_dw_common.h:264-267`）。
4. `dw_dma_config()` 状态守卫（`dma_dw_common.c:189-193`）对 ACTIVE 判 **`-EBUSY`**；`dw_dma_stop()` 因硬件已自清 `ChEn` 走**早退分支**（`dma_dw_common.c:664-667`）返回 0 而不复位 state ⇒ ch0 从此不可再 config。

**本 suite 的时间线（字母序执行）**：

- `test_dma_abort_mid_transfer` 是字母序第一项，也是**污染源**：它最后一段 config + start + 等完成（`dma_destructive_test.c:419-431`）**结束时不做 `dma_stop`**，而该传输的完成走了 BLOCK/DST_TRAN 回调 ⇒ 收束时 ch0 的 state = ACTIVE。它自己之所以 PASS，是因为中途那次 `dma_stop()`（`:395`）在硬件仍使能时走了 disable 分支、把 state 置回 IDLE（`dma_dw_common.c:724`），而收尾的 start 之后不再有 config 调用。同时 `dma_test_before` 钩子（`dma_destructive_test.c:51-59`）调用的 `dma_stop` 对"硬件已禁用但 state 仍 ACTIVE"这一残留形态**无效**（同走早退分支，不复位 state）。
- 其后 4 项（`block_too_large` / `data_size_zero` / `invalid_data_size` / `large_block` / `lli_pool_exhaust` / `one_byte` / `self_copy_rejected`）**第一次 `dma_config` 即撞 `-EBUSY`**：3 项因"只断言非 0"而假 PASS，`large_block` / `one_byte`（断言 == 0）、`lli_pool_exhaust` / `self_copy_rejected`（断言 == `-EINVAL`）真 FAIL。
- `test_dma_suspend_resume_guards` 的失败形态是同一根因的**反向表现**：它预期 ch0 处于非 ACTIVE（IDLE）以便 `suspend` 返回 `-EINVAL`（`dma_dw_common.c:776-779`），但 ch0 实际仍是 ACTIVE ⇒ `dma_suspend` **成功返回 0 并置 SUSPENDED**（`dma_dw_common.c:784-788`），断言 `:444` 失败。它不是"守卫坏了"，而是"前置状态被污染"，用例本身对状态前提的假设被驱动违反。

**执行标记 FAILED 不是采集截断**：日志完整跑完 9 项，以 `TESTSUITE dma_destructive failed.` + `PROJECT EXECUTION FAILED` 收尾（`serial_TGT.log:65-67`），无断流。

## 备注

- **历史对比（非本目录数据，取自工程 notes）**：本 suite 在 `projects/dma_test/tests/notes/DMA_DW_BLOCK_TS_MASK_AND_TFR_STATE_ROOTCAUSE.md:108` 与 `OPEN_ITEMS.md:42`（O-5）中记录为 **6/6 PASS**（会话 `reports/2026-09-17-dmaC` —— ⚠️ **该会话已不可得且从未入库**，见 `SPEC_REFERENCE` §6.4.7「语料可用性声明」）。本次 9 项中 5 项 FAIL（另 3 项为假 PASS），属**明显回归**。
- **当前工作区驱动不在"已修"状态（重要，需人工确认）**：notes 记录的修复在当前磁盘的 `zephyr/drivers/dma/dma_dw_common.{c,h}`（mtime 2026-09-21 11:30，`git status` 干净）中**均不在位**：
  - "TFR 解除屏蔽移出 else"未生效 —— `dma_dw_common.c:507-508`（BLOCK/DST_TRAN）与 `:512`（TFR，仍在 `else` 内），对照 notes `:103`。
  - `DW_CTLH_BLOCK_TS_MASK` 仍在变体 `#if` 之外 —— `dma_dw_common.h:186-187` 定义为 `MASK(16, 0)`，对照 notes `:102`。
  - `src == dst` 显式拒绝缺失 —— 全文件无 `source_address ==` 比较，对照 notes `:115-124`（O-5）。
  - **推断（需确认，非实测）**：驱动已还原为上游 pristine（`git log -1` = `8e862f7`，2026-04-29），未提交的本地修复丢失（`OPEN_ITEMS.md:42` 明写"代码未提交"）。
- **对报告表的修正建议**：`dma_test_report.md` 的"实测(INFO)"列对失败项全填 `-`，但本 case 有 2 处可直接取到实测值 —— `test_dma_lli_pool_exhaust` 的 `block_count=3,pool=2,ret=-16`（`serial_TGT.log:38`）与 `test_dma_suspend_resume_guards` 的 `suspend_idle_ret=0`（`serial_TGT.log:59`）。本文均已按串口原文录入，未做任何换算。
