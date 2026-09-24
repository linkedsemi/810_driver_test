# O-40：`regression/replay.py` 硬件隔离网漏洞 —— 修复报告

- **日期**：2026-09-24
- **范围**：**纯代码离线**。未碰硬件、未碰构建目录、未执行任何可能触达板子的路径。
- **改动文件**：
  - `regression/replay.py`（隔离网名单 + 新增一致性自检，+166 / -7 行）
  - `scripts/guard_demo.py`（自检补 1 条：`ensure_debugserver_alive` 的签名/语义）
- **O-40 状态建议**：本项已按"**改成显式 fail-closed**"这一支修掉（见 §3）。
  `OPEN_ITEMS.md` / `SPEC_REFERENCE.md` §6.10.1 / `FRAMEWORK_MAP.md` 的相关条目
  **本次未动**（避免与并行任务撞写域），建议由 keeper 收口时翻转状态。

---

## 1. 枚举：`acquire.__all__` 全量分类（先枚举、再动手）

分类判据 = **逐个读函数体**，判它有没有 `open/popen/gdb/串口/taskkill/netstat/构建/读目标内存`。
**不看自述**（本项目已证实自述两个方向都不可信：`verify_all.sh` 夸大、`hw_cmp.py` 缩小）。

`acquire.__all__` 共 **44** 个名字，穷尽分类如下（自检断言：`__all__ == HW_ENTRIES | DRIVERS | PURE`）。

### 1.1 会碰硬件 —— 必须被接管（`_ACQ_HW_ENTRIES`，24 个）

| 名字 | 碰的是什么 | 现状（本次修复后） |
|---|---|---|
| `SerialCapture` | 开串口（pyserial） | 已覆盖（类替换 `_ForbiddenCapture` / `FakeCap`） |
| `reader` | 串口读线程体（`ser.read`） | **新补** → `_ACQ_FORBIDDEN` |
| `discover_ports` | 枚举串口设备 | 已覆盖（`lambda explicit=None: []`） |
| `find_tgt_uart` | 枚举串口设备 | 已覆盖（`lambda: "REPLAY_TGT"`） |
| `probe_gdb_alive` | 起 gdb 连 1025 | 已覆盖（`lambda: False`） |
| `netstat_1025` | 起 `netstat -ano` | 已覆盖（`lambda: []`） |
| `kill_residual_gdb` | `netstat`+`tasklist`+`taskkill` | 已覆盖（`_ACQ_SILENT`） |
| `restart_debugserver` | `taskkill /IM` + `Popen(DebugServerConsole.exe)` | 已覆盖（`_ACQ_SILENT`） |
| `clean_gdb_residue` | 同上（含重启分支） | 已覆盖（`_ACQ_SILENT`） |
| `preflight_gdb` | gdb 探活 + 清残留 + 重启 | 已覆盖（`_ACQ_SILENT`） |
| **`ensure_debugserver_alive`** | **netstat 1025 + 杀 gdb + 重启 DebugServer** | **新补（O-40 主洞）** → 专用替身，见 §2 |
| **`dbg_run_preflight`** | 上者的调试入口壳 + `sys.exit(3)` | **新补** → `_ACQ_SILENT` |
| `gdb_flash_once` | gdb 烧录 | 已覆盖（`_ACQ_FORBIDDEN`） |
| `gdb_watch_session` | gdb 交互会话烧录+采集 | 已覆盖（`_ACQ_FORBIDDEN`） |
| `spawn_test_run` | `Popen([bash, test.sh, -r])` | 已覆盖（`_ACQ_FORBIDDEN`） |
| `kill_proc_group` | `taskkill /T /F` | 已覆盖（`_ACQ_FORBIDDEN`） |
| `west_build` | 起 `west build` | 已覆盖（`_ACQ_FORBIDDEN`） |
| `build_via_test_sh` | 起 `test.sh` 构建 | 已覆盖（`_ACQ_FORBIDDEN`） |
| `memory_backfill` | `subprocess.run(dump_results.py)`→gdb 读目标内存 | 已覆盖（`lambda: None`） |
| `dump_mem` | 同上 | 已覆盖（`lambda: None`） |
| `run_case_shell` | 5 个 L1 采集入口 | 已覆盖（`_ACQ_CAPTURE_ENTRIES`，逐策略换替身） |
| `run_case_dual` | 同上 | 同上 |
| `run_case_mbox` | 同上 | 同上 |
| `run_pass_iwdg` | 同上 | 同上 |

### 1.2 策略入口 —— 重放**合法调用**，**不得**打替身（`_ACQ_DRIVERS`，6 个）

`single_cap` / `ports_dict` / `ports_dict_case` / `dual_run` / `mbox` / `iwdg`

> 24 份薄壳 `run_test.py:main()` 就是 `acquire.<策略>(spec)`。它们内部逐名调用
> §1.1 的硬件入口 ⇒ 硬件动作在**被调用点**被替换。把它们也禁掉 = 重放根本进不去。

### 1.3 不碰硬件（`_ACQ_PURE`，14 个）

`GDB` `GDB_HOST` `GDB_PORT` `DEBUGSERVER_EXE` `DS_HEALTHY_MSG`（常量）、
`log`（print）、`Acquired` / `SingleCapSpec` / `PortsDictSpec` / `DualRunSpec` /
`MboxSpec` / `IwdgSpec`（dataclass）、

- `archive` —— 只写磁盘（落 L0 归档），不起进程；写点由 `--out` 守卫约束（落 `reports/` 需显式 `--force`）。
- `find_git_bash` —— 纯路径解析（`Path.exists` / `shutil.which`）；**仍被显式置成 `"bash"`** 以保证重放确定性，故同时列在 `_ACQ_SINGLE`。

### 1.4 不在 `__all__`、但同样是硬件入口（`_ACQ_HW_EXTRA`，7 个）

| 名字 | 碰的是什么 | 现状 |
|---|---|---|
| `build_via_test_sh_raise` | 起 `test.sh` 构建（rc≠0 抛） | 已覆盖（`_ACQ_FORBIDDEN`） |
| `restart_debugserver_stable` | 重启 DebugServer + 5s | 已覆盖（`_ACQ_SILENT`） |
| `clean_gdb_residue_plain` | `taskkill /IM gdb` | 已覆盖（`_ACQ_SILENT`） |
| **`capture_pc_snapshot`** | **gdb attach 读 `$pc`/`bt`（会 halt 目标）** | **新补** → `_ACQ_FORBIDDEN` |
| **`_log_pc_snapshot`** | 上者的日志壳（0 输出兜底路径） | **新补** → `_ACQ_FORBIDDEN` |
| `_serial` | 返回 pyserial 模块（开串口的唯一入口） | 已覆盖（`_ACQ_FORBIDDEN`） |
| `capture_case` | PORTS_DICT 的烧录+采集入口（**未导出**） | 已覆盖（`_ACQ_CAPTURE_ENTRIES`） |

> 把"未导出入口"单独列一张表，是因为**私有名是绕过反向枚举的后门**：
> `capture_case` / `capture_pc_snapshot` 都不在 `__all__` 里，靠 `__all__` 枚举抓不到。

**合计**：`_ACQ_HW_ALL = HW_ENTRIES(24) + DRIVERS(6) + EXTRA(7) = 37`；`covered`（三张名单 ∪ 逐个打的）= **32**。

---

## 2. 新补的 5 个入口 + 替身签名 + 为什么

| 名字 | 进哪张名单 | 替身 | 返回 | 为什么是这个替身（读调用方**函数体**定的） |
|---|---|---|---|---|
| **`ensure_debugserver_alive`** | `_ACQ_SINGLE`（专用替身） | `_stub_debugserver_alive(style="d3", *, wait_s=None, attempts=None)` | **`(True, "<replay: stubbed>")`** | 见下方 ★ |
| `dbg_run_preflight` | `_ACQ_SILENT` | `_noop` | `None` | 它是 `ensure_..` 的薄壳（+`sys.exit(3)`）。没有调用方消费其返回值 ⇒ `_noop` 安全；且替掉它同时消掉 `sys.exit`。 |
| `capture_pc_snapshot` | `_ACQ_FORBIDDEN` | `_forbidden` | 抛 `HardwareTouchAttempt` | 会 gdb attach（halt 目标）。批量路径当前不调用它（§1.4），一旦有人接线就是绕过隔离网 ⇒ 应当**响亮中止**而不是静默返回 `None`（`None` 是它"抓取失败"的合法值，静默会掩盖接线错误）。 |
| `_log_pc_snapshot` | `_ACQ_FORBIDDEN` | `_forbidden` | 抛 | 同上（它是 `capture_pc_snapshot` 的调用者，禁掉被调者也一并禁掉调用者，避免"看起来打了补丁其实只是没抓到"。） |
| `reader` | `_ACQ_FORBIDDEN` | `_forbidden` | 抛 | 唯一调用点是 `capture_case`（已替换）⇒ 重放期不可达；登记为绊线属**纵深防御**：真被调用说明有路径漏网。 |

★ **`ensure_debugserver_alive` 为什么不能进 `_ACQ_SILENT` 用 `_noop` 兜**
（这是本任务点名的坑，也是"逐个核签名"的示范）：

1. 5 个策略**全部**这样写：
   ```python
   ds_ok, ds_msg = ensure_debugserver_alive(style="d3")
   if not ds_ok:
       return Acquired(ok=False, err=f"DebugServer unavailable: {ds_msg}")
   ```
2. 真身签名 `ensure_debugserver_alive(style="d3", *, wait_s=None, attempts=None)` → 返回 **`(ok, msg)` 二元组**。
   `_noop` 返回 `None` ⇒ **`TypeError: cannot unpack non-sequence NoneType`**（5 个策略全炸）。
3. 就算能解包，`ok=False` 会让每个 case 走 **fail-closed** 分支 ⇒ 全部变成
   `CAPTURE FAILED`，重放结论整体作废。
4. 替身返回串**刻意 ≠ `DS_HEALTHY_MSG`**：策略见此就打一行
   `[ENV] DebugServer 前置检查: <replay: stubbed>` —— **这行只可能由替身产生**，
   正好当"真函数没被调用"的运行期证据（见 §4.2 实测输出）。

---

## 3. ★ 一致性自检（本任务的真正产出）

### 3.1 为什么需要它（写进了代码注释）

三张名单是**手写**的 ⇒ **新加的硬件入口不会自动进网**。
`scripts/guard_demo.py` 的自检 **PASS 也不代表无洞** —— 它的名单同样是**手写 7 条**，
`ensure_debugserver_alive` 就活生生被它放过去了（本次改动前实测 guard_demo PASS，
而该函数在 11 个 PORTS_DICT 工程的**每 case 前置**里真探 1025）。
⇒ **自检的覆盖面取决于名单全不全，而名单本身没人校。**

### 3.2 实现（`regression/replay.py:375 _assert_isolation_net()`）

**反向枚举 `acquire.__all__`**，三条 fail-closed 断言：

| # | 断言 | 抓住什么 |
|---|---|---|
| ① | `(_ACQ_HW_ENTRIES ∪ _ACQ_HW_EXTRA) ⊆ (三张名单 ∪ 逐个打的)` | 硬件入口**没登记** ⇒ 报错并指名道姓 |
| ② | `set(acquire.__all__) == _ACQ_HW_ENTRIES ∪ _ACQ_DRIVERS ∪ _ACQ_PURE`（双向） | **新加入口忘了分类** ⇒ 报错（"自动收编"的关键：名单不会自己长大，但**枚举会**）；把非公开名误归进这三张 ⇒ 也报错 |
| ③ | `covered ∪ _ACQ_HW_ALL` 的每个名字 `hasattr(acquire, n)` | 名单在给**不存在**的函数打补丁（`hasattr` 守卫静默跳过 = **打了空补丁**） |

**跑两次**：
- **import 时一次**（`regression/replay.py:488`）—— 覆盖**所有**路径，包括完全绕开适配器的 `--l2-direct`；
- `_guard_acquire()` 开头再一次（`:443`）—— 防运行期改了名单却没改分类表。

⇒ 漏项时**本文件根本 import 不进来**（启动即报错），而不是"某个 case 才暴露"，更不是静默落到真硬件。

### 3.3 反例验证（放宽类/覆盖面类改动的必做项）

**反例 A：真改文件、验"启动即报错"** —— 临时把 `preflight_gdb` 从 `_ACQ_SILENT` 去掉：

```
$ python regression/replay.py --session reports/2026-09-24-1 --list
Traceback (most recent call last):
  File "...\regression\replay.py", line 398, in _assert_isolation_net
    raise RuntimeError(
RuntimeError: 硬件隔离网有洞（O-40）：以下名字会碰板子/调试器/串口/进程/构建，却没进任何
替身名单 => 重放会打到真硬件：preflight_gdb —— 请登记进 _ACQ_SILENT / _ACQ_FORBIDDEN /
_ACQ_CAPTURE_ENTRIES，或逐个打替身并加进 _ACQ_SINGLE。
EXIT=1
```
**验完已改回**（`diff -q` 与改动前备份**字节一致**，随后 `--list` 恢复正常）。

**反例 B~E（同进程内，避免动文件）**：

| 造的场景 | 结果 |
|---|---|
| 去掉 `preflight_gdb`（`_ACQ_SILENT`） | ✅ 启动即报错 |
| 去掉 `ensure_debugserver_alive`（`_ACQ_SINGLE`） | ✅ 启动即报错 |
| `__all__` 追一个未分类新入口 `new_hw_entry_x` | ✅ 报"分类表过期"（**自动收编能力**） |
| 名单塞幽灵名 `_clean_gdb_residue` | ✅ 报"名单里有 acquire 上不存在的名字" |
| 公开入口误归 / 私有入口漏登记 | ✅ 报"分类表与 `__all__` 不一致" |

**反例 F（自证不自证伪）**：新加的双向断言在本次工作中**当场抓出了我自己的一处误分类** ——
`capture_case`（**不在 `__all__`**）被我误放进 `_ACQ_HW_ENTRIES`，断言报
"以下名字…并不在 `__all__`"，遂改归 `_ACQ_HW_EXTRA`。⇒ 该检查**非空转**。

---

## 4. 正例验证：重放是否仍跑通

### 4.1 离线跑 9 个 case（覆盖 5 个策略家族，**带进程绊线**）

为保证"绝无硬件副作用"不是靠自述，给 `acquire.subprocess` 装了**会抛异常的绊线**、
并把 `acquire._serial` 换成 `AssertionError`：任何 `netstat / taskkill / taskkill /IM /
Popen(DebugServer) / gdb / bash` 或开串口都会当场炸。

```
命令：replay.py --session reports/2026-09-24-1 \
      --project {trng,otbn,rtc_ls,mbedtls,dma,eth,i3c_dualcore,i2c_pure,mbox,app_iwdg_cpu2}_test \
      --limit 1 --freeze-time --out tmp/replay/o40-final
结果：完成：重放 9 / 与归档逐字节相同 3 / 跳过 0     main() rc = 0
      绊线：0 次触发；每 case 0.0~0.1s（对照：DS 未起时真前置检查每个 case 白等 ~52s）
```
每个 case 都打出 `[ENV] DebugServer 前置检查: <replay: stubbed>` ⇒ **替身确实接管了 O-40 那个入口**。

### 4.2 替身不改产出（与纯 L2 路径逐字节比对）

同一批 case 再跑 `--l2-direct`（**完全绕开适配器与 `_guard_acquire`**），
两份产物逐字节比对：**9/9 一致**。

```
[一致] app_iwdg_cpu2_test_case_non_destructive   [一致] mbox_test_case_mbox_basic
[一致] dma_test_case_default                     [一致] otbn_test_case_default
[一致] i2c_pure_test_case_pure_i2c_400k          [一致] rtc_ls_test_case_default
[一致] i3c_dualcore_test_case_mixed_1m_12m5      [一致] trng_test_case_default
[一致] mbedtls_test_case_all_on_max
```
⇒ 新增的替身/自检**不改变任何报告内容**。与归档的少量差异（`- 环境:` 行 `timeout=30.0s` vs `30s` 等）
在 `--l2-direct` 路径上**同样存在** ⇒ 与隔离网无关、属既有的 argv 事实渲染差异。

### 4.3 两个既有判据脚本

- `python scripts/guard_demo.py` → **PASS**（并把 `ensure_debugserver_alive` 补进 C 段，
  断言替身返回 `(True, msg)`；实测输出 `-> (True, '<replay: stubbed>')  [签名/语义正确]`）。
- `python scripts/audit_patch_targets.py` → **PASS**。该脚本与我的名单**不共享数据**，
  用的是"reload acquire → 跑适配器 → 列出值变了的全局名 → 断言都原本存在"的口径：
  5 个策略各 **32** 个被替换名，**"原本不存在的（补丁打空风险）：无"**，
  且 `ensure_debugserver_alive` 现在**出现在被替换名单里**（修复前不在）。

---

## 5. 还发现哪些"名单漏项"

1. **幽灵名单项（最阴的一类）**：`_ACQ_SILENT` 里 `_clean_gdb_residue` / `_restart_debugserver`
   在 `acquire.py` 里 `def` 计数 = **0**；`_guard_acquire` 里还有一处
   `if hasattr(acq, "list_serial_ports")` 的防御性补丁，同名函数也不存在。
   ⇒ 三处都是"**以为打上了、其实静默跳过**"。已删除，并新增断言③兜住未来同类。
2. **`capture_pc_snapshot` / `_log_pc_snapshot`**：真硬件入口（gdb attach，会 halt 目标），
   三张名单**都没收**。当前批量路径不调用（作者注释明确"批量不得自动调用"），
   但它恰好是最容易被"人工排障顺手接线"的一个 ⇒ 已加绊线。
3. **`reader`**：在 `__all__` 里、是串口读线程体，三张名单都没收（靠"调用点被替换"侥幸安全）。已加绊线。
4. **`capture_case` 不在 `acquire.__all__`**：只靠手工枚举才能发现它是硬件入口 ——
   这正是"私有名后门"的样本，故单列 `_ACQ_HW_EXTRA` 显式登记（见 §1.4）。
5. **`guard_demo.py` 的名单不会自愈**：它是**手写清单**，本次给它补了 1 条，
   但"补一条"不是机制。真正的兜底是本文件的 `__all__` 反向枚举 —— 两者互补：
   `guard_demo` 验**行为**（真调用后的返回/抛异常），`_assert_isolation_net` 验**覆盖**。
6. **GBK 控制台的静默失效（顺带修掉）**：`⇒`(U+21D2) **不在 cp936 字符集**里。
   `--l2-direct` 的 help 串与 `[WARN] 未加 --l2-direct ...` 都含它
   ⇒ 在用户的 GBK 控制台上**抛 `UnicodeEncodeError`**：本该提醒"你不是离线"的告警
   变成一页 traceback，而 argparse 的 `--help` 也会崩。已改为 ASCII `=>`
   （`replay.py:806` 附近，附注释）。这条正伤在"最需要它响"的时刻。
7. **未发现其他活体隔离网**：全仓 `HardwareTouchAttempt` / `_ACQ_*` 只出现在
   `regression/replay.py` + `scripts/guard_demo.py`（`tmp/` 下的同名副本属已废弃的临时件）。
   `regression/run_all.py` 直接 `import acquire` 走真硬件路径、**不 import replay**
   ⇒ 本次补丁不会污染批量路径（`run_all.py:699` 仍用真身）。

---

## 6. 复现判据（全部离线，可直接复跑）

```bash
# 1) 启动自检（漏项即非零退出）
python regression/replay.py --session reports/2026-09-24-1 --list

# 2) 覆盖面 + 行为自检
python scripts/guard_demo.py            # PASS
python scripts/audit_patch_targets.py   # PASS（32 个替换名，无"补丁打空"）

# 3) 离线重放（PORTS_DICT 族 = O-40 原受害路径；--l2-direct 为真离线的对照）
python regression/replay.py --session reports/2026-09-24-1 --project trng_test \
       --limit 1 --freeze-time --out tmp/replay/o40-final
python regression/replay.py --session reports/2026-09-24-1 --project trng_test \
       --limit 1 --freeze-time --l2-direct --out tmp/replay/o40-final-l2
```

> ⚠️ 不带 `--l2-direct` 时，隔离网**现在**会把 DS 前置检查换成替身（实测 0.0~0.1s/case）；
> 但离线优先仍建议 `--l2-direct`（不加载工程 main、更快更纯）。
