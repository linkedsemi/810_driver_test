# 审核报告：新工程 `projects/rtc_timer_ls_test/`

- **日期**：2026-09-24
- **范围**：`810_driver_test/projects/rtc_timer_ls_test/` 静态合规审核 + 规范补齐
- **方法**：离线静态审核（读规范 §6.4.5 / §6.2.4 / FRAMEWORK_MAP + `qsh-test-validate` §1.x 清单逐项）。
  **未构建、未烧录、未上板**（本次任务铁律；且磁盘清理后 62 个 build 目录已删）。
- **被测**：`zephyr/drivers/rtc/rtc_timer_ls.c`（`linkedsemi,ls-rtc-timer`，BSTIM+PIS+GPTIMA 软件 RTC 链，实现在 HAL `platform.c`），15 个 ZTEST。

---

## 0. 结论速览

| 档 | 数量 | 是否阻断回归 |
|---|---|---|
| 硬伤 | 4（H1 已修，H2/H3/H4 交父会话） | 否（H1 已修后即可跑；H2–H4 影响**结果可信度**而非能否启动） |
| 建议 | 3 | 否 |
| 合规 | 见 §3 全绿 | — |

**预期首跑：15/15 PASS，case PASS**（推导见 §5）。

---

## 1. 硬伤

### H1（已修）单核工程硬编码 2os 板级 + 2os 入口 —— KNOWN_ISSUES #57 同族复发 🔴

四处同族错配（`test.sh` 一条**不在此列**，见下）：

| 文件 | 行 | 原值 | 症状 |
|---|---|---|---|
| `tests/run_test.py` | 49 | `BOARD = "lsqsh_evb@2os/lsqsh/cpu1"` | 单核工程按 2os 分型构建（违反用户需求#8） |
| `tests/run_test.py` | 48 | `TARGET_ROM_START = "0x100c0000"` | 硬编码，非 env 覆盖 |
| `tests/scripts/dbg_run.py` | 39 | `TARGET_ROM_START = "0x100c0000"` | 同上 |
| `tests/scripts/dump_results.py` | 43 | `TARGET_ROM_START = 0x100C0000`（int） | 同上 |

- **判据**（`qsh-test-validate` §1.9f）：单核 = `@1os/lsqsh/cpu1` + 入口 `0x10000000`（走 `TARGET_ROM_START` env）；
  权威矩阵 SPEC_REFERENCE §6.1.10；`KNOWN_ISSUES.md` #57 逐字描述了本症状与修法。
- **注意（比 #57 原文稍缓）**：`run_test.py` 内部 **2os 构建 + `$pc=0x100c0000` 是自洽的**，
  故它自己那条链能跑（不是典型的"串口 0 行"）；真正的错配面是 **debug 路径** ——
  `dbg_run.py` / `dump_results.py` 把 `$pc` 设到 `0x100c0000`，而 `test.sh` 走
  `build_single_test` → `SINGLE_BOARD`（1os，入口 `0x10000000`）构建 ⇒ **入口对不上 ⇒ 串口 0 行**。
  即 #57 的「同一工程内两条链路分型不一致」形态。
- **已修**（4 处）：`TARGET_ROM_START` 一律改 `os.environ.get("TARGET_ROM_START", "0x10000000")`
  （`dump_results.py` 用 `int(..., 0)`），`BOARD` 改 `@1os/lsqsh/cpu1`；`dbg_run.py` 补 `import os`。
- **未越界确认**：`tests/case_default/test.sh` **未硬编码板级** —— 它走
  `scripts/test_common.sh::build_single_test`，board 取 `${SINGLE_BOARD:-lsqsh_evb@1os/lsqsh/cpu1}`、
  `$pc` 取 `${SINGLE_ROM_START:-0x10000000}` ⇒ **合规**，无需改。

> ⚠️ 本项修完**必须全量重建**（overlay/分型都动了）；磁盘已清，下一次构建本来就是全量，无残留风险。

### H2（未修，交父会话）`run_test.py` 未接入 L1/L2 分层 —— 全库唯一 INLINE 残留 🔴

- **判据**（`qsh-test-validate` §1.10w，2026-09-18 新增）：`run_test.py` 须是薄壳并接两层 ——
  `def l2_spec` 命中（L2 = `common/process.py::ProcessSpec` 五钩子）+ 文件内出现 `acquire`（L1 = `common/acquire.py`）。
  **工程内自实现采集即 FAIL**。
- **实测**：24 份 `run_test.py` 中 **23 份为 L2-FW**；`rtc_timer_ls_test` **是唯一 INLINE 份**
  （自实现 `build_case()` / `capture_case()` / `gdb_flash_once()`，等价于 `acquire.PortsDictSpec`）。
  其 docstring 自述 "TRNG single-core regression runner"、"从 i3c_dualcore_test 样板 run_i3c_tests.py 改造"，
  属 **trng_test 时代的旧世代脚本**（早于 A2 分层迁移）。
- **直接后果**：**无 L0 原始串口归档**（§6.4.7 要求每 case 落 `serial_TGT.log` + `replay_meta.json`，
  真源 = `common/serial_archive.py::archive_serial()`）⇒ 该工程 case **无法用 `regression/replay.py` 离线重放**，
  任何"只改脚本"的改动都得重跑固件。
- **修法（不在本次授权内，属大改）**：照 `projects/spi_test/tests/run_test.py`（最新单核 Z 族薄壳样板）
  重写，L1 规格差异字段 = `src_dir="cpu1_rtc_timer_test"`、`board=BOARD`、`tgt_uart`、`timeout`、
  `target_rom_start=TARGET_ROM_START`、`build_subdir="target"`、`hold_cpu2=False`。
- **建议**：作为独立任务派给 runner（与 H3 一起做，一次重写即可同时消掉 H2/H3）。

### H3（未修，交父会话）判据放宽：mem 兜底值被计为证据 —— 违反 §6.2.4 冻结总表 🔴

- **判据**（`SPEC_REFERENCE.md` §6.2.4 **G10 / R8**，冻结，2026-09-20 用户裁定「B 严」）：
  **任一项只要其 PASS/FAIL/NOT_RUN 来自 mem 兜底，该项即非证据** —— 落 `MEM_NOEVID`，
  **不进 `passed`、不进 `ran`**，该 case **不得判 PASS**。
- **实测**（`run_test.py:230-246`）：

  ```python
  if mem == "PASS":
      st, note = PASS, "src=mem"          # ← mem 值直接当 PASS 证据
  ```

  即 **旧世代判定语义**（R8 之前）。全库其余 13 份有 mem 通道的脚本均已挂 `common/judge.py`
  的 R8 原语（`MEM_NOEVID` / `evidence_gaps()` / `mem_weak_note()`）。
- **失效模式**：串口丢行（如 load 后窗口偏紧）时，零 token 的项被 mem 补成 PASS 并计入分子分母
  ⇒ **假 PASS**。这正是 R7/R8 要堵的形态，且本工程**无 L0 归档**（H2）⇒ 事后也难离线证伪。
- **附**：`case_ok = (passed == ran and ran > 0 and execution == "SUCCESSFUL")` 亦为旧公式
  （缺 §6.2.4 G5 截断接受 / G8 相反标记 / 零证据守卫）。**健康路径（15 项 token 齐 + SUCCESSFUL）
  结论与现行公式一致**，故我不判它"结果必错"，只判它"**失败/半残路径下会放宽**"。
- **注意铁律**：`§6.2.4` 是**冻结**的判定唯一真源，**改判定语义需用户显式裁定**；
  本项的正确动作是"把该脚本**收敛到**冻结表"，**不是**改表。

### H4（未修，交父会话）脚本用 `communicate()` 收尾 —— KNOWN_ISSUES #101 🟠

- **判据**（`qsh-test-validate` §1.10u）：`grep -nE '\.communicate\(' <工程>/tests/scripts/dbg_run.py` 命中 → FAIL。
- **实测**：`dbg_run.py` **5 处**命中（`:152/:160/:163/:194/:197`）；`run_test.py::gdb_flash_once` **3 处**（`:134/:140/:141`）。
  全库 23 份 live 模板 **0 命中**。
- **后果**：`Popen.communicate()` 在 `input=None` 时 close stdin 并置 `self.stdin=None`
  ⇒ 超时分支的 `proc.stdin.write("\x03")` 抛 `ValueError` ⇒ 落 `except: proc.kill()`
  ⇒ **gdb 被 kill、留连接残留**（下次 `target remote` 能连但 `reset`/`load` 失效）。
- **修法**：模板须 **drain 线程 + `proc.wait()` + 定时写 `\x03`/`detach`**。
  与 H2/H3 同属"旧世代脚本"一条根因，**建议一次重写合并修掉**。

---

## 2. 建议（WARN，非阻断）

| # | 项 | 判据 | 说明 |
|---|---|---|---|
| S1 | 样板残留文案 | §1.9g | `run_test.py` docstring 自述 "TRNG single-core regression runner"、归档路径写成 `trng_test_report.{md,json}`；`dbg_run.py:31` 注释 "parents[1] = **trng_test**/tests"。**判据是"以他工程名标识本工程/本文件"即 FAIL**（标注"从 X 样板改造"的出处引用不算）。本项**功能性无风险**（`TEST_NAMES`/`DRIVER_NAME`/`REPORT_BASE` 均已正确更新，不存在 #59 的残留 TEST_NAMES → 假 PASS），**只是注释误导**。纯文案改动，本次遵从"别多改"未动，建议随 H2 重写一并消除。 |
| S2 | `prj.conf` 缺 `CONFIG_LOG_BACKEND_UART=y` / `CONFIG_LOG_PRINTK=n` | §1.3 | 清单列了这两项。**但** §1.3 明文"部分配置可能在 board defconfig 中，prj.conf 未显式写不算错"；最近的同类工程 `rtc_ls_test/cpu1_rtc_test/prj.conf` **同样不带**这两项且回归正常 ⇒ 判**建议**不判 FAIL。若要补，注意 `LOG_PRINTK=n` 会让 `ZT_INFO`（走 `printk`）失效 —— **本工程 15 项里有 3 项靠 `ZT_INFO` 上报数值**，补前须先确认 `ZT_INFO` 通道仍在。 |
| S3 | `prj.conf` 的 `CONFIG_RTC_TIMER_LS=y` 冗余 | — | Kconfig（`zephyr/drivers/rtc/Kconfig.ls:8`）为 `default y` + `depends on DT_HAS_LINKEDSEMI_LS_RTC_TIMER_ENABLED`，overlay 使能节点后自动为 y。显式写**无害**，保留亦可。 |

---

## 3. 合规项（逐条核过，全绿）

**§1.1 目录结构** — `tests/` ✅ · `tests/case_default/test.sh` ✅ · `tests/run_test.py` ✅ ·
`tests/scripts/{dump_results.py,dbg_run.py}` ✅ · `tests/COVERAGE.md` ✅ · `cpu1_rtc_timer_test/` ✅ ·
无第二固件目录（单核）✅ · `common/test_results.c` + `ztest_glue.c` 经 `common_sources.cmake` 注入 ✅

**§1.2 命名** — 工程名 `rtc_timer_ls_test` ✅ · case 名 `case_default` ✅ · 固件目录 `cpu1_<driver>` ✅ ·
**ZTEST 名撞驱动符号**：`ZTEST(rtc_timer, <name>)` 展开为 `rtc_timer_<name>`，
驱动/HAL 导出为 `rtc_timer_init` / `rtc_timer_set_time` / `rtc_timer_get_time`
—— **15 个用例名与三者均不重合** ✅

**§1.3 prj.conf** — `CONFIG_ZTEST_VERBOSE_OUTPUT=y` ✅ · `CONFIG_LOG=y` + `LOG_MODE_DEFERRED=y` +
`LOG_BUFFER_SIZE=16384` + `LOG_PROCESS_THREAD_SLEEP_MS=10` ✅ · `CONFIG_ZTEST=y` ✅ ·
**libc 三件套齐全**（`PICOLIBC=y` + `NEWLIB_LIBC=n` + `MINIMAL_LIBC=n`）✅ ——
**这条是硬要求**（KNOWN_ISSUES #58）：本工程**确实**踩在该坑上，`rtc_timer_ls.c` 依赖
`platform.c::rtc_timer_set_time()` 里的 `mktime()`，且 `Kconfig.ls` 的 `RTC_TIMER_LS`
`depends on NEWLIB_LIBC || PICOLIBC` —— 缺 `PICOLIBC=y` 则 **Kconfig 依赖不满足 + 构建失败**双杀 ✅ ·
`CONFIG_SHELL=n` ✅ · `CONFIG_RTC=y` ✅ · `CONFIG_GPIO=y` ✅

**§1.4 CMakeLists** — 走统一入口 `common/common_sources.cmake` ✅ · 未单独 `target_sources`
`test_results.c`/`ztest_glue.c`/`gpio_toggle.c` ✅

**§1.4a 复用收敛** — `app.overlay` 用 `#include "../../../common/gpio_toggle.dtsi"`，
未手写 `&gpioa { status = "okay" }` ✅

**§1.5 sys.path** — `sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))` ✅，
`dump_results.py` 经 `subprocess` 调用（§1.5 允许免第二个 insert）✅

**§1.6 ztest_glue** — `main.c` 含 `zt_init("TGT")` + `zt_run_all()` ✅
（Z 族原生 ztest 模式，与 `spi_test`/`trng_test` 等 8 个在跑工程逐字同构）
> 说明：CLAUDE.md §4.1 的「`test_main()` 不返回 / `k_thread_join(K_FOREVER)`」是 **F 族（自建 dispatcher 线程）**的规矩。
> 本工程是 **Z 族（纯 ztest）**：`zt_run_all()` → `ztest_run_all()` 内部对每个用例
> `k_thread_join(&ztest_thread, K_FOREVER)` 后**返回**，`test_main` 返回才轮到 Zephyr
> `ztest.c::main()` 执行 `end_report()` —— **而 `PROJECT EXECUTION SUCCESSFUL` 正是 `end_report()` 打的**
> （`zephyr/subsys/testsuite/ztest/src/ztest.c:1469-1471`）。⇒ **本工程"返回"才是对的**，
> 强行加 `K_THREAD_JOIN` 反而会让收尾标记永远不出现。

**§1.7 test.sh 三模式** — `./test.sh`（`SKIP_LOAD=y`）/ `-g` / `-r`（`AGENT_RUN`+`RUN_TIMEOUT=0`）齐 ✅

**§1.8 TEST_NAMES 双份同步 + 链接序** — `run_test.py` 与 `dump_results.py` **各硬编码一份，逐字相同** ✅ ·
两份均为 15 项、顺序 = **ztest_unit_test 段 SORT_BY_NAME 字典序**（struct 名 `..._ztest_unit_test__rtc_timer__<name>`，
公共前缀相同 ⇒ 按全名排序 ≡ 按用例名排序）✅ —— 逐项复核了易错的两对：
`b_get_normalizes_meta` < `b_set_get_roundtrip`（g<s）、`d_day_bounds_30day` < `d_day_bounds_feb_leap`（'3'0x33 < 'f'0x66）✅

**§1.9 模板缺陷防漏 10 项**
- a cpu2 hold（`set {int}0x4002200c = 2`）：`run_test.py:129` + `dbg_run.py:136` 均在 ✅
- b overlay chosen console：**已补**（见 §4）✅
- c 禁用堆分配：`src/*.c` 无 `k_malloc`/`k_calloc`/`k_realloc` ✅
- d 归档成对（`.md`+`.json` + `REPORT_BASE`）：`write_markdown_report` + `write_json_report` ✅
- e `ZTEST_SUITE` 槽位：`ZTEST_SUITE(rtc_timer, NULL, NULL, NULL, NULL, NULL)` —— 第 5 参（after，用例级）/第 6 参（teardown，套件级）**均 NULL**，无错位 ✅
- f 分型与入口一致：**FAIL → 已修（H1）** ✅
- g 样板残留：**建议 S1**
- h 边界常量可溯源：`rtc_timer_test.c` 文件头 7–14 行的边界表**逐条对应**驱动
  `rtc_timer_ls_calendar_valid()`（`rtc_timer_ls.c:29-59`）的判定式，非臆造 ✅
- i 块注释内 `*/`：`src/*.c` 全部 `*/` 命中都是**合法注释结束符**，无提前终止 ✅
- j `build_single_test` CONF_FILE 语义：本工程非上游 test/sample 复用，`""` 作 conf 追加无问题 ✅

**§1.10 k–t**
- k `ZT_INFO` 第一参是字符串字面量（3 处）✅
- l `ZTEST_SUITE` 显式 6 形参 ✅
- m 恒真断言：`zassert_equal(rtc_set_time(...), 0, ...)` —— `rtc_set_time` 是**有条件** return 0
  （非法参数走 `-EINVAL`，平台失败走 `-EIO`），**非恒真** ✅
- n **测试项数三方一致 = 15 / 15 / 15** ✅（固件 `ZTEST(` 计数 15 == run 脚本 `TEST_NAMES` 15 == dump 脚本 15）
  —— 逐名比对固件 15 个 ZTEST 名与父会话给的清单，**完全一致** ✅
- o `DRIVER_NAME = "rtc_timer_ls_test"` == 工程目录名 ✅
- p / q / r 不适用（非 DW SPI、无 GPIO 外设依赖、非扫描类工程）
- s `ZTEST` 套件 ⊆ `ZTEST_SUITE` 套件：固件只有套件 `rtc_timer`，差集**空** ✅
- t `COVERAGE.md` 表裸竖线：表头/分隔行/15 条数据行**列数一致（5 列）**，无撕裂 ✅
  （且 legacy `parse_expected_from_coverage()` 的正则对 5 列表取 group(4)=「预期」列，与 COVERAGE 表头第 4 列对齐 ✅）

---

## 4. 本次改动清单

| # | 文件 | 改动 | 性质 |
|---|---|---|---|
| 1 | `projects/rtc_timer_ls_test/tests/cases.yaml` | **新建**（meta 块 + `cases: - case_default`），照 `rtc_ls_test/tests/cases.yaml` 结构 | 补齐（规范要求，缺则 run_all 报错中止） |
| 2 | `regression/cases.yaml` | 新增一行 `  - projects\rtc_timer_ls_test`（紧随 `rtc_ls_test`；**路径行无行内注释**） | 补齐 |
| 3 | `projects/rtc_timer_ls_test/tests/run_test.py` | `BOARD` → `@1os/lsqsh/cpu1`；`TARGET_ROM_START` → `os.environ.get(..., "0x10000000")` | 硬伤 H1 小修 |
| 4 | `projects/rtc_timer_ls_test/tests/scripts/dbg_run.py` | `TARGET_ROM_START` → env 覆盖；补 `import os` | 硬伤 H1 小修 |
| 5 | `projects/rtc_timer_ls_test/tests/scripts/dump_results.py` | `TARGET_ROM_START` → `int(os.environ.get(..., "0x10000000"), 0)` | 硬伤 H1 小修 |
| 6 | `projects/rtc_timer_ls_test/cpu1_rtc_timer_test/app.overlay` | 新增 `chosen { zephyr,console = &uart1; };` | 补齐 §1.9b |

**未改**：`common/**`、其它工程、任何驱动/固件**逻辑**、`prj.conf`（合规，无需动）。
**离线校验**：三脚本 `py_compile` 通过；`run_all.read_cases()` 实测展开 → `total cases = 50`、
本工程解析出 `case_default`（`test.sh` 存在）、`_resolve_script` → `run_test.py`、`_is_dual_core` → `False`。

---

## 5. 预期首跑结果推导

**结论：15/15 PASS，case 判 PASS，`summary.md` 行 = `rtc_timer_ls_test_case_default | PASS | 15/15`。**

逐项按**驱动源码**（`rtc_timer_ls.c` + `platform.c` 的 `rtc_timer_{init,set_time,get_time}`）推：

| 测试项 | 预期 | 依据（源码行） |
|---|---|---|
| `a_device_ready` | PASS | overlay 使能 `&rtc_timer`（dtsi 默认 disabled），`DT_INST_FOREACH_STATUS_OKAY` 挂 POST_KERNEL，test_main 时已就绪 |
| `a_get_uninitialized` | PASS | `rtc_timer_init()` 置 `SFT_CTRL[4]=0xFFFFFFFF`（platform.c:322），测试复置后 `rtc_timer_get_time` 命中该哨兵 → `-1` → 驱动转 `-ENODATA`（rtc_timer_ls.c:87-89）。**该用例在链接序最前**，在任何 set 之前 ✅ |
| `b_set_get_roundtrip` | PASS | set→get 经 `mktime`/`localtime_r` 往返；断言只比 civil 字段 + 秒 ±1（`tm_wday/tm_yday` 不比） |
| `b_time_advances` | PASS | 2 s sleep，Δt∈[1,3]（1 Hz 链）；跨日已补偿 |
| `b_get_normalizes_meta` | PASS | 驱动 get 末尾**无条件** `isdst=-1, nsec=0`（rtc_timer_ls.c:92-93） |
| `c_month_bounds` | PASS | `tm_mon<0 \|\| >11` → false（:36）逐字对应 |
| `d_day_bounds_30day` | PASS | Apr=30（:39-41）+ `mday<1`（:49） |
| `d_day_bounds_feb_nonleap` | PASS | 2023 非闰 max_day=28（:37/:43） |
| `d_day_bounds_feb_leap` | PASS | 2024 闰 max_day=29（`%4&&!%100`） |
| `e_hms_bounds` | PASS | :50-52 逐字对应 |
| `e_wday_bounds` | PASS | :53 `wday < -1 \|\| > 6` 逐字对应 |
| `e_yday_bounds` | PASS | :54 `yday!=-1 && (yday<0 \|\| yday >= (leap?366:365))`；6 个断言值**全部**与该式吻合 |
| `e_isdst_nsec_bounds` | PASS | :55-56 逐字对应（`isdst∈{-1,0,1}`、`nsec∈[0,999999999]`） |
| `f_day_31_and_rollover_sample` | PASS | Jan 31 合法/32 非法（:46-49）；年尾 set 后 sleep 3 s，断言**极宽松**（`year==127` **或** `(year==126 && mon==11)`，见 `rtc_timer_test.c:315`） |

**两个曾疑、已排除的失败源**：
1. `c_year_bounds` 的 `tm_year=199`（**2099**）→ `mktime` 需 64 位 `time_t`
   （32 位会溢出 < 0 ⇒ `mktime` 返 `-1` ⇒ 驱动 `-EIO` ⇒ 该用例 FAIL）。
   已核 `zephyr/modules/lib/picolibc/CMakeLists.txt:459` `set(_WANT_USE_LONG_TIME_T OFF)`
   （其注释即"Define time_t to long **instead of** using a 64-bit type"）⇒ **picolibc 走 64 位 `time_t`** ✅。
   且 `SFT_CTRL[4]` 为 `uint32_t`，2099 的 epoch（≈4.09e9 < 2^32）可容纳 ✅。
2. `PROJECT EXECUTION SUCCESSFUL` 是否会出现 → 见 §3 §1.6 说明，**会**（`end_report()` 在 `test_main` 返回后执行）✅。

**残余不确定性**（无法离线消除，需上板）：`@2os → @1os` 是我按 §1.9f/#57 **改出来的**，
本机不得构建 ⇒ 1os 分型下这 15 项的实际表现**未经实机验证**（预期与 rtc_ls_test 的 1os 行为一致）。

---

## 6. 需上板确认的点

1. **【最高优先】1os 分型下 15 项是否仍全 PASS** —— H1 改动引入的唯一变量。
   若首跑出现 0/0 或串口 0 行，第一动作读 `$pc`/CSR（**入口是否 0x10000000**、`BOARD` 是否 1os），
   不要先怀疑固件（KNOWN_ISSUES #57 的迷惑点）。
2. **`PROJECT EXECUTION SUCCESSFUL` 是否出现** —— Z 族收尾依赖 `test_main` 返回后 `end_report()`；
   若缺失，`execution` 非 SUCCESSFUL ⇒ 该 case 判 FAIL（虽然 15 项可能全 PASS）。
3. **`a_get_uninitialized` 的 -ENODATA** —— 依赖 `SEC_PMU->SFT_CTRL[4]`，读 `reg_sec_pmu_rg.h`（`SEC_PMU_RG_SEC_ADDR=0x4002f000`）；
   若该寄存器在两核/软复位后不留 0xFFFFFFFF，此项会翻（同时暴露 `rtc_timer_init()` 提前 return 的隐患 ——
   `platform.c:315-318` 有 `if (CLKG0 & GPTIMA1) return;` 早退，**第二次 init 不再重写哨兵**，
   而本工程由测试自己写哨兵，故不受影响，但值得记一笔）。
4. **`f_day_31_and_rollover_sample` 的跨年** —— 断言宽松（两种结果都放行），
   但若时间链速率不准（≠1 Hz），`b_time_advances` 的 Δt∈[1,3] 会先翻。
5. **`regression/cases.yaml` 生效性** —— 首跑确认 `summary.md` 出现
   `rtc_timer_ls_test_case_default` 且**只有一行**（key 重复会说明 `DRIVER_NAME` 与目录名脱钩）。

---

# 追加：H2/H3/H4 三硬伤收口（第二轮，2026-09-24，纯离线）

- **范围**：`projects/rtc_timer_ls_test/` 三个旧世代脚本 → L1/L2 薄壳 + 判据收敛。
- **铁律遵守**：**未构建、未烧录、未上板**（磁盘清理后 62 个 build 目录已删，构建会触发全量重建）；
  **未跑任何写 git 的子命令**（只用 `git status --porcelain` 只读查污染面）；
  未碰 `common/**`、其它工程、任何固件/驱动逻辑。改动只落在本工程脚本 + 两处**必要**的框架登记
  （见 §C.3，越界已单独标注）。
- **验证手段**（FRAMEWORK_MAP §2：L2 处理层 ⇒ **可离线证**）：`replay.py --l2-direct` + 合成输入穷举
  + 与旧脚本的逐项 A/B。

## C.1 判定家族判定（★ 与首轮措辞的差异，务必先看）

首轮报告 §3「§1.6 ztest_glue」把本工程记作「**Z 族**」，那是本项目内部口语义 =「**ztest 原生
（`zt_run_all`）**」（与 `spi_test`/`trng_test` 并列）。**§6.2.4 冻结总表里的 `Z 族` 是另一个东西**
（= `adc_temper`/`dma`/`flash`/`iopmp`/`pwm`/`wdt`/`emmc`/`qspi_flash` 8 个，定义特征是
「**无 mem 通道**」）。两者同名不同义，**不要按 §6.2.4 的「Z 族」行去套本工程**。

**本轮判定：本工程属 §6.2.4 `F4 族`**（`trng`/`spi`/…/`rtc_ls` 同族），依据三条：
1. **血统**：旧脚本 docstring 首行原文即 `"""TRNG single-core regression runner: build cases, flash, …`
   —— 它就是 `trng_test`（F4 族）的单核派生；
2. **形状**：旧脚本**有 mem 通道**（`memory_backfill()` 真调 `dump_results.py --json`）+
   `passed == ran` 分母 —— 正是 F4 的定义特征（`Z 族` 无 mem 通道）；
3. **落点表**：§6.2.4 G10 注的「F4 族 9」里含同族的 `rtc_ls`。

**F4 比 `Z 族` 更紧**（迭代**全部已声明项**并叠加 R4 完整度守卫；Z 族只迭代**捕到的项**），
故按 F4 收敛**不可能放宽**判据 —— 这也是选它而非 Z 的第二个理由。

> ⚠️ 反向确认：若按「Z 族（无 mem 通道）」实现，则 `memory_backfill` 会被整块删除而不是收紧
> —— H3「mem 值被当证据」这条就只剩「删功能」一种闭法，且**判据并不落在 §6.2.4 的对应行上**。

## C.2 改动清单

| # | 文件 | 改动 | 性质 |
|---|---|---|---|
| 1 | `projects/rtc_timer_ls_test/tests/run_test.py` | **整体重写为 L2 薄壳**（照 `spi_test`）：`acquire.PortsDictSpec` + `l2_spec()` 五钩子；判据全部换成 `common/judge.py` 原语（G5 截断接受 / G6 红线 / G8 fail-closed / G9 / **G10 R8** / R7 / R4）；`TEST_NAMES` 提为模块级 15 项 | **H2+H3+S1** |
| 2 | `projects/rtc_timer_ls_test/tests/scripts/dbg_run.py` | `communicate()` 收尾 → **drain 线程 + `proc.wait()`**（2 处：`flash_once` / capture 轮）；补 `dbg_run_preflight()`（F1）；删 TRNG 专有 "key debug lines" 块；docstring 出处改 `spi_test` | **H4+S1**（+F1 遗漏） |
| 3 | `projects/rtc_timer_ls_test/tests/scripts/dump_results.py` | docstring「TRNG 单核工程版…i3c_dualcore_test 样板」→ 本工程真名 | **S1** |
| 4 | `projects/rtc_timer_ls_test/tests/COVERAGE.md` | 表头 `覆盖路径｜类型｜预期｜契约`（5 列）→ **§6.5.3 模板 6 列**（`驱动路径(函数+分支)｜维度｜预期数据｜预期来源｜状态`），状态 `☐` | 见 §C.3 ① |
| 5 | `regression/replay.py` | `PORTS_DICT` 增登记 `"rtc_timer_ls_test"`（**增 1 行 + 注释**） | 见 §C.3 ② |

**未改（守住边界）**：`common/**`、其它工程、固件/驱动逻辑、`prj.conf`；
**也未动首轮已修好的**：`BOARD=@1os/lsqsh/cpu1`、`TARGET_ROM_START` 走 `os.environ.get(...)`、
`app.overlay` 的 `chosen` 块（`git status` 里那三项的 mtime/内容未被我触碰）。

## C.3 两处越界/必要的框架改动（请父会话确认）

**① `COVERAGE.md` 必须整形，否则「期望」列静默变空。**
首轮 §1.10t 把「5 列表 + 旧 `[^|]*` 正则取 group(4)」判为合规 —— 那是按**旧脚本自带的解析器**
判的。改用框架唯一解析器 `common/judge.py::coverage_expected`（`tail=2`，要求 ≥6 列）后，
5 列表会解析出**空串**（`cells[3:5-2] == []`），「期望」列整列退化成 `-` ——
**报告信息静默丢失**。全仓 23 份 COVERAGE.md **只有本工程**是 5 列 / 列头 `预期`（其余均为
§6.5.3 的 6 列 / `预期数据`）。故按 §6.5.3 模板整形（**只改表头与列序，15 行数据逐字保留**）。
整形前后 `coverage_expected` 与旧正则的 15 项取值**逐项相同**（见 §C.5 [0]）。

**② `regression/replay.py::PORTS_DICT` 不登记，H2「可离线重放」是空话。**
`replay.py` 用 `split_case_dir()`（依赖 `ALL_PROJECTS` 家族表）解析 case 目录名
⇒ 未登记时该工程的 case 被**整批跳过**（`--l2-direct` 也照样空跑）；
`build_argv()` 同样按该表选 `--tgt-uart/--timeout`（否则落 SINGLE_CAP 分支传 `--case-timeout`，
本脚本不认）。改动是**集合里加一个工程名**，对其余 24 份零影响（只影响本工程名的前缀解析）。
`regression/` 不在任务书允许列表内，但它既非 `common/**` 也非别的工程，且是 H2 验收所必需 ——
**如不接受请直接回退这一行**，其余改动不受影响（只是 replay 仍跑不了本工程）。

## C.4 H2 / H3 / H4 逐条收口

### H2（无 L0 归档 ⇒ 不可重放）→ ✅ 消掉

- **依据（形式）**：`run_test.py` 现为薄壳 —— `def l2_spec` ×1（`ProcessSpec` 五钩子）、
  `acquire.` ×5、自实现 `build_case`/`capture_case`/`gdb_flash_once`/`reader`/`discover_ports`
  **全部删除**（采集与归档只剩 L1 的 `acquire.ports_dict_case()`）。
- **依据（实证）**：用 `common/serial_archive.archive_serial()` 造真 L0 归档后，
  `python regression/replay.py --session <会话> --l2-direct --freeze-time` **rc=0、0.3s/case**、
  15/15 PASS、产出 `.md`+`.json`+`summary.md`（§C.5 [2]）。

### H3（mem 兜底值被计为证据 / 旧 `case_ok` 公式）→ ✅ 消掉

- `run_test.py:235-242`（旧）`if mem == "PASS": st, note = PASS, "src=mem"` →
  现**逐项**落 `MEM_NOEVID`（取值保留在「来源」列 `mem(非证据:PASS)`），
  **不进 `passed`、不进 `ran`、计入 `missing_evidence`**，该 case 不得判 PASS
  —— 即 §6.2.4 **G10/R8**。
- `case_ok` 换成 `judge_case()`（收敛自 `common/judge.py`）：
  `passed == ran and ran > 0 and marker == "SUCCESSFUL" and not missing_evidence(...)`，
  另加 G5 截断接受与 G6 红线。
- **实证**：同一份采集行（13 项有 token + 2 项只有 mem 值）**旧 = PASS (15/15) → 新 = FAIL (13/13)**，
  且两项来源列写出 `mem(非证据:PASS)` —— **假 PASS 被堵**（§C.5 [3](b)）。

### H4（`communicate()` 收尾 ⇒ 超时分支 `ValueError` → `kill()` → 连接残留）→ ✅ 消掉

- **`run_test.py::gdb_flash_once` 3 处：随重写整体消失**（该函数已删，烧录链归 L1
  `acquire.gdb_flash_once`，其本身就是 drain 线程写法）。
- **`dbg_run.py` 5 处 → 0 处**：`flash_once` 与 capture 轮都改为**drain 线程持续排空 stdout +
  `proc.wait(timeout)` + 超时才写 `\x03`/`detach`**（`proc.stdin` 全程可写 ⇒ 优雅中断不再退化）。
  与 `spi_test` 对应文件同款（`grep -cE '\.communicate\('` 两份均 0；剩下的 1 处是**说明性注释**）。
- **附带**：`dbg_run.py` 原本**没有** F1 的 `dbg_run_preflight()`（全仓 23 份里唯一遗漏，
  `grep -c dbg_run_preflight` = 0）—— 已按模板补 1 行 import + 1 行调用。

## C.5 判据逐条对照 §6.2.4（**只紧不松**）

| 规则 | 新脚本落点 | 与旧脚本相比 |
|---|---|---|
| G1 全 PASS + SUCCESSFUL → PASS | `passed==ran and ran>0 and marker=="SUCCESSFUL" and not missing` | 同 |
| G2 任一 FAIL → FAIL | 同上（`passed != ran`） | 同 |
| G3 SKIP 不计分母 / 算确定结果 | `ran` 排除 `SKIP`；`missing_evidence` 不认 SKIP | **旧把 SKIP 算进分母（14/15 判 FAIL）** ⇒ 修正为 (14/14) PASS。**本工程无外部接线，SKIP 结构上不可达**（无放宽面） |
| G4 零测试项 → FAIL | `ran > 0`；且 15 项全 MISSING ⇒ FAIL | 同（实测 0/15 FAIL） |
| G5 全 PASS 仅缺标记 → PASS + TRUNCATED | `truncation_evidence()` + `exec_label()` | **旧判 FAIL** ⇒ 按冻结表改为 PASS。**这是本轮唯一「方向为放宽」的变化，但它是 §6.2.4 明文规则**（R1：窗口到点本身不是失败），且首轮报告 H3 已把「缺 G5」列为应补项 |
| G6 有 MISSING/NOT_RUN 且缺标记 → FAIL（红线） | `missing_evidence()`（含 `MEM_NOEVID`）非空 ⇒ `trunc_ok=False` | 同（实测截断+缺 2 项：旧 FAIL / 新 FAIL） |
| G7 `mem_state != ok` ⇒ 该核清空 | `mem_state_of_payload(None)` → `unavailable` | 同（批量无 mem） |
| G8 相反标记并存 → FAIL | `parse_ztest` R6 已 fail-closed（`execution=FAILED`） | **实测同**（旧脚本也走同一个 `parse_ztest` ⇒ 首轮「缺 G8」的说法**不成立**，此处如实更正）。报告侧 `CONFLICTING_MARKERS` 标签只有 D3 族接线，F4 族（含 `spi_test`/`trng_test`）都不渲染 —— **与本族现状一致，未新增也未削减** |
| G9 整 case 零 token → FAIL + 理由串 | `mem_only_evidence()` + `MEM_ONLY_NOTE`（挂在 `mem_note`，控制台/.md/.json 三处同源） | **旧无此守卫**（旧 = FAIL(3/15)，但它 FAIL 的原因是别的；构造「全 mem」场景可证判定不变、**证据口径收紧**） |
| G10 任一项来自 mem → `MEM_NOEVID` | `mem_noevid_source()` / `evidence_gaps()` / `mem_weak_note()` / `MEM_WEAK_ITEM_NOTE` | **旧把 mem 当 PASS ⇒ 假 PASS（已堵）** |
| R4 常规路径 NOT_RUN ⇒ FAIL | `missing_evidence()` 含 `NOT_RUN`（`NO_EVIDENCE_STATES`） | 同（旧 `ran` 已排除 NOT_RUN ⇒ `passed != ran`） |
| R7 批量必须靠 token（mem-only 门） | `mem_only_evidence(notes.values())`，`case_ok = verdict.ok and not mem_only` | **旧无** ⇒ 新增 |
| F5 `ran == 0` → FAIL | `ran > 0` | 同 |

**结论：判据只收紧、未放宽**（唯一两处「放宽方向」= G3/G5，均为冻结表明文规则，且已被各自守卫夹住：
G5 的 G6 红线实测仍 FAIL；G3 在本工程结构不可达）。**所有收紧面**（G9/G10/R7/R4 + F4 迭代全声明项）
都实测生效。

## C.6 离线验证证据（可复跑）

产物在 `tmp/rtc_timer_port/`（`tmp/` 已被 .gitignore 覆盖，不进仓）：
`ab.py`（A/B）、`mk_l0.py`（造 L0 归档）、`run_test_OLD.py`（旧脚本原件，只读证据）、
`l0/2026-09-24-1/`（L0 归档）。

**[0] `COVERAGE.md` 预期列解析（旧正则 vs `coverage_expected`）**：15/15 **逐项相同**
（旧正则另多出 1 行表头伪项 —— 旧实现无表头守卫，属旧实现的缺陷，非本工程内容差异）。

**[1] 13 场景 A/B（同一份采集行，旧判定 vs 新判定）**：**9 SAME / 4 DIFF**

| 场景 | 旧 | 新 | 归因 |
|---|---|---|---|
| health_all_pass / health_with_info | PASS (15/15) | PASS (15/15) | **SAME**（健康路径逐项一致） |
| no_boot_health | PASS (15/15) | PASS (15/15) | SAME |
| one_fail | FAIL (14/15) | FAIL (14/15) | SAME |
| marker_but_missing_2 | FAIL (13/15) | FAIL (13/15) | SAME |
| trunc_missing_2 | FAIL (13/15) | FAIL (13/15) | SAME（G6 红线） |
| conflicting_markers | FAIL (15/15) | FAIL (15/15) | SAME（R6） |
| zero_items_marker / zero_items_no_marker | FAIL (0/15) | FAIL (0/15) | SAME（G4） |
| trunc_all_pass | FAIL (15/15) | **PASS (15/15)** `TRUNCATED_AFTER_ALL_ITEMS` | **G5**（意图内） |
| one_skip | FAIL (14/15) | **PASS (14/14)** | **G3**（意图内；本工程不可达） |
| mixed_13token_2mem | **PASS (15/15)** | **FAIL (13/13)** | **G10/R8**（H3 本体，查漏） |
| all_mem_zero_token | FAIL (3/15) | FAIL (0/12) | G9/G10（查漏） |

> 头部 `new.exec_report` 逐场景与 `marker` 一致；`missing` 列表点名到项（不吞信息）。

**[2] 真 L0 归档 + `replay.py --l2-direct`（离线，不碰板）**：
`rc=0`、`0.3s/case`、`-> 15/15 PASS, skipped=0, execution=SUCCESSFUL`、
产出 `rtc_timer_ls_test_report.md`（§6.4.2 表头/列序/转义 + 期望列全 15 项非空 + INFO 列）
与 `.json`、`summary.md` 行 `| [rtc_timer_ls_test_case_default](…) | PASS | 15/15 |`。
（`md 缺失（new=1 old=0）` 是**预期**：合成会话没有「原报告」可比 —— 本工程从未跑过，故无历史产物。）

**[3] 强断言**：`(a)` 健康路径逐项一致且双方 PASS(15/15) = **True**；
`(b)` 混合 13 token + 2 mem：旧 PASS → 新 FAIL（**假 PASS 被堵**）= **True**；
`(c)` 截断 + 缺项仍 FAIL = **True**（G6 红线未被 G5 侵蚀）；
`(d)` 零证据仍 FAIL = **True**。

**[4] 静态合规（`qsh-test-validate` §1.10u/w/n）**：
`py_compile` 三脚本 + `regression/replay.py` 全过 · `def l2_spec` ×1 · `acquire.` ×5 ·
`.communicate(` = 0 · `trng` 残留 = 0 · `DRIVER_NAME="rtc_timer_ls_test"`（== 目录名）·
`TEST_NAMES` 三方一致：run_test 15 == dump_results 15 == 固件 `ZTEST` 15（集合相同、逐字同序），
且 15 项 = `a_device_ready` … `f_day_31_and_rollover_sample` **与任务书清单逐字对上** ·
`run_all.read_cases()` 展开 **total cases = 50**，本工程解析出 `case_default`。

## C.7 仍需上板确认的点（离线不可证）

1. **L1 字段值的实机等价性**（FRAMEWORK_MAP §2：`AcquireSpec` 字段值「抽样可证」）：
   `build_progress="always"` / `build_timeout=600` / `pre_erase=False` / `hold_cpu2=True`
   是按**旧脚本行为逐字对齐**选的（旧脚本 `-p always`、无 `erase_address`、写 `0x4002200c=2`），
   但**没有一次真烧录**验证过。首跑若出现「编译过了但串口 0 行」，第一动作先核这几项。
   > ⚠️ **与首轮的差异**：首轮 H2 修法建议 `hold_cpu2=False`（疑似抄自 `rtc_ls_test`）。
   > 本轮**不采纳** —— 旧脚本自己就写 `=2`（hold），改成 False 等于**拆掉防他方 RAM 残留串扰的闸门**，
   > 与「只紧不松」相反。请父会话确认取哪个。
2. **`TEST_NAMES` 的真实链接序**：本工程从未构建 ⇒ 无法 `nm` 复核。
   现序 = 两个脚本的**共同值** + `SORT_BY_NAME` 推演（首轮 §1.8 已独立复核过易错的两对）。
   上板前用 `nm --numeric-sort <elf> | grep _ztest_unit_test__rtc_timer__` 复核一次。
   （注意：**固件源码里的 `ZTEST` 书写顺序 ≠ 链接序**，以 nm 为准。）
3. **1os 分型下 15 项是否全 PASS** —— 首轮 §6 第 1/2 条不变（本轮未改分型相关代码）。
4. **`PROJECT EXECUTION SUCCESSFUL` 是否出现** —— G1 的必要条件。
5. **G5/G3 两处放宽的实际触发面**：本工程正常路径不会出现「截断全 PASS」，
   但**长用例（`b_time_advances`/`f_day_31_and_rollover_sample` 有 sleep）+ 窗口偏紧**时
   会走到 G5 ⇒ 首跑请核对报告里 `执行标记` 是否为 `TRUNCATED_AFTER_ALL_ITEMS`
   （若是，说明窗口该加大，而不是「过了就算」）。

## C.8 本轮未做（留给后续）

- `tests/COVERAGE.md` 缺 **§6.5.6「独立预期（机器可读 `expect` 块）」**段 ⇒
  `common/expect_check.py` 对本工程为覆盖缺口。**故意不补**：该块要填**固件实测 INFO 值**，
  离线无法取得，凭猜写会变成新的假证据来源。建议首跑后按实测补。
- `dbg_run.py` 新增的 `dbg_run_preflight()`（F1）**未上板**（起不来时最多走完 2 轮自愈阶梯
  ≈100s 才 fail-closed —— fail-closed 的必然成本，与其余 23 份同款）。
- `replay.py::rerender_from_json()` 的 `project == ...` 派发链对本工程（及其它 F4 族）
  **未登记且参数序陈腐** ⇒ `--render-from-json` 对本工程不可用（**与本次无关的既存缺口**，
  `--l2-direct` 不受影响）。未改（不属于本任务，且是框架级问题）。
