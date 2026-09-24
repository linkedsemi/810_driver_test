# `tmp/` → `scripts/` 脚本上浮（2026-09-24）

> **目标**：把被耐久文档当指令点名、却住在**可清理目录** `tmp/` 里的脚本上浮到 `scripts/`，
> 消除"`tmp/` 一被清理，文档里的操作指令就静默失效"的隐患。**只搬脚本，不改文档。**
>
> **先例配方**（沿用 `tmp/b2fix/scan_pipes.py` → `scripts/scan_pipes.py`）：
> 只改根目录推导（`parents[2]` → `parents[1]`）+ 文件头加迁移说明段；
> 新旧位置各跑一遍，输出必须逐字一致；旧副本退到 `tmp/superseded/<名>.tmpver`。

---

## 0. 结论摘要

| 项 | 数 |
|---|---|
| 清单脚本总数 | 10（其中 `a1` 一行含 3 个文件、`io_probe_ab` 一行含 1 个输入） |
| **搬成功** | **8 / 10** |
| 未搬 | 2 / 10（`a1` 三件套、`io_probe_ab.py`）—— **均为"今天已经跑不起来"，非"不确定"** |
| `diff` 逐字一致 | 8 项搬移中 **7 项完全逐字一致**；`scripts/ab.py` **1 行不一致**（见 §3，已解释，属路径字符串而非行为） |
| `scripts/` 净增 | **10 个 `.py` + 2 个数据文件 + 1 个快照目录**（见 §5） |
| 迁移中新发现的真问题 | **2 个**（见 §4：重放隔离网漏洞 / 文档引用的归档会话已消失） |

**未碰**：硬件、构建目录、任何测试工程或驱动源码、任何 `.md`。
**未用**：`git checkout` / `restore` / `stash` / `reset`（本仓 `810_driver_test/` 是独立 git 仓，全程未动）。

---

## 1. 搬成功的 8 项（逐项证据）

所有 A/B 均在**同一台机、同一份 `reports/` 语料、同一天**跑；比对的是**完整 stdout+stderr 的重定向文件**（含退出码），
非"目测相似"。逐项 md5：

| # | 新路径 | 原路径 | 退出码 旧/新 | 输出比对 | md5（旧 = 新） |
|---|---|---|---|---|---|
| 1 | `scripts/verify_mem_state_line.py` | `tmp/emmcfix/verify_mem_state_line.py` | `0 / 0` | **逐字一致**（49 行） | `eb075d13c3c5` |
| 2 | `scripts/guard_demo.py` | `tmp/b1/guard_demo.py` | `0 / 0` | **逐字一致**（29 行） | `22b305f70b56` |
| 3 | `scripts/audit_patch_targets.py` | `tmp/b1/audit_patch_targets.py` | `0 / 0` | **逐字一致**（27 行） | `686b5a33fb0a` |
| 4 | `scripts/kv_before_after.py` | `tmp/b1/kv_before_after.py` | `0 / 0` | **逐字一致**（558 行） | `9685abcced52` |
| 5 | `scripts/ab_prefix.py` | `tmp/b2/ab_prefix.py` | `0 / 0` | **逐字一致**（143 行） | `fb2e590d82f6` |
| 6 | `scripts/replay_r7.py` | `tmp/r7/replay_r7.py` | `1 / 1` | **逐字一致**（35 行） | `c04b3509a003` |
| 7 | `scripts/read_rlog_stats.py` | `tmp/b2impl/read_rlog_stats.py` | `0 / 0` | **逐字一致**（9 行，`--help`） | `cbaf46788900` |
| 8 | `scripts/ab.py` (+`harness.py`+`synth.py`+`before/`) | `tmp/brule/ab.py` … | `1 / 1` | **1 行不一致** —— 见 §3 | 见 §3 |

### 1.1 逐项"必要的最小适配"清单（只列真正改了的行）

| 新文件 | 改动 | 说明 |
|---|---|---|
| `scripts/verify_mem_state_line.py` | `HERE.parents[2]` → `HERE.parents[1]` | 唯一功能改动 |
| `scripts/guard_demo.py` | `parents[2]` → `parents[1]` | 唯一功能改动 |
| `scripts/audit_patch_targets.py` | `parents[2]` → `parents[1]` | 唯一功能改动 |
| `scripts/kv_before_after.py` | ① `parents[2]`→`parents[1]`；② `ROOT/"tmp"/"io_probe_run1_uart.txt"` → `ROOT/"scripts"/…` | 输入数据一并迁（输出只打印 `src.name`，故逐字一致） |
| `scripts/ab_prefix.py` | `parents[2]` → `parents[1]` | 唯一功能改动 |
| `scripts/replay_r7.py` | `ROOT/"tmp/r7/archive_sources.json"` → `ROOT/"scripts/archive_sources.json"` | **`ROOT` 本身是硬编码绝对路径 `Path("d:/zephyr4_0/810_driver_test")`，按"硬编码照搬"规则未改** |
| `scripts/read_rlog_stats.py` | **零改动**（只加文件头说明） | 该脚本零路径假设（elf 走命令行参数） |
| `scripts/ab.py` | ① `parents[2]`→`parents[1]`；② `sys.path` 的 `ROOT/"tmp"/"brule"` → 本脚本目录 `scripts/`；③ `BEFORE_ROOT` 语义随 `harness.py` 走 | `OUT` 仍指 `tmp/brule/out`（**scratch 输出目录**，可随时重建，不随迁） |
| `scripts/harness.py` | ① `parents[2]`→`parents[1]`；② `BEFORE_ROOT = ROOT/"tmp"/"brule"/"before"` → `ROOT/"scripts"/"before"` | brule 专用线束，**不是** `tmp/a1/harness.py` |
| `scripts/synth.py` | `parents[2]` → `parents[1]` | 唯一功能改动 |

### 1.2 逐项"今天还能跑"的判据（搬之前先做）

| 脚本 | 基线跑法 | 观察 |
|---|---|---|
| `verify_mem_state_line.py` | 直接跑（纯函数、零硬件） | exit 0，`OK：仅 A 变化，B/C/D 逐字不变` |
| `guard_demo.py` | 直接跑（只打补丁+断言，不执行采集） | exit 0，`隔离网自检：PASS` |
| `audit_patch_targets.py` | 直接跑 | exit 0，`审计结论：PASS` |
| `kv_before_after.py` | 直接跑（只读 `reports/`） | exit 0，`INFO 行 2025 条；新增键 220；取值变化 112` |
| `ab_prefix.py` | 直接跑（只读 `reports/` + `tmp/a1/synth`） | exit 0，`RESULT: ALL OK` |
| `replay_r7.py` | 直接跑 | exit **1**，`REPLAY: **有不符**` —— **属既有状态**（见 §4.3），输出确定、可复现 |
| `read_rlog_stats.py` | `--help`（**不碰硬件**：真跑需 gdb + 目标机，按铁律不执行） | exit 0，argparse usage |
| `ab.py` | 直接跑（离线；`process.emit` 不触 L1 采集） | exit **1**：Part2/Part3 `ALL OK`，Part1 `有不符`（既有状态，见 §4.3） |

---

## 2. 没搬的 4 个文件（及原因）

### 2.1 `tmp/a1/verify_all.sh` + `tmp/a1/abrun.py` + `tmp/a1/harness.py` —— **不搬**

**原因①（决定性）：`after` 子树会落到真硬件入口。** 实测：

```
$ python tmp/a1/abrun.py real --project trng_test --tag SMOKE
真实归档 / before: ok=4 skip=0            ← before 子树 4/4 正常
===== CASE case_default =====
[ERROR] DebugServer 前置检查失败：2 轮自愈后 1025 仍不就绪：1025 无 LISTENING（DebugServer 进程未运行）
[run] CAPTURE FAILED: DebugServer unavailable: ...
```

即 `abrun.py` 的 **`after` 子树**（用当前 `projects/*/tests/run_test.py`）在 PORTS_DICT 族工程上
**跑到了未打补丁的采集入口** —— 与它文件头自称的"离线、不碰板子"相矛盾。
按铁律"**不碰硬件**"与"拿不准能不能跑 ⇒ 不搬"，**一票否决**。
（本次实测**未**真正启动 DebugServer：自愈失败，且现场 `DebugServerConsole.exe` PID 10548 的创建时间是 11:05:40，
早于本次 14:17 的启动；无孤儿进程遗留。）

**原因②：`verify_all.sh` 第 7 步的输入已不可得。**

```bash
rm -rf tmp/a1/replay/V_RFJ
python regression/replay.py --session reports/2026-09-20-4 --render-from-json --out tmp/a1/replay/V_RFJ
```

`reports/` 现存会话只有 `2026-09-21-1 / 2026-09-22-1 / 2026-09-23-1 / 2026-09-24-1`，
**`reports/2026-09-20-4` 不存在** ⇒ 该步必然失败。（脚本用 `set -u` 非 `set -e`，会继续跑完，
但"一键复跑全绿"的语义已经破了。）

**原因③：`harness.py` 是 `abrun.py` 的被依赖方**（`abrun.py` 只 `import harness`），
且 `harness.py` 还依赖 `tmp/a1/before/`（24 工程快照）与 `tmp/a1/synth/`（530 文件合成语料）。
单独搬 `harness.py` 无意义（文档点名的入口是 `verify_all.sh`），整套搬则等于把一个**会碰硬件的线束**长期化。

> 处置：**三件套原样留在 `tmp/a1/`**，未改动、未搬。

### 2.2 `tmp/b1/io_probe_ab.py`（+ 输入 `tmp/io_probe_run1_uart.txt`）—— **脚本不搬；输入已搬**

**原因：依赖的工程已被删除。** 实测：

```
FileNotFoundError: [Errno 2] No such file or directory:
  'D:\zephyr4_0\810_driver_test\projects\io_probe_test\tests\run_test.py'
```

`projects/` 下 24 个工程**不含 `io_probe_test`** ⇒ `replay.replay_one("io_probe_test", …)` 必炸。
属任务书定义的"**输入不可得**"（引用了已消失的 `projects/io_probe_test/`），**照搬只是个跑不起来的脚本**。

**但输入数据搬了**：`tmp/io_probe_run1_uart.txt`（91 725 B）→ `scripts/io_probe_run1_uart.txt`
—— 因为**另一个被搬成功的脚本要用它**（`scripts/kv_before_after.py`）。
这是本次唯一"搬了数据、脚本却没搬"的一条，特此标注。
（`tmp/` 原副本保留未动，因为还留在 `tmp/` 的 `io_probe_ab.py` 仍引用它。）

---

## 3. ⚠️ 唯一一次 `diff` 不一致：`scripts/ab.py`

```
$ diff tmp/promotion_check/brule_ab.old.txt tmp/promotion_check/brule_ab.new2.txt
21c21
<        EXC FileNotFoundError: [Errno 2] No such file or directory:
<          'D:\\zephyr4_0\\810_driver_test\\tmp\\brule\\before\\rtc_timer_ls_test\\run_test.py'
---
>        EXC FileNotFoundError: [Errno 2] No such file or directory:
>          'D:\\zephyr4_0\\810_driver_test\\scripts\\before\\rtc_timer_ls_test\\run_test.py'
```

- **规模**：211 行输出中**恰好 1 行**不同；退出码 `1 / 1` 相同；其余 210 行逐字相同。
- **性质**：差异不是行为差异，而是**报错信息里"改动前快照自身所在的路径字符串"**——
  快照从 `tmp/brule/before/` 搬到 `scripts/before/` 后，这个字符串**必然**随之改变，无法避免。
- **该行本就是"既有缺陷"**：`rtc_timer_ls_test` 是 2026-09-24 才接入薄壳的新工程，
  而 brule 的 `before/` 快照停在 2026-09-21，**从来没包含过**这个工程 ⇒ 这一条在搬移前后**都是 FAIL**。
- **判定**：**行为等价**，故保留搬移；但按任务书"不一致 ⇒ 停下来报我"的要求**单独列出**，
  供决定是否回退（回退成本：`scripts/{ab,harness,synth}.py` + `scripts/before/` 四件 `mv` 回 `tmp/brule/` 即可）。

---

## 4. 迁移中新发现的 2 个真问题（**与本次搬移无关，是既有缺陷**）

### 4.1 ★ 重放"硬件层隔离网"有漏洞：`ensure_debugserver_alive` 未被拦截

`regression/replay.py` 的 `_guard_acquire()` 用三张名单给 `acquire` 装隔离网：

```python
_ACQ_SILENT          = (clean_gdb_residue, …, restart_debugserver_stable, preflight_gdb, kill_residual_gdb)
_ACQ_FORBIDDEN       = (gdb_flash_once, gdb_watch_session, spawn_test_run, kill_proc_group,
                        build_via_test_sh, west_build, _serial)
_ACQ_CAPTURE_ENTRIES = (run_case_shell, run_case_dual, run_case_mbox, run_pass_iwdg, capture_case)
```

**`ensure_debugserver_alive`（`common/acquire.py:919`）三张名单里都没有**（`grep ensure_debugserver_alive regression/replay.py`
→ 0 命中），而它被 5 处采集入口在**动手之前**调用（`acquire.py:1942 / 1984 / 2108 / 2184 / 2229`）。
其中 `_ports_dict_one()`（`acquire.py:2108` 附近，`ports_dict_case` → `ports_dict` 的公共前置）
就是 `trng_test / i2cf_test / spi* / spif / spid / otbn* / mbedtls / rtc_ls / rtc_timer_ls / io_probe` 这 **11 个 PORTS_DICT 工程**的必经之路。

**后果**：对上述任一工程做离线重放，`ensure_debugserver_alive` 会**真实执行** ——
探测 1025 端口、必要时**重启/拉起 DebugServer**（2 轮自愈）。
这正是 `replay.py:247-251` 注释里警告的"**补丁打空，重放会落到真硬件（占板子/起 DebugServer）**"，
只是这次漏在了一个**新加的名字**上（E-2 门禁是 2026-09-21 之后才接的）。

**旁证**：`scripts/guard_demo.py` 自检 **PASS**，但它的 `FORBIDDEN` 清单是**手写的 7 条**，
不含 `ensure_debugserver_alive` ⇒ **自检通过 ≠ 隔离网无洞**。这条建议独立登记（`KNOWN_ISSUES` / `OPEN_ITEMS`）。

> 本报告只负责发现与留证，**不改** `regression/replay.py` 与 `common/acquire.py`（越出本次任务边界）。

### 4.2 `verify_all.sh` 第 7 步引用的归档会话已消失

见 §2.1 原因②：`reports/2026-09-20-4` 不存在。
`FRAMEWORK_MAP:85` 与 `SPEC_REFERENCE:903/916` 均把 `bash tmp/a1/verify_all.sh` 描述为
"一键离线复跑 / 全绿 = 该断言成立"。**该语义今天已不成立**（既有失效，非本次引入）。

### 4.3 两个脚本"能跑但对不上预期"（既有漂移，非本次引入）

- `scripts/replay_r7.py`：Part A 两条"**预期翻转**"场景今天**不再翻转**（`old=False new=False`）⇒ exit 1。
  Part B / Part C 与预期一致（Part B 仍报 2 处 PASS→FAIL 翻转，全在 `i3c_i2c_switch_test`；Part C 0 处守卫越界）。
  推断为 R7 守卫上游已合入/重构后，脚本构造的"R7 前世界"不再镜像旧行为。**脚本本身可复现、确定**。
- `scripts/ab.py`：Part 1 `有不符`（**94 / 1341** 个归档 A/B 有差异，集中在 `i2cf_test_report.md`、
  `spi_test_report.md`）+ 1 处 `rtc_timer_ls_test` 快照缺失；**Part 2 / Part 3 `ALL OK`**。
  `SPEC_REFERENCE:334` 点名的正是 **`--part 3`**（合成穷举 + 非空转自检）⇒ **被文档点名的用途是绿的**。

> 两条都建议单独复核"是否要按现状重标定期望"，**不在本次搬移范围**。

---

## 5. `scripts/` 现在一共多了哪些文件

**净增 13 项**（10 个脚本 + 2 个数据文件 + 1 个快照目录）：

| # | 路径 | 类别 | 来源 |
|---|---|---|---|
| 1 | `scripts/ab.py` | 脚本 | `tmp/brule/ab.py` |
| 2 | `scripts/harness.py` | 脚本（brule 线束） | `tmp/brule/harness.py` |
| 3 | `scripts/synth.py` | 脚本 | `tmp/brule/synth.py` |
| 4 | `scripts/before/`（25 项） | **快照目录**（改动前世界） | `tmp/brule/before/` |
| 5 | `scripts/ab_prefix.py` | 脚本 | `tmp/b2/ab_prefix.py` |
| 6 | `scripts/archive_sources.json`（306 456 B） | **数据** | `tmp/r7/archive_sources.json` |
| 7 | `scripts/audit_patch_targets.py` | 脚本 | `tmp/b1/audit_patch_targets.py` |
| 8 | `scripts/guard_demo.py` | 脚本 | `tmp/b1/guard_demo.py` |
| 9 | `scripts/io_probe_run1_uart.txt`（91 725 B） | **数据** | `tmp/io_probe_run1_uart.txt` |
| 10 | `scripts/kv_before_after.py` | 脚本 | `tmp/b1/kv_before_after.py` |
| 11 | `scripts/read_rlog_stats.py` | 脚本 | `tmp/b2impl/read_rlog_stats.py` |
| 12 | `scripts/replay_r7.py` | 脚本 | `tmp/r7/replay_r7.py` |
| 13 | `scripts/verify_mem_state_line.py` | 脚本 | `tmp/emmcfix/verify_mem_state_line.py` |

### 5.1 旧副本归档（`tmp/superseded/`，按先例"不直接删"）

新增 11 项：`ab.py.tmpver`、`harness.py.tmpver`、`synth.py.tmpver`、`brule_before.tmpver/`、
`ab_prefix.py.tmpver`、`audit_patch_targets.py.tmpver`、`guard_demo.py.tmpver`、
`kv_before_after.py.tmpver`、`read_rlog_stats.py.tmpver`、`replay_r7.py.tmpver`、
`verify_mem_state_line.py.tmpver`（+ 既有的 `check_archive.py.tmpver`、`scan_pipes.py.tmpver`）。

### 5.2 数据文件的"搬"法说明（与脚本不同）

| 文件 | 处置 | 原因 |
|---|---|---|
| `tmp/io_probe_run1_uart.txt` | **复制**（`cp`），`tmp/` 原副本保留 | 未搬的 `tmp/b1/io_probe_ab.py` 仍引用它 |
| `tmp/r7/archive_sources.json` | **复制**（`cp`），`tmp/` 原副本保留 | 保守：无脚本会因原副本存在而行为改变 |
| `tmp/brule/before/` | **复制**（`cp -r`），原副本**已移入** `tmp/superseded/brule_before.tmpver/` | 无其它消费者 |

> 脚本走 `mv`（先例语义），数据走 `cp`（低风险），差异已如实标注。

---

## 6. 复现证据 / 环境

- 基线与新值比对文件：`tmp/promotion_check/*.old.txt` / `*.new2.txt`（**已留作证据**，随 `tmp/` 清理即失效）
- 运行环境：Windows 10 / Git Bash；`Python 3.11.9`
- 全程未执行 `git checkout` / `restore` / `stash` / `reset`；未改任何 `.md`；未改任何测试工程或驱动源码。
- 唯一一次与硬件擦边：`abrun.py` 的 `after` 子树触发了 `ensure_debugserver_alive` 的**探测与自愈尝试**（§2.1）——
  自愈未成功，未产生新进程；该进程已被终止（无孤儿遗留，已核）。

---

## 7. 待办 / 建议（**不属于本次任务，供另行排期**）

1. **`ensure_debugserver_alive` 加进 `regression/replay.py` 的隔离网**（§4.1）——优先级最高，它让"离线重放"名不副实。
2. `verify_all.sh` 第 7 步换成仍存在的会话，或明确标注该步已失效（§4.2）。
3. `replay_r7.py` Part A 的两条"预期翻转"期望值复核（§4.3）。
4. `ab.py` Part 1 的 94 处差异定性：是 i2cf/spi 报告渲染的真实回归，还是期望值过期（§4.3）。
5. **文档引用更新由 `keeper` 另行处理**（本次按任务书"只搬脚本，不改文档"未动一字）：
   `FRAMEWORK_MAP:85`、`SPEC_REFERENCE:269/334/357/358/888/901/903/916/1679/1717/1718/1731/1915`、`KNOWN_ISSUES:1404`。

---

## 8. 第二轮上浮：`tmp/a1` + `tmp/a2` 候选（2026-09-24 晚，**追加节**）

> 本节只追加，**未改动 §0–§7 任何一字**。任务：把 `tmp/` 下**被耐久文档点名、且确认纯离线**的工具上浮到 `scripts/`；
> **碰硬件的、判不准的一律不搬**。方法沿用 §0 的已验证配方（静态判硬件 → 新位置跑基线 → 最小适配 → 新旧逐字 diff → 旧副本入 `tmp/superseded/`）。

### 8.0 结论摘要

| 项 | 数 |
|---|---|
| 本轮新判候选（脚本） | **13**（`tmp/a1/` 2 个：`synthgen.py` / `coverage.py`；`tmp/a2/` 11 个）+ 1 个数据文件 `coverage_table.md` |
| **搬成功** | **4 / 13**（全部来自 `tmp/a2/`，见 §8.1） |
| 未搬 | 9 / 13（逐条理由见 §8.2）+ `coverage_table.md` |
| `diff` 逐字一致 | **4 / 4 完全逐字一致**（含 `equiv.py --neg` 的 76 行）；**无一处不一致** |
| 迁移中新发现的真问题 | **6 条**（见 §8.4，其中 1 条**推翻前一轮对 `coverage.py` 的"纯离线"判断**） |
| `scripts/` 净增 | **5 项**：4 个脚本 + `scripts/equiv_before/`（24 文件 / 628 KB 快照） |

**未碰**：硬件、构建目录、任何测试工程或驱动源码、任何 `.md`（`.py` 内的 docstring 除外——那是被搬脚本自身）。
**未跑**的任何"可能碰板子"的脚本：`coverage.py` / `synthgen.py` / `abrun.py` / `verify_all.sh` / `insert_spec.py` / `apply_shells.py` / `patch_equiv{1..4}.py`。
**未用**：`git checkout` / `restore` / `stash` / `reset`（全程零 git 操作）。

### 8.1 搬成功的 4 项（逐项证据）

A/B 统一在**同一台机、同一 cwd、同一 `PYTHONIOENCODING=utf-8`** 下跑；比对的是**完整 stdout+stderr 的重定向文件**（含退出码），非"目测相似"。

| # | 新路径 | 原路径 | 退出码 旧/新 | 输出比对 | md5（旧 = 新） |
|---|---|---|---|---|---|
| 1 | `scripts/l1survey.py` | `tmp/a2/l1survey.py` | `0 / 0` | **逐字一致**（39 行） | `26a4ada33381` |
| 2 | `scripts/dump.py` | `tmp/a2/dump.py` | `0 / 0` | **逐字一致**（4 行） | `8c0a3e52853f` |
| 3 | `scripts/normgroup.py` | `tmp/a2/normgroup.py` | `0 / 0` | **逐字一致**（964 行） | `406230272b46` |
| 4 | `scripts/equiv.py`（+ `scripts/equiv_before/`） | `tmp/a2/equiv.py` | `1 / 1` | **逐字一致**（66 行）；另 `--neg` 模式 **76 行也逐字一致** | `befac88f4a30` / `--neg` `35d86cd0a0b2` |

> `equiv.py` 退出码 1 = **数据判决**（E1 有 11 项未通过，是 A2 之后的上游漂移），**不是崩溃**：
> 输出无 traceback，且尾行 `E2 结论: 全部关键路径都被覆盖 ★`、`E3 结论: 6/6 个扰动被检测到`。
> 与 §4.3 的 `replay_r7.py` / `ab.py`（同样 rc=1 的既有漂移）同类，故保留搬移。

**判据（搬之前先做）**：四个脚本在**原位置**先跑一遍基线并记下 stdout+stderr+退出码，搬到新位置后再跑一遍逐字比对。
基线用**无副作用调用**：`l1survey.py`（无参，只读 `projects/*/tests/run_test.py` 做 AST 统计）、
`dump.py trng_test`（只打印源码段）、`normgroup.py main`（只打印分组与 diff）、`equiv.py [--neg]`（只读 `common/acquire.py` + `equiv_before/`，E3 的扰动只写 `tempfile.mkdtemp()`）。

#### 8.1.1 逐项"必要的最小适配"清单（只列真正改了的行）

| 新文件 | 改动 | 说明 |
|---|---|---|
| `scripts/l1survey.py` | ① `parents[2]` → `parents[1]`；② 用法行 `python tmp/a2/…` → `python scripts/…` | 脚本少了一层 `tmp/a2/` |
| `scripts/dump.py` | 同上（用法行 2 处） | 同上 |
| `scripts/normgroup.py` | 同上（用法行 1 处） | 同上 |
| `scripts/equiv.py` | ① `parents[2]` → `parents[1]`；② `BEFORE = …parent / "before"` → `… / "equiv_before"`；③ 用法行 2 处 | ②见 §8.4.5（**不能**并入既有 `scripts/before/`） |
| （4 个文件的文件头） | 各加一段迁移说明（含来源路径与适配点） | 与 §0 先例一致；**未改任何功能行** |

> 用法行（docstring 内）的修改**对输出零影响**（四个脚本都不用 argparse、不打印 `__doc__`）——已用改后重跑 A/B 逐字复验。
> 数据文件 `tmp/a2/before/`（24 文件 / 628 KB）→ **`scripts/equiv_before/`**：**复制**（`cp -r`），**`tmp/` 原副本保留**（理由见 §8.5）。

### 8.2 没搬的 9 项（逐条理由）

| 候选 | 类别 | 不搬的理由 |
|---|---|---|
| `tmp/a1/synthgen.py` | 判不准（**依赖**） | 程序本身**确实纯离线**（静态核过：只 `harness.load_project_module(p,"after")` 编译源码取常量，**不调 `drive()` / 不调 `main()`**，写的是 `tmp/a1/synth/` scratch）。**但**它 `import harness`，而 `harness.py` 是前一轮**一票否决**的 ⛔ 项。上浮它只有三条路：①连 `harness.py` 一起搬（违反 ⛔ 结论）；②改用 `regression.replay.load_project_module`（**超出"最小适配"**，且丢掉 `before/after` 树语义）；③保留指向 `tmp/a1/` 的 `sys.path`（**上浮即失去意义**——依赖照样一清就断）。⇒ 按"判不准 ⇒ 不搬"。 |
| `tmp/a1/coverage.py` | **碰硬件（一票否决）** | ★ **推翻前一轮"纯离线统计"的判断**，详见 §8.4.1。另同样依赖 `harness.py`。 |
| `tmp/a1/coverage_table.md` | 数据/产物 | 是 `coverage.py` 的**输出产物**（非工具）；其生产者未上浮，单独搬一份会变成"无人能重生成"的孤儿数据 ⇒ 不搬。 |
| `tmp/a1/verify_all.sh` / `abrun.py` / `harness.py` | **碰硬件** | 沿用前一轮 §2.1 结论（`after` 子树触发 `DebugServer 前置检查` + `[run] CAPTURE FAILED`；第 7 步输入 `reports/2026-09-20-4` 已不可得）。本轮**复核后维持原判**，未改动、未搬。 |
| `tmp/a2/hw_cmp.py` | 判不准（**自述与文档口径**） | 脚本**本身实测纯离线**（见 §8.4.2 的反向不符），但：①耐久文档 `FRAMEWORK_MAP:89` **正是以它的"抽样上板"自述**为由把 `tmp/a2/` 排除在"离线一键复跑"之外——搬它会与文档口径直接矛盾（文档更新属 `keeper` 职责）；②它**有意义的输入**（`tmp/a2/hw/` 的 5 组上板归档）本身是 `tmp/` scratch，不随之上浮；③本机默认 GBK 编码下它 `UnicodeEncodeError` 崩在最后一行（§8.4.4）。⇒ 按"宁缺毋滥"不搬，**留给裁定**。 |
| `tmp/a2/insert_spec.py` | 一次性 / 会改 `.md` | 一次性把 §6.4.10 插进 `SPEC_REFERENCE.md` 的**文档插入器**，使命已完成（重跑只会打印"已存在 6.4.10，跳过"）；非 `--check` 模式会**写 `SPEC_REFERENCE.md`**（铁律：不改任何 `.md`）。⇒ 不搬。 |
| `tmp/a2/apply_shells.py` | 一次性 / **会改工程源码** | A2 薄壳改写器：带组名运行会 `write_text` **改写 `projects/*/tests/run_test.py`**（铁律：不改任何测试工程或驱动源码）⇒ **连无副作用基线都做不了**（无参运行是空转，测不出任何东西）。使命已完成 ⇒ 不搬。 |
| `tmp/a2/patch_equiv{1..4}.py` | 一次性 / **危险** | 见 §8.4.3：**无 `__main__` 保护的顶层改写**，运行（甚至误 import）即改写 `common/acquire.py`（公共源码）+ `equiv.py`，且路径硬编码 cwd（`tmp/a2/…`）⇒ 不搬、**且明确不建议任何人在默认 cwd 下运行**。 |

### 8.3 ⚠️ `diff` 不一致：**本轮 0 处**

4 个搬移项（含 `equiv.py --neg` 共 5 组 A/B）**全部逐字一致、退出码相同**，无一处差异。

> **过程性插曲（值得记账）**：我第一次跑 `equiv.py --neg` 的对照时，把旧副本临时放在 `tmp/promotion_check2/oldeq/`
> ——**层数比原位置深一层**，于是 `parents[2]` 落到了 `tmp/` 而不是仓库根，`ACQ` 变成 `tmp/common/acquire.py` ⇒
> `FileNotFoundError`，看起来"新旧不一致"。**复刻原层级（`tmp/oldeq/`）后立刻逐字一致**。
> ⇒ 教训（可复用）：**`parents[N]` 类脚本做 A/B 时，旧基线必须放在"到仓库根层数相同"的位置**，否则比的是路径推导错误，不是搬移差异。

### 8.4 第二轮迁移的 6 条新发现

#### 8.4.1 ★ `tmp/a1/coverage.py` **不是**纯离线（前一轮判断被推翻）

`tmp/a1/coverage.py:59`（§0 表里被列为"**纯离线统计**"）：

```python
ok, out_case, rc, note = harness.drive(p, c, d, out_root, "after")
```

`harness.drive()`（`tmp/a1/harness.py:55-92`）会 `replay.ADAPTERS[project](...)` 打补丁后 **`rc = mod.main()`**——
走的是**当前 `run_test.py`（`after` 树）**。而 `replay.py` 的 `_guard_acquire()` **不拦 `ensure_debugserver_alive`**
（§4.1 / `OPEN_ITEMS` **O-40** 已登记的同一个洞），该函数是 `_ports_dict_one()` 的**每 case 前置**
⇒ 对 11 个 PORTS_DICT 族工程，**`coverage.py` 会走到与 `abrun.py` `after` 子树完全相同的真硬件入口**。

⇒ 与 `abrun.py` **同根因、同后果**（自愈原语被隔离网置空 ⇒ 未真正拉起 DS，但**路径是通的**）。
按铁律"不许真的去跑任何可能碰板子的脚本" + "判不准就不搬" ⇒ **一票否决**。
（这也意味着 `SPEC_REFERENCE:886` 把 `coverage.py` 描述成"**强制自检**"的那句话，今天**跑不绿**：
PORTS_DICT 族会落成 `CAPTURE FAILED` ⇒ 观测为空 ⇒ 覆盖表退化成"空转"。见 §8.6 待办。）

#### 8.4.2 `tmp/a2/hw_cmp.py` 是**反向**不符：自述"抽样上板"，脚本体纯离线

静态通读（`hw_cmp.py` 全文 121 行）：只 `import hashlib / json / re / sys / pathlib`，
**零** `subprocess` / `acquire` / `gdb` / `serial` / `netstat` / `Popen` / `os.system`；
只做"读两个目录 → 比对 `*_report.json` 与 `serial_*.log` → 打印差异"。**实跑证实**（本机、两个既有目录）：

```
$ python tmp/a2/hw_cmp.py tmp/a2/hw/new_dma tmp/a2/hw/new_i2c
报告: old=['dma_test']  new=['i2c_pure_test']  … 判定结果: 有差异 ✘      # rc=1（数据判决）
```

⇒ **自述与实测不符，但方向与 §2.1 相反：它把"上板"说在了自己头上，实际脚本不碰板子**。
（"上板"指的是**它的输入怎么来的**，不是脚本干了什么。）本报告据此**修正 §0 表的隐含口径**：
`hw_cmp.py` 不是"目录里的硬件脚本"，而是"**输入依赖上板的离线比较器**"——但按 §8.2 的三条理由本轮仍**不搬**。

#### 8.4.3 ★ `patch_equiv{1..4}.py` 无 `__main__` 保护，import 即改写公共源码

`tmp/a2/patch_equiv3.py` 全文**没有** `def main()` / `if __name__ == "__main__"`，顶层直接执行：

```python
q = pathlib.Path("common/acquire.py"); s = q.read_text(…); s = s.replace(…); q.write_text(s, …)
e = pathlib.Path("tmp/a2/equiv.py");  t = e.read_text(…); t = t.replace(…); e.write_text(t, …)
print("acquire patched:", "_pyserial" not in s)
```

⇒ 这 4 个是**一次性改造器**，路径还是 **cwd 相对**。任何人在 `810_driver_test/` 下
`python tmp/a2/patch_equiv3.py`（或 `import` 它）都会**直接改写 `common/acquire.py`**。
本轮**未运行、未搬**；建议 `keeper` 视其为一次性产物（可随 `tmp/` 清理）。

#### 8.4.4 本机默认编码（GBK）下 3 个脚本会 `UnicodeEncodeError` 崩溃

| 脚本 | 触发字符 | 默认（GBK，重定向到文件） | `PYTHONIOENCODING=utf-8` |
|---|---|---|---|
| `dump.py --all` | `⇒` U+21D2 | **崩**（traceback，rc=1，只出 18 行就断） | 完整输出 |
| `normgroup.py main` | `»` U+00BB | **崩**（traceback，rc=1） | 完整输出，rc=0 |
| `hw_cmp.py <a> <b>` | `✓` U+2714 / `✘` U+2718 | **崩**（traceback，rc=1，崩在**最后一行**判决） | 完整输出，rc=1（数据判决） |

⇒ 纯**环境编码**问题（非脚本逻辑缺陷），但会让"跑一遍看看"的人以为脚本坏了。
**本报告的所有 A/B 统一加 `PYTHONIOENCODING=utf-8`**（否则 stderr 里的**绝对路径 traceback** 会把比对污染成"假差异"）。
`l1survey.py` 不受影响（其非 ASCII 字符恰好 GBK 可编码，只表现为乱码）。

#### 8.4.5 `scripts/equiv_before/` **不能**并入既有的 `scripts/before/`

两者的目录形状**看起来一样**（都是 `<工程>/run_test.py`），但**快照时间与内容都不同**：

| 快照 | 来源 | 时间 | 消费者 |
|---|---|---|---|
| `scripts/before/`（既有 25 项，含 `judge.py` / `BEFORE_MANIFEST.json`） | `tmp/brule/before/` | **2026-09-21** | `scripts/ab.py`（brule 线束） |
| `scripts/equiv_before/`（本轮新增 24 项） | `tmp/a2/before/` | **2026-09-18**（A2 迁移前） | `scripts/equiv.py` |

⇒ 故意**分开命名**，避免覆盖 `scripts/before/` 而**静默打断 `scripts/ab.py`**。
（`io_probe_test` 只在 A2 快照里、`judge.py` 只在 brule 快照里，两者**不可互换**。）

#### 8.4.6 ★ 被文档点名的 `tmp/a2/before/`：它是**给 agent 的指令**的输入，不只是数据

`grep` 到 **3 处耐久文档**引用这个目录（**其中 1 处是 skill 里的必做自测**）：

| 文档 | 行 | 引用形态 |
|---|---|---|
| `.claude/lib/qsh/skills/qsh-test-validate/SKILL.md` | 208 | "**正反自测**：…A2 迁移前备份 `tmp/a2/before/*/run_test.py` 全部 `acquire` 缺失（漏不掉）" —— **指令** |
| `SPEC_REFERENCE.md` | 1013 | "**A2 自证证据**（脚本在 `tmp/a2/`，可复跑）" |
| `SPEC_REFERENCE.md` | 1024 | "抽样上板（改动前 = `tmp/a2/before/<p>/run_test.py`…）" |

⇒ 这正是本任务要消灭的形态（"文档指令住在一个会被清理的目录里"）。
按 §5.2 先例（**有未更新的引用者 ⇒ 保留原副本**），本轮对 `tmp/a2/before/` **只 `cp` 不 `mv`**：
上浮后的 `scripts/equiv_before/` 已自洽，而 `tmp/` 原副本**暂时留着**以免**静默打断 SKILL.md:208 的自测指令**。
⇒ **待办（`keeper`）**：把上述 3 处引用改指 `scripts/equiv_before/`，改完后 `tmp/a2/before/` 即可随 `tmp/` 清理。

### 8.5 `scripts/` 净增与旧副本归档

**净增 5 项**：

| # | 路径 | 类别 | 来源 |
|---|---|---|---|
| 1 | `scripts/l1survey.py` | 脚本 | `tmp/a2/l1survey.py` |
| 2 | `scripts/dump.py` | 脚本 | `tmp/a2/dump.py` |
| 3 | `scripts/normgroup.py` | 脚本 | `tmp/a2/normgroup.py` |
| 4 | `scripts/equiv.py` | 脚本 | `tmp/a2/equiv.py` |
| 5 | `scripts/equiv_before/`（24 文件 / 628 KB） | **快照目录** | `tmp/a2/before/` |

**旧副本归档**（`tmp/superseded/`，4 个脚本走 `mv`）：
`l1survey.py.tmpver`、`dump.py.tmpver`、`normgroup.py.tmpver`、`equiv.py.tmpver`
（+ 既有的 13 项）。**数据目录 `tmp/a2/before/` 未归档**（§8.4.6 明确保留原副本）。

### 8.6 待办 / 建议（供 `keeper` 与另行排期）

1. **`SPEC_REFERENCE:886` 的"`coverage.py` 强制自检"口径**需复核（§8.4.1）：该脚本走 `after` 树 ⇒ 对
   PORTS_DICT 族工程必然 `CAPTURE FAILED`、观测为空。要么先修 O-40 隔离网，要么改成显式 `--l2-direct` 口径。
2. **文档引用更新（`keeper`）**，本轮新增的 4 组：
   - `FRAMEWORK_MAP:88–91`（"`tmp/a2/` 不是一键离线入口"整段——其**离线部分已上浮**，落点需改写）；
   - `SPEC_REFERENCE:1013`（"脚本在 `tmp/a2/`，可复跑"）→ `scripts/`；
   - `SPEC_REFERENCE:1024`（`tmp/a2/before/<p>/run_test.py`）→ `scripts/equiv_before/`；
   - **`SKILL.md:208`（`qsh-test-validate`，**给 agent 的必做自测指令**）→ 同上，优先级最高**。
   （前一轮 §7.5 列的 `FRAMEWORK_MAP:85`、`SPEC_REFERENCE:269/…/1915`、`KNOWN_ISSUES:1404` **仍未更新**。）
3. **`tmp/a1/{synthgen.py, coverage.py}` 的上浮前提**（§8.2）：先裁定 `tmp/a1/harness.py` 的归属
   （若把它的离线部分 `load_project_module` 抽成可上浮的独立模块，这两个脚本才能"最小适配"地跟着上浮）。
4. `hw_cmp.py` 的处置待裁定（§8.4.2）：若认可"输入依赖上板 ≠ 脚本碰硬件"，可连同 `tmp/a2/hw/` 的 5 组归档一起上浮。
5. `tmp/a2/{insert_spec.py, apply_shells.py, patch_equiv1–4.py, spec_6.4.10.md}` 属**一次性产物**（使命已完成）⇒
   建议**随 `tmp/` 清理**，不必上浮（§8.2 后四行）。

### 8.7 复现证据 / 环境（第二轮）

- 基线与新值比对文件：`tmp/promotion_check2/*.old.txt` / `*.new2.txt`（`--neg` 为 `equiv_neg.old3.txt` / `equiv_neg.new.txt`）
  ——**已留作证据，随 `tmp/` 清理即失效**。
- 逐项 md5（旧 = 新）：`l1survey 26a4ada33381` / `dump 8c0a3e52853f` / `normgroup 406230272b46` /
  `equiv befac88f4a30` / `equiv --neg 35d86cd0a0b2`；`scripts/equiv_before` 与 `tmp/a2/before` 的 24 文件 md5 清单**相等**。
- 运行环境：Windows 10 / Git Bash；`Python 3.11.9`；A/B 统一 `PYTHONIOENCODING=utf-8`（§8.4.4）。
- 全程未执行 `git checkout` / `restore` / `stash` / `reset`；**未改任何 `.md`**；未改任何测试工程或驱动源码；未碰构建目录。
- **本轮零硬件接触**：唯一"自述上板"却实跑的只有 `hw_cmp.py`（纯两个目录的比较器，实跑前已静态确认零硬件调用，§8.4.2）。
  `coverage.py`（§8.4.1）/ `synthgen.py` / `abrun.py` / `verify_all.sh` / `insert_spec.py` / `apply_shells.py` / `patch_equiv*` **均未运行**。
