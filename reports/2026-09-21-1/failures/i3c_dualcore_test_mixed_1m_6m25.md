# i3c_dualcore — case_mixed_1m_6m25 失败分析

| 项 | 值 |
|---|---|
| 工程 | i3c_dualcore_test |
| case | `case_mixed_1m_6m25` |
| 被测驱动 | `zephyr/drivers/i3c/i3c_linkedsemi.c`（controller，cpu2）+ `i3c_target_linkedsemi.c`（target，cpu1） |
| 报告 | **无独立报告**，只有 `d:\zephyr4_0\810_driver_test\reports\2026-09-21-1\logs\i3c_dualcore_test_case_mixed_1m_6m25.log` |
| 结果 | FAIL **未知**（`summary.md:19` 计数列为「未知」） |
| 执行标记 | 两轮 `pass-1 (prime, I-cache flush)` 均未进入 `pass-2 (capture)`；日志内**无** `finished in ... (rc=...)` 行 |
| 内存兜底 | 未采集到（该 case 未产出 per-case 目录，无 `dump_results.py` 输出可查） |

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| <无测试项采集到> | — | 35（`tests/COVERAGE.md` 末尾「**项数**：本表对应 `I3C_TEST_COUNT = 35` 项」） | 0（未采集到） | `pass-1 (prime)` 阶段 20s 内无任何固件输出，**两轮均如此** ⇒ 一个测试项都没判定出来；非"测得 0 项"，而是"该 case 根本没留下可信计数" | `logs/i3c_dualcore_test_case_mixed_1m_6m25.log:2849`（pass-1）→ `:2850`（RETRY）→ `:2853`（第二轮 pass-1）→ `:2854`（ERROR） |

## 归因

### 1. 失败发生在哪一步

**build 成功；失败在 load/prime 阶段；capture 从未开始。**

- build：日志开头即构建段，两个核都构建了 ——
  `:7  [case_mixed_1m_6m25] Build cpu0_i3c_controller -> .../build/controller`
  `:1440 [case_mixed_1m_6m25] Build cpu1_i3c_target -> .../build/target`
  并以成功收尾：
  ```
  2846  [BUILD] case_mixed_1m_6m25 OK (329.0s)
  ```
  全日志 `error:` 计数 = **0**；`warning:` = 31 条，全部是 `i3c_controller_test.c` 的 `format '%lu' expects ... 'unsigned int' [-Wformat=]`（如 `:1135`、`:1160`）⇒ **构建不是失败点**。
- prime（失败点）：
  ```
  2848  [RUN] case_mixed_1m_6m25
  2849  [RUN] case_mixed_1m_6m25 pass-1 (prime, I-cache flush)
  2850  [RETRY] case_mixed_1m_6m25: …失败，case_mixed_1m_6m25: pass-1 采集：20s 内无任何固件输出…
  ```
  注意 `:2849` 之后**没有任何**中间行 —— 既无 `[ENV] 1025 LISTENING`、也无任何 `[t+Ns]` 采样行 —— 直接跳到 `:2850` 的 RETRY。
- 重试后仍同点失败：
  ```
  2851  [ENV] restarting DebugServer (residue)
  2852  [RUN] case_mixed_1m_6m25
  2853  [RUN] case_mixed_1m_6m25 pass-1 (prime, I-cache flush)
  2854  ERROR: case_mixed_1m_6m25: pass-1 采集：……测试环境有问题（DebugServer 残留/烧录链路/镜像陈旧）
  ```
  第二轮**连 `[t+` 采样行都没有**。
- capture 未开始：全日志 **没有** `pass-2 (capture)` 行（对比同批通过例 `logs/i3c_dualcore_test_case_pure_i3c_12m5.log:2854` 有 `[RUN] case_pure_i3c_12m5 pass-2 (capture)`）。
- 本 case 日志内 **无 `load_fail` 字样**（`grep -c load_fail` = 0）⇒ 与下面 `mixed_400k_12m5` 的 load_fail 形态不同，本轮是「烧录/prime 采集静默」。

### 2. 有没有出现 `ZT_BOOT` / `i3c` / token 字样

**有字面出现，但全部不是固件真实输出。** 逐类列出：

| 字样 | 出现位置 | 性质 |
|---|---|---|
| `ZT_BOOT` | `:2850`、`:2854` | 仅出现在 **报错文案内部**（"20s 内无任何固件输出（ZT_BOOT/ztest 行/I3C_TEST: token…）"）。这是**判据的名称**，不是观测到的 token。 |
| `I3C_TEST:` | `:1160`、`:1163`、`:1166`、`:1170`、`:1173`、`:1176`、`:1180`、`:1183`、`:1186`、`:1190` … | 全部出现在 **编译告警的源码行引用**里，形如 `906 \| WRITE_STOP_READ_TEST(I3C_TEST_WRITE_STOP_READ_1, write_stop_read_1, 1)`，即宏名 `I3C_TEST_WRITE_STOP_READ_*` 的**源码文本**，不是运行期 token 行。 |
| `i3c`（不分大小写） | `:7`、`:11`、`:22`-`:24`、`:33`-`:42`、`:1133`-`…` | 全部是**构建路径 / 应用目录名 / 源文件名**（如 `case_mixed_1m_6m25/build/controller`、`cpu0_i3c_controller/src/i3c_controller_test.c`）。 |

⇒ **没有任何真实 token 行**（无 `I3C_TEST:CTRL:PASS:…` / `I3C_TEST:TGT:…`），也无 `ZT_BOOT` 启动横幅。这一结论与 `:2854` 的 ERROR 文案自述一致（"ZT_BOOT/ztest 行/I3C_TEST: token；test.sh/gdb 也没产出任何行"）。

### 3. 有没有出现 `ctrl=` / `tgt=` 的字节计数

**没有，一个都没有。** 本 case 连本组唯一的原始信号也未采集到：

- `grep -c 'ctrl=' <log>` = **0**
- `grep -c 'tgt=' <log>` = **0**
- `grep -c '\[t+' <log>` = **0**；`grep -c 'boot=' <log>` = **0**；`grep -c 'idle=' <log>` = **0**

⇒ 20s 监控窗口**整段采样行缺失**（既无 `[ENV] 1025 LISTENING pid=…`，也无 `[t+0s]…[t+18s] boot=… ctrl=… tgt=…` 那 7 行）。因此**"ctrl 计数非 0"这一判断对本 case 不适用** —— 不是"有字节但内容不明"，而是**字节计数本身未落盘**。

⚠️ 这与同批 `mixed_400k_12m5` 的记录形态**不同**（那边有 `[ENV] LISTENING` + 7 行 `[t+…]` 且 `ctrl=12`）。原因无从查证：失败轮不落盘原始串口字节，日志里也没有解释为何本 case 缺失采样段。

### 4. 其他可查事实

- 固件版本串（构建横幅，非运行输出）：`openbmc-zephyr-v0.1.0-827-g5f7a7de96f39`，controller 见 `:1089`、target 见 `:2506`。
- 串口映射正常：`:2 Controller UART: COM18`、`:3 Target UART: COM16`、`:4 Case timeout: 120s`，与本批其它 case 一致 ⇒ 静态映射不是失败点。

## 备注

- **失败性质**：环境/连接层失败，**不是**驱动功能失败 —— 连 ztest 都没跑到第一条用例。日志自述即为「测试环境有问题（DebugServer 残留/烧录链路/镜像陈旧）」（`:2854`）。
- **不是"用例未验证"**：`OPEN_ITEMS.md` 的 **O-24** 条目已登记本次回归，并给出历史反证（`logs/full_regression_2026-09-16.log` 7 个 i3c_dualcore 全 `rc=0`；`regress_2026-09-17_p1.log` 6/7 `rc=0`；`tmp/run_i3c_6m25_rA/rB.log`（09-18）两次 PASS）⇒ 是**回归**。（本文件不对 O-24 作任何修改。）
- **证据缺口（建议补）**：失败轮不落盘原始串口字节、也不落盘 `[t+…]` 采样段 ⇒ 无法回答"那 12 字节是什么"这类问题（`mixed_400k_12m5` 就卡在这里）。O-24 已建议「失败轮也落盘原始串口字节」。
- 本文件只读产出：未上板、未构建、未改动任何既有文件。
