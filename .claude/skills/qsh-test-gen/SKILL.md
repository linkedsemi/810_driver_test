---
name: qsh-test-gen
description: QSH 驱动测试代码生成 skill（三大类②自动化测试代码生成）：按 810_driver_test/CLAUDE.md 规范自动生成新的驱动测试工程或 case（目录骨架、协议头、双核固件骨架、公共结果模块注入、test.sh 三模式、dbg_run.py），并指导自行调试验证到可用。新建 <driver>_test 工程或新 case 时调用本 skill。
---

# QSH 测试工程 / 用例生成

目标：**新 agent 直接按规范生成新测试例程，并自己调试到可用**。
规范依据：`810_driver_test/CLAUDE.md`（命名/固件/协议/Agent 规范全部以它为准）。
样板工程：`810_driver_test/projects/i3c_dualcore_test/`（生成时逐文件对照抄）。

## 1. 生成新驱动测试工程（<driver>_test）

### 1.0 外部环境确认（★ 先做，`SPEC_REFERENCE.md` §6.6.1）

生成前先判断被测外设是否有**芯片外部连接路径**：
- **有**（I3C 对连、UART 环回、GPIO 互连、外接传感器等）→ **必须先问用户
  具体外部环境**（接线、对端设备、电平/上拉/速率），用户告知后才能开始生成；
  环境记录进顶层 README.md「外设接线记录」+ 工程 COVERAGE.md 标注 `外部: ...`
- **无**（纯片内外设：TRNG/加密/存储/看门狗）→ 直接生成，COVERAGE.md 标注"片内"
- **驱动有双核运行态守卫时**（如 `is_app_cpu_running()` = `SYSC_SEC_CPU->APP_CPU_SRST > 0`）：
  生成前**实测核对**该寄存器的回读值，**别只按 SDK 注释推断**。⚠️ **更正（2026-09-18 实测）**：
  写 `set {int}0x4002200c = 2` 后**回读 = `0x0`**（GDB + 固件自读一致）⇒ 守卫**不生效**、带守卫的
  API 正常（emmc 用例 SKIP=0）；"写 2 保持复位 ⇒ `>0` ⇒ API 全 `-EBUSY`"的推论**基于 SDK 注释、
  未获实测支持**（写 1 回读 `0x3` 且触发 bootrom 重启）。用例仍按寄存器回读值断言 / `ztest_test_skip`，
  别误判硬件。见 KNOWN_ISSUES #75 更正批注 / qsh-debug §7。
- **外设的引脚落在哪个 GPIO 端口**：lsqsh `dtsi` 里**每个 GPIO 端口默认 `status="disabled"`**，
  公共层 `common/gpio_toggle.dtsi` 只使能 `&gpioa` ⇒ 若被测外设的 `cs-gpios`/中断/复位脚落在
  别的端口（实例：`spi2` 的 CS 在 `gpioj 15`），**overlay 必须显式使能该端口**
  （`&gpioj { status = "okay"; };`），否则驱动 `device_is_ready(cs_gpio->port)` 失败、设备
  **静默 not ready**（无任何日志）。见 KNOWN_ISSUES #81。
- **被测驱动 init 是否引用 `resets`/`clocks`**：显式核对对应 CONFIG 开关与 DT 属性守卫是否**对称**
  ——`pwm_ls.c` 的 config 成员声明带 `IF_ENABLED(CONFIG_RESET, …)` 而实例初始化项没带 ⇒
  `CONFIG_RESET=n` 且 DT 有 `resets` 时编译失败（KNOWN_ISSUES #79）。

### 1.1 目录骨架（★ 工程统一收在 `projects/` 下）
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
    ├── scripts/dump_results.py # 从样板复制（GDB 读内存兜底，`SPEC_REFERENCE.md` §6.2.2，必须带 --json；TEST_NAMES 按链接序对齐 `KNOWN_ISSUES.md` #27）
    ├── run_test.py  # 一键回归**薄壳**：声明 AcquireSpec + `l2_spec()`（采集/处理在公共层，见 §1.6；勿自实现）
    ├── COVERAGE.md             # 覆盖矩阵（`SPEC_REFERENCE.md` §6.5.3 模板，见 §1.6）
    ├── expected/               # 确定性测试的预期数据（NIST 向量等，`SPEC_REFERENCE.md` §6.5.2）
    ├── case_<速记>/test.sh + overlay/conf 差异
    └── *.md                    # 问题总结
```

> 固件目录 `cpu<N>_<driver>_<model>/` 的 **model** 按 **命名结构规范**选：
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
- **反向提示（避免 DT 爆炸）**：需要**运行期**反复改 GPIO 方向/上下拉时**不必造 pinctrl 状态**
  ——lsqsh `gpio_lsqsh.c` 的 `gpio_pin_configure()` 直接写寄存器（方向 + `PU1_PU0`/`PD_PU2`），
  全驱动**无 `pinctrl_apply_state()`**；脚的身份来自实例 reg 基址。限制：该驱动**不会**把被外设
  复用功能占走的脚抢回来（只改方向不改 pinmux）⇒ 前提是**没有外设 pinctrl 占住该脚**
  （扫描类工程做法：一个被测外设都不使能）。端口使能仍是另一回事（默认全 disabled，见 §1.0）。
- prj.conf 需添加 `CONFIG_GPIO=y`
- 特殊工程（如 rtc_ls_test 不使用 ztest_glue）可手动选择包含哪些公共源文件
- **单出口（从核 → 主核 → 单串口，`SPEC_REFERENCE` §6.9.3/§6.11.6）若启用，三条硬约束**：
  ① `RLOG_TEXT_MAX` 必须按**最长行**抬高 —— 默认 54B **装不下 ztest 的逐项 D1 行**
  （` PASS - <name> in 0.001 seconds` = 56B）⇒ `vsnprintf` 静默截断 ⇒ **逐项全判"零证据
  FAIL"且无任何报错**；长行工程 `target_compile_definitions(app PRIVATE RLOG_TEXT_MAX=118)`
  + overlay 通道 width 128，**两核必须同值**（帧结构是双方 ABI）；
  ② **核信封加在"中继侧"**（主核 `rlog_set_relay_envelope`），**不塞进从核帧里**（14B 信封
  挤帧长必截断）；分拣侧 `split_relay_lines(strip=True)` 剥壳才与"从核本来自己打"逐行等价；
  ③ 覆盖头 `tc_util_user_override.h` 必须对**全部 TU** 可见（ztest 自己也用 `PRINT_DATA`）
  ⇒ 只能用 `zephyr_include_directories()`，`target_include_directories(app …)` 会让 ztest
  库编不过；`ZT_BOOT`/`ZT_INFO` 走 `printk` 不经 `PRINT_DATA`，须改用
  `RLOG_ZT_BOOT`/`RLOG_ZT_INFO`。细节与实测见 SPEC §6.11.5/§6.11.6。

> **补充（★ 2026-09-21）· token 拼写两类，别用 ztest 正则核 TGT 证据**：核"分拣是否到位"时按**工程自己的
> 文法**数 —— D1 = ztest 原生 `PASS - <name> in …`；D2 = 自定前缀 `<PREFIX>:<core>:(PASS|FAIL|SKIP):<name>`
> （`I3C_I2C_TEST`/`MBOX_TEST`/`RTC_TEST`，登记在 `common/ztest_report.py::NATIVE_PREFIXES`）。用 ztest 正则
> 数 D2 的 TGT 会得 `zt=0` ⇒ 误判"分拣失败"；先看 `ZT_BOOT <core>` / `<PREFIX>:<core>:BOOT` 是否存在
> （`SPEC_REFERENCE` §6.9.1 / qsh-test-compare §2）。

### 1.3 协议头（tests/include/<driver>_test_protocol.h）
- opcode 用 ASCII 助记符（`'S'/'E'/'W'/'L'/'I'/'H'/'R'`），避免与 direct CCC 命令码混淆
- **只保留跨核命令 opcode/常量（测试机制），reporting token 宏已删除**
  （ztest 原生化，见 §1.4；测试名 = `ZTEST(suite, name)` 注册名，与
  dump_results.py TEST_NAMES 链接序对齐，`KNOWN_ISSUES.md` #27）
- 驱动日志仍按 §1.4 第 6 条 `LOG_INF` 规范（ztest/glue 输出走 printk 直出，两者并存）

### 1.4 固件骨架要点（`SPEC_REFERENCE.md` §6.1 血泪教训 + ztest 原生化）
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
     - **⚠ 命令通道型固件（注册 0 个 `ZTEST`）不要调 `zt_run_all()`**（2026-09-20）：ztest 的
       `end_report()`（`ztest.c:76-82`）在 `test_status` 恒 0 时**无条件**打 `PROJECT EXECUTION
       SUCCESSFUL` ⇒ 与自裁决行冲突、给全 PASS 的核制造无谓的 `CONFLICTING_MARKERS` 噪声
       （真 FAIL 时靠 R6 fail-closed 兜住）。这类工程要么不调 `ztest_run_all`，要么让自裁决行
       用**异形文案**使 `_EXEC_RE` 不命中（KNOWN_ISSUES #98 前半 / G-HW5-3）。
   - 测试函数回归**纯 ztest**：`ZTEST(suite, name)` 注册 + `zassert_*`
     带值对比断言（`zassert_equal`/`zassert_within`/`zassert_mem_equal` 等），
     **零 token 宏、零 tr_record、零 EXPECT/finish 样板**（记账由 glue 自动做）
   - 数值实测上报用 `ZT_INFO(name, "k=v[,k=v]", ...)` 宏（`SPEC_REFERENCE.md` §6.5.4）——**用 `ZT_INFO` 前该 `.c`
     必须 `#include "ztest_glue.h"`**（glue 按需 include，`common_sources.cmake` 只保证编译进去、
     不保证每个文件 include；漏了报 `implicit declaration of ZT_INFO`，KNOWN_ISSUES #69）。
     **第一参是字符串字面量，不是标识符**：宏体是
     `printk("INFO %s: " fmt "\n", name, ##__VA_ARGS__)` —— `name` 落在 `%s` 的位置参数上，
     属**表达式**而非名字拼接 ⇒ 写 `ZT_INFO(chans_tested, "n=%u", n)` 会被 C 当变量引用，
     报 `'chans_tested' undeclared; did you mean '<suite>_chans_tested'?`。**一律写
     `ZT_INFO("chans_tested", "n=%u", n)`**（实测 12 处同错一次暴露）。同类的 `zassert_*`
     名字参不受影响。
     **例外：`rtc_ls_test` 是唯一未迁移 glue 的工程**，其新用例走自带的 `rtc_begin/rtc_finish`
     + `enum rtc_test_id` + `rtc_test_names[]` 三处同步，不能用 `ZT_INFO`
   - CMakeLists 注入 `../../../common/ztest_glue.c` + `../../../common/
     test_results.c` + include dir（工程在 projects/ 下）；prj.conf 必带
     **`CONFIG_ZTEST_VERBOSE_OUTPUT=y`**（`KNOWN_ISSUES.md` #26，=n 则逐项输出被抑制）
   - **libc 三件套必配（全工程统一）**：`CONFIG_PICOLIBC=y` +
     `CONFIG_NEWLIB_LIBC=n` + `CONFIG_MINIMAL_LIBC=n`。`NEWLIB_LIBC=y` 与
     Zephyr POSIX types 冲突（`pthread_cond_t` 重定义，构建失败）；缺
     `PICOLIBC=y` 则落到 minimal libc，它不含 `mktime` 等 POSIX 函数，
     驱动/共享层一旦调用即构建失败（KNOWN_ISSUES #58）
   - `dump_results.py` 的 TEST_NAMES 按**链接序**（SORT_BY_NAME，`KNOWN_ISSUES.md` #27）
     与 glue 记账 idx 对齐：`nm --numeric-sort <elf> | grep
     _ztest_unit_test__<suite>__`
5. （已删除旧 SUMMARY token 流程——完成标记 = ztest 原生
   `PROJECT EXECUTION SUCCESSFUL|FAILED`）
   **⚠ 不要在 app 里再自打完成标记**：ztest 提供强 `main()`（`ztest.c:1458`）会调 `end_report()`
   打一条 `PROJECT EXECUTION …`；若 app 的 `test_main()` 末尾也 `printk("PROJECT EXECUTION %s", …)`
   ⇒ 同一串口**两条并存**（各占一行、中间夹 `====` 分隔行），且固件不调 `ztest_run_all` 时 ztest
   那条不读 stats、**恒 SUCCESSFUL（空跑）** ⇒ 解析"取最后一条"会把 FAIL 读成 PASS（实测
   `i3c_i2c_switch_test`，KNOWN_ISSUES #98 前半）。完成标记只保留 ztest 原生一条；确需 app 自报
   状态就改走 `ZT_INFO`/`LOG_INF`，**别复用该字面量**。
6. **log 输出规范（★ 2026-08-29 用户要求，所有固件必须遵守）**：驱动
   log/token 输出统一走 `LOG_INF`（不用 printk），prj.conf 必带：
   `CONFIG_LOG=y`、`CONFIG_LOG_MODE_DEFERRED=y`（buf 模式）、
   `CONFIG_LOG_BUFFER_SIZE=16384`、`CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=10`、
   `CONFIG_LOG_BACKEND_UART=y`、`CONFIG_LOG_PRINTK=n`。
   **除了必要卡时序的地方**：ISR 内禁 log（`SPEC_REFERENCE.md` §6.1.2）、时序测量段不打
   log（`SPEC_REFERENCE.md` §6.1.3）不变。理由：消息进 log buffer 由 log 线程异步输出，
   测试线程不被 UART 阻塞、消息不丢。
   （ztest/glue 输出走 printk 直出，与 LOG 并存互不干扰）
   ⚠ **"消息不丢"只对正常速率成立**：deferred 下 ring 由 log 线程异步排空，
   115200 下 150 字节约 13 ms；**循环里连发大块报告**（如 75×75 邻接矩阵 ~20 KB、
   565 行矩阵 + ASYM）会**撑满 ring、超出部分被静默丢弃**（表现为整行丢失、多行被
   合并到同一行、中文成片乱码 —— "最需要证据时报告最像好消息"的同族坑）。
   ⇒ **单次输出 >几 KB 的报告类用例必须逐行给 log 线程留间隔**（`k_msleep(20)` 量级，
   **间隔按段长动态调**：固定 20 ms 对 200+ 行段仍不够）；缓冲放 `.bss`（`static`）
   不放线程栈，并把 `CONFIG_LOG_PROCESS_THREAD_STACK_SIZE` 提到 2048（150 字符行在
   log 线程里格式化，默认 1024 偏紧）。见 KNOWN_ISSUES #85。
7. **log 门禁判定（★ 2026-08-29 用户要求）**：等 log 有打印才开始测试；
   load 后规定时间（LOAD_WINDOW）内收不到任何 log/ztest 输出 → **直接判
   该 case 失败**，不深挖根因（正常 load 后几秒内必出 log）。见
   `SPEC_REFERENCE.md` §6.2。
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
   **模板一律显式写满 6 个形参并加注释**标明哪个是 after（用例级）、哪个是 teardown
   （套件级）——按位置读代码极易把两者看反（实测 `wdt_test` 两个套件
   `ZTEST_SUITE(..., NULL, NULL, teardown)` 与 `ZTEST_SUITE(..., NULL, teardown, NULL)`
   隔离性语义**相反**却都能跑通）。
9. **测试项命名不得与驱动导出符号撞型（ZTEST 实体函数名 = `<suite>_<name>`）**：
   `ZTEST(suite, name)` 生成的 C 函数名是 **`<suite>_<name>`**（注册段名才用裸 `name`）
   ⇒ suite 用驱动短前缀（`spif`/`i2cf`/`spid`）时，`ZTEST(spif, clk_check_config)` 会与驱动
   `spif_clk_check_config(...)` **函数名重定义 / 参数不符**（编译报 `conflicting types` /
   `too many arguments`）。约束：**`<suite>_<name>` 不得等于任何被测驱动的导出符号名**
   （改用 `sck_threshold_config` 这类不含 suite 前缀的语义名）。见 ref-gen-errors.md。
10. **扫描类用例必须带"还原自检"闸（★ 写出矩阵/邻接表类报告前必做）**：
   任何"逐脚设为输出低 → 逐脚读回"的扫描（邻接/连通矩阵），必须先证明**驱动过的脚能还原**
   —— 每脚驱动后还原、再读回自身，必须回到基线电平，否则 `LOG_ERR` + **单独 FAIL 闸**
   （命名如 `pin_restorable`，与被测项并列）。**缺该闸时矩阵不可用于任何接线结论**：
   实测 `io_probe_test` 首跑因还原走了**不生效的路径**（`gpio_pin_configure(GPIO_INPUT|GPIO_PULL_UP)`
   撤不掉输出使能——HAL 撤销写错寄存器半区，见 KNOWN_ISSUES #93）未把脚拉回高电平，
   产出**完美下三角**（第 i 行对全部 j<i 都是 `#`，`links=2862` 已超 75 脚上限 2775）
   —— 只有上三角才是真实信息。三条硬要求：
   - **还原走能真正撤销的路径 + 读回验证**（例：先 `GPIO_DISCONNECTED` 撤销输出使能，再配
     `GPIO_INPUT|GPIO_PULL_UP`）；失败即从"可驱动集"**永久剔除**，统计里视为**无证据**（不是"连通"）。
   - **基线门**：只有"**基线为高、本行读低**"才构成一条连线证据；基线为低的脚**不产生任何证据**
     （不是标个 `?` 就完事），单列 `BASELINE_LOW` 一节。
   - **不变量硬闸必须同时具备**：连通计数 ≤ `N*(N-1)/2`（无向对上限）+ 对称性检查；
     **阳性对照不能替代**不变量自检（实测阳性对照 PASS 而整张矩阵已坏）。
   见 KNOWN_ISSUES #86 / #93 / #94；招式入库 `debug-knowledge` common 域
   `probe-scan-selfcheck-invariants.md`。

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
    （残留 2os 常量会误导后续改动）；`date_str` **必须在构造 `DualRunSpec(session=…)` 之前赋值**
    —— 闭包在赋值前捕获它时，**不带 `--session-dir`** 会抛 `NameError: name 'date_str' is not defined`
    （带参则正常，极隐蔽；静态判据 = AST 首次 `Assign` 行号 ≤ 首次 `Load` 行号，见 ref-gen-errors）
  - `test.sh`：`TEST_NAME` + 头注释各字段（§2）；**board 一律走 `SINGLE_BOARD`/`TARGET_BOARD`
    变量（或 `build_single_test`），禁硬编码 `lsqsh_evb@...`**——硬编码 2os 板级却走单核
    `load_and_run_single`（`$pc`=1os 入口）即与入口常量错配的**同族坑**（KNOWN_ISSUES #57）
  范本 = 已迁移单核工程（otbn_test / otbn_selftest / mbedtls_test）。
  - `dbg_run.py` **capture 轮必须持续排空 gdb stdout**（★ 2026-09-19，模板硬要求）：
    `Popen(gdb, stdout=subprocess.PIPE)` 后整个采集窗**不读该管道** ⇒ Windows 管道缓冲写满 ⇒
    gdb 阻塞在写 stdout ⇒ DebugServer 会话停摆、目标中途 halt ⇒ **长用例尾部证据永远采不到**
    （串口只剩 boot banner + `START - <name>`，SUMMARY 恒 `ZT_BOOT seen`），而 RAM 里结果其实正确。
    模板固定写 **drain 线程**（`for ln in proc.stdout: gdb_out.append(ln)`）+ **`proc.wait()`**，
    禁用"窗口结束才 `communicate()`"。见 KNOWN_ISSUES #97 / qsh-test-run §3。
  - `dbg_run.py` **prime / 单遍轮收尾也不得用 `communicate(timeout=)`**（★ 2026-09-20）：
    `Popen.communicate()` 在 `input=None` 时**会 close stdin 并置 `self.stdin=None`** ⇒ 超时分支里
    `proc.stdin.write("\x03")` 抛 `ValueError: I/O operation on closed file` ⇒ 落进 `except: proc.kill()`
    ⇒ **gdb 被 kill（非 interrupt+detach）、留连接残留**（违反 qsh-debug §2.3）。实测
    `dbg_run.py::flash_once` 的 prime 轮曾即此缺陷（**capture 轮用 drain 线程 + 定时写 stdin，是对的**）。
    **模板现状（2026-09-20 已修，23 份 live 模板）：prime 轮也走 drain 线程 + `proc.wait(timeout=…)`
    + 定时写 `\x03` + `detach`**；新工程照抄，别回退。见 KNOWN_ISSUES #101 / qsh-debug §2.3 / ref-gen-errors。
  - `dbg_run.py` **输出编码兜底**（★ 实测中断采集）：`--raw` 逐行 `print(ln)` 串口原始行时，
    Windows 控制台默认编码可能是 GBK，固件日志只要含非 GBK 字符（如 `↔` U+2194）即抛
    `UnicodeEncodeError`、**脚本当场退出 rc=1，后续 RAW 输出全丢**（SUMMARY 因先打印而幸存）。
    模板统一在启动时 `sys.stdout.reconfigure(encoding="utf-8", errors="replace")`，或把
    `print` 包在 `try/except UnicodeEncodeError`。见 KNOWN_ISSUES #91。
  - `dbg_run.py` **加载一律内联 gdb，禁委托 `<case>/test.sh -r`**（★ 2026-09-19）：委托版把
    `TEST_BUILD_DIR` 钉在工程内、`TEST_ROOT` 从 `test.sh` 自身位置反推 ⇒ **构建目录无法隔离**
    ——想"构建到工程外"就得把 `test.sh` 与源目录一起镜像，否则 `bash: test.sh: No such file`
    秒退（0 行、`boot=False`）。`app_iwdg_cpu2_test` 是现存的唯一委托版实例；需隔离构建时把产物
    stage 到镜像，改用**内联 gdb 的通用 `dbg_run.py`** 驱动（cpu1 `0x100c0000` / cpu2 `0x10000000`
    同址）。当前**未**提供 `CASE_SCRIPT` env 覆盖口（与 O-14 构建注入开关同族，待定）；建议把
    该项目脚本对齐本模板。
- **凡 `Popen(stdout=PIPE)` 都必须持续排空（★ 2026-09-20，判据见 `SPEC_REFERENCE` §6.11.1）**：
  不止 `dbg_run.py` —— `common/**` / `regression/**` / 各 `tests/scripts/**` 全在范围内。
  同一函数内 `Popen(stdout=PIPE)` 后整段窗口不读该管道 ⇒ 缓冲写满 ⇒ 子进程写 stdout 阻塞
  （gdb 卡住即 DebugServer 会话停摆）；收尾用 `proc.wait()`，**禁用 `communicate()` 驱动**
  （它会 close stdin，见 qsh-debug §2.3）。**新增任何 `Popen` 后跑一次防线脚本
  `tmp/b2fix/scan_pipes.py`**（扫 LIVE 调用点，非零退出即缺陷）。
- **`test_common.sh` 的三个构建注入开关（2026-09-18 起可用，**勿再自建 `west build`**）**：
  `build_test`/`build_single_test` 新增 **`EXTRA_CMAKE_ARGS`**（任意 `-D` 透传）、
  **`BOARD_OVERRIDE`**（跳过 `_detect_board` 的目录名判板）、**`CONF_MODE=replace`**（只用第 4 参
  conf、不拼 `<src>/prj.conf`）三个 env 开关（默认全空 ⇒ 既有调用路径逐字节不变）。
  #70「无 `-D` 透传口」与 #76「无法整体替换上游 conf」两个痛点已解 ⇒ **新工程直接用开关**，
  不要每 case 自建 `west build` 片段（历史上的自建函数是开关出现前的权宜做法，且极易照抄
  硬编码 `-b lsqsh_evb@...`，正是 §1.5 禁的）。
- **`build_single_test` 的 `CONF_FILE` 是"追加"语义，不能表达"整体替换"**（复用上游 test/sample 时必看）：
  它在 `<src>/prj.conf` 存在时拼 `-DCONF_FILE=<src>/prj.conf;<conf>`。**复用上游 test/sample**
  且需 `-DCONF_FILE=<上游非 prj.conf>;<增量>`（整体替换，上游 `testcase.yaml` 的语义）时**不能用它**——
  否则带回上游 `prj.conf` 的错误分型 → 编译失败。改用本工程 `case_*/test.sh` 直接调
  `west build -b "${SINGLE_BOARD}" <src> -DCONF_FILE="<上游 conf>;<810 增量 conf>"`（仍走
  `${SINGLE_BOARD}` 与 `print_launch_json_single`/`load_and_run_single`，不硬编码板级）。
  见 qsh-test-validate §1.9j / KNOWN_ISSUES #76。
- **未注册模块就地注册（免 CLI 参数）**：模块未进 `zephyr/west.yml` 时，在应用 `CMakeLists.txt` 的
  `cmake_minimum_required` 之后、`find_package(Zephyr)` **之前** `set(EXTRA_ZEPHYR_MODULES "<绝对路径>")`
  （`zephyr_module.cmake:39` 此刻读该变量；**放之后无效**）——`test.sh`/`run_test.py`/手工编译三路径
  免传参、自动一致。见 KNOWN_ISSUES #77。

### 1.6 覆盖矩阵 + 预期数据 + 回归脚本（`SPEC_REFERENCE.md` §6.4/§6.5，★ 必做）

> **★ 分层现状（2026-09-20 起；写 run_test.py 前先读 `FRAMEWORK_MAP.md` §1）**：采集/处理已从各
> `run_test.py` 提到公共层 —— **L1 `common/acquire.py`**（唯一碰板子；`AcquireSpec` 家族 **5 个采集策略**，
> 「按工程形态选哪个」见 `SPEC_REFERENCE.md` §6.4.10 策略表）+ **L2 `common/process.py`**（纯离线；
> 工程唯一接入点 = 模块级 **`l2_spec()` → `ProcessSpec`**，含 `judge`/`console`/`render_md`/`render_json`/
> `summary` **五钩子**）。**新工程 = 薄壳**：选一个采集策略 + 写 `l2_spec()`，**不再**在工程内自实现
> 采集/解析/渲染（"每份 `run_test.py` 自己实现"的旧世界已结束）。契约与"改哪层验什么"见
> §6.4.8 / §6.4.9 / §6.4.10。
1. **COVERAGE.md**：对着被测驱动源码逐路径列测试项（每个条件分支至少一个
   测试触及），按 `SPEC_REFERENCE.md` §6.5.3 模板填：`测试项 / 驱动路径 / 维度 / 预期数据 / 预期来源 /
   状态`。维度从 `SPEC_REFERENCE.md` §6.5.1 清单选（功能通路/边界长度/异常参数/统计性质/并发/
   中断时序/复位掉电）。**不得拍脑袋**：先读驱动源码（`zephyr/drivers/` 对应
   `.c`），把分支点抄进矩阵再设计用例。
   **边界用例的常量必须有实测来源**：驱动掩码/数据手册，或（两者与实机不符时）
   寄存器**写-回读实测上限**——凭"看起来安全"拍的值会在硬件量程处静默截断
   （debug-knowledge mcu 域"寄存器位域以实测为准"）。
   **缩容 / 缩短超时 / 加兜底钩子 ≠ 修复**：根因未解时大小两个值同因同果，
   兜底钩子在前置条件不满足时还会空转，见 qsh-test-validate §1.9h。
   **预期数据列里的竖线必须转义**：COVERAGE.md 单元格内容出现**裸竖线**（绝对值差式如 `z = |V−2nπ|/…`、"或"写法如 `` `a|b` ``）会让该行**多切出一列**，整张表被撕坏、下游解析把一行撕成两半（实测值跑到期望列）。写成转义形式（反斜杠 + 竖线），或改写为等值表达式（如 `abs(V−2nπ)`）。`judge.coverage_expected()` 已按尾两列锚定回拼容错、报告不受影响，但**源文件仍坏**⇒ 校验侧判 FAIL（qsh-test-validate §1.10t）。
   **已知会污染实机寄存器的驱动入口不得用**：读源码时顺带确认入口安全性——如 pfr_tpm 的
   `spif_add_cmd*` 命中"命令已在固定表"分支会用过期索引**表外野写**，改用 `spif_add_general_cmd`
   并以 `spif_cmd_in_fixed_table()` 显式断言命令不在固定表内；用这类入口，断言成功也**不代表行为正确**，
   反而污染实机寄存器（KNOWN_ISSUES #78）。
   另：读源码时同步确认**器件运行态** —— 裸 `spi_nor_op` 类用例必须适配 `spi-ctrl-caps-mask`
   （**disable 掩码**，bit18 关 4-4-4）+ 容量决定的 4 字节地址，且**必须断言回读内容**
   （只断 `ret == 0` 是假 PASS）。见 KNOWN_ISSUES #84。
   **COVERAGE.md 末尾必带 `## 独立预期（机器可读）` 段（expect 围栏，★ 2026-09-21）**：逐项登记
   **不由被测代码算出**的独立断言，无确定预期的项显式 `!none` + 理由。文法 / op 表 / 三档来源标签 /
   两条纪律见 `SPEC_REFERENCE.md` **§6.5.6**；生成后跑 `python common/expect_check.py --case <工程>:<case>`
   （或 `--audit`）自检。**只有能落到单个标量的预期才登记断言**，多判据合取 / 非标量一律 `!none` —— 不得"编预期"。
2. **expected/**：确定性测试（通信数据/状态字/加密向量）生成预期数据文件，
   固件 include 后逐字节比对：
   - 加密类 → 抽 NIST/官方测试向量生成 `expected/nist_vectors.h`
   - 通信类 → 固定 pattern + 状态字（预期字节序列/寄存器值）放 expected/
   - 生成脚本随工程保留（如 `tests/scripts/gen_vectors.py`），保证可复现
3. **run_test.py** 必须实现（**薄壳**：采集走 `acquire.py`、处理走 `process.py`，下面列的是它必须满足的输出契约；样板见任一已迁移工程）：
   - **解析层用公共 `common/ztest_report.py` `parse_ztest`**（勿自写解析器），
     判定按 `SPEC_REFERENCE.md` §6.2：单核 = 全项 `PASS - <name>` + `PROJECT EXECUTION
     SUCCESSFUL`；双核 = 同名项双核 PASS + 两核 SUCCESSFUL
     （例外：i3c 工程 TGT 侧无 PROJECT EXECUTION，判定 = CTRL SUCCESSFUL
     且 TGT≠FAILED，见 `SPEC_REFERENCE.md` §6.2 双核例外）
   - **`SPEC_REFERENCE.md` §6.2.3 批量回归判据**：两遍烧录（prime 判"任何 ztest 输出"、pass-2 判
     ZT_BOOT）、20s 无 ZT_BOOT=环境问题中止、BOOT 后 10s 静默 + 完成标记
     （任一核 `PROJECT EXECUTION ...`）= 结束提前杀、串口实时采集、
     行解析 `re.search()` 不锚点、每 case 前后 DebugServer 自愈（`KNOWN_ISSUES.md` #17）、
     构建全量 case 后回归（`KNOWN_ISSUES.md` #18）
   - **归档用公共 `common/report_dir.py`**（`allocate_session_dir` /
     `case_dir_for` / `update_summary`，禁止自算路径）：报告写到会话目录
     `reports/<YYYY-MM-DD>-<idx>/<project>_<case>/<driver>_test_report.{md,json}`
   - **归档必须同时写 `.md` + `.json`**（样板 = 已迁移工程的
     `write_markdown_report()` + json writer；`.md` 用 items[] 渲染、
     `.json` 为机器可读源）；**漏写 .md 属模板缺陷**，summary.md 会把
     该 case 链到 .json、偏离 §6.4.1
   - summary.md 只在会话目录最外层，**行粒度 = case**（key
     `<project>_<case>`，`SPEC_REFERENCE.md` §6.4.4）
   - 解析 `INFO <name>: k=v[,k=v]` 约定行填报告"实际"列（`SPEC_REFERENCE.md` §6.5.4）
   - **报告头的"接线/器件"描述行必须与实测配置一致**（换址/换接线/换从设备后同步更新）——
     实测：换址后报告头仍写"外接 EEPROM@0x50"，与实测（片内 target `eeprom@33`）不符，报告**误导归因**；
     已生成的旧报告即**陈旧产物**，归因前先核新鲜度（`KNOWN_ISSUES.md` #88）。
   - 串口缺项用 `dump_results.py --json` 内存兜底合并（`SPEC_REFERENCE.md` §6.4.3；TEST_NAMES
     按链接序对齐 `KNOWN_ISSUES.md` #27）
4. **判据有效性 ——"判据三问"（★ 写任何断言前自问）**：(a) 驱动**正确**时这条一定过吗？
   (b) 驱动**典型失效**时这条一定**不过**吗？(c) 这条会不会因别的用例跑过/没跑过而改变结果？
   **三问答不全的断言不允许写入**。典型反例（实测族）：驱动 API **无条件 `return 0`**
   （如 `i2c_filter.c:69-80`/`:263-290`、`spid_linkedsemi.c:66-77`/`:148-156`）时
   `zassert_ok(该返回值)` **恒过**；纯零断言（只有 `ARG_UNUSED(ret)`）；"自洽式假断言"
   （断言之物由构造方式保证）。**判据必须独立于驱动自身的返回值** —— 从**可观测副作用**取：
   寄存器回读、`dev->data` 内部字段、`IO_FUNC` 复用位、中断使能位等。**确实无判别力时如实标注
   并降级为 `ZT_INFO`**（宁缺勿假）。
   **测量类用例（扫描/邻接/连通矩阵）额外追加一问**：(d) 这条断言依赖的**状态改写是否已读回
   验证**？建立在"以为生效、其实没生效"的状态上时，整组结果无效且**被级联污染成假阳性**——
   阳性对照 PASS 也救不了（见 §1.4 第 10 条 / KNOWN_ISSUES #93；招式见 `debug-knowledge`
   common 域 `probe-scan-selfcheck-invariants.md`）。
   **破坏性/跨复位 case 再追加一问**：(e) 判据是否依赖"采集窗口内看到现象"？窗口若按**标称超时**
   设（如 WDT 2s），延迟远大于标称的真事件会被误判成"没发生"（sec_iwdg 标称 2s、实测可达 ~34s）
   ⇒ 判据取**跨复位 `__noinit` 终态**（如 `test_phase==2`），或窗口 ≥60s（KNOWN_ISSUES #100）。
   **地址类用例（总线寻址/扫描/回环/EEPROM）再追加一问**：(f) 被测地址上**是否只有 DUT 一个应答者**？
   器件与板上既有器件**同址**时读写由"并联体"应答 ⇒ **任何 PASS/FAIL 都不可归因**（PASS 也可能来自邻居）。
   选地址**主动避开该总线的惯例地址段**（I2C EEPROM `0x50`~`0x57`），并在改址前**实测确认该地址无人应答**；
   怀疑撞址时把 DUT **挪到"改址前扫描不到"的地址再复测**（重扫看 `count`/`addrs` 两个量，辅助证据 =
   DUT 自身地址寄存器，如 target `OAR1`）。招式见 `debug-knowledge` mcu 域
   `bus-addr-collision-pass-not-attributable.md`；实例 KNOWN_ISSUES #110/#111。
5. **用例自包含（硬规则）**：任何依赖"前一项留下的状态"的用例，必须改为**在自身内部构造该状态**
   （反例：`emmc` 的 `request_mutex_not_leaked` 靠前一项已泄漏锁才会阻塞 ⇒ 单独跑即假 PASS）。
6. **分组准则：按"是否需要外部条件"切 case，不是按被测对象切**：case 是**屏蔽/启用的最小粒度**
   （`cases.yaml` 行粒度 = case），一个 case 内只要有一项需要外部条件，**整 case 就被屏蔽**
   ⇒ 同 case 内无外部条件的项（含**弱判据**，如"只读 PHY 状态、无对端也能确定判定"）连带
   失去默认覆盖。⇒ 无外部条件的项归入默认启用的 A 组；需外部条件的项**单独成 case 并屏蔽**。
7. **白盒观测：允许复刻驱动私有结构体做只读视图，但有三条纪律**：(i) 注明对应驱动结构体的
   file:line；(ii) **只断显式赋值过的字段** —— 寄存器结构体常是
   `union { uint32_t value; struct { … } }` 且**未清零**（如 `spif_cmd_t`，`spi_filter.c:881-883`），
   比整字会把栈垃圾读成差异 → **假 FAIL**；(iii) 注释说明"**布局漂移会 FAIL 而非假 PASS**"
   （这正是它比黑盒断言强的地方）。
9. **"前置外部条件未满足"的降级口径必须统一（★ 2026-09-21）**：依赖**外部接线/对端**的项，前置条件
   不满足时**一律 `ztest_test_skip()` 并在 COVERAGE 标注**，**不得硬 FAIL** —— 同一工程内对同一物理
   前置条件两种口径会造成分母/结果自相矛盾。反例（实测）：`spi_test` 的 7 个 roundtrip 项走
   `ztest_test_skip()`（不计分母），而同工程 `link_probe` 对同一"SPI1↔SPI3 跳线不通"**硬断言 FAIL**
   （`zassert_equal(lb_link_state, LB_LINK_OK, …)`）；正例：`i2cf_test` 探测不到对端一律 `ztest_test_skip()`
   （"扫描不到就 skip，不算失败"）。确需保留硬判据时，按第 6 条把强弱判据切成**不同 case**（无外部条件的
   项入默认启用组）。检查项见 qsh-test-validate §1.10v。
8. **双核 `dump_results.py` 的内存解析必须按「位置」切分**：`parse_x_bytes(out, N)` 每次都从
   gdb 输出**顶部**扫 ⇒ 对 CTRL/TGT 各调一次会**返回同一段**（TGT 的值实际是 CTRL 的，
   两核恰好一致时才看不出来）。必须按读取顺序位置切分（并容纳新增的哨兵字节）。

### 1.7 以太网类工程 prj.conf 补充（eth_dwmac 配置耦合，★ 生成时必带）

被测驱动涉及 `eth_dwmac*.c` / `mdio_dwmac.c` / 板载 PHY 的工程，prj.conf 除 §1.3/§1.4
通用配置外**必须**额外带下列项，否则**必然**编译或链接失败：

```kconfig
# ① eth_dwmac.c:73-75 的 RX refill 线程优先级取 CONFIG_NET_TCP_WORKER_PRIO-1，
#    NET_TCP=n 时该符号未定义 → 编译报 'CONFIG_NET_TCP_WORKER_PRIO' undeclared
CONFIG_NET_TCP=y

# ② eth_dwmac.c:832 在文件末尾**无条件** SHELL_CMD_ARG_REGISTER（无 #if 守卫）。
#    该宏在 CONFIG_SHELL=n 下不是空宏（把 entry 塞进 shell_root_cmds 段、handler 被强引用），
#    handler 调 shell_error()→shell_fprintf_error()，该符号只在 SHELL=y 时编入
#    ⇒ SHELL=n 必然 `undefined reference to 'shell_fprintf_error'`。
#    折中：开 SHELL 拿符号，但关串口 backend、只留 dummy —— shell 线程无传输通道，
#    不产生提示符、不占 uart1 RX，ztest 串口流不受污染。
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=n
CONFIG_SHELL_BACKEND_DUMMY=y
```

两个都是**驱动侧配置耦合缺陷**，与具体工程无关（任何带 eth1 的工程都会踩），
故不能靠"只改本工程"绕过。另：`mdio1` 的 init 挂在 `CONFIG_NETWORKING_AUTO_INIT` 下，
**网络栈必须打开**，否则 MDIO 事务必然超时。参考实现 = `projects/eth_test/cpu1_eth_test/prj.conf`
（含逐条注释）。已登记 KNOWN_ISSUES #72。

**网络配置类**：ztest 用 `CONFIG_NET_CONFIG_SETTINGS=y` 配静态 IPv4 时**必须同时设
`CONFIG_NET_CONFIG_INIT_TIMEOUT=0`** —— `net_config_init_by_iface()` 在网口未 up 时
`while (count-- > 0) k_sem_take(...)` 等链路（`loop = timeout/10`），**默认 30 ⇒ 最坏阻塞 30 s**，
把 case 超时窗口吃掉（表现为"load 后很久才有输出"或直接判 no_log）；设 0 则等待循环整体跳过、
静态地址仍立即写入。见 KNOWN_ISSUES #89。

**ARP 判据**：Zephyr ≥4.0 **无公开 ARP 表查询 API**（本树无 `net/arp.h`、无 `net_arp_lookup`）
⇒ ARP 用例只能以**回包源 IPv4 == 对端地址**做等价覆盖（ARP 未解析成功则收不到该对端回包）。
见 KNOWN_ISSUES #90。

> 这是 §1.4 第 6 条"810 惯例 `CONFIG_SHELL=n`"的**唯一例外**：只有以太网类工程开 SHELL。

### 1.8 外设使能类 overlay 模板要点（DTS/驱动耦合的硬约束）

写 overlay 使能某个外设实例前，先确认三件事（全部来自实测踩坑，见 KNOWN_ISSUES #81-#83）：

1. **实例归属看 `compatible`，不看实例号连续性**：`spi1/spi2` 是 `linkedsemi,ls-spi`
   （`spi_ls.c`），`spi3/spi4` 是 `snps,designware-spi`（`spi_dw.c`）——同前缀不同驱动。
   SPI↔SPIF 配对：`spif1/2 → spi3`、`spif3 → spi4`（pfr 的 `spi_master_communication`
   走 DW SPI，与 `spi_ls.c` 无关）。
2. **DW SPI（`spi3`/`spi4`）使能必须同时带两行**（缺一即 `west build` rc=1）：
   ```dts
   &dmac2 { status = "okay"; };          /* spi_dw.c:2221 要求 dmas 控制器已使能，
                                            且 CONFIG_DMA_DW_LLI_POOL_SIZE 只在 DW DMA 编入时有定义 */
   &spi3  { status = "okay"; timing-calibration-disabled; };
                                         /* spi_dw.c:467-469 幅度校准数组由 DT 子节点生成，
                                            无子节点 ⇒ 空数组 ⇒ 越界读 */
   ```
   回环本身走 `spi_dw.c:692-817` 通用 IRQ 通路，不需要 DMA。
3. **"配对控制器"必须让渡引脚**（不解除**不报错、只静默无波形**）：
   - pfr_tpm 模块给 `i2c1..i2c4` 标了 `pinctrl-noinit`（配对控制器，引脚默认归滤波器）；
     把某个 `i2cN` 挪作上游 master 时，本工程 overlay 须
     `&i2cN { status = "okay"; /delete-property/ pinctrl-noinit; };`（改完删 build 全量重建）。
   - SPIF master 模式用 SPIF 自己的 `pinctrl-2`（master 态引脚），**配对 DW SPI 控制器**
     仍持板级 pinctrl 就是"两个 owner 抢一组 net" ⇒ overlay 对配对控制器显式
     `&spi4 { pinctrl-0 = <>; pinctrl-names = "default"; };`（参考资产 `samples/spi_master_communication`）。
   - 判据分级：**"配对控制器"由 DT 可证**（`i2cfN.i2c = <&i2cN>`，引脚重叠）；
     **"上游 master"只有接线能定**（DT 无任何属性描述）⇒ 上游 master 做成 **DT alias**
     （用例一律 `DEVICE_DT_GET(DT_ALIAS(...))`），接线变更只改 overlay 一行。
     两个角色的引脚关系不同，**不可互相推断**，更不可用后者的假设否定前者。

## 2. 生成新 case（不改 C 代码）

新 case = 新建目录 + 三件套：
```text
tests/case_<速记>/
├── test.sh          # 从样板 case 复制，改 TEST_NAME 和头注释
├── controller.overlay / controller.conf   # 差异（如 I3C 速率）
└── target.overlay / target.conf
```
共享同一份固件源码，case 差异只体现在 overlay/conf/test.sh。

**变体：case 需要既有工程没有的源集时（"只增不改"扩展既有工程）**——不动既有 case/配置/CMake
既有行，给多 case 共享固件的工程加 case：应用目录新增 `Kconfig`（Zephyr 自动把它当
`KCONFIG_ROOT`）+ 工程 `CMakeLists.txt` **尾部追加**
`if(CONFIG_<X>) target_sources(app PRIVATE src/xxx.c) endif()` + 各 case 的 `*.conf` 写
`CONFIG_<X>=<case 号>`，固件按 case 号走运行时 switch。
- `build_test` 第 4 参 `conf` 本就是"追加 conf 文件"的透传口（`scripts/test_common.sh`），无需改公共脚本。
- **落地后必须回归验证 `ID=0` 的既有 case 仍以原样构建**：新增应用目录 `Kconfig` 会改
  `KCONFIG_ROOT`，属潜在爆炸半径（一次即可暴露配置漂移）。
- 比"每个 case 复制一套 app 目录"省数十个文件，也不必改 `common/`。

## 3. 生成后自检 + 调试验证（必须做）

> **迭代期构建用 `-p auto`，不要每个 case 都走 `test.sh`**：`test.sh` 内部
> （`build_single_test`/`build_test`）固定 `-p always`（全量重建，单工程约 3.5 分钟、mbedtls
> 单 case 更久），而"验证能否编过"用 `west build -p auto -d <既有 build 目录>` 是秒级复用。
> 多 case 共享同一份源码时，先在**一个** build 目录把编译错误全部消掉，再逐 case 跑一次
> `test.sh` 做最终合规构建（`-p auto` 也会检测 `CONF_FILE`/`OVERLAY` 依赖变化自动重配，无需手动
> 删 build；SRAM 地址类 overlay 变更仍按 KNOWN_ISSUES #9 全量重建）。

1. `./test.sh` 双核编译通过（零失败）
2. 确认 DebugServer 在跑（qsh-debug §2），跑 `dbg_run.py --case <新case>`
   - load 后几秒内必须出 log/ztest 输出（无输出 → 直接判失败，见 §1.4 第 7 条）
   - banner 角色识别正确（CTRL/TGT 无 `?`）
   - 每项测试 `PASS - <name>`，双核同名项两核都 PASS
3. 单 PASS 不算：target 侧也必须出 `PASS - <name>`；双核都要
   `PROJECT EXECUTION SUCCESSFUL`（例外：i3c_dualcore 类 target test_main
   不返回的工程，TGT 无该行属设计，判 CTRL SUCCESSFUL 即可，`SPEC_REFERENCE.md` §6.2）
4. 失败时按 zassert reason 定位：`--raw` 看原始日志（`Assertion failed at
   <file>:<line> (<func>): <msg>`）→ 必要时 GDB 断点抓现场（qsh-debug §6）
5. 串口无输出/被刷掉时：`python scripts/dump_results.py --case <case> --json`
   读内存兜底取结果（`SPEC_REFERENCE.md` §6.2.2 / §6.4.3；TEST_NAMES 按链接序对齐 `KNOWN_ISSUES.md` #27）
6. 调通后：更新顶层 README.md 接线登记表、写问题总结存档、
   把新坑写进 `.claude/gotchas/INBOX.md`（沉淀归 keeper）
7. 跑一次 `run_test.py` 一键回归，确认：
   - 报告落在 `reports/<日期>-<idx>/<project>_<case>/<driver>_test_report.{md,json}`
     （会话目录），内容含预期/实际列
   - 会话目录最外层 summary.md 有本 case 行（`SPEC_REFERENCE.md` §6.4.4，key `<project>_<case>`）
   - COVERAGE.md 所有测试项最终状态为 ✅（回归后对表）
8. **注册与同步（★ 生成收尾，清单见 SPEC_REFERENCE §6.4.5）**：新工程/case 的启用
   条目写入 `regression/cases.yaml`（唯一权威源），并同步 `CLAUDE.md` §2 清单与
   `(N)`（= cases.yaml 中该工程已启用的 case 数）、`README.md` §4 工程表 +
   §「外部接线记录」、`regression/run_all.py` 的 **`PROJECT_SCRIPT`**
   （逐工程硬编码的"工程名 → run 脚本"表；未入表会打印 `[run_all] SKIP ...` 并按 SKIP 记账，
   **只改 `cases.yaml` 会静默跳过**，SPEC_REFERENCE §6.4.5 第 5 步）。
   漏同步 → 回归跑不到该 case / 清单与实际不符。
   **双核工程另有一张漏登记表**：`regression/run_all.py` 的 **`DUAL_CORE`** 集合也逐工程硬编码，
   未登记时 run_all 只传 `--tgt-uart`、双核脚本缺 `--ctrl-uart` 直接退出
   ⇒ 该工程所有 case 恒 `FAIL 0/0`（日志尾部 `ERROR: --ctrl-uart/--tgt-uart required`）。
   **新双核工程两张表都要改**（`PROJECT_SCRIPT` + `DUAL_CORE`）。

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
