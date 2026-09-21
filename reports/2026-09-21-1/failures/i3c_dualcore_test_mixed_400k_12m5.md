# i3c_dualcore — case_mixed_400k_12m5 失败分析

| 项 | 值 |
|---|---|
| 工程 | i3c_dualcore_test |
| case | `case_mixed_400k_12m5` |
| 被测驱动 | `zephyr/drivers/i3c/i3c_linkedsemi.c`（controller，cpu2）+ `i3c_target_linkedsemi.c`（target，cpu1） |
| 报告 | **无独立报告**，只有 `d:\zephyr4_0\810_driver_test\reports\2026-09-21-1\logs\i3c_dualcore_test_case_mixed_400k_12m5.log`（全文仅 26 行） |
| 结果 | FAIL **未知**（`summary.md:20` 计数列为「未知」） |
| 执行标记 | `[RUN] case_mixed_400k_12m5 finished in 21.8s (rc=1, load_fail)`（`:20`）；两轮均未进入有效 `pass-2 (capture)` |
| 内存兜底 | 未采集到（该 case 未产出 per-case 目录，无 `dump_results.py` 输出可查） |

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| <无测试项采集到> | — | 35（`tests/COVERAGE.md` 末尾「**项数**：本表对应 `I3C_TEST_COUNT = 35` 项」） | 0（未采集到） | 第一轮 gdb 两次尝试均失败、`rc=1 load_fail`；重启 DebugServer 后第二轮 `pass-1` 仍 20s 无任何固件输出 ⇒ 一个测试项都没判定出来 | 第一轮 `logs/i3c_dualcore_test_case_mixed_400k_12m5.log:6-20`；第二轮 `:23-25` |

## 归因

### 1. 失败发生在哪一步

**本轮日志内没有构建段（无 `[BUILD]` 行）；失败在 load 阶段（第一轮）与 prime 阶段（第二轮）。capture 从未产出。**

- 第一轮（含 load 失败）：
  ```
  6   [RUN] case_mixed_400k_12m5
  7   [RUN] case_mixed_400k_12m5 pass-1 (prime, I-cache flush)
  8   [RUN] case_mixed_400k_12m5 pass-2 (capture)
  9   [ENV] 1025 LISTENING pid=12348 DebugServerConsole.exe       12348 Console
  10    [t+0s] boot=False ctrl=12 tgt=0 idle=0.0s
  ...
  16    [t+18s] boot=False ctrl=12 tgt=0 idle=18.1s
  17  [RUN] case_mixed_400k_12m5 连续 20s 无任何 gdb/固件输出——环境问题（DebugServer 残留/烧录链路/镜像陈旧），非测试失败
  18  [RUN] gdb 输出尾部（诊断）：
  19      （此行只有空白，无内容）
  20  [RUN] case_mixed_400k_12m5 finished in 21.8s (rc=1, load_fail)
  ```
  `:18` 明确标注了要输出「gdb 输出尾部（诊断）」，**而 `:19` 是空白**⇒ 连 gdb 侧也没有任何诊断内容。
- 重试（第一轮被判 load_fail 后）：
  ```
  21  [RETRY] case_mixed_400k_12m5: load_fail（环境问题）→ 重启 DebugServer 后重试
  22  [ENV] restarting DebugServer (residue)
  ```
- 第二轮（prime 失败，且**连监控采样行都没有**）：
  ```
  23  [RUN] case_mixed_400k_12m5
  24  [RUN] case_mixed_400k_12m5 pass-1 (prime, I-cache flush)
  25  ERROR: case_mixed_400k_12m5: pass-1 采集：20s 内无任何固件输出（ZT_BOOT/ztest 行/I3C_TEST: token；test.sh/gdb 也没产出任何行）——测试环境有问题（DebugServer 残留/烧录链路/镜像陈旧）
  ```
- `load_fail` 字样在全日志出现 **2 次**（`:20`、`:21`）。
- capture 未产出：`:8` 虽打了 `pass-2 (capture)` **头**，但紧随其后就是静默与 `load_fail`，**没有任何 token 行**。

### 2. 有没有出现 `ZT_BOOT` / `i3c` / token 字样

**有字面出现，但全部不是固件真实输出。**

| 字样 | 出现位置 | 性质 |
|---|---|---|
| `ZT_BOOT` | `:25` | 仅出现在 **ERROR 报错文案内部**（判据名称），非观测到的横幅。 |
| `I3C_TEST: token` | `:25` | 同上，是报错文案里引用的**判据名**（"…ZT_BOOT/ztest 行/I3C_TEST: token…"），**不是** `I3C_TEST:CTRL:PASS:…` 形态的真实 token 行。 |
| `i3c` | `:1`（`Cases: ['case_mixed_400k_12m5']`） | 仅出现在 case 名内。**注意本日志没有任何构建路径/源文件名**（无构建段）。 |

⇒ **没有任何真实 token 行**，也没有 `ZT_BOOT` 启动横幅。

### 3. 有没有出现 `ctrl=` / `tgt=` 的字节计数

**有，且 `ctrl=` 非 0。** 这是本 case 唯一的原始信号，7 个采样点全部一致：

```
:10    [t+0s]  boot=False ctrl=12 tgt=0 idle=0.0s
:11    [t+3s]  boot=False ctrl=12 tgt=0 idle=3.0s
:12    [t+6s]  boot=False ctrl=12 tgt=0 idle=6.0s
:13    [t+9s]  boot=False ctrl=12 tgt=0 idle=9.0s
:14    [t+12s] boot=False ctrl=12 tgt=0 idle=12.0s
:15    [t+15s] boot=False ctrl=12 tgt=0 idle=15.0s
:16    [t+18s] boot=False ctrl=12 tgt=0 idle=18.1s
```

- **controller 侧 `ctrl=12`（恒定 12 字节）**，tgt 侧 `tgt=0`。
- ⚠️ **`ctrl=` 计数非 0 ⇒ 串口确实有字节**。**那 12 字节的内容无从查证，因为失败轮不落盘原始串口字节** —— 日志里唯一可能承载它的地方是 `:18` 标为「gdb 输出尾部（诊断）」处，而 `:19` 是空白。
- 判读旁证（非结论）：`ctrl=12` 在 **`t+0s` 时就已经是 12** 并全程不变，且 `boot=False` 全程、`idle` 单调增长 ⇒ 更像**上一轮/DebugServer 残留**而非本 case 固件输出；同批通过例 `logs/i3c_dualcore_test_case_pure_i3c_12m5.log:2856` 在 `t+0s` 也是 `boot=False ctrl=2 tgt=0`（残留 2 字节），随后 `:2864` 才跳到 `boot=True ctrl=872 tgt=336`。**本 case 从未出现 `boot=True`。**
- 第二轮**完全没有** `[t+` 采样行、也没有 `[ENV] 1025 LISTENING` 行（`:23-25`）⇒ 第二轮连字节计数都没落盘。

### 4. 其他可查事实

- 串口映射正常：`:2 Controller UART: COM18`、`:3 Target UART: COM16`、`:4 Case timeout: 120s`，与本批其它 case 一致 ⇒ 静态映射不是失败点。
- `:9` 显示第一轮 DebugServer 是活的：`[ENV] 1025 LISTENING pid=12348 DebugServerConsole.exe` ⇒ 第一轮的静默**不是**「DebugServer 没起来」，而是「起来了但 gdb 烧录/固件启动未产生任何输出」。
- 本日志**无构建段**（无 `[BUILD]` 行、无 `Zephyr version:` 横幅）⇒ 本轮构建是否发生、是否用了缓存产物，**日志里未采集到**，不做推断。

## 备注

- **失败性质**：环境/连接层失败，**不是**驱动功能失败 —— 日志自述「环境问题（DebugServer 残留/烧录链路/镜像陈旧），非测试失败」（`:17`）、「测试环境有问题」（`:25`）。`rc=1` 的 `load_fail` 进一步指向**烧录**环节（`:20`）。
- **本 case 是本组唯一留下原始信号的一个**、也是唯一出现 `load_fail` 的一个：它证明 20s 监控窗口本身是工作的（有 7 行采样），失败点是「串口只有 12 字节残留、固件始终未启动（`boot=False` 全程）」。
- **不是"用例未验证"**：`OPEN_ITEMS.md` 的 **O-24** 条目已登记本次回归（条目原文：「失败轮观测到 `ctrl=12 tgt=0`（controller 有 12 字节输出但未启动），**该原始字节未落盘**」），并给出历史反证（09-16 全 `rc=0`、09-17 6/7 `rc=0`、09-18 两次 PASS）⇒ 是**回归**。本文件对 O-24 不作任何修改。
- **证据缺口（建议补）**：失败轮不落盘原始串口字节 ⇒ 无法回答"那 12 字节是什么"。O-24 已建议「失败轮也落盘原始串口字节」。
- 本文件只读产出：未上板、未构建、未改动任何既有文件。
