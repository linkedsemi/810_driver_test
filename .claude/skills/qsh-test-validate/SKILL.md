---
name: qsh-test-validate
description: 810_driver_test 测试工程合规性验证 skill：检查新建/修改的工程是否符合规范（目录结构、命名、prj.conf、公共模块注入、sys.path、ztest_glue、构建通过）。新建工程后、修改工程后、或回归前预检时调用。
---

# QSH 测试工程合规性验证

目标：**在跑回归之前，先验证工程本身是否合法**——结构对、配置对、编译过。
规范依据：`810_driver_test/CLAUDE.md` + `qsh-test-gen` SKILL.md。
样板对照：`810_driver_test/projects/i3c_dualcore_test/`（双核样板）、`trng_test/`（单核样板）。

## 0. 用法

指定工程名或路径 → 默认全量验证（静态检查 + 逐 case 编译）；也可只做静态检查
（结构/配置/代码模式，跳过编译）。

## 1. 检查维度清单

### 1.1 目录结构（必查）

| 检查项 | 单核工程 | 双核工程 | 判定 |
|--------|---------|---------|------|
| `tests/` 目录存在 | ✅ | ✅ | 必须 |
| `tests/case_*/` 至少一个 | ✅ | ✅ | 必须 |
| `tests/case_*/test.sh` 每 case | ✅ | ✅ | 必须 |
| `tests/scripts/run_test.py` 或 `tests/run_test.py` | ✅ | ✅ | 必须（一键回归入口） |
| `tests/scripts/dump_results.py` | ✅ | ✅ | 必须（GDB 内存兜底） |
| `tests/scripts/dbg_run.py` | ✅ | ✅ | 必须（单次调试） |
| `tests/COVERAGE.md` | ✅ | ✅ | 必须（覆盖矩阵） |
| `cpu1_*/` 固件目录 | ✅ | ✅ | 必须 |
| cpu1 之外的固件目录（`cpu0_*/` 或 `cpu2_*/`） | ❌ | ✅ | 双核必须 |
| `common/test_results.{h,c}` 引用 | ✅ | ✅ | 必须（公共结果模块） |
| `common/ztest_glue.c` 引用 | ✅ | ✅ | 必须（自动记账） |

**检查方法**：遍历 `tests/case_*/` 确认每 case 有 `test.sh`，对照上表逐项确认存在性。

### 1.2 命名规范（必查）

| 检查项 | 规则 | 示例 |
|--------|------|------|
| 工程目录名 | `<driver>_test` | `otbn_test`, `i3c_dualcore_test` |
| case 目录名 | `case_<速记>` | `case_pure_i3c_12m5`, `case_default` |
| I3C 角色术语 | `controller` / `target` | `cpu0_i3c_controller/` |
| I2C/SPI/UART 角色术语 | `master` / `slave` | `cpu0_spi_master/` |
| 纯片内单核 | `cpu1_<driver>` | `cpu1_trng_test/` |
| **禁用混搭** | 不得 `host_master` / `device_slave` | — |

**检查方法**：从工程类型推断期望角色词（I3C=`controller/target`，I2C/SPI/UART=`master/slave`），扫描 `cpu*_` 目录前缀验证。

### 1.3 prj.conf 配置（必查）

所有 case 的 `prj.conf`（含 overlay 注入的）必须包含：

```kconfig
# ztest 逐项输出（§10.26，=n 则 START/PASS/FAIL 行被抑制）
CONFIG_ZTEST_VERBOSE_OUTPUT=y

# log 规范（§6.1.7）
CONFIG_LOG=y
CONFIG_LOG_MODE_DEFERRED=y
CONFIG_LOG_BUFFER_SIZE=16384
CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=10
CONFIG_LOG_BACKEND_UART=y
CONFIG_LOG_PRINTK=n

# ztest 基础
CONFIG_ZTEST=y

# ★ libc 三件套必须显式写全（§10.35 + KNOWN_ISSUES #58）：NEWLIB=y 与 Zephyr POSIX
# types 冲突；缺 PICOLIBC=y 落 minimal libc（无 mktime 等）→ 共享层调用即构建失败
CONFIG_PICOLIBC=y
CONFIG_NEWLIB_LIBC=n
CONFIG_MINIMAL_LIBC=n
```

**检查方法**：解析 prj.conf（简单 `key=value` 格式），逐项比对。
注意：部分配置可能在 board defconfig 中，prj.conf 未显式写不算错——只检查**显式写了但值错**的项。
**例外：libc 三件套必须显式写全，缺一即 FAIL**（历史遗漏点——单核工程漏 `PICOLIBC=y` 时，共享层
一旦用到 mktime 等函数即构建失败，KNOWN_ISSUES #58）。

### 1.4 CMakeLists.txt 公共模块注入（必查）

每个固件的 `CMakeLists.txt` 必须包含：

```cmake
# 公共结果模块
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/test_results.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
# 或用 ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/ 取决于目录深度

# ztest_glue（自动记账）
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/ztest_glue.c)

# 双核 cpu2 侧还需要 overlay 注入：
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)
```

**检查方法**：grep CMakeLists.txt 中的 `test_results.c`、`ztest_glue.c`、`cpu2_console_uart5.overlay`。

### 1.4a 复用收敛检查（必查，SPEC_REFERENCE §6.1.9）

凡公共层已有等价物的内容，工程不得私拷副本：

| 检查项 | 规则 | 检查方法 |
|---|---|---|
| overlay 公共片段 | app.overlay 不得手写 `&gpioa { status` 使能块；cpu1 侧（单核+双核 target）应 `#include` `common/gpio_toggle.dtsi`，cpu2 侧不得包含 | `grep -n "&gpio" <固件>/app.overlay`：cpu1 侧命中=FAIL（应走 dtsi include），cpu2 侧命中=FAIL |
| 公共 C 源 | 不得单独 `target_sources` test_results.c/ztest_glue.c/gpio_toggle.c，必须走 `common_sources.cmake` | grep CMakeLists 直接点名这三个文件=FAIL（common_sources.cmake include 除外） |
| 私有 results 数组 | 禁止固件私有 results 数组（原有检查项，归并到本原则） | grep 源码内 `test_result` 自定义数组 |
| 脚本私拷 | 新增通用脚本必须入公共层/scripts 样板，不得工程内另起等价脚本 | 人工 review |

### 1.5 run_test.py sys.path 设置（必查）

`run_test.py` 必须在顶部（import dump_results 之前）设置：

```python
sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))          # ztest_report, report_dir
sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))  # dump_results
```

**检查方法**：读 run_test.py 确认含 `sys.path.insert` 且覆盖 `common/` 与 `scripts/`
（或用 subprocess 调 dump_results.py 则后者可免）。

### 1.6 ztest_glue 集成模式（必查）

固件 `main.c` / `test_main()` 必须遵循：

```c
void test_main(void) {
    zt_init("TGT");           // 第一行：打印 ZT_BOOT + tr_init
    // ... 业务线程（被动驱动型需 k_thread_join）...
    zt_run_all();             // 末尾：ztest_run_all + 自动记账
}
```

**检查方法**：grep 固件 `main.c` / `test_main` 中的 `zt_init` 和 `zt_run_all`。

### 1.7 test.sh 三模式（必查）

每个 case 的 `test.sh` 必须支持三种模式：
- `./test.sh` — 只编译（`SKIP_LOAD=y`），打印 launch.json 片段
- `./test.sh -g` — 编译 + GDB 加载运行
- `./test.sh -r` — 仅加载运行（agent 模式）

**检查方法**：grep test.sh 中的 `-g` / `-r` 参数处理、`SKIP_LOAD` 变量。

### 1.8 TEST_NAMES 双份同步 + 链接序对齐（建议查）

`run_test.py` 与 `dump_results.py` **各自硬编码一份** `TEST_NAMES`，两份都必须按
ztest_unit_test **链接序**（SORT_BY_NAME）排列，与 ztest_glue 记账 idx 对齐。
**增删测试项必须同步两份**（链接序 ≠ protocol.h 枚举序，勿用枚举序填）：新项插在
链接序前部会让未同步的旧列表整体错位一格，mem 兜底结果张冠李戴/假 MISSING
（2026-09-09 新增 broadcast_nack_recovery 只同步了 run_test.py、漏 dump_results.py 实例）。

**检查方法**：若已编译，用 `nm --numeric-sort <elf> | grep _ztest_unit_test` 对比两份
TEST_NAMES 顺序一致且都与链接序相符（含注释里的实测序清单）；新工程先核对再回归。

### 1.9 模板缺陷防漏检查（必查）

以下 8 项源自新工程实机跑测暴露的模板缺陷，**必须逐项检查**
（含检查方法代码）：[ref-template-defects.md](ref-template-defects.md)

| # | 检查项 | 一句话判定 |
|---|--------|-----------|
| a | cpu2 hold 步骤（串口 0 行最高频根因） | dbg_run.py/run_test.py 的 gdb 序列须含 `set {int}0x4002200c = 2`（reset 前） |
| b | overlay chosen 节点 | 单核 overlay 须显式 `chosen { zephyr,console = &uart1; };` |
| c | 测试代码禁用堆分配 | `src/*.c` 不得有 k_malloc/k_calloc/k_realloc（NULL→崩溃→串口 0 行假象） |
| d | 归档成对 | run_test.py 归档必须同时写 `.md`+`.json`（含 write_markdown_report + REPORT_BASE） |
| e | ZTEST_SUITE 槽位 | `setup`/`teardown`（套件级）须同层配对；共享句柄不得落在 `after`（用例级）；第 5 参不得是 `*_teardown` |
| f | 分型与入口地址一致 | 分型按是否有 **cpu1 之外的固件目录（`cpu0_*` 或 `cpu2_*`）** 判（不按 `controller`/`cpu2` 关键词猜，命名跨工程不统一）；单核 = `@1os/lsqsh/cpu1` + 入口 `0x10000000`（`TARGET_ROM_START` 走 env），双核 = `@2os/lsqsh/{cpu1,cpu2}` + 入口 `0x100c0000`；混用 = FAIL（权威矩阵 SPEC_REFERENCE §6.1.10）。**另核 `test.sh` 的 board 与加载函数同源**：单核须走 `SINGLE_BOARD`/`build_single_test`（默认 1os）、双核走 `TARGET_BOARD`，**硬编码 `lsqsh_evb@...` 即 FAIL**（板级与入口常量是错配的两个独立面，须分别核；含未启用 case，KNOWN_ISSUES #57） |
| g | 样板脚本残留识别常量 | dbg_run.py `TGT_BANNERS`/`CTRL_BANNERS` + dump_results.py `TEST_NAMES` + run_test.py 打印/连接文案 + **docstring/路径注释/文件头自述文字**，出现**其他驱动/工程名**即 FAIL（残留 TEST_NAMES → mem 兜底假 PASS，KNOWN_ISSUES #59）。判据 = 以他工程名标识**本工程/本文件**；标注"从 X 样板改造"的出处引用不算 |
| h | 边界常量来源 + 缩容/兜底改动 | 边界用例常量须可溯源到驱动源码或寄存器**写-回读实测上限**，不得凭猜；`git diff` 出现"参数缩容 / 超时调小 / 加 before 钩子兜底"而**无根因说明**即 FAIL（根因未解时同因同果，钩子在前置条件不满足时还会空转） |

## 2. 编译验证（必做）

对每个 `case_*` 目录执行 `west build`：

```bash
# 单核 1os
west build -p auto -b lsqsh_evb@1os/lsqsh/cpu1 -d <case>/build <src> \
    -DDTC_OVERLAY_FILE=<overlay>
# 双核 cpu1 / cpu2（board 分别为 lsqsh_evb@2os/lsqsh/cpu1、.../cpu2，
# build 目录分别 <case>/build_cpu1、<case>/build_cpu2）
west build -p auto -b <board> -d <build_dir> <src> -DDTC_OVERLAY_FILE=<overlay>
```

**判定**：编译 rc=0 且无 error/warning（或只容忍已知无害 warning）。

## 3. cases.yaml 集成检查（建议查）

若工程应加入全量回归，确认 `regression/cases.yaml` 含每个 `case_*` 的条目
（路径形如 `projects/<project>/tests/<case>`，正/反斜杠任一）。

## 4. 验证输出格式

结构化报告模板（静态检查表 + 编译验证表 + 总结）见
[ref-report-template.md](ref-report-template.md) §1。

## 5. 修复建议模板

对每个 FAIL 项自动给出修复建议（缺文件复制样板、缺配置补配置等对照表）见
[ref-report-template.md](ref-report-template.md) §2。

## 6. 典型验证场景

| 场景 | 调用方式 | 检查范围 |
|------|---------|---------|
| 新工程生成后自检 | 全量（静态+编译） | 所有维度 |
| 修改 prj.conf/overlay 后 | 静态+编译受影响 case | prj.conf + 编译 |
| 回归前预检 | 静态快速扫 | 目录+配置+sys.path |
| 排查回归失败 | 针对 FAIL case | 编译+配置+glue 集成 |

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| 全部 PASS | ★ qsh-test-run（首次验证） | 自动转 |
| 存在 FAIL 项 | 回 qsh-test-gen 修 | FAIL 标注具体修复项 |
| 存在 WARN 项 | 记录后继续 | 非阻塞 |

## 链路
> **输入**← `qsh-test-gen` · **输出**→ runner(STRATEGY_TEST) · **相关**→ skill-orchestrator
