---
name: qsh-test-run
description: QSH 驱动测试执行 skill（三大类③自动化测试）：dbg_run.py 单次烧录+双串口采集+ztest 原生输出解析、test.sh 三模式、run_test.py 一键回归（批量入口 regression/run_all.py）、ztest 原生输出格式与判定标准。构建/烧录/跑用例按本 skill 操作；连不上/烧不进先查 qsh-debug；结果分析见 qsh-test-compare。
---

# QSH 驱动测试执行

用例的构建、烧录、采集、解析全流程。**连接层问题（DebugServer/串口/连接残留）
先查 `qsh-debug`**；工程规范以 `810_driver_test/CLAUDE.md` 为准。

## 1. 前置检查（每次跑测试前）

```bash
# 1) DebugServer 在跑（在线判定=能连，别用 netstat）
tasklist | grep -i DebugServer
# 2) 无 CSKYFlashProgrammer 占用（RAM load 前；烧 flash 时反过来，见 §8）
tasklist | grep -i csky
# 3) 串口没被占用
tasklist | grep -i python
```
任一异常按 `qsh-debug` §2 处理。

**外部环境告知（★ `SPEC_REFERENCE.md` §6.6.2）**：被测外设存在芯片外部连接路径时
（接线/对端/环回/上拉），开始烧录采集前**先向用户报告本次测试环境**
（接线状态、对端设备、串口号、速率配置），确认后再跑；环境变更后重新回归，
不复用旧报告。

## 2. 构建

```bash
cd 810_driver_test/projects/i3c_dualcore_test/tests/case_pure_i3c_12m5
./test.sh            # 只编译（SKIP_LOAD=y），打印 launch.json 片段
./test.sh -g         # 编译 + GDB 加载运行
./test.sh -r         # 仅加载运行（agent 模式）
```
- `west build` 用 `-p auto`（`SPEC_REFERENCE.md` §6.1.8）
- **改过 overlay（SRAM 地址类）必须 `rm -rf build/controller build/target` 全量重建**
- 编译产物：`tests/<case>/build/{controller,target}/zephyr/zephyr.{elf,bin}`

## 3. 单次烧录 + 采集 + 解析（调试用）

```bash
cd 810_driver_test/projects/i3c_dualcore_test/tests
python scripts/dbg_run.py --case case_pure_i3c_12m5 --timeout 35
# 可选：--raw 全量输出 / --ctrl-uart COMxx --tgt-uart COMxx 显式指定
```

脚本做什么（理解后便于排查）：
1. 枚举非 CKLink 串口（排除 32bf/cklink），全部打开
2. **两遍烧录法**：prime 轮（20s，输出丢弃）→ capture 轮（35s，串口采集）
3. 解析 **ztest 原生输出**（`common/ztest_report.py` `parse_ztest`：ZT_BOOT /
   `PASS|FAIL|SKIP - <name> in X.XXX seconds` / `INFO <name>: k=v` /
   `PROJECT EXECUTION SUCCESSFUL|FAILED`，ANSI 剥离 + `re.search()`）
4. 按 banner 识别角色（CTRL/TGT），打印 SUMMARY 表
5. **capture 轮必须持续排空 gdb stdout**：`Popen(gdb, stdout=PIPE)` 后整个窗口不读该管道 ⇒
   Windows 管道缓冲写满 ⇒ gdb 阻塞在写 stdout ⇒ DebugServer 会话停摆、目标中途 halt ⇒
   **长用例尾部（`PASS - <name>` / `PROJECT EXECUTION`）永远采不到**（窗口结束才 `communicate()`
   排空管道，gdb 才解阻塞并 detach ⇒ 固件实际跑完、RAM 结果对，只有证据丢了）。
   正解 = drain 线程（`for ln in proc.stdout: gdb_out.append(ln)`）+ `proc.wait()`；
   `i2c_pure_test` 已修，其余工程的同模板 `dbg_run.py` 待逐个推广（`KNOWN_ISSUES.md` #97，
   新工程模板要求见 qsh-test-gen §1.5）。`run_test.py`（正式回归）自带 drain 线程，不受影响。
6. **prime 轮超时收尾禁用 `communicate(timeout=)`（★ 2026-09-20，模板缺陷，已修）**：`flash_once`
   若用 `proc.communicate(timeout=…)` 收尾——`communicate()` 在 `input=None` 时会 close stdin 并置
   `proc.stdin=None` ⇒ 超时分支的 `proc.stdin.write("\x03")` 抛 `ValueError` ⇒ 落进 `except: proc.kill()`
   ⇒ gdb **被 kill（非 interrupt+detach）**、留连接残留（违反 qsh-debug §2.3）。**23 份 live
   `dbg_run.py` 已改为 drain 线程 + `proc.wait()` + 定时写 `\x03`/`detach`**；新脚本照此，别回退
   （`KNOWN_ISSUES.md` #101，qsh-test-gen §1.5 / ref-gen-errors）。

**单核工程（otbn_test/otbn_selftest）差异**（★ `KNOWN_ISSUES.md` #23/#24）：
- **单遍烧录**：reset → load → set $pc → continue 直接采首轮。会话内
  interrupt 后二次 load 实测两度静默挂死（gdb 无任何输出）；reset 已冲刷
  I-cache，load 后 .data/.bss 天然干净，结果与两遍烧录法逐项一致。
  双核 i3c 工程维持两遍烧录不变（prime 轮确实有用）
- **写包提速默认开**：load 前发 `set remote memory-write-packet-size 16384` +
  `set remotewritesize 16384`，CKLink load 实测 **1KB/s → 41KB/s（41x）**
  （192KB 镜像 ~190s → ~5s）；`--wpacket-size 0` 关闭。stub 不接受时回
  E22 且立即失败，不会静默卡死
- **交互式流程（★ `KNOWN_ISSUES.md` #25）**：reset+load 后**等 gdb 打出 "Transfer rate"**
  再 set $pc+continue。一次性 `-ex` 链在 load 刚完成的瞬间发 continue，
  DebugServer 状态不佳时会被**静默丢弃**（CPU 停 halt 无 BOOT，detach 才
  跑起来，实测回归 3/3 复现；dbg_run.py 交互式 0 失败）。run_test.py
  已固化交互式 + 90s 无 BOOT 判 no_boot 强制 [RETRY] 自愈
- **免烧重跑 `--no-load`**：镜像已在 RAM（此前 load 过、未断电）时
  attach → sanity 查入口地址 `TARGET_ROM_START`（单核 1os = `0x10000000`，
  2os = `0x100c0000`，见 qsh-test-gen §1.5）→ `set $pc=TARGET_ROM_START` 重跑，
  0 烧录；另有 dump_results.py 内存兜底秒读（qsh-test-compare §3）。**单核脚本
  若把入口硬编码成 2os 的 0x100c0000，`$pc` 就设错、固件不跑、串口 0 行**
  （KNOWN_ISSUES #57）

**输出判读**：
- `UART ROLE DISCOVERY` 全 `?` → 固件没跑，回 `qsh-debug` §2.3 清理连接残留
- 失败项行有 ` FAIL - <name> in X.XXX seconds`，reason 来自 zassert 的
  `Assertion failed at <file>:<line> (<func>): <msg>`（打印在 FAIL 行之前），
  用 `--raw` 看原始日志定位
- gdb 输出存 `tests/dbg_gdb_last.log`，flash 失败时看它尾部

## 4. 一键全量回归

### 4.1 批量总控（★ 首选入口：regression/run_all.py）

```bash
cd 810_driver_test
python regression/run_all.py                # 环境预检 + cases.yaml 全部启用 case
python regression/run_all.py --no-precheck  # 跳过预检（调试用）
python regression/run_all.py --no-erase     # 跳过全量前 flash 全片擦除
python regression/run_all.py --dry-run      # 只列启用 case 与映射脚本
```
- **cases.yaml**：YAML list（`cases:` 下每行一个 case 目录，相对
  810_driver_test），`#` 注释 = 屏蔽。VS Code YAML 模式下 `Ctrl+/` 可直接
  toggle 注释。
- **`--dry-run` 不触发哨兵审计（★ 2026-09-18）**：`main()` 在 `args.dry_run` 处
  `return 0`（`run_all.py:425`），而 `audit_stale_elves()` 在 `_run_all()` 内（`:495`，
  **全片擦除之后**）⇒ `--dry-run` **永远看不到哨兵清单**（"只列 case"≠"列旧镜像"）。
  **要核哨兵清单**只能实际跑 run_all（含全片擦除），或直接
  `import run_all; audit_stale_elves(read_cases())`（本批用后者：51 启用 case，
  检出 38 个缺哨兵旧镜像）。相关：§4.2 / KNOWN_ISSUES #88。
- 流程（按 run_all.py 代码实际顺序）：flash 全片擦除一次 → **分配会话目录**
  （`SPEC_REFERENCE.md` §6.4.1）→ 环境预检（`SPEC_REFERENCE.md` §6.4.6，FAIL=exit 2 交 qsh-debug，报告写入该目录）→
  逐 case 调对应工程 run 脚本 → 日志存 session/logs/ →
  **单 case FAIL 不阻断后续**；退出码 0=全 PASS / 1=有 FAIL / 2=预检 FAIL
- **★ 结果作废判据（单点故障 → 级联假 FAIL，★ 2026-09-21）**：会话 `logs/` 或 DS 自身日志
  出现 **`error 138`** 或 **`DebuggerServer quit`** ⇒ 这是 CKLink **瞬时故障** 让 DebugServer
  自杀（探针**仍在 PnP 枚举中**，**不是**物理掉线，见 `qsh-debug` §2.3a）。**DS 一死，后续所有
  case 都会以 `error 138` 收场** ⇒ **该批次后续 case 的 FAIL 整段作废、不得引用**（它们是同一次
  单点故障的级联，不是各处回归）。**识别信号**：连续多个**不同工程**的 case 齐刷刷 FAIL 且都带
  同一 `error 138`、而各 case 本体此前 PASS ⇒ 先怀疑 DS 单点，**别逐 case 排查**。处置：重启
  DS 后整批重跑。
- **每 case 前置 DS 存活检查（★ G-E2，2026-09-21 已修，24 工程全覆盖）**：判据 =
  `common/acquire.py::ensure_debugserver_alive()` **一处实现、两处调用** —— ①
  `regression/run_all.py` case 循环内逐 case ②**五个 L1 策略入口**（`single_cap` /
  `_ports_dict_one` / `dual_run` / `mbox` / `iwdg`）⇒ 24 工程全覆盖（含不经 run_all 的单跑），
  `run_test.py` 一行未改。阶梯 = 健康（零副作用 netstat）→ 清 gdb 连接残留 → 重启 DS；
  仍不活 ⇒ **fail-closed**（run_all 记 `ENV` + summary `ENV-ABORT`，单跑明确报错中止该 case），
  **绝不放它退化成 `error 138` 假 FAIL**。验收（故障注入 A/B）：DS 掉线场景改动前
  **4/4 假 FAIL** → 改动后 **3/4 通过**（只损失被故障正中的那一个 case）。
- **健康判据不得用 `probe_gdb_alive()`（★ G-E2-1）**：任何 `gdb 连接 → detach → 退出`
  **必留连接残留**（`KNOWN_ISSUES.md` #17）⇒ 用 gdb 探活会**自己制造残留**、把"健康"变成
  "需要重启"（每 case 白多 ~35s）。健康路径只用**无副作用**的 netstat（1025 有 LISTENING
  **且**无 ESTABLISHED）。判据须抗抖动：不靠一次 netstat 下结论 —— **15s 轮询**等 listen 就绪、
  netstat **连续 3 次失败**才认定死（防错杀活实例、白重启）。健康路径实测 **24ms/case**。
- **★ 起后台长任务用绝对路径（G-E2-2）**：`cd X && cmd > log &` 后台化的是**整条链（含 `cd`）**，
  父 shell cwd 不变 ⇒ 按相对路径找日志必扑空 ⇒ **被误读成"没启动"而重复启动** ⇒ 两个
  `run_all.py` 并发抢板子、结果互相污染。纪律：后台命令用**绝对路径**重定向
  （或 `(cd X && cmd) > /abs/log &` 明确分组）；"日志没出现"先查**进程表**再判断死活
  （`debug-knowledge` common 域 `bash-bg-cd-subshell.md`）。
- **识别"放大"的判据（不随修法过期）**：24 份 `run_test.py` 中**只有 6 份**定义
  `DEBUGSERVER_EXE`（`i2c_pure` / `i3c_dualcore` / `i3c_i2c_switch` / `mbedtls` / `otbn_test` /
  `otbn_selftest`，其余 18 份自身无 DS 自愈）⇒ **绕过上面的入口、裸跑**那些
  `run_test.py` / `dbg_run.py` 时 DS 一死仍只能 `error 138`（F1 残留）。判据：连续多个**不同
  工程**的 case 齐刷刷 `error 138`、而各 case 本体此前 PASS ⇒ 先怀疑 DS 单点，**别逐 case 排查**
  （处置见上条 / `qsh-debug` §2.3a）。
- **双核 UART 由 run_all.py 显式注入**（COM15/COM16，`--ctrl-uart/--tgt-uart`）
  ——双核 run 脚本**必须显式 UART，不支持自动发现**（2026-08-29 实测；
  单核脚本可自动发现）
  - **★ `--no-precheck` 且无 `port_mapping.json` 时角色靠"猜"（G-RUN-1）**：此时 `run_all.py`
    按**串口枚举顺序**取头两个口当 CTRL/TGT（`CTRL,TGT = _ports[0], _ports[1]`）——**顺序与物理
    角色无关**。实测猜成 `CTRL=COM17`（应为 COM18/MCP2200）⇒ 双核逐项 token **全丢**（角色错 ⇒
    采集端口对不上）。跳过预检复跑**必须显式指定** `PRE_CTRL_UART`/`PRE_TGT_UART`（run_all 的
    env 入口；`--ctrl-uart/--tgt-uart` 只往下游 run 脚本传），**别信默认推断**，也别因"它自己打了
    一行猜的日志"就以为猜对了。
- **清理进程纪律（★ KNOWN_ISSUES #62）**：回归中要清残留时**只 `TaskStop` runner 的父任务**；
  清 gdb 用 `netstat -ano | grep :1025` 取对端 PID、清 west/ninja 用
  `tasklist | grep -E "west|ninja"` 取 PID，**一律按精确 PID**。
  **禁 `taskkill //F //IM python.exe`** —— west/ninja/run_test.py 本体全是 python 进程，
  按名字杀 = 连坐自杀，症状 = 每个 case **秒级整齐** `BUILD FAILED`（极易误判成代码回归）
- **长时回归的启动方式 + 可续跑兜底（★ KNOWN_ISSUES #64）**：长时（>1h）后台批量任务**可能**被外部中止，
  **用 Bash 工具的 background 任务跑更易被中止**（实测两次都在 ~66 分钟、第 21 case 处被 kill：
  整棵 python 树消失、DebugServer 独活、**无** `BUILD FAILED`）。**想真正一次连续跑完 → 用
  `nohup python -u regression/run_all.py > <log> 2>&1 &` 从 Bash 启动（脱离工具任务进程组）
  + 自主轮询日志**（实测一次连续 98 分钟 30 case 跑完无中止）。**日志重定向一律用绝对路径** ——
  `cd X && cmd > log &` 会连 `cd` 一起后台化、父 shell cwd 不变（G-E2-2，见下条）。同时仍**默认按"可续跑"设计**兜底：
  `run_all.py` 跑头部 + **幂等 resume 脚本**（写 done 标记、重跑自动跳过已完成 case）补齐尾部；
  **中断点与续跑情况必须写进报告**，禁止拼成"一次跑完"。心跳里见"整棵 python 树消失"
  = **外部中止**（非崩溃、非硬件、非上面 #62 的自杀）→ **直接续跑，勿重复排查环境**。

### 4.2 单工程/单 case 回归（run_test.py）

```bash
cd 810_driver_test/projects/i3c_dualcore_test/tests
export I3C_CTRL_UART=COM15 I3C_TGT_UART=COM16   # ★ 双核必须显式（无自动发现）
python run_test.py [--case case_pure_i3c_12m5] [--session-dir <dir>]
```
回归行为（`SPEC_REFERENCE.md` §6.4/§6.2，所有 run_test.py 一致）：
- **判据核心（★ §6.2）**：两遍烧录（prime 判"任何 ztest 输出"、pass-2 判
  ZT_BOOT）、**连续 20s 零输出** = 环境问题中止回归（判活口径详见下条，★ 2026-09-20
  更正）、BOOT 后 10s 无新串口字节 **且已见任一核 `PROJECT EXECUTION ...` 完成标记** = 套件结束提前杀
  （中途静默=慢速 case 长间隔，继续等，case_timeout 兜底）、串口实时采集
  （事后读只剩 shell 提示符）、ztest 输出解析 `re.search()` 不锚点（shell
  前缀）、每 case 前后 DebugServer **按需**自愈（`KNOWN_ISSUES.md` #17：残留检测通过才重启，
  实测 6 case 仅重启 1 次）、回归前构建全量 case（勿 --skip-build 混旧镜像，
  `KNOWN_ISSUES.md` #18；2026-09-18 起 run_all.py 对该路径加了**哨兵守卫**：仅当 ELF 含
  `test_results_valid` 才 `--skip-build`，缺符号=旧构建 ⇒ 强制重建 + 预检
  `audit_stale_elves()` 列出全部旧镜像）
  - **⚠ 烧录/启动窗口按"进度"判活，不用绝对秒数（★ 2026-09-20 更正）**：`proc_lines`
    仍在长 ⇒ 放行到 `case_timeout` 硬上限；**连续 20s 一点输出都没有**才判 `load_fail`。
    原"spawn 后 20s"是**绝对**窗口 ⇒ 把"烧录正常、只是慢"（CKLink 限速 41 KB/s，最大镜像
    实测 **17~50s**）误判成环境失败并 `abort_on_load_fail` 中止整批。反过来，该进度信号
    **依赖 drain 线程按 `\r`/`\n` 双向切分 + `os.read(fd)`**（gdb 的 `load` 进度是 `\r`
    原地刷新、不产生换行；只按行读则慢烧录期间 `proc_lines` 零增长 ⇒ 判据再准也看不见
    "它还在动"）。见 `SPEC_REFERENCE` §6.11.4 / `common/acquire.py::_LoadWatch`。
  - **⚠ 哨兵门仅验"符号存在"、不验"源码新鲜度"（★ 2026-09-20）**：ELF 含哨兵但 build 目录
    mtime 早于源码 ⇒ **照旧被 `--skip-build` 复用** ⇒ 烧陈旧镜像 ⇒ **假 FAIL / 假 PASS 双向**
    （实测 `i3c_dualcore_test case_mixed_400k_6m25` 假 FAIL、`case_pure_i3c_6m25` 假 PASS）。
    动过固件源码后必须按该 case 自己的 build 目录重构建，**别信"含哨兵即新鲜"**
    （G-FAMFILL-1，`KNOWN_ISSUES.md` #88 姊妹形态）
- **报告归档（会话目录）**：Markdown + JSON 写到
  `810_driver_test/reports/<YYYY-MM-DD>-<idx>/<project>_<case>/`（`common/
  report_dir.py` 统一分配，禁止自算路径）；`summary.md` 只在会话目录最外层，
  **行粒度 = case**（key `<project>_<case>`）；不带 `--session-dir` 自动分配
  新会话目录
  - **★ 编程式复用会话目录要 patch「定义模块」（G-RUN-1）**：`run_all._run_all` 内部写的是
    `from report_dir import allocate_session_dir`（**函数内 import**，调用时按模块属性解析）⇒
    只 patch `run_all.allocate_session_dir` **不生效**、会另建新会话目录（实测落到 `-3`）。
    复用同一会话目录须 patch **`report_dir.allocate_session_dir`**（招式见 `debug-knowledge`
    common 域 `python-from-import-patch-target.md`）。
- **INFO 约定行解析**：`INFO <test_name>: k=v[,k=v]` 数值型实测（吞吐/占比等）
  进报告"实际"列（`SPEC_REFERENCE.md` §6.5.4）
- **内存兜底合并**：case 跑完后调 `dump_results.py --case <case> --json`，
  串口缺项用内存值补，来源标 `mem`；数组全 -1 的项算 NOT_RUN 且 case 判 FAIL；
  GDB 读不到内存（连接残留等）标记 `mem-unavailable` 不阻断回归（`SPEC_REFERENCE.md` §6.4.3）
- **`mem_state` 要读要写（★ 2026-09-18）**：兜底 payload 带 `mem_state` 三态 ——
  `no_sentinel_symbol` = **旧构建，先重建再跑**（复跑无意义）/ `invalid` = 未跑到
  `tr_init`，内存不可信 / `ok`。非 `ok` 时兜底对该核 fail-closed；状态与原因必须
  进控制台 + `.md` + `.json`（判据见 SPEC_REFERENCE §6.2.2）
- **`--timeout` 决定覆盖度**（★ KNOWN_ISSUES #63）：太小会把尾部项目按超时截断成
  `NOT_RUN`（慢速工程 rtc_ls/mbedtls 尤甚）。跨轮对比**必须同一 timeout**，
  否则"上次 PASS 这次 FAIL"可能只是超时掩盖的潜伏项首次执行，**不是回归**
  （判据/差异表规范见 qsh-test-compare §5）
- Windows 下超时用 Python `subprocess.run(timeout=...)`，**不用 bash `timeout`**

### 4.3 离线重放（不碰板子；只改了脚本时首选）

- 判定 / 渲染 / JSON / summary / `common/process.py` 等**纯数据处理**改动**不需要上板** ——
  用归档的原始串口行重放即可（"什么改动可离线证"的判据见 `SPEC_REFERENCE.md` §6.4.9）。
```bash
cd 810_driver_test
python regression/replay.py --session reports/<date>-<idx> --list    # 列可重放 case
python regression/replay.py --session reports/<date>-<idx> --case <proj>_<case>
python regression/replay.py --session reports/<date>-<idx> --project <p> --l2-direct --freeze-time
```
- 输出默认落 `tmp/replay/<会话>/`，**绝不覆盖 `reports/`**；`--render-from-json` 用于校验渲染接线。
- **不可重放**（必须上板）：L1 采集段（时序）、mem 兜底（内存未归档）、固件 `.c/.h`。
- 分层全景 + 「改哪层要验什么」速查：`FRAMEWORK_MAP.md`。

## 5. ztest 原生输出格式与判定标准

```text
ZT_BOOT <core>                            # glue printk，test_main 第一行（BOOT 判据）
Running TESTSUITE <suite>                 # ztest 原生，套件开始
START - <name>                            # ztest 原生，逐项开始
 PASS - <name> in X.XXX seconds           # ztest 原生，逐项判定
 FAIL - <name> in X.XXX seconds
 SKIP - <name> in X.XXX seconds
INFO <name>: <k>=<v>[,<k>=<v>]            # ZT_INFO 宏，实测数值（`SPEC_REFERENCE.md` §6.5.4）
Assertion failed at <file>:<line> (<func>): <msg>  # zassert 原生，FAIL reason（`SPEC_REFERENCE.md` §6.5.5）
PROJECT EXECUTION SUCCESSFUL|FAILED       # ztest 原生，本核套件完成标记
```
判定标准（`SPEC_REFERENCE.md` §6.2）：
- 单核：该核全项 `PASS - <name>` + 本核 `PROJECT EXECUTION SUCCESSFUL`；
  双核：同名测试项**两核各自 `PASS - <name>`** + 两核各自 `PROJECT EXECUTION
  SUCCESSFUL` 才算过；单核假 PASS（如 target 只校验部分）不算
- **窗口到点不是失败（`execution=TRUNCATED_AFTER_ALL_ITEMS`，★ 2026-09-18）**：
  全项**都已拿到确定结果**（token/mem，无 MISSING）且全 PASS、只缺收尾标记 ⇒ 判 PASS
  并标注；**有项无证据 ⇒ 仍 FAIL**（红线）。**零证据（0/0）也不得判 PASS**
  （`all([]) == True` 陷阱）；双核**某一核整体**无确定结果 ⇒ 不得因另一核完好判 PASS
- **双核例外（i3c_dualcore_test）**：target `test_main` 不返回（`SPEC_REFERENCE.md` §6.1.1）
  → TGT 端口**没有** `PROJECT EXECUTION` 行，属设计；该工程完成判定 =
  CTRL 侧 SUCCESSFUL 且 TGT execution != "FAILED"（样板 `run_test.py`
  `evaluate_case`）。i2c_pure/i3c_i2c_switch 仍按两核各自 SUCCESSFUL 判
- 以机器输出为准，肉眼扫串口不可靠；逐项输出依赖
  `CONFIG_ZTEST_VERBOSE_OUTPUT=y`（`KNOWN_ISSUES.md` #26，=n 则 START/PASS 行被抑制全 MISSING）
- ztest/glue 输出 printk 直出 UART（无 LOG 前缀）；解析用 `common/ztest_report.py`
  `parse_ztest`（ANSI 剥离 + `re.search()`），勿自写解析器
- **token 拼写两类，核"TGT 有没有证据/分拣到没到位"时别只数 ztest 正则（★ 2026-09-21）**：
  **D1** = ztest 原生 `PASS - <name> in …`；**D2** = 自定前缀
  `<PREFIX>:<core>:(PASS|FAIL|SKIP):<name>`（`NATIVE_PREFIXES` 白名单）。拿 ztest 正则数 D2 工程的
  TGT 会得 `zt=0` ⇒ 误判"分拣失败"；判据 = 先看 `ZT_BOOT <core>` / `<PREFIX>:<core>:BOOT` 是否存在，
  再**按该工程自己的文法**分类计数（`SPEC_REFERENCE` §6.9.1 / qsh-test-compare §2）
- **完成标记可能不止一条**：app 若自打 `printk("PROJECT EXECUTION …")`，会与 ztest 强 `main()`
  的 `end_report()` 并存（固件不调 `ztest_run_all` 时后者不读 stats、**恒打 SUCCESSFUL**，是空跑）
  ⇒ 同一核流出现相反标记时**不得取最后一条**，须交叉 GDB 读 RAM 记账后再判
  （`KNOWN_ISSUES.md` #98、qsh-test-compare §2；新工程固件不应再自打，qsh-test-gen §1.4 第 5 条）
- 各族 × 各状态的完整判定（冻结总表）见 `SPEC_REFERENCE` §6.2.4——本节是执行视角速查，判定语义以总表为准

## 6. 常见问题速查

| 现象 | 原因 | 处理 |
|---|---|---|
| flash OK 但 0 行/banner 全 ? | 连接残留 | 清残留 → qsh-debug §2.3 |
| ztest 输出行匹配不上（全 MISSING） | 行首 ANSI / `uart:~$ ` 前缀污染，或 `CONFIG_ZTEST_VERBOSE_OUTPUT=n` | 剥离 + `re.search()`（qsh-debug §4.3）；查 prj.conf（`KNOWN_ISSUES.md` #26） |
| 测试顺序错乱假象 | prime/capture 输出混合 | 只采 capture 轮 |
| 某测试 ~6.5s 停摆无 PROJECT EXECUTION | 目标侧死窗/停摆 | 查目标日志，必要时加 GDB 断点验证 |
| 构建缓存旧地址 | overlay/源码改动后缓存未失效 | rm -rf build 全量重建 |
| 构建报 `ninja: error: failed recompaction: Permission denied` | 同一 build 目录被并发 `west build` 抢——**多数是自己上一轮的 `nohup west build &` 没退干净**（误判为已死又起前台构建，后台那次其实还活着） | 怀疑"后台构建死了"**先查进程树**（`Get-CimInstance Win32_Process` 看 nohup→west→python→cmake 链）再重跑；确认残留后**按 PID** `taskkill //PID <pid> //T` 杀自己那条链，**禁 `//IM python.exe`**（连坐，KNOWN_ISSUES #62）。见 qsh-test-validate §2 |
| load 极慢（~1KB/s，192KB 镜像要 ~190s） | 没设写包参数 | 单核 dbg_run.py 默认已开 16384（41x，§3）；手动 gdb 照 §3 发两条 `set`；E22 = stub 不接受，立即失败不卡死 |
| 单核工程 interrupt 后二次 load 无输出挂死 | 会话内二次 load 静默挂死（`KNOWN_ISSUES.md` #24） | 改单遍烧录（§3）；同镜像迭代用 `--no-load` 免烧重跑 |
| flash_program.py 卡死 80s（`ConnectType=0`） | DebugServer 会话后芯片处于调试 halt 状态，普通连接无法重连 | **flash_program.py 已改为 `ConnectType: 1`（硬复位连接）**，实测解决（2026-09-02） |
| 回归结束后下次 erase 卡死 | DebugServer 回归期间常驻，结束后未释放 CKLink | **run_all.py 已加 `cleanup_cklink()`（try/finally）**，回归结束必杀 DebugServer |
| 构建报 `pthread_cond_t` 类型冲突 | `CONFIG_NEWLIB_LIBC=y` 与 Zephyr POSIX types 冲突 | 改为 `=n`（Zephyr minimal libc 足够），全工程统一 |
| 全量回归**每个 case 秒级整齐** `BUILD FAILED`/`rc=1`（不是散点 FAIL） | 按名字杀了 `python.exe`（`//IM`）→ 连坐 west/ninja/run_test.py 本体 | 只 `TaskStop` runner 父任务；清 gdb/west/ninja 按精确 PID，**禁 `//IM python.exe`**（KNOWN_ISSUES #62） |
| 上次 PASS、这次 FAIL，但两轮 `--timeout` 不同 | 短 timeout 截断出的 NOT_RUN 掩盖了潜伏缺陷 | 先核两轮 timeout 是否一致；不一致则不可对比，"新 FAIL"≠回归（qsh-test-compare §5 / KNOWN_ISSUES #63） |
| 全量回归长任务中途被中止，**整棵 python 树消失、无 `BUILD FAILED`** | **外部中止**（非崩溃/非硬件；与 #62 的自杀相反）；Bash 工具 background 任务跑长时更易被中止 | 直接幂等续跑补齐尾部、中断点入报告；想连续跑完改用 `nohup ... &` 脱离工具进程组（§4.1 / KNOWN_ISSUES #64） |
| 某 case 报告 `mem_state=no_sentinel_symbol`、内存兜底整体失效，case 看起来像"内核挂死" | **`--skip-build` 复用了上一次会话的旧 ELF** —— 每个 case 有**自己**的 `tests/<case>/build/`，"首个 case 已构建"不覆盖后续 case 目录 | 按**该 case 自己的**产物校验新鲜度（哨兵/版本标记符号可作判据）；run_all.py 已改为哨兵感知 `--skip-build` + 循环前 `audit_stale_elves()`（§4.2 / KNOWN_ISSUES #88） |
| 双核 case 每轮固定多一项 `run::timeout` FAIL（本体用例其实都跑完了） | `test.sh -r` 的 gdb 命令带 `continue` 且 `RUN_TIMEOUT=0` ⇒ **进程永不退出**，`run_pass()` 的 `while proc.poll() is None` 只能靠超时跳出 ⇒ `timed_out` 恒真 | **`--timeout` 是采集窗口，不是完成预算**；判定只认证据（全项已出结果 + 收尾标记），窗口到点本身不是失败（§5；判定口径见 qsh-test-compare §2） |
| 串口窗口内**只有 boot banner + `START - <name>`**、无 PASS/FAIL/INFO，SUMMARY 恒 `ZT_BOOT seen`（用例其实已跑完） | **采集工具缺陷**：`dbg_run.py` capture 轮 gdb stdout 管道未排空 ⇒ gdb 阻塞、采集停摆 | 先看 RAM（`test_results_valid` + 槽位）确认用例跑没跑完，别按"固件挂死"继续查串口；修法 = 补 drain 线程 + `proc.wait()`（§3）；`i2c_pure_test` 已修、其余工程待推广（KNOWN_ISSUES #97） |
| 某 case 采集窗到点仍拿不到全部项 | 先分清**耗时**与**采集**（原"整轮 ≥40s"是窗口从 gdb 启动起算、被 `load` 占用的假象，**已撤回**，KNOWN_ISSUES #99） | `i2c_pure_test` `i2c_wave` 实测 **102.76 ms/轮 = 10.276s**、与注释 `~10s` 一致 ⇒ 采集窗须 ≥ 用例时长 + `load`(~4s)，不必按"变慢"加大；拿不到尾部证据的真因是上面的管道缺陷（#97） |
| 破坏性 / 跨复位 case 窗口内"没看到重启" ⇒ 误判"没复位" | 采集窗按**标称超时**设（如 WDT 2s）⇒ 看不到延迟很大的真复位。sec_iwdg 标称 2s、**实测延迟可达 ~34s**，"窗口内没看到"≠"没发生" | **判据取跨复位 `__noinit` 终态**（如 `test_phase==2`），不靠窗口内捕捉重启；只能看串口时窗口取 **≥60s**（不得按标称值反推）。同类 `app_iwdg_cpu2_test case_reset` / `case_pmu_iwdg_full_reset`（KNOWN_ISSUES #100；也见 qsh-debug `ref-flash-variants.md` §2） |
| flash 固化（2os）后 cpu2 串口 0 行、cpu1 正常 | **2os 板上 bootrom 只引导 cpu1**；cpu2 烧进 flash `0x08100000` ≠ 会自动起——镜像需先 staging 进 RAM（GDB `restore` 或 cpu1 boot 搬运代码）；`boot_cpu2()` 在该 build 下只 dereset、**不搬运镜像** | 先 GDB 读 `0x10000000` 是否为**本次构建**的 cpu2 镜像（ELF magic + `e_entry` 对照 objdump）→ 否 ⇒ 镜像从未上电；别按"固件挂死"查串口（KNOWN_ISSUES #102 / SPEC_REFERENCE §6.7.3） |
| `--skip-build` 复用了**含哨兵但陈旧**的 ELF ⇒ 结果假 FAIL / 假 PASS | 哨兵门**只验符号存在、不验源码新鲜度**（`_elf_has_sentinel`）⇒ build 目录早于源码的旧 ELF 被复用、构建段被跳过，烧的是旧镜像（与构建路径/优化级别无关） | 动过固件源码后按**该 case 自己的** build 目录重建；核 build mtime vs 源 mtime（G-FAMFILL-1，KNOWN_ISSUES #88 姊妹形态 / §4.2） |
| 连续多个**不同工程**的 case 齐刷刷 FAIL、都带 `error 138` | CKLink **瞬时故障** → DebugServer 自杀（探针仍在 PnP 枚举），DS 死后所有 case 均 `error 138`——**单点故障的级联**，不是各处回归 | **该批次后续结果整段作废**，别逐 case 排查；重启 DS 后整批重跑（§4.1 / qsh-debug §2.3a） |
| 某工程 DS 死后**不自愈**、直接 `error 138` | 该 `run_test.py` 未定义 `DEBUGSERVER_EXE`（6/24 有、18/24 无）；**批量入口与 5 个 L1 策略入口已每 case 前置自愈**（§4.1），但**绕过它们裸跑**该脚本时仍无自愈 | 走 `run_all.py` 即受覆盖；裸跑单工程先按 qsh-debug §2.3a 重启 DS；`dbg_run.py` 无自愈属已知残留（F1，FRAMEWORK_MAP §5） |

## 7. 坑沉淀要求

跑测试遇到的新问题，往 `.claude/gotchas/INBOX.md` 追加一条（现象/根因/处理）；沉淀到 §6 + KNOWN_ISSUES 归 keeper（skill-orchestrator §2）。

## 8. XIP 固件固化与启动测试（flash 烧录）

详细流程（烧录命令/双核一次会话/CKLink 互斥/地址约定/结果判定）见
[ref-xip-flash.md](ref-xip-flash.md)。要点：XIP 标准组合 = cpu1 XIP + cpu2 RAM；
烧 flash 时 DebugServer 必须退出；默认测试 = 全 RAM 不烧 flash。

**⚠ 2os 固化后 cpu2 串口全静默（★ 2026-09-20）**：**本板 bootrom 只引导 cpu1**，cpu2 镜像需
**先 staging 进 RAM**（GDB `restore` 或 cpu1 的 boot 搬运代码）——把 cpu2 烧进 flash `0x08100000`
**不等于**会自动起（`boot_cpu2()` 在 `# CONFIG_CPU2_IMAGE_HEADER is not set` 时只 dereset、
不搬运）。判别：GDB 读 `0x10000000` 是否为本 case 构建的 cpu2 镜像（ELF magic + `e_entry`）；
`app_iwdg_cpu2_test case_reset` 的 flash 流即此坑（KNOWN_ISSUES #102 / SPEC_REFERENCE §6.7.3）。

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| 采集完成 | ★ qsh-test-compare | 自动转，结果分析 |
| 连接/烧录失败 | qsh-debug（环境修复） | 环境问题 |
| 发现环境坑 | KNOWN_ISSUES.md | 反复踩的坑 |

## 链路
> **输出**→ `qsh-test-compare` · **前置**← runner(STRATEGY_TEST) · **相关**→ skill-orchestrator
