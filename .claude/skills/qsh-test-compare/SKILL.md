---
name: qsh-test-compare
description: QSH 测试数据对比 skill：token 结果解析、PASS/FAIL 判定（双核双 PASS 才算过）、Markdown/JSON 报告生成、多次回归结果差异对比、假 PASS 识别、串口不可用时 GDB 读内存取结果（dump_results.py）。分析测试输出、写报告、对比回归结果、串口无输出取结果时调用。
---

# QSH 测试数据对比与分析

结果解析、判定、报告、回归对比。**token 是唯一可信源**（CLAUDE.md §6），
肉眼扫串口不可靠。采集流程见 `qsh-test-run`，连接问题见 `qsh-debug`。

## 1. Token 解析规则

输入：dbg_run.py / run_*_tests.py 采集的原始行。解析步骤：
1. **剥离 ANSI**：`re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")` 先替换掉
   （shell 提示符重绘 `\x1b[8D\x1b[J` 会污染行首）
2. **去行首噪声**：token 行前可能有 `uart:~$ ` 前缀 → 用 `re.search()`，
   不用 `^` 锚点 / `.match()`
3. 匹配三类：`START` / `PASS|FAIL` / `SUMMARY:ITEM|END` / `DONE`

```python
TOKEN_RE = re.compile(r"I3C_TEST:(CTRL|TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*")
INFO_RE = re.compile(r"I3C_TEST:(CTRL|TGT):INFO:(\w+):([\w.=,\-]+)\s*")
SUMMARY_RE = re.compile(r"I3C_TEST:(CTRL|TGT):SUMMARY:(.*)")
DONE_RE = re.compile(r"I3C_TEST:DONE:(PASS|FAIL)")
```

4. **INFO token**（CLAUDE.md §13.4，数值实测上报）：测试项的 PASS/FAIL 之前
   可能先出 `INFO:<name>:<key>=<value>[,<key>=<value>...]`（如
   `TRNG_TEST:TGT:INFO:monobit_8k:ones=32775,percent=50.0`），
   解析后填入报告"实际"列，与 COVERAGE.md 的预期对比。

## 2. 判定标准（★ 双核双 PASS 才算过）

| 情况 | 判定 |
|---|---|
| CTRL:PASS + TGT:PASS | ✅ 通过 |
| 仅 CTRL:PASS、TGT 无 token 或 NOT_RUN | ❌ 不算（target 没跑/假 PASS） |
| 任一 FAIL（带 reason） | ❌ 失败，reason 即根因线索 |
| 无 START 的 PASS（顺序错乱） | 警惕两轮输出混合，重新采集 |

**假 PASS 识别**（实测发生过）：
- target 只校验 `last_read_len` 而 controller 实际读到 0 → target 假 PASS
  （write_read_×4 场景）。判定时 controller 侧 FAIL 了就不算过，即使 target PASS。
- 汇总表 `NOT_RUN` 项：target 没收到对应命令，整项重测。

## 3. 串口不可用时的兜底通道：GDB 读内存取结果（dump_results.py）

固件把每项测试结果记账到公共模块 `common/test_results.{h,c}` 的全局数组
（CLAUDE.md §4.5：`int8_t test_results[]` -1/0/1 + `char test_reasons[][96]`），
**链接地址=加载地址**，连 DebugServer 直接读内存即可，不依赖串口。

```bash
python dump_results.py --case case_pure_i3c_12m5               # 只读当前内存
python dump_results.py --case case_pure_i3c_12m5 --json        # JSON 输出（回归兜底用，§12.3）
python dump_results.py --case case_pure_i3c_12m5 --run 35      # 先两遍烧录跑 35s 再读
python dump_results.py --case ... --raw                        # 打印 gdb 原始输出
```

要点（脚本已实现，勿手写 gdb 流程）：
- `riscv64-unknown-elf-nm` 解析 `test_results`/`test_reasons` 符号地址
  （两核 elf 各查一次，符号同名按 elf 区分核）
- gdb 会话：`target remote` + `interrupt` + `x/<count>db 0xaddr` → 对 FAIL 项
  `x/s` reason（地址 = 基址 + i*96）→ `detach` 干净退出
- 判定：**串口可用时仍以 token 为准**（§2）；内存结果用于串口被刷掉/无串口
  时的兜底与交叉验证。双核结果对照同 §2 判定标准。
- `--json` 输出结构：`{"CTRL": {"<name>": "PASS|FAIL|NOT_RUN", ...},
  "TGT": {...}, "reasons": {"<name>": "..."}}`（单核工程只有 TGT）。
  回归脚本用它做**兜底合并**：token 缺失的项用内存值补，来源标 `mem`
  （CLAUDE.md §12.3，规则见下）。

## 4. 报告生成（★ 归档到 reports/，CLAUDE.md §12）

回归报告由 run_*_tests.py 自动生成并归档，agent 手工补报告时按同样规范：

- **归档位置**：`810_driver_test/reports/<YYYY-MM-DD>/<driver>_test_report.{md,json}`
- **Markdown 模板**（§12.2）：表头含 `预期 | 实际(INFO) | 结果 | 来源 | reason`；
  **预期**列来自该工程 COVERAGE.md，**实际**列来自 INFO token，
  **来源**列 `token`/`mem`（内存兜底）
- **JSON**：结构建议：
  ```json
  {
    "date": "2026-08-21 10:30", "board": "lsqsh_evb@2os",
    "case": "case_pure_i3c_12m5",
    "tests": [
      {"name": "daa_discovery", "ctrl": "PASS", "tgt": "PASS",
       "info": {"ctrl": "..."}, "src": "token", "reason": ""},
      {"name": "write_read_1", "ctrl": "FAIL", "tgt": "PASS",
       "reason": "read actual 0, URUNNACK"}
    ],
    "done": "PASS", "passed": 14, "ran": 15
  }
  ```
- 汇总行：`passed/ran` + `DONE:PASS|FAIL`
- **summary.md 索引**（§12.4）：当天全工程汇总，行格式
  `| <driver>_test | <日期 时间> | PASS|FAIL | <passed>/<ran> | 备注 |`；
  每工程脚本按工程名**替换**自己的行，不重复追加

## 5. 回归差异对比

对比两次回归（如探针清理前后、修复前后、`reports/` 下不同日期归档）：
1. 对齐同名测试项，生成差异表：`PASS→FAIL`（回归）/ `FAIL→PASS`（修复生效）/ 双 FAIL
2. **改前改后对比时先确认环境一致**：DebugServer 无连接残留、同 case、同 timeout
   （连接残留会让"修复"看起来像没生效，见 qsh-debug §2.3）
3. 差异项先看 reason 再下结论；单次 FAIL 先重跑排除瞬态
   （GDB 证据与串口日志矛盾时先重跑，排除 debug server 瞬态）
4. 跨日期对比直接读 `reports/<date>/<driver>_test_report.json`（含 INFO 实际值，
   可对比数值漂移，如吞吐/占比随探针/时钟配置变化）

## 6. 问题定位辅助（token 之外的信号）

dbg_run.py SUMMARY 里过滤的关键行（驱动/固件汇总）：
- `SERRWARN` / `URUN` → 硬件 NACK/欠载，总线上没应答
- `write_len 0` → target 没收到写数据（协议/时序问题）
- `read actual N != 32` → controller 读长度不符
- `ready IBI timeout` → 就绪握手超时，target 中断/状态未就绪
- 测试 ~6.5s 停摆无 SUMMARY/DONE → 目标侧死窗或停摆，查 8ms 死窗文档
  （`tests/ZTEST_PANIC_LOG_8MS_DEADWINDOW.md`）

## 7. 坑沉淀要求

对比分析中发现的新模式（如新假 PASS 形式），追加到 §2 假 PASS 识别 +
CLAUDE.md §10。
