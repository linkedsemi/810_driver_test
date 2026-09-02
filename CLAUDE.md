# CLAUDE.md — 810_driver_test 快速上手指南

> 本目录是 **LSQSH 驱动批量自动化测试工程集**。`i3c_dualcore_test` 为首个样板工程，
> 后续每个驱动测试工程都应**按本规范自动生成**，保持结构、协议、脚本、命名完全一致。
> 新 agent 上手路径：先读本文件 → 按任务调 `.claude/skills/` 下对应 skill；
> 遇到具体问题，先查 §4 速查表，再查 §10 完整坑清单。
> **迁移提示**：本文件是 810 工程级指南，**不随 `.claude/` 迁走**；skill 库便携说明/成员/接线/路由在
> `.claude/CLAUDE.md` + `.claude/plans/skill-library-unification.md`（随 `.claude` 整体迁移）。

---

## 1. 板级要求（环境准备）

### 1.1 板卡型号

- 主测板卡：`lsqsh_evb@2os`（双核双系统 AMP）
  - cpu1 = target（安全核，uart1）
  - cpu2 = controller（应用核，uart5）

### 1.2 接线

- I3C/I2C 跨核对连：I3C5 ↔ I3C6（按具体 case overlay 配置）。
- Flash/电源：板载 SPI flash 供电正常，CKLink 已连目标 JTAG。
- 串口：两路独立 UART，115200 8N1；cpu1 用 uart1，cpu2 用 uart5（经 `common/cpu2_console_uart5.overlay` 注入）。

### 1.3 COM 映射表

| COM | 设备/VID:PID | 角色 | 注意 |
|---|---|---|---|
| COM14 | CKLink Serial（VID 32BF:B210） | 调试器自带串口 | **必须排除**，不是目标日志口 |
| COM15 | MCP2200（04D8:00DD） | controller（cpu2，uart5） | |
| COM16/17 | CH342（1A86:55D2） | target（cpu1，uart1）+ 备用 | 口号以设备管理器为准 |

> 脚本靠 banner 动态识别角色（`"CPU2 MASTER"/"i3c_master_test"` → CTRL；`"CPU1 SLAVE"/"i3c_slave_test"` → TGT），不硬编码口号。

### 1.4 DebugServer 启动/清理

| 项 | 实测值 | 说明 |
|---|---|---|
| GDB | Xuantie `riscv64-unknown-elf-gdb` | **无 Python 支持**，验证逻辑全用 `-ex` 命令链 |
| GDB server | `127.0.0.1:1025` | 由 PC 进程 **XuanTie DebugServer** 提供，**不是** CKLink 固件内置 |
| DebugServer | `C:\uaer_app\debug_server\bin\DebugServerConsole.exe` | 端口在 `bin\default.ini` 的 `SOCKETPORT=1025`；**必须手动启动** |
| 冲突进程 | CSKYFlashProgrammer.exe | 它占用 CKLink 时 DebugServer 起不来（报 "CKLINK may be used by other program"） |

启动与清理：

```bash
# 启动（首次或被杀后）
cd C:/uaer_app/debug_server/bin && ./DebugServerConsole.exe &

# 启动失败且报 CKLINK used by other program：FlashProgrammer（GUI/CLI）正在占用
tasklist | grep -i -E "cskyflash|debugserver"   # 找占用进程
taskkill //F //PID <pid>          # 杀掉后重启 DebugServer

# 验证在线：直接 GDB 连一次（能进 "Remote debugging" 即通），勿用 netstat 判离线
riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" -ex "detach" -ex "quit"
```

> **CKLink 互斥（双向）**：DebugServer（GDB RAM load 调试）与 FlashProgrammer（XIP flash 烧录，§15）在同一时刻只能有一个持有 CKLink。烧 flash 前确保 DebugServer 已退出；烧完需要 GDB 时再启动 DebugServer。

### 1.5 环境检查 checklist

跑任何测试前先确认：

1. `tasklist | grep -i -E "cskyflash|debugserver"` — 无 FlashProgrammer 残留。
2. DebugServer 在线：`riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" -ex "detach" -ex "quit"` 能进 Remote debugging。
3. 串口空闲：候选端口被其他 pyserial/python 进程占用时 `taskkill //F //PID <pid>`。
4. 目标板静默：对候选串口听 10s 无输出，避免被其他会话固件spam污染。
5. （批量回归）运行 `python regression/precheck/run_precheck.py` 做链路级预检（§12.6）。

---

## 2. 测试内容一览

### 2.1 工程列表

| 工程名 | 单/双核 | case 数 | 测什么 | 当前基线签名 |
|---|---|---|---|---|
| `i3c_dualcore_test` | 双核 | 8 | I3C 主从跨核通信：pure I3C / mixed I2C-I3C、400k/1M/6.25M/12.5M 速率 | 以 `run_i3c_tests.py` 全量输出与 `reports/` 最新归档为准 |
| `i2c_pure_test` | 双核 | 1 | I2C 纯模式跨核对连 | 以 `run_i2c_tests.py` 输出与 `reports/` 最新归档为准 |
| `i3c_i2c_switch_test` | 双核 | 1 | I3C/I2C 动态切换 | 以 `run_i3c_i2c_switch_tests.py` 输出与 `reports/` 最新归档为准 |
| `trng_test` | 单核（cpu1） | 1 | 真随机数熵源 API、统计门限 | 以 `run_test.py` 输出与 `reports/` 最新归档为准 |
| `rtc_test` | 单核（cpu1） | 1 | RTC 设置/读取/闹钟/回绕 | 以 `run_test.py` 输出与 `reports/` 最新归档为准 |
| `otbn_test` | 单核 | 1 | OTBN 硬件 ECC/P256/P384 等 | 以 `run_test.py` 输出与 `reports/` 最新归档为准 |
| `otbn_selftest` | 单核 | 1 | OTBN 底层自检测试 | 以 `run_otbn_selftest.py` 输出与 `reports/` 最新归档为准 |
| `mbedtls_test` | 单核/双核视 case | 10 | mbedtls/wolfssl + OTBN 硬件加速：SM2/P256/P384/RSA/SHA/SM3 等 | 以 `run_test.py` 输出与 `reports/` 最新归档为准 |
| `gpio_toggle_test` | 单核 | 1 | 示波器观察型，**无自动化回归脚本**，默认屏蔽 | 人工示波器判定 |

> case 数来自 `regression/cases.yaml` 当前启用清单；实际目录以工程 `tests/case_*` 为准。

### 2.2 `regression/cases.yaml`

→ 详见 [`regression/cases.yaml`](regression/cases.yaml)。`#` 注释 = 屏蔽，VS Code 下 `Ctrl+/` toggle。

---

## 3. 测试方式（调用入口）

### 3.1 全量回归

```bash
cd 810_driver_test
python regression/run_all.py                # 环境预检 + cases.yaml 全部启用 case
python regression/run_all.py --no-precheck  # 跳过预检（调试用）
python regression/run_all.py --no-erase     # 跳过全量前 flash 全片擦除
python regression/run_all.py --dry-run      # 只列启用 case 与映射脚本
```

流程：读清单 → flash 全片擦除一次（DebugServer 互斥退出后重启，失败=环境 FAIL 批量中止）→ 分配会话目录 `reports/<date>-<idx>/` → 环境预检（可跳过）→ 逐 case 调对应工程 run 脚本 → 每 case 日志独立存 `session/logs/` → 结束总表 + 退出码（全 PASS=0；有 FAIL=1；预检 FAIL=2）。

- 单 case FAIL 不阻断后续 case。
- 双核 UART 由 `run_all.py` 配置区显式注入（COM15/COM16，`--ctrl-uart/--tgt-uart`）。
- 动过固件源码后，必须先对受影响工程单独全量构建，勿依赖 `run_all.py` 的 skip-build 假设（§10.32）。

### 3.2 单工程回归

```bash
cd 810_driver_test/projects/<driver>_test/tests
python run_test.py
```

示例：

```bash
cd 810_driver_test/projects/i3c_dualcore_test/tests
python run_test.py
```

### 3.3 单 case 调试

```bash
# 构建 + GDB 加载运行（test.sh 三模式）
cd 810_driver_test/projects/<driver>_test/tests/case_<name>
./test.sh
./test.sh -g   # 编译 + GDB 加载运行
./test.sh -r   # 仅加载运行（agent 模式）

# 单次烧录 + 双串口采集 + token 解析（不判 PASS/FAIL，只输出日志）
cd 810_driver_test/projects/<driver>_test/tests
python scripts/dbg_run.py --case case_<name> --timeout 35
```

### 3.4 `cases.yaml` 注释语法

- `#` 开头 = 屏蔽跳过。
- VS Code YAML 模式下 `Ctrl+/` 可直接 toggle 注释。
- 用例清单变更改 `regression/cases.yaml`，`run_all.py` 不硬编码 case。

### 3.5 输出与归档

- 一键回归结果写入 `reports/<YYYY-MM-DD>-<idx>/`（会话目录，idx 当天第几次会话，1 起累加）。
- `summary.md`：每 case 一行索引。
- 每 case 详细报告：`<project>_<case>/<driver>_test_report.{md,json}`。
- 单次 `dbg_run.py`（调试）不归档，只归档一键回归。
- 会话目录分配走公共模块 `common/report_dir.py`（`allocate_session_dir()` / `case_dir_for()` / `update_summary()`），禁止 run 脚本自算路径。

---

## 4. 调试方式

### 4.1 Skill 路由

本工程任务按三大类组织。本地 skill 库 = 本目录 `.claude/skills/`（平铺一层，为后续
抽离跨环境库的统一库体）；路由见下表：

| 用户诉求 | 调哪个 skill | 内容 |
|---|---|---|
| 驱动行为异常 / 烧录失败 / 串口没数据 / 定位根因 | `qsh-debug` | QSH 环境实操：连接层 / 烧录 / 串口 / 速查表 |
| 跨环境通用嵌入式调试方法（不依赖硬件环境） | `embedded-debug` | 分层根因定位、断点抓现场、探针纪律、坑沉淀（qsh-debug 的方法论层） |
| scons/HAL 示例验证（低频，非主环境） | `qsh-debug` §附（`ref-scons-hal-verify.md`） | HAL 流实操：scons 构建 / uart_capture / _run.gdb / dump 判读 |
| 构建 / 烧录 / 跑用例 / 全量回归 | `qsh-test-run` | dbg_run.py / test.sh / run_*_tests.py 用法、回归执行 |
| 分析结果 / 写报告 / 回归对比 / 串口无输出取结果 | `qsh-test-compare` | parse_ztest、双 PASS 判定、报告生成、dump_results.py 内存兜底 |
| 新建测试工程 / 新 case | `qsh-test-gen` | 目录骨架、协议头、固件模板、公共结果模块注入 |
| 新建/修改工程后验证合规性 / 回归前预检 | `qsh-test-validate` | 目录结构、命名、prj.conf、公共模块注入、sys.path、ztest_glue、编译验证 |
| 调试领域招式查询/沉淀（mcu/zephyr/linux/riscv/arm，非 QSH 专属） | `debug-knowledge` | 按域索引装载既往招式；可复用调试法自动填充入库 |
| 查外部插件/MCP 能力（superpowers/claude-mem 等）怎么启用 | `skill-registry` | 外部能力目录 + 新环境启用法 |
| 环境迁移/新工程引导/不确定调哪个 skill | `env-adapter` | 探测环境类型 + junction 接线 + 路由（非 810/新环境总入口） |

> 调试流程次序：`qsh-debug §0` 总流程 → 连接/烧录实操（qsh-debug）→ 分层定位/断点方法（embedded-debug）→ QSH 速查（qsh-debug §7）。

### 4.2 常见现象速查表（最高频 10 条）

| 现象 | 最可能原因 | 立即处理 |
|---|---|---|
| DebugServer 起不来，报 "CKLINK may be used by other program" | FlashProgrammer（GUI/CLI）占用 CKLink | `tasklist \| grep -i cskyflash`，`taskkill //F //PID <pid>`，重启 DebugServer |
| 串口 0 行，只有 `\x00` 复位噪声 | gdb 被杀后 DebugServer 连接残留，reset/load 无效 | 干净 detach 一次：`riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" -ex "detach" -ex "quit"`；不行重启 DebugServer |
| load 极慢（~190s） | CKLink 写包参数未调大 | 烧录前发 `set memory-write-packet-size 16384` + `set remotewritesize 16384`（41x 提速） |
| WDR / 时序项间歇性异常 | DebugServer 新实例不稳定期（~30s 内连跑） | 重启 DebugServer 后等 ~30s 稳定期再烧录 |
| 解析到的 PASS/FAIL 与预期相反 | shell 提示符 ANSI 重绘在行首加 `uart:~$ ` 前缀 | 解析用 ANSI 剥离 + `re.search()`，不用 `^` 锚点 |
| 改 overlay 后行为仍像旧的 | `-p auto` 不检测 Kconfig 缓存 | **删 build 全量重建** |
| COM 口被占用，采集失败 | 残留 pyserial/python 进程 | `tasklist \| grep python`，`taskkill //F //PID <pid>` |
| 逐项 `START - / PASS -` 全 MISSING | `CONFIG_ZTEST_VERBOSE_OUTPUT=n` 抑制了逐项输出 | 固件 prj.conf 必须 `CONFIG_ZTEST_VERBOSE_OUTPUT=y` |
| 测试跑到一半被截断/提前杀 | 完成标记判定错误：只看到静默但无 `PROJECT EXECUTION` | 正确实现 §6.2 判据：只有静默且无完成标记才继续等 |
| 共享树构建报错（隐式声明等） | 另一活跃会话改了公共 API | 查相关目录 mtime，等共享树稳定再构建；不代改 glue/调用点 |

### 4.3 复杂问题指引

- 连接层、断点法、分层定位、探针纪律 → `qsh-debug` skill。
- 完整 32 条已知坑清单 → 本文 §10。
- 结果判定、报告生成、内存兜底 → `qsh-test-compare` skill。
- 定位闭环后按 §7 写问题总结存档；一次性 bug 不进 §10，只写 `<BUG>_<SUMMARY>.md`。

---

## 5. 新建测试工程 / 新 case

→ `qsh-test-gen` skill（自动生成目录骨架、协议头、test.sh、公共模块注入）
→ `qsh-test-validate` skill（生成后合规性验证 + 编译检查）

---

## 6. 规范参考（详细文档）

完整规范已拆分到独立文档，按需查阅：

| 主题 | 文档 | 说明 |
|------|------|------|
| 固件编写规范（ztest/ISR/log/构建） | [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) §6.1 | ztest 契约、ISR 禁 LOG、公共结果模块注入 |
| 结果判定 / 报告归档 | [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) §6.2-6.4 | ztest 输出格式、双 PASS 判定、会话目录、summary.md |
| 覆盖矩阵 / 预期数据 | [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) §6.5 | COVERAGE.md 模板、INFO 约定行、zassert reason |
| 外部环境确认 | [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) §6.6 | 有外部连接路径的工程创建前/执行前确认 |
| XIP 烧录 | [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) §6.7 | flash 地址约定、多核模式、flash_program.py |
| 已知坑完整清单（36 条） | [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) | Top 10 在本文 §10，完整版含一次性/已修复条目 |

---

## 10. 已知坑清单（Top 10 高频，精简版）

> 完整 36 条清单见 `KNOWN_ISSUES.md`。以下只列**会反复踩**的高频坑。

1. **连接残留**：gdb 被杀后 DebugServer 残留 → 固件不跑/banner 全 `?`。
   清理四步：杀 gdb → taskkill DebugServer → 精确杀 1025 → 重启 DS。
   Python subprocess 里 taskkill **必须单斜杠** `/F /PID`。
2. **CKLink 互斥**：DebugServer 与 FlashProgrammer 同时只能一个占 CKLink。
   切换前 `tasklist | grep -i -E "cskyflash|debugserver"` 确认对方已退。
3. **两遍烧录**：I-cache 残留旧镜像 → 同镜像烧两遍，第二遍抓取。
   单核工程已改单遍（reset 冲刷 I-cache）。
4. **token 解析**：ANSI 重绘 + `uart:~$ ` 前缀 → `re.search()` 不锚点。
5. **ISR 禁 LOG**：阻塞 10ms+ → 中断饿死。ISR 只记账。
6. **test_main 不得返回**：→ LOG_PANIC 死窗。用 `k_thread_join(K_FOREVER)`。
7. **改 overlay 必须删 build 全量重建**：`-p auto` 不检测 Kconfig 缓存。
8. **CONFIG_ZTEST_VERBOSE_OUTPUT=y**（迁移必查）：=n 则逐项输出被抑制，全 MISSING。
9. **CONFIG_NEWLIB_LIBC=n**（全工程统一）：=y 与 Zephyr POSIX types 冲突（pthread 重定义）。
10. **ConnectType=1**（flash 烧录）：DebugServer 会话后 ConnectType=0 卡死 80s。
    回归结束后 cleanup_cklink() 释放 DS（run_all.py try/finally）。

