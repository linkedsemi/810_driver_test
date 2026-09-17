---
name: qsh-test-compare
description: QSH 测试数据对比 skill：ztest 原生输出解析（parse_ztest）、PASS/FAIL 判定（同名测试项双核双 PASS + 两核 PROJECT EXECUTION SUCCESSFUL；i3c_dualcore 例外 = CTRL SUCCESSFUL 且 TGT 无该行属设计）、Markdown/JSON 报告生成（会话目录 reports/<date>-<idx>/）、多次回归结果差异对比、假 PASS 识别、串口不可用时 GDB 读内存取结果（dump_results.py）。分析测试输出、写报告、对比回归结果、串口无输出取结果时调用。
---

# QSH 测试数据对比与分析

结果解析、判定、报告、回归对比。**串口 ztest 原生输出是唯一可信源**
（CLAUDE.md §6），肉眼扫串口不可靠。采集流程见 `qsh-test-run`，连接问题
见 `qsh-debug`。

## 1. ztest 原生输出解析规则

输入：dbg_run.py / run_test.py 采集的原始行。**解析用公共模块
`common/ztest_report.py` 的 `parse_ztest`**（勿自写解析器），返回
`{"boot_seen", "role", "tests": {name: {result, reason}}, "infos", "execution"}`。
正则要点（与解析器一致）：
1. **剥离 ANSI**：`re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")` 先替换掉
   （shell 提示符重绘 `\x1b[8D\x1b[J` 会污染行首）
2. **去行首噪声**：输出行前可能有 `uart:~$ ` 前缀 → 用 `re.search()`，
   不用 `^` 锚点 / `.match()`
3. 匹配的协议行（CLAUDE.md §6）：
   `ZT_BOOT (\S+)`、`(PASS|FAIL|SKIP) - (\S+) in [\d.]+ seconds`、
   `INFO (\S+):\s*(.*)`（k=v 逗号切分）、
   `PROJECT EXECUTION (SUCCESSFUL|FAILED)`；
   reason 归因：zassert 的 `Assertion failed at ...` 打印在 FAIL 行**之前**，
   解析器暂存断言行归到该项（§13.5）

4. **INFO 约定行**（CLAUDE.md §13.4，数值实测上报）：测试项的 PASS/FAIL 之前
   可能先出 `INFO <name>: <k>=<v>[,<k>=<v>]`（如
   `INFO monobit_8k: ones=32931, percent=50.25`），解析后填入报告"实际"列，
   与 COVERAGE.md 的预期对比。

## 2. 判定标准（★ 同名测试项双核双 PASS + 完成标记，i3c 例外见下）

| 情况 | 判定 |
|---|---|
| 单核：全项 `PASS - <name>` + 本核 `PROJECT EXECUTION SUCCESSFUL` | ✅ 通过 |
| 双核（i2c_pure/i3c_i2c_switch）：同名项两核各自 `PASS - <name>` + 两核各自 `PROJECT EXECUTION SUCCESSFUL` | ✅ 通过 |
| 双核（i3c_dualcore）：同名项两核各自 `PASS - <name>` + CTRL 侧 `PROJECT EXECUTION SUCCESSFUL`（TGT 端口无该行属设计，target test_main 不返回） | ✅ 通过 |
| 仅一核 PASS、另一核无输出或 NOT_RUN | ❌ 不算（target 没跑/假 PASS） |
| 任一 FAIL（reason 来自 zassert `Assertion failed at` 行） | ❌ 失败，reason 即根因线索 |
| `PROJECT EXECUTION FAILED`（任一核；i3c 含 TGT 侧意外出现） | ❌ 失败（套件内有断言失败） |
| 无 `START - ` 的 PASS（顺序错乱） | 警惕两轮输出混合，重新采集 |

**i3c_dualcore 双核例外（★ 照 `run_test.py` `evaluate_case` 实码）**：
该工程 target `test_main` 不返回（CLAUDE.md §4.1 dispatcher join）→ TGT 端口
**永远没有** `PROJECT EXECUTION` 行，`tgt_done=None` 是设计（Task 9 D1），
不判 FAIL；完成判定 = **CTRL SUCCESSFUL 且 TGT execution != "FAILED"**，
TGT 失败经逐项 FAIL 行暴露。i2c_pure/i3c_i2c_switch 的 target 正常返回，
仍要求两核各自 SUCCESSFUL。

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
- 判定：**串口可用时仍以串口 ztest 输出为准**（§2）；内存结果用于串口被刷掉/
  无串口时的兜底与交叉验证。双核结果对照同 §2 判定标准。
- 记账已自动化（glue 按 ztest stats 批量写入，§4.5），**TEST_NAMES 必须按
  ztest_unit_test 链接序**（SORT_BY_NAME，§10.27）与记账 idx 对齐——
  用 `nm --numeric-sort <elf> | grep _ztest_unit_test__` 核对，勿按源码序/plain
  nm 名字典序（尾下划线坑，数字在 '_' 之前）。
- `--json` 输出结构：`{"CTRL": {"<name>": "PASS|FAIL|NOT_RUN", ...},
  "TGT": {...}, "reasons": {"<name>": "..."}}`（单核工程只有 TGT）。
  回归脚本用它做**兜底合并**：串口缺失的项用内存值补，来源标 `mem`
  （CLAUDE.md §12.3，规则见下）。

## 4. 报告生成（★ 归档到会话目录 reports/，CLAUDE.md §12）

回归报告由 run_test.py 自动生成并归档（批量 = run_all.py，§12.5），
agent 手工补报告时按同样规范：

- **归档位置（会话目录）**：
  `810_driver_test/reports/<YYYY-MM-DD>-<idx>/<project>_<case>/<driver>_test_report.{md,json}`
  ——分配/写入一律走公共 `common/report_dir.py`（`allocate_session_dir` /
  `case_dir_for` / `update_summary`），禁止手工/脚本自算路径；一次批量会话
  内全部 case 共用同一会话目录（i3c 7 case 各自独立子文件夹）；单跑 case
  不带 `--session-dir` 自动分配新会话目录
- **Markdown 模板**（§12.2）：表头含 `预期 | 实际(INFO) | 结果 | 来源 | reason`；
  **预期**列来自该工程 COVERAGE.md，**实际**列来自 INFO 约定行（`INFO <name>:
  k=v[,k=v]`），**来源**列 `token`（串口 ztest 输出）/`mem`（内存兜底）
- **JSON**：结构建议：
  ```json
  {
    "date": "2026-08-21 10:30", "board": "lsqsh_evb@2os",
    "case": "case_pure_i3c_12m5",
    "tests": [
      {"name": "daa_discovery", "ctrl": "PASS", "tgt": "PASS",
       "info": {"ctrl": "..."}, "src": "token", "reason": ""},
      {"name": "write_read_1", "ctrl": "FAIL", "tgt": "PASS",
       "reason": "rtc_test.c: 85: rtc_set_get_roundtrip: (g.tm_mday not equal to s.tm_mday)"}
    ],
    "execution": "SUCCESSFUL", "passed": 14, "ran": 15
  }
  ```
- 汇总行：`passed/ran` + `execution=SUCCESSFUL|FAILED`
- **summary.md 索引**（§12.4）：只在会话目录最外层，**行粒度 = case**
  （key `<project>_<case>`），行格式 `| [<key>](<报告相对路径>) | <PASS|FAIL>
  | <passed>/<total> |`；按 key **替换**自己的行，不重复追加

## 5. 回归差异对比

对比两次回归（如探针清理前后、修复前后、`reports/` 下不同日期归档）：
1. 对齐同名测试项，生成差异表：`PASS→FAIL`（回归）/ `FAIL→PASS`（修复生效）/ 双 FAIL
2. **改前改后对比时先确认环境一致**：DebugServer 无连接残留、同 case、**同 `--timeout`**
   （连接残留会让"修复"看起来像没生效，见 qsh-debug §2.3）
3. **timeout 不一致 = 不可对比**（KNOWN_ISSUES #63）：`--timeout` 决定一轮能跑多少项 ——
   短 timeout 把尾部项目按超时截断成 `NOT_RUN`，拉长后**潜伏缺陷浮出水面**产生"新 FAIL"。
   差异表须把"本次/上次 timeout"并列，区分两类：**同 timeout 下 PASS→FAIL = 真回归**；
   **上次 NOT_RUN → 本次 FAIL = 超时掩盖，不算回归**（mbedtls 短 timeout 曾截断 11~17 项，
   `sm4_bad_params` 即拉长后才首次执行到）。
4. 差异项先看 reason 再下结论；单次 FAIL 先重跑排除瞬态
   （GDB 证据与串口日志矛盾时先重跑，排除 debug server 瞬态）
5. 跨会话对比直接读 `reports/<date>-<idx>/<project>_<case>/<driver>_test_report.json`
   （含 INFO 实际值，可对比数值漂移，如吞吐/占比随探针/时钟配置变化）；
   会话目录行粒度=case，`summary.md` 快速对全量差异（§12.4）

## 6. 问题定位辅助（token 之外的信号）

dbg_run.py SUMMARY 里过滤的关键行（驱动/固件汇总）：
- `SERRWARN` / `URUN` → 硬件 NACK/欠载，总线上没应答
- `write_len 0` → target 没收到写数据（协议/时序问题）
- `read actual N != 32` → controller 读长度不符
- `ready IBI timeout` → 就绪握手超时，target 中断/状态未就绪
- 测试 ~6.5s 停摆无 SUMMARY/DONE → 目标侧死窗或停摆，查 8ms 死窗文档
  （`tests/ZTEST_PANIC_LOG_8MS_DEADWINDOW.md`）

## 7. 坑沉淀要求

对比分析中发现的新模式（如新假 PASS 形式），往 `.claude/gotchas/INBOX.md`
追加一条；沉淀到 §2 + KNOWN_ISSUES 归 keeper。

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| 分析完成 | ★ 坑沉淀决策 | 自动转（见 skill-orchestrator §2 坑沉淀决策表） |
| 发现假 PASS 新模式 | qsh-test-compare §2 + KNOWN_ISSUES | 新模式 |
| 发现领域可复用调试法 | debug-knowledge §写入 | 非一次性、非纯环境坑 |

## 链路
> **输入**← `qsh-test-run` · **相关**→ `embedded-debug` · skill-orchestrator
