# wdt — case_boundary 失败分析

| 项 | 值 |
|---|---|
| 工程 | wdt_test |
| case | case_boundary |
| 被测驱动 | `zephyr/drivers/watchdog/wdt_iwdg_ls.c` |
| 报告 | [wdt_test_report.md](../wdt_test_case_boundary/wdt_test_report.md) |
| 结果 | FAIL (17/19) |
| 执行标记 | CTRL=N/A（单核 1os 工程，无 CTRL 侧），TGT=FAILED（`PROJECT EXECUTION FAILED`） |
| 内存兜底 | 不适用（本工程无内存兜底） |

板级：`lsqsh_evb@1os/lsqsh/cpu1`（单核，故只有 `serial_TGT.log`）。落盘证据：`wdt_test_report.json` / `serial_TGT.log`（无 `serial_CTRL.log`）。

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| test_setup_pause_in_sleep_enotsup | 异常参数（`iwdt_ls_setup` 的 `WDT_OPT_PAUSE_IN_SLEEP` 分支） | `ret == -ENOTSUP`（值 -95）；前置 `install_timeout` 成功（`ch >= 0`） | `ret = -16`（= `-EBUSY`） | 报告原文：`wdt_boundary_test.c:265: (ret not equal to -ENOTSUP)`；串口原文 `setup(PAUSE_IN_SLEEP) returned -16, expected -ENOTSUP`。驱动 `LOG_INF` 直接落在 `IWDT_CTRL != 0` 分支：`wdt_ls_setup: interrupt already configured`——即 `wdt_iwdg_ls.c:76-79` 的 `-EBUSY` 提前返回，`options` 位根本未被检查 | 断言 `wdt_boundary_test.c:265`；`serial_TGT.log:62`（Assertion）/`:63`（实际值）/`:60`（驱动分支日志） |
| test_setup_twice_ebusy | 异常参数（`iwdt_ls_setup` 的 `IWDT_CTRL != 0` 分支；二次 `setup` 语义） | 首次 `setup` `ret == 0`；二次 `setup` `ret == -EBUSY` | 首次 `ret = -16`（= `-EBUSY`）（**首次即失败，未走到二次断言**） | 报告原文：`wdt_boundary_test.c:281: (ret not equal to 0)`；串口原文 `first setup returned -16, expected 0`。同一驱动分支日志 `interrupt already configured` ⇒ 进入本次 `setup` 时 `IWDT_CTRL != 0`，被 `wdt_iwdg_ls.c:76-79` 判为"已配置" | 断言 `wdt_boundary_test.c:281`；`serial_TGT.log:69`（Assertion）/`:70`（实际值）/`:67`（驱动分支日志） |

> 报告表格的「实测(INFO)」列 19 行**全为 `-`**（含本可给出 INFO 的 `test_load_ticks_*` 三项，其对应行 `serial_TGT.log:26/30/34/38/42` 确有 `INFO ... ticks=` 原文）。
> 即报告未回填 INFO 列，上述"实际值"取自 `serial_TGT.log` 的断言回显行原文，非报告列。
> 预期列同理：报告「期望」列亦为 `-`，预期取自 `projects/wdt_test/tests/COVERAGE.md` §2.2 与测试源码断言，二者一致。

## 归因

**驱动（A 类：真失败，粒度错误的既有登记缺陷）。** 两项失败共用同一前因，非独立故障：

- `iwdt_ls_setup()` 的"已在用"守卫写的是 **整寄存器非零** 判定——`wdt_iwdg_ls.c:76` `if (config->iwdg_reg->IWDT_CTRL)`（本次读源码实测行号；`KNOWN_ISSUES #92` 记的是 `:73`，为文件改动后的**行号漂移**，语义同一处）。
- 该 `IWDT_CTRL` 里除使能位 `IWDT_EN`(bit0)/`IWDT_RST_EN`(bit1) 外，还有两个**非使能语义**的 strobe 位 `IWDT_INTR_CLR`(bit2)、`IWDT_CNT_CLR`(bit3)。`iwdt_ls_feed()`（`wdt_iwdg_ls.c:59-61`）写 `CNT_CLR` 后硬件把该位留 1，`iwdt_ls_disable()` 的 `IWDT_CTRL = 0x0`（`:134`）也清不掉它。
- 于是**前序用例**（`test_setup_pause_halted_by_dbg_feed_disable`，`serial_TGT.log:56-57` 已 PASS，内含一次 `wdt_feed` + 一次 `wdt_disable`）留下 `IWDT_CTRL` 非零，后两项 `setup` 一进门就被误判"已配置" ⇒ 本应返回 `0` / `-ENOTSUP` 的分别返回 `-EBUSY`。
- **不是** `install_timeout()` 在 `:122` 清 `CTRL` 绕过守卫那条路径（P1-3）：两处失败点之间都没有 `install_timeout` 重置，`KNOWN_ISSUES #92` 亦明确警告勿按该路径归因。
- **用例侧期望是对的**：干净态下"首次 `setup` → 0"、"`PAUSE_IN_SLEEP` → -ENOTSUP"是正确契约，故不能判 B（用例问题）。套件 `before/after` 为 NULL、`teardown` 仅套件末跑一次，属**可选加固**，非根因。
- 现有建议修法（**未修，需上游裁决**）：守卫按位判 `IWDT_CTRL & (IWDT_EN_MASK | IWDT_RST_EN_MASK)`；`iwdt_ls_disable()` 本身无需改。

## 备注

- **与既有登记吻合**：即 `KNOWN_ISSUES.md` **#92**（2026-09-18 登记，含 GDB 实测定案；本轮为复现，非新发现），`tmp/final/triage.md` §4.2 亦判 A。
- **与 O-15"改判"无关，请勿混淆**：本 case 的 2 个失败项与 `iwdt_calculate_load` 的 64 位换算改判**不是同一批**。改判涉及的是 `test_load_ticks_just_below_saturation` / `test_load_ticks_saturation_boundary` / `test_load_ticks_uint32_max_timeout` 三项，本轮**全部 PASS**，且 `serial_TGT.log` 实测值 `ticks=4294967263`（`:30`）、`4294967295`（`:38`）、`4294967295`（`:42`）与 `COVERAGE.md` §2.2 的**改后（64 位）口径**逐项一致，**不是**作废的旧值 `4294934 / 0 / 4294934`。⇒ 本报告数字均为**改后口径**。
- 报告本体未回填「期望 / 实测(INFO)」两列（全 `-`），本次分析已按 `COVERAGE.md` + 源码断言 + 串口原文补齐；建议后续回归在报告生成侧回填这两列，避免每次分析都要回捞串口。
- 无 `serial_CTRL.log` 属分型正常（单核工程）；`execution: FAILED` 与 `TESTSUITE wdt_boundary failed`（`serial_TGT.log:89-91`）同源。
