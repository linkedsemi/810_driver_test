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
| Flash 烧录 | `tools/flash_program/CSKYFlashProgramerConsole.exe`（工程内打包副本，回退 `C:\user_file\EC_FLASH_PROGRAM\...`）+ 算法 `tools/flash_program/qsh_flash_algo.elf` | 烧 XIP 固件用（§15），经 `scripts/flash_program.py` 封装；与 DebugServer **互斥占用 CKLink**，烧录时 DebugServer 必须退出 |

DebugServer 启动/清理（先于一切 **RAM load** 烧录操作）：

```bash
# 启动（首次或被杀后）
cd C:/uaer_app/debug_server/bin && ./DebugServerConsole.exe &
# 启动失败且报 CKLINK used by other program：FlashProgrammer（GUI/CLI）正在占用
tasklist | grep -i -E "cskyflash|debugserver"   # 找占用进程
taskkill //F //PID <pid>          # 杀掉后重启 DebugServer
# 验证在线：直接 GDB 连一次（能进 "Remote debugging" 即通），勿用 netstat 判离线
```

> **CKLink 互斥（双向）**：DebugServer（GDB RAM load 调试）与 FlashProgrammer（XIP flash 烧录，§15）
> 在同一时刻只能有一个持有 CKLink。烧 flash 前确保 DebugServer 已退出（`flash_program.py`
> 会检测并警告，`--kill-debugserver` 可强制杀）；烧完需要 GDB 时再启动 DebugServer。

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
            ├── tmp/                         # 调试临时物（探针/抓包/日志，git 忽略，见 §10.15）
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
| Flash 烧录封装 | `scripts/flash_program.py` | 调 CSKYFlashProgramerConsole.exe 烧/读 XIP flash（§15）；`dbg_run.py --xip` 的烧录后端 |

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
  <PREFIX>:<CTRL|TGT>:BOOT                       # 通用开始标记（必须，见下）
  <PREFIX>:<CTRL|TGT>:START:<test_name>
  <PREFIX>:<CTRL|TGT>:PASS:<test_name>
  <PREFIX>:<CTRL|TGT>:FAIL:<test_name>:<reason>
  <PREFIX>:<CTRL|TGT>:INFO:<test_name>:<key>=<value>[,<key>=<value>...]
  <PREFIX>:<CTRL|TGT>:SUMMARY:ITEM:<name>:<PASS|FAIL|NOT_RUN>[:reason]
  <PREFIX>:<CTRL|TGT>:SUMMARY:END:<passed>/<ran> PASS
  <PREFIX>:<CTRL|TGT>:DONE:<PASS|FAIL>           # 通用结束标记（必须，双核各自打印）
  <PREFIX>:DONE:<PASS|FAIL>
  ```
  - **BOOT/DONE 是所有驱动测试工程必须实现的通用标记**（新工程生成时注入）：
    BOOT 在固件入口（test_main 第一行）打印，DONE 在套件全部结束后打印。
    harness 在 load 后收不到 BOOT → 判定**测试环境有问题**（烧录链路/串口），
    区别于"测试失败"；DONE 用于判定套件完整结束。
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

### 6.2 批量回归判据（★ 所有 run_*_tests.py 必须实现，i3c 样板已校准）

一次 case 的完整判定链（参数默认值实测校准，勿凭感觉改）：

1. **两遍烧录**（§10.4）：prime 轮冲刷 I-cache 旧镜像，pass-2 为正式轮。
   - prime 轮判据 = 串口出现**任何 `<PREFIX>:` token**（旧镜像可能无 BOOT 标记，
     等 BOOT 会误报环境失败）；prime 轮输出丢弃，pass-2 前清空采集行。
   - pass-2 判据 = **BOOT 标记**（当前固件必有）。
2. **BOOT 环境判定**：pass-2 spawn 后 LOAD_WINDOW=20s 内收不到 BOOT → 判定
   **测试环境问题**（DebugServer 残留/烧录链路/镜像陈旧），**中止回归**
   （继续跑只会批量假 FAIL），区别于"测试失败"。
3. **完成判定 = 静默判据 + 完成标记**（★ 2026-08-25 用户反馈"测试跑到一半被
   截断"修正）：BOOT 之后连续 IDLE=10s 无新串口字节 **且已见套件完成标记**
   （任一核 `SUMMARY:END` 或 `DONE`）→ 判 done 提前杀 gdb。**只有静默但无
   完成标记 = 测试中途长间隔（慢速 case 相邻 token 间隔可 >10s）→ 继续等**，
   每 15s 打一条诊断；case_timeout 硬上限兜底。10s 是"套件结束后快速收尾"
   的窗口，不是"测试中途"的判据。
4. **串口必须实时采集**（用户要求，批量必开）：token 是实时流，跑完即过，
   事后读串口只剩 shell 提示符 → 后台线程逐行存 `lines`，判据与报告都取自
   该流；禁止事后一次性读串口。
5. **token 解析用 `re.search()` 不用 `^` 锚点**（§10.5：shell 提示符重绘会在
   行首加 `uart:~$ ` 前缀）；DONE 正则须支持带 core 的 DONE
   （`<PREFIX>:<TGT>:DONE:<PASS|FAIL>`，target 用 DONE_CORE 宏打印）。
6. **每 case 前后 DebugServer 自愈（按需重启，★ 2026-08-25 用户反馈"CKLink
   1s 开关 3-4 次"修正）**（§10.17）：先杀残留 gdb（1025 ESTABLISHED 对端），
   杀完复查——连接残留仍在（ESTABLISHED 未释放）或 server 已死（无 LISTENING）
   才重启；健康则**不重启**（实测 6 case 全量只重启 1 次）。重启 = taskkill +
   Popen + 轮询日志 `DebuggerServer$` 提示符判就绪；Python 里 taskkill/tasklist
   一律单斜杠参数（§10.16）。
7. **回归前构建一致性**（§10.18）：固件加新标记（BOOT/DONE）后必须先构建全部
   case 再回归，勿 `--skip-build` 混跑旧镜像（无 BOOT → 假 load_fail）。
8. 结果判定沿用 §6：CTRL+TGT 双 PASS 且双核 DONE=PASS 才判 case PASS；
   串口缺项按 §12.3 内存兜底合并（来源标 mem）。

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
   实现 §6.2 批量回归判据 + §12 报告归档（reports/<date>/ + summary.md 索引 +
   INFO 解析 + 内存兜底合并）。
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
   FlashProgrammer（GUI `CSKYFlashProgrammer.exe` 或 CLI 烧录，§15）占用 CKLink。
   反方向同理：**烧 flash 时 DebugServer 必须已退出**（`flash_program.py`
   会检测警告，`--kill-debugserver` 强制杀）。切换场景时先确认对方已退出：
   `tasklist | grep -i -E "cskyflash|debugserver"`。
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
13. **XIP 烧录会话日志**：CLI 烧录追加写 `Bins\flash-programer.log`（与 GUI 共用），
    `flash_program.py` 按文件偏移量 diff 出本会话新增段用于判定；排障先看
    该文件尾部（"CKLINK may be used"、"verify fail" 等关键字）。
14. **双核固化必须一次会话**：EraseType=0（全片擦）在编程前擦整个 flash；
    分两次 CLI 调用烧 cpu1/cpu2 时第二次会擦掉第一个核的镜像。双核 XIP
    用 `--bin a@0x08000000 --bin b@0x08100000` 一次烧（或 `--erase 2` 范围擦）。
15. **调试临时物统一放 `tests/tmp/`**：一次性探针脚本、GDB 断点脚本、串口
    抓包、临时采集脚本一律放工程 `tests/tmp/` 下（gitignore 规则
    `projects/*/tests/tmp/` 已全局忽略，含 `__pycache__/`），用完无需删；
    但**通用脚本不准放这里**——判断标准：别人/别的工程会不会直接调用它？
    会（dbg_run/dump_results/run_*_tests/GDB 会话脚本）→ 留 `tests/scripts/`
    永久提交；不会（一次性调查产物）→ `tests/tmp/`。
16. **Python subprocess 调 Windows 命令不能用 MSYS 双斜杠**（★脚本层高频）：
    Git Bash 里 `taskkill //F //PID x` 有效（MSYS 把 `//F` 转成 `/F`），但
    **Python subprocess 没有 MSYS 转换层**，`//F` 原样传给 taskkill →
    "无效参数/选项 - '//F'" 且进程没被杀；若代码用 `except: pass` 吞异常，
    表现为"taskkill 杀不掉/杀不干净"假象（实测：`tasklist //FI` 过滤失效
    显示 `?`、残留 DebugServer 占 1025、新实例绑定失败卡初始化全是这个根因）。
    **subprocess 里一律单斜杠**：`["taskkill", "/F", "/PID", pid]`、
    `["tasklist", "/FI", "PID eq x", "/FO", "CSV"]`。排查"杀不掉"先查参数斜杠。
17. **DebugServer 连接残留的脚本化自愈（run_*_tests.py 的 clean_gdb_residue 模式）**：
    detach 不能真正释放连接（任何"连接→detach→退出"的 gdb 都留残留，探测连接
    也制造残留）；唯一可靠清理 = **重启 DebugServer 进程**。脚本化四步：
    ① netstat 找 1025 上 ESTABLISHED 的对端 PID（是 gdb 则 taskkill /F /PID）；
    ② taskkill /F /IM DebugServerConsole.exe；③ netstat 精确杀 1025 LISTENING
    兜底（/IM 偶发杀不净）；④ Popen 重启（stdout 重定向到日志文件），**轮询
    日志出现 `DebuggerServer$` 提示符判就绪**（≤30s）——不能只固定 sleep
    （CKLink 初始化耗时随机）。就绪后 spawn 前打 1025 LISTENING 的 PID 便于诊断。
18. **回归前必须构建全部 case（勿轻易 --skip-build）**：固件加新标记（BOOT/DONE）
    后若只重建过部分 case，`--skip-build` 跑全量时旧镜像 case 无 BOOT → pass-2
    等 BOOT 超时报"测试环境有问题"假 load_fail。判断"环境问题"前先确认该 case
    的 build/ 是否包含当前固件（token 标记齐不齐）。

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

## 15. XIP 固件固化与启动测试（CSKYFlashProgrammer CLI）

把测试固件烧进板载 SPI flash，板卡复位后从 flash 启动（XIP 执行或 boot 拷贝）。
与 §3 RAM load（GDB 加载到 SRAM）互补：RAM 模式断电丢失、每轮重载；flash
固化是持久化的，适合 XIP 启动类测试 / 量产镜像。

### 15.1 工具链（实测，勿凭想象）

| 项 | 值 | 说明 |
|---|---|---|
| CLI | 工程内打包副本优先：`tools/flash_program/CSKYFlashProgramerConsole.exe`；回退 `C:\user_file\EC_FLASH_PROGRAM\FlashProgrammer\Bins\CSKYFlashProgramerConsole.exe` | 已打包进工程（自包含，含 Cklink.dll 固件等全部依赖）；注意拼写 "Programer"（GUI 是 "Programmer"）；`-f <config.json>` 必需 |
| 算法 | 工程内 `tools/flash_program/qsh_flash_algo.elf`；回退 `C:\user_file\EC_FLASH_PROGRAM\qsh_flash_algo.elf` | flash 芯片驱动；LE 系列用 `leo_flash_algo.elf`（也在 tools 内） |
| 会话日志 | `tools/flash_program/flash-programer.log`（随 FP_EXE 所在目录） | 追加写，按偏移 diff 会话（§10.13）；打包时已清空旧日志 |
| 路径默认 | `flash_program.py` 与 `test_common.sh`（`FP_EXE`/`FP_ALGO`）均为工程内优先 + 外部回退；用户可用环境变量覆盖 | 打包副本不依赖外部 C:\user_file，拷贝 810_driver_test 即自包含 |
| config | JSON：`DebugServerSetting` + `FlashProgramSession`（CommandType 0=写/1=读、TargetConfig.AlgorithmFile/EraseType 0=全片/1=扇区/2=范围/3=不擦/ConnectType/Timeout/ResetAndRun、TargetObjectArray 按序 Program+Verify，Type 0=ELF/1=Hex/2=Bin+TargetAddr/3=规则数据） | 由 `scripts/flash_program.py` 生成，勿手写 |

### 15.2 flash 地址约定

| 镜像 | 地址 | 说明 |
|---|---|---|
| cpu1（1os/2os 都烧这个核） | `0x08000000` | XIP flash 基址 |
| cpu2（仅 2os） | `0x08100000` | 与 cpu1 镜像相隔 16MB |

> **地址与启动信息必须在脚本中自己掌握**：地址约定集中定义在
> `scripts/test_common.sh` 环境变量（`CPU1_XIP_ADDR`/`CPU2_XIP_ADDR`/
> `FP_EXE`/`FP_ALGO`），case 的 test.sh 引用这些变量，不散落硬编码；
> 涉及 XIP 的 case 在 test.sh 头注释里**明确声明本 case 的启动模式**
> （见 §15.6）与所用地址。

### 15.6 多核运行模式（★ 每次固化前先想清楚本 case 用哪种）

**核心机制（用户明确，勿违背）**：
> **只要任一核固件是 XIP 的，所有核的固件都必须烧进 flash**——另一个核即使是
> RAM 固件，也先把它的 bin 烧到 flash 对应区（cpu2 的 RAM 固件烧 `0x08100000`），
> 由 XIP 核的 boot 代码负责把它引导启动。**只有单核运行、或双核全部 RAM 的
> 场景，才可以直接经 CKLink（DebugServer + GDB）load 到 RAM 执行。**

| 模式 | cpu1 | cpu2 | 操作 |
|---|---|---|---|
| **XIP 标准组合（默认 XIP 场景）** | flash `0x08000000`（XIP 变体构建，XIP 执行 + boot 引导） | flash `0x08100000`（**标准 2os 构建的 RAM 固件**，由 cpu1 boot 搬运启动） | cpu1 用 `@2os_xip` 构建、cpu2 用 `@2os` 构建（§15.3）；两个 bin 一次会话烧进 flash（§10.14），`--reset-and-run` 复位后 cpu1 XIP boot 统一引导 |
| 全 RAM（单核或双核，**默认测试**） | RAM（`load` ELF@0x100c0000） | RAM（`restore bin@0x10000000`） | 不烧 flash，直接 CKLink GDB load（现有 §3 两遍烧录法） |

要点：
- **没有"一个核烧 flash、另一个核 GDB load RAM"的混合模式**：涉及 XIP 就
  全烧 flash（cpu2 烧的是标准 2os 构建的 RAM 固件 bin，§15.3）；纯 RAM 才走 CKLink
- **cpu2 的标准 2os 构建也会产出 `zephyr_flash_*.bin`**（flash 段镜像），
  烧录就用它，不是 zephyr.bin
- **1os 只有 cpu1**：1os_xip 固化只烧 `0x08000000`；2os XIP 标准组合才需要
  cpu1+cpu2 两个区
- 固化前确认：本 case 是否涉及 XIP → 涉及则列出**全部核**的构建变体与
  flash 地址并一次会话烧录；纯 RAM 则照常 GDB。地址写进 test.sh 头注释与
  运行记录，避免"以为在跑 flash 镜像其实跑的是 RAM 旧镜像"

### 15.3 构建 XIP 变体（★ 默认标准：cpu1 XIP + cpu2 RAM）

**默认测试（单核/双核）= 全 RAM**（现有 §3 流程，不烧 flash）。
**XIP 场景的标准组合 = cpu1 XIP + cpu2 RAM**：
- cpu1 用 XIP 变体构建（XIP 执行 + boot 引导），烧 `0x08000000`
- cpu2 用**标准 2os 构建（RAM 固件）**，产物也烧进 flash `0x08100000`，
  由 cpu1 的 XIP boot 代码搬运启动（§15.6）

```bash
# cpu1: XIP 变体（2os_xip）——分别用 -d 区分构建目录
cd linkedsemi_zephyr_project/samples/<app>
west build -p auto -b lsqsh_evb@2os_xip/lsqsh/cpu1 -d build_xip_cpu1 .
# cpu2: 标准 2os（RAM 固件，不烧 XIP 变体）
west build -p auto -b lsqsh_evb@2os/lsqsh/cpu2 -d build_cpu2 .
```

- XIP board 名 = `lsqsh_evb@{os}_xip/lsqsh/cpu{N}`（实测 `@1os_xip`/`@2os_xip`
  都可用；`{os}`+`xip` 都在 `@` 后，cpu 后缀不带 xip；1os 只有 cpu1）
- 产物：cpu1（XIP）→ `build_xip_cpu1/zephyr/zephyr_flash_bram_xip_*.bin`
  （= BRAM boot header + flash 镜像合并，烧录用它）；cpu2（标准 2os）→
  `build_cpu2/zephyr/zephyr_flash_*.bin`（flash 段镜像，烧录也用这个）

### 15.4 烧录（scripts/flash_program.py）

```bash
# 标准 XIP 组合（cpu1 XIP + cpu2 RAM，§15.3）：两个 bin 一次会话烧录
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
- 烧录前 `flash_program.py` 检测 DebugServer/gdb 进程并警告（§10.2 互斥），
  `--kill-debugserver` 强制清理

### 15.5 启动测试（dbg_run.py --xip）

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
- 串口采集/角色识别/token 解析与 RAM 模式共用；token 前缀用 `--prefix` 指定
  （默认 `I3C_TEST`），烧任意固件（如 hello_world）时按需设置
- 启动方式（ResetAndRun vs DebugServer reset）以实机实测为准，回填本节约定
