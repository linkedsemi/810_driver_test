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

### 2.2 `regression/cases.yaml` 示例

```yaml
# 810_driver_test 全量回归 case 清单（路径相对 810_driver_test）
# 注释掉某行 = 屏蔽该 case（# 开头即跳过）
# gpio_toggle_test = 示波器观察型，无自动化回归（无 run_*_tests.py），默认屏蔽
cases:
  - projects\trng_test\tests\case_default
  - projects\rtc_test\tests\case_default
  - projects\otbn_test\tests\case_default
  - projects\otbn_selftest\tests\case_default
  - projects\i2c_pure_test\tests\case_pure_i2c_400k
  - projects\i3c_i2c_switch_test\tests\case_switch_core
  - projects\i3c_dualcore_test\tests\case_mixed_1m_12m5
  - projects\i3c_dualcore_test\tests\case_mixed_1m_6m25
  - projects\i3c_dualcore_test\tests\case_mixed_400k_12m5
  - projects\i3c_dualcore_test\tests\case_mixed_400k_1m
  - projects\i3c_dualcore_test\tests\case_mixed_400k_6m25
  - projects\i3c_dualcore_test\tests\case_pure_i3c_12m5
  - projects\i3c_dualcore_test\tests\case_pure_i3c_6m25
  # - projects\gpio_toggle_test\tests\case_toggle_1s
  - projects\mbedtls_test\tests\case_sw_baseline
  - projects\mbedtls_test\tests\case_otbn_ecc
  - projects\mbedtls_test\tests\case_hw_otbn_mix
  - projects\mbedtls_test\tests\case_hw_all
  - projects\mbedtls_test\tests\case_otbn_hash
  - projects\mbedtls_test\tests\case_otbn_rsa
  - projects\mbedtls_test\tests\case_otbn_all
  - projects\mbedtls_test\tests\case_otbn_sha256_hw_sm3
  - projects\mbedtls_test\tests\case_sw_hw_mix
  - projects\mbedtls_test\tests\case_all_on_max
```

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

## 5. 新建测试工程

### 5.1 用 `qsh-test-gen` Skill

新建工程/新 case 优先调用 `qsh-test-gen` skill，自动生成目录骨架、协议头、`test.sh` 三模式、`dbg_run.py`、`dump_results.py`、公共结果模块注入。

### 5.2 命名规则与目录骨架

工程目录：`810_driver_test/projects/<driver>_test/`

```text
projects/<driver>_test/
├── cpu<N>_<role>/              # 固件目录
│   └── src/main.c
├── tests/
│   ├── include/<driver>_test_protocol.h   # 共享 opcode + token 宏
│   ├── COVERAGE.md                      # 覆盖矩阵（新工程必须创建）
│   ├── expected/                        # 预期数据/黄金向量
│   ├── scripts/
│   │   ├── run_<driver>_tests.py        # 一键回归
│   │   ├── dbg_run.py                   # 单次烧录+双串口采集调试
│   │   ├── dump_results.py              # GDB 读内存兜底
│   │   └── *.gdb / *.gdbcmd             # GDB 会话脚本
│   ├── case_<速记>/                     # 每 case 一份构建
│   │   ├── test.sh                      # 三模式脚本
│   │   ├── *.overlay / prj.conf 差异
│   │   └── build/
│   └── tmp/                             # 调试临时物（git 忽略）
└── <问题总结>.md
```

角色术语必须来自被测驱动标准称谓：

| 驱动/总线模型 | 角色术语 |
|---|---|
| I3C | `controller` / `target` |
| SPI / I2C / UART | `master` / `slave` |
| 主机/设备模型 | `host` / `device` |
| 纯片内单核外设（无总线、无对侧角色） | 不用角色词，直接 `cpu1_<driver>`，如 `cpu1_trng_test` |

> `host_master/` 是临时试验，不属于规范；`controller/target` 是 I3C 专有驱动术语，非通用模板。

### 5.3 验收 checklist

1. `./test.sh` 双核编译零失败；launch.json 片段地址正确。
2. `./test.sh -g` 单 case 双核各自输出 `PASS` token（含 target 侧）；`dbg_run.py` 角色识别正确（CTRL/TGT 无 `?`）。
3. `run_<driver>_tests.py` 全量回归 `DONE:PASS`，报告归档 `reports/<date>-<idx>/` 且 `summary.md` 有本工程行。
4. `tests/COVERAGE.md` 存在，测试项覆盖 §13.1 维度、确定性测试标注预期来源。
5. 期间遇到的坑已沉淀进 §10 或对应 skill。

---

## 6. 规范参考（完整保留，后置）

> 以下各节为完整规范，供深入查阅。日常快速上手读 §1-§5 即可。

### 6.1 固件编写规范

#### 6.1.1 ztest 契约：`test_main()` 返回 = 测试结束（最重要）

- 被动驱动型测试（由对侧通过总线命令推进）必须用**事件循环线程**跑业务，但 `test_main()` **不得在创建线程后直接返回**：
  ```c
  k_thread_create(&dispatcher_thread, ..., K_NO_WAIT);
  k_thread_join(&dispatcher_thread, K_FOREVER);   /* 必须！ */
  ```
- 原因：返回后 ztest 执行 `LOG_PANIC()` → `panic_mode` 永不复位 → 同步日志 UART 阻塞造成 8ms 级死窗（实测 logspan 8115us → 11us）。详见 `tests/ZTEST_PANIC_LOG_8MS_DEADWINDOW.md`。

#### 6.1.2 ISR 内禁止打日志

- 任何 `LOG_INF/LOG_ERR/printk` 都不能出现在 ISR 回调里（deferred 模式同样影响时序）。
- ISR 只记账（计数/时间戳/标志），由 dispatcher 线程在测试结束时统一打印 `TGT_DBG`/`CTRL_STEP` 类汇总行。

#### 6.1.3 时序测量

- 用 `k_cyc_to_us_floor32` + `k_cycle_get_32` 记时间戳，不打 LOG 不拉 IO。
- 需要波形观察时用 GPIO 标记，统一走 `io_set/clr/toggle`，记入 case 注释的「调试 IO 定义」段，并与测试逻辑隔离（可编译开关）。**调试完必须移除**（见 §10.6）。

#### 6.1.4 命令协议

- opcode 用 ASCII 助记符（`'S'/'E'/'W'/'L'/'I'/'H'/'R'`），避免与 direct CCC 命令码在波形上混淆；定义与 token 宏统一放共享 `*_protocol.h`。
- target 侧 IBI/HJ 触发命令必须经事件队列交给 dispatcher 线程执行，禁止在 ISR 中调用会拿锁/互斥量的驱动 API。

#### 6.1.5 测试结果记账公共模块（记账自动化，测试函数零样板）

- 每项测试的结果**统一记账到公共模块** `common/test_results.{h,c}`（全局符号），**禁止**在各固件里定义私有 results 数组。串口不可用时靠 GDB 读内存兜底（§6.3），因此符号名、数组尺寸、取值语义必须全工程统一：
  ```c
  int8_t test_results[TR_MAX_COUNT];        /* -1 未跑 / 0 PASS / 1 FAIL */
  char   test_reasons[TR_MAX_COUNT][TR_REASON_LEN];  /* 失败原因文本 */
  void tr_init(uint8_t count);              /* 全部置 NOT_RUN */
  void tr_record(int idx, int result, const char *fmt, ...);  /* 结果记账 */
  ```
  - `TR_MAX_COUNT=64`、`TR_REASON_LEN=96` 在头文件定义，不许改（GDB 脚本按固定 stride 读 reason）。
  - **定长数组**原因：GDB 读内存要按符号地址+固定步长解析，变长/指针数组无法直接 dump。
  - 接入方式（两个固件 CMakeLists.txt 都要）：
    ```cmake
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common/test_results.c)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
    ```
  - 双核各自链接该模块，符号同名、地址不同，dump 时按 elf 区分核。
- **记账自动化（ztest 原生化后，测试函数零 `tr_record` 调用、零 EXPECT/finish 样板）**：由公共 glue `common/ztest_glue.c` 的 `zt_run_all()` 完成——`test_main` 第一行调 `zt_init(core)`（打印 `ZT_BOOT <core>`，§6.2 BOOT 判据），末尾调 `zt_run_all()`：`tr_init(TR_MAX_COUNT)` → `ztest_run_all(...)` → 套件结束后遍历 `_ztest_unit_test_list_start..end` 按 `test->stats` 批量记账（`fail_count>0 → TR_FAIL`、`pass_count>0 → TR_PASS`、`skip/未跑 → TR_NOT_RUN`）。
  - **索引 = 测试在链接段中的注册顺序（构建内稳定）**，`dump_results.py` 的 `TEST_NAMES` 必须按同一链接序对齐（SORT_BY_NAME 字典序，见 §10.27）。
  - reason 由 **zassert 自身 printk 输出**（`Assertion failed at ...`，§13.5），glue 只记统计性 reason（`ztest fail_count=%u`）；串口可用时 reason 以串口为准。
  - 双核各自链接该模块，两核的 `ztest_run_all` 都走同一套自动记账。
  - 约束：单固件测试数 < `TR_MAX_COUNT`(64)（现有最大 otbn_selftest 30 项）。

#### 6.1.6 驱动/固件注释规范（边界：只写本层语义，不写外部历史）

注释必须遵守**职责边界**：只描述**本层（驱动/固件）的行为语义与契约**，不得记录属于外部系统（Zephyr OS 框架、历史 bug、调试过程）的原因。判断标准：**"如果这段代码换到别的工程/别的 OS 配置下，这条注释还成立吗？"** 不成立就不该写在本层代码里。

| 应该写（本层语义） | 不应该写（外部原因/历史） |
|---|---|
| "TERM/URUN 在读欠载是 controller 主动提前终止的合法行为" | "panic_mode 已修（test_main 不返回，ztest 不触发 LOG_PANIC）" |
| "STOP 分支必须 FLUSHTB：清空 TX FIFO 使 prefill 从 buf[0] 起写" | "之前 LOG_ERR 阻塞 10ms 造成 8ms 死窗" |
| "opcode 用 ASCII 助记符避免与 CCC 命令码混淆" | "Windows gdb stdout 管道块缓冲导致读不到" |

外部系统问题（如 panic_mode、shell 后端阻塞、gdb 缓冲）的**记录位置**：
- 反复会踩的通用坑 → §10 已知坑 / 对应 skill（qsh-debug §2.3 等）
- 一次性定位闭环的 bug → 问题总结 .md（§7），存档后不进代码注释

调试时的临时注释（探针/断点说明）按 §10.6 探针纪律，**调试完必须清理**，不得随功能代码提交。

#### 6.1.7 log 输出规范（所有测试固件必须遵守）

- **所有 log/token 输出统一走 `LOG_INF`**（不用 printk），prj.conf 必带：
  `CONFIG_LOG=y`、`CONFIG_LOG_MODE_DEFERRED=y`（buf 模式）、`CONFIG_LOG_BUFFER_SIZE=16384`、`CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=10`、`CONFIG_LOG_BACKEND_UART=y`、`CONFIG_LOG_PRINTK=n`。
- **除了必要卡时序的地方**：ISR 内禁 log（§6.1.2）、时序测量/波形段不打 log（§6.1.3）维持不变。
- 理由：消息进 log buffer 由 log 线程异步输出——测试线程不被 UART 输出阻塞、消息不丢（UART 瞬时卡死时积压在 buffer 中）；token 行带 LOG 时间戳/模块名前缀，解析用 `re.search`（§10.5）不受影响。

#### 6.1.8 构建与 case 脚本规范

- 每个 case 独立 `build/` 目录；共享同一份源码，case 差异只体现在 overlay/prj.conf/test.sh。新 case = 新建目录 + 三件套，不改 C 代码。
- `west build` 优先 `-p auto`；改 overlay（SRAM 地址类）后必须删 build 全量重建（§10.9）。
- 每 case 的 `test.sh` 必须包含头注释模板：What is tested / CPU0/CPU1 行为 / Normal behavior / Abnormal behavior / Feedback / Hardware IO connections / Build artifacts / Usage。
- `test.sh` 三模式统一：`./test.sh`（只编译，打印 launch.json 片段）、`-g`（编译 + GDB 加载运行）、`-r`（仅加载运行，agent 模式）。构建时 `SKIP_LOAD=y` 只编译不烧录。参考 `tests/case_pure_i3c_12m5/test.sh`。

### 6.2 Agent 与结果规范

#### 6.2.1 ztest 原生输出即协议

ztest 原生化后，自定义 token 协议层已删除；所有行 printk 直出 UART，无 LOG 前缀。格式（`common/ztest_report.py` `parse_ztest` 实测正则为准）：

```text
ZT_BOOT <core>                                  # glue printk，test_main 第一行
Running TESTSUITE <suite>                       # ztest 原生，套件开始
START - <test_name>                             # ztest 原生，逐项开始
 PASS - <test_name> in X.XXX seconds            # ztest 原生，逐项判定
 FAIL - <test_name> in X.XXX seconds
 SKIP - <test_name> in X.XXX seconds
TESTSUITE <suite> succeeded|failed.             # ztest 原生，套件结束
INFO <test_name>: <k>=<v>[,<k>=<v>]             # ZT_INFO 宏，数值上报（§13.4）
Assertion failed at <file>:<line> (<func>): <msg>  # zassert 原生，FAIL reason（§13.5）
PROJECT EXECUTION SUCCESSFUL|FAILED             # ztest 原生，本核套件完成标记
CPU2 MASTER / CPU1 SLAVE ...                    # 角色 banner，保留，角色识别不变
```

- **ZT_BOOT / PROJECT EXECUTION 是所有驱动测试工程的通用标记**（由 `common/ztest_glue.c` + ztest 提供，新工程生成时注入）：ZT_BOOT 在 test_main 第一行打印（BOOT 判据）；`PROJECT EXECUTION ...` 由 ztest 在本核套件全部结束后打印（完成标记，替代旧 DONE）。harness 在 load 后收不到 ZT_BOOT → 按 §6.2.4 第 2 条 log 门禁判据处理（直接判失败，见下）。
- 逐项输出受 `CONFIG_ZTEST_VERBOSE_OUTPUT` 门控：**迁移固件 prj.conf 必须 `=y`**，否则 `START/PASS/FAIL` 行被抑制全 MISSING（§10.26）。
- 判定：**单核 = 该核全项 `PASS - <name>` + `PROJECT EXECUTION SUCCESSFUL`；双核 = 同名测试项两核各自 `PASS - <name>` + 两核各自 `PROJECT EXECUTION SUCCESSFUL`**。SKIP 项报告单独标注。
- **双核例外（i3c_dualcore_test，照 evaluate_case 实码）**：该工程 target `test_main` 不返回（§6.1.1 dispatcher join）→ TGT 端口**没有** `PROJECT EXECUTION` 行，属设计（Task 9 D1）；完成判定 = **CTRL 侧 SUCCESSFUL 且 TGT execution != "FAILED"**（样板 `run_i3c_tests.py` `evaluate_case`），TGT 失败经逐项 FAIL 行暴露。i2c_pure/i3c_i2c_switch 的 target `test_main` 正常返回，仍按两核各自 SUCCESSFUL 判。
- 记账由 **glue 自动完成**（§6.1.5：`zt_run_all()` 按 ztest stats 批量记账到 `test_results[]`），测试函数零样板；串口不可用时 GDB 读内存兜底（§6.3）。
- agent 回归输出报告并归档**会话目录** `reports/<date>-<idx>/` + 更新 `summary.md`（§12）；批量入口 = `regression/run_all.py`（§12.5）；超时用 Python `subprocess.run(timeout=...)`，不用 bash `timeout`（Git Bash 无）。
- 烧录用两遍烧录法规避 I-cache 残留旧镜像（§10.4）；单核工程已改单遍烧录（reset 冲刷 I-cache，§10.24）。

#### 6.2.2 串口不可用时：GDB 读内存取结果（dump_results.py）

- 结果数组是全局符号（链接地址=加载地址），连 DebugServer 后 `x/Ndb` 直接读，不依赖串口；失败项再 `x/s` 读 `test_reasons`（地址 = 基址 + i*96）。
- 脚本：`tests/scripts/dump_results.py`（新工程从样板复制），用法：
  ```text
  python dump_results.py --case case_pure_i3c_12m5              # 只读当前内存
  python dump_results.py --case ... --run 35                    # 先两遍烧录跑 35s 再读
  python dump_results.py --case ... --raw                       # 打印 gdb 原始输出
  ```
- 适用场景：串口 ztest 输出被刷掉/串口忙/远程无串口；**串口可用时仍以串口 ztest 输出为准**（§6.2.1 判定标准），内存结果用于兜底与交叉验证。

#### 6.2.3 批量回归判据（所有 run_*_tests.py 必须实现，i3c 样板已校准）

一次 case 的完整判定链（参数默认值实测校准，勿凭感觉改）：

1. **两遍烧录**（§10.4）：prime 轮冲刷 I-cache 旧镜像，pass-2 为正式轮。
   - prime 轮判据 = 串口出现**任何 ztest 输出**（`ZT_BOOT` / `Running TESTSUITE` / `START - `——旧镜像可能无 ZT_BOOT，等 ZT_BOOT 会误报环境失败）；prime 轮输出丢弃，pass-2 前清空采集行。
   - pass-2 判据 = **ZT_BOOT 标记**（当前固件必有）。
2. **log 门禁判定**：**等 log 有打印才开始测试**；pass-2 spawn 后 `LOAD_WINDOW=20s` 内收不到任何 log/ztest 输出 → 不深挖根因（环境/固件不区分），按 §10.22 自愈重试 1 次，仍无 log **直接判该 case 失败**（reason=no_log，继续后续 case）。正常 load 后几秒内必出 log——"load 进去没 log，直接当失败"。
3. **完成判定 = 静默判据 + 完成标记**：BOOT 之后连续 `IDLE=10s` 无新串口字节 **且已见套件完成标记**（**任一核 `PROJECT EXECUTION SUCCESSFUL|FAILED`**）→ 判 done 提前杀 gdb。**只有静默但无完成标记 = 测试中途长间隔（慢速 case 相邻逐项输出间隔可 >10s）→ 继续等**，每 15s 打一条诊断；case_timeout 硬上限兜底。10s 是"套件结束后快速收尾"的窗口，不是"测试中途"的判据。
4. **串口必须实时采集**：ztest 输出是实时流，跑完即过，事后读串口只剩 shell 提示符 → 后台线程逐行存 `lines`，判据与报告都取自该流；禁止事后一次性读串口。
5. **行解析用 `re.search()` 不用 `^` 锚点**（§10.5：shell 提示符重绘会在行首加 `uart:~$ ` 前缀）；`PASS - <name>` 按名字去重/多核区分，`PROJECT EXECUTION` 按核计数（双核两行都要）。
6. **每 case 前后 DebugServer 自愈（按需重启）**：先杀残留 gdb（1025 ESTABLISHED 对端），杀完复查——连接残留仍在（ESTABLISHED 未释放）或 server 已死（无 LISTENING）才重启；健康则**不重启**（实测 6 case 全量只重启 1 次）。重启 = taskkill + Popen + 轮询日志 `DebuggerServer$` 提示符判就绪；Python 里 taskkill/tasklist 一律单斜杠参数（§10.16）。
7. **回归前构建一致性**：固件加新标记（ZT_BOOT/PROJECT EXECUTION）后必须先构建全部 case 再回归，勿 `--skip-build` 混跑旧镜像（无 ZT_BOOT → 假 load_fail）。
8. 结果判定沿用 §6.2.1：单核 = 该核全项 PASS + SUCCESSFUL；双核 = 同名测试项双核 PASS + 两核 SUCCESSFUL 才判 case PASS（例外：i3c_dualcore_test 的 TGT 无 PROJECT EXECUTION 属设计）；串口缺项按 §12.3 内存兜底合并（来源标 mem）。

### 6.3 问题记录规范

- 定位闭环的问题写 `<BUG>_<SUMMARY>.md` 总结（现象/根因/证据/修复/验证/通用教训）存档供后续避坑；调试中的状态记 `<NAME>_STATUS.md` 并维护状态表。
- 新建工程先读样板 `projects/i3c_dualcore_test/tests/` 下已有问题总结，确认不会重犯。
- **沉淀准则**：只沉淀"会反复遇到"的坑（环境/工具链/方法学，如连接残留、I-cache、ISR 禁 LOG）进 §10 已知坑或对应 skill；**一次性问题**（修复后不再遇到的具体 bug）只写 .md 存档，不加入 §10/skill。判断标准：换机器/测试/驱动会不会再踩？不会再踩就不沉淀。

### 6.4 测试报告归档规范（所有 run_*_tests.py 必须实现）

一次回归 = 一次归档，脚本自动生成，不依赖人工收集。

#### 6.4.1 目录与文件（会话目录 `reports/<YYYY-MM-DD>-<idx>`）

```text
810_driver_test/reports/
└── <YYYY-MM-DD>-<idx>/              # 会话目录：idx = 当天第几次会话（1 起累加）
    ├── summary.md                   # 最外层，行粒度 = case（§6.4.4）
    ├── precheck_report.md           # 环境预检报告（批量跑 precheck 时写入，§6.4.6）
    ├── logs/                        # 批量时每 case 一个日志（run_all.py 写入）
    ├── <project>_<case>/            # 每 case 一个子文件夹
    │   ├── <driver>_test_report.md
    │   └── <driver>_test_report.json
    └── ...
```

- **会话目录分配/写入一律走公共模块 `common/report_dir.py`**（`allocate_session_dir()` / `case_dir_for()` / `read_summary()` / `update_summary()`），**禁止 run 脚本自算归档路径**；一次批量会话（`regression/run_all.py`）内全部 case 共用同一个会话目录；单独跑某个 case（`run_*_tests.py` 不带 `--session-dir`）时自动分配新会话目录。
- 旧格式目录（`reports/<date>/`，无 idx）不再产生，历史归档保持原样可读。
- 单次 `dbg_run.py`（调试）不归档，只归档一键回归。

#### 6.4.2 报告内容模板（Markdown）

```markdown
# <driver> 测试报告
- 日期: <YYYY-MM-DD HH:MM>  板卡: lsqsh_evb@2os  脚本: run_<driver>_tests.py
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，§14）
- case 数: N  总测试项: N  通过: N  失败: N

## case_xxx (PASS|FAIL)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| write_read_16 | 16B 0x00..0x0F | - | PASS | token | |
| monobit_8k | 46%~54% ones | ones=32775(50.0%) | PASS | token | |
```

- **预期**列：来自 COVERAGE.md 该测试项的预期描述（§13.3）。
- **实际**列：INFO 约定行（`INFO <test_name>: <k>=<v>[,<k>=<v>]`）的数值（`key=value` 拼接）；无 INFO 填 `-`。
- **来源**列：`token`（串口 ztest 输出）/ `mem`（GDB 内存兜底）。
- **reason**列：FAIL 的 reason 原文（§6.2.1/§13.5：zassert `Assertion failed at` 行）。

#### 6.4.3 内存兜底合并规则（串口缺项自动补）

回归结束（串口已关）后，脚本调用本工程 `dump_results.py --case <case> --json` 读内存结果，**只用于填补串口 ztest 输出缺失的测试项**（来源语义不变：token = 串口 ztest 输出；glue 自动记账，§6.1.5）：

| 串口/内存情况 | 判定 |
|---|---|
| 串口有 PASS/FAIL | 以串口为准（来源=token） |
| 串口缺但内存有值 | 用内存值（来源=mem，报告标注） |
| 两者都无（数组 -1） | NOT_RUN，case 判 FAIL |
| 内存读取失败（连接残留等） | 容忍：标记 `mem-unavailable`，不阻断回归 |

内存兜底在 case 跑完后立即执行（目标仍在，interrupt 可读）；`dump_results.py` 必须支持 `--json`（输出 `{"CTRL": {...}, "TGT": {...}, "reasons": {...}}` 或单核版 `{"TGT": {...}}`）；`TEST_NAMES` 必须按 **ztest_unit_test 链接序**（SORT_BY_NAME，§10.27）与 glue 记账 idx 对齐。

#### 6.4.4 summary.md 索引（会话目录最外层，行粒度 = case）

每工程脚本写完自己的报告后 `update_summary()` 更新会话目录的 `summary.md`：
- 文件不存在 → 创建并写表头；已存在 → **按 key 替换该行**（不追加重复行）。
- **key = `<project>_<case>`**（一行一个 case，i3c 7 case 各自一行）。
- 行格式：`\| [<project>_<case>](<报告相对路径>) \| <PASS\|FAIL> \| <passed>/<total> \|`
- case 级判定沿用 §6.2.1 标准（单核全项 PASS + SUCCESSFUL；双核同名项双 PASS + 两核 SUCCESSFUL；i3c_dualcore 例外 = CTRL SUCCESSFUL 且 TGT execution != "FAILED"，见 §6.2.1 双核例外）。

#### 6.4.5 一键批量总控（`regression/run_all.py` + `cases.yaml`）

批量入口 = `regression/run_all.py`（一次批量 = 一个会话目录）：

```bash
cd 810_driver_test
python regression/run_all.py                # 环境预检 + cases.yaml 全部启用 case
python regression/run_all.py --no-precheck  # 跳过预检（调试用）
python regression/run_all.py --no-erase     # 跳过全量前 flash 全片擦除
python regression/run_all.py --dry-run      # 只列启用 case 与映射脚本
```

- `cases.yaml`：YAML list（`cases:` 下每行一个 case 目录路径，相对 810_driver_test，反斜杠），`#` 开头 = 屏蔽跳过。VS Code YAML 模式下 `Ctrl+/` 可直接 toggle 注释。用例清单变更改这里，run_all.py 不硬编码 case。
- 流程（按 run_all.py 代码实际顺序）：读清单 → **flash 全片擦除一次**（`--erase-only`，DebugServer 互斥退出后重启，用户 #6；失败=环境 FAIL 批量中止）→ **分配会话目录**（§6.4.1）→ 环境预检（§6.4.6，报告写入该会话目录 `precheck_report.md`；可 `--no-precheck` 跳过）→ 逐 case 调对应工程 run 脚本（`--case` + `--session-dir`）→ 每 case 日志独立存 `session/logs/` → 结束总表 + 退出码（全 PASS=0；有 FAIL=1；预检 FAIL=2）。
- **单 case FAIL 不阻断后续 case**（日志记 FAIL 继续跑完）。
- **双核 UART 由 run_all.py 配置区显式注入**（COM15/COM16，`--ctrl-uart/--tgt-uart` 传参）——双核 run 脚本**必须显式 UART**，不支持自动发现（2026-08-29 实测）。
- **i3c skip-build 优化**：同会话首个 i3c case 触发构建，后续 6 个 case `--skip-build`。**勘误（2026-08-31 实测）**：首个 case 实际只构建它自己（run 脚本 per-case 构建粒度），并非"全量构建 7 个"——后续 case 会跑上次全量回归的旧镜像；同会话内固件源码未变时无害，**动过固件源码后必须先对 i3c 工程单独全量构建再跑 run_all**（§10.32 勘误，勿依赖 skip-build 假设）。
- 批量全程脚本自驱（构建/烧录/采集/归档/自愈在各 run 脚本内），agent 只负责启动与失败后介入。

#### 6.4.6 环境预检（`regression/precheck/run_precheck.py`，链路级）

```bash
python regression/precheck/run_precheck.py [--session-dir <dir>] [--timeout 40]
```

- **范围 = 链路级**：① 串口枚举可用（CTRL/TGT 端口存在且可开）② DebugServer 在线（GDB 连 127.0.0.1:1025）③ 双核 load + 各自 boot banner 与 ZT_BOOT ④ 两侧无静默。**不含外设接线探测**——4 个 IO 接线检测后置到**独立 IO 测试工程**（后续任务；IO 将固定接死，所有测试共用同一硬件环境，不再每次换线）。
- **任一 FAIL → 退出码 2**，批量（§6.4.5）中止，交 agent 排查（qsh-debug）；预检可独立运行；预检报告写入会话目录 `precheck_report.md`。
- 环境变量：`PRE_CTRL_UART`（默认 COM15）/ `PRE_TGT_UART`（默认 COM16）。

### 6.5 测试覆盖矩阵与预期数据规范（全面性 + 可判定性）

目标：测试不是"跑得通"，而是**对着驱动源码逐路径覆盖** + **结果可判定（预期 vs 实际）**。

#### 6.5.1 覆盖维度清单（新工程 COVERAGE.md 必须逐项对照）

| 维度 | 要求 | 示例 |
|---|---|---|
| 功能通路 | 驱动对外 API 的正常路径 | entropy_get_entropy 基本获取 |
| 边界长度 | 长度参数边界（API 类型极限、驱动内部缓冲阈值±1） | uint16_t 最大 65535、驱动 pool 128B 的 127/128/129 |
| 异常参数 | 非法入参必须返回明确错误码 | NULL/len=0 → -EINVAL |
| 统计/性质 | 随机/校验和类性质的统计门限（不 flaky） | monobit ±16σ |
| 并发 | 多线程/多实例同时访问（锁与状态隔离） | 双线程同时取熵 |
| 中断/时序 | ISR 路径、busywait 路径、驱动隐藏状态机分支 | get_entropy_isr、>64KB ADVANCE_STATE |
| 复位/掉电 | 时钟/复位路径（驱动 init 内） | clock_control_on/off 分支 |

**覆盖要求**：驱动源码里每个条件分支至少有一个测试项触及；COVERAGE.md 里每个测试项标注 `驱动路径` 列（函数名+分支）。

#### 6.5.2 预期数据规范

- **确定性测试必须有预期值并在固件内比对**（不是打印了事）：
  - 通信类：固定 pattern + 状态字（预期字节序列/预期状态寄存器值）
  - 加密类：**NIST/官方测试向量**，放 `tests/expected/`（如 `expected/nist_vectors.h`），固件 include 后逐字节比对；qsh-test-gen 生成加密类工程时从向量库抽取向量生成该文件
  - 数值类：统计门限（如 monobit 46%~54%）
- 预期值要随工程可复现：COVERAGE.md 里注明预期数据来源（标准/向量文件/公式）。

#### 6.5.3 COVERAGE.md 模板

```markdown
# <driver> 测试覆盖矩阵
| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| basic_16 | ls_trng_get_entropy 主循环 | 功能通路 | ret==0 | API 契约 | ✅ |
```

每个测试项一行；`预期来源` 注明协议文档/NIST 向量/统计公式/API 契约。

#### 6.5.4 INFO 约定行（实测数值上报，ZT_INFO 宏）

- 固件在测试内把数值型实测结果打 `INFO` 约定行（§6.2.1，`ZT_INFO(name, fmt, ...)` 宏 = `printk("INFO %s: " fmt "\n", name, ...)`），报告"实际"列取用；统计类测试（吞吐/占比）必须上报，否则报告里预期 vs 实际无从对比。
- 行格式：`INFO <test_name>: <k>=<v>[,<k>=<v>]`（key 小写下划线），**name 必须与 `ZTEST(suite, name)` 注册名一致**。实测例：`INFO throughput_64k: bytes=65536,us=14701,mbps=35.66`。
- 上报时机：对应测试的 PASS/FAIL 之前；解析器 `re.search(r"INFO (\S+):\s*(.*)")`（§10.5 前缀污染不变），k=v 按逗号切分进报告"实际"列（§6.4.2）。

#### 6.5.5 FAIL reason 可归因格式（来源 = zassert 输出）

`FAIL` 的 reason 来自 **zassert 原生输出**（断言失败时 ztest 自身 printk，`CONFIG_ZTEST_ASSERT_VERBOSE` 控制；成功后不打）：
`Assertion failed at <file>:<line> (<func>): <msg>`——解析器暂存断言行，遇到 `FAIL - <name>` 行归到该项（`<file>` 为固件按工作区相对路径**原样打印**（相对 `WEST_TOPDIR`），解析器无裁剪逻辑，实测例）：
`810_driver_test/projects/rtc_test/cpu1_rtc_test/src/rtc_test.c: 85:
rtc_set_get_roundtrip: (g.tm_mday not equal to s.tm_mday)`。
**判定要求**：断言必须用**带值对比的 zassert 宏**（`zassert_equal` / `zassert_within` / `zassert_mem_equal` / `zassert_false` 等），使失败文本含期望/实际值（`ret:1 != -EINVAL` 类），**禁止**只写"data mismatch"这类无信息文本；串口缺项时内存兜底 reason 为 glue 统计文本（`ztest fail_count=%u`，§6.1.5）。

### 6.6 外部环境确认规范（芯片外部连接路径）

测试项存在**芯片外部连接路径**（外设引脚连到对端设备/另一芯片、电平/上拉依赖外部电路、UART 环回等）时，生成与执行都要先确认环境：

#### 6.6.1 创建测试工程前（qsh-test-gen 必做）

- 生成前先判断被测外设是否有外部连接路径：
  - **有**（如 I3C 总线对连、UART 环回、GPIO 互连、外接传感器/EEPROM）→ **先问用户外部环境**（具体接线、对端设备型号、电平/上拉/速率要求），用户告知后才能开始生成工程；
  - **无**（纯片内外设：TRNG、加密引擎、片内存储、看门狗等）→ 无需确认，COVERAGE.md 标注"片内"。
- 用户提供的外部环境写入两个地方：
  - 顶层 `810_driver_test/README.md`「外设接线记录」表（工程级）
  - 工程 `tests/COVERAGE.md` 涉及外部路径的测试项标注 `外部: <接线描述>`

#### 6.6.2 执行具体测试前（qsh-test-run 必做）

- 跑回归/单 case 前，若涉及外部连接路径：**先向用户报告本次测试环境**（接线状态、对端设备、串口号、速率配置），确认后再开始烧录/采集。
- 测试报告归档（§6.4.2）头部固定带一行环境说明，指向 README.md 接线登记表，保证结果可复现；环境变更（改接线/换对端）后重新回归，不要复用旧报告。

#### 6.6.3 判定示例

| 外设 | 外部路径？ | 创建前动作 | 执行前动作 |
|---|---|---|---|
| I3C 双核对连 | 有（I3C5↔I3C6 跨核接线） | 问接线/上拉/速率 | 报告接线状态 |
| TRNG | 无（片内熵源） | 直接生成 | 无需报告环境 |
| UART 环回测试 | 有（TX↔RX 短接） | 问是否环回/电平 | 报告环回状态 |

### 6.7 XIP 固件固化与启动测试（CSKYFlashProgrammer CLI）

把测试固件烧进板载 SPI flash，板卡复位后从 flash 启动（XIP 执行或 boot 拷贝）。与 RAM load（GDB 加载到 SRAM）互补：RAM 模式断电丢失、每轮重载；flash 固化是持久化的，适合 XIP 启动类测试 / 量产镜像。

#### 6.7.1 工具链

| 项 | 值 | 说明 |
|---|---|---|
| CLI | 工程内打包副本优先：`tools/flash_program/CSKYFlashProgramerConsole.exe`；回退 `C:\user_file\EC_FLASH_PROGRAM\FlashProgrammer\Bins\CSKYFlashProgramerConsole.exe` | 已打包进工程（自包含，含 Cklink.dll 固件等全部依赖）；注意拼写 "Programer"（GUI 是 "Programmer"）；`-f <config.json>` 必需 |
| 算法 | 工程内 `tools/flash_program/qsh_flash_algo.elf`；回退 `C:\user_file\EC_FLASH_PROGRAM\qsh_flash_algo.elf` | flash 芯片驱动；LE 系列用 `leo_flash_algo.elf`（也在 tools 内） |
| 会话日志 | `tools/flash_program/flash-programer.log`（随 FP_EXE 所在目录） | 追加写，按偏移 diff 会话（§10.13）；打包时已清空旧日志 |
| 路径默认 | `flash_program.py` 与 `test_common.sh`（`FP_EXE`/`FP_ALGO`）均为工程内优先 + 外部回退；用户可用环境变量覆盖 | 打包副本不依赖外部 C:\user_file，拷贝 810_driver_test 即自包含 |
| config | JSON：`DebugServerSetting` + `FlashProgramSession`（CommandType 0=写/1=读、TargetConfig.AlgorithmFile/EraseType 0=全片/1=扇区/2=范围/3=不擦/ConnectType/Timeout/ResetAndRun、TargetObjectArray 按序 Program+Verify，Type 0=ELF/1=Hex/2=Bin+TargetAddr/3=规则数据） | 由 `scripts/flash_program.py` 生成，勿手写 |

#### 6.7.2 flash 地址约定

| 镜像 | 地址 | 说明 |
|---|---|---|
| cpu1（1os/2os 都烧这个核） | `0x08000000` | XIP flash 基址 |
| cpu2（仅 2os） | `0x08100000` | 与 cpu1 镜像相隔 16MB |

> **地址与启动信息必须在脚本中自己掌握**：地址约定集中定义在 `scripts/test_common.sh` 环境变量（`CPU1_XIP_ADDR`/`CPU2_XIP_ADDR`/`FP_EXE`/`FP_ALGO`），case 的 test.sh 引用这些变量，不散落硬编码；涉及 XIP 的 case 在 test.sh 头注释里**明确声明本 case 的启动模式**与所用地址。

#### 6.7.3 多核运行模式（每次固化前先想清楚本 case 用哪种）

**核心机制**：
> **只要任一核固件是 XIP 的，所有核的固件都必须烧进 flash**——另一个核即使是 RAM 固件，也先把它的 bin 烧到 flash 对应区（cpu2 的 RAM 固件烧 `0x08100000`），由 XIP 核的 boot 代码负责把它引导启动。**只有单核运行、或双核全部 RAM 的场景，才可以直接经 CKLink（DebugServer + GDB）load 到 RAM 执行。**

| 模式 | cpu1 | cpu2 | 操作 |
|---|---|---|---|
| **XIP 标准组合（默认 XIP 场景）** | flash `0x08000000`（XIP 变体构建，XIP 执行 + boot 引导） | flash `0x08100000`（**标准 2os 构建的 RAM 固件**，由 cpu1 boot 搬运启动） | cpu1 用 `@2os_xip` 构建、cpu2 用 `@2os` 构建；两个 bin 一次会话烧进 flash（§10.14），`--reset-and-run` 复位后 cpu1 XIP boot 统一引导 |
| 全 RAM（单核或双核，默认测试） | RAM（`load` ELF@0x100c0000） | RAM（`restore bin@0x10000000`） | 不烧 flash，直接 CKLink GDB load（现有两遍烧录法） |

要点：
- **没有"一个核烧 flash、另一个核 GDB load RAM"的混合模式**：涉及 XIP 就全烧 flash（cpu2 烧的是标准 2os 构建的 RAM 固件 bin，§6.7.4）；纯 RAM 才走 CKLink
- **cpu2 的标准 2os 构建也会产出 `zephyr_flash_*.bin`**（flash 段镜像），烧录就用它，不是 zephyr.bin
- **1os 只有 cpu1**：1os_xip 固化只烧 `0x08000000`；2os XIP 标准组合才需要 cpu1+cpu2 两个区
- 固化前确认：本 case 是否涉及 XIP → 涉及则列出**全部核**的构建变体与 flash 地址并一次会话烧录；纯 RAM 则照常 GDB。地址写进 test.sh 头注释与运行记录，避免"以为在跑 flash 镜像其实跑的是 RAM 旧镜像"

#### 6.7.4 构建 XIP 变体（默认标准：cpu1 XIP + cpu2 RAM）

默认测试（单核/双核）= 全 RAM（现有 §3 流程，不烧 flash）。
XIP 场景的标准组合 = cpu1 XIP + cpu2 RAM：
- cpu1 用 XIP 变体构建（XIP 执行 + boot 引导），烧 `0x08000000`
- cpu2 用**标准 2os 构建（RAM 固件）**，产物也烧进 flash `0x08100000`，由 cpu1 的 XIP boot 代码搬运启动（§6.7.3）

```bash
# cpu1: XIP 变体（2os_xip）——分别用 -d 区分构建目录
cd linkedsemi_zephyr_project/samples/<app>
west build -p auto -b lsqsh_evb@2os_xip/lsqsh/cpu1 -d build_xip_cpu1 .
# cpu2: 标准 2os（RAM 固件，不烧 XIP 变体）
west build -p auto -b lsqsh_evb@2os/lsqsh/cpu2 -d build_cpu2 .
```

- XIP board 名 = `lsqsh_evb@{os}_xip/lsqsh/cpu{N}`（实测 `@1os_xip`/`@2os_xip` 都可用；`{os}`+`xip` 都在 `@` 后，cpu 后缀不带 xip；1os 只有 cpu1）
- 产物：cpu1（XIP）→ `build_xip_cpu1/zephyr/zephyr_flash_bram_xip_*.bin`（= BRAM boot header + flash 镜像合并，烧录用它）；cpu2（标准 2os）→ `build_cpu2/zephyr/zephyr_flash_*.bin`（flash 段镜像，烧录也用这个）

#### 6.7.5 烧录（`scripts/flash_program.py`）

```bash
# 标准 XIP 组合（cpu1 XIP + cpu2 RAM，§6.7.4）：两个 bin 一次会话烧录
python 810_driver_test/scripts/flash_program.py \
    --bin build_xip_cpu1/zephyr_flash_bram_xip_xxx.bin@0x08000000 \
    --bin build_cpu2/zephyr_flash_xxx.bin@0x08100000 --verify

# 单核（1os 或只烧 cpu1）：烧 + 校验
python 810_driver_test/scripts/flash_program.py --bin zephyr_flash_xxx.bin@0x08000000 --verify

# 读回 dump + 与源文件比对
python 810_driver_test/scripts/flash_program.py --dump-out rb.bin \
    --dump-addr 0x08000000 --dump-len 0x1000 --compare cpu1.bin
```

- 环境变量覆盖：`FP_EXE` / `FP_ALGO` / `FP_LOG`（test_common.sh 风格）
- 结果：每对象 `FP_OK: <name>@<addr>`，失败 `FP_FAIL` + 退出码 1
- 判定为双通道（CLI 退出码 + stdout/日志关键字），实测校准见 §10 追加
- 烧录前 `flash_program.py` 检测 DebugServer/gdb 进程并警告（§10.2 互斥），`--kill-debugserver` 强制清理

#### 6.7.6 启动测试（`dbg_run.py --xip`）

```bash
# 单核：烧录(带 ResetAndRun) → 从 flash 启动 → 串口采集
python tests/scripts/dbg_run.py --xip \
    --bins zephyr_flash_xxx.bin@0x08000000 --timeout 15 [--prefix HELLO]

# 双核：两个 bin 一次烧，启动后双串口采集
python tests/scripts/dbg_run.py --xip \
    --bins cpu1.bin@0x08000000,cpu2.bin@0x08100000 --timeout 15

# 烧完经 DebugServer 复位启动（--reset-and-run 无效或已关闭时）
python tests/scripts/dbg_run.py --xip --bins ... --no-reset-run --gdb-reset
```

- 与 RAM 模式差异：**无两遍烧录法、无 GDB load**；烧录成功即从 flash 启动
- 串口采集/角色识别/token 解析与 RAM 模式共用；token 前缀用 `--prefix` 指定（默认 `I3C_TEST`），烧任意固件（如 hello_world）时按需设置
- 启动方式（ResetAndRun vs DebugServer reset）以实机实测为准，回填本节约定

### 6.8 已知坑清单（实测，逐条沉淀）

1. **gdb 被杀后 DebugServer 连接残留**（高频）：`continue` 超时被杀（dbg_run.py 的 kill）后，DebugServer 仍认为连接中；下次 `target remote` 能连上但 `reset`/`load` 可能无效 → 固件不跑，串口 0 行或只有 `\x00` 复位噪声。
   **处理**：先做一次干净连接再重跑：
   ```bash
   riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" -ex "detach" -ex "quit"
   ```
   还不行就重启 DebugServerConsole.exe。判断"目标真的在跑"：串口出现角色 banner（"CPU2 MASTER"/"CPU1 SLAVE"）。
2. **DebugServer 起不来**：报 "CKLINK may be used by other program" → FlashProgrammer（GUI `CSKYFlashProgrammer.exe` 或 CLI 烧录，§6.7）占用 CKLink。反方向同理：**烧 flash 时 DebugServer 必须已退出**（`flash_program.py` 会检测警告，`--kill-debugserver` 强制杀）。切换场景时先确认对方已退出：`tasklist | grep -i -E "cskyflash|debugserver"`。
3. **不要用 netstat 判离线**：1025 由 PC 进程 DebugServer 提供（非固件内置），判断在线 = GDB 连一次看 "Remote debugging"。用户明确纠正过。
4. **两遍烧录法**：单遍烧录 + continue 会因 I-cache 残留跑旧镜像（"慢一拍"）。同一镜像烧两遍，第二遍抓取；prime 轮串口输出丢弃。
5. **token 行污染**：shell 提示符 ANSI 重绘（`\x1b[8D\x1b[J`）会混入行首；token 前可能有 `uart:~$ ` 前缀 → 解析用 ANSI 剥离 + `re.search()`，不用 `^` 锚点和 `.match()`。
6. **调试探针必须清理**（用户要求，防污染其他工程）：`log_core.c` / `kernel/timeout.c` / `__assert.h` 等被插桩的文件用 `git checkout` 恢复；驱动/固件手工删除后 `grep` 确认无残留。**功能修复与调试残留要区分**：MPIL workaround（soc_isr_stacking_e906.c unstack 恢复 MPIL）、pmp.c 的 PMP_W（XIP 区可写）是功能修复，必须保留；`log_dbg_*`/`to_dbg_*`/`e906_irq_log_*`/PJ06 标记/probe 采样/while(1) 陷阱是调试残留，一律删。用 `git diff` 逐文件甄别。
7. **ISR 内禁 LOG**（§6.1.2）：LOG_ERR 走 shell 后端阻塞 10ms+/条，直接导致 8ms 级中断饿死。ISR 只记账。
8. **test_main() 不得返回**（§6.1.1）：返回即 LOG_PANIC → 同步日志死窗。
9. **改 overlay 删 build 全量重建**（§6.1.8）：`-p auto` 不检测 Kconfig 缓存。
10. **COM 口被占用**：`tasklist | grep python` 找残留 pyserial 进程 `taskkill //F //PID <pid>`。
11. **GDB 无 Python**：验证逻辑全用 `-ex` 命令链；`continue &` + `interrupt` 不可靠，用同步阻塞 continue + 超时。
12. **prime/capture 输出混合**：两轮烧录的输出会混排造成测试顺序错乱假象，串口只在 capture 轮开。
13. **XIP 烧录会话日志**：CLI 烧录追加写 `Bins\flash-programer.log`（与 GUI 共用），`flash_program.py` 按文件偏移量 diff 出本会话新增段用于判定；排障先看该文件尾部（"CKLINK may be used"、"verify fail" 等关键字）。
14. **双核固化必须一次会话**：EraseType=0（全片擦）在编程前擦整个 flash；分两次 CLI 调用烧 cpu1/cpu2 时第二次会擦掉第一个核的镜像。双核 XIP 用 `--bin a@0x08000000 --bin b@0x08100000` 一次烧（或 `--erase 2` 范围擦）。
15. **调试临时物统一放 `tests/tmp/`**：一次性探针脚本、GDB 断点脚本、串口抓包、临时采集脚本一律放工程 `tests/tmp/` 下（gitignore 规则 `projects/*/tests/tmp/` 已全局忽略，含 `__pycache__/`），用完无需删；但**通用脚本不准放这里**——判断标准：别人/别的工程会不会直接调用它？会（dbg_run/dump_results/run_*_tests/GDB 会话脚本）→ 留 `tests/scripts/` 永久提交；不会（一次性调查产物）→ `tests/tmp/`。
16. **Python subprocess 调 Windows 命令不能用 MSYS 双斜杠**（脚本层高频）：Git Bash 里 `taskkill //F //PID x` 有效（MSYS 把 `//F` 转成 `/F`），但 **Python subprocess 没有 MSYS 转换层**，`//F` 原样传给 taskkill → "无效参数/选项 - '//F'" 且进程没被杀；若代码用 `except: pass` 吞异常，表现为"taskkill 杀不掉/杀不干净"假象（实测：`tasklist //FI` 过滤失效显示 `?`、残留 DebugServer 占 1025、新实例绑定失败卡初始化全是这个根因）。**subprocess 里一律单斜杠**：`["taskkill", "/F", "/PID", pid]`、`["tasklist", "/FI", "PID eq x", "/FO", "CSV"]`。排查"杀不掉"先查参数斜杠。
17. **DebugServer 连接残留的脚本化自愈（run_*_tests.py 的 clean_gdb_residue 模式）**：detach 不能真正释放连接（任何"连接→detach→退出"的 gdb 都留残留，探测连接也制造残留）；唯一可靠清理 = **重启 DebugServer 进程**。脚本化四步：① netstat 找 1025 上 ESTABLISHED 的对端 PID（是 gdb 则 taskkill /F /PID）；② taskkill /F /IM DebugServerConsole.exe；③ netstat 精确杀 1025 LISTENING 兜底（/IM 偶发杀不净）；④ Popen 重启（stdout 重定向到日志文件），**轮询日志出现 `DebuggerServer$` 提示符判就绪**（≤30s）——不能只固定 sleep（CKLink 初始化耗时随机）。就绪后 spawn 前打 1025 LISTENING 的 PID 便于诊断。
18. **回归前必须构建全部 case（勿轻易 --skip-build）**：固件加新标记（BOOT/DONE）后若只重建过部分 case，`--skip-build` 跑全量时旧镜像 case 无 BOOT → pass-2 等 BOOT 超时报"测试环境有问题"假 load_fail。判断"环境问题"前先确认该 case 的 build/ 是否包含当前固件（token 标记齐不齐）。
19. **Python subprocess 里裸 `bash` 会被 WSL 抢解析**：Windows CreateProcess 先搜 System32 再搜 PATH，`C:\Windows\System32\bash.exe`（WSL 启动器）装了 WSL 的机器上永远赢过 Git Bash → test.sh 在 WSL 里跑，`west` 找不到，build rc=127（错误路径显示 `/mnt/d/...` 是特征）。修法：脚本里显式解析 Git Bash 完整路径（run_i3c_tests.py 的 `_find_git_bash()`：GIT_BASH 环境变量 → shutil.which 过滤 system32/windowsapps → Git 常见安装路径），不要 `subprocess.run(["bash", ...])`。
20. **DebugServer 重启后"不稳定期"**（2026-08-27 实测 2/2 复现）：DebugServer 新实例（PID 变化）后**立即连跑** → 间歇性环境异常：pass-1 无 token（load_fail）、pass-2 gdb 中途断连（"Remote communication error. Target disconnected"）、WDR 类时序测试 critical 异常缩水（len64 提前 20+ 档）、早期测试 NOT_RUN（固件状态错乱）。实例稳定 ~30min 后连跑 7 case 无任何环境失败。**处理**：run_i3c_tests.py 已实现自动修复——case 环境失败/load_fail → `restart_debugserver()`（含 30s 稳定期）→ 重试该 case 1 次；重试仍失败才中止/判 FAIL。**手动排查时**：重启 DebugServer 后先等 ~30s 稳定期再烧录，勿立即连跑。判断"新实例"= tasklist 里 PID 变了。
21. **dbg_run.py 不判定 PASS/FAIL**（易误读）：`finish_summary` 只打印 SUMMARY/角色识别，**从不解析 PASS/FAIL token，退出码恒 0**（唯一非零退出 = flash 失败）。日志里有 `CTRL:FAIL` 但 RC=0 是常态——**dbg_run 是调试工具，不能当判定**；结果判定一律走 run_i3c_tests.py 报告（§6.2.1 双核双 PASS）。用 dbg_run 链跑多个 case 时，判 PASS 前必须 grep 日志里的 `CTRL:FAIL|TGT:FAIL` token。
22. **run_i3c_tests.py 的 load_fail 自动修复重试**（§6.2.3 补充）：case 的 pass-1 无 token（RuntimeError）或 pass-2 load_fail 不再直接中止——自动重启 DebugServer（含 30s 稳定期，§6.8.20）后重试该 case 1 次；重试成功则该 case 正常判定，重试仍失败才中止/判 FAIL。回归日志里 `[RETRY] <case>: load_fail（环境问题）→ 重启 DebugServer 后重试` 表示该 case 经历过一次环境自愈。
23. **GDB 写包参数提速 CKLink load 41x**（2026-08-28 实测）：load 前发 `set memory-write-packet-size 16384` + `set remotewritesize 16384`，实测 192KB 镜像 1KB/s → **41KB/s（41x）**，load 时长 ~190s → ~5s。"bytes/write" 仍 ~3.1KB（stub 收包上限未变），提速来自写请求流水线化。已固化进单核 otbn 工程 dbg_run.py / run_*_tests.py（默认 16384，`--wpacket-size 0` 关闭）；stub 不接受时回 `Remote failure`/`E22` 且 wait_load_done 立即失败，不会静默卡死。**新单核工程移植 dbg_run.py 时一并带上此参数**。
24. **单会话内 interrupt 后的二次 load 静默挂死**（2026-08-28 实测 2/2）：同一 gdb 会话里第一次 load 成功后 interrupt、再发第二个 `load`，gdb **无任何输出**（连 "Loading section" 都没有），只能超时 proc.kill() 收场，且留下 DebugServer 连接残留（§6.8.17 重启才干净）。**处理（单核工程）**：dbg_run.py / run_*_tests.py 改**单遍烧录**（reset → load → set $pc → continue 直接采首轮）：reset 已冲刷 I-cache（§6.8.4 的"慢一拍"场景被 reset 覆盖），load 后 .data/.bss 天然干净，不再需要 pass-2 重 load；结果与两遍烧录逐项一致。免烧迭代用 `--no-load`（attach → sanity 查 0x100c0000 镜像在 → `set $pc=入口` 重跑，等效软复位，0 烧录）。**双核 i3c 工程维持 §6.2.3 两遍烧录不变**（prime 轮确实有用——WDR 类时序测试）。
25. **一次性 -ex 链的 continue 竞态**（2026-08-28 实测 3/3 复现）：gdb 用 `-ex reset -ex load -ex "set $pc=..." -ex continue` 命令链时，continue 在 load 刚完成的瞬间发出，DebugServer 状态不佳时该命令被**静默丢弃**——CPU 停在 halt 无 BOOT token，且 teardown 时 detach 会恢复 CPU 运行 → 套件在 teardown 窗口才跑起来，零散 token 骗过主流程 BOOT 检查产出半截报告。**处理**：run_*_tests.py 改交互式流程（与 dbg_run.py 同款）：reset+load 后**等 gdb 打出 "Transfer rate"** 再 set $pc+continue；另加 90s 无 BOOT 早停置 no_boot 标志**强制** [RETRY] 自愈（重启 DebugServer 重试），不再放行部分采集。**新单核工程生成脚本时照此交互式模式，勿用 -ex 链**。**例外（2026-08-31 最终交叉审查补注）**：trng/rtc 两工程的 run/dbg_run 脚本保留一次性 -ex continue 链（T4/T5 裁定保留，多轮回归实测零 [RETRY] 零竞态）——属已裁定存量，不视为违规、勿"顺手改链"；新工程仍一律交互式。
26. **ztest 逐项输出被 CONFIG_ZTEST_VERBOSE_OUTPUT 门控**（迁移必查，2026-08-29）：prj.conf 显式 `CONFIG_ZTEST_VERBOSE_OUTPUT=n`（trng/rtc 曾有此行）时，逐项 `START - <name>` / ` PASS - <name>` 行被抑制（tc_util.h TC_START_PRINT/TC_END_PRINT 走 print_nothing），串口只见 `PROJECT EXECUTION ...`（该行不门控）→ harness 按 ztest 原生输出判定会全部 MISSING 假失败。**处理**：ztest 原生化固件的 prj.conf 必须 `CONFIG_ZTEST_VERBOSE_OUTPUT=y`，构建后核对 .config；已有工程显式 =n 的必翻。本树 ztest 头在 `zephyr/subsys/testsuite/ztest/include/zephyr/`（非上游 include/ 路径）。
27. **ztest_unit_test 链接序 = 字典序（SORT_BY_NAME，GDB 兜底对齐，2026-08-29）**：链接器对 ztest_unit_test 段 SORT_BY_NAME → 注册顺序按测试名**字典序**，不是源码序。ztest_glue 的 tr_record idx 与 dump_results.py 的 TEST_NAMES 列表若按源码序写，GDB 兜底结果会张冠李戴（全 PASS 时潜伏不可见）。**坑中坑（同 2026-08-29 实测）**：TYPE_SECTION_ITERABLE 给段名追加**尾下划线**，SORT_BY_NAME 比较的是完整段名——数字字符 `'0'-'9'(0x30-0x39)` 排在 `'_'(0x5F)` **之前**，裸名字符串排序与真实链接序可能**相反**（如 st_sha3 vs st_sha384：链接序 sha384 在前，plain nm 名字典序却显示 sha3 在前）。**处理**：每工程用 `nm --numeric-sort <elf> | grep ztest_unit_test`（按地址序 = 链接序）或对照 `zephyr.map` 的 `._ztest_unit_test.static.*` 段顺序核实；**勿用 plain nm 名字典序输出**（会被尾下划线欺骗）。TEST_NAMES 按链接序对齐并注释 SORT_BY_NAME；增删测试项后重新核对。
28. **native gdb 拒 MSYS 路径（Git Bash 下 test.sh -g，2026-08-29 实测）**：Git Bash 里把 `/d/...` 风格路径直接传给 gdb 报 "No such file or directory"（MSYS 只转换 bash 参数，gdb 不认识 POSIX 路径）——test.sh -g 首个失败形态。**处理**：脚本里用 `cygpath -m` 转 Windows 路径再传 gdb（test_common.sh 的 win_path 模式，load_and_run_single 已修；双核函数同类处理）。
29. **test.sh -g 退出码 124 是设计行为**（2026-08-29 实测）：-g 模式由 timeout 杀阻塞 continue 收场，RC=124 ≠ 失败。加载成功证据 = gdb 输出 "Transfer rate"（写包提速下 41 KB/sec）+ 串口 banner/ZT_BOOT。**判 -g 成败看日志，不看退出码。**
30. **共享树被其他会话修改**（多会话并发工作流，2026-08-29 实测）：zephyr 树/共享模块被另一活跃会话改动（如 OTBN 层升级：ls_otbn_config.h 移除 API 而 mbedtls rsa_alt.c 调用点未同步）时，构建失败（隐式声明错误是特征）或回归结果存疑。**处理**：构建前 re-stat 相关目录 mtime；构建失败先查是否共享源 API 变更；他人工作流正在动的共享源**一律不碰**（不代改 glue/调用点），等树稳定（mtime 一段时间不变）再构建回归；驱动行为因此变化时不得自行研判，报告差异交用户协调。
    **硬件串扰分支**（同 2026-08-29 实测）：同板被另一会话跑回归时，他方固件输出会串进我方采集端口，他方 `PROJECT EXECUTION ...` 完成标记会触发我方提前杀 → 半截报告 + 会话目录污染（误记 FAIL）。**处理**：板被占用时不跑硬件回归；释放判据**不能只看进程**——他方 gdb/python 进程退出 ≠ 板已释放，RAM 镜像在他方采集结束后仍可能持续执行（cpu2 固件陷入 reset/重试循环实测 spam 每 1-2s）→ 加端口静默检查（对候选串口直接听 10s 无输出）；完成标记/静默判据**只认本固件 ZT_BOOT 已见（boot_seen）的端口**（他方固件无 ZT_BOOT，其标记一律忽略；比 banner 判定更强）；被污染的同会话目录用 `--session-dir` 指回覆盖写纠正。他方残留镜像属他方工作区，不代清（断电重启板可彻底安静，由用户决定）。
31. **ztest 的 duration = cycle-counter 活跃时间，非墙钟**（2026-08-30 迁移后回归报告核对）：`PASS - <name> in X.XXX seconds` 里的时长来自 tc_util.h（`get_start_time_cyc`/`get_test_duration_ms`：`k_cycle_get_32()` 计时，`k_cyc_to_ms_ceil32` 换算）——**CPU 活跃时间**，WFI 睡眠/等待事件不计入，与墙钟差异可达秒级（实测 `(0.282s)` 类时长远小于实际挂钟经过时间）。**处理**：回归报告的逐项时长只能当 CPU 活跃时间看（如"该项跑了多少 CPU 时间"），不能当测试总耗时/墙钟；总耗时以 case 级日志时间戳与 run 脚本 case_timeout 为准；对比两次回归的时长差异前先确认时钟/睡眠路径没变。
32. **i3c WDR 时序项间歇性 critical 机制异常**（2026-08-30/31 两轮实测，已知间歇项）：i3c_dualcore_test 的 `write_delay_read_len`（WDR 判定项）在批量回归中约 **1 case/轮**出现 critical 扫描机制级异常——输出非窗口值（实测 critical_ns=50、2550，正常窗口 5200-7650ns / 107-123 档），伴 `first_short_step=-1`，zassert `ctrl_failed is true`（i3c_controller_test.c:197）。特征：**宿主轮转**（连续两轮分别落在 1m_6m25 / 1m_12m5，非固定 case）、复跑即恢复、排除 DebugServer 稳定期变量（主动重启 + 30s 后仍出现）。背景：08-29 驱动更新后该项 margin 仅 ~16%（正常值 5200-7250 vs 阈值 4450）。**处理**：基线签名容忍 ≤1 case 此异常（复跑即恢复即视为瞬态）；批量回归遇单个 WDR 机制异常先复跑该 case 再判偏离，不直接立案。驱动侧根因归 i2c_frequency 分支 `scl-overhead-ns`（时序开销配置）工作流，落地后复验。另见勘误：run_all.py 的 i3c skip-build 优化实际只构建首个 case（"首个全构建"假设不成立）——固件源码变过时后续 case 可能静默跑旧镜像，修正前凡动过固件源码必须先对 i3c 工程做全量构建（勿依赖 run_all 的 skip-build 假设）。
