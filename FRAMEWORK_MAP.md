# FRAMEWORK_MAP — 810 测试框架全景索引

> **本文件是索引，不是副本。** 每条只给**指针**（文件 + 章节号），不复制内容——复制会立刻漂移。
> 内容真源永远是 `SPEC_REFERENCE.md` / `KNOWN_ISSUES.md` / 各 `SKILL.md`；本文件只回答
> 「现在有什么 / 在哪 / 怎么用 / 改完要验什么」。
>
> **同步纪律**：框架分层或接口变更时更新本文件；判定语义/冻结合同变更时**不改**本文件，
> 只改 `SPEC_REFERENCE.md`（见 §6 维护规则）。

---

## 1. 分层一页图（L3 编排 → L1 采集 → L0 归档 → L2 处理）

```text
固件侧（板上）  ztest 固件（D1 原生 token） / 命令通道固件（D2 自定前缀 token）
                从核 --(RLOG / mbox)--> 主核 单出口 ......... SPEC_REFERENCE §6.9 / §6.11.5-6.11.6
                记账模块 common/test_results.{c,h} + common/ztest_glue.{c,h}
                                   │  双路 UART 原始行
L3  编排   regression/run_all.py   分配会话目录 / 预检 / 擦除 / 逐 case / summary
       │    regression/precheck/run_precheck.py   链路级环境预检（串口+DebugServer+双核 load）
       │    调 (case, board, tgt_uart, ctrl_uart, timeout, session)
       ▼
L1  采集   common/acquire.py       唯一碰板子：构建 → 烧录 → 串口采集 → 自愈 → 落 L0
       │    in: AcquireSpec 家族（SingleCap / PortsDict / DualRun / Mbox / IwdgSpec）
       │    out: Acquired { lines, session, case_dir, facts, mem, ok/err/phase, rc }
       │    ★ L0 契约
       ▼
L0  归档   common/serial_archive.py   serial_CTRL.log + serial_TGT.log + replay_meta.json
       │    （判定/渲染标准同层：common/judge.py + common/ztest_report.py）
       │    纯数据（无时间轴、无内存内容）
       ▼
L2  处理   common/process.py       run() / process_archive() / read_archive()
           每工程唯一接入点 = 模块级 l2_spec() -> ProcessSpec（judge / console /
           render_md / render_json / summary 五个钩子；家族差异只放钩子）
       ▼
报告  reports/<YYYY-MM-DD>-<idx>/<project>_<case>/<driver>_test_report.{md,json}
      + 会话级 summary.md ................. SPEC_REFERENCE §6.4.1-§6.4.4（common/report_dir.py）

离线旁路（不碰板子）
  regression/replay.py     读 L0 归档 → 喂回同一条 L2（主路径猴补 L1 / --l2-direct 直调）
                           SPEC_REFERENCE §6.4.7 / §6.4.8 / §6.10.1
                           ⚠️ "不碰板子"有已知漏洞（PORTS_DICT 族 11 工程 / OPEN_ITEMS O-40）
  common/expect_check.py   读 COVERAGE.md 的「独立预期」段 → 校验归档 INFO 实测值
                           （独立预期；文法/判定/两条纪律 → SPEC_REFERENCE §6.5.6）
```

### 层职责与文件表

| 层 | 文件 | 接口（入口） | 能否离线验 |
|---|---|---|---|
| **L3 编排** | `regression/run_all.py` | CLI：`--no-precheck` / `--no-erase` / `--dry-run` / `--cases-file` | 否（编排，含擦除/会话分配） |
| L3 预检 | `regression/precheck/run_precheck.py` + `precheck/fw/{cpu1_target,cpu2_controller}` | CLI：`--session-dir` / `--timeout` | 否（上板） |
| **L1 采集** | `common/acquire.py`（5 策略 / 23 工程） | `single_cap` / `capture_case` / `run_case_dual` / `run_case_mbox` / `run_pass_iwdg`；`AcquireSpec` 家族 | **否**（时序） |
| **L0 归档** | `common/serial_archive.py` | `archive_serial()` / `read_serial_log()` / `read_meta()`；`ARCHIVE_VERSION(_SPLIT)=2` | **是**（往返契约） |
| L0 判定/渲染标准 | `common/judge.py`、`common/ztest_report.py` | `parse_ztest` / `strip_line` / `parse_kv`；judge 谓词族 | **是** |
| **L2 处理** | `common/process.py` | `run()` / `process_archive()` / `read_archive()`；`ProcessSpec` + `l2_spec()` | **是**（重放） |
| L2 会话索引 | `common/report_dir.py` | `allocate_session_dir()` / `update_summary()` | 是（有单测） |
| L2 独立预期 | `common/expect_check.py`（+ `test_expect_check.py`） | `audit()` / `check_case()`；CLI `--audit` / `--session` / `--case` / `--case-dir` | 是（有单测） |
| **离线重放** | `regression/replay.py` | CLI 见 §3 | — （入口本身） |
| 契约（固件侧） | `common/rlog.{c,h}`（单出口）、`test_results.{c,h}`、`ztest_glue.{c,h}`、`common.conf` / `*.overlay` / `common_sources.cmake` | 见 `common_sources.cmake`（公共模块注入唯一入口） | 否 |
| 契约（清单） | `regression/cases.yaml`（**只列工程**）+ `projects/<工程>/tests/cases.yaml`（该工程的 case）| 两级清单，run_all 展开；缺工程侧文件即报错 | — |

**设计口径**：家族/工程差异一律走 **spec 字段 + 钩子**，禁止在 `acquire.py` / `process.py`
里写 `if project == …`（否则等于把 23 份分叉搬进一个文件）。理由与边界见
`SPEC_REFERENCE.md` §6.4.8「家族差异怎么放」+ §6.4.10「为什么不是"一个通用采集脚本"」。

---

## 2. 改哪层要验什么（本会话核心产出）

> 真源 = `SPEC_REFERENCE.md` **§6.4.9 ⑦**（改处理层能否不碰板子）与 **§6.4.10 末表**
> （改动可只抽样验 vs 必须全量）。下表是**速查指针**，判定语义以真源为准。

| 改动落点 | 证据强度 | 必做的验证 | 真源 |
|---|---|---|---|
| L2 判定/渲染：`common/process.py`、`common/judge.py`、`common/ztest_report.py`、各工程 `l2_spec` 钩子 | **可离线证** | `replay.py --l2-direct` + 三档证据（合成输入穷举 / 全量归档重放 / 家族 byte-compare） | §6.4.9 ①②③⑦ |
| `main()` 里 **L1→L2 接线段**（`lines_by_core` / `archive_serial` / `process.run`） | **可离线证** | 重放主路径（猴补丁 L1） | §6.4.9 ⑦ |
| L0 归档格式 `common/serial_archive.py` | 可离线证 | 往返契约 `read(write(x)) == x` | §6.4.9 ① |
| **L1 采集**：`common/acquire.py`（命令序列 / 参数 / 等待时长 / 自愈 / 两遍烧录） | **必须全量上板** | 静态等价比对先做离线筛查（E1/E1b + 负对照 E3），再抽样 A/B 上板 | §6.4.10 末表 + A2 自证 |
| `AcquireSpec` 字段**值**（端口 / 超时 / banner / src_dir） | 抽样可证 | 字面量集合离线比对 | §6.4.10 末表 |
| **mem 兜底**（`dump_mem` / `memory_backfill`） | **必须上板**（或先扩 L0 契约） | 内存 payload 未归档 ⇒ 离线恒 `None`；**批量路径已不调用**（2026-09-22 用户裁定：批量稳定优先、只认 token；仅 `dbg_run.py` / 人工用，§6.4.10） | §6.4.8 边界 1 / §6.4.9 ① |
| **固件 `.c/.h`**（含 token / RLOG 出口） | **必须全量重跑** | 输出会变 | §6.4.9 ①⑦ |
| 环境预检 / 擦除 / DebugServer | 必须上板 | — | §6.4.9 ① |

**确定性断言（可被上表推翻）**：只要改动不落到「L1 采集段 / mem 兜底 / 固件」三者之一，
就不需要上板（§6.4.9 ⑦）。一键离线复跑：`bash tmp/a1/verify_all.sh`（A1 —— ⚠️ 自述与实测不符，见下注）、`tmp/a2/`（A2）。

> ℹ️ **`tmp/a2/` 不是"一键离线"入口（2026-09-24 核实）**：该目录**没有单一入口脚本**（无 `verify_all` /
> `run_all` 之类）。**纯离线四件套已于 2026-09-24 迁入 `scripts/`** —— `equiv.py`（静态等价）/
> `l1survey.py`（现状盘点）/ `dump.py` / `normgroup.py`（+ 快照 `scripts/equiv_before/`），须**逐个点名**运行；
> **未迁的 `tmp/a2/hw_cmp.py` 自述"抽样上板"**。**勿把 `tmp/a2/` 当作离线一键复跑。**

> ⚠️ **"自述"两个方向都别信（2026-09-24）**：`tmp/a2/hw_cmp.py` 自述"抽样上板"，但**脚本体零硬件调用**
> （只读两个目录）⇒ 自述**夸大**；`tmp/a1/harness.py` / `verify_all.sh` 自述"离线不碰板子"却**会落到
> 真硬件入口** ⇒ 自述**缩小**。**判据看代码与实测，不看文件头自述。**

> ⚠️ **以下 `tmp/` 脚本**不要搬运、也不要 `import`（2026-09-24 核实）：
> **`tmp/a2/patch_equiv{1..4}.py` 无 `__main__` 守卫** —— 顶层**直接**调 `common/acquire.py` 与
> `equiv.py` 的 `write_text`，路径是 cwd 相对 ⇒ **运行、甚至只是被误 `import`，就会改写公共源码**
> （同类：`insert_spec.py` / `apply_shells.py`，一次性 + 破坏性）。**碰硬件（一票否决、勿复跑）**：
> `tmp/a1/{verify_all.sh, abrun.py, harness.py, coverage.py}`；**依赖被否模块**：`tmp/a1/synthgen.py`
> （`import harness`）。

> ⚠️ **`tmp/a1/verify_all.sh` 的"离线复跑"自述与实测不符（2026-09-24）** —— 其 **`after` 子树会走到
> 真硬件入口**（PORTS_DICT 族工程触发 `DebugServer 前置检查` / `CAPTURE FAILED`，路径是通的）；且其
> 第 7 步输入 `reports/2026-09-20-4` **已不可得** ⇒ **勿据其自述在无人看管时运行**。该三件套属**未迁**
> 的 `tmp/` 线束；详见 `SPEC_REFERENCE` §6.4.9 ⑥ 注 · `reports/2026-09-24-1/scripts-promotion-2026-09-24.md` §2.1/§4.2。

> ⚠️ **"离线重放"本身也有已知洞（2026-09-24）**：隔离网漏收 `ensure_debugserver_alive` ⇒ **PORTS_DICT
> 族 11 个工程**重放**可能真探 1025 / 重启 DebugServer**；**自检 PASS ≠ 无洞**。见 `OPEN_ITEMS` **O-40**。

**抽样面选取**（确实要抽样时）：按"被改动触及的公共代码路径"选，不按工程选；7 判定家族各 ≥1、
5 个 L1 适配器组各 ≥1、单双核各 ≥1、异常路径（mem 兜底 / 零 token / truncated / 标记矛盾）各 ≥1
⇒ 约 10~12 个 case。真源 §6.4.9 ②。

---

## 3. 常用入口

> **★ 结果作废判据**：任一日志出现 `error 138` 或 `DebuggerServer quit` ⇒ **该批次后续 case 结果整段作废**（CKLink 瞬时故障致 DebugServer 自杀；探针仍在 PnP 枚举中，**非**物理掉线）。真源 `qsh-debug` §2.3a · `qsh-test-run` §4.1 + §6。

> **污染会话目录的识别与处置**：某会话目录若出现**同一 case 的重复产物**、`logs/` 与 `summary.md`
> 计数对不上、或**同一日志被两进程交替写入**（行乱序/交错）⇒ 疑为**双实例并发**（起后台任务时
> 相对路径误判 → 重复启动，见 `qsh-test-run` §4.1 G-E2-2）。处置：在该目录打 `POLLUTED.md` 标记、
> **不可作为证据引用**，重跑取干净会话。实例：`reports/2026-09-21-7` / `-8`
> （有效替代 = 同批 `-9` / `-10` / `-11` / `-12`，E-2 A/B 实测证据）。

> **★ 离线重放必加 `--l2-direct`**（仅 `--list` / `--render-from-json` 可不加）：不加即走工程
> `main()`（含 DebugServer 前置检查）⇒ **不是离线**、DS 未起时每 case 白等自愈超时（实测 51.7s）
> 且 **0 产出**（表现为「跑很久、目录是空的」）。真源 `SPEC_REFERENCE.md` §6.4.8 ·
> `qsh-test-compare` §4。

| 目的 | 命令 | 真源 |
|---|---|---|
| **跑全量回归** | `python regression/run_all.py`（`--no-precheck` / `--no-erase` / `--dry-run` / `--cases-file X`） | §6.4.5 · `qsh-test-run` §4.1 |
| 链路级预检 | `python regression/precheck/run_precheck.py [--session-dir d] [--timeout 40]` | §6.4.6 |
| 跑单工程 | `cd projects/<p>/tests && python run_test.py [--case case_x]` | §6.4.8 · `qsh-test-run` §4.2 |
| 跑单 case（调试） | `cd projects/<p>/tests && python scripts/dbg_run.py --case case_x --timeout 35` | `qsh-test-run` §3 |
| **离线重放**（⚠️ "不碰板子"有洞：PORTS_DICT 族 11 工程 → `OPEN_ITEMS` O-40） | `python regression/replay.py --session reports/<d> [--list] [--case X] [--project P --limit N] --l2-direct★ [--render-from-json] [--freeze-time]` | §6.4.7 · §6.4.8 · §6.10.1 · `qsh-test-compare` §4 |
| **查/审独立预期** | `python common/expect_check.py --session reports/<d>`（`--audit` 全库覆盖率，`--json`） | §6.5.6 |
| 查某工程预期 | `projects/<p>/tests/COVERAGE.md` 的 `## 独立预期（机器可读）` 段 | §6.5 |
| **查坑** | `KNOWN_ISSUES.md`（按域分节） | — |
| **查未决/待拍板** | `OPEN_ITEMS.md`（开工前必看；每轮回复末尾附未决清单） | — |
| 查判定口径 | `SPEC_REFERENCE.md` §6.2.4 冻结总表 | §6.2.4 |
| 框架改动前的 skill 链 | `.claude/skills/skill-orchestrator/SKILL.md` | — |

---

## 4. 文档地图（想查 X 看哪）

| 想查 | 看 |
|---|---|
| 板卡 / 接线 / COM / DebugServer | `HARDWARE_SETUP.md`（**硬件唯一源**）；`CLAUDE.md` §1（速查）；`README.md` §4（外部接线登记） |
| 工程清单 / 启用 case / 单双核 | `ls projects/` · `regression/cases.yaml`（只列工程）· `projects/<工程>/tests/cases.yaml` · `cpu*` 子目录数（2=双核） |
| 固件编写规范 / ztest 契约 | `SPEC_REFERENCE.md` §6.1；`qsh-test-gen` §1.4 |
| 判定标准（各族 / 各状态） | `SPEC_REFERENCE.md` §6.2.4（**唯一真源**，G1-G10 + R1-R8）；速查 → `qsh-test-compare` §2 / `qsh-test-run` §5 |
| 串口无输出兜底（GDB 读内存） | `SPEC_REFERENCE.md` §6.2.2 / §6.4.3；`qsh-test-compare` §3 |
| 报告归档 / 会话目录 / summary | `SPEC_REFERENCE.md` §6.4.1-§6.4.4 |
| **分层 / L1-L2 契约 / 重放** | `SPEC_REFERENCE.md` §6.4.7 / §6.4.8 / §6.4.10 / §6.10.1 |
| **改完要验什么** | `SPEC_REFERENCE.md` §6.4.9（§2 本文件速查） |
| 固件日志 token 契约（D1/D2 / 单出口） | `SPEC_REFERENCE.md` §6.9 / §6.11.5 / §6.11.6 |
| 覆盖矩阵 / 预期数据 / `expect` 块 | `SPEC_REFERENCE.md` §6.5 / **§6.5.6（独立预期文法）**；各 `projects/<p>/tests/COVERAGE.md` |
| 外部环境确认（接线前置） | `SPEC_REFERENCE.md` §6.6 |
| XIP / flash 固化 | `SPEC_REFERENCE.md` §6.7；`qsh-test-run` §8；`qsh-debug/ref-flash-variants.md` |
| Supervisor / runner 运营 | `SPEC_REFERENCE.md` §6.8；`.claude/agents/runner.md` + `runner-qsh-routing.md` |
| 管道 / 转义 / 新鲜度 / 烧录窗口（B2-fix） | `SPEC_REFERENCE.md` §6.11 |
| 新建工程 / case | `qsh-test-gen`；生成后 → `qsh-test-validate` |
| 构建 / 烧录 / 采集操作 | `qsh-test-run`；连不上 → `qsh-debug` |
| 驱动代码审核（mutex 泄漏 / 中断安全…） | `qsh-driver-review` |
| 调试方法学 / 领域招式 | `embedded-debug` / `debug-knowledge` |
| skill 库结构与加载机制 | `.claude/CLAUDE.md`；清单 `.claude/TOOLS_AND_SKILLS.md` |
| 文档编辑规范 | `.claude/DOC_EDIT_RULES.md` |
| 历史盘点 / 一次性档案 | `DEFECT_ANALYSIS_2026-09-17.md`、`TRIAGE_*.md`（**不入规范，只备查**） |

**skill 覆盖速查**（现行章节见各 `SKILL.md`）：`qsh-test-gen`(§1 建工程 / §2 建 case / §3 自检) ·
`qsh-test-run`(§4 回归入口 / §5 判定速查 / §8 XIP) · `qsh-test-compare`(§2 判定 / §3 mem 兜底 / §4 报告) ·
`qsh-test-validate`(§1 检查维度 / §1.10 生成期静态检查) · `qsh-debug`(§0-§9) · `qsh-driver-review`(§1 审核维度)。

---

## 5. 待办 / 已知缺口（本索引维护时登记）

| ID | 缺口 | 状态 |
|---|---|---|
| G-1 | `common/expect_check.py` 与 COVERAGE.md 的 ```expect``` 块格式**在 SPEC 中无章节**；`SPEC_REFERENCE.md` §6.4.9 仍写「G-C 独立预期真源 ❌ 未做」（该缺口事后已由 expect_check 落地） | ✅ **已补**（2026-09-21）：新增 `SPEC_REFERENCE.md` **§6.5.6**（文法 / op 表 / 三档标签 / 两条纪律 / 用法 / 判定标记）；§6.4.9 ⑤ G-C 改「已落地」并保留缺口量化 |
| G-2 | skill 未覆盖分层/重放/独立预期（`qsh-test-gen` / `qsh-test-run` / `qsh-test-compare` / `skill-orchestrator` 均未提 `acquire.py` / `process.py` / `replay.py` / `l2_spec`） | ✅ **已补**（2026-09-21）：`qsh-test-gen` §1.1/§1.6 分层指针 + run_test.py 薄壳；`qsh-test-compare` §4 / `qsh-test-validate` §1.10w / `skill-orchestrator` §2 各补指针 |
| G-3 | `KNOWN_ISSUES.md` 编号冲突：#38/#39/#40 各出现两次（模板缺陷 vs 脚本问题），#22 缺失 | ⏳ **方案已出、待裁定**（2026-09-21，勿直接改号）：见下 |
| G-4 | 24 份 `COVERAGE.md` 的「独立预期」段头仍只指向 `common/expect_check.py` 文件头，未指向新规范 `SPEC_REFERENCE.md §6.5.6`（两处都描述文法 ⇒ 有漂移风险） | ✅ **已补**（2026-09-21）：24/24 段头各加一行指针指向 §6.5.6（只加指针，段内未动） |

**G-3 映射（待父会话裁定）**：两个 #38/#39/#40 分属两组，**改号只动少引用的一组**（最小改动 = 续编 #104 起，历史号不变）：

| 现号 | 组（所在节） | 内容 | 外部引用 |
|---|---|---|---|
| #38 | **固件与构建**（模板缺陷，L98） | 新工程脚本漏按住 cpu2 | 多（CLAUDE.md §5、§6.4.9 ⑤ 邻域、qsh-debug §7、debug-knowledge×2、INBOX×2）⇒ **保留** |
| #39 | 固件与构建（模板缺陷，L113） | chosen 节点丢失 | 同上组引用 ⇒ 保留 |
| #40 | 固件与构建（模板缺陷，L120） | k_malloc 崩溃 | 同上组引用 ⇒ 保留 |
| #38 | **环境与并发**（L464） | `allocate_session_dir` 缺 `date_str` | 无 ⇒ 建议改 **#104** |
| #39 | 环境与并发（L469） | DebugServer/CKLink 被残留 GDB 占用 | 无 ⇒ 建议改 **#105** |
| #40 | 环境与并发（L474） | 串口被残留 Python 占用 | 1 处（`KNOWN_ISSUES.md:346`）⇒ 建议改 **#106** |

### 框架级残留（F 系列，2026-09-21 登记 → **同日全部收口**）

> E-2（每 case 前置 DS 存活检查）落地时**顺带发现**的框架缺口。
> 编号用 F- 前缀（避开 G-3 待裁定的 `#104+` 号段）。

| ID | 发现 | 位置 | 状态 |
|---|---|---|---|
| F1 | `dbg_run.py` **无 DebugServer 自愈** —— 单 case 调试入口，DS 死了只能 `error 138` | `projects/*/tests/scripts/dbg_run.py`（**23 份**，`mbox_test` 无此文件） | ✅ **已修** —— 新增薄适配 `common/acquire.py::dbg_run_preflight()`（判活/自愈**零复制**，只翻译结果：健康⇒静默 / 发生过自愈⇒留一行 / **起不来⇒明确报错 + `sys.exit(3)`**）。每份仅 **1 行 import + 1 行调用**。`--xip` 路径**刻意不加**（XIP 走 `flash_program.py` 独占 CKLink，DS 本就不该被占）。<br>**已知代价**：CKLink 真死时从"1 秒 `error 138`"变成"走完 2 轮自愈阶梯才 fail-closed（最坏 ~100 s）"—— fail-closed 的必然成本 |
| F2 | `PortsDictSpec.clean_before` **死字段**（声明了从未接线） | `common/acquire.py` | ✅ **已删除（不是接线）** —— ① 该意图已被 E-2 的**每 case 前置**严格覆盖；② **接线会倒退 E-2 的设计性质**（健康路径**零副作用**、实测 24 ms/case，而"入口无条件清残留"正是它刻意去掉的行为）；③ 10 份 `PortsDictSpec(...)` **全为关键字构造** ⇒ 删字段无位置漂移风险 |
| F3 | `report_dir.update_summary` **尾行把 `ENV` 算成失败** | `common/report_dir.py` | ✅ **已修** —— 尾行改三分计数（`PASS` / `FAIL` / `ENV-ABORT`），与 `run_all.py` 控制台口径一致。**向后兼容逐字节证明**：真实会话 `-10`/`-11` 用新渲染器重写与原文件**逐字节相同** |
| F4 | `restart_debugserver` 的 `"z"` 分支 `Popen` **无 try/except** | `common/acquire.py` | ✅ **已修** —— 按 `"d3"` 同构补包裹，失败**返回 `False`** 而非异常穿透。<br>**同文件其余 6 处 `Popen` 刻意不改**：那是"启动外部工具"的站点，**抛异常是期望的响亮失败**（且 Popen 抛异常 ⇒ 进程未起 ⇒ 无残留要清），与"自愈工具自己失败"性质不同 |

> ⚠️ **两处未上板验证（如实记录）**：
> - **F1** 只在 `trng_test` 与 `i2cf_test` 上板跑过；**双核族**（`i3c_dualcore_test` / `app_iwdg_cpu2_test`）的 `dbg_run.py` **未上板**
> - **F3** 的**端到端写盘**未上板（需一次真回归才会走到；已用真实会话离线证 byte-identical）

---

## 6. 维护规则（单一信息源）

- 本文件**只放指针**。任何表格里的"值"（超时、家族数、文件清单）若与真源冲突，以真源为准并更新本文件。
- 分层/接口变更 ⇒ 更新本文件 §1-§3；判定语义变更 ⇒ **只**改 `SPEC_REFERENCE.md` §6.2.4（冻结表，须用户显式裁定触发）。
- 新增 skill / 改 skill 章节 ⇒ 更新 §4 的 skill 覆盖速查行。
- 编辑本文件前读 `.claude/DOC_EDIT_RULES.md`。
