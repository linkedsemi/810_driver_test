# trng_test_default 失败分析

> ✅ **【2026-10-12 复测已转 PASS —— 本文件保留留痕】** 首轮 FAIL（`prime flash failed`，参见下方归因 C 环境）
> 经**定向复测**确认：DS 稳定后 `trng_test case_default` = **15/15 PASS, execution=SUCCESSFUL**，
> 同名 case 目录已覆盖（`summary.md` 该行现为 PASS 15/15）。⇒ **首轮 FAIL = 首 case 撞 DS 重启不稳定期的环境瞬态**。
> 本文件不再代表一个"未解决失败"，仅作首轮瞬态的留痕。


> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | （未给出） |
| case | logs |
| 执行标记 | （未给出） |
| 内存兜底 | （未给出） |
| 报告 | [trng_test_case_default.log](../logs/trng_test_case_default.log) |
| 结果 | FAIL 未知 |

## 失败项明细

| 测试项 | 结果 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| （无失败项可列） | FAIL | — | — | 本 case **未产出任何测试项**（固件未跑起来 / 采集未拿到） | `../logs/trng_test_case_default.log` / `logs/trng_test_case_default.log` |

> ⚠️ **优先看上面的「内存兜底」字段** —— 它若写「哨兵未置位 / invalid」，说明固件没跑到 `tr_init`（挂在早期初始化），这本身就是一条定位线索，别只盯着串口 0 行。

## 归因

**C 环境（瞬时）**。无测试项产出：`logs/trng_test_case_default.log` = `[run] CAPTURE FAILED: prime flash failed`。发生于整批**第 1 个 case**，紧随 `erase_flash_once` 后的 DebugServer 重启（**新 DS 不稳定期**，KNOWN_ISSUES #20）；其后主日志打出 `[ENV] restarting DebugServer (residue)` ⇒ 框架 **E-2 自愈**生效，后续 case 全部正常。

## 备注

**属首 case 撞 DS 重启不稳定期的瞬时失败**，非驱动/用例缺陷。建议：定向复跑 `trng_test case_default` （DS 已稳定）确认转 PASS，再据实归档。
