# `.claude` 子仓提交前审核报告（2026-09-24）

- 对象：`D:/zephyr4_0/810_driver_test/.claude`（submodule，HEAD `c64862f`）
- 范围：10 项未提交改动（8 改 + 2 新）
- 方式：只读 git（`status/diff/log`）+ 跨仓交叉引用核对（读主仓，未改）
- 审核人：keeper

## 0. 结论摘要

**可提交**（本次审核中发现并已就地更正的 2 处旧口径残留见 §1.3；另有 1 处口径歧义见 §2.2，
建议但不强制在提交前处理）。

---

## 1. 旧口径残留（审核重点 ①）

### 1.1 关键词全量扫描结果

对本子仓全部 `.md` 扫 `15ms` / `晚到` / `检查窗口` / `生成时刻` / `离线不碰` / `一键离线` / `无洞` /
`纯离线` / `晚同步` / `窗口过窄`：

| 关键词 | 命中 | 判读 |
|---|---|---|
| `15ms` | 5 处 | **全部已加删除线 + 撤回批注**，无一处作为结论留存 |
| `晚到` / `晚同步` / `检查窗口` / `窗口过窄` | 各 1–3 处 | 均为"被推翻的旧说法"的**引用形态**（`~~…~~` 或"既不要 A 也不要 B"的双向告诫） |
| `生成时刻` | 8 处 | 口径一致，统一为"**位置不可信、自带时间戳 = 生成时刻**"；`qsh-debug:261` 明确写了"上一版写成『生成时刻不可定』，**过头了**" |
| `离线不碰` / `一键离线` | **0 处** | 无残留 |
| `无洞` | 1 处 | `qsh-test-compare:204`，**否定式**用法（"PASS 不代表无洞"），正确 |
| `纯离线` | 1 处 | `qsh-test-gen:332`，指 L2 `process.py` 本身，非 O-40 语境，正确 |

`verify_all.sh` / `tmp/a2/` / `coverage.py`：**在本子仓内零引用**（前两者只在主仓 `tmp/a1/` 存在），
无悬空指引。

### 1.2 五条被推翻结论的落点核对

| 被推翻的旧说法 | 现口径落点 | 状态 |
|---|---|---|
| ANACK 晚 ~15ms | `qsh-debug §5/§7`、`qsh-test-compare §5`、两个新条目 | ✅ 全部删除线 + 撤回理由（deferred 打印延迟 + `ret=0` 与 `return -EIO` 矛盾） |
| 驱动同步复查窗口过窄 | 同上 | ✅ 明确"依据不成立 / 两侧归因均无据，回到未定" |
| deferred ⇒ 生成时刻不可定 | `qsh-debug:258-261`、`qsh-test-compare:298-300`、新条目 §根因 1、INBOX G-O39-1 | ✅ 已收紧为"位置不可信、时间戳 = 生成时刻"，且**显式标注旧版"过头了"** |
| 硬件不具备报错能力 | `OPEN_ITEMS` O-39（本子仓外）；本子仓内 `qsh-debug §5` 表述为"**两侧归因都无据**" | ✅ 未复述该过宽推论 |
| `check_archive.py` 只是口径误报 | `qsh-test-run §4.2`、INBOX G-O35-1 | ✅ 已补"崩溃 rc=1 与 BAD 同码 ⇒ 退出码四分"，并注明脚本已修 |

### 1.3 ★ 本次发现并已就地更正的残留（2 处）

**问题**：`qsh-test-run §4.3` 与 `qsh-test-compare §4` 的 O-40 批注仍按**缺陷未修**口径写
（"屏蔽名单**未收** `ensure_debugserver_alive` ⇒ **可能真探** 1025 / 重启 DebugServer
（既有缺陷、非本次引入）"），而它们自己指向的 `OPEN_ITEMS O-40` 已记
**"✅ 已修（2026-09-24）：洞不止一个，且加了枚举自检"**。

**代码核实（读主仓，未改）**——`regression/replay.py` 中：
- `_ACQ_...` 归集表（`:310`）**已含** `ensure_debugserver_alive`（原先 `grep -c` = 0）；
- `_assert_isolation_net()` 存在（`:375`，**import 期调用** `:488`），`_ACQ_DS_STUB_MSG = "<replay: stubbed>"`（`:350`）。

⇒ 文档落后于代码现实，属旧口径残留。

**更正（2 处，已改）**：
1. `lib/qsh/skills/qsh-test-run/SKILL.md:339-343`（原 4 行 → 7 行）：改为"**曾漏收** ⇒ 当时**会**真探板子
   （一票否决的真身）… **✅ 已修**：枚举 + 自动收编的 fail-closed 自检 …**但仍别把 `guard_demo` 自检 PASS
   当成『补丁打严了』**（`FORBIDDEN` 手写 7 条、不含该名）"。
2. `lib/qsh/skills/qsh-test-compare/SKILL.md:202-204`（4 行，行数不变）：同口径更正。

**保留项**：两处的**核心告诫不变** —— "离线 ≠ 绝对不碰板子"、"`guard_demo` 自检 PASS 不构成无洞证据"。
`guard_demo.py:46` `FORBIDDEN` 实测确为 **7 条**且不含该名，该断言成立。

---

## 2. 10 项之间的口径自洽（审核重点 ②）

### 2.1 O-39 一致性 ✅

`qsh-debug §5`（`:262-266`）与 `qsh-test-compare §5`（`:302-306`）对 O-39 的表述一致：
"那条日志与窗口内那笔读无关、属已消失树中的操作；**结构性推断非观测**；裁定权在驱动侧"。
与 `OPEN_ITEMS O-39` 右栏逐点吻合。

**证据抽查（读主仓源码）**：`zephyr/drivers/i3c/i3c_linkedsemi.c` 的
`:1161 / :1304 / :1761 / :2364 / :2454` **五行全部**确为 `ls_i3c_log_err_type(dev);`，
该函数定义在 `:2302`、`LOG_ERR("%s: Address NACK")` 在 `:2308`。
⇒ 文档中"5/5 调用点行号经复核全部对上""日志唯一产地"**属实**。

### 2.2 ⚠️ 一处口径歧义（**未改**，提请裁定）

同一 case 的**通过状态**在两处口径不同：

| 出处 | 说法 |
|---|---|
| `qsh-debug §7`（:384） | "`broadcast_nack_recovery` **恒 FAIL**、`reason = rstact_whole_should_nack`" |
| `OPEN_ITEMS O-39` | "曾长期 FAIL（**已按 B2 处置并转 PASS 11/11**）" |

**核实**（读主仓归档）：
- `reports/2026-09-24-1/i3c_i2c_switch_test_case_switch_core/i3c_i2c_switch_test_report.md:28`
  → `✅ PASS（判定=token）`，reason 已注明"原「应 NACK」判据不可达"；
- 同会话 `full-regression-2026-09-24.md:68` → `FAIL | 10/11 | 预期内`。

⇒ **两者并不矛盾，是两套判据口径**：批量汇总走 ztest 原生逐项（FAIL），case 报告走 B2 token 定向（PASS）。
`qsh-debug §7` 那行是**今天新加的**速查行，读起来像"当前仍恒 FAIL"，与 O-39 措辞对不上。
**建议**（不强制）：在该行加半句"，**口径**：ztest 原生 = FAIL 10/11；B2 token 定向 = PASS"。
因涉及判据口径而非事实错误，且裁定权在用户，**未自行改**。

### 2.3 新条目 ↔ `qsh-debug §5` ✅

`deferred-log-timestamp-not-generation-time.md` 的标题、§根因 1、§处理与 `qsh-debug §5:258-261`
**逐点一致**（含源码路径 `log_msg.h:375 → log_msg.c:51 → log_core.c:690`、"位置/时间戳/来源三件独立的事"）。
且 INBOX `G-O39-1` 明确记录"已改口径"，三处同步。

---

## 3. 链接层 / 索引 / 交叉引用 / 预算（审核重点 ③）

### 3.1 链接层 ✅

- `skills/` 下 **13 条符号链接全部可解析**（逐条 `-e "$l/SKILL.md"` 通过）；
- 13 条**全部指向 `lib/` 真身**（`lib/core/skills/` 6 条 + `lib/qsh/skills/` 7 条），**无镜像分叉**；
- `lib/*/skills/*/` 目录数 = **13**，与链接层**一一对应**；
- 本次**未增删改名 skill**（新增文件在 `lib/core/skills/debug-knowledge/knowledge/common/` 下，
  属已有 skill 的**知识条目**，不改变加载集）⇒ **无需重跑 `bootstrap.py`**。

### 3.2 新增条目的索引 ✅

`lib/core/skills/debug-knowledge/knowledge/common/`：**11 个条目文件 ↔ README 表 11 行**，
一一对应、不多不少。两条新条目**均已登记**：
- `window-probe-placement-and-negative-control.md`（README `:16`）
- `deferred-log-timestamp-not-generation-time.md`（README `:22`）

### 3.3 交叉引用完整性 ✅（逐条实测存在）

| 引用 | 结果 |
|---|---|
| `reports/2026-09-24-1/` 会话目录 | 存在，且**是 `<date>-1`**（符合"非 -1 不可得"约束） |
| `o39-nack-log-origin.md` / `o35-hw-discriminator.md` / `i3c-b1b2-experiment.md` / `i3c-erff-experiment.md` / `board-2026-09-24.md` / `i3c-b1b2-probe-run6.log` / `-run7.log` | **全部存在** |
| `scripts/check_archive.py` / `scan_pipes.py` / `guard_demo.py` / `ab.py` / `equiv_before/` / `before/` | **全部存在**（`tmp/` → `scripts/` 的迁移已完成，文档已同步改口） |
| `KNOWN_ISSUES` #112 / #115 / #116 / #117 / #118 / #98 | **全部存在**（#115/#118 为 `NNN.` 编号条目，非 `#NNN` 形式） |
| `projects/i3c_i2c_switch_test/tests/COVERAGE.md`、`i3c_dualcore_test/.../i3c_controller_test.c` | 存在 |
| `embedded-debug` §3.1 / §3.3 / §3.4 / §4 | 节号存在且语义对得上 |
| `qsh-test-gen §1.6 第 4 条 (g)/(h)`、`第 10 条` | 条目存在（编号见 §5 备注） |

`qsh-test-validate §1.10x` 的"**修复后正例不再命中**"经源码核实属实：
`i3c_i2c_controller_test.c:623` / `:700` 确在结账前调了 `i3c_do_daa()`。

### 3.4 token 预算 / 行数 ✅

- 各层 `CLAUDE.md`：库根 `CLAUDE.md` **119 行** ≤ 200 ✅（`ENVIRONMENTS.md` 134 / `DOC_EDIT_RULES.md` 303
  等为 L2 按需文件，无硬上限）。
- 被改 skill 行数：`qsh-test-run` 440→**443**、`qsh-test-compare` 356→**357**、`qsh-debug` **420**、
  `qsh-test-gen` **614**、`qsh-test-validate` **273**、`qsh-driver-review` **359**。
  按 §2.4"效率与可持续性优先、不以行数为准"，这些是操作手册型 skill，**未拆 ref 是正确的**：
  新增内容全部落在**高频路径**上（生成期判据、执行期判据、速查表行），拆分只会增加往返读文件。

---

## 4. 有没有"不该进库"的东西 ✅ 无

- 无绝对路径（`D:/` / `C:\` / `/d/zephyr`）新增；无串口号/内网地址/凭据。
- 无 `__pycache__` / 构建产物 / 临时文件混入（`?` 仅 2 个新 `.md`）。
- 新条目引用一律用**工程相对路径**（`reports/…`、`scripts/…`），符合可迁移要求。
- 一次性事故均已按 §6.1.6 只留在 `reports/` 存档，未进 skill。

---

## 5. 可提交性（审核重点 ⑤）

- **无半成品**：无 `TODO`/`TBD`/占位符/写到一半的段落；匹配到的 `X.XXX` 均为 ztest 时间格式，非占位符。
- 一处**既有**（非本次引入）的排版小瑕：`qsh-test-gen §1.6` 条目顺序为 `…7, 9, 8, 10`（9 与 8 互换了位置）。
  编号唯一且引用（"第 4/8/10 条"）均可解析，**不影响可提交性**，未动。
- 本次审核**新增 2 处更正**（§1.3），已落盘；改动前已确认与主仓代码现实一致。

## 6. 本次改动清单（keeper 所改）

| 文件 | 行 | 变更 | 行数 |
|---|---|---|---|
| `lib/qsh/skills/qsh-test-run/SKILL.md` | 339-343 | O-40 口径：未修 → **已修（枚举+自动收编）**，保留 `guard_demo` 告诫 | 440 → 443 |
| `lib/qsh/skills/qsh-test-compare/SKILL.md` | 202-204 | 同上口径更正 | 356 → 357 |

> 除上表外**未改动任何文件**；未执行任何 `git` 写操作；未碰主仓（仅读）。
