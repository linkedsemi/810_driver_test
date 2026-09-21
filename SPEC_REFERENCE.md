# 810_driver_test 规范参考（从 CLAUDE.md §6 拆出）

> 固件编写、结果判定、报告归档、覆盖矩阵、XIP 烧录等完整规范。
> CLAUDE.md 只保留项目背景 + 路由索引，详细内容在本文档和对应 skill 中。

| 章节 | 详细规范位置 |
|------|-------------|
| §6.1 固件编写规范 | 本文件 + `qsh-test-gen` |
| §6.2 Agent 与结果规范 | 本文件 + `qsh-test-compare` |
| §6.3 问题记录规范 | 本文件 |
| §6.4 测试报告归档规范 | 本文件 + `qsh-test-compare` |
| §6.5 覆盖矩阵与预期数据 | 本文件 + `qsh-test-gen` |
| §6.6 外部环境确认 | 本文件 + `qsh-test-gen` |
| §6.7 XIP 烧录 | 本文件 + `qsh-test-run` §8 |
| §6.8 Supervisor 运营规范 | 本文件 + `runner` agent |
| §6.9 固件测试日志标准（token 契约 + 单出口） | 本文件 + `qsh-test-gen` / `qsh-test-compare` |
| §6.10 L1 补丁契约与 INFO kv 解析 | 本文件 + `regression/replay.py` |
| §6.11 管道 / 转义 / 新鲜度 / 烧录窗口（B2-fix） | 本文件 + `qsh-test-run` / `qsh-debug` |

> 分层与"改哪层要验什么"的速查索引见 [`FRAMEWORK_MAP.md`](FRAMEWORK_MAP.md)。

---

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
- 需要波形观察时用 GPIO 标记，统一走 `io_set/clr/toggle`，记入 case 注释的「调试 IO 定义」段，并与测试逻辑隔离（可编译开关）。**调试完必须移除**（见 KNOWN_ISSUES #6）。

#### 6.1.4 命令协议

- opcode 用 ASCII 助记符（`'S'/'E'/'W'/'L'/'I'/'H'/'R'`），避免与 direct CCC 命令码在波形上混淆；定义与 token 宏统一放共享 `*_protocol.h`。
- target 侧 IBI/HJ 触发命令必须经事件队列交给 dispatcher 线程执行，禁止在 ISR 中调用会拿锁/互斥量的驱动 API。

#### 6.1.5 测试结果记账公共模块（记账自动化，测试函数零样板）

- 每项测试的结果**统一记账到公共模块** `common/test_results.{h,c}`（全局符号），**禁止**在各固件里定义私有 results 数组。串口不可用时靠 GDB 读内存兜底（§6.2.2），因此符号名、数组尺寸、取值语义必须全工程统一：
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
    # 统一方式（推荐）：包含公共源文件配置
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
    # 该文件自动包含 test_results.c, ztest_glue.c, gpio_toggle.c 及 include 目录

    # 手动方式（特殊工程，如 rtc_ls_test 不使用 ztest_glue）：
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/test_results.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/gpio_toggle.c)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)
    ```
  - 双核各自链接该模块，符号同名、地址不同，dump 时按 elf 区分核。
- **记账自动化（ztest 原生化后，测试函数零 `tr_record` 调用、零 EXPECT/finish 样板）**：由公共 glue `common/ztest_glue.c` 的 `zt_run_all()` 完成——`test_main` 第一行调 `zt_init(core)`（打印 `ZT_BOOT <core>`，§6.2 BOOT 判据），末尾调 `zt_run_all()`：`tr_init(TR_MAX_COUNT)` → `ztest_run_all(...)` → 套件结束后遍历 `_ztest_unit_test_list_start..end` 按 `test->stats` 批量记账（`fail_count>0 → TR_FAIL`、`pass_count>0 → TR_PASS`、`skip/未跑 → TR_NOT_RUN`）。
  - **索引 = 测试在链接段中的注册顺序（构建内稳定）**，`dump_results.py` 的 `TEST_NAMES` 必须按同一链接序对齐（SORT_BY_NAME 字典序，见 KNOWN_ISSUES #27）。
  - reason 由 **zassert 自身 printk 输出**（`Assertion failed at ...`，§6.5.5），glue 只记统计性 reason（`ztest fail_count=%u`）；串口可用时 reason 以串口为准。
  - 双核各自链接该模块，两核的 `ztest_run_all` 都走同一套自动记账。
  - 约束：单固件测试数 < `TR_MAX_COUNT`(64)（现有最大 otbn_selftest 30 项）。

#### 6.1.6 驱动/固件注释规范（边界：只写本层语义，不写外部历史）

注释必须遵守**职责边界**：只描述**本层（驱动/固件）的行为语义与契约**，不得记录属于外部系统（Zephyr OS 框架、历史 bug、调试过程）的原因。判断标准：**"如果这段代码换到别的工程/别的 OS 配置下，这条注释还成立吗？"** 不成立就不该写在本层代码里。

| 应该写（本层语义） | 不应该写（外部原因/历史） |
|---|---|
| "TERM/URUN 在读欠载是 controller 主动提前终止的合法行为" | "panic_mode 已修（test_main 不返回，ztest 不触发 LOG_PANIC）" |
| "STOP 分支必须 FLUSHTB：清空 TX FIFO 使 prefill 从 buf[0] 起写" | "之前 LOG_ERR 阻塞 10ms 造成 8ms 死窗" |
| "opcode 用 ASCII 助记符避免与 CCC 命令码混淆" | "Windows gdb stdout 管道块缓冲导致读不到" |

**一次性操作事故不入注释/规范**（与 debug-knowledge"一次性坑不沉淀"同一原则，本条款把它明确到代码注释）：

- 注释与规范只记录两类有长期价值的内容：**a) 驱动/硬件层面需要注意的问题**；**b) 应用层面容易反复出现的问题**。
- **一次性操作事故**（"某次迁移时一度丢失导致构建失败，某日已修复"这类历史事件叙述）不记录——不写进注释，也不进规范/速查。
- 区分标准：**保留"是什么/为什么必须这样"（仍成立的结构约束），删除"曾经发生过什么事故"（含日期的修复记录、"一度/曾经/迁移时/某次"的叙述）**。
  - ✅ 保留："`../tests/expected` 必须保留（protocol.h 引用的 *_vectors.h 向量本体在该目录）" —— 结构约束，永远成立
  - ❌ 删除："迁移 common_sources.cmake 时一度丢失导致全量重建编译失败（2026-09-04 修复）" —— 一次性事故史

外部系统问题（如 panic_mode、shell 后端阻塞、gdb 缓冲）的**记录位置**：
- 反复会踩的通用坑 → `KNOWN_ISSUES.md` / 对应 skill（qsh-debug §2.3 等）
- 一次性定位闭环的 bug → 问题总结 .md（§6.3），存档后不进代码注释

调试时的临时注释（探针/断点说明）按 KNOWN_ISSUES #6 探针纪律，**调试完必须清理**，不得随功能代码提交。

#### 6.1.7 log 输出规范（所有测试固件必须遵守）

- **所有 log/token 输出统一走 `LOG_INF`**（不用 printk），prj.conf 必带：
  `CONFIG_LOG=y`、`CONFIG_LOG_MODE_DEFERRED=y`（buf 模式）、`CONFIG_LOG_BUFFER_SIZE=16384`、`CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=10`、`CONFIG_LOG_BACKEND_UART=y`、`CONFIG_LOG_PRINTK=n`。
- **除了必要卡时序的地方**：ISR 内禁 log（§6.1.2）、时序测量/波形段不打 log（§6.1.3）维持不变。
- 理由：消息进 log buffer 由 log 线程异步输出——测试线程不被 UART 输出阻塞、消息不丢（UART 瞬时卡死时积压在 buffer 中）；token 行带 LOG 时间戳/模块名前缀，解析用 `re.search`（KNOWN_ISSUES #5）不受影响。

#### 6.1.8 构建与 case 脚本规范

- 每个 case 独立 `build/` 目录；共享同一份源码，case 差异只体现在 overlay/prj.conf/test.sh。新 case = 新建目录 + 三件套，不改 C 代码。
  - **变体（case 需要不同源集时）**："只增不改"扩展法 —— 应用目录新增 `Kconfig` + 工程 `CMakeLists.txt` 尾部 `if(CONFIG_<X>) target_sources(...) endif()` + 各 case `*.conf` 选配置，生成细节与强制回归验证（`ID=0` 既有 case 须原样可构建）见 qsh-test-gen §2。
- `west build` 优先 `-p auto`；改 overlay（SRAM 地址类）后必须删 build 全量重建（KNOWN_ISSUES #9）。
- 每 case 的 `test.sh` 必须包含头注释模板：What is tested / CPU0/CPU1 行为 / Normal behavior / Abnormal behavior / Feedback / Hardware IO connections / Build artifacts / Usage。
- `test.sh` 三模式统一：`./test.sh`（只编译，打印 launch.json 片段）、`-g`（编译 + GDB 加载运行）、`-r`（仅加载运行，agent 模式）。构建时 `SKIP_LOAD=y` 只编译不烧录。参考 `tests/case_pure_i3c_12m5/test.sh`。

#### 6.1.9 复用收敛原则（通用设计原则）

**凡多工程共用的内容（C 源、overlay 片段、脚本、配置模式），必须收敛到 `common/` 公共层单点维护，各工程只做 include/引用；禁止各工程私拷副本。**

- 适配上游/共享树变更时（如 SoC dtsi 节点 label 改名），**优先且只改公共层**——私拷副本会导致同一改动逐工程重复 N 次、且容易漏改。
- 现有公共层实例：
  - `common/common_sources.cmake` —— 公共 C 源（test_results.c / ztest_glue.c / gpio_toggle.c）
  - `common/gpio_toggle.dtsi` —— 公共 overlay 片段（GPIO enable 块，各 app.overlay `#include`）
  - `common/cpu2_mem_layout.overlay` —— cpu2 内存布局（经 EXTRA_DTC_OVERLAY_FILE 注入）
  - `projects/*/tests/scripts/` 共享执行脚本（dbg_run.py / dump_results.py 等同源复制自样板，新增通用脚本应入公共层）
- 判断标准：**"同一内容出现在 ≥2 个工程里且语义必须保持一致"→ 收敛到公共层**。
- 新工程骨架默认引用公共层（qsh-test-gen 模板已固化）；合规检查见 qsh-test-validate。

#### 6.1.10 工程分型与运行参数（单核 1os / 双核 2os）

分型决定 board、入口地址、烧录遍数与脚本路径，**新建工程第一步先定型**（生成期约束见 qsh-test-gen §1.5，校验期见 qsh-test-validate §1.9f）。

| 分型 | 判定依据 | board | cpu1 入口 | 烧录 |
|------|---------|-------|----------|------|
| 单核 | 只有 cpu1 侧固件目录 | `lsqsh_evb@1os/lsqsh/cpu1` | `0x10000000` | 单遍 |
| 双核 | 有 **cpu1 之外的固件目录**（`cpu0_*` 或 `cpu2_*`） | `lsqsh_evb@2os/lsqsh/{cpu1,cpu2}` | `0x100c0000` | 两遍 |

- **分型按"是否存在 cpu1 之外的固件目录（`cpu0_*` 或 `cpu2_*`）"判定**，不按工程名或关键词猜。第二核目录命名**跨工程不统一**（实测 `cpu0_i2c_master` / `cpu0_i3c_controller` / `cpu2_controller_mbox_test` 三种并存），靠 `controller`/`cpu2` 等关键词匹配会漏判——`i2c_pure_test` 第二核叫 `cpu0_i2c_master`（无 `cpu2`、无 `controller`），实为双核。
- **单核固件一律 1os 编译测试**（用户需求 #8）；2os 只用于双核的 cpu1/cpu2 两侧。
- **入口地址禁止从异型样板抄用**：设错 `$pc` → 固件根本没跑 → 串口 0 行，与"固件/配置问题"同症状。
  - Python 脚本（dbg_run / run_test / dump_results）单核一律 `os.environ.get("TARGET_ROM_START", "0x10000000")`（`dump_results.py` 用 `int(..., 0)`，默认值即 1os 入口，无需覆盖）；双核显式给出 `0x100c0000`。
  - shell 侧由 `scripts/test_common.sh` 分流：单核路径 `SINGLE_BOARD`/`SINGLE_ROM_START`（默认 1os + `0x10000000`），双核路径 `TARGET_BOARD`/`TARGET_ROM_START`（默认 2os + `0x100c0000`）。单核 test.sh 用 `build_single_test`/`load_and_run_single`，双核用 `build_test`/`load_and_run`——用错分型会强制错板级（KNOWN_ISSUES #43）。

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
INFO <test_name>: <k>=<v>[,<k>=<v>]             # ZT_INFO 宏，数值上报（§6.5.4）
Assertion failed at <file>:<line> (<func>): <msg>  # zassert 原生，FAIL reason（§6.5.5）
PROJECT EXECUTION SUCCESSFUL|FAILED             # ztest 原生，本核套件完成标记
CPU2 MASTER / CPU1 SLAVE ...                    # 角色 banner，保留，角色识别不变
```

- **ZT_BOOT / PROJECT EXECUTION 是所有驱动测试工程的通用标记**（由 `common/ztest_glue.c` + ztest 提供，新工程生成时注入）：ZT_BOOT 在 test_main 第一行打印（BOOT 判据）；`PROJECT EXECUTION ...` 由 ztest 在本核套件全部结束后打印（完成标记，替代旧 DONE）。harness 在 load 后收不到 ZT_BOOT → 按 §6.2.3 第 2 条 log 门禁判据处理（直接判失败，见下）。
- 逐项输出受 `CONFIG_ZTEST_VERBOSE_OUTPUT` 门控：**迁移固件 prj.conf 必须 `=y`**，否则 `START/PASS/FAIL` 行被抑制全 MISSING（KNOWN_ISSUES #26）。
- 判定：**单核 = 该核全项 `PASS - <name>` + `PROJECT EXECUTION SUCCESSFUL`；双核 = 同名测试项两核各自 `PASS - <name>` + 两核各自 `PROJECT EXECUTION SUCCESSFUL`**。SKIP 项报告单独标注。
- **各族 × 各状态的完整判定见 §6.2.4 判定规则总表（冻结）**——判定语义的唯一真源（含截断接受 `TRUNCATED_AFTER_ALL_ITEMS`、零证据守卫、哨兵门、各分族逐轮收紧）；表外行为一律视为未定义。`declared` 表取不到时截断放宽一律不生效（fail-closed）。
- **双核例外（i3c_dualcore_test，照 evaluate_case 实码）**：该工程 target `test_main` 不返回（§6.1.1 dispatcher join）→ TGT 端口**没有** `PROJECT EXECUTION` 行，属设计（Task 9 D1）；完成判定 = **CTRL 侧 SUCCESSFUL 且 TGT execution != "FAILED"**（样板 `run_i3c_tests.py` `evaluate_case`），TGT 失败经逐项 FAIL 行暴露。i2c_pure/i3c_i2c_switch 的 target `test_main` 正常返回，仍按两核各自 SUCCESSFUL 判。
- 记账由 **glue 自动完成**（§6.1.5：`zt_run_all()` 按 ztest stats 批量记账到 `test_results[]`），测试函数零样板；串口不可用时 GDB 读内存兜底（§6.2.2）。
- agent 回归输出报告并归档**会话目录** `reports/<date>-<idx>/` + 更新 `summary.md`（§6.4）；批量入口 = `regression/run_all.py`（§6.4.5）；超时用 Python `subprocess.run(timeout=...)`，不用 bash `timeout`（Git Bash 无）。
- 烧录用两遍烧录法规避 I-cache 残留旧镜像（KNOWN_ISSUES #4）；单核工程已改单遍烧录（reset 冲刷 I-cache，KNOWN_ISSUES #24）。

#### 6.2.2 串口不可用时：GDB 读内存取结果（dump_results.py）

- 结果数组是全局符号（链接地址=加载地址），连 DebugServer 后 `x/Ndb` 直接读，不依赖串口；失败项再 `x/s` 读 `test_reasons`（地址 = 基址 + i*96）。
- 脚本：`tests/scripts/dump_results.py`（新工程从样板复制），用法：
  ```text
  python dump_results.py --case case_pure_i3c_12m5              # 只读当前内存
  python dump_results.py --case ... --run 35                    # 先两遍烧录跑 35s 再读
  python dump_results.py --case ... --raw                       # 打印 gdb 原始输出
  ```
- 适用场景：串口 ztest 输出被刷掉/串口忙/远程无串口；**串口可用时仍以串口 ztest 输出为准**（§6.2.1 判定标准），内存结果用于兜底与交叉验证。
- **有效性哨兵门（`test_results_valid`，★ 2026-09-18）**：`tr_init()` 填完槽位后写 `TR_VALID_MAGIC(0x5A)`。读到非 `ok` 必须**三态分开**（`--json` 的 `mem_state` 字段），因为动作不同：

  | `mem_state` | 判据 | 动作 |
  |---|---|---|
  | `no_sentinel_symbol` | ELF 里查不到 `test_results_valid` | **旧构建**（哨兵加入前编译）⇒ **重新编译**，不是查驱动 |
  | `invalid` | 符号在但值 ≠ 0x5A | 固件没跑到 `tr_init`（挂在内核/POST_KERNEL）⇒ 内存结果**不可信**（`.bss` 零值语义 = PASS） |
  | `ok` | 符号在且值 = 0x5A | 内存结果可信 |

  非 `ok` ⇒ 清空该核结果并 fail-closed（消费者按 MISSING 处理），**禁止输出假 PASS**。`mem_state` 与原因串必须进控制台 + `.md` + `.json` 三处（同一份文案）：读者只翻报告就要能区分「无效」与「根本没跑」。
- **旧镜像批量风险**：哨兵加入前编译的 ELF 会让内存兜底集体 fail-closed（看起来像大面积失败）。`run_all.py` 因此**默认重建**，且 i3c 的 `--skip-build` 只在**该 case 自己的** ELF 含哨兵符号时才允许；case 循环前 `audit_stale_elves()` 列出全部缺符号旧镜像（§6.4.5）。
- **⚠ 哨兵只证明"符号在、格式对"，不证明"对应当前源码"（★ 2026-09-20，G-FAMFILL-1）**：ELF 含 `test_results_valid` 但 build 目录 mtime **早于**相关源码时，`_elf_has_sentinel()` 门**照旧放行** ⇒ `run_test.py` 构建段与 `test.sh`（`AGENT_RUN=y`）均跳过构建 ⇒ **烧陈旧镜像** ⇒ 结果**双向失真**：`-fipa-icf` 折叠到有断言的共享体 → **假 FAIL**；折叠到无断言的共享体 → **假 PASS**（实测 `i3c_dualcore_test` 的 `case_mixed_400k_6m25` / `case_pure_i3c_6m25`，两边 `.config` md5 逐字节相同 ⇒ 与构建路径/优化级别无关）。⇒ 产物新鲜度必须比对 **build mtime vs 源 mtime**（或内容哈希），**只验哨兵符号不够**（KNOWN_ISSUES #88 姊妹形态 / qsh-test-run §4.2）。

#### 6.2.3 批量回归判据（所有 run_*_tests.py 必须实现，i3c 样板已校准）

一次 case 的完整判定链（参数默认值实测校准，勿凭感觉改）：

1. **两遍烧录**（KNOWN_ISSUES #4）：prime 轮冲刷 I-cache 旧镜像，pass-2 为正式轮。
   - prime 轮判据 = 串口出现**任何 ztest 输出**（`ZT_BOOT` / `Running TESTSUITE` / `START - `——旧镜像可能无 ZT_BOOT，等 ZT_BOOT 会误报环境失败）；prime 轮输出丢弃，pass-2 前清空采集行。
   - pass-2 判据 = **ZT_BOOT 标记**（当前固件必有）。
2. **log 门禁判定**：**等 log 有打印才开始测试**；pass-2 spawn 后 `LOAD_WINDOW=20s` 内收不到任何 log/ztest 输出 → 不深挖根因（环境/固件不区分），按 KNOWN_ISSUES #20 自愈重试 1 次，仍无 log **直接判该 case 失败**（reason=no_log，继续后续 case）。正常 load 后几秒内必出 log——"load 进去没 log，直接当失败"。
3. **完成判定 = 静默判据 + 完成标记**：BOOT 之后连续 `IDLE=10s` 无新串口字节 **且已见套件完成标记**（**任一核 `PROJECT EXECUTION SUCCESSFUL|FAILED`**）→ 判 done 提前杀 gdb。**只有静默但无完成标记 = 测试中途长间隔（慢速 case 相邻逐项输出间隔可 >10s）→ 继续等**，每 15s 打一条诊断；case_timeout 硬上限兜底。10s 是"套件结束后快速收尾"的窗口，不是"测试中途"的判据。
4. **串口必须实时采集**：ztest 输出是实时流，跑完即过，事后读串口只剩 shell 提示符 → 后台线程逐行存 `lines`，判据与报告都取自该流；禁止事后一次性读串口。
5. **行解析用 `re.search()` 不用 `^` 锚点**（KNOWN_ISSUES #5：shell 提示符重绘会在行首加 `uart:~$ ` 前缀）；`PASS - <name>` 按名字去重/多核区分，`PROJECT EXECUTION` 按核计数（双核两行都要）。
6. **每 case 前后 DebugServer 自愈（按需重启）**：先杀残留 gdb（1025 ESTABLISHED 对端），杀完复查——连接残留仍在（ESTABLISHED 未释放）或 server 已死（无 LISTENING）才重启；健康则**不重启**（实测 6 case 全量只重启 1 次）。重启 = taskkill + Popen + 轮询日志 `DebuggerServer$` 提示符判就绪；Python 里 taskkill/tasklist 一律单斜杠参数（KNOWN_ISSUES #16）。
   - **★ E-2（2026-09-21）：提升为"每 case 前置动作"**。实现 = `common/acquire.py::ensure_debugserver_alive()`（**唯一一份**，复用既有 `netstat_1025`/`kill_residual_gdb`/`restart_debugserver_stable`，不另写 restart）；**两处都调**：① `regression/run_all.py` case 循环内逐 case 调（一处改动覆盖全部 24 工程）；② `common/acquire.py` 五个 L1 策略入口（`single_cap`/`_ports_dict_one`/`dual_run`/`mbox`/`iwdg`）各调一次（覆盖不经 run_all 的单跑）。
   - **判据（稳、不误杀）**：健康 = 1025 有 LISTENING **且**无 ESTABLISHED 残留 ⇒ 立即返回（零副作用，实测 **24ms/case**，**不做** `probe_gdb_alive`——探测连接本身必留残留 §10.17，会把"健康"变成"要重启"）；netstat 偶发失败内层重试 3 次（避免把"查询失败"误读成"进程已死"而错杀活实例）；**刚拉起还未 listen 不算死** ⇒ 有限等待 `DS_READY_WAIT_S=15s` 轮询。
   - **自愈阶梯**：轻自愈（`kill_residual_gdb`，只清残留不重启）→ 重自愈（`restart_debugserver_stable`，含 5s 稳定期）→ `DS_ENSURE_ATTEMPTS=2` 轮仍不就绪 ⇒ **fail-closed**：`run_all` 记 `ENV`（summary 写 `ENV-ABORT`，不计入 PASS/FAIL，退出码计 1），`run_test.py` 报 `[ERROR] DebugServer 前置检查失败` 并中止该 case。**为什么必须补**：此前该检查只在整批/单工程开始前做一次，一次 CKLink 探针瞬时故障把 DebugServer 打挂后，后续每个 case 的 gdb 只能报 `could not connect (error 138)` ⇒ **一次掉线放大成 N 个假 FAIL**（实测 1 → 4；修后同场景收敛为 1）。
   - **E-2 同族残留的收口（F1 / F3 / F4）**：E-2 落地时登记的同族缺口，三条已修，一条判定为删 ——
     * **F1 单 case 调试入口无 DS 自愈** ⇒ `dbg_run.py`（23 份，`mbox_test` 无此文件）在端口发现前接一行 `common/acquire.py::dbg_run_preflight()`。它是**薄适配**（判活/轻自愈/重自愈仍只有 `ensure_debugserver_alive()` 一份实现，**不复制逻辑**）；不自愈成功就 **fail-closed**（明确报错 + `sys.exit(3)`），不退化成 gdb `error 138` 假失败。一律 `style="d3"`。**`--xip` 路径不得加**（XIP 走 flash_program 独占 CKLink，DS 本就不该被占用）。
     * **F3 summary 尾行口径** ⇒ `common/report_dir.py::update_summary` 尾行按 `PASS` / `FAIL` / `ENV-ABORT` 三分计数，ENV（环境中止）单列，与 `run_all.py` 控制台口径一致。**硬约束：无 ENV 行的会话尾行逐字不变**（旧会话目录须能原样重渲染）。
     * **F4 `restart_debugserver` 启动失败** ⇒ `"z"` 分支 `Popen` 补 try/except（原为 exe 缺失时异常穿透到调用方）；三分支统一「`True` = 已就绪 / `False` = 启动失败或等待超时」。既有调用方一律忽略返回值 ⇒ 行为不变。
     * **F2（只评估，未改）**：`PortsDictSpec.clean_before` 是死字段（全仓唯一出现处即字段定义）。**结论 = 删除**：其"入口清一次残留"的意图已被 E-2 的**每 case 前置**严格覆盖，而 E-2 健康路径零副作用（不做 `probe_gdb_alive`、不起探测 gdb）是**有意设计**，接线等于把"入口无条件清残留"重新引回来。10 份 `PortsDictSpec(...)` 全为关键字构造 ⇒ 删字段无位置漂移风险。
7. **回归前构建一致性**：固件加新标记（ZT_BOOT/PROJECT EXECUTION）后必须先构建全部 case 再回归，勿 `--skip-build` 混跑旧镜像（无 ZT_BOOT → 假 load_fail）。
8. 结果判定沿用 §6.2.1：单核 = 该核全项 PASS + SUCCESSFUL；双核 = 同名测试项双核 PASS + 两核 SUCCESSFUL 才判 case PASS（例外：i3c_dualcore_test 的 TGT 无 PROJECT EXECUTION 属设计）；串口缺项按 §6.4.3 内存兜底合并（来源标 mem）。

#### 6.2.4 判定规则总表（冻结，2026-09-18）

判定语义的**唯一真源**。任何想改判定的人先看此表；表外行为一律视为未定义。

> 轮次代码：**基线**=原有语义；**R1**=F1 截断接受（`TRUNCATED_AFTER_ALL_ITEMS`）；**R2**=F2/F3 哨兵三态 + `mem_state` + 零证据守卫；**R3**=i3c 整核零证据收紧 + `NOT_RUN` 并入"无证据"（截断接受专用）；**R4**=常规路径 `NOT_RUN` 收紧（2026-09-18，离线重放 60 处翻转 / 0 非预期）；**R5**=单工程收紧（eth 兜底证据独立性 + 完整度；spi 的缺项守卫由 R4 覆盖）；**R6**=相反收尾标记并存时 fail-closed（判定链的"矛盾证据不得消解"通则）；**R7**=批量必须靠 token（**整个 case 一条串口逐项 token 都没有** ⇒ 不得判 PASS；2026-09-20 用户裁定）；**R8**=批量路径 **mem 取到的项一律不算证据**（口径由 R7 的"整个 case"**下沉到"逐项"**；触发来源 = **用户裁定 2026-09-20「B 严」**，2026-09-21 落地）。
>
> R5 实测校验：eth 校准样本 #1（串口仅 banner+标记、无 ZT_BOOT、5 项全 mem PASS）PASS→**FAIL**，正常兜底（ZT_BOOT 在 + mem 补齐）仍 PASS（未误伤）；spi 前缀截断 R4 起即 FAIL（`missing=24`）；`normal_all_pass` 零翻转。

**通用（适用全部族）**

| # | 逐项状态 | 收尾标记 | 判定 | 轮次 | 依据 |
|---|---|---|---|---|---|
| G1 | 全 PASS | SUCCESSFUL | PASS | 基线 | — |
| G2 | 任一 FAIL | 任意 | FAIL | 基线 | — |
| G3 | 含 SKIP（其余 PASS） | SUCCESSFUL | PASS；SKIP 不计分母，且**算确定结果** | 基线/R4 | `ztest_test_skip()` = 主动跳过 |
| G4 | 一个测试项都没有（items/tests 为空） | 任意 | **FAIL** | R2 | `all([]) == True` 陷阱；没有证据 ≠ 通过 |
| G5 | 全 PASS，**仅缺收尾标记** | 缺失 | **PASS** + `execution=TRUNCATED_AFTER_ALL_ITEMS` | R1 | 窗口到点本身不是失败 |
| G6 | 有 MISSING 或 NOT_RUN，**且缺收尾标记** | 缺失 | **FAIL**（红线） | R1+R3 | 截断放宽的前提是"每项都有确定结果" |
| G7 | `mem_state != ok`（哨兵门） | — | 该核结果清空，按 MISSING/NOT_RUN 走（fail-closed） | R2 | 见 §6.2.2 哨兵三态 |
| G8 | 同一份采集里**同时出现相反收尾标记**（`PROJECT EXECUTION FAILED` 与 `SUCCESSFUL`） | 矛盾 | **FAIL**（`execution` 归成 `FAILED`）+ 报告显式标注 `CONFLICTING_MARKERS` | **R6** | 矛盾证据**不得按"最后一条"消解**；`parse_ztest` 原为 last-wins，固件自打标记晚于 ztest 收尾行时会翻成假 PASS |
| G9 | **整个 case 一条串口逐项 token 都没有**（2026-09-20 用户裁定） | 任意 | **FAIL** + 理由串「证据弱（仅内存兜底，非串口逐项）」 | **R7**（R8 起降为 G10 的特例） | `common/judge.py::mem_only_evidence` / `MEM_ONLY_NOTE`。★ **R8 起本行不再是唯一防线**：判据已下沉到逐项（G10）⇒ 本行继续成立，但即使删掉它也拦不住 mem，故 R8 未撤它（`regression/replay.py` 仍以 `mem_only_evidence` 为公开入口） |
| G10 | **任一项**的逐项值（PASS/FAIL/NOT_RUN）来自 mem 兜底（**B 严**：mem 取到的项一律不算证据） | 任意 | 该项落 `MEM_NOEVID`：**不进 `passed`、不进 `ran`**、计入 `missing_evidence` ⇒ 该 case **不得判 PASS**（有 token 逐项 FAIL 的项除外，那是"测坏了"）；报告仍**给出 mem 取值**并显式标「非证据」 | **R8** | `common/judge.py::MEM_NOEVID` / `evidence_gaps()` / `mem_weak_note()` / `MEM_WEAK_ITEM_NOTE`；`NO_EVIDENCE_STATES` 并入 `MEM_NOEVID` |

> **G8 注**：实现——`common/ztest_report.py::parse_ztest` 新增 `exec_conflict` / `exec_seen`；**只有一条标记时 `execution` 逐字不变**（常规路径零影响）。报告侧由 `common/judge.py::EXEC_CONFLICT` + `exec_conflict_note()` 渲染（控制台 / `.md` / `.json` 三处）。
>
> **G5/G6 补充（R6）**：只有一条标记的常规路径逐字不变；R6 仅管相反标记并存。
>
> **G9 注（R7，2026-09-20 用户裁定）**：用户裁定「**调试**可用 mem 兜底，**批量必须靠 token**」。
> 判据 = **整个 case 一条串口逐项 token 都没捕到**，而**不是**"报告 `src` 列全是 mem" ——
> 双核工程的 `src` 是**跨核聚合**的（任一核用 mem 就把该项染成 mem），按聚合列判会误伤
> R3 明确保留的合法路径「**某一核整体丢行、另一核 token 完好，丢行由 §6.4.3 兜底补回
> ⇒ 不算零证据，仍可 PASS**」（基线场景 `tgt_all_missing_mem_ok`）。故 D3 族传的是
> **逐核**来源 `ev_srcs`。
>
> 落点（全部在**批量判定链**，调试路径不在内）：`common/judge.py::mem_only_evidence()`
> 是唯一真源（纯函数）；**14 份** `run_test.py` 各挂一次 —— F4 族 10（含 `rtc_ls_test`，
> `judge_case` 之外按 `notes.values()`）+ D3 族 3（`evaluate_case` 内按 `ev_srcs`）
> + `eth_test` 1。
> 理由串 `MEM_ONLY_NOTE` 挂在**既有** `mem_note` 上 ⇒ 控制台 / `.md` / `.json` 三处同源可见。
> `dbg_run.py`（23 份）零引用该守卫 ⇒ 调试路径的 mem 兜底行为逐字不变。
>
> **R7 重放证据（离线，不烧板；import 真实家族模块 + 给 `mem_only_evidence` 打猴补丁
> 构造"R7 前/R7 后"两界，新旧之差只有这一处守卫）**：
> 四条基准场景 `normal_all_pass` / `trunc_all_pass` / `trunc_missing_mem_ok` /
> `tgt_all_missing_mem_ok` **零翻转**（F4 与 D3 两种形态各测一遍）；
> 147 份归档报告（2427 个测试项，来源 token=1780 / mem=66）重放：PASS→FAIL **仅 2 例**，
> 全部是 `i3c_i2c_switch_test_case_switch_core`（O-13 本体，11/11 全 mem、两核零 token）；
> 另 6 例零 token 的 `eth_test`（17-4/17-5/18-1 的 mdio/phy_reset）本就 FAIL（E6），不构成翻转。
> 脚本：`tmp/r7/replay_r7.py`（`scan_archives.py` 产出 `tmp/r7/archive_sources.json`）。
>
> **O-13 的根治**：该工程 token 宏原为 no-op ⇒ 已恢复成真 `printk`（协议头），并让
> `common/ztest_report.py::parse_ztest` 认领它的原生 token 形态
> `I3C_I2C_TEST:<CTRL|TGT>:(PASS|FAIL|SKIP):<name>[:reason]`（该前缀全库仅此一处产出，
> 对他族零匹配面）。修好后该 case 走 token，不再命中 G9。
>
> **冻结再声明（2026-09-20，R7 收尾）**：本轮是**用户直接裁定**触发的唯一例外，范围
> 仅限「G9/D6 通则 + O-13 根治（token 宏恢复）+ O-12 修正（summary 未知计数）」三项，
> 判定语义其余部分**零改动**。R7 落定后**恢复冻结** —— 下一轮判定语义变更须同样由
> 用户显式裁定触发，不得由 agent 自行发起。
>
> **G10 注（R8，2026-09-21 落地；触发来源 = 用户裁定 2026-09-20「B 严」）**：用户原话
> 「**内存兜底只在调试时候使用，测试结果必须以 token 为判断标准**」；追问"批量测试里，
> 内存兜底（mem）取到的项该怎么算？"→ 裁定 **"B 严：mem 一律不算证据"**。
>
> 口径与 R7 的分工：**R7 只堵"整个 case 一条串口逐项 token 都没有"**；**R8 把它下沉到逐项**——
> 任一项只要其 PASS/FAIL/NOT_RUN 来自 mem，**该项即非证据**，不再需要"全 case 零 token"
> 这个前提（旧实现里"14 项 token PASS + 1 项 mem PASS"仍会判 PASS，即 R7 拦不住的形态）。
>
> 处置（三处一致，**优先复用既有机制**）：① 逐项结果落 `MEM_NOEVID`（**新状态**，已并入
> `NO_EVIDENCE_STATES`）⇒ 被 `missing_evidence()` 自动收走，走既有的"零证据 / 证据缺失"
> 通道；② 该状态**不进 `passed`、不进 `ran`**（分子分母两端同时摘掉 ⇒ `passed == ran` 不再
> 成立）；③ **报告仍给人看 mem 取值**：结果列 `⬜ 无证据(mem)`、来源列 `mem(非证据:PASS)`、
> 理由串 `MEM_WEAK_ITEM_NOTE`「证据弱（仅内存兜底，非串口逐项）」—— 与「驱动/测试失败」
> **严格分开**（`missing_evidence_note` 说"token 与内存兜底都没有值"，对 mem 项是假话，
> 故 `evidence_gaps()` 把两类拆开各说各的）。
>
> **既有门一律保留、R8 在其之上**：R2 的 `mem_state != ok ⇒ dumper 侧清空`照旧（G7）；
> R5-E6 的「`ok` 但无 `ZT_BOOT` ⇒ mem 项降级剔除」照旧（E6）；`mem_only_evidence` /
> `MEM_ONLY_NOTE`（R7）照旧（未撤，`regression/replay.py` 仍引用它）。**唯一必要的兼容改动**：
> `eth_test` 的 E6 门原本按 `source == "mem"` 精确判归属，R8 把来源串改成 `mem(非证据:X)`
> ⇒ 改成**前缀判**（门语义不变）。
>
> **`truncation_evidence()` 不另行处理**：`MEM_NOEVID` 在 `NO_EVIDENCE_STATES` 里 ⇒ 截断接受
> 自动不放行。**D3 的 `incomplete` 容忍（D3 行）同步收紧**：某一核靠 mem 补出的"没坏"不构成
> 证据 ⇒ 不再走容忍路径。**但 token 显式 FAIL 仍是"测坏了"**：`has_explicit_fail` 只认 token
> （mem 值已落 `MEM_NOEVID`），故"CTRL token FAIL + TGT mem"仍报 `FAIL`，不会被抹成"没测到"。
>
> **落点（全部在批量判定链）**：`common/judge.py`（新原语）+ **13 份**有 mem 通道的
> `run_test.py`（F4 族 9：`trng`/`spi`/`spid`/`spif`/`i2cf`/`otbn`/`otbn_selftest`/`mbedtls`/`rtc_ls`
> + D3 族 3 + `eth_test`）；**Z 族 8 份与 `app_iwdg_cpu2_test` 无 mem 通道 ⇒ 零改动**
> （`mem_line="内存兜底: 不适用"`，结构上不可达）。**调试路径 `dbg_run.py`（22 份）零引用**
> 该守卫 ⇒ mem 兜底在调试时照常可用。`.json` 一律**只增字段**：D3 的 `mem_map` **仅在该 case
> 真有 mem 项时才出现**（否则非 mem case 的产物字节会漂移，下游 diff 被噪音淹没）。
>
> **报告样例（真实渲染）**：F4 族 —— 同一份合成采集，改动前 `PASS (15/15)`（前 3 项
> `✅ PASS | mem`）；改动后（`tmp/brule/out/sample/after/…/trng_test_report.md`）：
>
> ```text
> - 内存兜底: 有效 — 证据弱（仅内存兜底，非串口逐项）：3 项无串口逐项 token，只有 GDB 内存兜底
>   读到的值（autocorr_8k(mem=PASS)、basic_16(mem=PASS)、block_freq_8k(mem=PASS)）⇒ 批量路径
>   **一律不计为证据**，按『无证据』处理（用户裁定 2026-09-20 …）…
> - 结果: FAIL (12/12)
> | autocorr_8k  | ⬜ 无证据(mem) | mem(非证据:PASS) | 前 8 个 lag 的 \|R(d)\| < 6/√n = 0.02344 | - | 证据弱（仅内存兜底，非串口逐项）：本项没有串口逐项 token… |
> ```
>
> ★ `(12/12)` 读作"**12 项有 token 证据、全过**"（mem 项已从**分子分母两端**同时摘掉），
> 须与「内存兜底」行的"3 项证据弱"合读 —— 报告不再允许"看起来全过"。
> 双核族的**真归档**样例（`i3c_i2c_switch_test/case_switch_core`，`--part 2` 产物）：
> `broadcast_nack_recovery | ❌ FAIL | mem(非证据:TGT=NOT_RUN) | … | CTRL:rstact_whole_should_nack`
> —— CTRL 的 **token FAIL** 照旧表达"测坏了"（不被抹成"没测到"），TGT 的 mem 取值照旧人可查
> 但显式标"非证据"。
>
> **R8 离线证据（不碰板子；脚本 `tmp/brule/`）**：
> **（a）合成穷举 + 非空转自检**（`tmp/brule/ab.py --part 3`）：6 族 × 9 变体
> （全 token / 全 mem / 混合 / 无 `ZT_BOOT` / `mem_state != ok` / 截断 / token FAIL），
> 逐变体比对"改动前 vs 改动后"的**判定签名 + .md/.json 字节**：
> **非 mem 变体 0 差异**（`alltoken`/`memstate_bad`/`token_fail1`/`dma` 的 `mem_payload_ignored`）；
> **mem 变体 PASS→FAIL**（`mixed_*_mem` / `mixed_truncated`，各族一致）；
> `allmem` 本就 FAIL（R7 已堵）⇒ 仅内容变；`ctrl_fail_tgt_mem` 维持 FAIL（"测坏了"未抹掉）；
> `eth mem_noboot` **FAIL→FAIL 且 0 差异**（E6 门逐字保留）。
> 非空转自检：每族观测种类 7–8/9，**与 `alltoken` 观测相同者为 0**，无空转嫌疑
> （`mixed_first3_mem ≡ mem_noboot` 的等价仅出现在 F4 族 —— 该族不消费 `ZT_BOOT`，这本身是
> "R8 未引入新依赖"的正面证据）。
> **（b）真归档重放 A/B**（`--part 1`，mem=None，与 `regression/replay.py` 同口径）：
> 现存活归档 **1125** 份可重放 case（`reports/**` + `tmp/{a1,a2,b1,e2fix,ffix,replay}/**`，含
> `serial_*.log`），实跑 **1124** 份（1 份因文件 ACL 读失败跳过）⇒ **差异 0**（判定签名 +
> `.md`/`.json` **逐字节**）。即：**不含 mem 项的 case 零变化**，"零意外翻转"成立。
> ⚠ 语料快照提示：跑本 A/B 期间另一路 agent 正在重写 `reports/`（`reports/2026-09-21-2` 消失、
> `-1` 被重建），故上列数字是**当时**语料的结论；脚本可随时复跑。
> **（c）真归档 + 反推 mem 载荷**（`--part 2`；mem 不进 L0 归档 = D7 缺口，故由归档报告里的
> `src=mem` 记录反推载荷，串口行仍是**真归档**）：现存活语料里含 mem 项的 case **仅 1 份**
> —— `i3c_i2c_switch_test/case_switch_core`：`FAIL → FAIL`（CTRL 有 **token** FAIL ⇒ 仍是
> "测坏了"），2 个产物文件变化（`.md`/`.json`），变化**全部落在 mem 项那一行**。
> ⚠ 该 case 在改动前的报告里已是 FAIL ⇒ **现存活语料中不存在"带 mem 项且判 PASS"的归档**，
> 故"PASS→FAIL 预期翻转"由（a）合成穷举给出（R7 时代有 2 份这样的归档
> `reports/2026-09-17-{1,3}/i3c_i2c_switch_test_case_switch_core`，但**已随语料被清理**
> 而不复存在 —— 见 `tmp/r7/archive_sources.json`）。
>
> **冻结再声明（2026-09-21，R8 收尾）**：本轮同样是**用户直接裁定**触发的例外（裁定 2026-09-20
> 「B 严：mem 一律不算证据」），范围仅限「G10 通则 + D3/D6/E-table 的同步收紧 + 明确不做第 6 条
> 撤销」。**R8 未新增任何"用户未裁定"的判定语义**，也未放宽任何既有守卫（R2/R5-E6/R6/R7 全部
> 保留）。R8 落定后**恢复冻结**。

**F4 族**（`trng`/`spi`/`spid`/`spif`/`i2cf`/`otbn`/`otbn_selftest`/`mbedtls`）—— 有 mem 通道 + `passed == ran` 分母

| # | 逐项状态 | 收尾标记 | 判定 | 轮次 |
|---|---|---|---|---|
| F1 | 全 PASS | SUCCESSFUL | PASS | 基线 |
| F2 | 有 NOT_RUN（该项**从未执行**） | **SUCCESSFUL** | **FAIL** + 理由串**列出哪几项**（"证据缺失，非驱动失败"） | **R4** |
| F3 | 有 MISSING/NOT_RUN | 缺失 | FAIL（=G6） | R1+R3 |
| F4 | 全 PASS | 缺失 | PASS + TRUNCATED | R1 |
| F5 | `ran == 0`（零证据 / 全 NOT_RUN） | 任意 | FAIL（`ran > 0`） | 基线 |

> **F2 注（前缀截断形态）**：该守卫同时覆盖"banner 在、前 N 项 token 丢、后 2 项 PASS、标记齐全"：丢失项经 mem 读成 `NOT_RUN` 即不进 `ran` 分母 —— R4 之前此形态判 PASS，R4 起判 FAIL（实测 spi：`missing=24`）。**故 `spi_test` 无需另加守卫**。

**Z 族**（`adc_temper`/`dma`/`flash`/`iopmp`/`pwm`/`wdt`/`emmc`/`qspi_flash`）—— **无 mem 通道**

| # | 逐项状态 | 收尾标记 | 判定 | 轮次 |
|---|---|---|---|---|
| Z1 | 全 PASS | SUCCESSFUL | PASS | 基线 |
| Z2 | 一个项都没有 | SUCCESSFUL | **FAIL**（`has_evidence`） | R2 |
| Z3 | 全 PASS | 缺失 | PASS + TRUNCATED | R1 |
| Z4 | 有项未被捕到 | 缺失 | FAIL（=G6；declared 表取同工程 `dump_results.py`） | R1 |
| Z5 | NOT_RUN | — | **结构不可达**（无 mem 通道；token 只产 PASS/FAIL/SKIP） | R4 分析 |

**`eth_test`** —— mem 合并**主动跳过** NOT_RUN

| # | 逐项状态 | 收尾标记 | 判定 | 轮次 |
|---|---|---|---|---|
| E1 | 全 PASS | SUCCESSFUL | PASS | 基线 |
| E2 | `items` 为空 | SUCCESSFUL | **FAIL**（判据含 `and items`） | 基线 |
| E3 | 缺项（被跳过 → MISSING） | 缺失 | FAIL | R1 |
| E4 | NOT_RUN | 任意 | **不可达**（mem 合并跳过 NOT_RUN 项） | R4 分析（保留） |
| E5 | **有已声明项无确定结果**（token 无 / mem 被拒或跳过） | SUCCESSFUL | **FAIL** + 逐项列出 | **R5（原"待父会话"已转正）** |
| E6 | mem 证据的**独立性门**：`mem_state == ok` **且** 串口见 `ZT_BOOT`，二者缺一 | — | mem 项**降级剔除**（落回 MISSING），交 E5 判 FAIL | **R5** |
| E7 | **任一项**的值来自 mem（**B 严**，E6 通过后仍适用） | 任意 | 该项落 `MEM_NOEVID`（=G10）：不进 `passed`/`ran`、计入 `missing_evidence` ⇒ 不得判 PASS；报告仍给取值并标「非证据」 | **R8** |

> **E7 与 E6 的关系**：E6 是"mem 结果**本身可不可信**"（独立性），E7 是"可不可信都**还不算证据**"（B 严）。二者**串联**，E6 在前、E7 在后，**E6 未删**。
>
> **E4 与 E7**：E4 的"mem 合并跳过 NOT_RUN"仍在（`merge_mem` 里 `result == "NOT_RUN"` 直接 `continue`）⇒ NOT_RUN 项根本不进 `items`，走 E5 的"无确定结果"通道 —— 与 E7 的落点不同（E7 管 PASS/FAIL 项），故 E4 保留。

> **E6 依据**：mem 是 `SPEC_REFERENCE.md` §6.4.3 正式兜底，**不是**"只认 token"；但 RAM 两次运行之间不清零 ⇒ `mem_state==ok` 只证明"曾有一次跑到 tr_init"，**不证明这一轮**（可能是上一轮残留结果 + 上一轮置位的 0x5A）。`ZT_BOOT` 才证明本轮确实在跑本固件。缺佐证 ⇒ mem 降级，避免"串口只捕到 banner+标记、逐项 token 全丢"时 5 项全 `[mem] PASS` 判过（即校准样本 #1 复活）。
>
> **E6 已知取舍（刻意保守，非"待放宽"）**：不加 `--trust-mem-without-boot` 之类逃生口。"boot 行被整段刷掉但套件确实跑完"会被误判 FAIL —— 但那正是 `eth_test_case_mdio` 假 PASS 的原形，加逃生口等于重开一条缝；且该形态目前只是**假设**风险。**触发再审的唯一条件** = 硬件轮（H12）实测确实出现"单核 boot 行丢失 + 套件跑完"形态。

> **E6 诊断可见性（★ 2026-09-18 记录；2026-09-19 已修）**：E6 触发时产生的 `mem_note`（"内存兜底结果被拒绝…"）曾**只进 JSON** —— `common/judge.py::mem_state_line()` 在 `mem_state==ok` 分支丢弃 `note` ⇒ `.md`/控制台只显示"内存兜底: 有效"，**看不到"被拒绝"**。**已修**：`mem_state_line()` 在 `ok` **且 note 非空**时照常拼接 note（`ok` 且无 note 的常规路径输出逐字不变），`.md`/`.json` 现均可见该诊断串；**残留**：`projects/eth_test/tests/run_test.py` 控制台 `print_report()`（`:360-373`）不经 `mem_state_line()` ⇒ **eth_test 控制台仍不可见**（其余 14 工程已生效）。JSON `mem_note` 字段不变。（缺陷登记见 KNOWN_ISSUES #95 / qsh-test-compare §2。）

**`rtc_ls_test`** —— `ran == len(declared)` 天然含完整度约束（行号前缀 `K*`，与「轮次」列 R1–R4 无关）

| # | 逐项状态 | 收尾标记 | 判定 | 轮次 |
|---|---|---|---|---|
| K1 | 全 PASS | `RTC_TEST:DONE:PASS` | PASS | 基线 |
| K2 | 有 NOT_RUN / MISSING | 任意 | FAIL（`ran == len`） | 基线（天然紧） |
| K3 | 全 PASS，DONE 缺失 | 缺失 | PASS + TRUNCATED | R1 |

**D3 族**（`i2c_pure`/`i3c_dualcore`/`i3c_i2c_switch`）—— 双核 ztest

| # | 逐项状态 | 收尾标记 | 判定 | 轮次 |
|---|---|---|---|---|
| D1 | 同名项双核 PASS | 见 D5 | PASS | 基线 |
| D2 | **某一核整体**无确定结果（既非 PASS 也非 FAIL） | 非 FAILED | **FAIL** + `evidence_gap` | R3 |
| D3 | **部分**项缺失（另一核完好） | 非 FAILED | **PASS**（`incomplete` 容忍） | **明确不做**（★ **R8 起不覆盖 mem 项**：某一核靠 mem 补出的"没坏"不再是证据 ⇒ 该形态走 G10 判 FAIL；容忍只剩"真 MISSING 且无 mem 值"这一支） |
| D4 | 全 PASS | 缺失 | PASS + TRUNCATED（须全 PASS 才放行） | R1 |
| D5 | 完成判据 | — | i3c_dualcore：CTRL SUCCESSFUL 且 TGT ≠ FAILED；i2c_pure/i3c_i2c_switch：两核**各自** SUCCESSFUL | 基线 |
| D6 | **两核都没有**任何逐项 token（整份证据来自 mem 兜底） | 任意 | **FAIL**（=G9）+ 理由串「证据弱（仅内存兜底，非串口逐项）」 | **R7**（R8 起降为 G10 的特例） |

> **D6 注（R7）**：判据取**逐核**来源（`evaluate_case` 里的 `ev_srcs`）。**不**取聚合后的
> `tests[i]["src"]` —— 那个"任一核 mem ⇒ 该项 mem"的聚合会把"TGT 整核丢行 + CTRL token
> 完好 + mem 补齐"（= D2 的例外、基线 `tgt_all_missing_mem_ok`）误判成零 token。
>
> **D6 注补（R8，2026-09-21）**：R8 起**逐项**口径生效（G10）⇒ 上句保留的
> `tgt_all_missing_mem_ok` 路径**不再合法**（凡 mem 补出的核该项即 `MEM_NOEVID`）；
> D6 的逐核 `ev_srcs` 判据仍在（`mem_only_evidence` 未撤），只是不再是唯一防线。

**`app_iwdg_cpu2_test`**

| # | 逐项状态 | 收尾标记 | 判定 | 轮次 |
|---|---|---|---|---|
| A1 | 两核各 ≥1 项且全 PASS | 两核 SUCCESSFUL | PASS | 基线 |
| A2 | 任一核 0 项（ZT_BOOT 已见） | 任意 | FAIL + `cpu2::evidence`/`cpu1::evidence` | R2 |
| A3 | 无 ZT_BOOT | — | FAIL | 基线 |
| A4 | 全项已出结果，仅缺标记 | 缺失 | PASS + 两核 `TRUNCATED_AFTER_ALL_ITEMS` | R1 |
| A5 | 执行窗口到点 | — | **不判失败**（只加 `run::timeout` NOTE item） | R1 |

> **A 族口径（★ 2026-09-19，勿再混淆）**：本表是**场景判据**表 —— 每行回答"在该场景下**不得**判成什么"，
> **不是**某个 case 的**预期结果**。以 `case_quick_invalid` 为例：其**用例预期**是 **PASS**
> （`test.sh` 写明 Normal = 两核 SUCCESSFUL + 全项 PASS；实测 `ready=0, init_res=22` 即驱动按
> `quick-timeout-ms=0` 拒绝，**正是设计期望**；硬件批次 2 实测 **PASS (3/3)**，判定正确）。
> A3（任一核无 `ZT_BOOT` ⇒ FAIL）约束的是**场景**：**若**两核均无 `ZT_BOOT` 才不得判 PASS，
> **不可**反读为"`case_quick_invalid` 必须 FAIL"。

**`mbox_test`** —— marker-only 判定

| # | 逐项状态 | 收尾标记 | 判定 | 轮次 |
|---|---|---|---|---|
| M1 | ~~无逐项证据，仅两核 marker~~ **（2026-09-20 B2 起事实性失效：逐项改走 D2 结果 token `MBOX_TEST:<core>:(PASS\|FAIL):<name>`，见 §6.9.8）** | 两核各自 `PROJECT EXECUTION SUCCESSFUL`（空格形态） | PASS = 两核各自收尾 SUCCESSFUL **且** 全部行为项 PASS（零行为项 ⇒ 不得 PASS） | 基线 / **明确不做**（F1 的参考口径；本行仅更正"无逐项证据"这一事实描述，判据口径未变） |

**基础设施**（`regression/run_all.py` + 哨兵）

| # | 情形 | 处置 | 轮次 |
|---|---|---|---|
| I1 | ELF 缺 `test_results_valid` | **强制重建** + 循环前 `audit_stale_elves()` 列出全部旧镜像 | R2 |
| I2 | i3c `--skip-build` | 仅当**该 case 自己的** ELF 含哨兵符号时才允许，否则重建 + 告警 | R2 |

> **R6 可达性实测（2026-09-19）**：全仓 1666 份归档产物中，同一核流里"相反标记并存"**仅 1 例真实运行实例** —— `i3c_i2c_switch_test/case_switch_core`（硬件批次 3，r1/r2/r3 可复现）：CTRL 流为 `FAILED` → `SUCCESSFUL`，last-wins 取到 `SUCCESSFUL`。**该实例未产生框架假 PASS**（逐项证据/整核零证据守卫兜住了；hw3 用的 `dbg_run.py` 是调试工具、显式不判定）。结构上仅该工程会命中：其固件自己在两核各打一条标记（`cpu0_i3c_controller/src/i3c_i2c_controller_test.c:663`、`cpu1_i3c_target/src/main.c:49`）；`app_iwdg`/`wdt` 的自打标记都在**破坏性 case**（已屏蔽），`mbox_test` 用自己的 substring 判定且**已显式要求 FAILED 不出现**（天然免疫）。

> **归因纪律（★ 2026-09-21，非判定语义变更）**：本表冻结的是**判定语义**；"结果由 PASS 转 FAIL"的**归因**另见 qsh-test-compare §5 —— 框架级改动（日志文法统一 / 单出口 / `LOG_INF` 路径）会改变跨核 dispatcher 时延，**时序敏感用例**（固定 `k_sleep` 抢窗口，如 `broadcast_nack_recovery`）的翻转在排除前**不得记为驱动回归**（KNOWN_ISSUES #98 增补段）。

**明确不做（防反复重提）**

1. **i3c 的"部分项缺失容忍"（D3）** —— 有意设计；只堵"整核零证据"（D2）。
2. **`mbox_test` 的 marker-only（M1）** —— 无独立逐项证据，是 F1 的参考口径。
3. **`SKIP` 计入"确定结果"（G3）** —— 主动跳过 ≠ 无证据。
4. **Z 族的"部分证据"不收紧（Z5）** —— 无 mem 通道，NOT_RUN 结构不可达。
5. **`eth_test` 的 mem 合并跳过 NOT_RUN（E4）** —— 现状保留，缺项会落成 MISSING；R5 起该路径受 E6 独立性门约束。
6. ~~**"某一核整核 mem 兜底"不收紧（D6 的反面）** —— `tgt_all_missing_mem_ok` 是 R3 有意保留的合法路径；R7 只堵"**整个 case 零 token**"（G9）。再提此议须先推翻 R7 的判据口径。~~ **【R8（2026-09-21）撤销本条】** —— 用户裁定 2026-09-20「B 严：mem 一律不算证据」⇒ `tgt_all_missing_mem_ok`（某一核整核由 mem 补回）**不再合法**，一律按 G10 判 FAIL。R7 的"只堵整个 case"口径**已被 R8 取代**（R8 下沉到逐项）；本条不再作为"明确不做"项保留，**反向重提须先推翻 R8**（= 推翻用户裁定）。

### 6.3 问题记录规范

- 定位闭环的问题写 `<BUG>_<SUMMARY>.md` 总结（现象/根因/证据/修复/验证/通用教训）存档供后续避坑；调试中的状态记 `<NAME>_STATUS.md` 并维护状态表。
- 新建工程先读样板 `projects/i3c_dualcore_test/tests/` 下已有问题总结，确认不会重犯。
- **沉淀准则**：只沉淀"会反复遇到"的坑（环境/工具链/方法学，如连接残留、I-cache、ISR 禁 LOG）进 `KNOWN_ISSUES.md` 或对应 skill；**一次性问题**（修复后不再遇到的具体 bug）只写 .md 存档，不加入 `KNOWN_ISSUES.md`/skill。判断标准：换机器/测试/驱动会不会再踩？不会再踩就不沉淀。

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
    │   ├── <driver>_test_report.json
    │   ├── serial_CTRL.log          # 原始串口归档（§6.4.7，双核才有）
    │   ├── serial_TGT.log           # 原始串口归档（§6.4.7）
    │   └── replay_meta.json         # 离线重放元信息（§6.4.7）
    └── ...
```

- **会话目录分配/写入一律走公共模块 `common/report_dir.py`**（`allocate_session_dir()` / `case_dir_for()` / `read_summary()` / `update_summary()`），**禁止 run 脚本自算归档路径**；一次批量会话（`regression/run_all.py`）内全部 case 共用同一个会话目录；单独跑某个 case（`run_*_tests.py` 不带 `--session-dir`）时自动分配新会话目录。
- 旧格式目录（`reports/<date>/`，无 idx）不再产生，历史归档保持原样可读。
- 单次 `dbg_run.py`（调试）不归档，只归档一键回归。

#### 6.4.2 报告内容模板（Markdown）

渲染真源 = `common/judge.py` 报告渲染收敛区（`REPORT_TABLE_HEADER` / `format_report_header()` / `coverage_expected()` / `source_label()` / `clip_cell()`；该区**只做渲染**，PASS/FAIL 算法不在此）。全家族逐字一致。

```markdown
# <driver> 测试报告
- 工程: <driver>
- case: <case_xxx>
- 日期: <YYYY-MM-DD HH:MM:SS>
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，§6.6）
- 执行标记: SUCCESSFUL|FAILED|UNKNOWN
- 内存兜底: 有效 | 无效（哨兵未置位） | 不可用（未执行）…
- 结果: PASS (N/N)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| write_read_16 | ✅ PASS | token | 16B 0x00..0x0F | - | |
| monobit_8k | ✅ PASS | token | 46%~54% ones | ones=32775(50.0%) | |

## 汇总

OVERALL: PASS
```

- **头部 bullet**：字段顺序固定 **工程 / case / 日期 / 板级 / 环境 / 执行标记 / 内存兜底 / 结果**，缺项不输出空字段（`format_report_header()`）。「内存兜底」行收 `mem_state_line()` 输出（`有效` / `无效（哨兵未置位）` / `不可用（…）` 等），工程可另加 `核执行标记` 行。
- **结果**列：结果词 → 文案（`result_text()`：`✅ PASS` / `❌ FAIL` / `⏭️ SKIP` / `⬜ NOT_RUN` / `⬜ MISSING` / `⚠️ INCOMPLETE`；未登记词保留原样 `⬜ <词>`）。
- **来源**列：统一词表 `token` / `mem` / `mem(NOT_RUN)` / `missing`（`source_label()`：去 `src=` 前缀；`token(skip)`→`token`，`mem-unavailable`→`missing`；**`mem(NOT_RUN)` 保留** = 内存明确读到该项没跑，与 `missing` 语义不同）。
- **期望**列：来自 COVERAGE.md 该测试项的预期描述（`coverage_expected()`，§6.5.3）；超 60 字符截断 + `…`（`clip_cell()`），**完整值在 `tests/COVERAGE.md`**。
- **实测(INFO)**列：INFO 约定行（`INFO <test_name>: <k>=<v>[,…]`）的 `key=value` 拼接；无 INFO 填 `-`。
- **失败原因**列：FAIL 的 reason 原文（§6.2.1/§6.5.5：zassert `Assertion failed at` 行）。
- **单元格转义**：`md_table_row()` 自动 `|`→`\|`、换行压平（裸 `|` 不转义会把整行顶错列）。
- **尾部**：`## 汇总` + `OVERALL: <status>`。判定口径见 §6.2.4（本模板只渲染，不定义判定）。
- **例外 —— `mbox_test` 刻意不套本模板**：其表为「核 × 类别」矩阵 `| 测试项 | 核 | 类别 | 结果 | 明细 |`，结果列用 `👁 OBS` / `🛡 GUARD` / `⚠ OBS_DIFF` 语义（观察项/结构性守卫，**非行为证据**），且**无** `## 汇总`/`OVERALL` 尾部。**这是有意保留的差异，不是漏改 —— 禁止去"统一"它**。

#### 6.4.3 内存兜底合并规则（串口缺项自动补）

回归结束（串口已关）后，脚本调用本工程 `dump_results.py --case <case> --json` 读内存结果，**只用于填补串口 ztest 输出缺失的测试项**（来源语义不变：token = 串口 ztest 输出；glue 自动记账，§6.1.5）：

| 串口/内存情况 | 判定 |
|---|---|
| 串口有 PASS/FAIL | 以串口为准（来源=token） |
| 串口缺但内存有值 | 用内存值（来源=mem，报告标注） |
| 两者都无（数组 -1） | NOT_RUN，case 判 FAIL |
| 内存读取失败（连接残留等） | 容忍：标记 `mem-unavailable`，不阻断回归 |
| 哨兵门非 `ok`（`no_sentinel_symbol`/`invalid`，★ 2026-09-18） | 该核结果**清空**（消费者按 MISSING 处理，fail-closed）；状态进报告的 `mem_state`，三态判据与动作见 §6.2.2 |

内存兜底在 case 跑完后立即执行（目标仍在，interrupt 可读）；`dump_results.py` 必须支持 `--json`（输出 `{"CTRL": {...}, "TGT": {...}, "reasons": {...}}` 或单核版 `{"TGT": {...}}`）；`TEST_NAMES` 必须按 **ztest_unit_test 链接序**（SORT_BY_NAME，KNOWN_ISSUES #27）与 glue 记账 idx 对齐。

- **TEST_NAMES 双份同步（★ 2026-09-09）**：run 脚本（run_test.py）与 dump_results.py **各自硬编码一份** `TEST_NAMES`（都是链接序，非 protocol.h 枚举序）。增删测试项必须同步两份：新项插在链接序前部会让未同步的旧列表整体错位一格，mem 兜底结果张冠李戴/假 MISSING。构建后 `nm --numeric-sort <elf> | grep z_ztest_unit_test` 复核全序，并同步更新两处注释里的实测序清单。

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
- **`cases.yaml` 是"哪些工程/case 已启用"的唯一权威源**。`CLAUDE.md` §2 各工程后的 `(N)` = 本文件该工程**已启用**的 case 数（≠ 工程目录里的 case 总数）。增删工程/case 时按此清单同步，勿只改一处：
  1. `regression/cases.yaml` —— 增删条目（唯一权威源）
  2. `CLAUDE.md` §2 —— 单核/双核清单 + `(N)`
  3. `README.md` §4 工程表 + §「外部接线记录」（新工程登记接线，片内写"无"）
  4. 工程自带 `tests/COVERAGE.md` / `expected/`（骨架见 qsh-test-gen）
  5. `regression/run_all.py` 的 **`PROJECT_SCRIPT`** —— 加 `{工程名: "run_test.py"}` 映射。
     该表是**逐工程硬编码**的"工程名 → run 脚本"字典；未入表的工程命中
     `PROJECT_SCRIPT.get(proj) == None` → 打印 `[run_all] SKIP <case>: 无 run 脚本映射` 并**按 SKIP 记账**
     （不崩、但也不跑）⇒ **只改 `cases.yaml` 不够**，新工程接入必漏这一步。
     **已实施（2026-09-18）**：`_resolve_script()` 已按目录约定自动发现 `<proj>/tests/run_test.py`，
     `PROJECT_SCRIPT` 仅保留**覆盖用**（未命中不再静默 SKIP）。
  6. `regression/run_all.py` 的 **`DUAL_CORE`** —— **第二张**逐工程硬编码表：未登记该集合的**双核**工程，
     run_all 只传 `--tgt-uart`，双核 run 脚本缺 `--ctrl-uart` 直接退出 ⇒ 该工程**所有 case 恒 `FAIL 0/0`**
     （日志尾部 `ERROR: --ctrl-uart/--tgt-uart required`）。**新双核工程 `PROJECT_SCRIPT` + `DUAL_CORE` 两张都要核**。
- 流程（按 run_all.py 代码实际顺序）：读清单 → **flash 全片擦除一次**（`--erase-only`，DebugServer 互斥退出后重启，用户 #6；失败=环境 FAIL 批量中止）→ **分配会话目录**（§6.4.1）→ 环境预检（§6.4.6，报告写入该会话目录 `precheck_report.md`；可 `--no-precheck` 跳过）→ **每 case 前置 DebugServer 存活检查**（E-2，§6.2.3 第 6 条：不活则复用 acquire 自愈链拉起，起不来记 `ENV` 并跳过该 case、不烧录）→ 逐 case 调对应工程 run 脚本（`--case` + `--session-dir`）→ 每 case 日志独立存 `session/logs/` → 结束总表 + 退出码（全 PASS=0；有 FAIL **或 ENV**=1；预检 FAIL=2）。
- **单 case FAIL 不阻断后续 case**（日志记 FAIL 继续跑完）。
- **双核 UART 由 run_all.py 配置区显式注入**（COM15/COM16，`--ctrl-uart/--tgt-uart` 传参）——双核 run 脚本**必须显式 UART**，不支持自动发现（2026-08-29 实测）。
- **i3c skip-build 优化**：同会话首个 i3c case 触发构建，后续 6 个 case `--skip-build`。**勘误（2026-08-31 实测）**：首个 case 实际只构建它自己（run 脚本 per-case 构建粒度），并非"全量构建 7 个"——后续 case 会跑上次全量回归的旧镜像；同会话内固件源码未变时无害，**动过固件源码后必须先对 i3c 工程单独全量构建再跑 run_all**（KNOWN_ISSUES #88 勘误，勿依赖 skip-build 假设）。**2026-09-18 起该路径已自动化（哨兵守卫）**：`run_all.py` 仅在该 case **自己的** ELF 含有效性哨兵 `test_results_valid` 时才 `--skip-build`，否则强制重建 + 告警；并在 case 循环前 `audit_stale_elves()` 列出全部缺哨兵的旧镜像（缺符号 = 哨兵加入前编译的旧构建，内存兜底会对它 fail-closed 成 `no_sentinel_symbol`）。
- 批量全程脚本自驱（构建/烧录/采集/归档/自愈在各 run 脚本内），agent 只负责启动与失败后介入。

#### 6.4.6 环境预检（`regression/precheck/run_precheck.py`，链路级）

```bash
python regression/precheck/run_precheck.py [--session-dir <dir>] [--timeout 40]
```

- **范围 = 链路级**：① 串口枚举可用（CTRL/TGT 端口存在且可开）② DebugServer 在线（GDB 连 127.0.0.1:1025）③ 双核 load + 各自 boot banner 与 ZT_BOOT ④ 两侧无静默。**不含外设接线探测**——4 个 IO 接线检测后置到**独立 IO 测试工程**（后续任务；IO 将固定接死，所有测试共用同一硬件环境，不再每次换线）。
- **任一 FAIL → 退出码 2**，批量（§6.4.5）中止，交 agent 排查（qsh-debug）；预检可独立运行；预检报告写入会话目录 `precheck_report.md`。
- 环境变量：`PRE_CTRL_UART`（默认 COM15）/ `PRE_TGT_UART`（默认 COM16）。

#### 6.4.7 原始串口归档与离线重放（★ 2026-09-20）

**动机（用户裁定）**：判定层消费的是**串口原始行**（`parse_ztest(lines, …)` / 各家族行解析），
而报告只落**派生结果**（`.md`/`.json`）。没有原始行，任何"只改脚本"的改动都无法离线验证，
只能重跑固件 —— 而整轮回归的开销主要就花在重跑固件上。故：**每 case 的原始行按核落盘**，
使"只改脚本、不重跑固件"成为可能。

**归档布局**（跟随既有会话目录，§6.4.1）：

```text
<会话>/<project>_<case>/
├── <driver>_test_report.md          # 既有：派生结果
├── <driver>_test_report.json        # 既有：派生结果
├── serial_CTRL.log                  # 新增：controller 核原始行（单核工程无此文件）
├── serial_TGT.log                   # 新增：target 核原始行
└── replay_meta.json                 # 新增：case/board/各核串口名/timeout/脚本 md5/时间戳
```

- **写入真源 = `common/serial_archive.py`（`archive_serial()` / `read_serial_log()` /
  `read_meta()`）**，24 份 `run_test.py` 在 **采集之后、判定之前**调用，禁止各脚本自写归档逻辑。
- **双核必须分核各写一份**（`serial_CTRL.log` + `serial_TGT.log`）。合并成一份会把两核的行
  交错，破坏 `parse_ztest` 的核归属判定 ⇒ **无法按核重放**。单核只写 `serial_TGT.log`。
- **硬约束（缺一不可）**：① 归档逻辑整体 try/except，异常只打一行 warning，**不改变判定与控制流**；
  ② 不改动任何既有函数的返回值/签名；③ 判定逻辑一行不改；④ 落盘用 `open(..., newline="")`
  （`Path.write_text()` 在 Windows 会把 LF 翻成 CRLF，本仓行尾混用）。
- 行尾契约：写入 = `"\n".join(lines) + "\n"`（非空时）；读侧 `read_serial_log()` 去**唯一一个**
  末尾换行再切分 ⇒ `read_serial_log(write(lines)) == lines`（逐行等价，已单测覆盖空行 / 末尾空行）。

**重放入口 = `regression/replay.py`（不碰板子、不构建、不烧录）**：

```bash
cd 810_driver_test
python regression/replay.py --session reports/2026-09-20-1 --list          # 列出可重放 case
python regression/replay.py --session reports/2026-09-20-1 --case trng_test_case_default
python regression/replay.py --session reports/2026-09-20-1 \
    --project trng_test --project i3c_dualcore_test --limit 2             # 按工程抽样批量重放
python regression/replay.py --session reports/2026-09-20-1 --render-from-json   # 用 .json 反推 .md（校验）
```

- **复用同一套判定/渲染函数**：重放不另写一套判定。它把归档的原始行接回**该工程自己的
  `run_test.py`**，让它的 `main()` 原样跑完 判定 → 渲染 → 归档；**只把"硬件采集层"
  （`run_case` / `capture_case` / `run_pass` / `SerialCapture`）与"调试器层"
  （`preflight_gdb` / `clean_gdb_residue` / `restart_debugserver`）换成替身**。
  24 份脚本的采集函数签名不同 ⇒ 需一层**接线**（`regression/replay.py` 的适配器），
  **接线层不含任何判定逻辑**。
  > **2026-09-20 A1 更新**：判定 → 渲染 → 落盘已收敛到 `common/process.py`（§6.4.8），
  > 适配器现在**只用于替换 L1 采集层**。另新增 `--l2-direct`：读归档后**直接**调
  > `process.process_archive()`，完全不经 `main()`、不猴补丁 —— 这是"L1/L2 边界变成
  > 接口"之后的直接形态，产出与 `main()` 路径逐字节相同（§6.4.9）。
- **默认输出 `tmp/replay/<会话>/`，绝不覆盖 `reports/`**（写进 `reports/` 需 `--out` + `--force`）。
- `--freeze-time`：把渲染时钟冻结到归档时间戳，使重放 `.md` 可与归档 `.md` 逐字节比对。
- **可重放 / 不可重放边界**（用户据此决定"要不要重跑板子"）：

| 改动类型 | 可重放 | 说明 |
|---|---|---|
| 串口解析（`parse_ztest` / 各家族行解析） | ✅ | 输入就是原始行 |
| 判定逻辑（`evaluate_case` / `judge_case` / 各家族谓词） | ✅ | 输入就是串口行 |
| 报告渲染（`write_markdown_report` / 表头 / 列序 / 转义） | ✅ | 输入是判定结果 |
| 报告 JSON 落盘、`summary.md` 索引 | ✅ | 同上 |
| **内存兜底（`dump_results.py` 读目标内存）** | ❌ | **内存内容未归档**，重放一律走 `mem-unavailable` 分支 |
| 构建 / 烧录 / 串口时序 / 环境预检 | ❌ | 与脚本无关，必须上板 |
| 固件本身（`.c/.h`） | ❌ | 输出会变，必须重跑 |

- **与归档 `.md` 逐字节比对不成立 ≠ 重放失效**：归档 `.md` 是**当时那版渲染器**的产物；
  渲染器改过之后，重放必然产出新格式（这正是"只改脚本"要达到的效果）。
  验证渲染接线本身是否正确的方法：对**由当前渲染器产出**的报告跑 `--render-from-json`，
  应当逐字节相同。
- 已知边界补充：`mbox_test` 的判定入参是 `(时刻, 字节块)` 形式，归档时用**判定同款的
  `_split_lines()`** 还原成逐行文本，保证归档内容与判定实际消费的行一致；
  该工程报告的核×类别矩阵模板（§6.4.2 例外项）不在重放"统一"范围内。

#### 6.4.8 分层原子化：L1/L2 边界与处理层契约（★ 2026-09-20 A1）

**目标（用户裁定原话）**：「尽量将脚本原子化…每一层脚本都隔离，捕获串口脚本只负责捕
串口的输出然后输出数据出来，整理数据的脚本、调用全部测试的脚本，每个脚本应当独立，
确保脚本的输入和输出标准化…各个节点互不干扰」；「如果测试代码本身没有修改，只是外部
处理脚本的修改，就不要强制全量测试了…只调整相关脚本去测试固件之前输出的值」。

**分层与边界**（分界线本来就存在 —— 每份 `run_test.py` 里那行 `archive_serial()`；
本步只是把它**提成接口**）：

```text
L3 编排  regression/run_all.py
   │  调 L1（case / 端口 / timeout）
L1 采集（唯一碰板子）  main() 的 构建→烧录→串口采集 段
   │  ★ L0 契约（§6.4.7）= serial_<CORE>.log + replay_meta.json
L2 处理（纯离线）  common/process.py 的 run() / process_archive()
   │
L0 契约  common/serial_archive.py（归档格式） + common/judge.py + common/ztest_report.py（判定/渲染标准）
```

实现落点：`common/process.py`（新建）。命名理由：层名取 L2 的通用语义（processing），
不绑定任何工程；`common/` 已在 24 份 `run_test.py` 的 `sys.path` 上，`import process` 零配置。

**L2 输入契约**

| 项 | 形态 | 来源 / 是否归档 |
|---|---|---|
| 归档 | `serial_CTRL.log` / `serial_TGT.log` + `replay_meta.json` | L1 产出（§6.4.7）；读侧 `process.read_archive()` |
| 采集行 | `{核名: [原始行 str, …]}` | 在线 = L1 归一后的**同一个对象**；离线 = 从归档读 ⇒ 两条路径输入逐字相同 |
| `mem` | `dump_results.py --json` 的 payload 或 `None` | **L1 侧**（读目标内存）。★ **不进归档** ⇒ 离线恒 `None`（走 mem-unavailable 分支） |
| `facts` | `{tgt_uart, ctrl_uart, timeout, case_timeout, timed_out, elapsed_s, case, …}` | **未归档的采集事实**。只允许喂给"报告里明确声明不参与判定"的字段（环境行 / 采集时长） |
| case 配置 | 家族谓词 / `TEST_NAMES` / banner / 渲染函数 | 各工程 `run_test.py`（本层不复制第二份） |

**L2 输出契约**

| 项 | 形态 |
|---|---|
| 判定结果 | **各家族自己的结构，原样透传** —— 本层不归一化、不补字段、不排序 |
| 报告 | `<driver>_report.json` + `<driver>_report.md`（由工程自己的渲染函数写，写法与行尾**逐字不变**） |
| 汇总 | `summary.md` 一行（key = `<driver>_<case_label>`，计数口径由工程 `summary` 钩子给） |
| 返回码 | `0` = 全部 PASS；`1` = 有 FAIL |

**每工程的唯一接入点 = 模块级 `l2_spec(facts=None, test_names=None)`**（24/24 已接入）：

```python
def l2_spec(facts=None, test_names=None) -> process.ProcessSpec:
    return process.ProcessSpec(
        project=DRIVER_NAME, driver=DRIVER_NAME, report_base=REPORT_BASE,
        judge=...,          # ★家族特有：谓词 / mem 门 / 零证据守卫 / R6 / R7 全在这
        console=...,        # 控制台渲染（逐项行、NOTE 行）
        render_md=...,      # 报告 .md（工程既有函数，不改）
        render_json=...,    # 报告 .json（缺省 = process.py 的通用写法）
        summary=...,        # (status, passed, total)
    )
```

`main()` 的接线（24 份同构，只有函数名不同）：

```python
lines_by_core = {"TGT": [l for _, l in raw["tgt_lines"]]}   # ← L0 归档形状
if not args.no_archive:
    archive_serial(..., lines_by_core, ...)                 # L1 产出归档
return process.run(l2_spec(facts), case=args.case, lines=lines_by_core,
                   session=session, write=not args.no_archive, mem=...).rc
```

**家族差异怎么放**：`ProcessSpec` 的钩子。**不在 `process.py` 里写 `if family == …`** ——
那等于把 24 份的分叉搬进一个文件（7 个判定家族的差异是真实语义差异：双核结构不对称 /
mbox 标记式 / rtc_ls 自定义 token / iwdg 双 pass，不是噪音）。

**已知边界（诚实清单）**

1. **mem 兜底不进归档**（D7）：`process.py` 只声明"mem 是 L1 入参"，不负责取得它。
   离线重放一律 `mem=None` ⇒ **改内存兜底逻辑永远无法离线证**（要离线证需把 dump 的
   原始 payload 一并归档 = L0 契约升版）。
   - 本步顺带把 `eth_test` 的 `mem_fallback()` 拆成 **L1 的 `dump_mem()`**（起 gdb 读内存）
     + **纯合并的 `merge_mem()`**，使"判定内含硬件访问"这个边界泄漏消失（其余 F4 族
     本来就已拆分）。
2. **`facts` 里的时序事实不进归档**：`timed_out` / `elapsed_s` 离线取缺省值 ⇒ 报告里
   "采集时长"行离线为 `0.0s`。该行已明文标注"不参与判定"。
3. **L1 完全不在本步范围内**：构建 / 烧录 / 串口时序 / 环境自愈仍散在 24 份 `main()` 里
   （A2 处理）。L2 的抽取**不改变**这些行为。

**离线入口**：

```bash
# 主路径：把归档喂回该工程自己的 main()（只把硬件层换替身）
python regression/replay.py --session reports/<会话> --project <p> --freeze-time

# 纯 L2 路径（不经 main()、不猴补丁）：直接调 process.process_archive()
python regression/replay.py --session reports/<会话> --project <p> --l2-direct --freeze-time
```

两条路径的产出**逐字节相同**（已验，见 §6.4.9）。

#### 6.4.9 「凭什么抽样就够」——可操作的判据（★ 2026-09-20 A1）

**先说清楚一条**：**逐字节比对只证"输出没变"，不证"输出仍对"** —— 新旧代码若犯同一个
错，比对必然通过。下表的"证据强度"一列就是这个意思。

**① 判据：什么改动属于"抽样/离线可证"**

唯一判据 = **改动的输入是否是「一串原始串口行 + 该 case 的 declared 表」，输出是否是
「判定 dict / 报告」**。

| 改动类型 | 可离线证? | 为什么 |
|---|---|---|
| `common/judge.py` 谓词 / `ztest_report.py` 解析 / `write_markdown_report` 渲染 / JSON 落盘 / summary 索引 / `common/process.py` | ✅ | 输入是纯数据，**输入域可任意构造** ⇒ 可做穷举/属性测试，不需要板子 |
| 归档落盘（`serial_archive`） | ✅ | 有往返契约 `read(write(x)) == x` |
| L1 采集（超时 / 两遍烧录 / 串口发现 / 环境自愈） | ❌ | 时序相关，归档里没有"时间轴" |
| `dump_results.py` 内存兜底 | ❌ | **内存内容未归档**（§6.4.8 边界 1） |
| 固件 `.c/.h` | ❌ | 输出会变 |
| 环境预检 / 擦除 / DebugServer | ❌ | 与脚本无关 |

**② 选样规则（若确实要抽样）**

1. **按"被改动触及的公共代码路径"选，不按"工程"选** —— 先列分支，再找触发它的 case；
2. **每个判定家族至少 1 个**（Z / F4 / D3 / K / M / A / eth）；
3. **每个 L1 适配器组至少 1 个**（`SINGLE_CAP / PORTS_DICT / DUAL_RUN / MBOX / IWDG`）；
4. **双核与单核各至少 1 个**；
5. **含异常路径的**：≥1 个带 mem 兜底、≥1 个零 token、≥1 个 truncated、≥1 个标记矛盾；
6. 数量上 **7 家族 × 1 + 5 适配器去重 ≈ 10~12 个 case** 是"改了 L2 一行"的合理抽样面。

**③ 凭什么断定"没抽到的也对" —— 三档证据，强度递减**

| 档 | 做法 | 本步状态 |
|---|---|---|
| **第 1 档（最强）** | **合成输入穷举**：判定是纯函数 ⇒ 直接构造串口行覆盖中间状态矩阵 | ✅ **已做**：24 工程 × 10 变体 = 240 个合成归档（见下） |
| **第 2 档** | **全量归档重放** + 逐字节比对 | ✅ **已做**：17 个真实归档 case（覆盖 12 个工程 / 全部 7 个判定家族） |
| **第 3 档（兜底）** | 家族级 byte-compare | ⚠️ **不足**：只覆盖"归档过的输入"，未归档 case **零证据** |

**第 1 档的状态矩阵（每个工程各跑一遍，`tmp/a1/synthgen.py` 生成）**：

| 变体 | 目标判定分支 |
|---|---|
| `allpass` | 常规全 PASS + `PROJECT EXECUTION SUCCESSFUL` |
| `fail` | 逐项 FAIL（带 `Assertion failed` 归因行）+ `FAILED` 标记 |
| `skip` | 主动跳过（`ztest_test_skip`）⇒ 不计入分母 |
| `missing` | 已声明项**无 token** ⇒ `MISSING` ⇒ 维持 FAIL（R4 红线） |
| `nomarker` | 全 PASS 但**缺收尾标记** ⇒ F1 截断接受路径（`TRUNCATED_AFTER_ALL_ITEMS`） |
| `conflict` | `SUCCESSFUL` 与 `FAILED` 并存 ⇒ R6 fail-closed（`CONFLICTING_MARKERS`） |
| `zero` | 一个 token 都没有 ⇒ **零证据守卫**（不得判 PASS） |
| `noboot` | 无 `ZT_BOOT` ⇒ BOOT 门 / mem 独立性门 |
| `extra` | 未声明的额外 token（不得被吞掉/误计） |
| `empty` | 该核零行 |

**④ 防"空转比对"（本步实测踩到的坑）**：合成输入若没走到判定分支，比对就是**假绿**
（0 产物文件 = 0 差异）。实测两例：① 合成行缺角色 banner ⇒ otbn / otbn_selftest /
mbedtls 的 `_own_port` BOOT 门直接 `CAPTURE FAILED`；② `rtc_ls` 缺 `CPU1 RTC` banner
⇒ 采集行落空。

⇒ **强制自检**：`tmp/a1/coverage.py` 对每个 (工程 × 变体) 抽出**实际落盘的判定面签名**
（`result` / `execution` / `ctrl_done` / `overall`），要求**同一工程的多个变体给出互不
相同的观测**。当前结果：**24 个工程全部 ≥ 4 种不同观测，空转嫌疑 0 个**
（`tmp/a1/coverage_table.md`）。典型观测：`allpass→result=PASS/execution=SUCCESSFUL`、
`fail→FAIL/FAILED`、`zero→FAIL/UNKNOWN`、`conflict→FAIL/CONFLICTING_MARKERS`。

**⑤ 还缺什么（诚实清单）**

| 缺口 | 状态 |
|---|---|
| **G-A 分支覆盖表**（"每条公共分支 ← 由哪个 case/合成输入触发"） | ✅ 本步给出（第 1 档状态矩阵 + `coverage_table.md`） |
| **G-B 未归档 case 的合成输入**（12 个无串口归档的工程） | ✅ 本步给出（240 个合成归档覆盖全部 24 个工程） |
| **G-C 独立预期真源**（"输出仍**对**"的判据，而非"没变"） | ✅ **已落地**（2026-09-21）：`common/expect_check.py` + 各工程 `tests/COVERAGE.md` 的 `expect` 块，文法/判定/纪律见 **§6.5.6**。**剩余缺口量化**（`--audit`，2026-09-21）：全库 **407** 项中已建模 **152**、**有独立预期 151（37.1%）**，其中**强预期仅 72（17.7%）**；**弱预期 79 = 已覆盖项的 52.0%**（`contract`/`driversrc` —— 只证"没变"不证"仍对"）。⇒ "判定仍正确"目前**只有约 1/5 的行有强独立支撑**，其余靠弱预期 + §6.2.4 冻结总表兜底。 |

**⑥ 本步的量化结论**（全部可复跑，命令见 §6.4.8）：

| 证据 | 规模 | 结果 |
|---|---|---|
| 真归档重放（改动前 vs 改动后） | 17 case / 62 产物文件 | **0 处差异** |
| 合成输入穷举（改动前 vs 改动后） | 240 case / 781 产物文件 | **0 处差异** |
| 纯 L2 路径 vs 原 `main()` 路径 | 14 工程×会话 / 34 报告文件 | **0 处差异** |
| 分支覆盖非空转自检 | 24 工程 × 10 变体 | **空转嫌疑 0** |
| 归档 `.json` → `.md` 重渲染（既有 `--render-from-json`） | 11 case | **11/11 与归档逐字节相同** |
| `py_compile` + 导入冒烟 | 24 `run_test.py` + `common/*` + `regression/*` | **全过** |

一键复跑：`bash tmp/a1/verify_all.sh`（全部离线，不碰板子）。

**⑦ 本步之后，「改处理层」能否不碰板子？**

| 改动落在哪 | 能否不碰板子 | 依据 |
|---|---|---|
| `common/process.py` / `common/judge.py` / `common/ztest_report.py` / 各工程 `l2_spec` 的 `judge`·`console`·`render_*`·`summary` 钩子 | ✅ **可以** | 输入是归档的一串原始行；`replay.py --l2-direct` + §6.4.9 的三档证据可离线证到"逐字节等价" |
| 各 `run_test.py` 的 `main()` 中 **L1→L2 接线段**（`lines_by_core` / `archive_serial` 调用 / `process.run`） | ✅ **可以** | 该段与渲染/判定同为纯数据变换；重放主路径（猴补丁 L1）可证 |
| **L1 采集段**（构建 / 烧录 / 超时 / 串口发现 / 环境自愈） | ❌ **必须上板** | 归档里没有"时间轴"，离线重放无从覆盖 |
| **mem 兜底**（`memory_backfill` / `dump_mem`） | ❌ **必须上板**（或先扩 L0 契约） | 内存 payload 未归档 |
| **固件 `.c/.h`** | ❌ **必须重跑** | 输出会变 |

> 断言（可被上表推翻）：**只要改动不落到"L1 采集段 / mem 兜底 / 固件"三者之一，就不需要上板**。
> `bash tmp/a1/verify_all.sh` 全绿 = 该断言在本仓库当前状态下成立。

#### 6.4.10 L1 采集层契约（★ 2026-09-20 A2）

A1 把 **L2**（判定 → 渲染 → 落盘）提到了 `common/process.py`（§6.4.8）。本步把
**它之前的那一段**提到 `common/acquire.py`：**构建 → 烧录 → 串口采集 → 落 L0 归档 →
释放资源**。分层线不是新发明的，是 §6.4.8 里已经写死的那条 —— 每份 `run_test.py`
里 `archive_serial()` 那一行之前碰硬件、之后纯数据。

```text
L3 编排  regression/run_all.py（+ 会话目录分配 / 预检 / 擦除 / 汇总）
   │  调 L1（case / 端口 / timeout / session）
L1 采集（唯一碰板子）  common/acquire.py 的 5 个策略
   │  ★ L0 契约（§6.4.7）= serial_<CORE>.log + replay_meta.json
L2 处理（纯离线）  common/process.py 的 run() / process_archive()（§6.4.8）
```

实现落点：`common/acquire.py`（新建，约 1100 行）。命名理由：层名取 L1 的通用语义
（acquisition），不绑定任何工程；`common/` 已在 24 份 `run_test.py` 的 `sys.path` 上，
`import acquire` 零配置。

**L1 输入契约**（`AcquireSpec` 家族，5 个 `*Spec` dataclass）

| 项 | 形态 | 说明 |
|---|---|---|
| case 名 | `str` | 归档子目录 `<driver>_<case>` |
| board | `str` | 归档元信息（`lsqsh_evb@1os/…` / `@2os`）；同时也是 `west build -b` 的取值 |
| 串口 | `tgt_uart` / `ctrl_uart` | 单核可留 `None` ⇒ `find_tgt_uart()` 自动发现；双核必须显式（不支持自动发现） |
| timeout | `case_timeout`（Z/D3/MBOX/IWDG）/ `timeout`（PORTS_DICT） | **采集窗口硬上限**，不是"固件跑完需要多久" |
| 构建 | `skip_build` / `src_dir` / `build_subdir` / `build_progress` / `build_timeout` / `build_conf` | 全部是 spec 字段 |
| 采集形态 | `driver_kind`(oneshot\|watch) / `pre_erase` / `hold_cpu2` / `stall_s` / `retry_on_no_boot` | 见下"策略"表 |
| 钩子 | `core_view` / `combined_lines` / `split_lines`（mbox）/ `mem_hook`（eth） | 工程特有语义留在工程侧，经 spec 注入 |

**L1 输出契约**（`Acquired`）

| 项 | 形态 | 是否进 L0 归档 |
|---|---|---|
| `lines` | `{核名: [原始行 str, …]}` | ✅ 就是 `serial_<CORE>.log` 的内容；同时**就是**交给 L2 的同一个对象 |
| `session` / `case_dir` | `Path` | ✅（归档落点） |
| `facts` | `{timed_out, elapsed_s, load_failed, case_timeout, …}` | ❌ **未归档的采集事实**。只允许喂给报告里"明确声明不参与判定"的字段 |
| `mem` | `dump_results.py --json` 的 payload 或 `None` | ❌ **不进归档**（§6.4.8 边界 1 的已知缺口 D7）⇒ 离线重放恒 `None` |
| `ok` / `err` / `phase` | `bool` / `str` / `'build'|'capture'` | — 供调用方复刻各自的失败分支 |
| `rc` | `int` | 非 0 = 中止整个回归（DUAL_RUN 的 `2` = 环境中止） |

**5 个采集策略**（与 `regression/replay.py` 的适配器分组同源；共 24 工程）

| 策略 | 采集机制 | 工程数 / 工程 | 关键参数 |
|---|---|---:|---|
| `single_cap` | `test.sh -r` 驱动 gdb；Python 只采一路串口，干等 timeout | 9（adc_temper / dma / emmc / eth / flash / iopmp / pwm / qspi_flash / wdt） | `bash_style='fixed'`、`com99_probe`（eth=False）、`mem_hook`（仅 eth） |
| `ports_dict` / `ports_dict_case` | Python 直接驱动 gdb；**两遍烧录**（prime 20s + capture）；串口经 `discover_ports` + `reader` | 10（i2cf / io_probe / spi / spid / spif / trng / rtc_ls / otbn / otbn_selftest / mbedtls） | `driver_kind`：`oneshot`（7 个）/ `watch`（otbn×2 + mbedtls）；`pre_erase`（前 5 个=True）；`hold_cpu2`（rtc_ls=False）；`stall_s`（mbedtls=300，其余 60）；`retry_on_no_boot`（watch 三个=True） |
| `dual_run` | `test.sh -r` + 双路串口 + **两遍烧录** + 静默判完成 + 环境自愈重试 | 3（i2c_pure / i3c_dualcore / i3c_i2c_switch） | `banner_key`（`I2C_PURE_TEST:` / `I3C_TEST:` / `I3C_I2C_TEST:`）；`announce`、`abort_on_load_fail`（仅 i3c_dualcore） |
| `mbox` | `test.sh -r`（stdout 丢弃）+ 双核早收敛 + wmic 兜底杀 gdb | 1（mbox_test） | 字节块级钩子 `core_view` / `combined_lines` / `split_lines` |
| `iwdg` | `test.sh -r` 跑 **prime 轮（丢弃）+ capture 轮**，`RUN_TIMEOUT=0` | 1（app_iwdg_cpu2_test） | `extra_logs`（cpu1/cpu2 合并旁证文件） |

**时序常量是实测值，一律逐字搬运**（不得"整理"/"优化"/改数值）：两遍烧录的 prime 轮、
`IDLE_S = 10`、`LOAD_WINDOW_S = 20`、`P1_HOLD_S = 5`、单核 hold cpu2 的
`set {int}0x4002200c = 2`、写包提速 `16384`、优雅中断 `\x03` + `detach`/`quit`、
以及各处的 `time.sleep`。

**为什么不是"一个通用采集脚本"**：24 份的采集差异分 5 个**真实语义形态**（差异是两遍
烧录 / 单核 hold cpu2 / mbox 字节块 / iwdg 双 pass / rtc_ls 无 hold 这类**真实差异**，
不是噪音）。把 `if project == …` 写进 `acquire.py` = 把 24 份的分叉搬进一个文件。
故：**函数体逐字搬运**，工程差异走 **spec 字段 / 钩子**，每个工程只留 <40 行薄壳
声明"我用哪个策略、参数是什么"。

**会话目录分配不在 L1**：`allocate_session_dir()` 有"取下一个空闲序号"的副作用 ⇒ 全流程
只能分配一次，故仍由 L3（各 `main()`）负责，L1 只接收 `session`（可为工厂函数以保持
原码的**求值时机**）。

**"改动可只抽样验" vs "必须全量"**（沿用 §6.4.9 的判据）

| 改动落点 | 证据强度 | 说明 |
|---|---|---|
| `AcquireSpec` 字段**值**（端口/超时/banner/src_dir） | **抽样可证** | 只影响一次采集的输入；等价性可由"字面量集合"离线比对（A2 的 E1b） |
| L1 内部**命令序列 / 参数 / 等待时长** | **必须全量上板** | 时序相关，归档里没有时间轴 —— 但可用 A2 的**静态等价比对**（E1/E1b + 负对照 E3）先做离线筛查 |
| `common/process.py`（L2） | 可离线重放（§6.4.8） | — |
| 固件 `.c/.h` | **必须全量** | 输出会变 |

**A2 自证证据**（脚本在 `tmp/a2/`，可复跑）

| 检查 | 方法 | 结果 |
|---|---|---|
| E1 逐字搬运 | 旧函数体经**声明的重命名映射**归一后，作为**连续语句序列**出现在 `acquire.py` 对应函数里 | 27/35 逐字；其余 8 个为参数化重写（cmd 构造 / 分支包装），列明在案 |
| E1b 字面量完整 | 旧 L1 体里的每个 str/int/float 字面量都必须在新 `acquire.py` **或本工程新薄壳**里出现 | 33/35 精确 + 2 个**已声明等价**（`60s no token` → `spec.stall_s`；`I2C_PURE_TEST:` 前缀 → `spec.banner_key`） |
| E2 非空转自检 | 17 条**关键路径**（两遍烧录 / hold cpu2 / 写包提速 / drain 线程 / `\x03` / IDLE / LOAD_WINDOW / P1_HOLD / wmic / 自愈 / mem 兜底 / west build / …）各自落在哪些被比对的旧函数里 | 17/17 全部落点非空（不是"比了个空"） |
| E3 负对照 | 故意扰动 `acquire.py` 的 6 处常量/命令/时长/寄存器 | **6/6 全部被检出** ⇒ 比对器对命令序列、参数、等待时长**敏感** |
| E4 接线表 | 24 个薄壳各自恰好调一次采集策略，且 L1 定义已清空 | 24/24 |
| 抽样上板 | 5 个代表（见下）改动前/后 A/B | 判定 5/5 全部一致；归档 3/5 逐字节完全相同（含两个双核工程的两路），其余差异经 **A/A 对照**证实为运行间噪声 |

抽样上板（改动前 = `tmp/a2/before/<p>/run_test.py`，改动后 = 现文件；同一 build）：

| 代表 | 形态 | 判定 A/B | 归档 A/B |
|---|---|---|---|
| `dma_test` | 单核 + `test.sh -r`（SINGLE_CAP） | PASS 7/7，逐项一致 | `serial_TGT.log` **逐字节相同** |
| `trng_test` | 单核 + 直驱 gdb（PORTS_DICT/oneshot） | 15/15 PASS，逐项一致 | 69 行，7 行差异 = TRNG 随机量；**A/A 对照同样是这 7 行** |
| `i2c_pure_test` | **双核** + `test.sh -r`（DUAL_RUN） | OVERALL PASS，`cases` 逐字一致 | `serial_CTRL.log` **逐字节相同**；`serial_TGT.log` 19/19 行一致（仅毫秒启动时刻不同） |
| `mbox_test` | **双核**（MBOX） | PASS 5/5，逐项一致 | **两路归档都逐字节相同** |
| `app_iwdg_cpu2_test` | **双核**（IWDG 两轮） | PASS 19 项逐项一致 | **两路归档都逐字节相同** |

**⚠ 对 `regression/replay.py`（§6.4.7 / A1）的影响 —— 已于 B1 接线（§6.10.1）**

A1 的离线重放靠**猴补丁工程模块上的 L1 名字**（`mod.run_case` / `mod.capture_case` /
`mod.SerialCapture` / `mod.run_pass` / `mod.memory_backfill` 等）。A2 之后这些名字
**已不在任何工程模块里**（实测 24/24 薄壳均已清空），补丁会打空 ⇒ 重放将落到真硬件上。

修法很小（A1 侧一行改一行）：把补丁目标从 `mod.<name>` 换成 **`acquire.<name>`** ——
`common/acquire.py` 的策略在调用原语时走的是**自身模块全局**，因此同样的猴补丁写法
直接生效（已实测：打 `acquire.run_case_shell` / `acquire.find_tgt_uart` 后调
`acquire.single_cap()`，走的确定是补丁版）。本步**未改** `regression/replay.py`
（不在 A2 写域内）；**B1 已按 §6.10.1 接线**（补丁目标由 `mod.<名>` 改为 `acquire.<名>`
+ `_guard_acquire()` 隔离网）。


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

- 固件在测试内把数值型实测结果打 `INFO` 约定行（§6.2.1，`ZT_INFO(name, fmt, ...)` 宏 = `printk("INFO %s: " fmt "\n", name, ...)`），报告"实际"列取用；统计类测试（吞吐/占比）必须上报，否则报告里预期 vs 实际无从对比。**第一参是字符串字面量，不是标识符**——它落在 `%s` 的**位置参数**上（属表达式而非名字拼接）⇒ 一律写 `ZT_INFO("throughput_64k", "bytes=%u,…", n)`；写 `ZT_INFO(throughput_64k, …)` 会被 C 当变量引用，报 `'throughput_64k' undeclared (first use in this function); did you mean '<suite>_throughput_64k'?`（实测 12 处同错一次暴露；同类的 `zassert_*` 名字参不受影响）。
- 行格式：`INFO <test_name>: <k>=<v>[,<k>=<v>]`（key 小写下划线），**name 必须与 `ZTEST(suite, name)` 注册名一致**。实测例：`INFO throughput_64k: bytes=65536,us=14701,mbps=35.66`。
- 上报时机：对应测试的 PASS/FAIL 之前；解析器 `re.search(r"INFO (\S+):\s*(.*)")`（KNOWN_ISSUES #5 前缀污染不变），k=v 按逗号切分进报告"实际"列（§6.4.2）。

#### 6.5.5 FAIL reason 可归因格式（来源 = zassert 输出）

`FAIL` 的 reason 来自 **zassert 原生输出**（断言失败时 ztest 自身 printk，`CONFIG_ZTEST_ASSERT_VERBOSE` 控制；成功后不打）：
`Assertion failed at <file>:<line> (<func>): <msg>`——解析器暂存断言行，遇到 `FAIL - <name>` 行归到该项（`<file>` 为固件按工作区相对路径**原样打印**（相对 `WEST_TOPDIR`），解析器无裁剪逻辑，实测例）：
`810_driver_test/projects/rtc_test/cpu1_rtc_test/src/rtc_test.c: 85:
rtc_set_get_roundtrip: (g.tm_mday not equal to s.tm_mday)`。
**判定要求**：断言必须用**带值对比的 zassert 宏**（`zassert_equal` / `zassert_within` / `zassert_mem_equal` / `zassert_false` 等），使失败文本含期望/实际值（`ret:1 != -EINVAL` 类），**禁止**只写"data mismatch"这类无信息文本；串口缺项时内存兜底 reason 为 glue 统计文本（`ztest fail_count=%u`，§6.1.5）。

#### 6.5.6 独立预期（机器可读 `expect` 块，G-C 落地，★ 2026-09-21）

**解决什么**：逐字节比对只证"输出**没变**"，不证"输出**仍对**"（新旧代码犯同一个错时比对必然通过）。
本节定义"仍对"的**独立证据源**：每个测试项从 `projects/<proj>/tests/COVERAGE.md` 末尾的
`## 独立预期（机器可读）` 段取一条**不由被测代码算出**的断言，拿归档报告的**实测 INFO 值**去校验。

工具 = `common/expect_check.py`（+ `common/test_expect_check.py` 单测），纯离线：只读
`COVERAGE.md` 与 `reports/` 归档，不碰硬件、不起子进程、不写文件（除非 `--out`）。

**① 块文法**（写在 COVERAGE.md 末尾「独立预期」段的 expect 围栏内）

```text
# <测试项> | <断言>[; <断言>...] | <来源标签>[:说明]
set_get_roundtrip    | delta_s in [0,1]                       | handcalc:1Hz tick 最大丢秒窗口
calibration_negative | calib_reg == 0x36007FFE; ctrl.b1 == 1  | handcalc:寄存器位布局手工代入
invalid_set_args     | ret == -EINVAL                         | contract:API 契约（无 INFO 通道 ⇒ 恒 NO_ACTUAL）
alarm_fires          | !none                                 | !none:INFO 被正则截断成阶段标记，回调无证据
```

- 一行一项；`#` 起首为注释，空行忽略；**围栏之外的行不解析**（既有覆盖矩阵不受影响）。
- 字段分隔 = `|`：`名 | 断言体 | 标签[:说明]`（第 3 段可省）。**名与断言里不得出现裸 `|`**（会串列），需要时写 `~` 正则。
- 双核工程用**不带核前缀**的项名（`test_x`）：检查器自动剥掉报告里的 `cpu2::` / `CTRL::` 前缀再匹配 ⇒ 同一条预期自动作用于两核。

**② 断言文法**：`key op value`

| op | 语义 |
|---|---|
| `==` `!=` `<` `<=` `>` `>=` | 数值优先比较（容忍 `0x0f` ≡ `15`）；非数值退化为字符串比较 |
| `in [lo,hi]` | 闭区间 |
| `~ <regex>` | 正则，对值整体匹配 |
| `absent` | 该 key 不得出现或无值 |

- `key` = 归档 INFO 里的键（`INFO <name>: k=v,…`，§6.5.4）；**`key.bN` 取该键第 N 位**（如 `ctrl.b1 == 1` 对应寄存器位域 `CTRL.CALIB_EN(BIT1)`）—— 否则只能断言整个字，而字里其余位是上一状态残留、不是确定预期（写下去就是编预期）。
- **关系型断言的右端可以是另一个 INFO 键**（如 `max_after <= first_after`）⇒ 表达**跨键不变量**（堆不单调上涨、两轮时间单调）；把右端写死成字面量 = 把某次实测值抄进预期。
- 值：十进制 / `0x` / `0b` / 带引号字符串 / `[lo,hi]`。
- 核限定写法 `core::key`（如 `CTRL::ret`）**暂未启用**（见 ⑦）。

**③ 来源标签三档**（决定独立性；实际判定还要过下面的伪独立三闸）

| 档 | 标签 | 含义 |
|---|---|---|
| **独立·强** | `handcalc` 手算 · `spec` 外部规范/数据手册/DT/寄存器位定义 · `golden` 黄金向量 · `crosscheck` 跨来源交叉 | 预期**不由被测代码算出** |
| **独立·弱** | `contract` API 契约值 · `driversrc` **转录自被测驱动实现** | 判别力真实但弱 —— 见纪律 2 |
| **非独立** | `samecode` 用被测代码自己的公式反解/回读复算 | 恒判 `PSEUDO_SAME_SOURCE`，**不计入覆盖** |
| **无预期** | `!none`（+ 理由） | 显式标注的**覆盖缺口**，不是"已验证无需预期" |

伪独立三闸（任一命中即不计入"有独立预期"）：① **标注闸** `samecode`；② **关键词闸**（说明或「预期来源」列含 `公式反推`/`复算`/`同源`/`回读驱动`/`驱动自身公式`）；③ **判别力闸（会真跑）** —— 拿一组错值探针（0/±1/边界/极大/极小/字符串噪声）灌进断言谓词，**若无一被拒** ⇒ 该断言恒真 ⇒ `PSEUDO_TAUTOLOGY`（抓 `in [0,2^64-1]`、正则 `.*` 这类"宽到永不为假"）。

**④ 两条纪律**（本节价值所在）

1. **只有预期能落到单个标量上才登记断言**。多子判据的合取、非标量（"读回字节 == pattern"、"三次返回值全匹配"）、无门限的量 ⇒ **一律 `!none` 并写清理由**。**不得"编预期"**：把实测值、或被测代码自己的输出抄进断言，不是独立证据（正是 `samecode`/`PSEUDO` 要挡的）。
2. **弱预期只证"没变"、不证"仍对"**。`driversrc` 的预期值**转录自被测驱动实现本身**（如 `sys_write32(0x11f, CR1)` ⇒ 断言 `CR1==0x11f`）：它有判别力（驱动写值一变就 DISAGREE），但**结构上发现不了既有缺陷** —— 写预期时若驱动已经把值算错，断言就是把那个错值抄了一遍。故计入"有独立预期"但一律记 **weak**，报告里**单列计数**，不与 `handcalc`/`spec`/`golden` 混同。

**⑤ 用法**（纯离线，回归前后随时可跑；退出码非 0 = 存在假 PASS 风险）

```bash
python common/expect_check.py --case <工程>:<case>                       # 单 case（默认取最新会话归档）
python common/expect_check.py --case <工程>:<case> --session reports/<d> # 指定归档会话
python common/expect_check.py --audit                                    # 全库覆盖率审计
python common/expect_check.py --audit --json                             # 机器可读
```

**⑥ 判定标记**

| 标记 | 含义 |
|---|---|
| `AGREE` | 有独立预期且实测满足 ⇒ **这批 PASS 有独立支撑** |
| `DISAGREE` | 有独立预期但实测违反 ⇒ 高价值发现 |
| `NO_EXPECTATION` | 该项没有独立预期（含显式 `!none`）⇒ 覆盖缺口 |
| `NO_ACTUAL` | 有预期，但归档取不到断言用到的实测键（缺 INFO 通道 / 键名不符）⇒ 缺口；**"没数据" ≠ "违规"**，不判 DISAGREE |
| `PSEUDO_*` | 有"预期"但**永远不可能 DISAGREE** ⇒ 伪独立，**不计入覆盖** |

两条**方向性**标记（比判定本身更该看）：

| 标记 | 形态 | 含义 |
|---|---|---|
| ★ **假 PASS 风险** | `DISAGREE` + 固件判 `PASS` | 实测违反外部真源却判过 ⇒ **这个 PASS 不可信，必须停下报告**（CLI 退非 0） |
| ○ 独立佐证 | `DISAGREE` + 固件判 `FAIL` | 与固件同向 ⇒ 对**已知缺陷**的独立佐证，不是新发现 |
| （信息） | `AGREE` + 固件判 `FAIL` | 断言只覆盖了本项**一部分**判据（INFO 常只报前缀阶段），不构成矛盾 |

**⑦ 已知缺口**：核限定 `core::key` 未启用（双核只能写"两核都应满足"的预期）；`NO_ACTUAL`
（无 INFO 通道）与显式 `!none` 仍是主要覆盖缺口 —— 全库 407 项中显式 `!none` 有 255 项
（见 `--audit` 输出）。

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
| 会话日志 | `tools/flash_program/flash-programer.log`（随 FP_EXE 所在目录） | 追加写，按偏移 diff 会话（KNOWN_ISSUES #13）；打包时已清空旧日志 |
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
| **XIP 标准组合（默认 XIP 场景）** | flash `0x08000000`（XIP 变体构建，XIP 执行 + boot 引导） | flash `0x08100000`（**标准 2os 构建的 RAM 固件**，由 cpu1 boot 搬运启动） | cpu1 用 `@2os_xip` 构建、cpu2 用 `@2os` 构建；两个 bin 一次会话烧进 flash（KNOWN_ISSUES #14），`--reset-and-run` 复位后 cpu1 XIP boot 统一引导 |
| 全 RAM（单核或双核，默认测试） | RAM（`load` ELF@0x100c0000） | RAM（`restore bin@0x10000000`） | 不烧 flash，直接 CKLink GDB load（现有两遍烧录法） |

要点：
- **没有"一个核烧 flash、另一个核 GDB load RAM"的混合模式**：涉及 XIP 就全烧 flash（cpu2 烧的是标准 2os 构建的 RAM 固件 bin，§6.7.4）；纯 RAM 才走 CKLink
- **cpu2 的标准 2os 构建也会产出 `zephyr_flash_*.bin`**（flash 段镜像），烧录就用它，不是 zephyr.bin
- **★ 2os bootrom 只引导 cpu1 —— 烧进 flash ≠ 会自动起（2026-09-20，G-HW5-4）**：cpu2 镜像须**先
  staging 进 RAM**（GDB `restore` 或 cpu1 的 boot 搬运代码）。cpu1 build 若
  `# CONFIG_CPU2_IMAGE_HEADER is not set`，`boot_cpu2()` 只 `app_cpu_dereset_by_addr(
  CONFIG_CPU2_BOOT_ADDR)`、**不搬运镜像** ⇒ cpu2 RAM 里是上次残留（GDB 读 `0x10000000` 的
  ELF magic / `e_entry` 与本次 cpu2 产物 `objdump` 对不上）。**判别顺序**：cpu2 串口全静默时先
  核 `0x10000000` 的镜像归属，再判"固件挂死"。实例：`app_iwdg_cpu2_test case_reset` 的 flash 流
  （KNOWN_ISSUES #102 / qsh-debug `ref-flash-variants.md` §2）。
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
- 判定为双通道（CLI 退出码 + stdout/日志关键字），实测校准见 `KNOWN_ISSUES.md` 追加
- 烧录前 `flash_program.py` 检测 DebugServer/gdb 进程并警告（KNOWN_ISSUES #2 互斥），`--kill-debugserver` 强制清理

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

---

### 6.8 Supervisor 运营规范

> Supervisor agent 的详细执行规则。agent profile（定位/铁律/路由表）见
> `810_driver_test/.claude/agents/runner.md`。

#### 6.8.1 策略详细实现

**STRATEGY_TEST（自动化回归）**
```text
1. 读 cases.yaml 获取 case 清单
2. 查 state → 过滤已完成 case
3. ★ 手动环境预检（qsh-debug §2.4 六项：DebugServer 进程/1025 就绪/GDB 连通/串口数/冲突/残留，全过才继续；不得委托 run_all.py 内置预检）
   - **与 E-2 每 case 前置检查的分工（2026-09-21）**：手动预检 = **批次入口门**（含"GDB 真连通"的整链验证，且必须人/agent 手动做）；E-2 的每 case 检查 = **批内兜底**（只用无副作用的 netstat 判活，覆盖"批次中途掉线"）。**两者不可互相替代** —— 预检过后仍可能在跑批中途掉线，这正是 E-2 要修的缺口（§6.2.3 第 6 条）。
4. 启动 run_all.py（background 模式，run_in_background=true）
5. 心跳监控循环（每 60s 查日志增长 + summary 行数；日志超过 case_timeout 无增长判卡死，按 runner.md「心跳监控」节判定表恢复；load/prime 期不计静默）
6. 全部 case 完成后：读取 summary → 失败分类（见 §6.8.2）→ 恢复或跳过
7. 汇总总表（见 §6.8.4）
```

> **`--dry-run` 不能替代哨兵审计（★ 2026-09-18）**：`run_all.py --dry-run` 在 `main()` 的 `args.dry_run` 处即 `return 0`（`:425`），而 `audit_stale_elves()` 在 `_run_all()` 内（`:495`，**全片擦除之后**）⇒ **`--dry-run` 看不到哨兵清单**。想核旧镜像哨兵只能实际跑 run_all（含全片擦除）或直接 `import` 调 `audit_stale_elves(read_cases())`（§6.4.5 / KNOWN_ISSUES #88）。
> **⚠ 且 `audit_stale_elves()` 只据"哨兵符号是否存在"判新旧**（★ 2026-09-20，G-FAMFILL-1）：**"含哨兵但源码已改"的陈旧 ELF 不在它清单内**，会被 `--skip-build` 静默复用 ⇒ 假 FAIL / 假 PASS。核产物新鲜度须比对 build mtime vs 源 mtime，不能只数符号（KNOWN_ISSUES #88 姊妹形态 / §6.2.2）。

**STRATEGY_DEBUG（单项调试）**
```text
1. 读现象描述，匹配 CLAUDE.md §5 速查表
2. 按 qsh-debug §0 总流程推进：
   a. 环境确认（§1-3）
   b. 复现抓现象（dbg_run.py --raw）
   c. 分层定位（embedded-debug §1）
   d. 断点抓现场（embedded-debug §2）
3. 每步检查：是否命中已知坑（KNOWN_ISSUES.md）
4. 定位闭环 → 验证 → 写问题总结
5. 3 轮定位未闭环 → 升级（报告已有线索 + 需要什么信息）
```

**STRATEGY_DEV（代码开发）**
```text
1. 理解需求 → 拆分子任务
2. 逐子任务：
   a. 查现有实现（避免重复）
   b. 实现
   c. 构建验证（west build）
   d. 构建失败 → 读错误 → 修复 → 重试（最多 2 次）
   e. 更新 state
3. 全部子任务完成 → 整体验证
```

**STRATEGY_ANALYZE（问题分析）**
```text
1. 明确问题
2. 收集证据（日志/代码/现象）
3. 形成假设列表（至少 2 个）
4. 逐假设验证：查 debug-knowledge + KNOWN_ISSUES.md + 代码/日志分析
5. 排除不可能 → 收敛到最可能原因
6. 报告结论 + 验证方法
```

**STRATEGY_MULTI（混合多步）**
```text
1. 拆分子任务，每个子任务独立识别类型 → 路由到对应策略
2. 串行执行（有依赖时）或标记可并行
3. 每个子任务独立的 state 跟踪
4. 子任务失败不阻断兄弟任务
```

#### 6.8.2 失败分类表

收到执行结果后，按以下顺序匹配：

```text
1. 环境类（优先检查）
   特征: "CKLINK"/"cannot open"/"timeout"/"no banner"/"连接残留"/"DebugServer"
   恢复: clean_gdb_residue → 重启 DebugServer(5s) → 重试
   最大: 2 次

2. 超时类
   特征: 进程超过 5 分钟无输出 / subprocess.TimeoutExpired
        （5 分钟为事后分类外层兜底；实时看门狗按 case_timeout 判定，见 runner.md「心跳监控」节）
   恢复: 杀进程 → 清理 → 重试（缩短 timeout）
   最大: 1 次

3. 构建类
   特征: "error:"/"undefined reference"/"fatal"/编译退出码非0
   恢复: 读最后 50 行错误 → 查 KNOWN_ISSUES.md 匹配 →
         已知坑 → 按坑修复（如删 build 重建）→ 重试
         未知 → 跳过，记 ESCALATE
   最大: 1 次（可修复时）

4. 逻辑类（测试 FAIL）
   特征: 测试断言失败 / FAIL token / execution=FAILED
   恢复: 读 FAIL 行 + reason → 查已知坑 →
         命中已知坑 → 按坑修复 → 重跑
         未命中 → 记录详情，标记 FAIL，继续
   最大: 1 次

5. 未知类
   特征: 以上都不匹配
   恢复: 记录完整输出（最后 100 行）→ ESCALATE
   最大: 0 次
```

#### 6.8.3 重试纪律与进度持久化

**重试纪律**：
- 每个 item 最多重试 2 次（环境类）或 1 次（其他类）
- 重试前必须说明：上次失败原因 + 本次恢复动作
- 重试时恢复动作与上次相同 → 不重试，直接 ESCALATE
- 连续 3 个 item ESCALATE → 暂停循环，报告整体环境可能有问题

**进度持久化**（每个 item 完成后立即更新 state 文件）：

```python
state["items"][item_id] = {
    "status": "PASS|FAIL|SKIP|TIMEOUT|ESCALATE",
    "retries": N,
    "error": "最后一次错误摘要（≤200字）",
    "action": "RETRY|SKIP|ESCALATE",
    "updated": "ISO timestamp"
}
state["last_activity"] = now()
```

#### 6.8.4 汇总报告模板

```text
[SUPERVISOR] 任务完成报告
═══════════════════════════════════════
任务: <描述>
策略: <STRATEGY_XXX>
耗时: <开始> → <结束>

结果:
  ✅ PASS:      N 个
  ❌ FAIL:      N 个
  ⏭️ SKIP:      N 个
  ⏰ TIMEOUT:   N 个
  🔺 ESCALATE:  N 个

详情:
  ✅ case_trng          PASS (0 retries)
  ❌ case_i3c_12m5      FAIL → ESCALATE (2 retries, "CTRL no banner")
  ...

需要人工处理:
  1. case_i3c_12m5: controller 核无 banner 输出，怀疑硬件接线

State 已保存: <项目根>/runner_state.json
═══════════════════════════════════════
```

#### 6.8.5 断点续跑

启动时检查 state 文件：

```text
if runner_state.json 存在:
  读取 state
  if state.task 与当前任务匹配:
    过滤 items 中 status 为 PASS 的 → 跳过
    对 status 为 FAIL 且 retries < max 的 → 加入待办
    对 status 为 ESCALATE 的 → 跳过（已升级，不重复）
    对 status 为 PENDING 的 → 加入待办
    输出: "[SUPERVISOR] 从断点续跑: 已完成 X/Y，剩余 Z 个"
  else:
    新建 state
else:
  新建 state
```

### 6.9 固件测试日志标准（统一 token 契约 + 双核单出口，★ 2026-09-20 A3）

**用户裁定**：固件日志「**全部统一**」；双核中不适合输出 token 的一核可**经 mbox 把结果送另一核，
由单核统一输出**；**测试结果必须以 token 为判断标准**，读 mem 只用于驱动调试。

#### 6.9.1 标准的定义域（先划定"统一"统的是什么）

统一 **语义**，不追求**字符拼写**统一。统一项 = 四类标记的语义 + **唯一解析器**：

| 类别 | 语义 | 表层拼写（两种，均由 `parse_ztest` 归一） |
|---|---|---|
| 引导 | 本核起来了 | `ZT_BOOT <core>`（D1）/ `<PREFIX>:<core>:BOOT`（D2） |
| 逐项结果 | `(core, name, state, reason)` | ` PASS\|FAIL\|SKIP - <name> in X.XXX seconds`（D1）/ `<PREFIX>:<core>:(PASS\|FAIL\|SKIP):<name>[:reason]`（D2） |
| INFO | 数值上报 `(name, k=v…)` | `INFO <name>: k=v[,k=v]`（D1）/ `<PREFIX>:<core>:INFO:<name>:k=v[,…]`（D2） |
| 收尾 | 本核结论 | `PROJECT EXECUTION SUCCESSFUL\|FAILED`（D1、D2 自打） / `<PREFIX>:<core>:DONE:<PASS\|FAIL>`（D2 备选） |

`<core>` ∈ `CTRL` \| `TGT`；`<PREFIX>` ∈ **登记前缀白名单**（当前 `I3C_I2C_TEST`、`RTC_TEST`，
拟增 `MBOX_TEST`）；`<name>` 必须与 `ZTEST(suite, name)` 注册名或 `dump_results.py` 的 `TEST_NAMES` 一致。

**为什么允许两种拼写**：D1 由 ztest 框架产出，改写它意味着拆掉 ztest 注册、
失去 `Assertion failed at …` reason（§6.5.5）、`SKIP` 语义、per-suite stats 与 `zt_run_all` 自动记账（§6.1.5），
并作废 §6.2.4 冻结表与 §6.4.9 的三档离线证据。收益仅为"字符串相同"，**代价不可接受** ⇒
标准落在"可机器解析的语义 + 唯一解析器"上。D2 供**不注册 ztest 的命令通道型固件**使用（同一文法，不同前缀）。

#### 6.9.2 与 `parse_ztest` 的关系（唯一解析器）

- **唯一入口**：`common/ztest_report.py::parse_ztest`。**禁止新增第二个 token 解析器**。
- D1 已由 `_RESULT_RE` / `_INFO_RE` / `_EXEC_RE` / `_ASSERT_RE` 消费；D2 已由 `_NATIVE_RESULT_RE` /
  `_NATIVE_INFO_RE` 消费（当前硬编码 `I3C_I2C_TEST`）。
- **不冲突**：`_NATIVE_*` 在 `_RESULT_RE` 之前试匹配、命中即 `continue`（`ztest_report.py:85-99`）；
  判别式含 `:(CTRL|TGT):` + 完整前缀 ⇒ 两种拼写互不误吞。
- **唯一需新增的框架改动**：把 `_NATIVE_RESULT_RE` / `_NATIVE_INFO_RE` 的前缀从硬编码推广成
  **登记白名单**（`I3C_I2C_TEST|RTC_TEST|MBOX_TEST`）。这是 `common/ztest_report.py` 的纯 L2 改动，
  按 §6.4.9 **可离线证**（147 份真实归档 + 240 个合成输入），**不需要上板**。
- **判定必须走结果 token，不得挤在 INFO 上**：`INFO` 的语义是数值上报，不承载判定
  （用户裁定"结果必须以 token 为判断标准"）。现状违反者 = `mbox_test`（`INFO <name>: pass=<0\|1>` 承载判定）。

#### 6.9.3 双核单出口协议（`RLOG`，从核 → 主核 → 单串口）

**适用面**：**仅当从核为命令通道型（不注册 ztest）**。ztest 型从核的价值在原生 reason 与 stats 记账，
经 mbox 搬运等于重实现 ztest，收益为零 ⇒ ztest 型从核**继续本地输出**（现状即最优）。
⚠️ 被测对象本身是 mbox 的工程（`mbox_test`）**排除**——用被测通道传结果会自我污染。

**角色**：`PRIMARY`（有 UART 出口，负责统一输出）/ `SECONDARY`（不产本地 token）。

**传输**：专用 mbox 通道（建议 ch6，避开板级 ch0–3 与 `mbox_test` ch4/ch5；**通道号需实测确认**）。
载荷上限 = 通道 MTU（参考 `mbox_test` 的 64 B）。

```c
/* 统一结果上报帧（小端；magic 与 mbox_test 的 0xABCD/0xABCE 区分） */
struct rlog_frame {
    uint8_t magic[2];    /* 'R','L' */
    uint8_t ver;         /* =1 */
    uint8_t type;        /* 1=HELLO 2=BOOT 3=ITEM 4=INFO 5=DONE */
    uint8_t core;        /* 0=CTRL 1=TGT */
    uint8_t seq;         /* 每核单调 +1，回绕 255→0 */
    uint8_t item_id;     /* 项序号（与 TEST_NAMES / 枚举序对齐） */
    uint8_t state;       /* 0=PASS 1=FAIL 2=SKIP 3=NOT_RUN */
    uint8_t reason_len;  /* 0..RLOG_REASON_MAX(=96，与 TR_REASON_LEN 对齐) */
    uint8_t payload[];   /* reason 文本 / k=v 文本 / DONE 结论 */
};
```

- **长度上限**：单帧 payload ≤ MTU−10。`reason` 上限取 `RLOG_REASON_MAX=96`（与
  `test_results.h::TR_REASON_LEN` 对齐）⇒ 需 **≤2 片**（`frag_idx`/`frag_cnt` 各 1 B，
  占用 `item_id`/`state` 之外的保留字节）。**推荐先实现"单帧 + 超长截断 + `…`"**，
  分片仅当实测 reason 常超限时才启用（降低失败面）。
- **握手 / 序号 / 丢失**：
  - PRIMARY 广播 `HELLO`；SECONDARY 回 `HELLO`（带 `item_total`）。未收到 HELLO 前 PRIMARY 处于 `WAIT_SECONDARY`。
  - 每帧带 `seq` + 校验（CRC16 或 `mbox_test_crc32` 同源实现）。**跳号或校验失败 ⇒ 计入丢失 ⇒ fail-closed**。
- **超时**：PRIMARY 套件结束后最多等 `RLOG_TX_TIMEOUT_MS`（建议 2000 ms，**需实测校准**）收 `DONE`。
- **谁输出 / 顺序**：
  - PRIMARY **不实时转发**（避免两核行交错与 UART 抢占）；收齐或超时后**一次性**打印。
  - 顺序 = **从核行在前、主核行在后**。
  - ⚠️ **硬约束**：`PROJECT EXECUTION` / `DONE` **必须是整条流最后一个标记** ——
    harness 的完成判据是"静默 + 已见完成标记"（§6.2.3 第 3 条），标记提前出现会让 gdb 提前被杀、从核结果被截断。
- **fail-closed 三态（禁止静默当成功）**：

  | 情形 | 判据 | 处置 |
  |---|---|---|
  | 从核没送 | 未收到任何 SECONDARY 帧 | case **FAIL**，reason=`SECONDARY_SILENT` |
  | 送一半 | `seq` 跳号 / 校验失败 / 帧数 < 声明数 / 无 `DONE` | case **FAIL**，reason=`SECONDARY_INCOMPLETE` |
  | 主核没收到 | 等待超时 | case **FAIL**，reason=`SECONDARY_RESULT_TIMEOUT` |
  | 从核自身判负 | `DONE:FAIL` | case **FAIL**（常规路径） |

#### 6.9.4 ⚠️ 对"按核归档重放"的影响与保底

- **现状**（§6.4.7 硬约束）：重放输入 = `{核名: [原始行 str,…]}`，两核行**物理隔离**在
  `serial_CTRL.log` / `serial_TGT.log`；合并成一份会破坏 `parse_ztest` 的核归属 ⇒ 无法按核重放。
- **单出口后输入形态变成什么**：从核 token 只出现在**主核串口流**里 ⇒
  ① `serial_TGT.log` 空 ⇒ 重放时从核零 token ⇒ 命中 **D6/G9 被判 FAIL（误判）**；
  ② 若整份塞进 `serial_CTRL.log`，`parse_ztest` 的 `role` 是**单值**（后匹配覆盖前匹配）⇒ 核归属错乱。
- **保底（推荐）——归档侧按来源分拣**：主核把从核的 **D2 同构 token 行原样转发打印**；
  L1 采集后按 `<PREFIX>:<core>:` 前缀**分拣回 `serial_TGT.log`**，`serial_CTRL.log` 只留主核自己的行。
  ⇒ 重放输入形态**逐字不变**，§6.4.7 / §6.4.9 全部结论继续成立。
  代价：`common/serial_archive.py` 的 L0 契约增"分拣"能力 ⇒ `ARCHIVE_VERSION` 升版（1→2），
  `replay_meta.json` 增 `"split_from_primary": true`。
- **硬约束**：同一条从核行**只能出现在一个归档文件里**（否则重放重复计项）⇒
  主核的**人读汇总块不得带 `<PREFIX>:<core>:` 前缀**，或归档时按前缀剔除。
- **替代保底（不动 L0）**：从核**本地也打一份**同样 token（双出口），"单出口"降级为"单份人读汇总"。
  零框架改动、零重放破坏，代价是放弃"单出口"的字面目标。
- **⇒ 二选一需用户裁定**；推荐"分拣"，只有它同时满足单出口与可重放。
  （**已于 B2 落地 = 分拣**，见 §6.9.8：`acquire.split_relay_lines` + `ARCHIVE_VERSION_SPLIT=2`。）
- **本仓适用性**：唯一候选 `i3c_i2c_switch_test`（其 TGT 非 ztest）。当前两核 UART 均可用，
  改用单出口**无功能收益**、只增重放复杂度 ⇒ **建议暂不启用，协议先定义备用**。

#### 6.9.5 迁移清单（工单粒度；详版见 `tmp/a3/FIRMWARE_LOG_STANDARD.md`）

| 级别 | 工程 | 缺口 | 改动 | 量 | 重跑板子 |
|---|---|---|---|---|---|
| **A 合规（22）** | 21 个 ztest 工程 + `i3c_i2c_switch_test` | 无（D1 / D2 + BOOT + 收尾齐，走 `parse_ztest`） | **不动**（改了反而多余） | 0 | 否 |
| **B 只差一点（1）** | `rtc_ls_test` | 唯一不调 `zt_init` ⇒ **无 `ZT_BOOT`**；私有 parser | `cpu1_rtc_test/src/main.c::test_main` 首行加 `zt_init("TGT");` | **+1 行** | 是（1 工程） |
| **C 要动结构（1）** | `mbox_test` | 收尾 `PROJECT EXECUTION:` **带冒号**（`_EXEC_RE` 不匹配）；判定挤在 `INFO … pass=` 上 | ① `tests/src/mbox_test_report.c::mbox_print_verdict` 去冒号（1 行）；② 逐项改走 D2 结果 token（固件 ~15 行）；③ parser 收敛（A2/L2 域） | ~16 行 | 是（1 工程） |
| **框架（离线可证）** | — | 前缀白名单化 | `common/ztest_report.py` `_NATIVE_*` 前缀推广 | ~5 行 | **否**（§6.4.9） |

- ⚠️ C 级动 `mbox_test` 的 wire 格式**前需用户明确点头**：`§6.2.4 M1`（冻结）与
  `§6.4.2`（明文"禁止统一其报告模板"）在边界上，本步只动格式、不动判定口径。
- `rtc_ls_test` 的 token 宏形态**已符合 D2 文法** ⇒ 只需进白名单即可被 `parse_ztest` 认领，**固件零改动**亦可统一文法；
  若要 `ZT_BOOT`（BOOT 判据 / §6.2.3 门禁）才需那 1 行。

#### 6.9.6 推荐：最小改动路径（反对"20 个工程全改"）

**A 级 22 个工程本就合规 ⇒ 不重跑。** 只改：`rtc_ls_test`（1 行）+ `mbox_test`（~16 行）+
`common/ztest_report.py`（~5 行，离线可证）。**这仍满足"全部统一"** ——
统一的是"语义 + 唯一解析器"；20 个工程的 ztest 原生行**本来就是 token**，
改写它们不产生任何新能力，却要付出"废掉 ztest 断言/记账 + 全量重跑 + 触碰 §6.2.4 冻结表"的代价。
若坚持**逐字**单一前缀，代价：20 工程固件重写 + 24 工程全量重跑 + §6.2.4 全面重审 + §6.4.9 离线证据作废（**不建议**）。

#### 6.9.7 需要确认

> **落地状态（B2）**：第 1/4/6 条已由 §6.9.8 落地（分拣保底 + mbox_test 文法统一 + switch 启用单出口）；
> 第 2/3 条已由 §6.9.8 实测回答（通道号=consumer 遍历序号 4；`RLOG_TX_TIMEOUT_MS=2000` 足够）；
> 第 5 条（`rtc_ls_test` 私有 parser 收敛）见 §6.9.8「文法统一」行。以下保留为提问当时的原始清单。

1. 单出口 vs 可重放：§6.9.4 的"归档分拣（L0 升版）"与"双出口保底"二选一，需用户裁定。
2. mbox 专用通道号（建议 ch6）**未实测**；`mbox_max_channels_get()` 确认。
3. `RLOG_TX_TIMEOUT_MS`（建议 2000 ms）**未实测校准**。
4. `mbox_test` 是否纳入统一（触 §6.2.4 M1 / §6.4.2 边界）需用户点头。
5. `rtc_ls_test` 私有 parser 是否收敛到 `parse_ztest`（属 A2/L2 域，本任务未动）。
6. `i3c_i2c_switch_test` 是否启用单出口（建议暂不启用）。

#### 6.9.8 落地登记（B2，2026-09-20）：文法统一 + 严格单出口

**§6.9.7 的未验证项已实测确认以下两条**（其余各条状态见下表）：

| 原未验证项 | 实测结论 |
|---|---|
| mbox 专用通道号（建议 ch6） | **"通道号"不是硬件常量**：驱动 `MBOX_NCHANNELS = DT_NUM_INST_STATUS_OKAY(vnd,mbox-consumer)`，`mbox_dt_spec.channel_id` 取 DTS `mboxes` 单元格，直接当 `fifo[slot][channel]` 的**下标** ⇒ **通道号必须等于该 consumer 节点在 `&mbox` 子节点中的遍历序号**。板级占 0–3，本工程追加第 5 个 consumer ⇒ **id = 4**（不是 6）。两核必须声明在相同序号上。 |
| `RLOG_TX_TIMEOUT_MS`（建议 2000 ms） | **2000 ms 足够**：从核 `'P'` 之后一次性送 SUMMARY（11 项）+ DONE 共 ~14 帧，实测 ~100 ms 内到齐；主核在等待循环里边等边 `drain`（给从核 FIFO 腾空间），无丢帧（`rlog_lost()==0`）。 |

**文法统一（§6.9.5 最小路径）落地**：

| 落点 | 改动 | 证据 |
|---|---|---|
| `common/ztest_report.py` | `_NATIVE_RESULT_RE` / `_NATIVE_INFO_RE` 前缀从硬编码改**登记白名单** `NATIVE_PREFIXES = (I3C_I2C_TEST, RTC_TEST, MBOX_TEST)` | 314 份真/合成归档 A/B：差异**只有** `RTC_TEST` 行被新认领，零丢失、其余逐字不变（`tmp/b2/ab_prefix.py`） |
| `projects/rtc_ls_test/cpu1_rtc_test` | `main.c` 首行 `zt_init("TGT")`；`CMakeLists.txt` 补编 `common/ztest_glue.c`（该工程原先**刻意不编**，故为全仓最后一个无 `ZT_BOOT` 者） | 上板 15/15 PASS，归档出现 `ZT_BOOT TGT` |
| `projects/mbox_test` | 逐项改走 D2 结果 token `MBOX_TEST:<core>:(PASS\|FAIL):<name>[:<明细>]`；收尾去冒号；observe/guard 留在 INFO 类别；`tests/run_test.py` 同步 | 上板 `case_mbox_basic` 5/5、`case_mbox_invalid` 7/7（observe/guard 正常呈现） |

**严格单出口（§6.9.3/§6.9.4）落地 —— 适用面按 §6.9.3 取"命令通道型从核"**：

| 落点 | 改动 |
|---|---|
| `common/rlog.{h,c}`（新） | RLOG 帧（63 B ≤ 通道 width 64）：`magic/ver/type/core/seq/len/verdict/text`；从核发送（FIFO 满重试 + 超长截断标 `...`）；主核 ISR **只入环形缓冲**、thread 上下文 `rlog_relay_drain()` 打印；跳号/坏帧/缓冲满计入 `rlog_lost()` |
| `projects/i3c_i2c_switch_test`（两核） | 协议头输出宏经 `I3C_I2C_OUT` 分流：从核（`I3C_I2C_SINGLE_EGRESS=1`）走 RLOG、主核走 printk；从核 `main.c` 的 `ZT_BOOT` 与收尾 `PROJECT EXECUTION` 一并改走 RLOG（收尾走 **DONE 帧**，是整条流最后一条）；主核 `test_main` 注册接收 + 在 `ctrl_test_begin/end` 与末尾等待期 drain + `rlog_wait_secondary_done()` **fail-closed** |
| `common/acquire.py` | `split_relay_lines()` + `DualRunSpec.relay_prefixes`：按 `<PREFIX>:<core>:` 把主核流里的从核行分拣回从核列表（**无损划分**，实测 0 丢 0 增） |
| `common/serial_archive.py` | `ARCHIVE_VERSION_SPLIT = 2` + `replay_meta.json.split_from_primary`（**仅分拣时升版**，其余工程元信息逐字不变） |

**两条连带修正**（均属本工程，非判定语义）：`test_main` 返回会进 ztest 强 `main()` 的 `TC_END_REPORT`（`tc_util.h:58/227`）在本核补打 `====` + 一条本地 `PROJECT EXECUTION` —— 既破坏"完成标记是整条流最后一个"（§6.9.3 硬约束）又触发 R6 相反标记并存；两核按 CLAUDE.md §4.1 补 `k_sleep(K_FOREVER)`。`run_test.py` 的 `date_str` 在 session lambda 之后赋值（A2 迁移遗留）⇒ 不带 `--session-dir` 时 `NameError`，已前置。

**未做 / 待办**（如实登记）：

1. **其余 4 个双核工程未启用单出口** —— 按 §6.9.3 的适用面，ztest 型从核（`i3c_dualcore_test` / `i2c_pure_test` / `app_iwdg_cpu2_test`）**继续本地输出**（经 mbox 搬运等于重实现 ztest，收益为零）；`mbox_test` 的被测对象就是 mbox 本身（用被测通道传结果会自我污染）⇒ **排除**。本仓唯一适用者 `i3c_i2c_switch_test` 已落地。
   > **已被 §6.11.6（B2-impl）推翻的 ztest 型部分**：`i3c_dualcore_test` / `i2c_pure_test` /
   > `app_iwdg_cpu2_test` 三者经 `tc_util_user_override.h` 覆盖 `PRINT_DATA` 后**也已启用单出口**，
   > 不再是"继续本地输出"；`mbox_test` 仍排除。适用面的最新口径见 §6.11.5 / §6.11.6。
2. **`mbox_test` 的报告模板例外保持不动**（§6.4.2）。
3. **已知危害**：主核的 `printk`（转发）与 deferred log 线程在 UART 上非原子交错，偶发把紧邻 token 的 ANSI 转义**截断**，`ANSI_RE` 会连带吞掉 token 首字母（实测 `\x1b[1;3I3C_I2C_TEST:…` ⇒ 吃掉 `I`）。该 CTRL 项由 §6.4.3 内存兜底补齐（判定不变），但 `src` 由 `token` 变 `mem`。彻底修需控制台级互斥（超出本步写域）。

### 6.10 L1 补丁契约与 INFO kv 解析（B1，2026-09-20）

#### 6.10.1 离线重放的补丁目标 = `common/acquire.py` 的**模块全局名**

`regression/replay.py` 靠"给 L1 采集层打替身"实现离线重放。补丁**必须**打在
`acquire.<名>` 上（`acquire.run_case_shell` / `capture_case` / `run_case_dual` /
`run_case_mbox` / `run_pass_iwdg` / `SerialCapture` / `memory_backfill` / `dump_mem`），
**不能**打在 `run_test.py` 模块上：A1 收敛 L2（`common/process.py`）+ A2 抽出 L1
之后，24 份薄壳只剩 `acquire.<策略>(spec)`，`mod.run_case` 之类的名字**已不存在**。

* **失效模式**：补丁打空 ⇒ 重放**静默落到真硬件**（占板子 / 起 DebugServer）。
* **隔离网**：每个适配器先 `_guard_acquire()` 把 `acquire` 上所有能碰板子/进程的
  入口换成替身（置空或硬中止 `HardwareTouchAttempt`），再覆盖本策略要用的采集入口。
  未预期触达 ⇒ **抛异常中止整批**（不静默跳过）。
* **判据**：`tmp/b1/guard_demo.py`（逐策略：本策略入口=替身、其余 4 个采集入口=绊线；
  烧录/构建/起 gdb/起 test.sh/开串口/pyserial 全部拦截）；`tmp/b1/audit_patch_targets.py`
  （补丁目标必须都是 `acquire` 的**既有**全局名）。

#### 6.10.2 INFO kv 解析唯一规则：`common/ztest_report.parse_kv`

INFO 值串的分隔符**同时**容忍逗号与空白（`rtc_ls` 用逗号；`io_probe` / `spid`
用空格）。逐段判定：先按 `,` 切段，仅当某段第一个 `=` 之后仍出现 `word=` 时才把该段
按空白继续切 —— 这样既不让 `pins=75 not_controlled=2` 的第二个键被吞，
也不把 `id=ab cd ef`（空格属**值的一部分**）切短。

* 唯一实现：`common/ztest_report.parse_kv`。`common/expect_check.expand_kv` 已收敛到它
  （禁止再留第二套规则）。
* 判定/渲染是 L2 路径 ⇒ 改动须按 §6.4.9 用真归档重放验证：**只有原本被吞掉的键新出现**，
  其余零变化（证据：`tmp/b1/kv_before_after.py`、`tmp/b1/io_probe_ab.py`）。
* INFO 的**名字**必须是字符串字面量（`ZT_INFO("rsa3072_modexp", …)`），
  不得把格式串当名字（`ZT_INFO("rsa%d_modexp", …)` 会让两个测试项**撞同一个键**，
  后写覆盖前写 ⇒ 两项在报告里恒为 `-`）。跨测试项共用一个 helper 时，
  按 `mbedtls_test/src/ecc_test.c::ecdsa_curve_once(ztest_name, …)` 的既有写法，
  把名字作为参数传入、在 `ZTEST` 处写字面量。

### 6.11 B2-fix：管道/转义/新鲜度/烧录窗口（2026-09-20）

#### 6.11.1 stdout=PIPE 必须从 Popen 那刻起持续排空（同族第 4 处）

判据：**凡是 `Popen(stdout=PIPE)`，就必须在同一个函数里有一条 drain 线程**，
收尾用 `proc.wait()`，**不得**用 `communicate()` 驱动（它会 close stdin，见
KNOWN_ISSUES #101）。已排空清单（`ast` 全仓扫描，`common/**` + `regression/**`
+ `projects/*/tests/**`，共 76 处 PIPE 调用）：`common/acquire.py`
（`spawn_test_run` / `gdb_flash_once` / `gdb_watch_session` / `run_pass_iwdg`）、
各 `tests/scripts/dbg_run.py`、`dump_results.py`。

* **本轮修的一处**：`regression/precheck/run_precheck.py::_gdb_session_b_proc` ——
  `restore` 的分段进度整段打 stdout，采集窗内无人读 ⇒ 管道写满 ⇒ gdb 阻塞在写
  stdout ⇒ DebugServer 会话停摆。改为 Popen 处起 drain 线程 + 收尾 `proc.wait()`。
* **非 live 遗留**（`tests/tmp/`，不参与回归）：`flash_test/tests/tmp/run_capture.py`、
  `otbn_test/tests/tmp/probe_pc.py` —— 保留原样，不再维护。
* **可执行防线**（防第 5 处）：`tmp/b2fix/scan_pipes.py` —— 扫出所有
  `stdout=PIPE` 且所在函数内**没有**任何 `.stdout` / `.communicate` 用法的调用点，
  非零退出视为缺陷。新增任何 `Popen` 后跑一次即可。

#### 6.11.2 串口是"两方非原子共享"：截断转义不得吞掉证据首字母

`CONFIG_LOG_MODE_DEFERRED=y` + `CONFIG_LOG_PRINTK=n` ⇒ 主核 `printk` 与 log 线程
的 UART 后端是**两个独立写入方**；且 `CONFIG_PRINTK_SYNC` 是
`default y if SMP && MP_MAX_NUM_CPUS > 1`，QSH 单核 SoC **不开** ⇒ 连两次 printk
之间也无互斥。实际交错（真归档 `reports/2026-09-20-11/…/serial_CTRL.log`）：

```
[00:00:00.005,000] \x1b[1;3I3C_I2C_TEST:CTRL:PASS:pinctrl_states
```

log 线程写到 `\x1b[1;3` 被 printk 切开、printk 整行插入、log 线程续写 `1m…`
⇒ `ANSI_RE` 的 `[A-Za-z]` 收尾把 token 首字母 `I` 当转义末字节吃掉 ⇒ 该项
查不到 ⇒ §6.4.3 内存兜底补齐 ⇒ **判定不变，`src` 由 `token` 降级为 `mem`**
（批量必须以 token 为准 ⇒ 等于证据降级）。

* **不是单出口引入的**（`_NATIVE_PREFIXES` 上方"不得加词边界"那条注释
  `…:00.005,0I3C_I2C_TEST:…` 就是单出口之前的同款疤）。
* **修在消费者唯一收口** `common/ztest_report.py::strip_line`：§"仅当原始行里有
  证据锚、严格剥离后却没了"时退回只剥 SGR（`_SGR_RE`）。纯增益 —— 锚点仍在或
  原始行本就无锚 ⇒ 逐字返回旧结果。
* **为什么不去改发送侧**：受害行**可以是主核自己的 token**（上面那条就是
  `:CTRL:PASS:`，不是转发行）⇒ 只改 `rlog.c` 的转发路径盖不住。控制台级互斥要动
  `zephyr/**`（log 后端写入不持 printk 的锁），超边界。**根因侧建议**：
  `CONFIG_LOG_BACKEND_SHOW_COLOR=n`（去掉转义即去掉放大器）或让两方共用一个写入者。
* 证据：`reports/2026-09-20-11/-12/-13` 三份归档离线 A/B（仅 2 行变化、
  `pinctrl_states` 由 MISSING 恢复为 PASS）+ 上板 `reports/2026-09-20-18`
  （CTRL/TGT 两核各命中一次截断，`srcs={'token':10,'mem':1}`，唯一 mem 是
  TGT 真未跑的 `broadcast_nack_recovery`）。

#### 6.11.3 产物新鲜度只看"真正链进该工程"的输入

`common/` 是公共目录但**不是每份都进每个工程**（`rlog.c` 只在
`i3c_i2c_switch_test` 的 CMakeLists 里 `target_sources`）。把整个 `common/**` 当
输入 ⇒ `common/` 每新增一个文件就让**所有**工程判"产物陈旧"⇒ 全线误报。
判据（`regression/run_all.py::_common_build_inputs` 与
`scripts/test_common.sh::_common_build_inputs` 逐字同构）：文件基名出现在
(本工程各 `CMakeLists.txt`) 或 (`common_sources.cmake`) 才算输入，头文件按**同干
`.c`** 被引用即算。取不到引用集 ⇒ 视为空（**宁可漏报也不误报**）。

#### 6.11.4 烧录窗口按"进度"判活，不用绝对秒数

`LOAD_WINDOW_S = 20` 原为**绝对**窗口；但烧录受 CKLink 限速（实测
`Transfer rate: 41 KB/sec`），最大镜像（`i3c_dualcore_test` target ELF 114 KB +
controller bin restore）实测 **17～50s** 才落地 ⇒ "烧录正常但慢"被判 `load_fail`，
`abort_on_load_fail` 直接中止整批。改判据为**基于进度**
（`common/acquire.py::_LoadWatch`）：`proc_lines` 仍在长 ⇒ 放行到 `case_timeout`
硬上限；**连续 `LOAD_WINDOW_S` 一点输出都没有**才判死。该进度信号依赖 drain 线程
（§6.11.1）—— 不排空的实现连"它是否在动"都看不见。

> ⚠️ **光有 drain 线程还不够，读法也是判据的一部分**：gdb 的 `load` 进度是 **`\r` 原地
> 刷新**（不产生换行）。若 drain 线程**按行读**（`for line in proc.stdout` / `readline`），
> 慢烧录期间 `proc_lines` **零增长** ⇒ `_LoadWatch` 仍会误判"连续无输出" ⇒ 复现同一个
> `load_fail` 中止。必须**按 `\r` / `\n` 双向切分**，且用 **`os.read(fd)`**
> （`read(n)` / `read1` 会攒够 n 字节才返回，同样看不到进度）。
> 实测：修前端到端 2/3（`i3c_dualcore_test` 假 FAIL），修后 3/3。
> 实现见 `common/acquire.py` 的 d3 reader。

#### 6.11.5 单出口适用范围（§6.9.3 补充裁定，2026-09-20）

`mbox_test` **接受排除**（被测对象就是 mbox，用被测通道传结果 = 自我污染，技术硬阻塞）。
`i3c_dualcore_test` / `i2c_pure_test` / `app_iwdg_cpu2_test` **可行性评估结论：不需
改 ztest**——Zephyr 上游为此留了覆盖点：`subsys/testsuite/include/zephyr/tc_util.h`
的 `#ifndef PRINT_DATA` + `CONFIG_ZTEST_TC_UTIL_USER_OVERRIDE`
（`subsys/testsuite/ztest/Kconfig`）⇒ 从核用 `tc_util_user_override.h` 把
`PRINT_DATA` 指向 `rlog_emitf` 即可（纯后端替换，ztest 源码零改动）。
**代价不在 ztest 而在 L0 分拣契约**：ztest 的 D1 行（` PASS - <name> …`）**没有**
`<PREFIX>:(CTRL|TGT):` 段 ⇒ `acquire.split_relay_lines` 无法把它们分拣回从核，
`serial_TGT.log` 会空、`tgt_done` 判据假 FAIL。需在覆盖头里带核信封前缀并登记进
`NATIVE_PREFIXES`。已知残余风险：ztest **连续**输出（35 项 ≈ 4.4 KB）与 switch
实测的"收尾一次性 ~14 帧"负载不同，`RLOG_RETRY_MS=2 × 200` 的背压是否够**须上板实测**。
建议先在 `i2c_pure_test`（1 项、无高速时序）试点。

#### 6.11.6 严格单出口推到 **ztest 型从核**（B2-impl，2026-09-20 落地）

§6.11.5 的可行性结论已实施：`i2c_pure_test`（试点）/ `app_iwdg_cpu2_test` /
`i3c_dualcore_test` 三者的 TGT 核**不再往自己的串口打 token**，逐项证据经 RLOG
交主核统一出口。`mbox_test` 仍排除（被测对象即 mbox，自我污染）。`zephyr/**` 未改。

**机制（ztest 源码零改动）**：`CONFIG_ZTEST_TC_UTIL_USER_OVERRIDE=y` ⇒ `tc_util.h`
在定义 `PRINT_DATA` 之前 include 工程自带的 `tc_util_user_override.h`，该头把
`PRINT_DATA` 指向 `rlog_emitf`。覆盖头必须对**全部** TU 可见（`ztest.c` 自己也用
`PRINT_DATA`）⇒ 只能用 `zephyr_include_directories()`（挂 `zephyr_interface`），
`target_include_directories(app …)` 会让 ztest 库编不过。

**三处偏离原设想（均已实测）**：

1. **核信封加在"中继侧"而不是从核文本里**。从核发**纯文本**，主核在
   `rlog_relay_drain()` 里按 `rlog_set_relay_envelope(prefix, core)` 加
   `<前缀>:<核>:`。理由：帧长上限（`RLOG_TEXT_MAX`）会被 14B 的信封挤掉，
   而 ztest 的逐项 D1 行本身就长 —— 信封挤进帧里必截断。信封加在唯一出口处，
   也正是"单出口"发生的地方。
2. **`RLOG_TEXT_MAX` 必须按工程抬高，否则逐项证据被静默吞掉**（本步踩到的真坑）：
   ` PASS - test_cpu1_does_not_own_app_iwdg in 0.001 seconds` = 56B > 默认 54B ⇒
   `rlog_emitf` 的 `vsnprintf` 把结尾的 `seconds` 截掉 ⇒ `_RESULT_RE` 认不出该行
   ⇒ **TGT 逐项全判"零证据 FAIL"**（实测 `case_quick_invalid` 由基线 3/4 PASS
   翻成 1/3 FAIL）。修法：`rlog.h` 的 `RLOG_TEXT_MAX` 改为**可覆盖**，长长的两个
   工程在 CMakeLists 里 `target_compile_definitions(app PRIVATE RLOG_TEXT_MAX=118)`
   并把 overlay 的通道 width 抬到 128（帧 127B ≤ 128）；两核必须同值（帧结构是
   双方 ABI）。`rlog_emitf` 同时补了截断标记 `...`，超长不再静默。
   另外 `tx_send` 现在**行首换行也剥掉**：ztest 的断言行走
   `PRINT_DATA("\n    Assertion failed at …")`，行首换行若留着，中继的信封只加在
   第一段上，第二段（真正的断言文本）会掉出分拣范围而留在主核归档里。
3. **分拣要"剥壳"**：`acquire.split_relay_lines(..., strip=True)` +
   `DualRunSpec.relay_strip`。ztest 型从核的本地输出本来**没有**前缀（信封纯属
   传输痕迹）⇒ 搬运时把 `<前缀>:<核>:` 剥掉，`serial_TGT.log` 才与"从核本来自己
   打"**逐行等价**；命令通道型（switch）的 token 自带前缀，`strip=False` 保持原样。
   归档 `replay_meta.json` 记 `relay_envelope_stripped`（`split_from_primary` 之外）。

**BOOT / INFO 的出口**：`ztest_glue.c::zt_init()` 与 `ZT_INFO` 走 `printk`，不经
`PRINT_DATA` ⇒ 覆盖不到。为不动公共模块（那会让 22 个工程的固件全部重编），从核
在**本工程 main.c / 用例**里改用 `rlog.h` 的 `RLOG_ZT_BOOT(core)` /
`RLOG_ZT_INFO(name, fmt, ...)`（与公共宏同一文法，只是换出口）。

**收尾与 fail-closed**：从核在 `zt_run_all()` 之后发 DONE 帧（文本 =
`PROJECT EXECUTION SUCCESSFUL|FAILED`）并 `k_sleep(K_FOREVER)`（返回会进 ztest 强
`main()` 的 `TC_END_REPORT`，在本核补一条**晚于**主核收尾的标记，破坏"完成标记是
整条流最后一个"）。主核 `rlog_wait_secondary_done(RLOG_TX_TIMEOUT_MS)`；没等到
DONE、或 `rlog_lost() != 0` ⇒ 显式判负（`SECONDARY_SILENT` / `SECONDARY_INCOMPLETE`）。
`i3c_dualcore_test` 的 target 侧 `test_main` 本就不返回（dispatcher join，无 DONE，
属设计）⇒ 主核改用**静默判据**（连续 `RLOG_QUIET_MS` 无新行）等它到齐。

**背压（先测后改，§6.11.5 的第一风险）**：从核侧的发送重试/阻塞由
`rlog_stat_tx_*` 全局符号量化（GDB 直读，不依赖调试信息，见
`tmp/b2impl/read_rlog_stats.py`）。**实测无阻塞**：试点 `i2c_pure_test` 单次上电
`tx_lines=9, retries=0, max_retry=0, max_block_ms=0, lost=0`，主核接收环高水位
4/24（环 24 行）。结构性解释：主核 RX ISR 把帧从 mbox FIFO（8 深）搬进环
（24 行）⇒ 从核要连发 >32 帧且主核线程不 drain 才可能被压住；ztest 的逐项输出
只发生在**测试项之间**，不进时序敏感段。

**已知残余（非本步引入）**：DUAL_RUN 的 prime 轮固件在被杀 gdb 后仍继续跑，其收尾
输出可能落进 pass-2 的采集窗 ⇒ 归档里出现两份 token 块（历史归档
`2026-09-20-4` / `-11` 的 switch 同样 `EXEC=2` 可证）。判定按名字覆盖，不受影响。
