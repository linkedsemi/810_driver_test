# CLAUDE.md — 810_driver_test 自动化测试工程规范

> 本目录是 **LSQSH 驱动批量自动化测试工程集**。`i3c_dualcore_test` 为首个样板工程，
> 后续每个驱动测试工程都应**按本规范自动生成**，保持结构、协议、脚本、命名完全一致，
> 避免工程间差异和重复踩坑。新工程生成后，必须同步更新顶层 `README.md` 的接线登记表。
> 工程上下文（项目背景/board 命名/构建命令/west 工作流）见顶层 `README.md`「工程背景」
> 节——摘录自仓库根 CLAUDE.md，根文件缺失时按该节理解，平时不重复加载。
>
> **新 agent 上手路径**：先读本文件（工程规范）→ 读 `.claude/skills/qsh-driver-test/SKILL.md`
> （skill 调用关系与操作总入口）→ 按具体任务调对应 skill。遇到新问题，先查 §10 已知坑。

## 0. 硬件环境与工具链（实测，勿凭想象）

| 项 | 实测值 | 说明 |
|---|---|---|
| 板卡 | `lsqsh_evb@2os` | 双核双系统 AMP；cpu1=target（安全核，uart1）、cpu2=controller（应用核，uart5） |
| GDB | Xuantie 工具链 `riscv64-unknown-elf-gdb` | **无 Python 支持**，验证逻辑全用 `-ex` 命令链 |
| GDB server | **`127.0.0.1:1025`** | 由 PC 进程 **XuanTie DebugServer** 提供，**不是** CKLink 固件内置！ |
| DebugServer | `C:\uaer_app\debug_server\bin\DebugServerConsole.exe` | 端口在 `bin\default.ini` 的 `SOCKETPORT=1025`；**必须手动启动**，启动后才有 1025 |
| DebugServer 冲突 | CSKYFlashProgrammer.exe | 它占用 CKLink 时 DebugServer 起不来（报 "CKLINK may be used by other program"），`taskkill //F //PID <pid>` 后重启 |
| COM 映射 | COM14=CKLink Serial（VID 32BF:B210，**必须排除**）、COM15=MCP2200（04D8:00DD）=controller uart5、COM16/17=CH342（1A86:55D2）=target uart1+备用 | **口号以设备管理器为准**，脚本靠 banner 动态识别角色，不硬编码 |
| 角色 banner | "CPU2 MASTER"/"i3c_master_test" → CTRL；"CPU1 SLAVE"/"i3c_slave_test" → TGT | 烧录后从串口实际输出判定 |
| 串口参数 | 115200 8N1，pyserial | 两个核各一路，不要混用 |

DebugServer 启动/清理（先于一切烧录操作）：

```bash
# 启动（首次或被杀后）
cd C:/uaer_app/debug_server/bin && ./DebugServerConsole.exe &
# 启动失败且报 CKLINK used by other program：
tasklist | grep -i cskyflash     # 找到占用进程
taskkill //F //PID <pid>          # 杀掉后重启 DebugServer
# 验证在线：直接 GDB 连一次（能进 "Remote debugging" 即通），勿用 netstat 判离线
```

## 1. 工程模板（生成新工程时按此骨架）

```text
810_driver_test/
├── README.md                 # 硬件环境 + 接线登记表（新工程必须追加一行接线说明）
├── CLAUDE.md                 # 本规范
├── common/                   # 板级通用配置，所有工程共用
│   ├── cpu2_console_uart5.overlay   # cpu2 控制台统一用 uart5（PN14/PN13）
│   ├── cpu2_mem_layout.overlay      # cpu2 内存布局（sram_ro/sram 划分，统一管理）
│   └── test_results.h / test_results.c   # 测试结果记账公共模块（§4.5，所有固件必须接入）
├── scripts/                  # 通用脚本，所有工程共用
│   └── test_common.sh        # build/load 公共函数（需调用方设 TEST_ROOT）
├── reports/                  # 测试报告归档（§12，按日期目录组织，脚本自动生成）
│   └── <YYYY-MM-DD>/
│       ├── summary.md                    # 当天全工程汇总索引
│       ├── <driver>_test_report.md       # 各工程详细报告
│       └── <driver>_test_report.json
└── projects/                 # ★ 所有测试工程统一收在此目录下（工程根 = 810_driver_test/projects/<driver>_test/）
    └── <driver>_test/
        ├── cpu0_<role>_controller/     # cpu2 侧固件（发起方）
        ├── cpu1_<role>_target/         # cpu1 侧固件（响应方）
        └── tests/
            ├── include/<proto>_protocol.h   # 共享 opcode + token 宏
            ├── COVERAGE.md                  # 测试覆盖矩阵（§13，新工程必须创建）
            ├── expected/                    # 预期数据/黄金向量（§13，确定性测试用）
            ├── scripts/                # 工程特有脚本（仅本工程用）
            │   ├── run_<driver>_tests.py    # 一键回归 agent（报告写入 reports/<date>/）
            │   ├── dbg_run.py               # 单次烧录 + 双串口采集调试
            │   ├── dump_results.py          # GDB 读内存取结果兜底（§6，串口不可用时用）
            │   └── *.gdb / *.gdbcmd         # GDB 会话脚本
            ├── case_<速记>/                 # 时序/配置 case（每 case 一份构建）
            │   ├── test.sh                  # 三模式脚本（见 §5）
            │   ├── *.overlay / prj.conf 差异
            │   └── build/                   # 构建产物（.gitignore）
            └── *.md                         # 问题总结/状态记录（见 §7）
```

### 1.1 固件目录命名结构规范（★ 新工程必须按此规则命名，不是"样例"）

每个测试工程的固件目录统一为 `cpu<N>_<role>/`：

| 组成 | 规则 |
|---|---|
| `cpu<N>` | 固件运行的核：`cpu1` = 安全核（target 侧）、`cpu2` = 应用核（controller 侧） |
| `<role>` | 该核在被测驱动/总线模型中的**标准术语角色**，由被测外设的总线协议决定，取协议规范的标准称谓 |

角色术语对照（按被测驱动选择，不得臆造混搭名）：

| 驱动/总线模型 | 角色术语 |
|---|---|
| I3C（MIPI I3C 主从模型） | `controller` / `target` |
| SPI / I2C / UART 类主从模型 | `master` / `slave` |
| 主机/设备模型（如 USB、PC 侧对端） | `host` / `device` |
| **纯片内单核外设（无总线、无对侧角色）** | 不用角色词，直接 `cpu1_<driver>`，如 `cpu1_trng_test` |

示例：
- I3C 双核工程：`cpu0_i3c_controller/` + `cpu1_i3c_target/`
- TRNG 单核工程：`cpu1_trng_test/`（无角色）
- 后续 SPI 工程：`cpu0_spi_master/` + `cpu1_spi_slave/`

> 角色名必须来自被测驱动的**标准术语**；出现混搭名（如 `host_master`）说明
> 没有按规范选角色，按上表更正。单核工程只有一种角色时该角色即该核的角色。

## 2. 固定命名规则（新工程一律照抄，不许改名）

| 项 | 规则 | 示例 |
|---|---|---|
| 工程目录 | `projects/<driver>_test/`（★ 所有工程统一收在 `projects/` 下） | `projects/i3c_dualcore_test/` |
| 固件目录 | `cpu<N>_<role>/`，role 按 §1.1 命名结构规范选（I3C: controller/target、SPI/I2C/UART: master/slave、host/device、单核片内外设直接 `cpu1_<driver>`） | `cpu1_i3c_target/` |
| case 目录 | `case_<速率/模式速记>_<值>`，全小写下划线 | `case_pure_i3c_12m5/` |
| token 前缀 | `I3C_TEST`（`<DRIVER>_TEST`），大写 | `I3C_TEST:` |
| 共享头文件 | `tests/include/<driver>_test_protocol.h` | `i3c_dualcore_test_protocol.h` |
| agent 脚本 | `projects/<driver>_test/tests/scripts/run_<driver>_tests.py` | `run_i3c_tests.py` |
| 调试脚本 | `projects/<driver>_test/tests/scripts/dbg_run.py` | 同左 |
| 公共构建脚本 | `scripts/test_common.sh`（810_driver_test 级，所有工程共用） | 同左 |
| 每 case 入口 | `tests/case_*/test.sh` | 同左 |
| 覆盖矩阵 | `tests/COVERAGE.md`（§13，新工程必须创建） | `trng_test/tests/COVERAGE.md` |
| 预期数据 | `tests/expected/`（黄金向量，§13） | `expected/nist_vectors.h` |
| 报告归档 | `reports/<YYYY-MM-DD>/<driver>_test_report.{md,json}` + `summary.md`（§12） | `reports/2026-08-21/i3c_dualcore_test_report.md` |

### 脚本路径速查（i3c_dualcore_test 样板）

| 脚本/文件 | 路径 | 作用 |
|---|---|---|
| 公共构建/加载 | `scripts/test_common.sh` | `build_test` / `load_and_run` / `print_launch_json`，环境变量全走 `GDB_SERVER_*`、`*_UART`、`RUN_TIMEOUT` 等 |
| 单次烧录+采集 | `projects/i3c_dualcore_test/tests/scripts/dbg_run.py` | GDB 两遍烧录 + 双串口采集 + token 解析 + 角色动态识别 |
| 一键回归 | `projects/i3c_dualcore_test/tests/scripts/run_i3c_tests.py` | 遍历 case → 构建 → 烧录 → 采集 → 解析 → Markdown/JSON 报告 |
| GDB 会话脚本 | `tests/scripts/flash_and_run.gdbcmd`、`check_panic_mode.gdb`、`pc_sample.gdb` | 调试辅助 |
| 内存布局 | `common/cpu2_mem_layout.overlay` | sram_ro/sram 划分（384K RO + 376K RW @0x10000000），所有 cpu2 工程必须注入 |
| 控制台串口 | `common/cpu2_console_uart5.overlay` | cpu2 用 uart5，所有 cpu2 工程必须注入 |

## 3. 硬件/串口/GDB 固定约定（勿在工程内重复定义）

- 串口：cpu1 用 uart1（板级默认），cpu2 用 **uart5**（经
  `common/cpu2_console_uart5.overlay` 注入，不直接改 chosen/pinctrl）。115200 8N1。
- GDB：**`127.0.0.1:1025`**（端口由 PC 端 DebugServer 进程提供，见 §0）；cpu1 `load` ELF
  入口 `0x100c0000`；cpu2 `restore <zephyr.bin> binary <CONTROLLER_LOAD_ADDR>` 后
  `set $pc=0x100c0000`。CPU2 加载地址变更时需同步修改：overlay `sram_ro` 基址、
  `CONFIG_CPU2_BOOT_ADDR`、`scripts/test_common.sh` / `tests/scripts/dbg_run.py` /
  `tests/scripts/*.gdbcmd` 的 `CONTROLLER_LOAD_ADDR` 四处。
- 环境参数走环境变量（`GDB_SERVER_HOST/PORT`、`*_UART` 等），脚本内不硬编码；
  接线变更登记到顶层 `README.md`「外设接线记录」表；COM 口号靠 banner 动态
  识别角色（§0），脚本排除 CKLink 自身口（VID 32BF:B210）。

## 4. 固件编写规范（血泪教训，违反会出诡异问题）

### 4.1 ztest 契约：`test_main()` 返回 = 测试结束（★最重要）
- 被动驱动型测试（由对侧通过总线命令推进）必须用**事件循环线程**跑业务，
  但 `test_main()` **不得在创建线程后直接返回**：
  ```c
  k_thread_create(&dispatcher_thread, ..., K_NO_WAIT);
  k_thread_join(&dispatcher_thread, K_FOREVER);   /* 必须！ */
  ```
- 原因：返回后 ztest 执行 `LOG_PANIC()` → `panic_mode` 永不复位 → 同步日志
  UART 阻塞造成 8ms 级死窗（实测 logspan 8115us → 11us）。
  详见 `tests/ZTEST_PANIC_LOG_8MS_DEADWINDOW.md`。

### 4.2 ISR 内禁止打日志
- 任何 LOG_INF/LOG_ERR/printk 都不能出现在 ISR 回调里（deferred 模式同样影响时序）。
- ISR 只记账（计数/时间戳/标志），由 dispatcher 线程在测试结束时统一打印
  `TGT_DBG`/`CTRL_STEP` 类汇总行。

### 4.3 时序测量
- 用 `k_cyc_to_us_floor32` + `k_cycle_get_32` 记时间戳，不打 LOG 不拉 IO。
- 需要波形观察时用 GPIO 标记，统一走 `io_set/clr/toggle`，记入 case 注释的
  「调试 IO 定义」段，并与测试逻辑隔离（可编译开关）。**调试完必须移除**（见 §10.6）。

### 4.4 命令协议
- opcode 用 ASCII 助记符（`'S'/'E'/'W'/'L'/'I'/'H'/'R'`），避免与 direct CCC
  命令码在波形上混淆；定义与 token 宏统一放共享 `*_protocol.h`。
- target 侧 IBI/HJ 触发命令必须经事件队列交给 dispatcher 线程执行，
  禁止在 ISR 中调用会拿锁/互斥量的驱动 API。

### 4.5 测试结果记账公共模块（★ 所有测试固件必须接入）
- 每项测试的结果**统一记账到公共模块** `common/test_results.{h,c}`（全局符号），
  **禁止**在各固件里定义私有 results 数组。串口不可用时靠 GDB 读内存兜底
  （§6.1），因此符号名、数组尺寸、取值语义必须全工程统一：
  ```c
  int8_t test_results[TR_MAX_COUNT];        /* -1 未跑 / 0 PASS / 1 FAIL */
  char   test_reasons[TR_MAX_COUNT][TR_REASON_LEN];  /* 失败原因文本 */
  void tr_init(uint8_t count);              /* setup 里调，全部置 NOT_RUN */
  void tr_record(int idx, int result, const char *fmt, ...);  /* 结果记账 */
  ```
  - `TR_MAX_COUNT=64`、`TR_REASON_LEN=96` 在头文件定义，不许改（GDB 脚本按
    固定 stride 读 reason）。
  - **定长数组**原因：GDB 读内存要按符号地址+固定步长解析，变长/指针数组
    无法直接 dump。
  - 接入方式（两个固件 CMakeLists.txt 都要）：
    ```cmake
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common/test_results.c)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
    ```
  - 双核各自链接该模块，符号同名、地址不同，dump 时按 elf 区分核。
  - 记账时机：controller 在测试结束（zassert 前后皆可，但必须在打印 token
    前）、target 在 `tgt_finish`/校验函数里，与 token 输出同一处调用。

## 5. 构建与 case 脚本规范

- 每个 case 独立 `build/` 目录；共享同一份源码，case 差异只体现在
  overlay/prj.conf/test.sh。新 case = 新建目录 + 三件套，不改 C 代码。
- `west build` 优先 `-p auto`；改 overlay（SRAM 地址类）后必须删 build 全量重建（§10.9）。
- 每 case 的 `test.sh` 必须包含头注释模板：What is tested / CPU0/CPU1 行为 /
  Normal behavior / Abnormal behavior / Feedback / Hardware IO connections /
  Build artifacts / Usage。
- `test.sh` 三模式统一：`./test.sh`（只编译，打印 launch.json 片段）、
  `-g`（编译 + GDB 加载运行）、`-r`（仅加载运行，agent 模式）。构建时
  `SKIP_LOAD=y` 只编译不烧录。参考 `tests/case_pure_i3c_12m5/test.sh`。

## 6. Agent 与结果规范

- token 格式（机器可读，printk 输出，规避 LOG 前缀干扰）：
  ```
  <PREFIX>:<CTRL|TGT>:START:<test_name>
  <PREFIX>:<CTRL|TGT>:PASS:<test_name>
  <PREFIX>:<CTRL|TGT>:FAIL:<test_name>:<reason>
  <PREFIX>:<CTRL|TGT>:INFO:<test_name>:<key>=<value>[,<key>=<value>...]
  <PREFIX>:<CTRL|TGT>:SUMMARY:ITEM:<name>:<PASS|FAIL|NOT_RUN>[:reason]
  <PREFIX>:<CTRL|TGT>:SUMMARY:END:<passed>/<ran> PASS
  <PREFIX>:DONE:<PASS|FAIL>
  ```
  - INFO token 与 FAIL reason 格式规范见 §13.4 / §13.5。
- 双核各自记账到 **§4.5 公共模块** `test_results[]`（-1 未跑/0 PASS/1 FAIL +
  reason），controller 在测试全部结束后先发 `'P'`（打印总结）命令，sleep 200ms
  等 target 总结先出，再打 CTRL 总结，最后 `DONE`。
- agent 回归输出报告并归档 `reports/<date>/` + 更新 summary.md（§12）；
  超时用 Python `subprocess.run(timeout=...)`，不用 bash `timeout`（Git Bash 无）。
- 烧录用两遍烧录法规避 I-cache 残留旧镜像（§10.4）。

### 6.1 串口不可用时：GDB 读内存取结果（dump_results.py）
- 结果数组是全局符号（链接地址=加载地址），连 DebugServer 后 `x/Ndb` 直接读，
  不依赖串口；失败项再 `x/s` 读 `test_reasons`（地址 = 基址 + i*96）。
- 脚本：`tests/scripts/dump_results.py`（新工程从样板复制），用法：
  ```
  python dump_results.py --case case_pure_i3c_12m5              # 只读当前内存
  python dump_results.py --case ... --run 35                    # 先两遍烧录跑 35s 再读
  python dump_results.py --case ... --raw                       # 打印 gdb 原始输出
  ```
- 适用场景：串口 token 被刷掉/串口忙/远程无串口；**串口可用时仍以 token 为准**
  （§6 判定标准），内存结果用于兜底与交叉验证。

## 7. 问题记录规范

- 定位闭环的问题写 `<BUG>_<SUMMARY>.md` 总结（现象/根因/证据/修复/验证/通用教训）
  存档供后续避坑；调试中的状态记 `<NAME>_STATUS.md` 并维护状态表。
- 新建工程先读样板 `projects/i3c_dualcore_test/tests/` 下已有问题总结，确认不会重犯。
- **沉淀准则（用户要求）**：只沉淀"会反复遇到"的坑（环境/工具链/方法学，如连接
  残留、I-cache、ISR 禁 LOG）进 §10 已知坑或对应 skill；**一次性问题**（修复后
  不再遇到的具体 bug）只写 .md 存档，不加入 §10/skill。判断标准：换机器/测试/
  驱动会不会再踩？不会再踩就不沉淀。

## 8. 新工程生成 checklist

1. 在 `projects/` 下建 `<driver>_test/` 目录骨架（§1 模板，工程根 =
   `810_driver_test/projects/<driver>_test/`），全部文件按 §2 命名。
2. cpu2 侧工程在 `find_package(Zephyr)` 前注入 `common/cpu2_console_uart5.overlay`
   和 `common/cpu2_mem_layout.overlay`。
3. 实现 `*_protocol.h`、双核固件（遵守 §4 全部规范，含 §4.5 公共结果模块）、
   `test_common.sh`、首个 case 的 `test.sh`（带完整头注释）、`dbg_run.py`、
   复制 `dump_results.py`（§6.1 兜底，含 `--json` 模式）。
4. 编写 `tests/COVERAGE.md`（§13 覆盖矩阵，对着驱动源码逐路径列场景，
   不得拍脑袋；确定性测试标注预期数据来源）。
5. 顶层 README.md「外设接线记录」登记接线；`run_<driver>_tests.py` 必须
   实现 §12 报告归档（reports/<date>/ + summary.md 索引 + INFO 解析 + 内存兜底合并）。
6. 先 `./test.sh -g` 验证一个 case 双核 PASS，再跑全量回归并核对归档（§11）。
7. 单 case 实测通过前，不声明自动化回归 PASS。
8. 新工程可用 `qsh-test-gen` skill 自动生成（见 §9）。

## 9. Skill 调用关系（三大类，都在 `.claude/skills/` 下）

本工程的任务按**三大类**组织：**① 驱动 debug 调试**、**② 自动化测试代码生成**、
**③ 自动化测试（执行+结果）**。每个 skill 明确归属一个大类，总入口按类路由。

| 大类 | Skill | 用途 | 何时调用 |
|---|---|---|---|
| 总入口 | **qsh-driver-test** | 调用关系图、任务路由、标准流程总览 | 任何 810_driver_test 任务先读它 |
| ① 驱动 debug 调试 | **qsh-debug** | 连接层（DebugServer/GDB 烧录/串口）坑 + 断点抓现场法 + 分层根因定位（应用→驱动→硬件）+ 探针纪律 | 驱动行为异常、烧录/跑不起来/串口没数据、定位根因、硬件时序 |
| ② 代码生成 | **qsh-test-gen** | 新工程/新 case/协议头/test.sh 模板生成，公共结果模块注入，checklist | 新建驱动测试工程或 case |
| ③ 自动化测试 | **qsh-test-run** | dbg_run.py / test.sh / run_*_tests.py 用法、token 解析、回归执行、报告归档到 reports/（§12） | 构建、烧录、跑某个 case 或全量回归 |
| ③ 自动化测试 | **qsh-test-compare** | 结果解析、PASS/FAIL 判定、报告生成、回归差异对比、串口不可用时 GDB 读内存取结果（dump_results.py，§6.1）并**按 §12.3 兜底合并**进归档报告 | 分析测试结果、写报告、对比多次回归（含 reports/ 历史归档）、串口无输出时取结果 |

调用关系与分工边界（详见 qsh-driver-test skill）：
- 驱动有问题/定位根因 → **qsh-debug**；新建工程/case → **qsh-test-gen**；
  构建/烧录/跑回归 → **qsh-test-run**；结果判定/报告/对比 → **qsh-test-compare**。
- 分工：debug 管"为什么错"，run 管"怎么跑"，gen 管"怎么建"；生成后的验证
  用 debug + run，跑不起来的问题回 debug。

## 10. 已知坑清单（实测，逐条沉淀）

1. **gdb 被杀后 DebugServer 连接残留**（★高频）：`continue` 超时被杀（dbg_run.py
   的 kill）后，DebugServer 仍认为连接中；下次 `target remote` 能连上但 `reset`/
   `load` 可能无效 → 固件不跑，串口 0 行或只有 `\x00` 复位噪声。
   **处理**：先做一次干净连接再重跑：
   ```bash
   riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" -ex "detach" -ex "quit"
   ```
   还不行就重启 DebugServerConsole.exe。判断"目标真的在跑"：串口出现角色 banner
   （"CPU2 MASTER"/"CPU1 SLAVE"）。
2. **DebugServer 起不来**：报 "CKLINK may be used by other program" →
   CSKYFlashProgrammer.exe 占用，`tasklist | grep -i csky` 找到后
   `taskkill //F //PID <pid>`，重启 DebugServer。
3. **不要用 netstat 判离线**：1025 由 PC 进程 DebugServer 提供（非固件内置），
   判断在线 = GDB 连一次看 "Remote debugging"。用户明确纠正过。
4. **两遍烧录法**：单遍烧录 + continue 会因 I-cache 残留跑旧镜像（"慢一拍"）。
   同一镜像烧两遍，第二遍抓取；prime 轮串口输出丢弃。
5. **token 行污染**：shell 提示符 ANSI 重绘（`\x1b[8D\x1b[J`）会混入行首；
   token 前可能有 `uart:~$ ` 前缀 → 解析用 ANSI 剥离 + `re.search()`，
   不用 `^` 锚点和 `.match()`。
6. **调试探针必须清理**（用户要求，防污染其他工程）：log_core.c / kernel/timeout.c /
   `__assert.h` 等被插桩的文件用 `git checkout` 恢复；驱动/固件手工删除后
   `grep` 确认无残留。**功能修复与调试残留要区分**：MPIL workaround
   （soc_isr_stacking_e906.c unstack 恢复 MPIL）、pmp.c 的 PMP_W（XIP 区可写）是
   功能修复，必须保留；`log_dbg_*`/`to_dbg_*`/`e906_irq_log_*`/PJ06 标记/
   probe 采样/while(1) 陷阱是调试残留，一律删。用 `git diff` 逐文件甄别。
7. **ISR 内禁 LOG**（§4.2）：LOG_ERR 走 shell 后端阻塞 10ms+/条，直接导致
   8ms 级中断饿死。ISR 只记账。
8. **test_main() 不得返回**（§4.1）：返回即 LOG_PANIC → 同步日志死窗。
9. **改 overlay 删 build 全量重建**（§5）：`-p auto` 不检测 Kconfig 缓存。
10. **COM 口被占用**：`tasklist | grep python` 找残留 pyserial 进程
    `taskkill //F //PID <pid>`。
11. **GDB 无 Python**：验证逻辑全用 `-ex` 命令链；`continue &` + `interrupt`
    不可靠，用同步阻塞 continue + 超时。
12. **prime/capture 输出混合**：两轮烧录的输出会混排造成测试顺序错乱假象，
    串口只在 capture 轮开。

## 11. 新建驱动测试工程时的验收标准

1. `./test.sh` 双核编译零失败；launch.json 片段地址正确。
2. `./test.sh -g` 单 case 双核各自输出 `PASS` token（含 target 侧）；
   `dbg_run.py` 角色识别正确（CTRL/TGT 无 `?`）。
3. `run_<driver>_tests.py` 全量回归 `DONE:PASS`，报告归档 `reports/<日期>/`
   且 summary.md 有本工程行（§12.4）。
4. `tests/COVERAGE.md` 存在，测试项覆盖 §13.1 维度、确定性测试标注预期来源。
5. 期间遇到的坑已沉淀进 §10 / 对应 skill。

## 12. 测试报告归档规范（★ 所有 run_*_tests.py 必须实现）

**一次回归 = 一次归档**，脚本自动生成，不依赖人工收集。

### 12.1 目录与文件

```text
810_driver_test/reports/
└── <YYYY-MM-DD>/                    # 本地日期（datetime.now()）
    ├── summary.md                   # 当天全工程汇总索引（§12.4）
    ├── <driver>_test_report.md      # 每工程一份
    └── <driver>_test_report.json    # 同内容结构化
```

- 报告路径由脚本从 `810_driver_test/reports/` 推导，不手工指定；
  同一工程同一天多次回归 = 覆盖写（保留 latest 语义，对比历史用其他日期）。
- 单次 dbg_run.py（调试）不归档，只归档一键回归。

### 12.2 报告内容模板（Markdown）

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
- **实际**列：INFO token 的数值（`key=value` 拼接）；无 INFO 填 `-`。
- **来源**列：`token`（串口采集）/ `mem`（GDB 内存兜底）。
- **reason**列：FAIL 的 reason 原文（§6 格式）。

### 12.3 内存兜底合并规则（★ 串口缺项自动补）

回归结束（串口已关）后，脚本调用本工程 `dump_results.py --case <case> --json`
读内存结果，**只用于填补串口 token 缺失的测试项**：

| token 情况 | 判定 |
|---|---|
| 有 PASS/FAIL | 以 token 为准（来源=token） |
| 无 token 但内存有值 | 用内存值（来源=mem，报告标注） |
| 两者都无（数组 -1） | NOT_RUN，case 判 FAIL |
| 内存读取失败（连接残留等） | 容忍：标记 `mem-unavailable`，不阻断回归 |

内存兜底在 case 跑完后立即执行（目标仍在，interrupt 可读）；
`dump_results.py` 必须支持 `--json`（输出 `{"CTRL": {...}, "TGT": {...},
"reasons": {...}}` 或单核版 `{"TGT": {...}}`）。

### 12.4 summary.md 索引

每工程脚本写完自己的报告后更新当天 `summary.md`：
- 文件不存在 → 创建并写表头；已存在 → **按工程名替换该行**（不追加重复行）。
- 行格式（Markdown 表格）：
  `| <driver>_test | <date> <time> | <PASS|FAIL> | <passed>/<ran> | 备注 |`
- case 级判定沿用 §6 标准（双核双 PASS / 单核 TGT PASS + DONE）。

## 13. 测试覆盖矩阵与预期数据规范（★ 全面性 + 可判定性）

目标：测试不是"跑得通"，而是**对着驱动源码逐路径覆盖** + **结果可判定
（预期 vs 实际）**。

### 13.1 覆盖维度清单（新工程 COVERAGE.md 必须逐项对照）

| 维度 | 要求 | 示例 |
|---|---|---|
| 功能通路 | 驱动对外 API 的正常路径 | entropy_get_entropy 基本获取 |
| 边界长度 | 长度参数边界（API 类型极限、驱动内部缓冲阈值±1） | uint16_t 最大 65535、驱动 pool 128B 的 127/128/129 |
| 异常参数 | 非法入参必须返回明确错误码 | NULL/len=0 → -EINVAL |
| 统计/性质 | 随机/校验和类性质的统计门限（不 flaky） | monobit ±16σ |
| 并发 | 多线程/多实例同时访问（锁与状态隔离） | 双线程同时取熵 |
| 中断/时序 | ISR 路径、busywait 路径、驱动隐藏状态机分支 | get_entropy_isr、>64KB ADVANCE_STATE |
| 复位/掉电 | 时钟/复位路径（驱动 init 内） | clock_control_on/off 分支 |

**覆盖要求**：驱动源码里每个条件分支至少有一个测试项触及；COVERAGE.md
里每个测试项标注 `驱动路径` 列（函数名+分支）。

### 13.2 预期数据规范

- **确定性测试必须有预期值并在固件内比对**（不是打印了事）：
  - 通信类：固定 pattern + 状态字（预期字节序列/预期状态寄存器值）
  - 加密类：**NIST/官方测试向量**，放 `tests/expected/`（如
    `expected/nist_vectors.h`），固件 include 后逐字节比对；
    qsh-test-gen 生成加密类工程时从向量库抽取向量生成该文件
  - 数值类：统计门限（如 monobit 46%~54%）
- 预期值要随工程可复现：COVERAGE.md 里注明预期数据来源（标准/向量文件/公式）。

### 13.3 COVERAGE.md 模板

```markdown
# <driver> 测试覆盖矩阵
| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| basic_16 | ls_trng_get_entropy 主循环 | 功能通路 | ret==0 | API 契约 | ✅ |
```

每个测试项一行；`预期来源` 注明协议文档/NIST 向量/统计公式/API 契约。

### 13.4 INFO token（实测数值上报）

- 固件在测试内把数值型实测结果打 `INFO` token（§6），报告"实际"列取用；
  统计类测试（吞吐/占比）必须上报，否则报告里预期 vs 实际无从对比。
- 上报时机：对应测试的 PASS/FAIL 之前；key 小写下划线，如
  `TRNG_TEST:TGT:INFO:throughput_64k:mbps=5.23,bytes=65536`。

### 13.5 FAIL reason 可归因格式

`FAIL` 的 reason 必须含断言位置 + 期望/实际值（§6）：
`byte[3]:0x12!=0x34`、`ret:-EINVAL!=-EINVAL`、`ones:32768 out of [30146,35389]`。
禁止只写"data mismatch"这类无信息的文本。

## 14. 外部环境确认规范（★ 芯片外部连接路径）

测试项存在**芯片外部连接路径**（外设引脚连到对端设备/另一芯片、电平/上拉
依赖外部电路、UART 环回等）时，生成与执行都要先确认环境：

### 14.1 创建测试工程前（qsh-test-gen 必做）

- 生成前先判断被测外设是否有外部连接路径：
  - **有**（如 I3C 总线对连、UART 环回、GPIO 互连、外接传感器/EEPROM）→
    **先问用户外部环境**（具体接线、对端设备型号、电平/上拉/速率要求），
    用户告知后才能开始生成工程；
  - **无**（纯片内外设：TRNG、加密引擎、片内存储、看门狗等）→ 无需确认，
    COVERAGE.md 标注"片内"。
- 用户提供的外部环境写入两个地方：
  - 顶层 `810_driver_test/README.md`「外设接线记录」表（工程级）
  - 工程 `tests/COVERAGE.md` 涉及外部路径的测试项标注 `外部: <接线描述>`

### 14.2 执行具体测试前（qsh-test-run 必做）

- 跑回归/单 case 前，若涉及外部连接路径：**先向用户报告本次测试环境**
  （接线状态、对端设备、串口号、速率配置），确认后再开始烧录/采集。
- 测试报告归档（§12.2）头部固定带一行环境说明，指向 README.md 接线登记表，
  保证结果可复现；环境变更（改接线/换对端）后重新回归，不要复用旧报告。

### 14.3 判定示例

| 外设 | 外部路径？ | 创建前动作 | 执行前动作 |
|---|---|---|---|
| I3C 双核对连 | 有（I3C5↔I3C6 跨核接线） | 问接线/上拉/速率 | 报告接线状态 |
| TRNG | 无（片内熵源） | 直接生成 | 无需报告环境 |
| UART 环回测试 | 有（TX↔RX 短接） | 问是否环回/电平 | 报告环回状态 |
