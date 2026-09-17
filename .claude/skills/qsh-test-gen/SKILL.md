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
├── cpu<other>_<driver>_<model>/ # 第二核固件（发起方：controller/master）；前缀实测有 cpu0_ / cpu2_ 两种
│   ├── src/main.c + src/<driver>_<model>_test.c
│   ├── prj.conf
│   └── CMakeLists.txt          # 注入 common/cpu2_console_uart5.overlay + cpu2_mem_layout.overlay + test_results.c
├── cpu1_<driver>_<model>/      # cpu1 侧固件（响应方：target/slave）
│   ├── src/main.c + src/<driver>_<model>_test.c(.h)
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

> 固件目录 `cpu<N>_<driver>_<model>/` 的 **model** 按 **CLAUDE.md §1.1 命名结构规范**选：
> I3C → controller/target；SPI/I2C/UART → master/slave；host/device 模型 →
> host/device；**纯片内单核外设（无总线角色）→ 直接 `cpu1_<driver>`**（如
> `cpu1_trng_test`）。角色名必须来自被测驱动标准术语，不臆造混搭名。
>
> **单核工程不建第二核目录，只有 `cpu1_<driver>/`**（board 走 1os，§1.5）。
>
> **分型判定取广义**：存在 cpu1 之外的固件目录（`cpu0_*` **或** `cpu2_*`）即双核。
> 第二核目录前缀/命名跨工程不统一（实测 `cpu0_i2c_master` / `cpu0_i3c_controller` /
> `cpu2_controller_mbox_test` 三种并存），靠 `controller`/`cpu2` 等关键词猜会漏判
> （`i2c_pure_test` 第二核叫 `cpu0_i2c_master`，实为双核）。

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
   - **libc 三件套必配（全工程统一）**：`CONFIG_PICOLIBC=y` +
     `CONFIG_NEWLIB_LIBC=n` + `CONFIG_MINIMAL_LIBC=n`。`NEWLIB_LIBC=y` 与
     Zephyr POSIX types 冲突（`pthread_cond_t` 重定义，构建失败）；缺
     `PICOLIBC=y` 则落到 minimal libc，它不含 `mktime` 等 POSIX 函数，
     驱动/共享层一旦调用即构建失败（KNOWN_ISSUES #58）
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
8. **ZTEST_SUITE 槽位纪律（★ 套件级 vs 用例级）**：签名是
   `ZTEST_SUITE(name, predicate, setup, before, after, teardown)`——
   **`setup`/`teardown` 是套件级（整个 suite 各跑一次），`before`/`after`
   是用例级（每例前后都跑）**。凡在 `setup` 里 open/init 的共享资源，必须在
   `teardown` 里 close/deinit，**两者落在同一层级**；用例间共享的句柄
   （flash_area、设备指针等）一律走套件级 setup/teardown，**不要混用 after**。
   落错槽的后果：共享句柄在第一个用例之后被置 NULL，第二个用例拿到 NULL →
   空指针 → load access fault → SoC 异常入口 `while(1)` → **串口从此静默**
   （症状极像配置/烧录问题，实测走了弯路）。
   注：`before`/`after` 形参是 `void(*)(void*)`、`setup` 是 `void*(*)(void)`，
   类型不通——所以"把 teardown 挪去 before/after"修不通，只能挪进 `teardown` 槽。

### 1.5 构建/加载（scripts/test_common.sh 复用）
- `test_common.sh` 已通用化：设 `TEST_ROOT` 后 source，`run_test` 一把跑
- case 的 test.sh 三模式：`./test.sh`（只编译）/ `-g`（+GDB 加载运行）/ `-r`（仅加载）
- GDB 地址/串口全走环境变量（`GDB_SERVER_HOST/PORT`、`*_UART`），不硬编码
- **先定工程分型（单核/双核），再选 board 与加载函数**（权威矩阵 SPEC_REFERENCE §6.1.10）：
  - 单核 = 只有 cpu1 侧固件目录 → board `lsqsh_evb@1os/lsqsh/cpu1`、cpu1 入口
    `0x10000000`、test.sh 用 `build_single_test`/`load_and_run_single`（单遍烧录）
  - 双核 = 有 cpu1 之外的固件目录（`cpu0_*` 或 `cpu2_*`）→ board
    `lsqsh_evb@2os/lsqsh/{cpu1,cpu2}`、cpu1 入口 `0x100c0000`、test.sh 用
    `build_test`/`load_and_run`（两遍烧录）
  - **单核固件一律 1os 编译测试**（用户需求 #8）；用错分型 = 板级/入口错 → 固件不启动
- **入口地址不得从异型样板抄用**：单核 Python 脚本一律
  `TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")`
  （`dump_results.py` 用 `int(os.environ.get(...), 0)`；默认值即 1os 入口，无需覆盖），
  双核显式给出 `0x100c0000`。从 2os 样板抄脚本最易漏改此常量 → `$pc` 设错 →
  固件根本没跑 → **串口 0 行**，与"固件/配置问题"同症状，极易误判。
- **脚本复制后必须改的样板残留**（逐项核对，漏一项即出上面这类假故障）：
  - `dbg_run.py`：`TESTS_DIR` + `TGT_BANNERS`/`CTRL_BANNERS` 文案（残留上一驱动
    banner，如 "CPU1 TRNG" → 角色识别全 `?` 并误报 "no firmware banner seen"）
  - `dump_results.py`：`TARGET_ROM_START`（同入口规则）+ `TEST_NAMES`（必须=
    **本工程自己的 ztest 链接序**；残留他工程名 → mem 兜底读本工程内存槽却按他
    工程名映射 → **假 PASS**，见 KNOWN_ISSUES #59）
  - `run_test.py`：`BOARD`（单核必须 `@1os/lsqsh/cpu1`）+ `TARGET_ROM_START`；
    连接/打印文案不得残留他工程名（如报告头写别的驱动名）；删除样板遗留的死常量
    （残留 2os 常量会误导后续改动）
  - `test.sh`：`TEST_NAME` + 头注释各字段（§2）；**board 一律走 `SINGLE_BOARD`/`TARGET_BOARD`
    变量（或 `build_single_test`），禁硬编码 `lsqsh_evb@...`**——硬编码 2os 板级却走单核
    `load_and_run_single`（`$pc`=1os 入口）即与入口常量错配的**同族坑**（KNOWN_ISSUES #57）
  范本 = 已迁移单核工程（otbn_test / otbn_selftest / mbedtls_test）。

### 1.6 覆盖矩阵 + 预期数据 + 回归脚本（CLAUDE.md §12/§13，★ 必做）
1. **COVERAGE.md**：对着被测驱动源码逐路径列测试项（每个条件分支至少一个
   测试触及），按 §13.3 模板填：`测试项 / 驱动路径 / 维度 / 预期数据 / 预期来源 /
   状态`。维度从 §13.1 清单选（功能通路/边界长度/异常参数/统计性质/并发/
   中断时序/复位掉电）。**不得拍脑袋**：先读驱动源码（`zephyr/drivers/` 对应
   `.c`），把分支点抄进矩阵再设计用例。
   **边界用例的常量必须有实测来源**：驱动掩码/数据手册，或（两者与实机不符时）
   寄存器**写-回读实测上限**——凭"看起来安全"拍的值会在硬件量程处静默截断
   （debug-knowledge mcu 域"寄存器位域以实测为准"）。
   **缩容 / 缩短超时 / 加兜底钩子 ≠ 修复**：根因未解时大小两个值同因同果，
   兜底钩子在前置条件不满足时还会空转，见 qsh-test-validate §1.9h。
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
   - **归档必须同时写 `.md` + `.json`**（样板 = 已迁移工程的
     `write_markdown_report()` + json writer；`.md` 用 items[] 渲染、
     `.json` 为机器可读源）；**漏写 .md 属模板缺陷**，summary.md 会把
     该 case 链到 .json、偏离 §6.4.1
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
6. 调通后：更新顶层 README.md 接线登记表、写问题总结存档、
   把新坑写进 `.claude/gotchas/INBOX.md`（沉淀归 keeper）
7. 跑一次 `run_test.py` 一键回归，确认：
   - 报告落在 `reports/<日期>-<idx>/<project>_<case>/<driver>_test_report.{md,json}`
     （会话目录），内容含预期/实际列
   - 会话目录最外层 summary.md 有本 case 行（§12.4，key `<project>_<case>`）
   - COVERAGE.md 所有测试项最终状态为 ✅（回归后对表）
8. **注册与同步（★ 生成收尾，清单见 SPEC_REFERENCE §6.4.5）**：新工程/case 的启用
   条目写入 `regression/cases.yaml`（唯一权威源），并同步 `CLAUDE.md` §2 清单与
   `(N)`（= cases.yaml 中该工程已启用的 case 数）、`README.md` §4 工程表 +
   §「外部接线记录」。漏同步 → 回归跑不到该 case / 清单与实际不符。

## 4. 常见生成错误

生成后编译/烧录/运行出错时查 [ref-gen-errors.md](ref-gen-errors.md)（错误现象→根因→修法速查表）。

## 5. 坑沉淀要求

生成/调试过程遇到的新问题，往 `.claude/gotchas/INBOX.md` 追加一条；沉淀到对应 skill 速查表 + KNOWN_ISSUES 归 keeper。

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| 工程/case 生成完成 | ★ qsh-test-validate | 自动转，不可跳过 |
| qsh-test-validate FAIL | 回本 skill 修模板 | FAIL 项存在 |
| qsh-test-validate PASS | qsh-test-run（首次验证） | 全部 PASS |
| 生成过程发现模板缺陷 | keeper（模板补漏） | 模板规则需改进 |

## 链路
> **输出**→ `qsh-test-validate` · **前置**← runner(STRATEGY_DEV) · **相关**→ skill-orchestrator
