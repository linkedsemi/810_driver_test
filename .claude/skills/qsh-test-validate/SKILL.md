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
| **测试项名（ZTEST 第二参）** | `<suite>_<name>` 不得等于任何被测驱动的导出符号名 | `ZTEST(spif, clk_check_config)` 撞驱动 `spif_clk_check_config()` → 编译 `conflicting types` / `too many arguments` |

**检查方法**：从工程类型推断期望角色词（I3C=`controller/target`，I2C/SPI/UART=`master/slave`），扫描 `cpu*_` 目录前缀验证。
**测试项名**：ZTEST 实体函数名 = `<suite>_<name>`（注册段名才用裸 name），suite 用驱动短前缀
（`spif`/`i2cf`/`spid`）时最易撞——grep 固件里 `ZTEST(` 拼接出的名字，与被测驱动头文件的导出函数名比对，命中即 FAIL。

### 1.3 prj.conf 配置（必查）

所有 case 的 `prj.conf`（含 overlay 注入的）必须包含：

```kconfig
# ztest 逐项输出（`KNOWN_ISSUES.md` #26，=n 则 START/PASS/FAIL 行被抑制）
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

# ★ libc 三件套必须显式写全（`KNOWN_ISSUES.md` #35 + #58）：NEWLIB=y 与 Zephyr POSIX
# types 冲突；缺 PICOLIBC=y 落 minimal libc（无 mktime 等）→ 共享层调用即构建失败
CONFIG_PICOLIBC=y
CONFIG_NEWLIB_LIBC=n
CONFIG_MINIMAL_LIBC=n
```

**检查方法**：解析 prj.conf（简单 `key=value` 格式），逐项比对。
注意：部分配置可能在 board defconfig 中，prj.conf 未显式写不算错——只检查**显式写了但值错**的项。
**例外：libc 三件套必须显式写全，缺一即 FAIL**（历史遗漏点——单核工程漏 `PICOLIBC=y` 时，共享层
一旦用到 mktime 等函数即构建失败，KNOWN_ISSUES #58）。

**以太网类工程额外必带（条件必查）**：被测驱动涉及 `eth_dwmac*.c` /
`mdio_dwmac.c` / 板载 PHY 的工程，prj.conf **必须**含 `CONFIG_NET_TCP=y` +
`CONFIG_SHELL=y` + `CONFIG_SHELL_BACKEND_SERIAL=n` + `CONFIG_SHELL_BACKEND_DUMMY=y`，
**缺任一即 FAIL**（原因见 qsh-test-gen §1.7 / KNOWN_ISSUES #72；此条是 §1.4 第 6 条
"统一 `SHELL=n`"的例外）。
**检查方法**：工程引用 `eth_dwmac`/`mdio_dwmac`/PHY 驱动 → 逐项比对上述 4 项。

**网络配置类（WARN）**：出现 `CONFIG_NET_CONFIG_SETTINGS=y` 而无
`CONFIG_NET_CONFIG_INIT_TIMEOUT=0` → **WARN**（网口未 up 时 `net_config_init_by_iface()`
按默认 30 s 等链路，吃掉 case 超时窗口，表现为"load 后很久才有输出"）。
原因见 qsh-test-gen §1.7 / KNOWN_ISSUES #89。

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
| overlay 公共片段 | app.overlay 不得手写 `&gpioa { status` 使能块；cpu1 侧（单核+双核 target）应 `#include` `common/gpio_toggle.dtsi`，cpu2 侧不得包含。**公共层未覆盖的其它端口**允许使能（如 CS/中断脚落在 `&gpioj`），但须注释说明用途 | `grep -nE "&gpioa\b" <固件>/app.overlay`：cpu1 侧命中=FAIL（应走 dtsi include），cpu2 侧命中=FAIL。**判据收窄到 `&gpioa`**——原 `grep "&gpio"` 会把公共层未覆盖端口的**合法**使能块一并判 FAIL：`common/` 属铁律禁改、不可能把 `gpioj` 加进去，故旧判据必然误报 |
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

以下 10 项源自新工程实机跑测暴露的模板缺陷，**必须逐项检查**
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
| i | C/H 文件块注释内不得出现 `*/` | `src/**/*.{c,h}` 的**块注释内**匹配到 `*/` 即 FAIL（高频写法：通配路径 `case_*/`、`a*/b`）→ `*/` 提前终止注释 → 编译报 `expected '=', ',', ';' ... before '.' token`，列号指向注释行 |
| j | build_single_test 的 CONF_FILE 语义（复用上游 test/sample） | 复用需**整体替换** conf 的上游 test/sample 时，`test.sh` 须自持 `west build -DCONF_FILE="<上游 conf>;<增量>"`；仅用 `build_single_test`（只拼 `<src>/prj.conf;<conf>`，**追加**语义）即 FAIL（带回上游 prj.conf 的错误分型 → 编译失败，KNOWN_ISSUES #76） |

### 1.10 生成期静态检查补充（2026-09-18 新增，含「必然误报」纠正）

| # | 检查项 | 一句话判定 |
|---|--------|-----------|
| k | `ZT_INFO` 第一参是字符串字面量 | `grep -nE 'ZT_INFO\([A-Za-z_]'` 命中（即非 `ZT_INFO("`）→ FAIL（宏把 name 塞进 `%s` 位置参数，传标识符当变量引用，KNOWN_ISSUES #69 / qsh-test-gen §1.4 第 4 条） |
| l | `ZTEST_SUITE` 显式 6 形参 | 每处 `ZTEST_SUITE` 位置与"是否需项间清理"相符；第 5 参 = after（用例级）、第 6 参 = teardown（套件级）——**按位置读代码极易看反**，要求模板显式写满 6 参并加注释（qsh-test-gen §1.4 第 8 条） |
| m | 恒真断言（WARN） | 用例内 `zassert_ok(<driver_api>(...))` 形式，而该 API 在驱动源码里是**无条件 `return 0`**（含只做指针赋值/只写寄存器而不判错）→ `WARN: 恒真断言`，要求补一个**副作用判据**（寄存器回读 / `dev->data` 内部字段 / 复用位）或降级为 `ZT_INFO`（qsh-test-gen §1.6 第 4 条） |
| n | 测试项数三方一致 | 固件 `ZTEST(` 数（按 suite 分核）**==** run 脚本 `TEST_NAMES` 数 **==** dump 脚本 `TEST_NAMES` 数，且顺序符合 `nm --numeric-sort <elf> \| grep _ztest_unit_test__<suite>__`。**加了 `ZTEST` 没同步脚本 ⇒ 报告静默漏项**，靠人记同步清单必漏，机器判据才拦得住 |
| o | `DRIVER_NAME` == 工程目录名 | `grep -n 'DRIVER_NAME' <工程>/tests/run_test.py` 与 `projects/<dir>` 比对；不一致 ⇒ 违反 `SPEC_REFERENCE.md` §6.4.4 `<driver>_test` 前缀，且 summary key 与 `run_all.py` 的 `<proj>_<case>` 不匹配（多写一行 `0/0`） |
| p | DW SPI 实例使能 | overlay 使能 `snps,designware-spi` 节点（`spi3`/`spi4`）却未同时带 `&dmac2 { status = "okay"; }` 与 `timing-calibration-disabled` → WARN（编译期或运行期硬失败，qsh-test-gen §1.8 / KNOWN_ISSUES #82） |
| q | GPIO 端口使能（WARN） | DT 有 `cs-gpios = <&gpioX …>` / 中断 / 复位脚落在 `&gpioX`，而 overlay 未使能该端口 → WARN（lsqsh 端口默认全 `disabled`，驱动 `device_is_ready(port)` 失败后**静默 not ready**，KNOWN_ISSUES #81） |
| r | 扫描类工程的还原自检闸 | 邻接表/连通矩阵类工程须有"每脚驱动后还原再读回自身"的**独立 FAIL 闸**（如 `pin_restorable`），且还原路径须是**能真正撤销**的那条并**读回验证**（写错半区的还原静默无效，KNOWN_ISSUES #93）；另须有**基线门** —— 基线为低的脚不产生连线证据、单列 `BASELINE_LOW`（#94）。缺失 → FAIL（缺闸时矩阵的下三角全是 artifact，不可用于接线结论，KNOWN_ISSUES #86） |
| s | `ZTEST` 套件必须有同名 `ZTEST_SUITE` | `ZTEST` 的套件集合必须 ⊆ 同名 `ZTEST_SUITE` 集合，**差集非空即 FAIL**。缺 `ZTEST_SUITE` 时用例**函数被编译但永不注册执行、无任何构建期/运行期报错**（报告静默少数）。判据：`grep -nE '\bZTEST\(' <file> \| grep -v '\\$'` 与 `grep -nE '\bZTEST_SUITE\(' <file>` 各取套件名求差集（`\b` 使 `ZTEST_SUITE(`/`my_ZTEST(` 不被误配为 `ZTEST(`；`grep -v '\\$'` 排除宏体续行）。实测：emmc 差集 `{emmc_app_cpu_guard}`（2 项从未跑）、qspi 差集 `{qspi_flash_id}`（**8 项从未跑**）→ 不漏；全库 84 个 `src/*.c` 扫描，其余 38 个含 `ZTEST` 的文件差集空 → 不误报；`i3c_target_test.c` 的 `TGT_ZTEST(item)` 宏体靠 `grep -v '\\$'` 排除（不排除则误报形参 `item`）。**局限**：宏生成的 ZTEST 文本不可见 ⇒ 该类工程需另人工核 `ZTEST_SUITE` 数与宏实例化数一致（i3c 是显式枚举 ZTEST_SUITE 的范式）（KNOWN_ISSUES #96） |
| t | COVERAGE.md 表的裸竖线撕裂（G-FMT-2） | 每张 markdown 表内**所有行**的**未转义**竖线数必须一致(表头 / 分隔行 / 每数据行)。判据：逐行数未转义竖线（反斜杠转义的不计），同一表块内列数不一致 ⇒ FAIL——裸竖线让该行多切出列，**该表在 markdown 里就是坏的**，下游解析会把一行撕成两半（实测值跑到期望列）。修法：转义成「反斜杠 + 竖线」或改写为等值表达式（如 `abs(diff)`）。实测：全库 24 份 COVERAGE.md / 130 张表，18 份零命中（不误报）、6 份命中真坏表（trng `runs_8k`、iopmp `register_readback`、spi `spi_ls_isr`、emmc `O-A`、eth `:121/122`、io_probe 表头）⇒ 不漏。下游 `coverage_expected()` 已按**尾两列锚定回拼**容错、报告不受影响，**但源文件仍坏** ⇒ 工程内仍须修 |
| u | `dbg_run.py` 不得用 `communicate()` 收尾（G-HW5-2） | `grep -nE '\.communicate\(' <工程>/tests/scripts/dbg_run.py` 命中 → FAIL。原因：`Popen.communicate()` 在 `input=None` 时 **close stdin 并置 `self.stdin=None`** ⇒ 超时分支的 `proc.stdin.write("\x03")` 抛 `ValueError` ⇒ 落进 `except: proc.kill()` ⇒ **gdb 被 kill、留连接残留**（下次 `target remote` 能连但 `reset`/`load` 失效）。模板须 **drain 线程 + `proc.wait()` + 定时写 `\x03`/`detach`**（qsh-test-gen §1.5 / KNOWN_ISSUES #101）。正反自测：23 份 live 模板 **0 命中**（不误报）、`projects/i3c_i2c_switch_test/tmp/backup_*/tests/scripts/dbg_run.py` 旧副本 **5 命中**（漏不掉） |
| v | 外部条件未满足的降级口径不统一（G-FINAL-4，WARN） | 依赖**外部接线/对端**的用例，前置条件不满足时应 `ztest_test_skip()`（+ COVERAGE 标注）而非硬断言；**同一工程内对同一物理前置条件两种口径并存**（有的项 skip、有的项硬 FAIL）⇒ WARN（"同一物理前置条件"需人工结合 COVERAGE / 接线记录确认）。正例 `spi_test`：7 个 roundtrip 项 `ztest_test_skip()` 不计分母，而 `link_probe` 对同一"SPI1↔SPI3 跳线不通"硬断言 `zassert_equal(lb_link_state, LB_LINK_OK, …)` ⇒ 命中；反例 `i2cf_test`：探测不到对端一律 `ztest_test_skip()`（3 处）⇒ 不命中。修法 = 统一为 skip，或按 qsh-test-gen §1.6 第 6 条把强弱判据切成**不同 case** |
| w | 分层接入完整（新工程 / 新 `run_test.py`） | `run_test.py` 须是**薄壳**并接上两层：`grep -nE 'def l2_spec' <proj>/tests/run_test.py` **命中**（L2 接入 = `common/process.py` 的 `ProcessSpec` 五钩子）+ 文件内出现 `acquire`（L1 接入 = `common/acquire.py`）；工程内**自实现采集**（自写 gdb/west 采集段替代 `AcquireSpec`）即 FAIL（旧"每份 `run_test.py` 自己实现采集"的世界已结束）。**正反自测**：24 份 live 全命中（不误报）；A2 迁移前备份 `tmp/a2/before/*/run_test.py` 全部 `acquire` 缺失（漏不掉）。详见 `FRAMEWORK_MAP.md` §1 / `SPEC_REFERENCE.md` §6.4.8 / §6.4.10 |

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

**并发构建检查（★ 同一 workspace 可能被别的会话同时构建）**：编译验证类任务**开工与收尾各跑一次**
进程检查 —— `tasklist | grep -iE "ninja|cmake|west"`，必要时
`wmic process where "name like '%cmake%'" get CommandLine,CreationDate` 看发起命令与时间。
发现并发（出现"刚验证过的 ELF 消失"即信号）⇒ **收尾重新验证一遍 ELF**
（新断言串 + `nm --numeric-sort` 链接序）再下结论，**不要沿用最早的证据**。
构建目录本应串行化（分级路由），并发属越界，应在报告中显式提出。

**并发的第二种来源：自己上一轮的残留**。`nohup west build … &` 起后台构建后误判它已死，
又起前台构建到**同一** `-d` 目录 ⇒ `ninja: error: failed recompaction: Permission denied`
（两者互抢，后台那次其实还活着）。判据/处理：怀疑"后台构建死了"时**先查进程树再重跑**
（`Get-CimInstance Win32_Process` 可见 `nohup→west→python→cmake` 链）；确认残留后**按 PID**
`taskkill //PID <pid> //T` 杀**自己那条链**，**禁 `//IM python.exe`**（连坐，KNOWN_ISSUES #62）。
见 qsh-test-run §6。

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
