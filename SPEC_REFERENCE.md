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

**一次性操作事故不入注释/规范**（与 debug-knowledge"一次性坑不沉淀"同一原则，本条款把它明确到代码注释）：

- 注释与规范只记录两类有长期价值的内容：**a) 驱动/硬件层面需要注意的问题**；**b) 应用层面容易反复出现的问题**。
- **一次性操作事故**（"某次迁移时一度丢失导致构建失败，某日已修复"这类历史事件叙述）不记录——不写进注释，也不进规范/速查。
- 区分标准：**保留"是什么/为什么必须这样"（仍成立的结构约束），删除"曾经发生过什么事故"（含日期的修复记录、"一度/曾经/迁移时/某次"的叙述）**。
  - ✅ 保留："`../tests/expected` 必须保留（protocol.h 引用的 *_vectors.h 向量本体在该目录）" —— 结构约束，永远成立
  - ❌ 删除："迁移 common_sources.cmake 时一度丢失导致全量重建编译失败（2026-09-04 修复）" —— 一次性事故史

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
4. 启动 run_all.py（background 模式，run_in_background=true）
5. 心跳监控循环（每 60s 查日志增长 + summary 行数；日志超过 case_timeout 无增长判卡死，按 runner.md「心跳监控」节判定表恢复；load/prime 期不计静默）
6. 全部 case 完成后：读取 summary → 失败分类（见 §6.8.2）→ 恢复或跳过
7. 汇总总表（见 §6.8.4）
```

**STRATEGY_DEBUG（单项调试）**
```text
1. 读现象描述，匹配 CLAUDE.md §4.2 速查表
2. 按 qsh-debug §0 总流程推进：
   a. 环境确认（§1-3）
   b. 复现抓现象（dbg_run.py --raw）
   c. 分层定位（embedded-debug §1）
   d. 断点抓现场（embedded-debug §2）
3. 每步检查：是否命中已知坑（§10 / KNOWN_ISSUES.md）
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
