# emmc_test `set_io_clock_out_of_range_not_silent` 失败分析

> **⚠️ 历史轮次（HEAD `b30f5d04223`，结果 11/12）。** 后续 HEAD `f4fe4746114` 一度出现
> `clock == 0` 被拒的初始化回归（3/12），见
> [`emmc_test_clock0_init_regression.md`](emmc_test_clock0_init_regression.md)。
> **最终 HEAD `a9aebb55833` 已修复，两 case 均 PASS 12/12，本靶点实测 `ret=-22` 转 PASS。**

> 会话：`2026-10-22-1` · 被测版本：`zephyr` HEAD `b30f5d042231b786c8507128d9cfef9d4159f9c4`
> （分支 `dw_dma_ctlh_block_ts_mask`；EMMC 相关子树 `subsys/sd`、`include/zephyr/sd`、
> `drivers/disk`、`drivers/sdhc` 工作区干净）
> 结论：**已知缺陷靶点，非本次 EMMC 改动引入的回归**。

## 失败项明细

| 项 | 值 |
|---|---|
| 工程 | emmc_test |
| case | `case_block_driver`（SDMA=y）/ `case_block_pio`（SDMA=n） |
| 结果 | 两 case 均 **FAIL (11/12)**，唯一失败项相同 |
| 失败项 | `set_io_clock_out_of_range_not_silent` |
| 证据 | `emmc_test_case_block_driver/serial_TGT.log`、`emmc_test_case_block_pio/serial_TGT.log` |
| 复现 | 两 case 独立烧录各一轮，行为逐字一致（同一固件、仅 conf 不同） |

## 场景 / 预期 / 现象

- **场景**：卡片已初始化（`ensure_card()` 成功），调用 `sdhc_set_io()` 并传入
  `clock = 1000 Hz`，低于 DT 的 `min-bus-freq`（`f_min = 50000`）。该路径不改控制器寄存器，
  对 eMMC 无副作用。
- **预期**：越界时钟不得静默接受 —— `sdhc_set_io()` 应返回非 0（上游 SDHC 契约为 `-EINVAL`，
  COVERAGE 该行标为「❌预期FAIL」缺陷靶点）。
- **现象**：驱动静默忽略该参数并返回 0。串口实测
  `INFO set_io_clock_out_of_range_not_silent: f_min=50000,clock=1000,ret=0`
  （两 case 一致）。对应驱动 `zephyr/drivers/sdhc/sdhci-of-linkedsemi.c:198-202`：
  仅当 `clock` 落在 `[min_bus_freq, max_bus_freq]` 内才改频，越界分支既不报错也不返回错误码。
- **为什么 `ret=0` 不阻塞后续**：该实现只跳过 `mmc_clock_freq_change()`，没有寄存器副作用，
  故用例按契约断言失败，未影响同套件其它项（12 项中其余 11 项全 PASS）。

### 本轮复跑（测试侧补 `ZTEST_SUITE` 后）新增执行的两项 —— 均 PASS

测试源 `projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c` 在套件 1 前补入
`ZTEST_SUITE(emmc_app_cpu_guard, NULL, NULL, NULL, NULL, NULL);`（源码由协调方修改，
本轮未再改动），`emmc_app_cpu_guard` 的 2 项首次真正执行。ELF 已含
`z_ztest_suite_node_stats_emmc_app_cpu_guard`，串口 `Running TESTSUITE` 由 2 条变 3 条。

| 测试项 | 结果 | 实测 INFO | 判读 |
|---|---|---|---|
| `guard_matches_driver_behavior` | ✅ PASS | `app_cpu_srst=0x0,get_host_props=0,card_present=66715888` | 当前加载环境 `APP_CPU_SRST=0` ⇒ 守卫不生效分支；`sdhc_get_host_props()` 返回 0、`sdhc_card_present()` 非负，与预期一致 |
| `host_props_match_devicetree` | ✅ PASS | `f_max=200000000,f_min=50000,power_delay=500,hs200=1,hs400=0,bus8=1,bus4=1,is_spi=0` | 与 DT 逐字段一致（`max/min-bus-freq` 由 `DT_PROP` 直接比对）；`hs400=0` 亦钉住 HS400 `while(1);` 分支不可达的前提 |

两 case 的上述 INFO 值逐字相同（同一固件，仅 SDMA conf 不同）。

## 归因

**驱动侧已知缺陷靶点，且本次未被修复——原因是修复分支未合入被测分支，不是回归。**

- 修复存在：`df4bc2a46e7`（分支 `driver_test`）与 `78443c78846`（分支
  `sdhci_of_linkedsemi_fix`），2026-09-23，「... reject out-of-range clock」，
  在 `set_io` 中越界时打印 `clock %u out of range` 并 `return -EINVAL`。
- `git merge-base --is-ancestor <fix> HEAD` 对两个 commit 均返回 **NO** ⇒ 被测 HEAD
  不含该修复。COVERAGE 的 2026-09-23 批注把该行改判为「⏳ 待上板实测（应当转 PASS）」
  是按**当时工作区**（未提交）写的；该工作区改动后来被提交到 `driver_test` /
  `sdhci_of_linkedsemi_fix`，未进入当前分支。这是 `qsh-test-run` §4.1
  「G-BRANCH-1：分支差异常被误读成驱动回归」的同类情形。
- 与本次 EMMC 改动（`3dea124f037`、`ac39f0551cd`、`33138948869`、`e6a20960a3c`、
  `9c46c4745c9`、`ab2b9f2ee8e`、`cdbc3200b90`、`b30f5d04223`）无调用关系：
  失败发生在 `sdhci_set_io()` 的时钟边界分支，BKOPS 系列改动均在 `subsys/sd` 的
  eMMC 初始化/CMD6/重试路径。本次 `sd_init` 全链路（CMD 序列、EXT_CSD 解析、
  `disk_access_init=0`）全 PASS。

## 对照项（本次已转 PASS，非失败但值得记录）

| 测试项 | 本次结果 | 证据 | 说明 |
|---|---|---|---|
| `request_mutex_not_leaked` | ✅ PASS | `INFO ... join=0,probe_ret=-134,probe_ret2=-134,done=1` | 被测 HEAD 已含 2026-07-03 的 `5a789cc15db fix(sdhc): fix mutex leak on unsupported data opcode`（单出口 `goto out` + `out: k_mutex_unlock`），故两次非法请求均立即返回 `-ENOTSUP`，无泄漏。2026-10-12 在旧状态上的 FAIL 已不复现。 |
| `request_data_invalid_opcode_notsup` | ✅ PASS | 同上（`-134` = `-ENOTSUP`） | 与 mutex 项同源。 |
| 其余 9 项 | ✅ PASS | 各 case 串口 `SUITE PASS` | 块层读写/ioctl（7 项）+ 本轮新增执行的 2 项 `emmc_app_cpu_guard`；SDMA 与 PIO 两通路均正常。 |
