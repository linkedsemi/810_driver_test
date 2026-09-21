# SPI filter (PFR/TMP) — spif_test_case_default 失败分析

| 项 | 值 |
|---|---|
| 工程 | spif_test |
| case | `spif_test_case_default`（报告内记 `case_default`） |
| 被测驱动 | `D:\zephyr4_0\modules\lib\pfr_tpm\drivers\pfr\spi\spi_filter.c`（接口 `spi_filter.h`，寄存器 `reg_spi_filter.h`） |
| 报告 | [spif_test_report.md](../spif_test_case_default/spif_test_report.md) |
| 结果 | FAIL (16/18) |
| 执行标记 | CTRL=未采集到（本 case 为 `lsqsh_evb@1os/lsqsh/cpu1` 单核工程，报告目录内无 `serial_CTRL.log`，`replay_meta.json` 只登记 `TGT`/COM16/182 行）, TGT=PROJECT EXECUTION FAILED |
| 内存兜底 | 有效（`spif_test_report.md:9`；`spif_test_report.json` 的 `mem_state="ok"`, `missing_evidence=[]`） |

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| cmd_idx_accessors | 功能通路（固定命令表按 idx 读写往返） | `spif_set_dummy_by_idx(dev, 9, 5)` 后 `spif_get_dummy_by_idx(dev, &dummy, 5)` 回读 `dummy == 9`（COVERAGE.md:34 预期数据「CMD 与 DUMMY_CYCLE 按 idx 读写往返一致」；断言在 `spif_test.c:327` 的 `zassert_equal(dummy, 9, ...)`） | **未采集到** —— 断言在用例的 `ZT_INFO` 之前触发，用例与报告均未打印 `dummy` 回读值；串口只有断言原文。可确定的是 `dummy != 9` | 报告原文：`spif_test.c:327: spif_cmd_idx_accessors: (dummy not equal to 9)`（`spif_test_report.md:22`、`spif_test_report.json` 的 `reasons.cmd_idx_accessors`） | `serial_TGT.log:37-41`；`spif_test.c:321-327` |
| passthrough_mode_switch | 功能通路（pinctrl 三态互斥） | cold_reset 后 `filter_mode_check()==true` **且** `passthrough_mode_check()==false`（COVERAGE.md:43；断言在 `spif_test.c:776` 的 `zassert_false`） | `spif_pinctrl_passthrough_mode_check(dev)` 返回 **true**（即断言「is false」失败）。紧邻的 `spif_pinctrl_filter_mode_check(dev)` 为 true —— 该断言在 `spif_test.c:774`，**未**被报错 ⇒ cold_reset 后 filter 与 passthrough **两态判据同时为 true，不互斥** | 报告原文：`spif_test.c:776: spif_passthrough_mode_switch: (spif_pinctrl_passthrough_mode_check(dev) is true)`（`spif_test_report.md:31`、`spif_test_report.json` 的 `reasons.passthrough_mode_switch`） | `serial_TGT.log:163-167`；`spif_test.c:771-777` |

## 归因

### 1. `cmd_idx_accessors` — 驱动侧（字段写回未生效），**具体根因无法判定**

- 同一 idx(5) 的前置 `CMD` 往返（`spif_set_cmd_by_idx` → `spif_get_cmd_by_idx`，`spif_test.c:321-323`）**通过**，说明：寄存器 `dev_config->base + SPIF_CMD_BASE + 5*4` 的**读-写通路与信号量保护均正常**。
- `spif_set_dummy_by_idx()`（`spi_filter.c:1015-1032`）与 `spif_set_cmd_by_idx()`（`spi_filter.c:964-981`）是**逐行同构**的 RMW（`value = sys_read32(...)` → 改字段 → `sys_write32(...)`），唯一差别是写入的字段（`DUMMY_CYCLE` vs `CMD`）。CMD 位成功、DUMMY 位失败 ⇒ 问题集中在 `DUMMY_CYCLE` 字段本身。
- 因此指向两点之一，**无法区分**（缺实际回读值）：
  - (a) 该 IP 的 `DUMMY_CYCLE` 实际位宽/位置与 `reg_spi_filter.h:182` 的声明（`DUMMY_CYCLE : 5 /* [12:8] */`）不符 ⇒ 9 被截断/搬到别处；
  - (b) 固定命令表该字段写入被硬性忽略（例如仅对 general 槽生效）。
- **判定不了的原因（缺的证据）**：`dummy` 的实际回读值未采集到。补齐方式：报告/用例在断言**之前**加一条 `ZT_INFO("cmd_idx_accessors","dummy=%u",dummy)`，或直接回读 `SPIF_CMD_BASE + 5*4` 的整字。注意本项在 `COVERAGE.md:213` 的独立预期表中被登记为 `!none`（"合取、无单一标量"），预期值本身来自 `spif_test.c:327` 的字面断言，无外部黄金值可交叉。
- 若补齐后 `dummy == 1`（= `9 & 0x7`）或 `dummy == 0` 之类，则坐实 (a)。

### 2. `passthrough_mode_switch` — **用例判据不可满足（DT + 驱动判据共同导致），不是运行时状态机故障**

有直接源码证据，可判定：

- 用例断言 `cold_reset 后 passthrough_mode_check()==false`，但 `cold_reset` 走的是 `pinctrl-apply(DEFAULT)`（`spi_filter.c:1629` 起的 reset 序列 + `spif_switch_to_filter()` `:1482-1492`），即应停在 **default(filter)** 态。
- 驱动判据 `spif_pinctrl_mode_check()`（`spi_filter.c:1429-1458`）**只比对 `pins->pinmux`**（`pins->pinmux.pin` / `pins->pinmux.func` / `pins->pinmux.func_valid` 与实管脚 `per_func_get(pin)`），**从不读 `pins->pin_attr`**（`:1445-1455`）。
- 而 DT 里 spif2 的 **default 与 passthrough 两态用的是同一个 pinmux 单元**，仅靠额外属性 `analog;` 区分：
  - default：`spif2_clk_in_pg08 { pinmux = <SPIF2_CLK_IN_FUNC4_PG08_PINMUX>; input-enable; };`（`lsqsh_pinctrl_pfr_tpm.dtsi:320-323`）
  - passthrough：`spif2_clk_in_pg08_analog { pinmux = <SPIF2_CLK_IN_FUNC4_PG08_PINMUX>; input-enable; analog; };`（`:596-600`）
  - `analog` 被存进**独立字段** `pin_attr.analog`（`zephyr/soc/linkedsemi/lsqsh/pinctrl_soc.h:54`、`:91`），**不进入 `pinmux`**。
- 于是对这两个态，`spif_pinctrl_mode_check()` 的每一步比较结果**完全相同** ⇒ `filter_mode_check(dev) == passthrough_mode_check(dev)` 恒成立。用例在 `spif_test.c:774` 已断言 filter 为 true，则 `spif_test.c:776` 的 passthrough 为 false **在构造上必然失败**，与硬件实际停在哪个态无关。
- 结论：**归因 = 用例判据（预期写错）/ 驱动判据不充分（`spif_pinctrl_mode_check` 漏比 `analog` 属性）**，二者任一处修即可；不是 pinctrl 状态机没切过去。这一条同时印证 `COVERAGE.md:43` 里对 passthrough 判据「比对 pinctrl 状态表与实管脚 func」的设计假设在 spif2 上不成立。
- 备注：本项在 `COVERAGE.md:216` 独立预期表登记为 `!none`，无机器可读预期，故不会与归档值冲突。

## 备注

- 本轮 FAIL 2 项、PASS 16 项；`spif_test_report.json` 的 `execution="FAILED"`、`missing_evidence=[]`（归档脚本认为证据齐全），但上述两项的**实际值**在串口中确实不存在——`missing_evidence` 只覆盖 case 级采集，不覆盖单条断言的可观测值。
- 与 `KNOWN_ISSUES.md` 的对照：本项未见已在册条目命中。`passthrough_mode_switch` 的 `analog` 属性漏比属**新增发现**，建议按缺陷编号登记到 `COVERAGE.md` 的「上游缺陷观察」表（现有 #1–#20 均不含此条）。
- 同目录 `pinctrl_state_switch` 本轮 PASS（`serial_TGT.log:169-170`），说明 spif3→spif4 的 filter/master 判据有区分度；差异在于 master 态用的是**不同 pinmux 单元**（`spi3_mst_*`），与 spif2 的 default/passthrough 同 pinmux 不同。可佐证第 2 条的归因。
