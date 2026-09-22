# i2cf_test — case_default 失败分析

| 项 | 值 |
|---|---|
| 工程 | i2cf_test |
| case | case_default（18 项） |
| 报告 | [i2cf_test_report.md](../i2cf_test_case_default/i2cf_test_report.md) |
| 结果 | FAIL (17/18；失败 1) |
| 执行标记 | SUCCESSFUL 缺收尾（逐项已全部出结果） |
| 本轮配置 | 片内 target = `0x33`（overlay `reg=<0x33>`）；i2cf1/2/3 关闭 |
| 上一轮（2026-09-21-1） | FAIL (14/18)，失败 4 项，配置为片内 target `0x50` |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| filter_blocks_nonwhitelist | ❌ FAIL | token | ① filter ON、白名单外 `0x33` → `ret_blocked < 0`；② **filter OFF 后同址应可达** → `ret_free == 0` | ① 通过（被拦，负值）；② **`ret_free = -5`（`-EIO`）—— 关掉滤波器后同址仍不可达** | `i2cf_test.c:916: (ret_free is non-zero)`；日志原文 `with filter OFF 0x33 must be reachable, got -5` | `serial_TGT.log:45-51` |

## 归因

**归因 B/C：用例的「自证」步 + 驱动恢复路径待定性 —— 不确定，需驱动侧裁定。**

- **失败点是该用例的第二步（自证步）**：第 1 步（filter ON → 被拦，`ret<0`）**已通过**，第 2 步「关掉滤波器后同一地址应当恢复可达」实测仍 `-5`。⇒ **拦截动作本身生效**，问题在**恢复**。
- **与本轮改动无关**：本项在**基线（2026-09-21-1）在上一行就断了**（基线 `filter_blocks_nonwhitelist` 失败于 `:876`，即 filter ON 那一步）⇒ **本步是换址后才第一次被执行到**，**属潜伏项首次浮出，非改址引入**（同 `OPEN_ITEMS` O-30 记载）。
- **领先假设（未证实）**：拦截过后滤波器停在**阻塞/残留态**，需显式清中断（`INTR_CLR`）或硬复位才恢复；而该自证步**不清中断**，另一用例每笔前都清 ⇒ 它过。
- **明确不做**：**不得放宽 `ret_free == 0` 判据** —— 放宽会掩盖可能的驱动缺陷。（对照 `OPEN_ITEMS` O-31 的口径。）
- ⇒ **待驱动侧回答**：拦截后是否必须显式清中断才能恢复？正确恢复序列是什么？

> **本轮复核（2026-09-22-2）**：本文件为本轮**新写**（失败项与基线不同：基线 4 项 → 本轮 1 项）。
> 其余 3 项（`isr_addr_beyond_whitelist` / `master_eeprom_rw` / `filter_passthrough_whitelist`）
> 本轮**均已 PASS**，与换址后撞址消除的结论一致（详见 `OPEN_ITEMS` O-30、交接单 `tmp/i2cf_final/CONCLUSION.md`）。

## 备注

- 基线 4 FAIL → 本轮 1 FAIL：消失的 3 项全部是**旧 `0x50` 撞址污染**的产物（板载 EEPROM 与片内 target 同址）。
- **接线依赖提醒**：本 case 需 `i2c2 → i2cf4 输入`、`i2cf4 输出 → i2c3` 两段跳线。本项失败时**接线未见异常**（filter ON/透传/旁路各步均正常），故**不归因接线**。
- 关联：`OPEN_ITEMS` O-30；`KNOWN_ISSUES` #110/#111（更正段）。
