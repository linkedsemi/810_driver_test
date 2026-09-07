---
name: qsh-test-gen
description: QSH 驱动测试代码生成 skill（三大类②自动化测试代码生成）：按 810_driver_test/CLAUDE.md 规范自动生成新的驱动测试工程或 case（目录骨架、协议头、双核固件骨架、公共结果模块注入、test.sh 三模式、dbg_run.py），并指导自行调试验证到可用。新建 <driver>_test 工程或新 case 时调用本 skill。
---

# QSH 测试工程 / 用例生成

目标：**新 agent 直接按规范生成新测试例程，并自己调试到可用**。
规范依据：`810_driver_test/CLAUDE.md`（命名/固件/协议/Agent 规范全部以它为准）。
样板工程：`810_driver_test/projects/i3c_dualcore_test/`（生成时逐文件对照抄）。

## 1. 生成新驱动测试工程（<driver>_test）

### 1.0 外部环境确认（★ 先做，CLAUDE.md §14.1）

生成前先判断被测外设是否有**芯片外部连接路径**：
- **有**（I3C 对连、UART 环回、GPIO 互连、外接传感器等）→ **必须先问用户
  具体外部环境**（接线、对端设备、电平/上拉/速率），用户告知后才能开始生成；
  环境记录进顶层 README.md「外设接线记录」+ 工程 COVERAGE.md 标注 `外部: ...`
- **无**（纯片内外设：TRNG/加密/存储/看门狗）→ 直接生成，COVERAGE.md 标注"片内"

### 1.1 目录骨架（CLAUDE.md §1，★ 工程统一收在 `projects/` 下）
```text
810_driver_test/projects/<driver>_test/
├── cpu0_<role>_controller/     # cpu2 侧固件（发起方）
│   ├── src/main.c + src/<role>_controller_test.c
│   ├── prj.conf
│   └── CMakeLists.txt          # 注入 common/cpu2_console_uart5.overlay + cpu2_mem_layout.overlay + test_results.c
├── cpu1_<role>_target/         # cpu1 侧固件（响应方）
│   ├── src/main.c + src/<role>_target_test.c(.h)
│   ├── prj.conf
│   └── CMakeLists.txt          # 注入 test_results.c
└── tests/
    ├── include/<driver>_test_protocol.h
    ├── scripts/dbg_run.py      # 从样板复制改名，改 TESTS_DIR/banner（解析走公共 parse_ztest）
    ├── scripts/dump_results.py # 从样板复制（GDB 读内存兜底，CLAUDE.md §6.1，必须带 --json；TEST_NAMES 按链接序对齐 §10.27）
    ├── run_test.py  # 一键回归（公共 parse_ztest + report_dir，见 §1.6）
    ├── COVERAGE.md             # 覆盖矩阵（§13.3 模板，见 §1.6）
    ├── expected/               # 确定性测试的预期数据（NIST 向量等，§13.2）
    ├── case_<速记>/test.sh + overlay/conf 差异
    └── *.md                    # 问题总结
```

> 固件目录 `cpu<N>_<role>/` 的 role 按 **CLAUDE.md §1.1 命名结构规范**选：
> I3C → controller/target；SPI/I2C/UART → master/slave；host/device 模型 →
> host/device；**纯片内单核外设（无总线角色）→ 直接 `cpu1_<driver>`**（如
> `cpu1_trng_test`）。角色名必须来自被测驱动标准术语，不臆造混搭名。

### 1.2 关键注入（cpu2 侧 CMakeLists.txt，样板照抄；工程在 projects/ 下，common 引用比原来多一级 `../../../`）
```cmake
# 项目特定源文件
target_sources(app PRIVATE
    src/main.c
    src/<role>_test.c)

# 项目特定include目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件（统一入口：test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# cpu2侧额外overlay
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)
```

**注意**：
- `common_sources.cmake` 统一管理公共源文件；GPIO toggle（PA05/PA07）按
  BOARD_QUALIFIERS 自动仅编入 cpu1（cpu2 侧不编入，勿手动加回）
- **复用收敛（SPEC_REFERENCE §6.1.9）**：新工程一律引用公共层，禁止私拷副本：
  - GPIO enable：cpu1 侧（单核工程 + 双核 target 侧）app.overlay 用
    `#include "../../../common/gpio_toggle.dtsi"`（相对层级按工程位置算准），
    cpu2 侧不 include；禁止在 app.overlay 里手写 `&gpioa { status = "okay"; };`
  - C 源：走 `common_sources.cmake`，不单独 target_sources 公共文件
- prj.conf 需添加 `CONFIG_GPIO=y`
- 特殊工程（如 rtc_ls_test 不使用 ztest_glue）可手动选择包含哪些公共源文件

### 1.3 协议头（tests/include/<driver>_test_protocol.h）
- opcode 用 ASCII 助记符（`'S'/'E'/'W'/'L'/'I'/'H'/'R'`），避免与 direct CCC 命令码混淆
- **只保留跨核命令 opcode/常量（测试机制），reporting token 宏已删除**
  （ztest 原生化，见 §1.4；测试名 = `ZTEST(suite, name)` 注册名，与
  dump_results.py TEST_NAMES 链接序对齐，CLAUDE.md §10.27）
- 驱动日志仍按 §1.4 第 6 条 `LOG_INF` 规范（ztest/glue 输出走 printk 直出，两者并存）

### 1.4 固件骨架要点（CLAUDE.md §4 血泪教训 + ztest 原生化）
1. **test_main() 必须 `k_thread_join(&dispatcher_thread, K_FOREVER)`**，
   不得建线程即返回（否则 LOG_PANIC → 8ms 死窗）
2. ISR 回调内**禁止打日志**，只记账；dispatcher 线程在测试结束时汇总打印
3. target 侧 IBI/HJ 触发命令必须经事件队列交给 dispatcher 线程（ISR 内禁止
   调会拿锁的驱动 API）
4. **ztest 原生化（★ 迁移后模板，样板见 trng/otbn 已迁移工程）**：
   - `test_main()` 三行模板：`zt_init("TGT")`（`common/ztest_glue.c`，
     test_main 第一行，printk `ZT_BOOT <core>` + tr_init）→ 事件循环线程
     （被动驱动型，`k_thread_join(K_FOREVER)`）→ 末尾 `zt_run_all()`
     （ztest_run_all + 套件结束后按 stats 自动记账）
   - 测试函数回归**纯 ztest**：`ZTEST(suite, name)` 注册 + `zassert_*`
     带值对比断言（`zassert_equal`/`zassert_within`/`zassert_mem_equal` 等），
     **零 token 宏、零 tr_record、零 EXPECT/finish 样板**（记账由 glue 自动做）
   - 数值实测上报用 `ZT_INFO(name, "k=v[,k=v]", ...)` 宏（§13.4）
   - CMakeLists 注入 `../../../common/ztest_glue.c` + `../../../common/
     test_results.c` + include dir（工程在 projects/ 下）；prj.conf 必带
     **`CONFIG_ZTEST_VERBOSE_OUTPUT=y`**（§10.26，=n 则逐项输出被抑制）
   - **`CONFIG_NEWLIB_LIBC=n`**（★ 2026-09-02 实测：`=y` 与 Zephyr POSIX
     types 冲突——`pthread_cond_t` 类型重定义导致构建失败；全工程统一 `=n`，
     minimal libc 已覆盖 snprintf/strlen 等常用函数）
   - `dump_results.py` 的 TEST_NAMES 按**链接序**（SORT_BY_NAME，§10.27）
     与 glue 记账 idx 对齐：`nm --numeric-sort <elf> | grep
     _ztest_unit_test__<suite>__`
5. （已删除旧 SUMMARY token 流程——完成标记 = ztest 原生
   `PROJECT EXECUTION SUCCESSFUL|FAILED`）
6. **log 输出规范（★ 2026-08-29 用户要求，所有固件必须遵守）**：驱动
   log/token 输出统一走 `LOG_INF`（不用 printk），prj.conf 必带：
   `CONFIG_LOG=y`、`CONFIG_LOG_MODE_DEFERRED=y`（buf 模式）、
   `CONFIG_LOG_BUFFER_SIZE=16384`、`CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=10`、
   `CONFIG_LOG_BACKEND_UART=y`、`CONFIG_LOG_PRINTK=n`。
   **除了必要卡时序的地方**：ISR 内禁 log（§4.2）、时序测量段不打
   log（§4.3）不变。理由：消息进 log buffer 由 log 线程异步输出，
   测试线程不被 UART 阻塞、消息不丢。
   （ztest/glue 输出走 printk 直出，与 LOG 并存互不干扰）
7. **log 门禁判定（★ 2026-08-29 用户要求）**：等 log 有打印才开始测试；
   load 后规定时间（LOAD_WINDOW）内收不到任何 log/ztest 输出 → **直接判
   该 case 失败**，不深挖根因（正常 load 后几秒内必出 log）。见
   CLAUDE.md §6.2。

### 1.5 构建/加载（scripts/test_common.sh 复用）
- `test_common.sh` 已通用化：设 `TEST_ROOT` 后 source，`run_test` 一把跑
- case 的 test.sh 三模式：`./test.sh`（只编译）/ `-g`（+GDB 加载运行）/ `-r`（仅加载）
- GDB 地址/串口全走环境变量（`GDB_SERVER_HOST/PORT`、`*_UART`），不硬编码

### 1.6 覆盖矩阵 + 预期数据 + 回归脚本（CLAUDE.md §12/§13，★ 必做）
1. **COVERAGE.md**：对着被测驱动源码逐路径列测试项（每个条件分支至少一个
   测试触及），按 §13.3 模板填：`测试项 / 驱动路径 / 维度 / 预期数据 / 预期来源 /
   状态`。维度从 §13.1 清单选（功能通路/边界长度/异常参数/统计性质/并发/
   中断时序/复位掉电）。**不得拍脑袋**：先读驱动源码（`zephyr/drivers/` 对应
   `.c`），把分支点抄进矩阵再设计用例。
2. **expected/**：确定性测试（通信数据/状态字/加密向量）生成预期数据文件，
   固件 include 后逐字节比对：
   - 加密类 → 抽 NIST/官方测试向量生成 `expected/nist_vectors.h`
   - 通信类 → 固定 pattern + 状态字（预期字节序列/寄存器值）放 expected/
   - 生成脚本随工程保留（如 `tests/scripts/gen_vectors.py`），保证可复现
3. **run_test.py** 必须实现（样板为已迁移工程的 run_test.py）：
   - **解析层用公共 `common/ztest_report.py` `parse_ztest`**（勿自写解析器），
     判定按 §6.2/§6：单核 = 全项 `PASS - <name>` + `PROJECT EXECUTION
     SUCCESSFUL`；双核 = 同名项双核 PASS + 两核 SUCCESSFUL
     （例外：i3c 工程 TGT 侧无 PROJECT EXECUTION，判定 = CTRL SUCCESSFUL
     且 TGT≠FAILED，见 §6 双核例外）
   - **§6.2 批量回归判据**：两遍烧录（prime 判"任何 ztest 输出"、pass-2 判
     ZT_BOOT）、20s 无 ZT_BOOT=环境问题中止、BOOT 后 10s 静默 + 完成标记
     （任一核 `PROJECT EXECUTION ...`）= 结束提前杀、串口实时采集、
     行解析 `re.search()` 不锚点、每 case 前后 DebugServer 自愈（§10.17）、
     构建全量 case 后回归（§10.18）
   - **归档用公共 `common/report_dir.py`**（`allocate_session_dir` /
     `case_dir_for` / `update_summary`，禁止自算路径）：报告写到会话目录
     `reports/<YYYY-MM-DD>-<idx>/<project>_<case>/<driver>_test_report.{md,json}`
   - summary.md 只在会话目录最外层，**行粒度 = case**（key
     `<project>_<case>`，§12.4）
   - 解析 `INFO <name>: k=v[,k=v]` 约定行填报告"实际"列（§13.4）
   - 串口缺项用 `dump_results.py --json` 内存兜底合并（§12.3；TEST_NAMES
     按链接序对齐 §10.27）

## 2. 生成新 case（不改 C 代码）

新 case = 新建目录 + 三件套：
```text
tests/case_<速记>/
├── test.sh          # 从样板 case 复制，改 TEST_NAME 和头注释
├── controller.overlay / controller.conf   # 差异（如 I3C 速率）
└── target.overlay / target.conf
```
共享同一份固件源码，case 差异只体现在 overlay/conf/test.sh。

## 3. 生成后自检 + 调试验证（必须做，验收标准 CLAUDE.md §11）

1. `./test.sh` 双核编译通过（零失败）
2. 确认 DebugServer 在跑（qsh-debug §2），跑 `dbg_run.py --case <新case>`
   - load 后几秒内必须出 log/ztest 输出（无输出 → 直接判失败，见 §1.4 第 7 条）
   - banner 角色识别正确（CTRL/TGT 无 `?`）
   - 每项测试 `PASS - <name>`，双核同名项两核都 PASS
3. 单 PASS 不算：target 侧也必须出 `PASS - <name>`；双核都要
   `PROJECT EXECUTION SUCCESSFUL`（例外：i3c_dualcore 类 target test_main
   不返回的工程，TGT 无该行属设计，判 CTRL SUCCESSFUL 即可，CLAUDE.md §6）
4. 失败时按 zassert reason 定位：`--raw` 看原始日志（`Assertion failed at
   <file>:<line> (<func>): <msg>`）→ 必要时 GDB 断点抓现场（qsh-debug §6）
5. 串口无输出/被刷掉时：`python scripts/dump_results.py --case <case> --json`
   读内存兜底取结果（§6.1 / §12.3；TEST_NAMES 按链接序对齐 §10.27）
6. 调通后：更新顶层 README.md 接线登记表、写问题总结存档（CLAUDE.md §7）、
   把新坑沉淀进 CLAUDE.md §10
7. 跑一次 `run_test.py` 一键回归，确认：
   - 报告落在 `reports/<日期>-<idx>/<project>_<case>/<driver>_test_report.{md,json}`
     （会话目录），内容含预期/实际列
   - 会话目录最外层 summary.md 有本 case 行（§12.4，key `<project>_<case>`）
   - COVERAGE.md 所有测试项最终状态为 ✅（回归后对表）

## 4. 常见生成错误

| 错误 | 原因 | 修法 |
|---|---|---|
| ztest 输出不出现 | test_main 返回导致 LOG_PANIC 死窗/停摆 | `k_thread_join(K_FOREVER)`；另查 prj.conf `CONFIG_ZTEST_VERBOSE_OUTPUT=y`（§10.26） |
| ISR 里 LOG 导致时序崩 | 违反 §4.2 | 改记账，dispatcher 汇总打印 |
| IBI/HJ 卡死 | ISR 里调 i3c_ibi_raise（拿 mutex） | 经事件队列 |
| 编译过但烧录不跑 | overlay 缓存旧 SRAM 地址 | `rm -rf build/*` 全量重建 |
| 串口角色全 ? | 连接残留 / 口号错 | qsh-debug §2.3 / §4.1 |
| INFO 宏编译错（"macro passed N arguments" / format 警告） | variadic 宏把格式串与数值参数一起透传，格式串既拼进 fmt 又重复入参 → 参数错位 | 用公共 `ZT_INFO(name, fmt, ...)` 宏（printk 直出，见 common/ztest_glue.h），勿自造 INFO 宏 |
| run 脚本 build 失败 "source directory -D does not exist" | west build 的 `-D` 必须连写 `-DVAR=VAL`，拆开会被当 source 目录 | `["west","build","-p","auto","-b",BOARD,"-d",build, src, f"-DDTC_OVERLAY_FILE={overlay}"]` |
| cmake 报 Invalid character escape '\z' | Windows 反斜杠路径传进 cmake 被转义 | 路径统一 `.as_posix()`（正斜杠） |
| run 脚本 ValueError: too many values to unpack | token 正则 `(TGT)` 用了捕获组，groups() 多出角色组 | 固定文本用非捕获组 `(?:TGT)`，只有变量用 `(...)` |
| run 脚本 TypeError: write_text() got unexpected keyword 'mode' | Python 3.5+ 的 Path.write_text 无 mode 参数 | 用 `with open(path, "a"/"w", encoding=...)` |
| dbg_run.py / run_test.py 串口 0 行（单核工程） | 脚本漏 `set {int}0x4002200c = 2`（cpu2 hold），cpu2 自启劫持 uart1 | ★ **模板必须注入**：gdb 命令序列在 `reset` 前加 `set {int}0x4002200c = 2`（APP_CPU_SRST bit1）；所有 dbg_run.py / run_test.py 模板必须包含此步骤 |
| overlay 编译后 printk 无输出 | overlay 缺 `chosen { zephyr,console = &uart1; };`，DT_CHOSEN 为 NULL → uart_console_init 跳过 | ★ **模板 overlay 必须含 chosen 块**：单核工程 overlay 显式声明 `chosen { zephyr,console = &uart1; };`；双核 cpu2 侧走 cpu2_console_uart5.overlay |
| 测试代码 store fault 挂死 → 串口 0 行 | k_malloc 返回 NULL → memset(NULL) → 崩溃；极具迷惑性表现为"串口无输出" | ★ **测试代码禁用堆分配**：不用 k_malloc/k_calloc，改用静态数组（`static uint8_t buf[N]`）；prj.conf 不依赖 CONFIG_HEAP_MEM_POOL_SIZE；排查串口无输出时**先查崩溃再查配置** |

## 5. 坑沉淀要求

生成/调试过程遇到的新问题，处理方式写入对应 skill 速查表 + CLAUDE.md §10。

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| 工程/case 生成完成 | ★ qsh-test-validate | 自动转，不可跳过 |
| qsh-test-validate FAIL | 回本 skill 修模板 | FAIL 项存在 |
| qsh-test-validate PASS | qsh-test-run（首次验证） | 全部 PASS |
| 生成过程发现模板缺陷 | skill-keeper（模板补漏） | 模板规则需改进 |
