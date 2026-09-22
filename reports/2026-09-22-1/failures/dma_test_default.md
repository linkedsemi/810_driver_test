# dma_test — case_default 失败分析

| 项 | 值 |
|---|---|
| 工程 | dma_test |
| case | case_default（套件 `dma`，7 项） |
| 被测驱动 | `zephyr/drivers/dma/dma_dw_common.c` / `dma_dw_common.h`（dmac1 `0x40041000` + dmac2 `0x40042000`） |
| 报告 | [dma_test_report.md](../dma_test_case_default/dma_test_report.md) |
| 结果 | FAIL (1/7) |
| 执行标记 | CTRL=不适用（单核工程，目录内无 `serial_CTRL.log`）, TGT=FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| test_dma_channel_config_stop | 通道生命周期 | `dma_config`/`dma_start`/`dma_stop` == 0，且 stop 后**重新 config/start 并再次 memcmp** 逐字节相等（`COVERAGE.md:40`；源码 `dma_test.c:204-216`） | 第二次 `dma_config` 返回非 0；报告/断言未打印该 ret 数值。同刻驱动日志显示通道状态 = 3（`DW_DMA_ACTIVE`），按 `dma_dw_common.c:189-193` 判 `-EBUSY` | 报告原文 `:210: dma_test_dma_channel_config_stop: (dma_config(dma_dev, DMA_CHANNEL, &cfg) is non-zero)`；串口原文 `reconfig after stop failed` | `serial_TGT.log:14`（`channel 0 must be inactive to reconfigure, currently 3`）、`serial_TGT.log:16-17` |
| test_dma_invalid_channel | 异常参数 | **正对照**：同一 cfg 在合法 ch0 必须被接受（== 0）；ch99 返回**精确 `-EINVAL`(-22)**（`COVERAGE.md:41`、`COVERAGE.md:155`；源码 `dma_test.c:245-250`） | 正对照 `dma_config(dma_dev, 0, &cfg)` = **-16**（`-EBUSY`），未走到 ch99 断言 | 报告原文 `:246: (ret not equal to 0)`；串口原文 `positive control: valid channel rejected: -16` | `serial_TGT.log:28` |
| test_dma_lli_chain | 功能通路 | `block_count=2` 两块各 64B（共 128B）逐字节相等 + 回调触发（`COVERAGE.md:42`；源码 `dma_test.c:335-348`） | `dma_config(2 blocks)` = **-16** | 报告原文 `:337: (ret not equal to 0)`；串口原文 `dma_config(2 blocks) failed: -16` | `serial_TGT.log:32`、`serial_TGT.log:35` |
| test_dma_loop_transfer | 功能通路 + 重复重载 | 4 轮 256B M2M 逐字节相等（`COVERAGE.md:38`；源码 `dma_test.c:53-97`） | 第 1 轮 `dma_config` = **-16**（第一轮即失败） | 报告原文 `:81: (ret not equal to 0)`；串口原文 `dma_config failed: -16` | `serial_TGT.log:39`、`serial_TGT.log:42` |
| test_dma_multi_channel_concurrent | 并发 | ch0/ch1 两路各自 memcmp 相等（不串扰）+ 两回调都触发（`COVERAGE.md:43`；源码 `dma_test.c:395-410`） | ch0 `dma_config(dma_dev, 0, &cfg0)` 返回非 0（`zassert_ok`，未打印数值；同时刻状态 = 3 ⇒ `-EBUSY`） | 报告原文 `:395: (dma_config(dma_dev, 0, &cfg0) is non-zero)`；串口原文 `ch0 config failed` | `serial_TGT.log:46`、`serial_TGT.log:48-49` |
| test_dma_transfer_ones_zeros | 功能通路 + 数据模式边界 | 全 0x00 / 全 0xFF 各 256B 逐字节相等（`COVERAGE.md:39`；源码 `dma_test.c:134-152`） | `dma_config` 返回非 0（`zassert_ok`，未打印数值；同时刻状态 = 3 ⇒ `-EBUSY`） | 报告原文 `:134: dma_config(dma_dev, DMA_CHANNEL, &cfg) is non-zero` | `serial_TGT.log:53`、`serial_TGT.log:55` |

> 未失败项：`test_dma_dmac2_transfer` PASS，唯一实测 INFO = `bytes=256,status=1`（`serial_TGT.log:21`）。
> 失败项中 `test_dma_multi_channel_concurrent` 的 `ZT_INFO`（源码 `dma_test.c:404`）、`test_dma_lli_chain` 的 `ZT_INFO`（源码 `dma_test.c:347`）均排在断言之后，未执行到 ⇒ **未采集到** INFO（原因：断言 `zassert` 致命中断，函数未继续）。

## 归因

**驱动缺陷（单一根因），不是用例或环境问题。**

根因链（6 个失败项全部指向同一处）：

1. `dw_dma_config()` 在 `complete_callback_en=1` 时只解除 **BLOCK / DST_TRAN** 屏蔽（`dma_dw_common.c:502-508`），**不解除 TFR 屏蔽**、也不注册 tfr 回调；`DW_MASK_TFR` 的解除只写在 `else` 分支（`dma_dw_common.c:512`）。
2. `dw_dma_isr()` 中**只有 TFR 分支**执行 `chan_data->state = DW_DMA_IDLE`（`dma_dw_common.c:130`）；BLOCK 分支（`:102-119`）与 DST_TRAN 分支（`:83-100`）都不复位状态。
3. ⇒ 任何一次成功传输后，`chan_data->state` **永远停在 3 = `DW_DMA_ACTIVE`**（枚举序 `dma_dw_common.h:264-267`：IDLE=0, PREPARED=1, SUSPENDED=2, ACTIVE=3）。
4. 此后 `dw_dma_config()` 入口的状态守卫判定 `state != IDLE && state != PREPARED` ⇒ `-EBUSY`（`dma_dw_common.c:189-193`）。
5. 而 `dw_dma_stop()` 在硬件已自清 `ChEn`（TFR 后硬件自行禁用通道）时走**早退分支直接返回 0**（`dma_dw_common.c:664-667`），**不复位 state** ⇒ 没有任何软件路径能把通道带回可配置态。

由此的失败分化：

- `test_dma_channel_config_stop` 是**首爆点**：它用 `complete_callback_en=1`，第一次 config→start→等回调→memcmp→stop 全部走过，但 `stop` 走早退分支（返回 0）而 state 仍是 ACTIVE，于是**第二次 `dma_config` 撞 `-EBUSY`** —— 这正是 `COVERAGE.md:40` 所要求的"停止后必须回到可配置态"不变量被驱动违反。
- 其余 4 项（`invalid_channel` / `lli_chain` / `loop_transfer` / `multi_channel_concurrent` / `transfer_ones_zeros`）是**级联**：`dma_test.c:472` 的 `ZTEST_SUITE(dma, NULL, dma_test_setup, NULL, NULL, NULL)` 第 4 个参数（before 钩子）为 **NULL**，即 case_default **没有**每项前释放通道的钩子（对比 `dma_destructive_test.c:502` 传入 `dma_test_before`；语义见 `COVERAGE.md:73`）。首爆点留下的 ACTIVE 通道被后续每一项继承，导致它们**连第一次 `dma_config` 都进不去**。
- `test_dma_dmac2_transfer` 之所以是唯一 PASS：它走 **dmac2 独立实例**（独立的 `dev_data->chan[]`），未被 dmac1/ch0 的污染状态波及。

`test_dma_invalid_channel` 的失败尤其值得注意：它的**正对照**（`dma_config(dev, 0, cfg)` 必须 == 0）被前面的状态污染打掉，说明本项此刻**根本没有验证"通道越界"这一维**——断言在正对照处就退了，ch99 的 `-EINVAL` 断言（`dma_test.c:250`）未执行。

**执行标记 FAILED 不是采集截断**：两份日志均完整跑完套件（7 项 START/PASS/FAIL 行齐全），并以 `TESTSUITE dma failed.` + `PROJECT EXECUTION FAILED` 收尾（`serial_TGT.log:59-61`），无中途断流迹象。

## 备注

- **历史对比（非本目录数据，取自工程 notes）**：该 case 在 `projects/dma_test/tests/notes/DMA_DW_BLOCK_TS_MASK_AND_TFR_STATE_ROOTCAUSE.md:108-109` 与 `810_driver_test/OPEN_ITEMS.md:42`（O-5）中记录为 **4/4 PASS**（会话 `reports/2026-09-17-dmaC`）。本次 7 项中 6 项 FAIL，属**明显回归**。
- **当前工作区驱动不在"已修"状态（重要，需人工确认）**：上述 notes 记录的修复在当前磁盘上的 `zephyr/drivers/dma/dma_dw_common.{c,h}`（mtime 2026-09-21 11:30，`git status` 干净）中**均不在位**：
  - 缺陷 2 的修复（"`DW_MASK_TFR` 解除屏蔽移出 else，两种回调模式都解除"）不在位 —— `dma_dw_common.c:507-508` 只在 `complete_callback_en` 分支解 BLOCK/DST_TRAN，`:512` 的 `DW_MASK_TFR` 解除仍在 `else` 分支内（notes `:103`）。
  - 缺陷 1 的修复（`DW_CTLH_BLOCK_TS_MASK` 移入 `#if CONFIG_DMA_DW_2_20A`）不在位 —— `dma_dw_common.h:186-187` 中 `#endif` 已闭合，`MASK(16, 0)` 仍定义在变体 `#if` 之外（notes `:102`）。
  - O-5 的 `src == dst` 显式拒绝不在位 —— 全文件检索无 `source_address ==` 比较（notes `:115-124`）。
  - **推断（需确认，非实测）**：驱动被还原为上游 pristine 版本（`git log -1` 指向 `8e862f7`，2026-04-29），此前**未提交**的本地修复（`OPEN_ITEMS.md:42` 明写"代码未提交"）丢失。本次失败高度符合"修复丢失后缺陷重新暴露"的形态。
- 本文中凡 ret 数值未由串口/报告直接打印者（`dma_test.c:134/210/395` 三处 `zassert_ok`），均已标注为"由 err 日志的状态值 3 + `dma_dw_common.c:189-193` 推定"，未作直采。

---

> **本轮复核（2026-09-22-2）**：失败项集合与本文件（源自 2026-09-21-1）**逐项一致**（自动比对报告 JSON 通过），结果数值亦一致 ⇒ 分析沿用，未复现新变化。
