---
name: qsh-test-compare
description: QSH 测试数据对比 skill：ztest 原生输出解析（parse_ztest）、PASS/FAIL 判定（同名测试项双核双 PASS + 两核 PROJECT EXECUTION SUCCESSFUL；i3c_dualcore 例外 = CTRL SUCCESSFUL 且 TGT 无该行属设计）、Markdown/JSON 报告生成（会话目录 reports/<date>-<idx>/）、多次回归结果差异对比、假 PASS 识别、串口不可用时 GDB 读内存取结果（dump_results.py）。分析测试输出、写报告、对比回归结果、串口无输出取结果时调用。
---

# QSH 测试数据对比与分析

结果解析、判定、报告、回归对比。**串口 ztest 原生输出是唯一可信源**
（`SPEC_REFERENCE.md` §6.2.1），肉眼扫串口不可靠。采集流程见 `qsh-test-run`，连接问题
见 `qsh-debug`。

## 1. ztest 原生输出解析规则

输入：dbg_run.py / run_test.py 采集的原始行。**解析用公共模块
`common/ztest_report.py` 的 `parse_ztest`**（勿自写解析器），返回
`{"boot_seen", "role", "tests": {name: {result, reason}}, "infos", "execution"}`。
正则要点（与解析器一致）：
1. **剥离 ANSI**：`re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")` 先替换掉
   （shell 提示符重绘 `\x1b[8D\x1b[J` 会污染行首）。**⚠ 转义在行中间被截断会吞掉证据首字母**
   （★ 2026-09-20）：主核 `printk` 与 log 线程是**两个独立写入方**（单核 SoC 连
   `CONFIG_PRINTK_SYNC` 都不开）⇒ 转义序列可被另一方的整行插入切开（实测
   `[00:00:00.005,000] \x1b[1;3` + 插入行 + `1m…`）⇒ 上面正则的 `[A-Za-z]` 收尾把 token
   首字母（如 `I3C_I2C_TEST` 的 `I`）当转义末字节吃掉 ⇒ 该行认不出 ⇒ 项降级成 `mem` 证据
   （后果见 §2"批量必须靠 token"）。修在消费者唯一收口 `common/ztest_report.py::strip_line`
   （仅当"原始行有锚、严格剥离后却没了"才退回只剥 SGR，纯增益）；**根因侧建议
   `CONFIG_LOG_BACKEND_SHOW_COLOR=n`**（去转义即去放大器）。见 `SPEC_REFERENCE` §6.11.2。
2. **去行首噪声**：输出行前可能有 `uart:~$ ` 前缀 → 用 `re.search()`，
   不用 `^` 锚点 / `.match()`
3. 匹配的协议行（`SPEC_REFERENCE.md` §6.2.1）：
   `ZT_BOOT (\S+)`、`(PASS|FAIL|SKIP) - (\S+) in [\d.]+ seconds`、
   `INFO (\S+):\s*(.*)`（k=v 逗号切分）、
   `PROJECT EXECUTION (SUCCESSFUL|FAILED)`；
   reason 归因：zassert 的 `Assertion failed at ...` 打印在 FAIL 行**之前**，
   解析器暂存断言行归到该项（`SPEC_REFERENCE.md` §6.5.5）

4. **INFO 约定行**（`SPEC_REFERENCE.md` §6.5.4，数值实测上报）：测试项的 PASS/FAIL 之前
   可能先出 `INFO <name>: <k>=<v>[,<k>=<v>]`（如
   `INFO monobit_8k: ones=32931, percent=50.25`），解析后填入报告"实际"列，
   与 COVERAGE.md 的预期对比。

5. **日志可见性：i3c（`style="d3"`）不回声 `test.sh` 的 stdout（★ 2026-09-21，G-O24-3）**：
   `common/acquire.py::_spawn_test_sh` 只有 `style="z"`（`single_cap` 9 工程）**逐行回声**
   `test.sh` 的 stdout；`style="d3"`（i3c 双核）**不回声**（`proc_lines` 只进内存列表，仅在
   `load_fail` 诊断尾部才打印）⇒ i3c 的 `logs/serial_*.log` 里**没有** `Loading images into RAM via GDB...`
   之类加载横幅（新旧日志都没有）。**判据**：要"从日志核 shell 侧加载段的顺序/行为"（如横幅是否在新鲜度
   检查之前），**选 `single_cap` 工程**；核 i3c 加载段只能靠"有没有 20s 静默被判 `load_fail`"间接推断
   （或临时加回声）。执行侧见 `qsh-test-run` §4.2。

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
该工程 target `test_main` 不返回（`SPEC_REFERENCE.md` §6.1.1 dispatcher join）→ TGT 端口
**永远没有** `PROJECT EXECUTION` 行，`tgt_done=None` 是设计（Task 9 D1），
不判 FAIL；完成判定 = **CTRL SUCCESSFUL 且 TGT execution != "FAILED"**，
TGT 失败经逐项 FAIL 行暴露。i2c_pure/i3c_i2c_switch 的 target 正常返回，
仍要求两核各自 SUCCESSFUL。

**假 PASS 识别**（实测发生过）：
- target 只校验 `last_read_len` 而 controller 实际读到 0 → target 假 PASS
  （write_read_×4 场景）。判定时 controller 侧 FAIL 了就不算过，即使 target PASS。
- **零证据不得判 PASS**（★ 2026-09-18）：`items`/`tests` 为空（0/0，一个项都没捕到）
  ⇒ FAIL，不是"全过"也不是"跳过"（`all([]) == True` 陷阱）。双核**某一核整体**
  无确定结果（非 PASS 非 FAIL：串口没捕到且内存兜底也无值）⇒ 也不得因另一核完好判 PASS；
  **部分**项缺失仍按各工程原容忍语义（i3c 的 `incomplete`）。
- **批量必须靠 token（★ 2026-09-20 用户裁定）**：**整个 case 一条串口逐项 token 都没有**
  （两核皆零，整份证据来自 mem 兜底）⇒ **FAIL** + 理由串「证据弱（仅内存兜底，非串口逐项）」
  （`MEM_ONLY_NOTE`；判定语义以 `SPEC_REFERENCE` §6.2.4 **G9/D6** 为准——本节只是速查）。
  判据必须取**逐核**来源（`evaluate_case` 的 `ev_srcs`），**不得**用报告 `src` 列 —— `src` 是
  **跨核聚合**（任一核用 mem 就把该项标 mem），按聚合列判会误伤 R3 有意保留的合法形态
  「某一核整核丢行 + 另一核 token 完好 + §6.4.3 兜底补回」（`tgt_all_missing_mem_ok`，仍判 PASS）。
  **部分项** mem 兜底不受影响（§6.4.3 仍是正式机制）；**调试路径（`dbg_run.py`）不在守卫内**，
  其 mem 兜底行为逐字不变。
- **`execution=TRUNCATED_AFTER_ALL_ITEMS`**：窗口到点但**全项都已拿到确定结果** ⇒ PASS
  （窗口到点本身不是失败）；**有项无证据仍是 FAIL**（`mbox_test` 是只认标记的参考口径）。
  **`NOT_RUN` 不算确定结果**（mem 明确读到"该项没跑"）——「前 N 项 PASS + 其余 NOT_RUN +
  无标记」= 套件中途挂死，**不得放宽**；**`SKIP` 是主动跳过 = 确定结果，不受此限**。
- **`mem_state` 三态**：`no_sentinel_symbol`（旧构建 ⇒ 要求重建）/ `invalid`（未跑到
  `tr_init` ⇒ 查固件）/ `ok`。报告显示"无效"而不是"没跑"= fail-closed 生效，不是漏测。
- **`mem_state==ok` 时 `note` 在 `.md`/控制台不可见（★ 2026-09-18 缺陷）**：
  `common/judge.py:116-121` 的 `mem_state_line()` 在 `ok` 分支**直接丢弃 `note`** ⇒ 而
  "`ok` **但本轮无 `ZT_BOOT`** ⇒ mem 项被门禁拒绝（§6.2.4 E5/E6）"**恰好落在 `ok` 分支**，
  报告只剩"内存兜底: 有效"，读者**看不到"结果被拒绝"**（该串只进 JSON `mem_note`）。
  实测 `eth_test case_mdio` 拔线等效场景：JSON 有 `mem_note=…被拒绝…`，`.md`/控制台
  `grep -c 被拒绝 = 0`。⇒ **核验 mem 是否真被采纳/拒绝，必须以 JSON `mem_note` 为准**，
  不能只看 `.md`（KNOWN_ISSUES #95）。
  **处置（2026-09-19）**：`common/judge.py:127` 已修（`ok` 且 note 非空照常拼接），`.md`/`.json`
  已可见。**残留**：`projects/eth_test/tests/run_test.py` 的控制台 `print_report()`
  只打印 `mem={mem_state}`、**不经** `mem_state_line()` ⇒ **eth_test 控制台仍看不到被拒诊断**
  （其余 14 个工程已生效）（KNOWN_ISSUES #95 残留缺口）。
- 汇总表 `NOT_RUN` 项 = "该项从未执行"、**无确定结果**（区别于 `SKIP` 的主动跳过）：target 没收到对应命令，需整项重测；但 F4 族该情形**本身即判 case FAIL**（§6.2.4 F2/F5），重测只为定位根因。
- **同一核流出现相反完成标记（★ 2026-09-19，根因已定论）**：`PROJECT EXECUTION FAILED` 与
  `SUCCESSFUL` **并存**时 `parse_ztest` **取最后一条** ⇒ 可能把 `ctrl_failed=1` 的轮次读成 SUCCESSFUL。
  根因 = **完成标记有双来源**：app 自打 `printk("PROJECT EXECUTION …")` + ztest **强 `main()`**
  （`ztest.c:1458`）的 `end_report()`；固件不调 `ztest_run_all` 时后者不读 stats、**恒打
  SUCCESSFUL（空跑）** —— 实测 `i3c_i2c_switch_test case_switch_core`（KNOWN_ISSUES #98 前半）。
  ⇒ **不得只信标记**，必须交叉 GDB 读 RAM 记账（`ctrl_failed` / 失败槽 / reason）后再判。
  **命令通道型固件（注册 0 个 ztest 测试）**尤其如此：`test_status` 恒 0 ⇒ 第二条**无条件**
  `SUCCESSFUL`（产生点 `ztest.c:76-82` `end_report()`），会给全 PASS 的核也制造无谓矛盾噪声；
  真 FAIL 时靠 **R6 fail-closed** 判 `exec_conflict`（KNOWN_ISSUES #98 前半 / G-HW5-3）。
- **破坏性 / 跨复位 case 的判定不得依赖"窗口内看到重启"（★ 2026-09-20）**：这类 case 的真相在
  **跨复位 `__noinit` 状态**（如 `test_phase == 2`），复位延迟**远大于**驱动标称超时且不确定
  （sec_iwdg 标称 2s、实测可达 ~34s）⇒ 采集窗内"无输出"**不能**当"没复位"的证据。判定取
  `__noinit` 终态（或窗口 ≥60s），别把窗口到点当失败（与"窗口到点不是失败"同源，KNOWN_ISSUES #100）。
- **token 拼写有两类，核证据时不能只数 ztest 正则（★ 2026-09-21）**：`SPEC_REFERENCE` §6.9.1 ——
  **D1** = ztest 原生 `PASS - <name> in X.XXX seconds`；**D2** = 自定前缀
  `<PREFIX>:<core>:(PASS|FAIL|SKIP):<name>`（`I3C_I2C_TEST` / `MBOX_TEST` / `RTC_TEST`，登记在
  `common/ztest_report.py::NATIVE_PREFIXES`；ztest 型从核经 RLOG 中继、剥壳后才与 D1 逐行等价）。
  ⇒ 核"分拣是否到位"**必须按该工程自己的文法**数 token：拿 ztest 正则数 D2 工程的 TGT 会得 `zt=0`，
  被**误判成"分拣失败"**（本轮 TGT token 实测完整、险些误判）。判据 = 先看
  `ZT_BOOT <core>` / `<PREFIX>:<core>:BOOT` 是否存在，再分类计数。
- **独立预期交叉校验（★ 2026-09-21）**：报告判 PASS **≠** 判定正确。用 `common/expect_check.py` 把归档
  实测 INFO 值拿去核该工程 `tests/COVERAGE.md` 的「独立预期」断言 —— 出现 **`DISAGREE` + 固件判 `PASS`
  = 假 PASS 风险，必须停下报告**（`--case <工程>:<case>` / `--audit`；文法/判定/两条纪律见 `SPEC_REFERENCE.md` §6.5.6）。
- **传输成功（`ret=0`）≠ 数据有效（★ 2026-09-22）**：`i2c_read/transfer` 返回 `0` 只说明**本层传输完成**，
  不证明数据来自目标器件 —— **单点 ACK 不证明器件在指定链路上**（应答者可能是**同址邻居**、别的链路
  或中间器件）。判"器件真应答/数据可信"必须核**数据内容**（并与本级锁存寄存器交叉）；只断言 `ret`
  的用例会把"被拦"读成 PASS。
  ⚠️ **2026-09-22 更正**：原"被 I2C 滤波器拦截的地址**照常 ACK**（`ret=0`）"及"断言'被拦 ⇒ `ret<0`'
  必然 FAIL"取自**同址冲突**（DUT 与板载器件撞址 `0x50`）的观测 ⇒ **作废、待非撞址复验**
  （换址后被拦地址实测 `-EIO`）；重验前**勿放宽** `ret<0` 类判据（归属待驱动侧裁定）
  —— KNOWN_ISSUES #110/#111（更正段）· debug-knowledge mcu 域 `addr-ack-not-device-evidence.md` /
  `bus-addr-collision-pass-not-attributable.md`。
- **各族 × 各状态的完整判定（冻结总表）见 `SPEC_REFERENCE` §6.2.4**——本节是分析视角速查，判定语义以总表为准（表外行为视为未定义）。

## 3. 串口不可用时的兜底通道：GDB 读内存取结果（dump_results.py）

> **★ 2026-09-22 用户裁定：批量路径已不走本通道** —— `run_all.py` / `run_test.py` 稳定优先、
> 只认串口 token（`SPEC_REFERENCE.md` §6.4.10）。本节保留为**调试 / 人工**手段（`dbg_run.py`
> 与手工读结果）；下文"回归脚本用它做兜底合并"的描述**不再适用于批量**（判定侧 mem 早已不算
> 证据，见 §2「批量必须靠 token」+ R8）。

固件把每项测试结果记账到公共模块 `common/test_results.{h,c}` 的全局数组
（`SPEC_REFERENCE.md` §6.1.5：`int8_t test_results[]` -1/0/1 + `char test_reasons[][96]`），
**链接地址=加载地址**，连 DebugServer 直接读内存即可，不依赖串口。

```bash
python dump_results.py --case case_pure_i3c_12m5               # 只读当前内存
python dump_results.py --case case_pure_i3c_12m5 --json        # JSON 输出（回归兜底用，`SPEC_REFERENCE.md` §6.4.3）
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
- **`x/s` 读空 ≠ 没写**（★ 2026-09-19）：`test_reasons` 是**字节数组**，`x/s` 遇首字节 `0` 即停
  ⇒ 读到空串不代表没写过；必须看**原始字节**（`x/<n>db`）再下结论。
- **★ 但"读空"更常见的成因是「读了一个没跑过的核」（2026-09-21 更正，KNOWN_ISSUES #98 后半）**：
  在 cpu2 **没跑过**的会话里读它的 `.bss`，读到的就是**全 0（`.bss` 初值）或上一轮残留**。
  `case_switch_core` 的 `test_reasons[10]` 原始字节 `00×8` + `"hole_should_nack"` 一度被判为
  "写出后被部分/整体清零"；拿到 cpu2 调试手段（`qsh-debug` §6「cpu2 通道」）后复测**不可复现** ——
  逐字节 = `rstact_whole_should_nack`、无第三方清零 ⇒ **改判为观测口径可疑（读到了陈旧/未运行的
  cpu2 RAM）**。**纪律：读对侧核（cpu2）RAM 取结果前，先确认该核本轮真的跑过**（CTRL 串口有流量 /
  PC 不在 `__start`），否则"没跑"会被读成"被抹"。**碰过 1026 端口后尤其如此**（该访问本身把
  cpu2 留在 halted，见 `qsh-debug` §2.3 / §6；让 cpu2 真跑起来 / 1026 出现的**配方**（重启 DebugServer，软件动作、非物理操作，可自动化）亦见该 §6）。
- **例外：`i2c_pure_test` 串口窗口内拿不到测试启动后的逐项输出** —— 根因已定位 =
  `dbg_run.py` capture 轮 gdb stdout 管道未排空（**采集工具缺陷**，该工程已修）；**未修的工程**
  仍以内存兜底为准、串口判 PASS/FAIL 不可用（KNOWN_ISSUES #97 / qsh-test-run §3）。
- 记账已自动化（glue 按 ztest stats 批量写入，`SPEC_REFERENCE.md` §6.1.5），**TEST_NAMES 必须按
  ztest_unit_test 链接序**（SORT_BY_NAME，`KNOWN_ISSUES.md` #27）与记账 idx 对齐——
  用 `nm --numeric-sort <elf> | grep _ztest_unit_test__` 核对，勿按源码序/plain
  nm 名字典序（尾下划线坑，数字在 '_' 之前）。
- `--json` 输出结构：`{"CTRL": {"<name>": "PASS|FAIL|NOT_RUN", ...},
  "TGT": {...}, "reasons": {"<name>": "..."}}`（单核工程只有 TGT）。
  回归脚本用它做**兜底合并**：串口缺失的项用内存值补，来源标 `mem`
  （`SPEC_REFERENCE.md` §6.4.3，规则见下）。

## 4. 报告生成（★ 归档到会话目录 reports/，`SPEC_REFERENCE.md` §6.4）

回归报告由 run_test.py 自动生成并归档（批量 = run_all.py，`SPEC_REFERENCE.md` §6.4.5），
agent 手工补报告时按同样规范：

> **实现落点（2026-09-20 分层后）**：判定/渲染由公共 **L2 `common/process.py`** 产出，工程唯一接入点 =
> 模块级 `l2_spec()`（`ProcessSpec` 五钩子）；**本节是判定/渲染口径，实现在公共层**。改渲染/判定
> **不必上板** —— `python regression/replay.py --l2-direct` 离线重放即可（`FRAMEWORK_MAP.md` §1-§2 /
> `SPEC_REFERENCE.md` §6.4.8 / §6.4.10）。
>
> **★ 离线重放必加 `--l2-direct`（2026-09-22 实测）**：不加则走工程 `main()`（含 DebugServer
> 前置检查）⇒ **不是离线、也不产出报告** —— DS 未起时每 case 白等自愈超时（实测 51.7s），
> 表现为「**跑很久、`tmp/replay/<会话>/` 是空的**」。机制见 `SPEC_REFERENCE.md` §6.4.8。

- **归档位置（会话目录）**：
  `810_driver_test/reports/<YYYY-MM-DD>-<idx>/<project>_<case>/<driver>_test_report.{md,json}`
  ——分配/写入一律走公共 `common/report_dir.py`（`allocate_session_dir` /
  `case_dir_for` / `update_summary`），禁止手工/脚本自算路径；一次批量会话
  内全部 case 共用同一会话目录（i3c 7 case 各自独立子文件夹）；单跑 case
  不带 `--session-dir` 自动分配新会话目录
- **Markdown 模板**（`SPEC_REFERENCE.md` §6.4.2）：表头含 `预期 | 实际(INFO) | 结果 | 来源 | reason`；
  **预期**列来自该工程 COVERAGE.md，**实际**列来自 INFO 约定行（`INFO <name>:
  k=v[,k=v]`），**来源**列 `token`（串口 ztest 输出）/`mem`（内存兜底）；
  双核工程的 `src` 是**跨核聚合**（任一核 mem ⇒ 该项 mem），**不得**据此判 case 级证据强度
  （case 级判据取逐核来源，见 §2 的「批量必须靠 token」条）
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
- **summary.md 索引**（`SPEC_REFERENCE.md` §6.4.4）：只在会话目录最外层，**行粒度 = case**
  （key `<project>_<case>`），行格式 `| [<key>](<报告相对路径>) | <PASS|FAIL>
  | <passed>/<total> |`；按 key **替换**自己的行，不重复追加
  - **核"项数"以 `summary.md` 为准，不复数 `report.json`（★ 2026-09-21）**：`passed/total` 的分母由各
    `run_test.py` 按**工程语义**写（SKIP / observe / guard / `run::timeout` 各有处理）—— 如 `spi_test`
    18/19 = 26 项里 7 项 SKIP 不计分母，直接数 json 会得 18/26；`mbox_invalid` 7/7 = 12 项里 3 项观测项不计、
    `app_iwdg` 18/19 含 1 项 `run::timeout`。`report.json` 只用于取**失败项名与 reason**。

### 4.1 失败归因报告 `failures/`（★ 2026-09-21 起**固定动作**）

**每轮回归跑完，凡有 FAIL case，必须在会话目录下产出 `failures/`，一个失败 case 一个文件。**
（用户 2026-09-21 裁定；不是可选项。）

- **第一步：跑生成器（零判断、纯机械）**
  ```bash
  python 810_driver_test/common/failures_report.py --session reports/<会话>
  ```
  从 `summary.md` + 各 case 报告抽取已有信息（工程 / case / 执行标记 / 内存兜底 / 结果 /
  失败项名 / 来源 / 期望 / 实际 / 失败原因），生成 `failures/<driver>_<case>.md` 骨架。
  **默认不覆盖已存在文件**（agent 可能已填过），`--force` 才重写。单测：`python common/test_failures_report.py`。
  —— 确定性的活不重复让 LLM 做：骨架机械生成，agent 只做增量，成本降一个量级。
- **命名**：**驱动名打头** —— `dma_test`+`case_default` → `dma_test_default.md`；
  `i3c_dualcore_test`+`case_mixed_1m_6m25` → `i3c_dualcore_test_mixed_1m_6m25.md`。
- **agent 只补 `⚠️` 标记处**，两件事：① 报告里为 `-` 的**实测值**（去 `serial_CTRL.log` /
  `serial_TGT.log` 挖原文）；② **归因段**（驱动 / 用例 / 环境 / **无法判定**）。
- **取数纪律（最重要）**：任何"实际值"必须有 `文件:行号` 证据；**取不到就写「未采集到」，绝不臆造**。
  报告「实测(INFO)」列大量是 `-` —— 这里最容易顺手编数据。
- **两个特别提醒**：
  - `FAIL 0/0` / `未知` 的 case **没有失败项可列** ⇒ 先判"失败发生在 build / load / prime /
    capture 哪一步"，再判环境还是用例。**优先看报告头部的「内存兜底」字段**：写「哨兵未置位 /
    invalid」⇒ 固件没跑到 `tr_init`（挂在早期初始化），这本身就是一条定位线索，别只盯串口 0 行。
  - `执行标记` 不是 `SUCCESSFUL` 时，**可能是采集截断而非真实断言失败**，必须在归因里点明 ——
    否则会把"没测到"写成"测出来是错的"。

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
   **同族（★ 2026-09-21）**：**框架级改动**（日志文法统一 / 单出口 / `LOG_INF` 路径）改的虽是"输出通道"，
   却会改变**跨核 dispatcher 时延分布** ⇒ 时序敏感用例（用固定 `k_sleep` 抢窗口，如
   `broadcast_nack_recovery` 的 `k_sleep(6ms)`）的 **PASS→FAIL 不得直接记成驱动回归**；框架改动后该类
   用例必须重测，驱动改动后同理。判据补强（本轮）：失败**只在首帧**、后续步骤（r2/r3、EVR BUSY）全好
   ⇒ "窗口错过"成立、**恢复路径完好**（KNOWN_ISSUES #98 / #51）。
4. 差异项先看 reason 再下结论；单次 FAIL 先重跑排除瞬态
   （GDB 证据与串口日志矛盾时先重跑，排除 debug server 瞬态）
5. 跨会话对比直接读 `reports/<date>-<idx>/<project>_<case>/<driver>_test_report.json`
   （含 INFO 实际值，可对比数值漂移，如吞吐/占比随探针/时钟配置变化）；
   会话目录行粒度=case，`summary.md` 快速对全量差异（`SPEC_REFERENCE.md` §6.4.4）
6. **归因前先确认所引报告项真的跑过**（★ 2026-09-19，KNOWN_ISSUES #96）：历史报告里
   "看起来有结果"的项，可能来自**从未注册执行的套件**（有 `ZTEST` 无同名 `ZTEST_SUITE`
   ⇒ 用例被编译但永不运行，报告里却因 `TEST_NAMES` 列着而"像有结果"）。
   ⇒ 用报告项做推断前，先核对它在**该轮报告**真的产生过结果；`nm` 见
   `_ztest_unit_test_list` 符号只证明"链进去了"，**不证明"会跑"**。判据见
   qsh-test-validate §1.10s。
7. **归因必须钉到报告对应的源码版本**（★ 2026-09-19）：拿历史报告归因前，先比对该工程
   **源码/ELF 的 mtime 与报告生成时间**——报告对应的是**当时**的源码，不是现在的。
   源码其后被改（或 ELF 被重建）而未复测时，要么复测，要么在结论里显式标注
   "归因基于旧版本、当前版本未复测"。与 `--skip-build` 的产物新鲜度同族
   （哨兵符号判据见 KNOWN_ISSUES #88）；本条多发于**跨会话/跨批次**，与并发写域的
   "源文件漂移"同族但窗口更长。
   **★ 2026-09-20 深化（G-FAMFILL-1）**：哨兵门**只验"符号存在"、不验"源码新鲜度"** —— ELF 含
   `test_results_valid` 但 build 目录 mtime 早于源码时，`--skip-build` **照旧放行**、烧陈旧镜像
   ⇒ **假 FAIL**（`-fipa-icf` 把函数折叠到有断言的共享体）或**假 PASS**（折叠到无断言的共享体，
   更危险）。实测两边 `.config` md5 逐字节相同 ⇒ **与构建路径/优化级别无关，唯一变量是 ELF 陈旧**。
   判据 = 比对 build 目录 mtime 与相关源 mtime（KNOWN_ISSUES #88 姊妹形态）。

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
