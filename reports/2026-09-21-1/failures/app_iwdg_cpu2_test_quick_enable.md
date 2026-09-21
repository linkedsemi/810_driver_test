# app_iwdg_cpu2_test — case_quick_enable 失败分析

| 项 | 值 |
|---|---|
| 工程 | app_iwdg_cpu2_test |
| case | case_quick_enable |
| 被测驱动 | `zephyr/drivers/watchdog/wdt_iwdg_ls.c` |
| 报告 | [app_iwdg_cpu2_test_report.md](../app_iwdg_cpu2_test_case_quick_enable/app_iwdg_cpu2_test_report.md) |
| 结果 | FAIL (2/4) |
| 执行标记 | CTRL=FAILED, TGT=SUCCESSFUL |
| 内存兜底 | 不适用（本工程无内存兜底） |

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| cpu2::test_quick_enable_boot_state_feed_disable | 功能通路 | 启动态 `IWDT_LOAD==1966080`；启动态 `IWDT_CTRL==0x3`；`wdt_feed` 返回 0；`wdt_disable` 返回 0 **且回读 `IWDT_CTRL==0`**（`COVERAGE.md:86`；断言见 `app_iwdg_quick_test.c:56,59,66,71,72`） | 前 4 条断言通过（INFO：`ms=60000,load=1966080,ctrl=0x3`）；末条「disable 后回读 `IWDT_CTRL`」非 0 —— **具体位值未采集到**（断言消息未打印实测值） | 报告原文：`app_iwdg_quick_test.c: 72: app_iwdg_quick_test_quick_enable_boot_state_feed_disable: (APP_IWDG_REGS[IWDG_CTRL_WORD] not equal to 0u)` | `serial_CTRL.log:11`（断言行）、`serial_CTRL.log:9`（INFO 行）、`serial_CTRL.log:13`（FAIL 行） |

## 归因

**指向驱动/硬件层与用例预期不匹配（非采集问题）。**

- 该测试项是单个 ztest 项内固定顺序的 5 条断言（`app_iwdg_quick_test.c:44-74`）。ztest 在首条失败断言处中止本项，故可反推**前 4 条全部通过**：`load==1966080`（`:56`）、`ctrl==0x3`（`:59`）、`feed==0`（`:66`）、`dis==0`（`:71`）。唯一失败点是 `:72` —— `wdt_disable()` 返回 0 成功，但**回读 `IWDT_CTRL` 非 0**。
- `iwdt_ls_disable()` 实现为整寄存器直写 `IWDT_CTRL = 0x0`（`wdt_iwdg_ls.c:131-136`）。驱动侧写入动作本身无返回值异常（`dis==0` 已证）。
- 本项在 disable 之前**先执行过 feed**（`app_iwdg_quick_test.c:64`）。feed 路径 `MODIFY_REG(IWDT_CTRL, CNT_CLR|INTR_CLR, 1→0)`（`wdt_iwdg_ls.c:59-61`），HAL 位定义 `IWDT_CNT_CLR_MASK=0x8`(bit3)、`IWDT_INTR_CLR_MASK=0x4`(bit2)（`reg_iwdgv2_type.h:34-37`）。
- **既有实证**：`KNOWN_ISSUES.md:1041-1046`（#92，2026-09-18）已记录同一寄存器同一现象 —— 「`feed` 后硬件**留存** `IWDT_CNT_CLR`(bit3)，使整寄存器读回非零」，该条正是上一轮 `wdt_test::test_setup_twice_ebusy` 两项失败方向的根因。本次 `:72` 断言读的是**同一个寄存器**、且同样发生在 feed 之后，机制高度吻合。
- 因此本失败最可能是：**disable 已把 EN/RST_EN 清掉，但 `IWDT_CTRL` 回读仍残留 CNT_CLR/INTR_CLR 之类的硬件留存位（预期 0x8 量级）**；用例把「回读整寄存器 == 0」当作 disable 的判据，判据本身对本硬件过强。**注意：残留位的具体数值本轮未采集到**（断言未打印实测值），故"是否恰为 0x8"属推断而非实测，标注为待验证假设。
- **不是采集截断**：CTRL 侧日志完整走到 `PROJECT EXECUTION FAILED`（`serial_CTRL.log:16`），TGT 侧完整走到 `PROJECT EXECUTION SUCCESSFUL`（`serial_TGT.log:20`），语义内容无截断。报告中的 `run::timeout`（case_timeout=120s 到点）是设计使然 —— cpu2 作为被测量核套件结束后 `k_sleep(K_FOREVER)` 不返回（`main.c:66`，§6.1.1），与本次断言失败无关。
- **还缺的证据**：若要坐实"残留位"结论，需在 `:72` 断言前打印 `APP_IWDG_REGS[IWDG_CTRL_WORD]` 的实际十六进制值（当前未采集到）。

## 备注

- 失败方向与 `KNOWN_ISSUES.md` #92（2026-09-18）同源：**feed 会在 `IWDT_CTRL` 留下非零残留位**；#92 影响的是 `setup` 的 EBUSY 守卫，本案影响的是 disable 后的整寄存器回读断言。
- 不可与 `KNOWN_ISSUES.md:752-758`（#92 之前的 `iwdt_calculate_load` uint32 溢出死分支，2026-09-18 已修）混淆 —— 本项 `load==1966080` 断言已通过，溢出修复有效。
- 建议修法（供参考，未实施）：`:72` 改为只校验使能位，如 `zassert_equal(APP_IWDG_REGS[IWDG_CTRL_WORD] & 0x3u, 0u, ...)`，或在断言前将实际值经 `ZT_INFO` 打印以留证。
