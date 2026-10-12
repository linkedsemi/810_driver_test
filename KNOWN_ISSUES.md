# 810_driver_test 已知坑清单（完整版）

> 本文档记录所有实测沉淀的问题，含一次性/已修复的条目。
> `CLAUDE.md` §5「常见现象速查」只保留最高频几条（精简版），完整清单在本文档。
> 新坑先加这里，高频的再精简进 CLAUDE.md。
>
> ⚠️ **语料可用性（2026-09-24）**：本文引用的 `reports/` 会话凡**非 `<date>-1`** 者
> （`2026-09-17~09-20` 全部，及 `2026-09-21-2` / `2026-09-22-2` 等同日其它序号）
> **均已不可得、且从未入库** ⇒ 相关引用**仅作留痕**（不得据此外出查证，也不得据此再向外指路）；
> 留存范围与纪律见 `SPEC_REFERENCE` §6.4.7「**语料可用性声明**」。**不要重建或推测缺失报告的内容。**

---

## 连接与 DebugServer

**1. gdb 被杀后 DebugServer 连接残留**（高频）
`continue` 超时被杀后 DebugServer 仍认为连接中；下次 `target remote` 能连上但 `reset`/`load` 可能无效 → 固件不跑，串口 0 行或只有 `\x00`。
处理：`gdb -ex "target remote ..." -ex "detach" -ex "quit"` 做干净连接；不行重启 DebugServer。

**2. DebugServer 起不来：CKLINK may be used by other program**
FlashProgrammer 占用 CKLink。互斥是双向的：烧 flash 时 DebugServer 也必须退出。
处理：`tasklist | grep -i -E "cskyflash|debugserver"` 找占用进程 → taskkill。
迭代补充（2026-09-05）：DebugServerConsole.exe 卡死占用 port 1025，Python subprocess `taskkill /F /PID` 无法杀死（MSYS 单斜杠不转换经典坑），新 DebugServer 启动报同样错误。最终在 bash 层面直接执行 `taskkill //F //PID` 成功（双斜杠在 MSYS bash 中被正确转换为单斜杠）。
杀进程顺序：先杀残留 gdb → 再杀 DebugServerConsole（按 PID） → 确认 port 1025 释放（`netstat -ano | grep :1025`） → 再重启。
极端情况：`taskkill //F` 仍杀不掉 → 拔插 CKLink USB 物理释放。

**3. 不要用 netstat 判离线**（用户纠正）
1025 由 PC 进程 DebugServer 提供（非固件内置），判断在线 = GDB 连一次看 "Remote debugging"。

**17. DebugServer 连接残留的脚本化自愈**
detach 不能真正释放连接；唯一可靠 = 重启 DebugServer 进程。四步：① netstat 杀 gdb ② taskkill /F /IM DebugServerConsole ③ netstat 精确杀 1025 LISTENING ④ Popen 重启 + 轮询 `DebuggerServer$` 判就绪。
Python subprocess 里 taskkill 用单斜杠 `/F /PID`（#16）；若杀不掉，fallback = bash 层 `taskkill //F //PID`（见 #2）。

**20. DebugServer 重启后"不稳定期"**
新实例立即连跑 → 间歇性 load_fail/断连/WDR 异常。实例稳定 ~5s 后正常。
处理：run_test.py 自动重试 1 次（含 5s 稳定期）。

**33. ConnectType=0 在 DebugServer 会话后卡死**（2026-09-02）
flash_program.py 用 `ConnectType=0` 会卡死 80s。已改为 `ConnectType: 1`（硬复位）。
仍卡死 → 检查 Device Manager 是否有多个 CKLink（含 Unknown），禁用异常设备。

**34. 回归结束后 DebugServer 残留**（2026-09-02）
run_all.py 加 `cleanup_cklink()`（try/finally），任何退出路径都释放 CKLink。

**36. GDB -batch 不打印 "Remote debugging"**（2026-09-02）
探测脚本改判 `returncode==0 and ("detached" in output)`，或直接判退出码 0。

---

## 烧录与 I-cache

**4. 两遍烧录法**
单遍烧录 + continue 因 I-cache 残留跑旧镜像（"慢一拍"）。同一镜像烧两遍，第二遍抓取。
单核工程已改单遍烧录（reset 冲刷 I-cache，#24）。

**23. GDB 写包提速 41x**
load 前发 `set remote memory-write-packet-size 16384` + `set remotewritesize 16384`。
192KB 镜像 ~190s → ~5s。已固化进单核 dbg_run.py/run_test.py。
> **★ 实测口径更正（2026-09-21，N-3）**：请求 `memory-write-packet-size` 16384 / 2457 / 1024 →
> gdb **协商** `2493 / 1820 / 875 bytes/write`，**但加载总吞吐恒 41 KB/s、墙钟恒 3~4s（与包长无关）**
> ⇒ **41 KB/s 才是链路瓶颈**，包长在实测区间内不改变加载耗时。**判据看 gdb 的
> `Transfer rate: N KB/sec`**（真实吞吐），**不得从 `N bytes/write`（协商包长）反推提速倍数**。
> 双核 `load_and_run` 原注释"不设时慢约 6.7 倍 / 单核 41x 提速"**已按实测撤回**
> （工作区已无 `GDB_WRITE_PKT`）；单核路径两行 `set` 仍在。见 `qsh-debug` §3.1。

**24. 单会话内二次 load 静默挂死**
第一次 load 后 interrupt 再发 load → gdb 无输出。改单遍烧录；免烧用 `--no-load`。

**25. 一次性 -ex 链 continue 竞态**
-ex 链的 continue 在 load 刚完成瞬间发出 → 静默丢弃。改交互式：等 "Transfer rate" 再 continue。
例外：trng/rtc 保留 -ex 链（已裁定存量）。

**13. XIP 烧录会话日志**
CLI 烧录追加写 `flash-programer.log`，排障看尾部关键字。

**14. 双核固化必须一次会话**
全片擦会擦掉另一核。用 `--bin a@addr1 --bin b@addr2` 一次烧。

---

## 固件与构建

**7. ISR 内禁 LOG**
LOG_ERR 走 shell 后端阻塞 10ms+/条 → 中断饿死。ISR 只记账。

**8. test_main() 不得返回**
返回即 LOG_PANIC → 同步日志死窗。用 `k_thread_join(K_FOREVER)`。

**9. 改 overlay 删 build 全量重建**
`-p auto` 不检测 Kconfig 缓存。

**55. `-p always` 后台重建 + gdb load 竞态：gdb 加载了旧二进制**（2026-09-15）
脚本用 `west build -p always ... &`（后台）立即起 gdb `load`，但构建尚未完成 →
gdb load 的是 build 目录里的旧 .bin → 固件行为与源码不一致，极难定位。
`-p always` 会先清 build 目录再重建，清完瞬间 .bin 不存在或残留旧版 → gdb load
可能拿到旧文件或报 file not found。
处理：**构建必须同步完成**（不加 `&`），或显式 `wait`/轮询构建进程退出后再起 gdb。
已知安全模式：`west build ... && python dbg_run.py ...`（串行，构建完才跑脚本）。

**26. CONFIG_ZTEST_VERBOSE_OUTPUT 门控**（迁移必查）
=n 则 START/PASS/FAIL 行被抑制 → 全 MISSING 假失败。prj.conf 必须 =y。

**35. CONFIG_NEWLIB_LIBC=y 与 Zephyr POSIX types 冲突**（2026-09-02）
`pthread_cond_t` 类型重定义。全工程统一 =n。

**6. 调试探针必须清理**
`log_dbg_*`/`to_dbg_*`/probe/while(1) 陷阱一律删。功能修复（MPIL workaround/PMP_W）保留。

**38. 新工程脚本漏"按住 cpu2"——cpu2 自启劫持串口**（2026-09-07，模板缺陷）
6 个新工程的 dbg_run.py/run_test.py 全部没有 `set {int}0x4002200c = 2`
（APP_CPU_SRST 保持 cpu2 复位），老 4 个单核工程全都有此步骤。
后果：cpu2 从 flash 自启跑旧固件，可能经 vuart 代理劫持 uart1 输出，
导致单核工程串口 0 行（看似"无输出"实际是 cpu2 抢了串口）。
根因：qsh-test-gen 模板生成脚本时未注入 cpu2 hold 步骤。
修复：dbg_run.py / run_test.py 的 gdb 命令序列在 `reset` 前加
`set {int}0x4002200c = 2`（APP_CPU_SRST 寄存器，bit1=cpu2 srst）。
★ 该写入的**副作用**：`is_app_cpu_running()`（= `APP_CPU_SRST > 0`）会返回 true，使带双核运行态
守卫的驱动 API 全部 `-EBUSY`（eMMC/sdhci 即此）——见 #75。
⚠️ **2026-09-18 更正**：写 2 后实测**回读 = `0x0`**（GDB + 固件自读一致，见 #75 更正批注），
该副作用**未复现**；emmc 用例 SKIP=0、API 正常。本条 cpu2 hold 步骤本身仍是模板必需项
（防 cpu2 自启劫持 uart1），仅"副作用"论断待裁定。
已固化为 qsh-test-gen 模板约束（见 qsh-test-gen §4）。

**39. chosen 节点丢失导致串口无输出**（2026-09-07，模板缺陷）
新工程 overlay 未显式声明 `chosen { zephyr,console = &uart1; };`，
DT_CHOSEN(zephyr_console) 编译时为 NULL → uart_console_init 跳过 → printk 无输出。
根因：qsh-test-gen 模板 overlay 不含 chosen 块。
修复：所有单核工程 overlay 必须显式声明 chosen 节点。
已固化为 qsh-test-gen 模板约束（见 qsh-test-gen §4）。

**40. k_malloc 依赖堆导致崩溃——表现为串口 0 行极具迷惑性**（2026-09-07，模板缺陷）
adc_temper 测试代码 `k_malloc(80)` 返回 NULL → `memset(NULL, ...)` →
store fault → 系统挂死 → log 线程拿不到调度 → 串口 0 行。
迷惑性：表现为"串口无输出"，诊断时一直追 console/log 配置，
实际根因是堆内存不足的崩溃。
根因：测试代码用堆内存而非静态数组；prj.conf 缺 `CONFIG_HEAP_MEM_POOL_SIZE`。
修复：测试代码禁用 k_malloc，改用静态数组；qsh-test-gen 模板约束测试代码
不得依赖堆分配。排查串口无输出时应**先查崩溃（fault/hardfault 日志）再查配置**。
已固化为 qsh-test-gen 模板约束（见 qsh-test-gen §4）。

**56. ZTEST_SUITE 回调落错槽 → 用例间共享句柄 NULL → 空指针崩溃挂死**（2026-09-16，模板缺陷）
`ZTEST_SUITE(name, predicate, setup, before, after, teardown)` 中
**`setup`/`teardown` 是套件级（整个 suite 各跑一次）、`before`/`after` 是用例级（每例前后都跑）**。
flash_test 写成 `(..., setup, NULL, teardown, NULL)`——setup 正确落套件级只开一次 flash_area，
teardown 却落进 `after` → 第一个用例后即把 `fa` 置 NULL → 第二个用例 `flash_area_erase(NULL,...)`
读地址 0x8 → load access fault → SoC 异常入口 `while(1)` → 串口从此静默（症状极像配置/烧录问题）。
修法：teardown 挪进 `teardown` 槽——**不能挪进 `before`/`after`**（形参类型不同，`void(*)(void*)`
vs `void*(*)(void)`；当初"看着能编译过"正因错槽类型不同也不报错）。
已固化为 qsh-test-gen 模板约束（§1.4 第 8 条）+ qsh-test-validate 检查项 §1.9e；现场重建过程（CSR
判读 + 断点打印入参定位 NULL 来源）见 `.claude/skills/debug-knowledge/knowledge/riscv/trap-hang-csr-scene.md`。

**58. 单核工程 prj.conf 缺 libc 三件套 → 共享层用到 mktime 等函数即构建失败**（2026-09-16，模板缺陷）
单核工程 prj.conf 必须**显式**含 libc 三件套：`CONFIG_PICOLIBC=y` + `CONFIG_NEWLIB_LIBC=n`
+ `CONFIG_MINIMAL_LIBC=n`。缺 `PICOLIBC=y` 时落到 Zephyr **minimal libc**，它不含 `mktime`/
`localtime` 等 POSIX 函数——驱动/共享层（如 HAL `platform.c` 的 `rtc_timer_set_time()`）一旦新增
此类调用，**构建直接失败**（`implicit declaration of function 'mktime'`）。
迷惑点：报错落在**共享树**（`modules/hal/...`），不在工程里，极易误判成共享树被改坏。
判据：同一份共享源码在**别的工程**构建通过 → 是工程配置差异，不是共享树问题，**不要盲目改共享
HAL**；`-DCONFIG_PICOLIBC=y` 命令行覆盖可非侵入验证假设。`NEWLIB_LIBC=y` 的冲突见 #35。
防再犯：qsh-test-validate §1.3 已加"单核工程 prj.conf 必须显式含三件套"检查项。

**68. mbedtls 堆统计 API 全被 `MBEDTLS_MEMORY_DEBUG` 门控**（2026-09-17）
`mbedtls_memory_buffer_alloc_cur_get()` / `max_get()` / `status()` / `count_get()` 及其计数字段
（`total_used`/`maximum_used`/`header_count`）**全部在 `MBEDTLS_MEMORY_DEBUG` 下**
（`memory_buffer_alloc.c` 与 `memory_buffer_alloc.h:73-114` 同一 `#if`）⇒ 写 mbedtls 堆相关用例前
必须 `CONFIG_MBEDTLS_MEMORY_DEBUG=y`（`zephyr/modules/mbedtls/configs/config-tls-generic.h:363-365`
把它映射到该宏）；否则编译报 `implicit declaration`。
- 迷惑点：`CONFIG_MBEDTLS_ENABLE_HEAP=y` **只建堆**（`zephyr_init.c` 的 `init_heap()`），**不含任何
  用量可读接口**；不开 `MEMORY_DEBUG` 时唯一可用的是 `mbedtls_memory_buffer_alloc_verify()`
  （头链完整性，**不是占用**）——容易误以为 `verify()==0` 就是"没泄漏"。
- 该宏只加计数、不改分配行为（`MBEDTLS_SELF_TEST` 未开时 `check_all_free()` 也不会激活），属安全开关。

**69. 用 `ZT_INFO` 前确认该 `.c` 已 include `ztest_glue.h`；`rtc_ls_test` 是唯一未迁移工程**（2026-09-17）
- `ZT_INFO` 来自 `common/ztest_glue.h`，而 glue 是**按需 include** 的（`common_sources.cmake` 只保证
  编译进去，不保证每个 `.c` 都 include）⇒ 新写 `ZT_INFO` 的 `.c` 必须先补 `#include "ztest_glue.h"`，
  否则 `implicit declaration of ZT_INFO` 编译失败（实测漏在 adc_temper_test / iopmp_test）。
  判据：文件里能 grep 到 `zt_init`/`zt_run_all`/`ztest_glue.h` 才是 glue 工程。
- **`rtc_ls_test` 是唯一未迁移到 ztest 原生 glue 的工程**（自带 `tr_record` + 自定义 token +
  自写 `rtc_test_run()`）⇒ 它的新用例必须沿用它那套 `rtc_begin/rtc_finish` + `enum rtc_test_id`
  + `rtc_test_names[]` 三处同步，**不能用 `ZT_INFO`**。

**71. 以太网 PHY 复位 `do{read}while` 无超时自旋 → PHY 不应答时 POST_KERNEL 永久挂死（串口 0 行新根因）**（2026-09-17）
`zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c:166-174`（`phy_rt_rtl8211f_reset()` 尾段）是
`do { phy_rt_rtl8211f_read(dev, MII_PHYID1R, &reg_val); } while (reg_val != REALTEK_OUI_MSB);`
（`REALTEK_OUI_MSB = 0x1CU`，见同文件 L29）—— **无超时自旋**：read 本身不报错、只是返回值
始终不是 `0x1C`（复位失败 / MDIO 不通 / 器件异常）时，在 **POST_KERNEL** 阶段**永久挂死**，
固件不启动、**串口 0 行**。
- **与 #38/#39/#40/#56/#57 同症状但根因完全不同**：那组是"固件没跑 / 崩溃 / 配置"，
  带 eth1 的工程走它们会**白查一轮**。
- 判据：**带 eth1 的工程** load 后串口 0 行 → **优先怀疑 PHY 复位自旋挂死**：gdb attach
  读 PC，栈落在 `phy_rt_rtl8211f_reset` 的自旋循环内即确认。
- 驱动侧修法（属改上游，需裁定）：给该自旋加超时（`k_timepoint` 超时后返回 `-ETIMEDOUT`）。
- 领域招式（跨工程复用）见 `debug-knowledge` zephyr 域 `knowledge/zephyr/init-unbounded-spin-hang.md`；
  同族 P0（丢弃 PHY 复位返回值致误归因）见 #73。

**72. 以太网 / MDIO 工程 prj.conf 必须显式带 `CONFIG_NET_TCP=y` + `CONFIG_SHELL=y`（驱动侧配置耦合）**（2026-09-17）
`eth_dwmac.c` 把工程配置耦合进驱动，与具体工程无关（**任何带 eth1 的工程都会踩**）：
- `eth_dwmac.c:73-75`：RX refill 线程优先级取 `CONFIG_NET_TCP_WORKER_PRIO-1` ⇒ `NET_TCP=n` 时
  该符号未定义 → 编译报 `'CONFIG_NET_TCP_WORKER_PRIO' undeclared`。
- `eth_dwmac.c:832` 文件末尾**无条件** `SHELL_CMD_ARG_REGISTER`（无 `#if` 守卫）：该宏在
  `SHELL=n` 下**不是空宏**（把 entry 塞进 `shell_root_cmds` 段、handler 被强引用），handler 调
  `shell_error()→shell_fprintf_error()`，该符号只在 `SHELL=y` 时编入 ⇒ 链接报
  `undefined reference to 'shell_fprintf_error'`。
修法：prj.conf 显式带 `CONFIG_NET_TCP=y` + `CONFIG_SHELL=y` + `CONFIG_SHELL_BACKEND_SERIAL=n`
+ `CONFIG_SHELL_BACKEND_DUMMY=y`（开 SHELL 拿符号、关串口 backend 只留 dummy：不产生提示符、
不占 uart1 RX，ztest 串口流不受污染）—— 这是 qsh-test-gen §1.4 第 6 条"810 惯例 `CONFIG_SHELL=n`"
的**唯一例外**。另 `mdio1` 的 init 挂在 `CONFIG_NETWORKING_AUTO_INIT` 下 ⇒ 网络栈必须打开，
否则 MDIO 事务必然超时。已固化为 qsh-test-gen §1.7 + qsh-test-validate §1.3 + ref-gen-errors 速查；
参考实现 `projects/eth_test/cpu1_eth_test/prj.conf`（含逐条注释）。

**73. `mdio_dwmac.c` 丢弃 PHY 复位返回值 → PHY 失效被误归因为 "PHY ID 不符"（P0，未修）**（2026-09-17）
`mdio_dwmac.c:274-276` 的 `mdio_dwmac_ethphy_reset(dev);` **返回值被丢弃**（qsh-driver-review
§1.5「硬件控制类返回值丢弃 → P0」）。后果：PHY 复位失败被静默忽略 → MDIO 读回垃圾值 → 测试报成
**"PHY ID 不符"** 这种**误导性归因**，而不是 "复位失败"。
- 调试判据：eth/MDIO 用例报 PHY ID / BCR 不符时，**先确认复位是否真的成功**（单独驱动复位
  GPIO / 读复位前后寄存器复现），再怀疑 PHY 型号或 MDIO 时序。
- 状态：**未修**（`zephyr/` 下上游文件禁改）；是否上报上游 / 测试侧加防护待裁定（OPEN_ITEMS O-16）。

**119. 非 810 工程（采样工程 `samples/mbedtls_throughput`）在当前工作树不可构建 —— 两处互相独立的既有破损，`-p auto` 必现**（2026-09-29 / 2026-10-09 两度实测）
现象：`west build -p auto -b lsqsh_evb@1os/lsqsh/cpu1 linkedsemi_zephyr_project/samples/mbedtls_throughput`
失败，且两处原因**都与本次改动无关**（`-p auto` 触发 pristine ⇒ 必现；非 pristine 增量可能因缓存 `.obj` "假通过"）：
- ① **源树自身悬挂引用（未提交改动孤儿）**：`src/main.c`（未提交的改动）调
  `ls_otbn_rng_timing_{reset,cycles,isrs,words}()`，该组符号**全树无实现**（`HEAD:main.c` 不含）⇒
  调用方成孤儿。属 **#108** 家族（未提交的工作区改动被静默冲掉、调用方成孤儿）。
- ② **共享树在别处配置下的既有限制**：`modules/hal/linkedsemi/soc/rv32/qsh/platform.c:350` 的
  `rtc_timer_set_time()` 调 `mktime()`，而该工程 `CONFIG_MINIMAL_LIBC` 的 `time.h` **不给原型**
  ⇒ GCC 14.1.1 把隐式声明判 **error**（与 **#58** 同源同报错，只是本工程配置不在 810 手上、
  "libc 三件套"修法不适用）。
- **诊断迷惑点**：报错落在**共享树**（`modules/hal/...`），不在你改的模块里 ⇒ **别按"自己改坏了
  共享树"排查**（#58 / #115）。
- **绕法（不改任何他人文件，两度实测有效）**：`west build ... -- -DEXTRA_CFLAGS="-Wno-error=implicit-function-declaration"`
  （把 `mktime` 隐式声明降级；`rtc_timer_set_time` 无调用者 ⇒ `--gc-sections` 丢弃、链接无未定义）。
  09-29 曾用垫片绕法（`-include <tmp>/rng_timing_shim.h`：给孤儿符号 no-op `static inline` 存根 +
  `mktime` 原型），实测垫片对 `aes_alt.c` 编译产物逐位无影响。**修根**：删 `src/main.c` 孤儿调用 +
  `platform.c` 补 `mktime` 原型。
- 关联：**#58**（mktime / MINIMAL_LIBC）· **#108 / #115**（未提交改动 / 游离损坏）· `qsh-test-run` §6。

---

## 串口与解析

**5. token 行污染**
shell 提示符 ANSI 重绘混入行首。解析用 ANSI 剥离 + `re.search()`，不用 `^` 锚点。

**10. COM 口被占用**
`tasklist | grep python` → `taskkill //F //PID <pid>`。

**12. prime/capture 输出混合**
两轮烧录输出混排 → 只采 capture 轮。

**27. ztest_unit_test 链接序 = 字典序**
TEST_NAMES 按 `nm --numeric-sort <elf>` 链接序对齐，勿用 plain nm 名字典序。
坑中坑：尾下划线使 `'_'(0x5F)` 排在数字之后，裸名排序与真实链接序可能相反。

**31. ztest duration = CPU 活跃时间**
`PASS - in X.XXX seconds` 是 cycle-counter 时间，非墙钟。

**114. 批量报告里 `内存兜底: 不可用` + `来源=missing` 是 2026-09-22 裁定的预期形貌，不是采集故障**（2026-09-23 登记）
`common/acquire.py:507-517` 载明用户裁定（"全量测试 gdb 只负责烧录、不自动取结果"）：批量路径
**三个取数点一律写死 `mem = None`** ⇒ 报告头部渲染 `内存兜底: 不可用（未执行）`、来源列落 `missing`。
- **语义**：`missing` = "该核**既无 token 也无 mem**"，**不是**"结果被采集链路弄丢"。
- **群体现象（实测）**：`reports/2026-09-21-1` **0/50** 个 case 标"不可用"（裁定生效前）；
  `2026-09-22-1` / `2026-09-23-1` 各 **29/50**（裁定生效后**全体**如此，**非**某 case 特有异常）。
- **误判形态（已发生）**：排查时把 `missing` 当"采集故障"、去查串口/管道 —— 属诊断陷阱，不是缺陷。
- **判定中性**：取与不取 mem 的 PASS/FAIL **完全相同**（`SPEC_REFERENCE` §6.4.10 的 51-case 重放：
  一致 51 / 不一致 0 / 缺失 0）。
- 真源：`SPEC_REFERENCE` §6.4.10（机制）/ §6.4.2（渲染词表）· `qsh-test-run` §4.2 · `qsh-test-compare` §3。

---

## 脚本与工具链

**11. GDB 无 Python**
验证逻辑全用 `-ex` 命令链；`continue &` + `interrupt` 不可靠。

**15. 调试临时物统一放 tests/tmp/**
一次性产物 → `tests/tmp/`（gitignore）；通用脚本 → `tests/scripts/`。

**16. Python subprocess 调 Windows 命令用单斜杠**
MSYS `//F` 只对 Git Bash 有效，subprocess 无转换层 → "无效参数"。一律 `/F /PID`。

**18. 回归前必须构建全部 case**
勿 `--skip-build` 混旧镜像（无 BOOT → 假 load_fail）。

**19. subprocess 里裸 bash 被 WSL 抢解析**
显式解析 Git Bash 完整路径（`_find_git_bash()`），不要 `subprocess.run(["bash", ...])`。

**21. dbg_run.py 不判定 PASS/FAIL**
退出码恒 0，是调试工具不能当判定。判定走 run_test.py。

**28. gdb `file` 用 MSYS 路径（`/d/...`）静默失败 → `load` 不报错但保留上一次驻留的旧镜像**（2026-09-17）
Windows 版 gdb 不认 MSYS 风格 `/d/...`：`-ex "file /d/.../zephyr.elf"` 只回一行
`...: No such file or directory.`，随后 `reset`/`load` **照常打印 "Start address / Transfer rate" 不报错**
——因无可加载段而静默不做任何事，**RAM 里仍是上一次会话的旧固件**。断点按裸地址设恰好命中旧镜像
同地址的指令 → "看起来一切正常"却读到完全错误的数据（实测：`0x10000d00` 本应 `lw ra,28(sp)`，
实读 `addi a1,sp,4`）。比 #61（`-x` 遇错中止）更隐蔽：**连"报错"都没有**。
处理：`file` 一律用 **Windows 路径**（`D:/zephyr4_0/...`，`cygpath -m` 亦可）。
防再犯：下断点后先 `x/3i $pc` 与 `objdump -d` 对照，地址/指令对不上 = 镜像不对（先别怀疑固件）。

**29. test.sh -g 退出码 124 是设计行为**
timeout 杀 continue 收场，判成败看日志不看退出码。

**50. run_test.py 与 dump_results.py 的 TEST_NAMES 双份硬编码，增删测试项漏同步 → mem 兜底错位/假 MISSING**（2026-09-09）
新增 broadcast_nack_recovery（第 11 项）时只同步了 run_test.py，漏了 dump_results.py（仍是旧 10 项）
→ broadcast 读回 MISSING 假 FAIL；且新项插在链接序**前部**，旧列表整体错位一格，把真实结果张冠李戴。
修法/防再犯：两份 TEST_NAMES 都是 **ztest 链接序**（不是 protocol.h 枚举序，两者不一致）；增删测试项后
两份同改，构建后 `nm --numeric-sort <elf> | grep z_ztest_unit_test` 复核全序，并同步更新各自注释里的实测序清单。
（本项漏同步实例的代码修复 = runner 跟进：dump_results.py TEST_NAMES 补 broadcast 到表头 + 更新其头部注释实测序。）

**57. 单核工程脚本硬编码 2os（入口 `0x100c0000` 或 test.sh `-b` 板级）→ 固件不跑、串口 0 行**（2026-09-16，模板缺陷）
单核 1os 构建 cpu1 入口 = `0x10000000`，2os = `0x100c0000`（readelf 实测，用户需求#8：单核一律 1os）。
模板从 2os 样板抄脚本时把 2os 值抄了进来，**两处同族错配**，症状相同（固件根本没跑 → 串口 0 行，
**极易被误判成"固件/配置问题"**，0/0 报告即此症状）：
- **Python 入口常量**：`TARGET_ROM_START = "0x100c0000"` → `dbg_run.py` 把 `$pc` 设到 0x100c0000。
- **test.sh 板级硬编码**：`test.sh` 直接 `west build -b lsqsh_evb@2os/lsqsh/cpu1`（不经 `SINGLE_BOARD`），
  但加载路径 `load_and_run_single` 用 `$pc = SINGLE_ROM_START = 0x10000000`（1os）
  → 构建入口 0x100c0000 与 `$pc` 错配。**入口常量对≠板级对，两者须分别核**。
修法/防再犯：① 入口一律 `os.environ.get("TARGET_ROM_START", "0x10000000")`（`dump_results.py` 用
`int(..., 0)`），2os 工程显式覆盖；② `test.sh` 的 board 一律走 `SINGLE_BOARD`/`TARGET_BOARD` 变量
（或 `build_single_test`），**禁硬编码 `lsqsh_evb@...`**。已固化为 qsh-test-gen 模板约束（§1.5）
+ qsh-test-validate 检查项 §1.9f。
**扫描范围**：本类残留必须**全工程扫，含未启用 case**——未进 `cases.yaml` 的 case 不会被回归带出，
但同样会被后续启用，漏扫即留下定时炸弹（`dma_test/case_destructive` 即因此漏网）。
**已修（2026-09-16 runner 完成并验证）**：入口硬编码 3 处（`dbg_run.py` / `dump_results.py` /
`run_test.py`）全部改走 `TARGET_ROM_START` 环境变量；`run_test.py` 的 `BOARD` 同步为
`@1os/lsqsh/cpu1`；样板遗留死常量删除；`dma_test/tests/case_destructive/test.sh` 的 `-b` 改走
`${SINGLE_BOARD}`（readelf 入口实测 0x10000000）。同时修复 `rtc_ls_test/prj.conf` 缺 libc 三件套
（见 #58）。涉及工程：adc_temper_test / dma_test / iopmp_test / rtc_ls_test / trng_test /
wdt_test / flash_test（**含未启用的 dma_test/case_destructive**）。回归验证：flash 7/7、rtc_ls 10/10、
trng 9/9、adc 6/6、dma 4/4、iopmp 5/5、wdt 5/5。范本 = otbn_test / otbn_selftest / mbedtls_test
（1os board + env 入口）；双核工程用 2os 入口，不需改。

**59. 样板脚本残留他工程识别常量 → 角色识别 `?` + mem 兜底假 PASS**（2026-09-16，模板缺陷）
从样板工程整体拷贝 `tests/scripts/` 后，他工程专属字符串未改成自身值（四类）：
- `dbg_run.py` 的 `TGT_BANNERS`/`CTRL_BANNERS` 残留他工程 banner（如 trng 的 `("CPU1 TRNG",...)`）
  → UART 角色识别对该端口显示 `?`，靠 ztest token 才能解析出结果（看似固件没跑）。
- `dump_results.py` 的 `TEST_NAMES` 残留他工程测试名（如 trng 的 `basic_16/distinct_32/...`）
  → mem 兜底读的是**本工程**内存槽，却按**他工程名**映射 → 输出**假 PASS**（实测 adc 如此），
  **比无输出更危险**。
- `run_test.py` 的打印/连接文案残留他工程名（如 flash 报告头写 "--- ADC temper test results ---"）。
- **docstring / 路径注释 / 文件头自述文字**残留他工程名（如 `dump_results.py` 头部自称
  "TRNG 单核工程版"、`dbg_run.py` 路径注释写 `trng_test/tests`、角色说明行仍写 `"CPU1 TRNG"`）。
  不影响运行，但误导 review 与后续复制。
判据：脚本中以**他工程名标识本工程/本文件**（识别常量、自述文字）即残留；单纯标注"从 X 样板改造"
的出处引用不算。残留只在串口不可用走 mem 兜底时才暴露，故须在生成阶段就核掉，勿等兜底。
防再犯：qsh-test-gen §1.5（复制后必须改的残留清单）+ qsh-test-validate §1.9g（含他工程名即 FAIL）。

**已修（runner 完成并验证）**：四类残留全部清除 —— `dump_results.py` `TEST_NAMES`
（adc_temper_test / dma_test / iopmp_test / wdt_test / flash_test）改为各自 ELF 的 ztest 链接序
（取值 `nm --numeric-sort <elf> | grep z_ztest_unit_test__<suite>__`）；`dbg_run.py` `TGT_BANNERS`
（adc / dma / iopmp / wdt）改为各自固件 banner，角色识别由 `?` 恢复 `TGT`；`flash_test` 报告头改
"--- Flash test results ---"；docstring/路径注释/文件头自述（5~6 工程）一并改净。trng_test 为合法源
（其值即 trng 自身）。判据实测：兜底通路修后 ≡ 串口直采（同名同 verdict，5/5）；串口路径无回归
（flash 7/7、adc 6/6、dma 4/4、iopmp 5/5、wdt 5/5）。

**60. 一个工程一份 `TEST_NAMES`，多 case 套件名/测试项不同 → 启用新 case 即 mem 兜底错位**（dma_test 已修）
`dump_results.py` 的 `TEST_NAMES` 按**工程**硬编码一份，但同工程的不同 case 可有不同套件名与测试项：
`dma_test` 的 `case_destructive` 套件名为 `dma_destructive`、6 项与 `case_default` 的 4 项全不同。
**一旦把该 case 加进 `cases.yaml` 启用**，mem 兜底会拿 `case_default` 的表去映射该 case 的内存槽
→ 全部错位/张冠李戴假结果（与 #50 同类假象，且更隐蔽：工程整体 PASS 却被错名）。
防再犯：启用一个工程的新 case 前，先比对该 case ELF 的套件名/项数与该工程现有 `TEST_NAMES`；
不符即按该 case ELF 的 ztest 链接序补一份对应表（脚本注释已标注此限制，见 `dma_test`）。
**已修（2026-09-16 runner 完成并验证）**：`dma_test/tests/scripts/dump_results.py` 改为
`TEST_NAMES_BY_CASE{case_default: 4, case_destructive: 6}` + `test_names_for(case)`，按 case 选表；
两表取值均与各自 ELF 的 `nm --numeric-sort` ztest 链接序一致。其余工程仍按上面防再犯流程比对。

**61. `.gdbcmd` 用无效命令名 → `-x` 模式整脚本中止（误判为烧录失败）**
`riscv64-unknown-elf-gdb -x xxx.gdbcmd` 里某命令无效 → 报 `Error in sourced command file: ...`
后**中止整个脚本**，后续 `load`/`set $pc`/`continue` 全不执行 → 表现像"烧录失败/固件不跑"。
典型无效命令 `set memory-write-packet-size`（本机 gdb 无此名）→ 正确写法
`set remote memory-write-packet-size 16384`（或 `set remotewritesize 16384`）。
根因：`-x` 源文件模式遇错即 abort；`-ex` 链逐条执行、遇错**只跳过该条**继续——
dbg_run.py / run_test.py / test_common.sh 用 `-ex`，故该无效行被静默跳过、错误被掩盖
（41x 提速实际由 `set remotewritesize` 提供）。
处理：手写 `.gdbcmd` 用正确命令名；排障优先 `-ex` 链或逐条确认命令名不会被静默丢弃。
跟进：各工程脚本里的 `set memory-write-packet-size` 无效行建议一并改 `set remote ...`（runner 跟进，纯清理）。
- **增补（2026-09-24，G-O39-4）：破坏面比"报错"更值得记 —— 整段静默、只留一行误导串。**
  实测：B 段脚本里写了无效的 `set memory-write-packet-size`，导致该段的 `reset` / `restore`
  **一条都没跑**，而输出里**只留一行 `No symbol table is loaded`**（并没有 `Error in sourced
  command file:`）⇒ **极易误判成"环境/连接问题"**，白查一轮。**纪律：每段 gdb 脚本收尾都要"断言
  关键动作真的发生"**（如断言出现 `Restoring binary file` / `Loading section`，否则中止），
  **不能只看"有没有报错"** —— 静默什么都没做比报错更危险。配比见
  `reports/2026-09-24-1/o39-nack-log-origin.md` §2.2；速查见 qsh-debug §7。

**62. 按名字杀 python.exe = 自杀：连坐 west/ninja/run_test.py 本体 → 全量回归每个 case 秒级 `BUILD FAILED`**（2026-09-16，极具迷惑性）
- 场景：回归中途想清残留构建进程，执行 `taskkill //F //IM python.exe`（连带 `west.exe`/`ninja.exe`）。
- 根因：`run_test.py`/`run_all.py`、`west`、`ninja` 本体**全是 python 进程** → 按名字杀把
  正在跑的构建/采集链路一起打死。
- 迷惑点：每个 case 在 1~60s 内**秒级整齐** `BUILD FAILED` / `rc=1`，看起来像代码回归或
  环境崩溃，**实际是自己杀出来的**。区分点：报错快而整齐（不像散点 FAIL），且时间点与"清进程"操作对齐。
- 正确做法：① 优先只 `TaskStop` 掉 runner 的父任务；② 清 gdb 残留：`netstat -ano | grep :1025`
  取对端 PID，只杀那一个；③ 清 west/ninja：`tasklist | grep -E "west|ninja"` 取 PID 后逐个
  `//F //PID` 杀。**永远不要 `//IM python.exe`**。
- 关联：#106 旧修复写法 `wmic ... name='python.exe' delete` 同属按名字杀，已一并纠正。
- 反向：**外部**中止（整棵 python 树消失、**无** `BUILD FAILED`）与本条相反，处置是直接续跑 → 见 #64。

**63. 对比两次回归前先确认 timeout 一致，否则"新 FAIL"不是回归而是超时掩盖的潜伏缺陷**（2026-09-16）
- 现象：把 mbedtls 用例"上次 PASS、这次 FAIL"当作回归判据。
- 根因：`run_test.py --timeout` 默认值决定一轮能跑多少项。短 timeout（如 300s）下 mbedtls
  各 case 有 11~17 项被超时截断成 `NOT_RUN`；拉长到 600s 后全部执行完，把**一直存在、
  只是从没跑到过**的 `sm4_bad_params` 暴露出来。
- 判据：两次 timeout 不一致 → **不可对比**。这类"新 FAIL"性质 = 超时掩盖的潜伏缺陷浮出水面，
  **不是回归**；报告必须与真回归（同 timeout 下 PASS→FAIL）分开标注，勿误判成共享层改动引入。
- 防再犯：跨轮对比先核两次会话报告头部的 `timeout=` 值（报告含"环境: ... timeout=Ns"）；
  差异表把"本次/上次 timeout"一并列出。慢速工程按 COVERAGE.md 实测耗时设 timeout
  （如 rtc_ls 全项 90~120s → 默认 150s）。
- 实例（2026-09-16 全量回归）：`sm4_bad_params` 在 4 个 mbedtls case 同时 FAIL，终判 =
  **驱动真缺陷**（`modules/crypto/mbedtls/library/sm4_alt.c` 的 `mbedtls_sm4_ctr_crypto()`
  丢弃 `HAL_SM4_CTR_Crypt()` 返回值恒返回 0，吞掉 NULL 参数错误），修复后定向复跑 4/4 PASS。
  但**性质上不是本轮改动引入的回归**——缺陷一直存在，只是短 timeout 从未执行到该用例。

**64. 长时全量回归后台任务可能被外部中止（整棵 python 树消失、DebugServer 独活）→ 全量回归按"可续跑"设计**（2026-09-17）
- 现象 A（被中止）：全量回归后台任务被 kill（任务状态 `killed`，output 尾部 `[killed]`），**整棵 python
  进程树同时消失、DebugServer 单独存活**；两次都在第 21 个 case（mbedtls `case_otbn_sha256_hw_sm3`）、
  约 66 分钟处中止。特征是**无** `BUILD FAILED`、时间点与清进程操作无关。
- 现象 B（不中止，反例）：同环境、同入口 `run_all.py`，改用 `nohup python -u regression/run_all.py
  > <log> 2>&1 &` 从 Bash 启动并自主轮询日志，**一次连续跑完 98 分钟（30 case 全执行到）无中止**。
- 根因：长时（>1h）批量任务**可能**被外部中止，**不是必然**。被中止的两轮都是 **Bash 工具的
  background 任务**——驻留在工具的任务进程组内，长时易被 harness 清理；`nohup ... &` **脱离工具
  任务进程组**可规避（现象 B 实测）。故"单次连续跑完"不是保证可达，但**用对启动方式可以达成**。
- 判据（**与 #62 相反，勿混淆**）：本条 = 进程树**整体**消失、**无** `BUILD FAILED`、时间点与清进程操作无关；
  #62 = **自己** `//IM python.exe` 按名字杀，症状是每 case **秒级整齐** `BUILD FAILED`（时间点与清进程操作对齐）。
  处置相反：本条直接续跑；#62 要先改清理方式（只按精确 PID / `TaskStop`）。
- 处理：① **想一次连续跑完 → 用 `nohup ... &` 从 Bash 启动（脱离工具任务进程组）+ 自主轮询日志**，
  不要用 Bash 工具的 background 任务；② 仍**默认按"可续跑"设计**兜底——`run_all.py` 跑头部 +
  **幂等 resume 脚本**（写 done 标记、重跑自动跳过已完成 case）补齐尾部；**中断点与续跑情况必须写进报告**，
  禁止拼成"一次跑完"；③ 监控心跳见"整棵 python 树消失"即判**外部中止**，**直接走续跑，勿重复排查环境**。

**70. `build_test`/`build_single_test` 无任意 CMake 变量的透传口；`_detect_board` 按目录名关键词判板**（2026-09-17）
- `test_common.sh` 的构建函数只透传 `overlay` 与 `conf`（第 4 参 = **追加一个 `.conf` 文件**，
  走 `-DCONF_FILE=...`），**没有传 `-D<任意变量>` 的口**。case 需要额外 CMake 变量
  （如 `-DWDT_BOUNDARY_TEST=y`）时只能绕过 `test_common.sh` 直接 `west build`——此时**极易照抄样板
  硬编码 `-b lsqsh_evb@...`**（正是 #57 / qsh-test-validate §1.9f 禁的），必须改走
  `${SINGLE_BOARD}` / `${TARGET_BOARD}`。
- `_detect_board`（`test_common.sh:142`）按**目录名关键词**判板：含 `controller`/`master`/`host` →
  `CONTROLLER_BOARD`，含 `target`/`slave`/`device` → `TARGET_BOARD`，**都不含即
  `ERROR: cannot detect board`**（如 `cpu1_wdt_test`）→ 该类 case 不能用 `build_test`。
- 改进诉求（是否改脚本待用户裁定）：给构建函数加额外 CMake 变量透传口；`_detect_board` 加
  1os/2os 或调用方显式传 board 的兜底。见 `OPEN_ITEMS.md` O-14。
- 残留：`wdt_test/tests/case_destructive_reset/test.sh:47` 仍硬编码 `-b lsqsh_evb@2os/lsqsh/cpu1`
  （该 case 为 flash boot 破坏性测试，2os 值本身正确，但写法属 #57 同类，应改 `${TARGET_BOARD}`）。**runner 跟进**。

**76. `build_single_test()` 的 `CONF_FILE` 是"追加"语义，无法表达"复用上游 test 时整体替换其 `prj.conf`"**（2026-09-17，模板缺陷）
`scripts/test_common.sh` 的 `build_single_test()`（`:344-372`）在 `<src>/prj.conf` 存在时拼
`-DCONF_FILE=<src>/prj.conf;<conf>`（**追加**，与 #70 的"第 4 参 = 追加一个 conf"同源）。这对
"810 自建固件 + case 差异化 conf"成立，**复用上游 test/sample 时不成立**：上游
`zephyr/tests/subsys/fs/fat_fs_api` 需要 `CONF_FILE="prj_mmc.conf"`（**整体替换**，其 `testcase.yaml`
的 `filesystem.fat.api.mmc` 即如此），用 `build_single_test` 会带回上游 `prj.conf` 的
`CONFIG_DISK_DRIVER_FLASH=y` → `test_fat.h` 选中 FLASH 分型（`DISK_NAME=DT_NODELABEL(test_disk)`）
→ 本板无该节点 → **编译失败**。`-DCONF_FILE` 没有"只替换不追加"的写法，`-DEXTRA_CONF_FILE` 只能追加。
- 处理：复用一个需要整体替换 conf 的上游 test 时，**本工程 `case_*/test.sh` 直接调
  `west build -b "${SINGLE_BOARD}" <src> -DCONF_FILE="<上游 conf>;<810 增量 conf>"`**，仍走
  `${SINGLE_BOARD}` 与 `print_launch_json_single`/`load_and_run_single`（不硬编码板级、不碰公共脚本）。
- "给 `build_single_test` 加 conf 覆盖开关"属公共脚本改造，**需裁定**（同 #70 的改进诉求）。
- 已固化为 qsh-test-gen §1.5 + qsh-test-validate §1.9j。

**77. 未进 `west.yml` 的模块可在应用 `CMakeLists.txt` 就地注册（`set(EXTRA_ZEPHYR_MODULES ...)`，免 CLI 参数）**（2026-09-17）
未注册模块（如 `modules/lib/pfr_tpm`）若只能经 `west build -- -DEXTRA_ZEPHYR_MODULES=...` 传，
则 `test.sh` / `run_test.py` / 手工编译三个调用点都要重复传参、易漏（正是 #70"无透传口"那类痛点）。
改为在应用 `CMakeLists.txt` 的 `cmake_minimum_required` 之后、`find_package(Zephyr)` **之前**
`set(EXTRA_ZEPHYR_MODULES "<绝对路径>")` —— `zephyr/cmake/modules/zephyr_module.cmake:39` 的
`zephyr_get()` 此刻读该 CMake 变量（**放 `find_package` 之后无效**，模块处理已跑完）。实测 1os/2os
均通过，`zephyr_modules.txt` 与 `zephyr_settings.txt`（`DTS_ROOT`）正确登记，**无需任何 CLI 参数**。
"注册"跟着工程源码走，三路径自动一致。`ZEPHYR_EXTRA_MODULES` 是 Zephyr 3.4 前废弃名（本树仍兼容读取）。
已固化为 qsh-test-gen §1.5。

**101. `dbg_run.py` 的 `flash_once` 超时若用 `communicate(timeout=)` 收尾，会落到 `kill` 而非 `interrupt+detach` ⇒ 连接残留**（2026-09-20 记录 / **2026-09-20 已修**）
原 `projects/*/tests/scripts/dbg_run.py::flash_once`：`proc.communicate(timeout=run_secs)` → `TimeoutExpired`
→ 原意 `proc.stdin.write("\x03")` + `detach`，实际落进 `except: proc.kill()`。
- 根因：CPython `Popen.communicate()` 在 `input=None` 时**会 close stdin 并置 `self.stdin = None`**
  （实测抛 `ValueError: I/O operation on closed file`）⇒ **prime 轮**那条 gdb 是**被 kill 的**，
  违反 qsh-debug §2.3（kill 留连接残留：下次 `target remote` 能连但 `reset`/`load` 失效 ⇒
  "flash 输出正常但串口 0 行 / 只有 `\x00`"）。
- **影响面**：只有 prime 轮这条路径 —— capture 轮本就走 drain 线程 + 定时写 stdin，是对的。
- **修法（已落地，2026-09-20）**：prime 轮改为 **drain 线程持续排空 stdout + `proc.wait(timeout=…)`**，
  超时分支再 `stdin.write("\x03")` + `detach\nquit`（stdin 全程可写，`\x03` 才真发得出去）；
  23 份 live `dbg_run.py` 均已改并加注释"不能用 communicate()"。**残留**：非 live 的
  `projects/i3c_i2c_switch_test/tmp/backup_*/tests/scripts/dbg_run.py` 仍是旧写法（备份，非模板）。
- 纪律：**任何"超时后写 `\x03`+detach"的脚本都不得依赖 `communicate(timeout=)` 保留 stdin**。
- 关联：qsh-debug §2.3 · qsh-test-gen §1.5（模板硬要求）· ref-gen-errors。

**103. `Path.write_text()` 在 Windows 把整份文件的 LF 翻成 CRLF —— diff 从几行炸到几百行**（2026-09-20 登记）
归档/报告落盘若用 `Path.write_text()`（默认 `newline=None`），Python 会把文件中**所有** `\n`
翻成 `\r\n`（不只本次新写的那几行）⇒ 一个只改两行的脚本改动，`git diff` 显示整份文件全变。
**修法**：写文本一律 `open(..., "w", newline="")`（或 `write_bytes()`）。已落地于
`common/serial_archive.py`（`SPEC_REFERENCE.md` §6.4.7 硬约束 ④）。
**当场识别法**：diff 行数远大于改动行数 ⇒ 先疑行尾翻转，别急着逐行看。
- 详版（含实测与失败样例）：`debug-knowledge` 知识库 `common/write-text-newline-flip.md`。
- 关联：`SPEC_REFERENCE.md` §6.4.7。

**107. `set -e` 下"命令替换 + 事后查 `$?`"是死写法 —— 赋值那一步就终止脚本**（2026-09-21 登记；本体属 O-24 工作区改动，**未提交**，勿当已结案）
- 现象：`scripts/test_common.sh::warn_if_build_stale()` 的外层 `timeout ${STALE_SCAN_TIMEOUT_S}` 触发时，
  脚本不是"打印 `[WARN] …已跳过` + continue"，而是**整条 `test.sh` 当场退出 rc=124**；WARN 一行打不出来、
  `if [ "$rc" = "124" ]` 分支**不可达**。
- 根因：`set -e` 在各 `test.sh` 顶部（行号随工程不同）与 `scripts/test_common.sh:32` 都开着。bash 对"**仅赋值、末位是命令替换**"
  的简单命令**取命令替换的退出码**为整条命令的退出码 ⇒ `raw="$(timeout … find …)"` 在 find 超时
  （`timeout` rc=124）时自身即"失败命令" ⇒ `set -e` 在**赋值处**终止脚本，后面的 `rc=$?` 轮不到执行。
- 实测（真实脚本，非合成）：`cd projects/iopmp_test/tests/case_default && STALE_SCAN_TIMEOUT_S=0.001 bash test.sh -r`
  → 只输出 2 行加载横幅后 **rc=124**、无 WARN；同函数加 `set +e` 唯一变量对照 → 正常打印 WARN 并 continue。
  ⇒ "离线已验证超时分支正确跳过"**只在未开 `set -e` 时成立**。
- 判据：任何 `var=$(外部命令)` 后跟 `$?` 判断的代码，在带 `set -e` 的脚本里都要复核；修法 =
  `if ! raw="$(cmd)"; then …` 或 `raw="$(cmd)" || rc=$?`（本文件 `test_common.sh:328` 注释即此约定）。
- 通法（跨硬件）：`debug-knowledge` common 域 `set-e-swallows-command-substitution-rc.md`；
  执行侧指针 `qsh-test-run` §4.2。
- 状态：本体属 **O-24 工作区改动（未提交）**；`set -e` 死代码的同批处置属 **O-25（已修 2026-09-21，忠实探针 A/B 验证：修前无 WARN 无标记行 rc=124 → 修后 WARN + 标记行 + rc=0）**。**代码未提交**。两处 `find` 现已改为 `raw="$(…)" || rc=$?`。
  本条**只沉淀判据，不代表已修 / 已验证结案**。

**108. "文档记已修" ≠ 代码里有 —— 未提交的工作区改动会被 `checkout`/`reset`/`west update` 静默冲掉，文档不跟着变**（2026-09-21 登记）
- 实例一：`projects/eth_test/tests/COVERAGE.md:200` 记 `phy_realtek_rtl8211f.c` 两处自旋已于
  2026-09-18 加 `k_timepoint` 超时（`PHY_RT_RTL8211F_RESET_TIMEOUT_MS = 1000`）。**2026-09-21 复核：
  该修复不在工作区、也不在任何提交里** —— 复核 `phy_realtek_rtl8211f.c:162/:174` 仍是原来的
  **无超时 `do…while`**；符号 `PHY_RT_RTL8211F_RESET_TIMEOUT_MS` 在全仓**只出现在文档里**
  （本条目 + `COVERAGE.md` + `DEFECT_ANALYSIS_2026-09-17.md` + `OPEN_ITEMS.md`），**任何 `.c/.h` 里都没有**。
  （注：任务书原话"全仓只存在于该 COVERAGE 文档中"**不准确** —— 另两份文档也有该符号，但不改变
  "源码里没有"这一结论。）
- 实例二：`OPEN_ITEMS O-16` 记的 `mdio_dwmac.c` 返回值修复同样不在树里。
- 根因：这些改动当时只在工作区、未提交 ⇒ 后续 `git checkout`/`reset`/`west update` **静默丢弃**；
  而文档（COVERAGE / OPEN_ITEMS）不跟着回退 ⇒ 看文档的人完全无从察觉，误当"已修"。
- **通法（引用任何"已修"结论前必做两步）**：① `git status --porcelain <file>`；② 在源码里
  **搜该修复的符号**（如上面那个宏名）—— 符号不存在 = 修复不存在。
- 现状参考：`zephyr` 仓库当前 `git status` 只有 7 处未提交改动，**驱动相关仅 `wdt_iwdg_ls.c`**
  （唯一幸存的一处）。
- 关联：**#88**（`--skip-build` 产物新鲜度）同族 —— 都是"看着像最新的东西其实不是"。

**109. 多工程同进程加载器只隔离了"工程脚本"、没隔离"工程辅助模块" ⇒ 裸名 import 跨工程串味，全量离线重放在半路整批断掉，却看起来像跑完了**（2026-09-22 实测）
- 现象：`python regression/replay.py --session reports/<会话> --l2-direct --freeze-time` 全量重放时
  **半路抛一个 `ImportError` 整批中止**，但**看起来像跑完了** —— 前面各 case 的 `[等价]` / `[差异]`
  行都正常打印，只有一条 Traceback；后半程 case **完全没被验证**（无任何"未跑"标记）。实测
  `51 个 case 只重放到第 38 个就断`（第 39 个起完全没跑）。
- 报错：`ImportError: cannot import name 'TEST_NAMES' from 'dump_results'
  (…/projects/mbedtls_test/tests/scripts/dump_results.py)`。
- 根因：重放在**同一进程**里依次加载多个工程；工程脚本用**裸名** import 自己的辅助模块
  （`from dump_results import TEST_NAMES`）。`load_project_module` 已用唯一模块名
  （`_replay_<project>`）隔离工程脚本**本身**，但**没隔离工程的辅助模块** ⇒ 首个 import 过
  `dump_results` 的工程（`mbedtls_test`，其模块只有 `unit_test_names`）把它缓存进 `sys.modules`，
  后到的工程（`otbn_selftest` / `otbn_test`）再 import 同名模块就**拿到上一个人的** ⇒ ImportError。
  与 `import` 补丁目标同族（G-RUN-1：`from X import Y` 绑定的是定义模块的属性，不是调用模块的）。
- **教训（可复用通法）**：**多工程同进程加载器必须隔离"工程本地模块"，不只隔离工程脚本本身** ——
  凡是模块 `__file__` 落在本仓 `projects/` 之下的，加载下一个工程前都要从 `sys.modules` 清掉；
  `common/` 下的**共享模块不动**（本就全局共享、无同名冲突）。判据按 `__file__` 归属，不按模块名。
- 已修（2026-09-22）：新增 `regression/replay.py::_drop_project_local_modules()`，在
  `load_project_module()` 开头调用（清 `sys.modules` 中 `__file__` 位于 `<repo>/projects/` 之下的条目）。
  **修前后全文重放对比**：修前 `重放 38（半路崩）` → 修后 `完成：重放 51 / 与归档逐字节相同 12 / 跳过 0`。
- 关联：全量重放的判定中性证据见 `SPEC_REFERENCE.md` §6.4.10；重放入口/用法见 `qsh-test-compare` §4。

---

## 环境与并发

**30. 共享树被其他会话修改**
构建失败先查是否共享源 API 变更；他人工作流正在动的共享源不碰。
硬件串扰：完成标记只认本固件 ZT_BOOT 已见的端口。

**32. i3c WDR 时序项间歇性 critical 异常**
约 1 case/轮，复跑即恢复。基线容忍 ≤1 case；驱动侧 `scl-overhead-ns` 工作流跟进。

**37. 共享树 dtsi 节点 label 改名 + 构建缓存掩盖**（2026-09-04）
共享树 SoC dtsi 节点 label 改名（如 lsqsh GPIO `gpio0`→`gpioa`）后，810 全部引用旧 label 的
overlay 报 `undefined node label`；但 `-p auto` 的旧 build 缓存不触发重配 → 部分 case 用陈旧固件
"假通过"，部分报构建错，失败模式割裂难定位。
处理：① 全量构建失败先 diff 共享树 dtsi label（`grep -n "gpio" zephyr/dts/riscv/linkedsemi/*.dtsi`）；
② 适配一律改公共层（复用收敛 §6.1.9），GPIO 使能块唯一来源 `common/gpio_toggle.dtsi`；
③ 固件类 overlay 适配后删全部 build 目录再回归，不信缓存。
同类迁移断裂信号：双核 case 串口全静默但 mem 兜底全 PASS → 查 cpu2 `zephyr,console` 是否被
`cpu2_console_uart5.overlay` 注入（`build/controller/zephyr/zephyr.dts` 应见 `console = &uart5`）。

**104. run_test.py allocate_session_dir 缺 date_str 参数**（2026-09-07）
- 现象：`TypeError: allocate_session_dir() missing 1 required positional argument: 'date_str'`
- 根因：新工程 run_test.py 生成时漏传 date_str
- 修复：`date_str = datetime.now().strftime("%Y-%m-%d"); session = allocate_session_dir(REPORTS_ROOT, date_str)`
- ⚠️ **2026-09-21 改号**：原为 `#38`，与"固件与构建"组的 `#38`（新工程脚本漏按住 cpu2）**撞号**；该组有 7 处外部引用，故不动，本条续编为 **#104**。

**105. DebugServer/CKLink 被残留 GDB 进程占用**（2026-09-07）
- 现象：`ERROR: Your CKLINK may be used by other program`
- 根因：测试中断后 GDB 进程未退出，持有 CKLink 连接
- 修复：`wmic process where "name='riscv64-unknown-elf-gdb.exe'" delete`
- ⚠️ **2026-09-21 改号**：原为 `#39`（撞号），续编为 **#105**。

**106. 串口被残留 Python 进程占用**（2026-09-07）
- 现象：`PermissionError(13, '拒绝访问')`
- 根因：测试中断后 Python 进程未退出，占用串口
- 修复：先按精确 **PID** 杀使用该串口的那个进程（`tasklist | grep python` 取 PID →
  `taskkill //F //PID <pid>`）。**禁用 `//IM python.exe` / `wmic ... name='python.exe' delete`
  之类按名字杀**——west/ninja/run_test.py 本体都是 python 进程，会连坐自杀（详见 #62）。
- ⚠️ **2026-09-21 改号**：原为 `#40`（撞号），续编为 **#106**。

**41. 新工程固件加载成功但串口无输出**（2026-09-07，待解决）
- 现象：固件加载正常（Transfer rate: 41 KB/sec），GDB 断点可达 zt_init/test_main，但串口 COM10 无任何输出
- 影响：adc/dma/flash/wdt/iopmp 等新工程均受影响
- 排查方向：uart1 初始化、时钟配置、overlay 配置

**42. 双核 run_test.py 串口捕获用 readline() 阻塞**（2026-09-07）
- 现象：mbox 测试固件加载成功但串口捕获 0 行（readline 等待 \n 超时）
- 根因：shell prompt `uart:~$ ` 与固件输出交错，readline() 无法完成单行读取
- 修复：SerialCapture._run() 用 `read(4096)` 替代 `readline()`（原始字节流不依赖行结构）

**43. 双核 test.sh 用 build_single_test 选错板**（2026-09-07）
- 现象：mbox cpu1 target 固件 entry=0x10000000（1os cpu1）而不是 0x100c0000（2os cpu1）
- 根因：`build_single_test` 硬编码 `SINGLE_BOARD=lsqsh_evb@1os/lsqsh/cpu1`，双核工程应用 `build_test`
- 修复：mbox test.sh 改用 `build_test`（_detect_board 自动识别 2os cpu1/cpu2）

**44. mbox overlay 用旧格式（3 cell + tx/rx names）**（2026-09-07）
- 现象：mbox 驱动报错 `msg size error, msg size: 64, mtu: 16`
- 根因：驱动改版后 mboxes 用 5 cell `<&mbox ch tx_w tx_d rx_w rx_d>` + `mbox-names="mbox"`，但测试 overlay 还是旧的 3 cell 格式
- 修复：改 overlay 为新格式，width 16/8 → 64（容纳64字节测试消息）

**45. mbox firmware 用 MBOX_DT_SPEC_GET(node, rx/tx) 分 tx/rx spec**（2026-09-07）
- 现象：新驱动改版后 mboxes 单 phandle，不再分 tx/rx
- 修复：固件改用单一 `MBOX_DT_SPEC_GET(node, mbox)`，同一 spec 用于 send 和 receive

**46. sec_iwdg 复位类型是 HART 复位（SEC_IWDT_HART_RESET=6）不是 FULL RESET（2026-09-07）**
- 现象：WDT 破坏性测试期望 reset_reason==SYS_IWDT_FULL_RESET(4)，实际得到 SEC_IWDT_HART_RESET(6)
- 根因：sec_iwdg 配 `WDT_FLAG_RESET_CPU_CORE`（=1，唯一被 sec_iwdg 匹配的 flag）→ 触发 HART 复位（只复位 cpu core），不是 full system reset
- soc_reset.h 枚举：SYS_IWDT_FULL_RESET=4（sys_iwdg full）、SEC_IWDT_HART_RESET=6（sec_iwdg hart）
- 修复：破坏性测试期望值用 SEC_IWDT_HART_RESET

**47. WDT flash boot 测试的完整软件流程（2026-09-07）**
```
1. 构建（普通 2os 固件，非 XIP）：west build -b lsqsh_evb@2os/lsqsh/cpu1 -DWDT_DESTRUCTIVE_RESET=y
2. 烧录含 bootram 头的 bin：zephyr_flash_bram_xip_*@0x08000000（非 zephyr.bin！zephyr.bin 无引导头 bootrom 不认）
3. 启动 DebugServer → GDB 清 noinit（test_phase 等）→ 杀 DebugServer
4. flash_program --reset-and-run 触发（固件已在 flash，重烧一次触发复位）
5. 全程无外部干预，WDT 自动复位 → PHASE 1 读 reset_reason
```
- 关键：烧录 flash 需独占 CKLink（先杀 DebugServer/CSKYFlashProgramer）；GDB 连接会 halt CPU，WDT_OPT_PAUSE_HALTED_BY_DBG 使 WDT 在 halt 时暂停，所以复位触发必须用 flash_program 硬复位（ConnectType=1 不 halt）

**48. flash 引导测试要烧 zephyr_flash_bram_*.bin 不是 zephyr.bin（2026-09-07）**
- 现象：烧 zephyr.bin 到 0x08000000 后 bootrom 不引导（PC 停在 0x1000000 bootrom，无固件输出）
- 根因：zephyr.bin 是裸应用，无 bootrom 引导头；zephyr_flash_bram_*.bin 含 0x0 引导头 + 应用（bootrom 按头信息加载）
- 修复：flash boot 烧 zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin@0x08000000

**49. GDB reset/detach 会 halt CPU 阻止 bootrom 从 flash 引导（2026-09-07）**
- 现象：GDB `reset` + `detach` 后 PC 停在 0x1000000（bootrom），固件不启动
- 根因：GDB 连接 halt CPU，detach 不清除 halt；bootrom 无法继续执行
- 修复：需要 flash_program 硬复位（--reset-and-run）触发，不 halt CPU；或用户断电重启

**75. 单核加载流程写 `APP_CPU_SRST=2`（保持 cpu2 复位）⇒ `is_app_cpu_running()` 返回 true ⇒ 带该守卫的驱动 API 全部 `-EBUSY`**（2026-09-17）
> ⚠️ **更正批注（2026-09-18 硬件验证批次 2 实测；证据来源：`tmp/hw2/` 日志）——本条前提未获实测支持**
> **证据可得性（2026-09-24）**：`tmp/hw2/`（含 `emmc_fresh_raw.log`）**已随 2026-09-24 的 `tmp/` 清理移除**，
> 外部不可回溯；下文数值均已**内联**，结论以**本批注正文**为准。被引作反证的 `reports/2026-09-17-4` /
> `2026-09-17-5` / `2026-09-18-1` **亦不在本仓**（`reports/` 现仅存 09-21 起；这三份从未提交、无法从
> git 恢复）⇒ 引用本条时**不得再向外指路**。
> - **寄存器实测（GDB 与固件自读，两路一致）**：`reset` 后 `x/1xw 0x4002200c` = `0x0`；
>   `set {int}0x4002200c = 2` 之后**回读 = `0x0`**（**并非 `>0`**）；写 `1` 后回读 = `0x3`，
>   **且写 1 会触发 bootrom 级重启**（写 1 = **释放**，释放后核从 bootrom 起 ⇒ 该输出属正常，见下方措辞更正）。
> - **固件侧**：emmc `case_block_driver` 用当前源码重新构建后，10 项带 `EMMC_SKIP_IF_GUARDED`
>   的用例 **SKIP 计数 = 0**，`disk_access_init=0`、读写 PASS ⇒ `is_app_cpu_running()` 为假
>   ⇒ **守卫未生效**。（原始日志 `tmp/hw2/emmc_fresh_raw.log`（**已随 2026-09-24 `tmp/` 清理移除**）：`[emmc_block_data] pass=7
>   fail=0 skip=0 total=7`、`[zz_emmc_block_edge] skip=0`，两套件 `skip` 均为 0。）
> - ⇒ 下文"写 2 ⇒ `>0` ⇒ 守卫误判 ⇒ API 全 `-EBUSY`"的因果链**基于 SDK 注释（`app_cpu_reset`
>   语义），未获实测支持**；此前"10 项 SKIP"的观测**未留下任何日志证据**（全树 grep 无
>   `app_cpu_srst=0x` 记录）。
> - **历史报告反证（2026-09-19 复核归档）**：`reports/2026-09-17-4`、`reports/2026-09-17-5`、
>   `reports/2026-09-18-1` 三份 `emmc_test_case_block_driver/emmc_test_report.md` 均记为
>   **`FAIL (8/10)`** —— 10 项**全部有结果**、8 PASS 2 FAIL、**`grep -c SKIP` = 0**（零 SKIP）。
>   两项 FAIL 分别是 `request_mutex_not_leaked`（修复前的 mutex 泄漏，`:441 jret is non-zero`）
>   与 `set_io_clock_out_of_range_not_silent`（那个**预期就该 FAIL** 的未修缺陷）。
>   ⇒ **eMMC 读写通路当时确实被跑到并被真正判定**。"`emmc_test` 12 项里 10 项 SKIP、默认回归下
>   eMMC 通路没被测"的说法**从未被观测到**（该项数 10 系 `emmc_app_cpu_guard` 套件缺
>   `ZTEST_SUITE` 声明所致，另见 #96，与守卫无关）。
> - **本条目保留**（登记的机制疑点仍有价值），但读者**不得当作已证事实**引用。**本更正不反推
>   "反之如何"**——写 1 触发 bootrom 重启这一事实照实登记。
> - ⚠️ **措辞更正（2026-09-19）**：上句原文曾写"其语义与 SDK 注释的'释放'**指向相反**"，
>   **该解释错误**。`platform.h:126-129`（写 2 = reset）/ `:152-156`（写 1 = release）——
>   **写 1 = 释放复位**；释放后核从 **bootrom 起**（正常启动路径）⇒ 串口打 OTP dump / `Flash Boot...`，
>   与注释『写 1 = 释放』**一致，不矛盾**。写 1 回读 `0x3`（`>0`）与"释放 ⇒ 在跑"亦自洽；
>   bootrom 输出落在 COM16 可由 #38 的 vuart 代理解释。**实测现象照实保留，解释以本条为准。**
> - **未决项（待父会话裁定）**：写 2 究竟是"保持复位"还是"释放"？**勿据本条改 `scripts/` 加载脚本**。
810 的单核加载流程在 `reset` 前写 `set {int}0x4002200c = 2`（`scripts/test_common.sh` + 各工程
`dbg_run.py`/`run_test.py`，见 #38）——本意是"按住 cpu2 防其自启劫持 uart1"。但该寄存器
`APP_CPU_SRST`（0x4002200c）**按 SDK 注释**是 **W1S 语义**：**写 2 = 保持复位、写 1 = 释放、写 0 无效**
（⚠️ **2026-09-18 实测未复现**：写 2 回读 `0x0`、写 1 回读 `0x3` 且触发 bootrom 重启——见上方更正批注）；
而驱动侧运行态守卫的判据是 `is_app_cpu_running() = (SYSC_SEC_CPU->APP_CPU_SRST > 0)`
（`modules/hal/linkedsemi/soc/rv32/qsh/include/platform.h:180-183`）——**"CPU 被按住"也满足 `>0`** ⇒ 返回 **true**。
- 后果：凡带该守卫的驱动（`zephyr/drivers/sdhc/sdhci-of-linkedsemi.c` 的 6 处 API 入口
  :189-194 / :219-224 / :304-309 / :319-324 / :415-420 / :886-891；其他 AMP 外设同型），
  **在默认单核加载环境下 API 全部早退 `-EBUSY`** → 通路整条静默不可用（eMMC 表现为 `sd_init`
  第一步 `sdhc_get_host_props` 即失败）。**⚠️ 此后果按上述推论；2026-09-18 emmc 实测未观察到**
  （SKIP=0、`disk_access_init=0`、读写 PASS），见上方更正批注。
- 迷惑性：用例全 FAIL / 串口无有效输出，**极易被误判成硬件或固件问题**。
- 做测试前先确认：被测驱动**是否有 `is_app_cpu_running()`/双核运行态守卫**；有则先核 `APP_CPU_SRST`
  的**实测回读值**（**语义待裁定**：SDK 注释称写 2 保持复位，2026-09-18 实测写 2 回读 `0x0`、
  写 1 回读 `0x3` 且触发 bootrom 重启，见上方更正批注）。用例可裸读 `SYSC_SEC_CPU->APP_CPU_SRST`
  （`#include <reg_sysc_sec_cpu.h>`）断言"`>0` ⇒ API 应 `-EBUSY`；`==0` ⇒ API 应成功"
  （**该判据建立在未证的因果链上，属待裁定项**，勿据此判"API 坏"），
  其余功能用例在 `>0` 时 `ztest_test_skip()` 并把原因打进 INFO。
- **若要在 1os 下真正跑通，需加载脚本写 1（释放）**——与"按住 cpu2 防串扰"冲突，属**需裁定**项
  （勿自行改 `scripts/`）。**⚠️ 2026-09-18 实测补充**：写 1 后回读 = `0x3` 且**触发 bootrom 级
  重启**（OTP dump + `Flash Boot...`）。⚠️ **解释更正（2026-09-19）**：这**不是**"与释放语义相反"——
  写 1 = **释放复位**（`platform.h:152-156`），释放后核从 **bootrom 起**属正常启动路径，故见 bootrom
  输出，**与注释一致**。该修法与"按住 cpu2 防串扰"的冲突仍在（释放正是用户需求#7 要防的串扰源），
  但"语义指向相反"**不成立**，勿再据此论证。
- 已固化为 qsh-debug §7 速查 + qsh-test-gen §1.0。

**115. 构建 FAILED 报在"你没改过"的文件里 ⇒ 先查工作区是否有 HEAD 之外的游离损坏，别按"自己的改动引发回归"排查**（2026-09-23 实测）

- 现象：改了某模块（本轮 = DMA）后构建 **BUILD FAILED**，但报错出现在**另一个模块**的文件——
  本轮是 `zephyr/include/zephyr/drivers/i3c/target_device.h` 报
  `error: expected expression before 'return'`（它被 i3c 驱动链引入 ⇒ 任何含 i3c 的固件都编不过）。
- 根因：该头文件在**工作区**被意外编辑（`if (...) {++`，一个游离的 `++` 紧随 `{`），
  与 HEAD 的**唯一**差异就是这一处（**未提交**，mtime 为当天）⇒ 表现为"改 A 却在 B 报错"。
- ⚠️ **报错行未必是损坏行**：本轮报错在 `:259`，真正的损坏在 `:258`。别只盯着报错那行找原因。
- **判据 / 纪律**：构建失败落在**你没碰过**的文件里 ⇒ **先 `git -C <该文件所属仓库> diff -- <file>`
  核对是不是 HEAD 之外的游离改动**（含别的会话的残留、误编辑、上游未合入的冲突残留），
  **不要先按"自己的改动引发回归"排查**。本仓库是多模块（`zephyr/` 等各自独立 git）+ 多会话共享树、
  工作区长期含大量**未提交**改动 ⇒ 这个排查面必然存在，不是偶发。
- **处置（按破坏性从低到高，逐级尝试，**先试低的**）**：
  1. **能局部修就局部修** —— 若漂移是**小而明确**的损坏（本轮 = 一个游离的 `++`），
     直接删掉那一处即可解除阻塞。**这是唯一不丢任何内容的做法，应作为首选。**
  2. **必须整体还原时，先保全再还原** —— `git checkout -- <file>` **不可恢复**
     （内容若从未 `git add` 过就再也找不回来）。在**多会话共享树**里，"来源不明的漂移"
     完全可能是**别的会话正在进行的编辑**。故还原前先把原文件**另存一份**，
     或用 `git stash push -- <file>`（可恢复），再还原。
  3. **拿不准就问** —— 该文件不属于你的写域、或你不清楚它为何漂移时，
     **报告并询问**比直接还原安全。**本项记录的事故形态本身就是"未经确认即还原"。**
- ⚠️ **不要照搬探针清理纪律（qsh-debug §8）**：那边清的是**你自己**产生的探针产物，
  这里动的是**来源不明**的漂移 —— **动作相同，风险不同**。
- 关联：**#30**（共享树被其他会话修改；构建失败先查共享源）同族；**#108** 是其镜像方向
  （**自己的**未提交改动被 `checkout`/`reset`/`west update` 静默**冲掉**）。
  ⚠️ **本项与 #108 合起来才是完整教训**：在共享树里 `git checkout --` / `reset` **两个方向都会伤人** ——
  既会冲掉自己的未提交改动（#108），也会冲掉别人的（本项）。
  **凡"还原到 HEAD"的动作，先问一句：被丢弃的内容有备份吗？** 已固化 qsh-test-run §6。

---

## i3c 切换 / CCC 恢复域（i3c_i2c_switch_test，2026-09-09）

**51. 广播 CCC NACK 后 controller 错误残留 → 后续 CCC 卡死（已修，有常驻回归项）**
广播 RSTACT whole 遇 target 切 I2C（不响应广播）会 NACK；此前 controller 错误残留（EVR BUSY/FCF/ERRF 等）
使后续 CCC 卡死/失败。修复集中在 `zephyr/drivers/i3c/i3c_linkedsemi.c`：
- `ls_i3c_xfer_reset` 加固 = 统一清理口：等 BUSY 清（超时软复位 master）→ while 清 FCF/ERRF/RXTGTEND 残留
  （带超时）→ flush RX/TX/Control FIFO → Status FIFO flush 移最后；
- `ls_i3c_do_ccc`：RequestTransfer(TSFSET) 后等 CFNFF 再填 CR 控制字（超时报错）；
- `ls_i3c_do_daa` 收尾 / `out_ccc_stop`：精简显式清标志（统一交 xfer_reset）。
EVR bit12=BUSY 定义（`I3C_EVR_BUSY_MASK/POS` / `I3C_EVR_BUSY` / `LL_I3C_IsActiveFlag_BUSY`）同步进
zephyr 与 HAL 两份寄存器头。
**回归保护** = i3c_i2c_switch_test 第 11 项 `broadcast_nack_recovery`：'N' 让 target 临时切 I2C → 首帧
RSTACT whole 预期 NACK → 切回后 RSTACT peripheral / RSTDAA 应正常 → 帧末 EVR BUSY=0。改动该恢复路径必跑。

**52. switch 二次 DAA 回归未解（I2C→I3C 切回后 re-DAA 失败；首轮 i3c_baseline DAA 正常）**
- 2026-09-09 两次回归复现：switch_i2c_to_i3c / switch_stress 仍 FAIL；仅 I2C→I3C 切换后的二次 DAA 场景失败，
  i3c_baseline 首轮 DAA 一直 PASS。
- **已排除**：do_daa 收尾精简非根因（回退显式清标志后仍 FAIL）。
- 排查方向：target 侧 I2C6 占用期间 TX FIFO 残留（切回 I3C 时 flush TX + prefill 是否到位）、
  控制器侧切换后总线/中断残留；TGT broadcast NOT_RUN（'B'/'E' 是否到达 target）待查。
- **判据注意**：end-of-run 立即 mem 兜底读数可能抓到未稳定状态（曾误报 TGT 全 FAIL），数分钟后稳定 RAM
  快照才一致；本域判据以 CTRL（DAA/读回发起方）稳定槽为准，读数冲突复读确认。

**53. mem 兜底 reason 槽真空：写入(枚举序 legacy 手动记账) vs 读取(链接序 glue 自动记账) 可能错位（根因待证）**
- 现象：FAIL 时 `ctrl_fail_reason`（vsnprintk，kernel API）有值，但 dump_results 按链接序 x/s 读
  `test_reasons[id]`（char[96]）全空，dump reasons 永远空串。
- 代码事实：i3c_i2c_switch 控制器仍 **legacy 手动记账**（`ctrl_test_end` 显式 `tr_record(枚举 id,...)`），与
  §6.1.5/glue 的**链接序自动记账**是两套下标约定并存 → dump(链接序) 与写入(枚举序) 槽位可能对不上；
  不是所有 FAIL 都进得了 dump 读的那个槽。
- 两个待证假设：① 写/读槽位错位（枚举序≠链接序，最可能）；② `tr_record` 内 libc `vsnprintf`
  （test_results.c）在 minimal libc 下空实现（CONFIG_NEWLIB_LIBC=n）。
- 验证：gdb 同时读 `test_reasons[枚举id]` 与 `[链接id]` 两位置对比；确认前别先入为主怪 vsnprintf。
  判 FAIL 的 reason 以串口 zassert printk / 探针为准，勿依赖 mem reason。

**54. pinmux 双状态切换：I3C 引脚缺 `drive-push-pull` → 切回后开漏位残留，read 失效（2026-09-14，根因已定）**
- 现象：I2C→I3C 切回后私有 read `ret=0 n=0`、GETBCR NACK；写正常；拔掉总线让 I3C5 独自切 pinmux 仍失败
  （纯寄存器残留，与总线毛刺无关）；切不相干的 i3c6 pinmux 不影响（排除系统性副作用）。
- 根因：I3C 引脚节点（`i3c5_scl_pj10`/`i3c5_sda_pj11`）**缺 `drive-push-pull`**。`pinctrl_configure_pin`
  里 `if(drive_open_drain) io_cfg_opendrain()` / `if(drive_push_pull) io_cfg_pushpull()` 是互斥分支；
  切 I2C 时 I2C5 引脚带 `drive-open-drain` 置了 `PMUJ_OD`（`IO_CFG[9].OD_FIR`，0x4006f2b8）的 bit(16+10)/bit(16+11)，
  切回 I3C 时 i3c5 引脚无 `drive-push-pull` → 分支不执行 → 开漏位不清理，SCL/SDA 卡在开漏。
- 实测：`PMUJ_OD` pre=`0x03000000`（PJ10/11 推挽）→ post=`0x0f000000`（PJ10/11 开漏残留）。
- 为什么"只有整板 load 能恢复、模块 CRG 复位救不活"：`OD_FIR` 在 **PMU 电源域**（reg_map 注释
  "power 域！模块复位不触碰"），I3C 模块 SRST3 复位不动它；只有全芯片复位把 PMU 一起复位回默认推挽。
  此前据此误判为"硅级 erratum"，实为配置遗漏。
- 复位默认 = 推挽（OD_FIR bit 清零），所以上电首次 I3C 正常；只有切换后泄漏。
- 修复：i3c5 引脚节点补 `drive-push-pull;`（sim 工程 `cpu0_i3c_controller/app.overlay`）。
  **target 侧同样隐患**：i3c_i2c_switch_test 的 i3c6（PJ08/09）也缺 `drive-push-pull`，回归时须同步补。
- 防再犯：pinctrl 双状态（尤其外设复用同一组线 I3C/I2C 切换）时，**每个状态都必须显式声明完整电气
  属性**（`drive-push-pull`/`drive-open-drain`/上下拉），否则上一次 apply 的电平配置会泄漏到下一次；
  且 PMU 域 pad 配置模块复位清不掉，只有全芯片复位才还原——排查"切回后失效"优先扫 PMU 域寄存器。

**98. `i3c_i2c_switch_test` 的 CTRL 串口同一轮出现 `PROJECT EXECUTION FAILED` 与 `SUCCESSFUL`（前半已定论）；RAM 侧 `ctrl_failed=1` 而 `ctrl_fail_reason` 为空（后半：2026-09-21 拿到 cpu2 手段后**不可复现**，改判"观测口径可疑"）**（2026-09-19）
`case_switch_core` 的 COM18 原始行里两个相反的完成标记**并存**（`parse_ztest` 取最后一条 ⇒ 报告显示 SUCCESSFUL）；
同轮 GDB 读 RAM：`ctrl_failed = 1`、`ctrl_fail_reason = ""`（空）、`test_results[10] = TR_FAIL`
（`broadcast_nack_recovery`），TGT 同槽 = `TR_NOT_RUN`。r1/r2/r3 **三次复现**。

**前半 —— 双标记来源已定论（2026-09-19）**：Zephyr ztest 提供**强 `main()`**
（`zephyr/subsys/testsuite/ztest/src/ztest.c:1458`）：`z_init_mock(); test_main(); end_report();
flush_log(); LOG_PANIC();`，其中 `end_report()` → `TC_END_REPORT` → `PRINT_LINE`（67 个 `=`）+
`PROJECT EXECUTION SUCCESSFUL`。而 app 的 `test_main()` 末尾**也**自打一条标记
（ctrl `i3c_i2c_controller_test.c:663` 依 `ctrl_failed`；tgt `main.c:49` 依 `any_fail`）。
switch 固件**从不调 `ztest_run_all`** ⇒ ztest `test_status` 恒 0 ⇒ `end_report()` **恒打 SUCCESSFUL（空跑）**。
⇒ CTRL = app(FAILED) + ztest(SUCCESSFUL)；TGT = app(SUCCESSFUL) + ztest(SUCCESSFUL)，与原始行逐条吻合
（两标记各占一行、中间隔 `====…` 分隔行 ⇒ 是两段并列输出，不是同一行的 ANSI 重复）。
- **危害**：解析器若"见到 `SUCCESSFUL` 即判 PASS"（或 `parse_ztest` last-wins）⇒ **CTRL 假 PASS**。
- **纪律**：同一核流出现相反完成标记 ⇒ **不得只取最后一条**，必须交叉 GDB 读 RAM 记账后再判
  （已入 qsh-test-compare §2）；新工程固件**不应再自打**完成标记（qsh-test-gen §1.4 第 5 条）。
- **同族增补（2026-09-20，G-HW5-3）**：第二条恒 `SUCCESSFUL` 的**产生点**锁定为 `ztest.c:76-82` 的
  `end_report()` → `TC_END_REPORT(TC_PASS)`（`tc_util.h:56-60` 的 `PRINT_LINE` + `:227`）。
  **只要工程"注册 0 个 ztest 测试"**（命令通道型固件：无 `ZTEST()`），`zt_run_all()` 的 `test_status`
  恒 0 ⇒ **无条件打 SUCCESSFUL**。后果两层：a) 真 FAIL 时 `parse_ztest` 的 **R6 fail-closed** 判
  `exec_conflict`（`ctrl_done=CONFLICTING_MARKERS`）—— 此例结论恰好正确；b) 但该固定字面量是
  **同族"假 SUCCESS 标记"**，会给**全 PASS 的核**也制造无谓的相反标记矛盾噪声（把本可干净的核
  染成 CONFLICTING）。⇒ 二选一：让命令通道工程的自裁决行**不被 `_EXEC_RE` 命中**（异形文案），
  或 **glue 在无注册测试时不调 `ztest_run_all`**（qsh-test-gen §1.4 第 4 条）。

**后半 —— "reason 被抹" 2026-09-21 用 cpu2 手段复测：不可复现，改判"观测口径可疑"**：
拿到 cpu2 调试/写监视手段后（见本节末「cpu2 通道」）复测，**未复现任何清零**：
- 控制器 ELF 地址：`ctrl_fail_reason`@`0x100665f0` / `test_reasons`@`0x10066658` / slot10@`0x10066a18`。
- 硬件写监视（`HWWP=3`）只抓到两个写者：`ctrl_set_fail`→`vsnprintk`、`tr_record`→`vsnprintf`，
  **都写入完整正确串**；4 次不同配置（带/不带 1026 客户端、带断点/带写监视、干净 DS）均无清零。
- 真实 harness（`run_test.py --case case_switch_core`）跑完后读 cpu2：`ctrl_fail_reason` 与
  `test_reasons[10]` **逐字节 = `rstact_whole_should_nack`**（`0x72 0x73 0x74 0x61 0x63 0x74 0x5f 0x77 …`），
  `test_results[10] = 0x01 = TR_FAIL`。
- ⇒ 原"有确定性痕迹 / 写过又被部分清零"**改判为观测口径可疑**：最可能的成因是按下面
  「cpu2 通道」的访问纪律读到了**未运行 / 陈旧**的 cpu2 RAM（cpu2 没跑过的会话里其 `.bss`
  读作全 0 初值或上一轮残留），**不是真被谁清零**。**"缺手段"卡点就此消解**。
- **不得据此推翻前半与 `broadcast_nack_recovery` 的 FAIL**：双 `PROJECT EXECUTION` 标记（前半，
  已定论）与 `broadcast_nack_recovery` 的 FAIL **真实且稳定复现**，是固定 `k_sleep(6ms)` 抢窗的
  **时序脆弱项**，与"reason 被抹"是两件事。
  ⚠️ **2026-09-24 更正**：此处"固定 `k_sleep(6ms)` 抢窗 ⇒ 时序脆弱项"的**归因**已被「增补 3」的
  极端对照证伪（"FAIL 真实且稳定复现"仍成立，"是抢窗造成"不成立）。见下方「增补 3」。

**cpu2 通道（2026-09-21 实测；本项据此消解"缺手段"）**：
- DebugServer 为 **cpu2 开第二个 TCP 端口 `1026`**（启动日志 `GDB connection command for CPUs(CPU0): …:1025`
  与 `CPUs(CPU1): …:1026` 并列）；`target remote 127.0.0.1:1026` 后 `info threads` 显示
  `Thread 2 (CPU#1)`、PC 落在 cpu2 地址域 ⇒ **这是对 cpu2 下断点 / 看 cpu2 现场的手段**。
  **前提：DS 启动时 cpu2 必须已脱离复位**，否则日志打 `ERROR: CPU_1: Fail to enter debug mode … AllUnavail: 1`
  且**只监听 1025、不监听 1026**（单核路径 `load_and_run_single` 会写 `APP_CPU_SRST=2` 按住 cpu2，
  紧接的 DS 重启就会命中此形态）；处置 = 先 `set {int}0x4002200c = 1` 释放 cpu2，再重启 DS。
- **★ 让 1026 出现的操作配方（2026-09-22，用户提供——用户拥有该硬件，硬件域权威；本次未实测复现）**：
  ① **先把 cpu2 跑起来**（运行态；单核路径写 `APP_CPU_SRST=2` 按住 cpu2 时**没有 1026**）；② **重启
  DebugServer**，让调试器**重新枚举核**；③ 之后 **1026 才出现**（= cpu2 那个核）。**软件动作、无物理
  操作、可自动化**（框架已有 `restart_debugserver_stable()`）。与上条「DS 启动时 cpu2 必须已脱离复位」
  是**同一件事的两种说法**（一个说状态、一个说怎么做）——**唯一路径，不存在"二者其一"**；与上面记的
  **实测失败形态恰好自洽**（都归结为"DS 重启时 cpu2 必须在运行态"）。配方细节与判据的单一来源见
  `qsh-debug` §6「cpu2 通道」。
- 目标信息报 `HWBKPT number is 3, HWWP number is 3` ⇒ **硬件写监视（gdb `watch`）可用**，
  实测能停在写者处并给出 backtrace。
- **★ 凡碰过 1026，下一次批跑前必须重启 DebugServer**：任何对 1026 的 gdb 访问（含 `detach`）
  都把 cpu2 留在 halted —— `detach` 只"恢复连接前状态"，实测 cpu2 停在 `__start`(0x10000000)
  不再前进 ⇒ 此后**不重启 DS，后续回归的 cpu2 侧全部不运行**（CTRL 串口 0 字节、cpu2 `.bss`
  保持陈旧值或全 0）。实测对照：同一条加载流程，DS 重启前 = CTRL 0 字节 / cpu2 PC 停 `__start`；
  DS 重启后 = CTRL 4954 字节 / 完整 token 流 / cpu2 PC 在 `arch_cpu_idle`。操作侧见 `qsh-debug` §2.3 / §6 / §7。
- **勿据此判驱动**。关联：#53（mem reason 写入/读取下标口径不一致，同属待证）· #51/#52（本工程恢复域）·
  `COVERAGE.md` 已把 `broadcast_nack_recovery` 记为脆弱项（固定 `k_sleep(6ms)` 抢窗口）。
- **增补（2026-09-21，归因隔离）**：该脆弱项 2026-09-17 批次 PASS、本轮 FAIL，而同期**框架改造**
  （日志文法统一 / 单出口 / `LOG_INF` 路径）**改变了采集与日志落地路径**、可能改变跨核 dispatcher 时延
  ⇒ **框架级改动后时序敏感用例必须重测**，其 PASS→FAIL **不得直接记成驱动回归**（驱动改动后同理）。
  判据补强（本轮）：失败**只在首帧**、后续步骤（r2/r3、EVR BUSY）全好 ⇒ "窗口错过"成立、**恢复路径完好**。
  ⚠️ **2026-09-24 更正：此"窗口错过"判据已被极端对照证伪**（见下方「增补 3」）—— 后续步骤全好
  只说明**恢复路径完好**，**不**说明失败源于"窗口错过"。
  归因纪律见 qsh-test-compare §5 第 3 条 / `SPEC_REFERENCE` §6.2.4 归因纪律注。
- **增补 2（2026-09-23，改动窗口已精确钉到提交级）** ⚠️ **其归因已被 2026-09-24 的「增补 3」证伪，见下**：该脆弱项在本轮三份会话
  （`2026-09-21-1` / `2026-09-22-1` / `2026-09-23-1`）**恒定 `FAIL 10/11`**、reason 恒为
  `rstact_whole_should_nack`（**首帧被 ACK**，`i3c_i2c_controller_test.c:566-572`）。
  抢窗用的**固定 `k_sleep(K_MSEC(6))` 是 09-15 的旧代码**（`9c808e9`）；但 `43b232e`
  （**2026-09-21 11:12:53**）给本工程引入单出口时，**恰好在该用例的计时区间内**又插入三处：
  `ctrl_test_begin` 的 `rlog_relay_drain()`、`ctrl_test_end` 末尾的 `k_sleep(K_MSEC(2))`、
  `test_main` 的 `rlog_wait_secondary_done(2000)` —— **与 target 侧 6~8ms 的 NACK 窗口同量级**
  ⇒ **归因 = 用例自身时序竞态 + 框架改造扰动，不是驱动回归**（唯一失配 = "首帧到达时 target
  已不在 I2C 窗口"；对照 `2026-09-21-1` 执行于 `43b232e` 之后 2 小时）。
  ⇒ 处置在**用例侧**（复跑取众数 / 把 6ms 调大 / 改为等 target 状态信号），
  **不要在驱动侧改 `i3c_linkedsemi.c`**。见 qsh-test-compare §5 第 3 条。
  ⚠️ **（2026-09-24 更正，勿静默并存）本段"抢窗"归因与其处置口径已被「增补 3」的极端对照证伪**
  —— 仍成立的只有"PASS→FAIL 与 `43b232e` 相关"这一**观测**；"窗口位置/抢窗"这一**机制**不成立。

- **增补 3（2026-09-24，极端对照证伪了"抢窗"归因）**：`broadcast_nack_recovery` 的 FAIL
  **不是** target 侧 NACK 窗口位置问题。做了一次**极端**对照（非"抢窗"式微调）：
  `NACK_WINDOW_MS` 10 → **500**（target 切 I2C 保持 500ms）、controller 等待 10 → **250ms**
  （深插窗口内部，比任何 dispatcher 抖动量级大 **40 倍**）、窗口后等待 12 → 600ms
  ⇒ 结果**逐字相同**的 FAIL（`CTRL:rstact_whole_should_nack`、10/11），两次复测一致
  （**2/2 确定性，非偶发**）⇒ **"到得太早/太晚"的时序推演被证伪** —— 在 500ms 宽窗口内、250ms
  时刻深插，该帧**仍被判"被 ACK"**。
- **关键否定证据（同轮）**：整个 broadcast 段 CTRL 串口**无任何驱动错误日志** —— 既无
  `No response to broadcast address`，也无 `CCC[0x%02x] broadcast error, SER=0x%X`
  （`zephyr/drivers/i3c/i3c_linkedsemi.c` 的广播分支**只在 `EVR & ERRF` 时才置 `-EIO`**）
  ⇒ `ls_i3c_do_ccc` 按设计返回 0，**不是"驱动把 NACK 吞了"**。后续三帧
  （RSTACT peripheral / RSTDAA / EVR.BUSY 清零）**全部 PASS** ⇒ 总线本身工作正常。
  ⚠️ **2026-09-24 推论边界更正（G-B1B2-3，勿把"无日志"读成"硬件不置位"）**：上面"无任何驱动
  错误日志"只证明**同步检查看不到**，**不**证明"控制器硬件不锁存"。同一轮实测：一次窗口内
  **直接地址**读失败后**约 15 ms** 才出现 `<err> i3c: … Address NACK`，而该笔的 **API 返回值早已是 0**
  ⇒ **硬件确实锁存了 ANACK，只是跨时钟域晚同步**；`ls_i3c_do_ccc` 只看 `EVR.FCF|ERRF`、
  `ls_i3c_request_transfer_flag` 复查 `SER.ANACK`，**两者都是"同步"复查** ⇒ **结构性错过**。
  ⇒ 本条**不能**据"无日志"断言"控制器硬件判该帧正常收尾"；正确表述是**"同步检查窗口内看不到"**。
  ⚠️⚠️ **2026-09-24 二次更正 —— 上面这条边界批注自己的依据已被推翻（勿静默并存）**：
  "实测 ANACK **晚 ~15 ms** 到位 ⇒ 硬件确实锁存、只是跨时钟域晚同步"**不成立**：
  ① **日志模式**：本工程 `projects/i3c_i2c_switch_test/cpu0_i3c_controller/prj.conf:2` =
  `CONFIG_LOG_MODE_DEFERRED=y` ⇒ **日志行的位置/先后不可信**（deferred 只延迟格式化与输出；
  **但自带时间戳仍 = 生成时刻**，见下「纪律收紧」）；
  ② **实证**：`reports/2026-09-24-1/i3c-b1b2-probe-run6.log`（同 `-run7.log`）第 118–119 行那条 `<err>`
  **被劈开、插进了后一行 `B1B2PROBE:S t=28ms …` 的中间** —— deferred 日志线程与 `printk` 抢串口的
  典型形貌 ⇒ 它是**打印晚了**，不是"错误位晚置位"（**位置不可信**）；
  ③ **更硬的矛盾**：通用路径 `ls_i3c_request_transfer_flag()`（`zephyr/drivers/i3c/i3c_linkedsemi.c:1161` 段）
  检出错误会 `msg_state = LS_I3C_MSG_ERR` 并 **`return -EIO`**，而该笔实测 **`ret = 0`** ⇒ 那条日志
  **不是从通用路径来的**，其**来源未确定**。
  ⇒ **"ANACK 晚到"不成立**；**"驱动检查窗口过窄"与"硬件无锁存能力"两侧均无据** —— 该问题**回到"未定"**，
  **不得据此裁定驱动**。
- **★ 2026-09-24 纪律收紧（勿把"位置不可信"读成"时刻不可定"）**：deferred **只延迟格式化与输出**，
  **不延迟时间戳采集** —— `LOG_ERR()` 宏**在调用线程内**就完成打包并提交（`zephyr/include/zephyr/logging/log_msg.h:375`
  `z_log_msg_finalize` → `zephyr/subsys/logging/log_msg.c:51 z_log_msg_commit(msg)` →
  `zephyr/subsys/logging/log_core.c:690 msg->hdr.timestamp = timestamp_func();`），时间戳在这一刻写入。
  ⇒ 正确表述是 **"串口行的位置不可信、自带时间戳 = 生成时刻"**，**不是**"生成时刻未定"（上一版写过头了）。
  可复用纪律：**判硬件时序读该行的时间戳、别看它在串口里排第几**；"位置 / 时间戳 / 来源"是**三件独立的事**
  （见 `debug-knowledge/common/deferred-log-timestamp-not-generation-time.md`）。
- **真问题（重新界定，2026-09-24 已部分回答）**：窗口内那笔直接读 `ret = 0`，却留有一条 `Address NACK`
  日志 —— **它由哪个调用点产生、生成于何时？** O-39 断点定位实验（GDB 软断点打在 `ls_i3c_log_err_type`
  五个候选调用点，两次独立跑共 **40 次命中**）实测：**`SER.ANACK` 从未置位**、**通用路径 `:1161` 零命中**；
  命中只落在 `:2364`（37 次，均 `SER = 0`，每个用例项起始的 ISR ERR）与 `:1761`（3 次，启动期
  `i3c_ccc_do_getmrl` 收 `SER.PERR`）⇒ **当前（B2 修复后）树里不存在能置 `SER.ANACK` 的操作**，
  那条日志**不可复现**（**结构性推断**：属当时存在、现已不在树中的一次操作 —— **非观测**，本实验未
  直接观测到那次 ANACK）。见 `reports/2026-09-24-1/o39-nack-log-origin.md`。全文与处置见 `OPEN_ITEMS.md` O-39。
- **剩下两条独立线索（未决，裁定权在驱动侧）**：① target 切 I2C 引脚复用后，I3C 广播在总线上
  **是否真的无应答** —— 需示波器看窗口内 `PJ08/PJ09` 的 **ACK 位**（`embedded-debug` §3.3：
  寄存器全对 ≠ 总线没问题）；② 本 SoC 控制器在**广播地址无人应答**时**是否在某个（同步窗口之后
  的）时刻置位 ERRF/`SER.ANACK`** —— **不能由"同步检查无日志"推断**（~~晚同步已实测~~，
  **该"已实测"已按上游二次更正撤回 —— 回到未定**）；须用**延迟复查**（读标志前先等足够量级，
  或由 ISR 侧异步复查）取证，但**"等 ≥15 ms"这一具体量级同样来自已被推翻的观测，不得当依据**。若确认该位
  **永不置位**，则 `r1 != 0` 判据在此硬件上**本身不可达**（属**判据**问题，**不得擅自放宽**，
  须驱动侧裁定；**驱动侧是否改成异步复查 `SER/ERRF` 属未决**，若改，应把本用例判据**收紧回
  `r1 != 0`** —— 用例已用 `INFO … win_broadcast_ret=` 保留现场）。
- 证据：`reports/2026-09-24-1/board-2026-09-24.md`「事项 ①」（探针已完整回退、镜像已重建）；
  边界更正与晚同步实测见 `reports/2026-09-24-1/i3c-b1b2-experiment.md` §2.4 / §3.2 / §8、
  `i3c-erff-experiment.md` §5（推论已收紧）。
  ⚠️ 其中"晚同步实测"一节**已于同日二次更正撤回**（依据不成立）—— 见上述二次更正批注，各落点均已挂批注。
- **★ 与「增补」/「增补 2」的更正关系（不得静默并存）**：增补 2 的归因
  "**用例自身时序竞态 + 框架改造扰动**"（连同增补里"'窗口错过'成立"的判据）**因本轮极端对照而被
  证伪** ⇒ 其**处置口径（"处置在用例侧、不要在驱动侧改"）随之失效** —— 被证伪的正是**唯一指向用例侧
  的证据**。**仍成立的只有**"PASS→FAIL 与 `43b232e` 相关"这一**观测**；**"窗口位置/抢窗"这一
  *机制* 不成立**。**"框架级改动后时序敏感用例必须重测"这条通则不变**（它不依赖本例机制）。

**118. `broadcast_nack_recovery` 的结账命令 `'E'` 在 `RSTDAA` 之后不可达 ⇒ 对端该项恒 `MISSING`、case 结构上不可能双核 PASS**（2026-09-24 实测）
- 现象：按 B2 改完判据后上板，**CTRL 侧判 PASS，整 case 仍 FAIL** —— TGT 列恒 `MISSING`；
  target 自己的 `SUMMARY:END` = `10/11`、`DONE:FAIL`，且 TGT 日志里**只有
  `START:broadcast_nack_recovery`、没有对应 `PASS/FAIL`**。
- 根因：第三帧 `RSTDAA` 清掉 target 的**硬件动态地址 `0x32`**，而 controller 侧 dev list 的
  `desc->dynamic_addr` **不同步清** ⇒ 紧随其后的结账命令 `'E'` 被打到**已不存在的地址**上、
  target 收不到 ⇒ TGT 侧该项**恒无记账** ⇒ 该 case **结构上不可能双核 11/11**。
  **历史归档该行 `来源=missing` 就是这个形貌 —— 既存缺陷，与本轮判据改动无关**。
- 处置（已落地）：结账前先 `i3c_do_daa()` 恢复 `0x32`（与 `test_main` 收尾处**本来就要做的同一套
  动作**，仅提前到 `'E'` 之前；**幂等、不新增被测行为、判据一字未改**）。回退该条只影响
  `broadcast_nack_recovery` 的 TGT 记账可达性，回退后退回"CTRL 11/11 但整体 FAIL(TGT MISSING)"旧形貌。
- **可复用纪律**：**会让从机丢失自身地址的测试项（`RSTDAA` / 复位类）之后不得直接结账** ——
  结账命令本身要走总线，必须先恢复寻址（重跑 DAA / HJ 重连 / SETNEWDA）。生成期规则见
  qsh-test-gen §1.6 第 10 条；检查项见 qsh-test-validate §1.10x。
- 关联：#51 / #52（本工程恢复域）· #98（同一实验场，本项即该实验 §4.2 定位）·
  `projects/i3c_i2c_switch_test/tests/COVERAGE.md`「已知基线」B-2。
- 证据：`reports/2026-09-24-1/i3c-b1b2-experiment.md` §4.2 / §4.3（修复后 `case_switch_core`
  双核 11/11 PASS，连跑两次一致）。

---

## IOPMP 域（2026-09-17）

**65. IOPMP 是"静默拦截器"，不能当作总线错误源 → DMA/外设 ERR 分支不可注入**（2026-09-17）
- 场景：为覆盖 DW DMAC 的 ERR 中断分支，用 IOPMP3（`0x40001800`，守 DMAC1/ETH1/EMMC1）配拒绝规则
  + allow-all 兜底。规则**确实生效**（被拒页的写未落地，写-回读确认 `addr0=0x40201ff, cfg0=0x18`），
  但 DMA 仍 `status=1`（`DMA_STATUS_BLOCK`）报"完成"、`err_cb` 恒 0、`DW_RAW_ERR` 恒 `0x0`
  ——**即使拒绝整个 1MB SRAM（含描述符池）**。
- 根因：该 SoC 互联对被拒/未解码访问**一律回 OKAY**（不产生 HRESP/BRESP error），IOPMP 只丢弃不回错误
  ⇒ 依赖"总线错误响应"的驱动错误路径**在本平台不可注入**。非法传输配置（非对齐/尺寸非整倍数/burst 失配/
  读外设区）亦无 ERR——或被驱动 `dma_config()` 在软件阶段拒绝（与硬件错误路径无关）。
- 处理：要测得 ERR 路径需**能返回 ERROR 响应的从设备**（对端/FPGA/仿真模型）或芯片级错误注入寄存器
  （本仓库未见）；在此之前用**代理实验 + 不变量论证**（`embedded-debug` §3.4）+ 与 TFR 分支的对称性
  论证替代动态注入，**不得记为"已实测"**。现场记录：`projects/dma_test/tests/notes/DMA_DW_ERR_PATH_INJECTION_ATTEMPT.md`；
  未决台账 `OPEN_ITEMS.md` O-8。可复用调试法已入库 `debug-knowledge/mcu/bus-error-response-not-injectable.md`。
- **IOPMP 配置附带事实**（配 IOPMP 时直接用，避免重新试错）：
  ① `IOPMP_EN`（bit1）**单独即可使能过滤**；`MD_EN`（bit0）单独不使能。`MD_CFG` bit4 是"曾违例"粘滞位，
  写 `INTR_CLR(0x34)` 清不掉（不影响功能，写回 `MD_CFG=0` 即恢复）。
  ② `iopmp.h` 的 `iopmp_config_enable(dev, enable)` **忽略 `enable` 参数**（恒写 `IOPMP_EN=1|MD_EN=1`）
  ⇒ HAL 无法关闭 IOPMP，只能直接写 `MD_CFG=0`。
  ③ `linkedsemi_zephyr_project/samples/iopmp/src/main.c` 调用的 `iopmp_config_region_napot4()` **全仓库不存在**，
  该样例未编译验证过、**不可作参考**（正确 API 是 `iopmp_config_region_napot`）。
  ④ **CFG 是打包寄存器**：`PMPCFG_STRIDE = 4` 字节/寄存器，一个 32 位 CFGx 含 **4 个条目**（每条目 1 字节）。
  `iopmp_set_pmpxcfg(base, idx, val)` 写的是 **`idx` 所落的整个 CFGx 寄存器** → 会连带改写同寄存器另外 3 条
  （`iopmp_set_pmpxcfg(base, 0, 0)` 把条目 0~3 全清零）。要按**单条目**改须用 `iopmp_set_pmpxcfg_by_idx()`
  （内部 `ROUND_DOWN(idx, PMPCFG_STRIDE)` 读改写）。**写 IOPMP 用例/驱动时按"整寄存器"保存与恢复**才正确
  （可复用陷阱模式见 `debug-knowledge/mcu/packed-register-setter-scope.md`）。

---

## MBOX 域（2026-09-17）

**66. MBOX 三条板级/驱动约束：共享区仅 4 KiB、ch0 被 flash delegation 占用、`mbox_mtu_get()` 恒 `-ENOSYS`**（2026-09-17）
动 mbox 用例前先知道（换人读源码也未必得出）：
- **共享 MBOX 区只有 4 KiB**：`lsqsh_evb_cpu1.dts:65-69` 的 `memory@100bf000`，上界被 cpu1 入口区卡死
  （`0x100bffff` 以上即 cpu1 入口区）。驱动为每个 consumer **同时**分配 TX+RX 两个 region ⇒
  **单帧 4 KB 物理上不可能**，大块数据只能分片（`case_mbox_big_crc` 用 79 片 × 64 B 帧 + 端到端 CRC32）。
  容量上界要顺着 `zephyr,memory-region = "MBOX"` 反查 SRAM 布局。
- **板级 ch0 = `mbox_consumer_qspi1`，在双核构建里是 flash delegation 专线**：cpu1 `soc_flash_ls.c:506`、
  cpu2 `soc_flash_ls_client.c:84` 都注册了回调，往 ch0 打测试流量会被 delegation 协议互相解析 ⇒
  consumer 覆盖只能做 **ch1..ch5**。判断某通道是否已被占用 = 把 `mbox` phandle 的引用者全 grep 一遍。
- **`mbox_mtu_get()` 是 per-device 接口（签名只有 `dev`，没有 `channel`）**，驱动里存在的
  `mbox_linkedsemi_tx_mtu_get/rx_mtu_get(dev, channel)` 是**未挂进 `api.mtu_get` 的死代码** ⇒
  该接口统一返回 `-ENOSYS`（`case_mbox_invalid` 已把它固化成断言项，驱动补齐后该项要改判）。
  照驱动里的 `*_mtu_get` 签名去写会直接编译报错（"too many arguments"）。
工程内逐项登记与未覆盖率见 `projects/mbox_test/tests/COVERAGE.md` §4。

---

## Flash 分区与容量（2026-09-17）

**67. boot flash 物理只有 4 MiB，分区表是陈旧信息；唯一可写的 `rwfs` 落在 15 MiB（超芯片）**（2026-09-17）
- **容量证据只有 `flash1` 节点的 `reg = <0x8000000 DT_SIZE_M(4)>`**（4 MiB，用户裁定 S-5）。
  **分区表的 offset/size 不能当容量证据**——现分区表是从另一颗大容量 flash 抄来的（排到 32 MiB）。
- 4 MiB 内的 4 个分区（`sbl` / `sbl_env` / `a_sec_image` / `a_app_image`）**全是固件镜像区、
  无空闲可写段**；唯一带 `PMP_R|PMP_W` 的 `rwfs` 在 `0xf00000`（15 MiB）、**超出芯片**
  （既有 7 项 flash 读写用例正是"在超芯片地址上读写"）。
- ⇒ 今后任何 flash **写/擦**用例：要么承认走 15 MiB（现状），要么先与用户确认可牺牲的区段；
  **只读用例没有这个限制**。

---

## WDT 域（2026-09-17）

**74. `iwdt_calculate_load()` 两个 uint32_t 相乘先回绕 → 饱和判据恒假（死分支）**（2026-09-17 记录 / **2026-09-18 已修 + 用例改判**）
`zephyr/drivers/watchdog/wdt_iwdg_ls.c:43-48`（原）：`uint64_t ticks = timeout_ms * frequency / 1000;`
—— `timeout_ms` 与 `frequency` **都是 `uint32_t`**，乘法/除法都在 32 位内完成、结果才赋给
`uint64_t` ⇒ 饱和判据 `ticks > UINT32_MAX` **恒假（死分支）**。sec_iwdg/pmu_iwdg 的
`clock-frequency = 32768`（`lsqsh.dtsi`），回绕起点 = `131072 * 32768 == 2^32`。
- 修前实际语义（host 侧同式复现）：131071999 ms → **4294934**（64 位算式为 4294967263）；
  131072000 ms → **0**（64 位算式设想"溢出饱和到 0xFFFFFFFF"）；0xFFFFFFFF ms → **4294934**。
- **修复（2026-09-18）**：`wdt_iwdg_ls.c:43-51` 现为
  ```c
  uint64_t ticks = (uint64_t)timeout_ms * frequency / 1000;
  return (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;
  ```
  显式 64 位提升 ⇒ 饱和判据 `ticks > UINT32_MAX` **可达**，超限即钳到 `0xFFFFFFFF`。
  修后语义：131071999 ms → **4294967263**；131072000 ms → **0xFFFFFFFF**；
  0xFFFFFFFF ms → **0xFFFFFFFF**。**饱和阈值 = 131072000 ms（≈ 36.4 h）**
  ⚠ **不是** 131072 ms —— 后者是旧的"32 位回绕起点"，两者**差 1000 倍**
  （档案 `DEFECT_ANALYSIS_2026-09-17.md §1.6` 已就地更正过这个错误）。
- **用例同步（已改判）**：`wdt_test/case_boundary` 的 3 项
  （`test_load_ticks_just_below_saturation` / `_saturation_boundary` / `_uint32_max_timeout`）
  及常量组（`wdt_boundary_test.c:50-55`）已改为 64 位语义、预期 PASS；
  `wdt_test/tests/COVERAGE.md` §2.2 脚注与 §3 饱和分支条目同步更新
  （原"死代码 / 未修 / 回绕周期 131072 ms"表述已作废）。
- **判据风险（保留提醒）**：这 3 项断言的是**当前驱动行为**；若日后有人把
  `(uint64_t)` 提升去掉，它们会 FAIL —— 这正是它们该有的判别力，不是假 FAIL。
- 状态：**已修**（驱动 + 用例双改判）。原 OPEN_ITEMS O-15 可收口。
- 可复用教训：拿"数学期望"当预期值前，先按 **C 整型提升/溢出语义**核算，或把式子抽到 host
  用相同类型复现（qsh-test-gen §1.6「边界常量必须有实测来源」的延伸）。

**100. 破坏性复位 case 的采集窗口按"标称超时"设 ⇒ 误判"没复位"（判据须取跨复位 `__noinit` 终态，或窗口 ≥60s）**（2026-09-20）
`wdt_test case_destructive_reset`（sec_iwdg）按 `run_destructive.sh` 的 18s（另两次 20s / 25s）采集，
串口在 `PHASE 0 complete, waiting for watchdog reset...` 之后**再无输出** ⇒ 初判"sec_iwdg 没复位"。
- **实际复位发生了**：GDB 读回 `__noinit test_phase` **终态 = 2**（只有 Phase 1 跑过才会写 2）；
  把窗口拉到 **90s** 才看到真正的重启在 PHASE 0 之后 **33.9s**（另一样例 4.1s ⇒ 延迟不确定）。
- 根因：`WDT_TIMEOUT_MS 2000` 是驱动给的**标称**值，sec_iwdg 的实际复位延迟受 IWDT 时钟/复位域影响
  可达 ~34s；**"窗口内没看到"≠"没发生"**。
- **判据（跨复位状态机通用）**：
  a. **首选 `__noinit` 终态**（`test_phase == 2` 等），不依赖在窗口内捕捉到重启；
  b. 只能看串口时，采集窗取**远大于标称超时**的值（≥60s），且**不得**按标称 2s 反推"是否发生"。
- 同类（"标称 2s、实际未知"的跨复位状态机）：`app_iwdg_cpu2_test case_reset`、`case_pmu_iwdg_full_reset`。
- 关联：#46/#47（sec_iwdg = HART 复位 / WDT flash boot 流程）· qsh-test-run §6 · qsh-test-compare §2 ·
  qsh-debug `ref-flash-variants.md` §2。

**102. `app_iwdg_cpu2_test case_reset` 的 flash 固化流不启动 cpu2（COM18 全静默）—— 2os 板上 bootrom 只引导 cpu1**（2026-09-20）
按 `run_reset.sh` 一次会话烧 cpu1@0x08000000 + cpu2@0x08100000、`--reset-and-run`，采 COM18（cpu2 uart5）
25s / 90s 均 **0 行**；COM16（cpu1）正常（`boot_cpu2: address: 0x10000000` + cpu1 supervisor + 2 项 ztest PASS）。
- 根因：cpu1 build `CONFIG_CPU2_BOOT_ADDR=0x10000000` 且 `# CONFIG_CPU2_IMAGE_HEADER is not set`
  ⇒ `boot_cpu2()` 只 `app_cpu_dereset_by_addr(0x10000000)`、**不搬运镜像**；启动日志只有一条
  `flash_image --exe_addr:0x10124000`（cpu1）。GDB 读 `0x10000000` = ELF magic、`e_entry=0x10000800`，
  **不是**本次构建的 cpu2 镜像 ⇒ **该 flash 流从未校准**（与 `run_reset.sh` 文件头自述的未校准项
  #1/#4"若 bootrom 不引导则按 §6.7.4 换 CPU1 构建变体"完全对上）。
- 事实：**这台 2os 板上 bootrom 只引导 cpu1**；cpu2 的镜像需**先 staging 进 RAM**（GDB `restore`
  或 cpu1 的 boot 搬运代码），**烧进 flash `0x08100000` ≠ 会自动起**。
- 判别顺序（cpu2 串口全静默时）：先 GDB 读 `0x10000000` 是否为**本次构建**的 cpu2 镜像
  （ELF magic + `e_entry` 对照该 case 的 `objdump`）→ 是 ⇒ 查固件；否 ⇒ 镜像从未被搬运上电。
- 关联：#47-#49（flash boot 流程 / 引导头 / GDB halt 阻止 bootrom）· SPEC_REFERENCE §6.7.3/§6.7.4 ·
  qsh-debug `ref-flash-variants.md` §2 · qsh-test-run §8。

**117. `acquire.iwdg()` 的采集窗口下界 = `clear()` 与采集线程的竞态；窗口过短 ⇒ 混入上一轮残片 / 静默假 PASS（判定对退化完全不敏感，只有归档完整性抓得到）**（2026-09-24）

`common/acquire.py::iwdg()` 两遍烧录法：prime 轮 → `ctrl_cap.clear()` → capture 轮。
**下界由 `clear()` 与采集线程之间的竞态决定，与判定逻辑无关**：窗口过短时，prime 轮的**收尾行**
会在 `clear()` **之后**才到达采集缓冲 ⇒ 混进本轮归档。
- **实测下界 = 11s**（两侧各复现 2 次）：**10s 两轮复现 BAD**（归档第 1 行 = 孤立的
  `PROJECT EXECUTION SUCCESSFUL`（prime 轮尾巴），第 3 行才是本轮 boot banner）；**11s 两轮干净**。
- **8s 更严重 = 静默假 PASS**：归档 = `[prime 轮完整残片 64 行] + [本轮：boot banner / ZT_BOOT /
  1 行 START]`，**本轮 capture 只跑到第 1 项**（末行 = `START - test_feed_within_window_no_reset`）；
  报告却按 **prime 轮残片**判 `PASS (18/18)`、两核 `SUCCESSFUL`。**被判定的是 prime 轮，capture 轮
  根本没跑。**
- **★ 本项真正的教训 = "判定对退化完全不敏感"**：`--case-timeout` 从 120s 一路压到 8s，**三个 case
  的 PASS/NOTE/MISSING 逐项结果一字未变**（全 PASS）⇒ **"逐项与基线一致"这条判据抓不到任何东西**；
  **只有归档完整性**能暴露退化。
- **归档完整性三判据**（每核独立，要求归档是「**一次、完整、从头到尾**」的运行）：**H1 头部完整** ——
  第一条 ztest 信号行必须是 `ZT_BOOT`（其前若出现 `START/PASS/FAIL/SKIP/INFO` 行 = "尾片段"）；
  **H2 唯一性** —— `ZT_BOOT <CORE>` / `Running TESTSUITE` / `PROJECT EXECUTION` 各**恰好 1 次**
  （>1 = 夹带了上一轮残留）；**H3 尾部完整** —— 最后一条非空行必须是 `PROJECT EXECUTION SUCCESSFUL`
  （否则窗口到点把收尾截断了）。判据脚本 `scripts/check_archive.py`（2026-09-24 迁出 `tmp/`；已**正反自测**：对既有
  `baseline120`(COMPLETE) 与 `probe8`/`probe10`(BAD) 的判定与人工读串口一致）。
- **增补（2026-09-24，单核适用性 · G-O35-1）**：`ARCHIVE: BAD` 有**两类**成因，引用前须先分清 ——
  ① **内容退化**（H1/H2/H3 真出问题，本项要抓的）；② **脚本口径误报**（**旧版** `tmp/check_archive.py`，
  **已于 2026-09-24 修复**）：**写死 `("CTRL","TGT")` 两口**、并把"`serial_<CORE>.log` 不存在"一律计 BAD，而**单核工程**的 case
  目录**本就只有** `serial_TGT.log`（`projects/<工程>/` 下 `cpu*` 子目录数 **1 = 单核**，cpu1=target；
  **2 = 双核**才有 `serial_CTRL.log`(cpu2)）⇒ **单核恒报 `[CTRL] MISSING` / `ARCHIVE: BAD`**，
  与归档是否干净无关。**判据**：应核几口由**工程核数**决定（**脚本现已自动判定**，见末句），**单核只核
  TGT**（H1/H2/H3 本身不因单核失效）；**"判 BAD 先分成因"仍是要做的动作**。实测 `reports/2026-09-24-1/i2cf_test_case_default` = `[CTRL] MISSING` + `[TGT] OK
  lines=86` ⇒ 整体 BAD；基线快照 `tmp/o35_base/i2cf_test_case_default` **同形貌** ⇒ 属 ② 非 ①。
  **危害**：24 个工程中 **19 个是单核** ⇒ 会把其正常归档**一律误判为坏归档**，进而诱使放弃在这些
  工程上做归档核验 —— 而那是本项**唯一**的退化探测器。**已修（2026-09-24）**：脚本迁至
  `scripts/check_archive.py`，**核集合由工程决定**（1 = 单核只核 TGT）、解析不出工程时退回按实际
  日志枚举并打 `[NOTE]`、**零日志 fail-closed**（`[ERR]`/rc=2，不再静默判 `COMPLETE`）；**上述误报
  现象留痕不删**。**仍存的口径边界**：按 ztest **原生 D1 文法**核 ⇒ **D2 token 型**工程仍会一律报
  BAD，须按工程文法人工核。操作面见 `qsh-test-run` §4.2。来源：`reports/2026-09-24-1/o35-hw-discriminator.md` §4.2。
  **增补 2（2026-09-24，崩溃与 BAD 同码 —— 已修）**：迁移后曾暴露两条缺陷，**均已修**（现象留痕不删）：
  ① **编码**：非 UTF-8 输出通道（重定向 / 管道 / 被捕获 ⇒ `sys.stdout.encoding` = `gbk/cp936`）下打印
  含 `⇒` 的 `[工程] …` 行抛 `UnicodeEncodeError`、**无判定输出**（无参数打印 `__doc__` 亦崩）；现存以
  `sys.stdout.reconfigure(encoding="utf-8", errors="replace")` 一处覆盖两条路径（同 **#91** 家族）。
  ② **★ 更要紧的：退出码语义**。原脚本 **rc=1 = `BAD`**，而 Python 未捕获异常**默认退 1** ⇒
  **崩溃与"真判到 BAD"同码、不可区分**（现由 `try/except BaseException` 兜住）。**定案四分**：
  `0 = COMPLETE` / `1 = BAD` / `2 = 用法·零证据` / **`3 = 内部错误`**（`[ERR] 内部错误（非判定结果）`
  走 stderr）。**判据：见 `rc=3` 即"没判"**，不得采信、不得当日归档结论用。
- **处置（本轮，只本工程）**：`projects/app_iwdg_cpu2_test/tests/run_test.py` 的 `--case-timeout`
  `default` **120 → 30**（= 实测下界 11s 的 ~3 倍，留裕量）；回归耗时 **12.3 → 3.1 min**。
  **`common/` 未改**（按既定口径"只调本工程默认值，不动公共层"）。
- **待评估（本项的核心悬置项）**：走"**两遍烧录 + `clear()`**"的**其它工程同样有该竞态** —— 只是
  它们的采集窗远大于自身用例时长、**尚未暴露**。**建议另立事项评估公共层 `clear()` 是否要加
  "清空后确认无残留"**（治本），而非逐个工程调默认值。
- **可复用教训**：窗口取值**贴边界不可靠**（它是竞态，11s 只是"本次两轮都干净"）⇒ 取实测下界的
  数倍；且**跨轮对比前先核归档完整性**（H1/H2/H3），别只看"逐项结果一致"。
- 关联：#100/#102（本工程"采集窗按标称值设"的同族）· #88（产物/证据新鲜度同族）· `qsh-test-run` §4.2 ·
  `reports/2026-09-24-1/board-2026-09-24.md`「事项 ②」。

---

## SPI filter / pfr_tpm 域（2026-09-17）

**78. `spif_add_cmd()` 表外野写：命中"命令已在固定表"分支时用过期索引算写地址（P0，2026-09-18 已修；数值已更正）**（2026-09-17 记录 / 2026-09-18 更正数值并修复）
`modules/lib/pfr_tpm` 的 `spif_add_cmd()` 在"命令已存在于固定表"分支用**过期 `idx`**计算写地址
`table_base + idx * 4`（`spi_filter.c:884`；`spif_add_cmd_with_dummy` 同型在 `:938`）。
**数值更正（2026-09-18）**：过期 `idx` = `-ENOSR` = **-63**（本项目 picolibc；**`-28` 是 `ENOSPC`，旧记录有误**）
⇒ 偏移 **-252 字节（-0xFC）**，写到 `SPIF_CMD_BASE - 252` 的**表外寄存器**；spif1 落点 =
**`0x4006FF30`**（落在 RTC 块 `0x4006FC00..0x4006FFFF`），**不是旧记录的 `0x4006ff90` / `-112`**。
- 触发条件：任何"传一个 DT 默认命令值"的自然写法都会命中该分支（**负向模式：断言成功 ≠ 行为正确**，
  反而污染实机寄存器）。
- 写 spif 测试时：改用 `spif_add_general_cmd()`（无该分支），并在用例内以 `spif_cmd_in_fixed_table()`
  **显式断言所选命令不在固定表内**，从源头规避。
- **修复（2026-09-18）**：两处写址改用固定表循环变量 **`off`**（即 `table_base + off * 4`，`off ∈ [0,30)`）。
  原 `idx` 是第一个扫描循环失败后残留的 `-ENOSR`，而固定表槽位号的唯一正确来由是 `off`。
  **已加回归护栏（2026-09-18 F1 波）**：`spif_test` 的 `cmd_table_add_remove` 末尾新增"固定表分支"段
  —— 用 `spif_set_cmd_by_idx()` 只改 HW、不动 SW 镜像 `fixed_cmd_tab[]` 制造 HW/SW 不一致，再调
  `spif_add_cmd(0x03)`，断言目标槽 `CMD` 回写且 **`EN==1`**（修复前 EN 保持 0 ⇒ 该段必 FAIL）。
  详见 `spif_test/tests/COVERAGE.md`「跳过项」首行 + `cmd_table_add_remove` 行。
  ⚠ 该段**只断言 `CMD`/`EN` 两个字段**（`spif_cmd_t` union 未清零，见缺陷 #20）。
- 可复用形态（**过期索引/变量作写地址**）已登记 qsh-driver-review §1.8 P0 清单 + qsh-test-gen §1.6。

---

## 驱动 / 硬件 / 工具 / 用例（2026-09-18 登记）

**79. `pwm_ls.c` 的 `resets` 成员声明与实例初始化项守卫不对称 → `CONFIG_RESET=n` 时编译失败**（2026-09-18）
`zephyr/drivers/pwm/pwm_ls.c` 的 `ls_pwm_config` **成员声明**带
`IF_ENABLED(CONFIG_RESET, (struct reset_dt_spec reset;))`（`:44`），而实例初始化项
`IF_ENABLED(DT_INST_NODE_HAS_PROP(n, resets), (.reset = RESET_DT_SPEC_INST_GET(n), ))`（`:158`）
**没有同样的守卫** ⇒ `CONFIG_RESET=n` 且 DT 节点有 `resets` 属性时，为不存在的成员做指定初始化
→ 编译失败。本板 `CONFIG_RESET` 默认 `y`（`dma_test` 未显式写也是 `y`）⇒ 当前**潜伏**，
一旦有人在 prj.conf 关掉 RESET 才暴露。
- 判据（跨驱动通用）：**带 `resets`/`clocks` 的 DT 节点，其驱动的 config 成员声明与实例初始化项
  守卫必须对称**；写新工程 prj.conf 前显式核对被测驱动 init 引用的 CONFIG 开关与 DT 属性守卫。
- 本工程处置：prj.conf 显式 `CONFIG_RESET=y` 规避；**未改上游**（纪律"发现缺陷只登记不修"）。
  细节见 `projects/pwm_test/tests/COVERAGE.md` §6。

**80. `spi_ls.c` 缺陷簇：P0 空指针解引用 / 同指针重配静默改频 / 分频循环越界 / 同步路径重复 complete**（2026-09-18 记录，只登记不修改）
`zephyr/drivers/spi/spi_ls.c` 四处独立缺陷（写 SPI 用例前必读）：
1. **P0 空指针解引用**：`spi_ls_configure()` 的 `-ENOTSUP`（半双工 `:72-75`、TI 模式 `:77-80`）与
   `-EINVAL`（超频 `:118-121`）都在 `ctx->config = config`（`:140`）**之前**返回；
   `spi_ls_transceive()` 走 `goto done` → `spi_context_release(ctx, err)`，在 `CONFIG_SPI_ASYNC=n`
   下执行 `ctx->config->operation`，而**首次调用** `ctx->config == NULL`（dev_data 零初始化、
   `spi_context_lock` 只写 `owner`）⇒ **load access fault**，固件进异常入口、串口静默、整轮套件中断。
   ⇒ 错误路径用例必须**先成功配置一次**建立 `ctx->config` 再触发错误分支。
2. **同指针重配静默改频**：`spi_ls_configure()` `:64-70` 在 `spi_context_configured()`（**指针比较**）
   早退之前就无条件 `SPE=0` + `BR ← scaler[0]`（=/8）⇒ 用**同一个 `spi_config` 指针**第二次
   `spi_transceive`，实际波特率被静默改成 `clock/8`（本板 9.375 MHz），无任何日志。
3. **分频循环越界**：`spi_ls.c:123` 循环上界写成 `i <= ARRAY_SIZE(scaler)`（数组长 6）⇒
   `frequency < clock>>8`（本板 < 292968 Hz）时越界读 `scaler[6]` 并写入不可预测的 BR。
   ⇒ 分频表下界取 `292968`。
4. **同步路径重复 complete（P0）**：`spi_ls_transceive()` 先 `spi_context_wait_for_completion()`
   （`:359`）、紧接着又 `spi_ls_complete()`（`:360`），后者 `:274` 调 `spi_context_complete()` ——
   非 ASYNC 下**无条件 `k_sem_give(&ctx->sync)`**（`spi_context.h:209-212`）⇒ 每完成一次传输
   `ctx->sync` 残留 1 个令牌。主设备侧因 ISR 把线程饿死到传输结束而**表象正常**，只有从设备侧暴露
   （返回后 `spi_ls_complete()` 立刻 `SPE=0`，等不到主机时钟）。对照 `spi_gd32.c:400-406`
   （同为 STM32-like SPI）：同步模式**不再** complete。
- 本工程处置（`spi_test`）：错误路径用例先 `spi_arm_instance()`；分频下界取 `292968`；第 2 条做成
  `reconfigure_same_cfg_prescaler` **刻意固化**；从设备侧加带运行时自检的
  `lb_drain_stray_sync()` 清残留令牌（**驱动修好后应整体删除**）。
- 共 11 条观察记在 `projects/spi_test/tests/COVERAGE.md`；**未改上游**。
- 可复用教训：**"配置失败的返回值检查必须早于任何已落地副作用"**；判据 = "返回值语义
  （`0` vs 有界超时 `-115`）反推对端是否真在等"（`debug-knowledge` mcu 域）。

**81. lsqsh 所有 GPIO 端口默认 `status = "disabled"` → CS/中断脚在非 GPIOA 端口时设备静默不就绪**（2026-09-18）
`lsqsh.dtsi` 里每个 GPIO 端口节点都带 `status = "disabled"`（`:183` gpioa、`:264` gpioj …）。
`spi_ls_init()` → `spi_context_cs_configure_all()`（`spi_ls.c:462` / `spi_context.h:220-236`）对每个
`cs-gpios` 条目做 `if (!device_is_ready(cs_gpio->port)) return -ENODEV;` ⇒ **CS 所在端口未使能时
`spi_ls_init` 直接失败，设备静默 "not ready"（无任何日志）**。
- 实例：`spi_test` 的 `spi2` → CS 在 `gpioj 15`，板级 `cs-gpios` 给出但 overlay 未使能 `&gpioj`
  ⇒ `spi2` 不可用；补 `&gpioj { status = "okay"; }` 后恢复。
- 防再犯：新工程 overlay 前先查外设的 `cs-gpios`/中断/复位引脚落在哪个端口，**该端口须显式
  `status="okay"`**（公共层 `common/gpio_toggle.dtsi` 只使能 `&gpioa`）。判据见 qsh-test-gen §1.0。

**82. `spi1/spi2` 与 `spi3/spi4` 是两个不同驱动（`spi_ls.c` vs `spi_dw.c`）；启用 DW SPI 实例须同时带 `&dmac2` + `timing-calibration-disabled`**（2026-09-18）
- **归属看 compatible，不看实例号连续性**：`lsqsh.dtsi:1179-1234` —— `spi1@4008a000` / `spi2@4008a400`
  是 `linkedsemi,ls-spi`（`spi_ls.c`，`DT_DRV_COMPAT linkedsemi_ls_spi`）；`spi3@4006e000` /
  `spi4@4006e400` 是 **`snps,designware-spi`（`spi_dw.c`）**。SPIF 通路：`spif3.spi = <&spi4>`
  （`modules/lib/pfr_tpm/include/lsqsh_pinctrl_pfr_tpm.dtsi:1063`）、`spif1/2 → spi3` ⇒ pfr 的
  `spi_master_communication` 走 **DW SPI**，与 `spi_ls.c` 无关。
- **使能 `spi3`/`spi4` 的两条硬依赖**（缺一即 `west build` rc=1）：
  ① `spi_dw.c:2221` 的 `DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, rx))` 要求 `dmas = <&dmac2 …>`
  的控制器**已使能**，且 `CONFIG_DMA_DW_LLI_POOL_SIZE`（`drivers/dma/Kconfig.dw_common:11`）只在
  DW DMA 编入时才有定义 ⇒ overlay 加 **`&dmac2 { status = "okay"; };`**（回环本身不走 DMA，
  走 `spi_dw.c:692-817` 通用 IRQ 通路）。
  ② `spi_dw.c:467-469` 在 master 且未禁用幅度校准时下标访问
  `info->timing_calibration_delay_arr[config->slave]`，该数组由 `DT_INST_FOREACH_CHILD_STATUS_OKAY`
  （`:2161-2163`）生成 —— **无子节点 ⇒ 空数组 ⇒ 越界读** ⇒ overlay 加
  **`timing-calibration-disabled;`**（binding `snps,designware-spi.yaml` 有此布尔属性）。
- **NSS/片选复用宏的命名变体**：该 SoC 片选叫 **`CSN`**（`SPI3_CSN0_FUNC2_PG13_PINMUX` /
  `SPI3_CSN1_FUNC2_PG12_PINMUX`，`lsqsh-pinctrl_func_2_3_4.h:119-120`），与板级
  `&spi3 cs-gpios = <&gpiog 13>, <&gpiog 12>` 逐脚对应 ⇒ **spi3 有片选复用**。判定某外设
  "有/没有某类复用"必须按**关键字全集**检索（`NSS|CSN|CS|SS|FSS|片选`），勿只搜 `NSS`。
- 主从方向：`spi_dw.c:407-411` 要求 DT `serial-target` 才允许从模式，而全树 `.dts/.dtsi/.overlay`
  里该属性**出现 0 次**（spi3/spi4 在所有用例中都是 master）⇒ 唯一有证据支持的回环方向 =
  **`spi3`(DW)=master / `spi1`(LS)=slave**；`spi3` 可当从设备属"未证伪"（证据强度降权）。

**83. "配对控制器"让渡引脚：pfr_tpm 的 `pinctrl-noinit` 与 SPIF master 的配对 SPI `pinctrl-0` 是同一坑的两个实例**（2026-09-18）
pfr_tpm 模块把若干控制器标为某滤波器的**配对控制器**，其引脚默认归滤波器所有；一旦把该控制器
**挪作他用（上游 master）**，这层假设不再成立，必须显式解除。**不解除不报错、不编译失败，只会静默无波形**。
- **I2C 侧（`pinctrl-noinit`）**：模块 dtsi（`lsqsh_pinctrl_pfr_tpm.dtsi:899-902`）给 `i2c1..i2c4`
  一律标 `pinctrl-noinit`，而 `i2c_ls.c:901` 是 `if (!dev_config->pinctrl_noinit) { i2c_ls_pinctrl(...); }`
  ⇒ 该实例 SCL/SDA 停在上电默认态（GPIO/模拟），控制器根本没有引脚。症状伪装成"接线错 / 从设备不在位"。
  修法：本工程 overlay `&i2cN { status = "okay"; /delete-property/ pinctrl-noinit; };`
  （属性在 `dts/bindings/i2c/linkedsemi,ls-i2c.yaml:48` 有绑定声明，删除合法），**改完须删 build 全量重建**。
- **SPI 侧（配对 DW SPI 的 `pinctrl-0`）**：`spif_switch_to_master(spifX)` 用 SPIF 自己的 `pinctrl-2`
  （master 态引脚），但**配对控制器**（`spif3.spi = <&spi4>`）默认仍持有板级 pinctrl ⇒ 两个 owner
  抢同一 net。修法：overlay 对配对控制器显式 `&spi4 { pinctrl-0 = <>; pinctrl-names = "default"; };`
  （参考资产 `samples/spi_master_communication` 即如此）。
- 另：**"配对控制器（O 侧）"与"上游 master"是两个不同角色**——前者 `i2cfN.i2c = <&i2cN>` 由 DT 可证
  （引脚重叠），后者是外接跳线到 `i2cfN.SCLI/SDAI` 的**任意**实例，**DT 里没有任何属性描述**。
  两个角色的引脚关系不同、不能互相推断；更不可用后者的假设去否定前者。上游 master 建议做成
  **DT alias**（用例一律 `DEVICE_DT_GET(DT_ALIAS(...))`），接线变更只改 overlay 一行。
- **与 i2cf 上板另两条独立坑的区别**（本节现象 = **总线无波形/设备静默不就绪**）：#110（单点 ACK 不能定位器件）
  与 #111（i2cf4 "零流量"真因是**接线错**）现象是 **有 ACK、也有数据段（`0xFF`）**，**不属引脚让渡问题**，勿混为一谈。

**84. 裸 `spi_nor_op` 用例必须适配器件运行态；`spi-ctrl-caps-mask` 是 disable 掩码（更正 INBOX G-C1C2-2 的"上电即进 QPI 4-4-4"）**（2026-09-18）
- **属性语义**：binding `dts/bindings/spi/linkedsemi,ls-qspi.yaml` 的位表全部写作 **"bit N : disable …"**
  （bit18 = disable 4-4-4）；驱动 `spi_nor_multi_dev.c:2490` 做
  `data->cap_mask = ~(cfg->spi_ctrl_caps_mask | cfg->spi_nor_caps_mask)` 之后才判
  `cap_mask & SPI_NOR_MODE_4_4_4_CAP`（`:2498` / `:2644` 的 `enter_qpi_mode`）。
  本工程编译产物实测 `.spi_ctrl_caps_mask = 262144`（= 0x40000）⇒ **关掉** 4-4-4，
  `spi_nor_enter_qpi_mode()` 不会被调用。
- **真实运行态**：**SPI 单线 + 4 字节地址**（`flash_size` 32 MiB > 16 MiB ⇒ `:2676` 使能 4B 地址、
  `:1380` 用 `addr_len=4`）。故 `qspi_flash_test` 两个 raw case 的修法是 `addr_len` 3 → 4，
  **不是**改 `MODE_444`、也不是"先退出 QPI"。
- **raw 用例必须断言内容**：只断言 `ret == 0` 会得到假 PASS（驱动只按参数发波、内容错位也返回 0）；
  `raw_read_1_1_4_ok` / `raw_read_1_4_4_ok` 即如此。
- 防再犯（读 DT 属性纪律）：**先看 binding 位表的措辞（enable 还是 disable），别按属性名想当然**。
  完整分析见 `810_driver_test/TRIAGE_QSPI_SPIF_2026-09-18.md` §1/§4。

**85. `LOG_MODE_DEFERRED` 下批量直吐大块输出会丢消息/合并行/中文乱码，必须给 log 线程留间隔**（2026-09-18）
deferred 模式下消息进 ring、由 log 线程异步排空。115200 下 150 字节约 13 ms 才能送完；
循环里不睡就一秒塞进 20 KB，**ring 直接被撑满，超出部分被静默丢弃**。
- 实例：`io_probe_test` 的矩阵 + ASYM 共 565 行逐行 `LOG_INF` 连发 ⇒ 观察到的现象是
  `IOPROBE total measured links (undirected) = N` 行**整条丢失**、若干行被**合并到同一行**、
  中文成片乱码；串口侧只有 ASCII 数字/脚名可可靠解析。同族于
  `spi_test` 的 75×75 邻接矩阵（每行 ~150 字符，共约 20 KB）。
- 处理：逐行打印必须 `k_msleep(20)` 让 log 线程排空（`IO_PROBE_LINE_GAP_MS`），**分块间隔须按段长动态调**
  （固定 20 ms 对 200+ 行段仍不够）；缓冲一律放 `.bss`（`static`）不放线程栈，并把
  `CONFIG_LOG_PROCESS_THREAD_STACK_SIZE` 提到 2048（150 字符行在 log 线程里格式化，默认 1024 偏紧）。
- ⚠ 这是"**最需要证据的时候报告最像好消息**"的同族坑（与内存兜底假 PASS —— INBOX G-P6-1 / G-D1，已由哨兵门修复 —— 同源）：
  报告缺行不会报错，只会静默少内容。
- ⚠ **衍生纪律（2026-09-24，同日收紧）：deferred 下"行的位置不可信、自带时间戳 = 生成时刻"** ——
  日志被 log 线程延迟排出、甚至**跨行插入**（同族但独立于"丢消息"：本例消息没丢，是**位置/顺序**不可信）。
  **区分两件事**：① **行的物理位置/先后** ⇒ 不可信，**禁止**用"日志出现的先后"推硬件事件次序；
  ② **该行自带的时间戳** ⇒ **= 生成时刻，可用**（`LOG_ERR` 宏在调用线程内打包并提交，deferred 只延迟
  格式化/输出；源码路径见 `log_msg.h:375 → log_msg.c:51 → log_core.c:690`）。⇒ **判硬件时序读时间戳、
  别看行排第几**。此前"打印时刻 ≠ 生成时刻 ⇒ 生成时刻不可定"的写法**过头了**，已更正 —— 见
  `debug-knowledge/common/deferred-log-timestamp-not-generation-time.md`。

**86. `io_probe_test` 首跑矩阵是下三角 artifact —— 驱动过的脚没还原，整张矩阵不可信**（2026-09-18）
- 实测：`links=2862 / asym=2624`（75 脚无向对数上限 2775，links 已超上限）。逐行统计 block1 的 `#`
  数 = `0,1,2,3,4,5,7,9,10,11,12,14,…` **严格递增**，`row49(PG13)` block1 = 36 个 `#`（占满 cols0..35）
  —— 即第 i 行对全部 j<i 都是 `#`，**完美下三角**。
- 根因：某脚被 `GPIO_OUTPUT_LOW` 驱动后，`pin_to_input_pullup()`
  （`gpio_pin_configure(dev,pin,GPIO_INPUT|GPIO_PULL_UP)`，`io_probe_test.c:239-242`）**没把它拉回高电平**
  ⇒ 之后所有行都把它读成低 ⇒ 误判"连通"。⇒ **只有上三角（i<j，`dump_links()` 的口径）才是真实信息**；
  下三角 + 全部 `asym` 都是 artifact。设计自带的 `positive_control_i3c_link` 仍 PASS
  （PJ10↔PJ08、PJ11↔PJ09 都是上三角）⇒ 检测通路没问题，坏的是**驱动后还原**。
  还原失败的真因见 **#93**（HAL `io_cfg_input()` 清错 `OE_DIN` 半区，撤销静默无效）。
- 附带：`pin_controllable` 实测 FAIL（PH12 `spif4.SCKI`、PG07 `spif4.IO3_IN` 不受控，
  PF12 `spif4.IO2_IN` 基线为低）—— spif4 三根脚行为异常，详见 **#94**。
- 防再犯：**扫描类工程必须加"还原自检"**——每脚驱动后还原、再读回自身，必须回到基线电平，
  否则 `LOG_ERR` + 单独 FAIL 闸（命名如 `pin_restorable`，与现有 a/b 闸并列）。
  **加闸前该矩阵不可用于任何接线结论**。见 qsh-test-gen §1.4 第 10 条。

**87. `i3c_dualcore_test case_mixed_400k_6m25` 实测有 flaky（约 1/4 复现）**（2026-09-18）
- 数据：改前基线（2026-09-17-1）PASS 35/35；某轮基线（2026-09-17-3）FAIL 22/35
  （尾部 `write_*` 13 项 `src=mem` 全 NOT_RUN，串口在 `write_delay_read_len` 后长时间静默、
  始终未见 `PROJECT EXECUTION`）；随后定向复跑 2 次均 PASS 35/35。4 次采样 3 PASS / 1 FAIL
  ⇒ **真 flaky**，不是框架回归、也不是稳定缺陷。
- 失效形态 = **套件中后段停摆**（不是单项断言失败）。
- 处置：跨轮对比时必须**同 timeout**；该 case 的"新 FAIL"不可直接当回归；后续如需闭环，
  按 qsh-debug 分层定位在停摆点断点抓现场。
- **补注（2026-09-21）：同族第二种 sub-mode —— 「prime 首轮零输出」**。原形态是**套件中后段停摆**
  （上条：尾部 `write_*` 全 NOT_RUN、长时间静默）；本轮实测到的是 **prime 轮 20s 内完全没有固件
  输出**（首轮就出不来）。二者**同族、不同 sub-mode**：一个是"跑起来后停摆"，一个是"根本没进首轮"。
  - 新形态实测（各 3 次）：`case_mixed_1m_12m5` = **FAIL / FAIL / PASS**；
    `case_pure_i3c_6m25` = **FAIL / PASS / PASS**。
  - **失败从不落在断言层**：全部 FAIL 都是 **`prime 20s 内无固件输出`**，且**都伴随
    `[ENV] restarting DebugServer`**；复跑时**同样的 prime 失败在 DS 重启后重试即成功**
    ⇒ 与用例断言无关，是**环境（DebugServer / 连接）层**的间歇失败。
  - **判据**：这两个 case 的**单次结果不可判**，必须**多次取众数**（多数 PASS 才判 PASS），
    并把 prime 失败 + DS 重启记进报告。与本次 G-E1/G-E2（DS 单点故障级联）同层，见 qsh-test-run §4.1。
- **补注 2（2026-09-21 晚）：第三种 sub-mode —— 「prime 首轮零输出，且 harness 重启 DS 重试后仍零输出」**。
  定向复测中 `case_pure_i3c_6m25`：首轮 `pass-1 (prime)` **20s 零输出 → `load_fail`**，harness
  重启 DS 重试**仍**零输出；随后 DS 重启后单跑复测 **PASS 35/35、零 RETRY**。同批其余 3 个 i3c case
  （`case_mixed_1m_12m5` / `case_mixed_400k_1m` / `case_pure_i3c_12m5`）一轮即过。
  原始日志：`810_driver_test/tmp/ethpc/flake_evidence/round1_FAIL_pure_i3c_6m25.log`。
- **⚠ 与 `OPEN_ITEMS O-24` 是两个独立问题、症状完全同形**：O-24 = `warn_if_build_stale`
  未剪枝导致 121s 吃掉判活窗口（已修）；**#87 是与它无关的既有 flaky**。
  **排查时不得互相引用为同一事**（2026-09-21 当天已因此误导过一次父会话）。

**88. per-case 构建粒度：`--skip-build` 复用前必须按该 case 自己的产物校验新鲜度**（2026-09-18）
- 事实：**每个 case 有自己独立的 `tests/<case>/build/` 目录**，同一工程"首个 case 已构建"**不覆盖**
  后续 case 的目录 ⇒ `--skip-build` 复用到的可能是**上一次会话**留下的 ELF。
  （`SPEC_REFERENCE.md:293` 早在 2026-08-31 就有勘误写明"首个 case 只构建它自己"——**文档是对的，
  只有 `run_all.py` 的代码注释没同步、且说反了**。）
- 放大效应：哨兵 `test_results_valid`（2026-09-18 加入）之后，旧 ELF 无该符号 ⇒
  `dump_results.py` fail-closed 成 `no_sentinel_symbol` ⇒ 内存兜底整体失效 ⇒ case 集体翻成
  看起来像"内核挂死"的 FAIL。实测：`cases.yaml` 启用 51 个 case、现存 56 个 ELF，其中 **41 个
  缺哨兵符号（73%）**。
- 已修（2026-09-18）：`--skip-build` 改为**哨兵感知**（仅当该 case **自己的** ELF 含
  `test_results_valid` 才加，否则强制重建 + `WARN`），并在 case 循环前新增
  `audit_stale_elves()` 显式列出全部缺哨兵旧镜像。
- 通用原则：**任何 `--skip-build` / 产物复用前，必须按该 case 自己的产物校验新鲜度；
  哨兵/版本标记类符号可作新鲜度判据**（qsh-test-run §4.2 已同步）。
- **姊妹形态：哨兵门只验"符号存在"、不验"源码新鲜度" ⇒ 烧陈旧 ELF 产生假 FAIL / 假 PASS**（2026-09-20，G-FAMFILL-1）——
  `--skip-build` 的 `_elf_has_sentinel()` 门（`run_all.py:513-518`）**只**检查 ELF 内是否含
  `test_results_valid` 符号（保证内存兜底可用）。**"含哨兵"≠"源码最新"**：fix 之后源码已改、
  但 build 目录仍是 fix 前的旧 ELF 时，哨兵符号在、门放行 ⇒ `run_test.py:1088` 构建段被跳过、
  `test.sh`（`AGENT_RUN=y`）也跳过构建 ⇒ **烧旧镜像**。实测 `i3c_dualcore_test` 的
  `case_mixed_400k_6m25`（build 目录 2026-09-18 21:40，早于 F5 修复）与 `case_pure_i3c_6m25`
  （09-18 18:18）两边 `.config` **md5 逐字节相同**、CMakeCache 参数一致 ⇒ 与构建路径 / Kconfig /
  优化级别 / 时序**无关**，**唯一变量是 ELF 陈旧**。
  后果**双向**：旧 ELF 里 `validate_read_underrun_16_32` 被 GCC `-fipa-icf` 折叠到别处 ⇒ **假 FAIL**
  （`write_len 2 != 1`）；`case_pure_i3c_6m25` 那份更老源码折叠到**没有断言的共享体** ⇒ **假 PASS**
  （更危险）。三重锁定：反汇编 + DWARF 行号 + 失败串格式。
  ⇒ 修法方向（**未改代码，`run_all.py` 在写域外**）：`audit_stale_elves()` 增加"build mtime < 相关源
  mtime 即强制重建"，或去掉该快路径。**通用**：哨兵 / 版本标记只证明"当时编过、格式对"，
  **不证明"对应当前源码"**；产物新鲜度必须比对 mtime（或内容哈希），与 G-CLEAN-2 / qsh-test-compare §5 同族。

**89. 以太网/网络类工程：`CONFIG_NET_CONFIG_INIT_TIMEOUT` 默认 30 s —— 网线未插时 ztest 开机阻塞**（2026-09-18）
`subsys/net/lib/config/init.c` 的 `net_config_init_by_iface()` 在 `check_interface()` 为假（网口未 up）时
`while (count-- > 0) k_sem_take(&waiter, K_MSEC(loop))` 等链路，`loop = CONFIG_NET_CONFIG_INIT_TIMEOUT / 10`，
**默认 timeout=30 ⇒ 最坏阻塞 30 s**。网线未插 / 对端未激活正是"网口 up 不了"的场景 ⇒ 开机白等 30 s，
把 case 超时窗口吃掉，表现为"load 后很久才有输出"或直接判 no_log。
- 修法：凡 ztest 用 `CONFIG_NET_CONFIG_SETTINGS` 的工程**必须同时设 `CONFIG_NET_CONFIG_INIT_TIMEOUT=0`**
  （`count=0` ⇒ 等待循环整体跳过，**静态地址仍立即写入**）。
- 已固化为 qsh-test-gen §1.7 + qsh-test-validate §1.3（WARN 级）。

**90. Zephyr 4.0 已移除 `net/arp.h` —— ARP 判据只能间接做**（2026-09-18）
本树 `zephyr/include/zephyr/net/arp.h` **不存在**，全树 grep 无 `net_arp_lookup`/`net_arp_query`
公开声明（ARP 表在 `subsys/net/l2/ethernet/arp.c` 内部，无导出查询 API）
⇒ 只能以**回包源 IPv4 == 对端地址**做等价覆盖（ARP 未解析成功则收不到该对端回包）。
- 已写入 `eth_test/tests/COVERAGE.md` 跳过项 16；生成网络类工程时按此设计判据。

**91. `dbg_run.py` 直接 `print()` 串口原始行 → GBK 控制台遇非 GBK 字符抛 `UnicodeEncodeError`，采集半途中断**（2026-09-18）
`python scripts/dbg_run.py --case case_default --raw` 时，`dbg_run.py:245` 的裸 `print(ln)`
在 Windows 控制台默认 GBK 编码下，遇到固件日志里的 `↔`（U+2194，`spi_loopback.c` 的调试打印）
即抛 `UnicodeEncodeError: 'gbk' codec can't encode character '↔'` ⇒ **脚本当场退出 rc=1**，
后续 RAW 输出全丢（SUMMARY 因先打印而幸存）。
- 影响面：任何固件日志含非 GBK 字符（箭头/数学符号/生僻汉字）的工程。
- **同族复发（2026-09-24，脚本侧）**：已修 `scripts/check_archive.py`（见 #117 增补 2）；**待修**：
  `scripts/dump.py`（`--all` 的 `⇒`）、`scripts/normgroup.py`（`»`）、`tmp/a2/hw_cmp.py`（`✓`/`✘`）。
  三者**同形**：输出被重定向 / 管道（`sys.stdout.encoding` = `gbk/cp936`）即抛 `UnicodeEncodeError`
  —— `check_archive` 还叠了"**崩溃 rc=1 与它自己的 `BAD` 同码**"的加倍风险。
  **纪律**：脚本输出避开非 GBK 字符，或统一起手 `sys.stdout.reconfigure(encoding="utf-8")`。
- **同族复发（2026-09-29，`dbg_run.py`）**：`dbg_run.py` 顶部**无** `reconfigure`（与 `check_archive.py`
  不同）⇒ 非 tty（重定向 / 管道，`sys.stdout.encoding` = `gbk/cp936`）通道下打印 **R9** 的 FAIL reason
  时，reason 串里的全角 `⇒`(U+21D2) 即触发 `UnicodeEncodeError` ⇒ **SUMMARY 摘要循环中断、其后的 `--raw`
  段整个不打印**（原始串口行随之丢失）。**状态 = 部分修复（2026-09-29）**：R9 理由串的 `⇒` 改 `=>`
  （它是该串里**唯一**编不进 gbk 的字符）⇒ 原触发消失、离线复算 gbk 下各项仍判 FAIL 且与归档逐字相同
  （仅箭头变）；**兜底缺口未修** —— `dbg_run.py` 仍无 reconfigure ⇒ 非 tty 下凡非 ASCII 仍会崩。
  **处置**：跑前 `PYTHONIOENCODING=utf-8`（或给 23 份 `dbg_run.py` 补 reconfigure）。见 `OPEN_ITEMS` O-47 · `qsh-test-run` §4.2。
- 修法（模板统一，全 23 份 `dbg_run.py`）：启动时
  `sys.stdout.reconfigure(encoding="utf-8", errors="replace")`，或把 `print` 包在
  `try/except UnicodeEncodeError`。已固化 qsh-test-gen §1.5 + ref-gen-errors。

**92. `wdt_iwdg_ls.c:73` 守卫用整寄存器非零判定，被 `IWDT_CNT_CLR`(bit3) 误触发**（2026-09-18）
`test_setup_twice_ebusy` 两项失败方向与预期相反（该返回 0 的返回 `-EBUSY`）。
- 根因：该守卫判"整寄存器非零"，而 `feed` 后硬件留存的 `IWDT_CNT_CLR`(bit3) 使寄存器非零
  ⇒ 误判为"已在使用"。**不是** `install_timeout:119` 清 CTRL 绕过 EBUSY 守卫（P1-3）那条路径：
  两项 `setup` 之间无 `install_timeout`，**勿再按 P1-3 归因这两项**。
- 完整定位见 `TRIAGE_MISC_2026-09-18.md` §1。

**93. HAL `io_cfg_input()` 清错 `OE_DIN` 半区 —— OUTPUT→INPUT 撤不掉输出使能，pad 一直保持旧值**（2026-09-18 记录；**2026-09-20 已修**）
`modules/hal/linkedsemi/soc/rv32/qsh/ls_soc_gpio.c` 的四个函数对 **同一个 `OE_DIN` 寄存器**各写一半：

| 函数 | 动作 | 半区 |
|---|---|---|
| `io_cfg_output()` | 置 `1<<pin<<16` | 高半区 = 输出使能 |
| `io_is_output_enabled()` | 读 `1<<pin<<16` | 高半区 |
| `io_cfg_disable_output()` | 清 `1<<pin<<16` | 高半区（**正确**） |
| `io_cfg_input()` | 清 `1<<pin` | **低半区 = DIN 回读位** |
⇒ `gpio_pin_configure(dev, pin, GPIO_INPUT|GPIO_PULL_UP)` **从不撤销输出使能**（清的是只读回读位，
是个空动作），pad 保持驱动在上一次写入的值；而驱动读回
`zephyr/drivers/gpio/gpio_lsqsh.c::gpio_ls_port_get_raw()` = `OE_DIN & 0xffff`（读的正是 pad 值）
⇒ **一根脚一旦被驱动过，此后永远读到那个值**，且**任何调用都返回成功、无日志**。
- 实例：`io_probe_test` 逐脚驱动 LOW 后想还原成高阻输入 → 整张矩阵"完美下三角"、
  `links=2862` 超 75 脚无向对上限 2775（首跑）。见 #86。
- **绕过（不改 HAL）**：还原走 `gpio_pin_configure(dev, pin, GPIO_DISCONNECTED)`
  （内部 `io_cfg_disable()` → `io_cfg_disable_output()`，清的是高半区）再配
  `GPIO_INPUT|GPIO_PULL_UP`，**并且必须读回验证**（见 #86 的还原自检闸）。
- **影响面**：任何"先输出、再改回输入"的 lsqsh GPIO 用法，不限于单工程。
- 可复用检查维度（已入 qsh-driver-review §1.10）：**enable/disable 成对函数的"撤销位"必须
  与"使能位"同掩码同半区**；撤销落到只读回读位 ⇒ 撤销静默失败、状态残留污染后续所有观测。
- **已修（2026-09-20，用户裁定"O-17，修复"）**：`ls_soc_gpio.c::io_cfg_input()` 第一句改为
  清 **OE 高半区** `OE_DIN &= ~(1<<pin<<16)`（原 `&= ~(1<<pin)` 清的是 DIN 回读位）。
  **语义依据（权威头文件，非对称性推测）**：`reg_app_pmu_rg.h` 的 `IOE_DIN_OE` 字段定义
  `GPIOE_DIN_MASK=0xffff @0` / `GPIOE_OE_MASK=0xffff0000 @16`；同库 leo 版
  `soc/rv32/leo/ls_soc_gpio.c::io_cfg_input()` 只清 OE、从不写 DIN。
  **硬件实验**（独立最小工程，除这一行外无其它改动）：对 DIN 低位**置 1 与清 0 均无任何可观测效果**
  （方向不变、回读不变）⇒ 原低半区写确为空动作，**没有**"另行用途"需要保留。
  **修复前后对比**（同一工程同一序列 `GPIO_OUTPUT_LOW → GPIO_INPUT|GPIO_PULL_UP`）：
  修复前 `OE_DIN=0x00100000`（OE 位仍 1、`port_get_raw=0`，缺陷）；修复后
  `OE_DIN=0x00000010`（OE=0、DIN=1、`port_get_raw=1`）。四根脚（PB04/PB01/PB15/PE02）一致。
  **回归**：`io_probe_test` 复跑 `links=22 / asym=0 / not_restored=0`（与既有基线一致）；
  `trng_test` 15/15 PASS；`spi_test` 18 PASS + 1 FAIL(`link_probe`，既有缺陷) + 7 SKIP，与基线一致。
  影响面清单：`OE_DIN` 低半区在**全仓仅此一处**被写（`ls_soc_gpio.c` 其余引用全是高半区读写）；
  `io_cfg_input()` 调用方中的 `pinctrl_lsqsh.c` 走的是 `io_cfg_input_pure()`（不受影响），
  `psram.c` 的 PSRAM 数据脚调用点上一步 OE 本就为 0（行为等价）。**代码未提交**。
- ⚠️ **未由此修复解释的**：`io_probe_test` 的 `PH12`/`PG07` not-controlled **复跑后依旧存在**
  （见 #94）—— 与 `OE_DIN` 无关；疑为 `CONFIG_PSRAM=y` 的 `psram_pin_init()`
  把 `PH12`(PSRAM_DAT3)/`PG07`(PSRAM_CSN0)/`PF12`(PSRAM_CLK) 设成 Func3，待单独定论。

**94. `spif4` 输入侧三根脚行为异常：`PH12`/`PG07` 不受 GPIO 控制、`PF12` 基线即低（2026-09-18，待查）**
`io_probe_test` 实测（首跑 + 修复轮共 3 次复现，稳定现象）：
- `PH12`（`spif4.SCKI`）、`PG07`（`spif4.IO3_IN`）：`gpio_pin_configure(GPIO_OUTPUT_LOW)` 后读回
  仍为**高**，按"轮询到 5ms 上限"排除 settle 太短 ⇒ `pin_controllable` FAIL，两脚被排除出证据集。
- `PF12`（`spif4.IO2_IN`）：全输入 + 内部上拉时读回**低** ⇒ 被外部压住，无法归因
  ⇒ 按基线门不产生任何连线证据，单列 `BASELINE_LOW`。
- 已排除：生成 `zephyr.dts` 中无任何启用的 pinmux 占用这两脚。**真因未定**（疑默认 pinmux 非
  GPIO / 焊盘特性 / 外部驱动）。同分组 3/5 输入脚异常（`SI=PH10`、`IO1_IN=PH08` 正常）⇒ 指向
  spif4 那几根焊盘/跳线的外部条件，值得后续单独查（超出 `io_probe_test` 写域）。
- 数据：`projects/io_probe_test/tests/COVERAGE.md` §7.2 / `README.md` §8。

**95. `common/judge.py::mem_state_line()` 在 `mem_state==ok` 时丢弃 `note` ⇒ "mem 结果被门禁拒绝"这一诊断串在 `.md`/控制台不可见**（2026-09-18）
`common/judge.py:116-121`：`if state == MEM_OK: return f"{prefix}: {label}"` —— **直接丢 `note`**。
而"`mem_state==ok` **但本轮无 `ZT_BOOT`** ⇒ mem 项被门禁拒绝（E5/E6）"**恰好落在 `state==MEM_OK`
分支** ⇒ 报告只剩"内存兜底: 有效"，读者**看不到"结果被拒绝"**；该 `mem_note` 只存在于 JSON。
- 实测：`eth_test case_mdio` 拔线等效场景，JSON 有
  `mem_note="内存兜底结果被拒绝（无 ZT_BOOT 佐证…）：5 项降级为无证据"`，而
  `.md`/控制台 `grep -c 被拒绝 = 0`。
- 影响：**最需要证据的时候报告最像好消息**（同族于零证据假 PASS / deferred 丢行）。核验 E6 是否
  触发**必须以 JSON `mem_note` 为准**，不能只看 `.md`。
- 修法：`mem_state_line()` 在 `ok` 且 `note` 非空时也应输出 note（与其余分支同形）。
- **✅ 已修（2026-09-19）**：`common/judge.py:116-129` 的 `mem_state_line()` 改为
  `if state == MEM_OK and not note:`（`:127`）才走裸标签 —— `ok` **且 note 非空**时照常拼接。
  向后兼容（`ok` 且无 note 的常规路径输出**逐字不变**）已离线验证：
  四种输入（`ok+note` / `ok 无 note` / `invalid` / `no_sentinel_symbol`）渲染控制台 / `.md` / `.json`，
  仅 `ok+note` 变化，其余三种 `new == old`；脚本 `scripts/verify_mem_state_line.py`（2026-09-24 由 `tmp/emmcfix/` 迁入）。
  ⚠ **残留缺口**：`projects/eth_test/tests/run_test.py` 的**控制台** `print_report()`（:360-373）
  只打印 `mem={mem_state}`，**不经** `mem_state_line()` ⇒ 该工程控制台仍看不到 note
  （`.md`（:393）/ `.json` 已可见）。H12② 的症状恰来自 eth_test；补齐需改该工程（不在本轮写域）。
- 关联：SPEC_REFERENCE §6.2.4 E6 诊断可见性注 · qsh-test-compare §2。

**96. 有 `ZTEST(<suite>,…)` 但缺同名 `ZTEST_SUITE(<suite>,…)` ⇒ 该套件全部用例从未注册执行**（2026-09-18）
Zephyr ztest 里**只有 `ZTEST_SUITE` 才会把套件登记进 `ztest_unit_test` 链接段**；只有 `ZTEST()`
而无同名 `ZTEST_SUITE()` 时，**用例函数被编译但永不运行，也无任何构建期/运行期报错**。
- 实例：`projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c` 有
  `ZTEST(emmc_app_cpu_guard, …)` 两处（`:137`/`:166`），全文件却**只有**
  `ZTEST_SUITE(emmc_block_data,…)`（`:209`）与 `ZTEST_SUITE(zz_emmc_block_edge,…)`（`:358`）
  ⇒ 套件 `emmc_app_cpu_guard` 2 项**从未执行**（报告项数 10 而非 12）。
- **第二实例（2026-09-18 keeper 静态扫描新发现，待 runner 核验）**：
  `projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c` 有 8 处
  `ZTEST(qspi_flash_id, …)`（`:127-229`），而该文件**只有** `ZTEST_SUITE(qspi_flash_rw/…_stg/…_calib)`
  三处 ⇒ 套件 `qspi_flash_id` 8 项**从未注册**。该 8 项却在
  `tests/scripts/dump_results.py` 的 `TEST_NAMES` 里（`jedec_id_match`/`jedec_id_cached`/
  `jedec_id_null_arg`/`line_width_set`/`sfdp_header`/`size_is_32mib`/`get_parameters`/
  `four_byte_mode_unknown_mfr`）⇒ 宣称项与实际运行项不一致，**同时命中 §1.10n 检查项**。
  修法方向（未改码，属 runner 写域）：补 `ZTEST_SUITE(qspi_flash_id, NULL, NULL, NULL, NULL, NULL);`
  并重核 `TEST_NAMES` 链接序（§1.10n）。
- 后果层级：本该打印 `app_cpu_srst=0x%x` 的守卫探针从未跑过 ⇒ 寄存器态失去**直接观测点**
  （正是 #75 争议中本可一锤定音的那条证据）。
- 防再犯（已入 qsh-test-validate §1.10s）：机器判据 = `grep -nE '\bZTEST\(' <file>` 取套件集合
  必须 ⊆ `grep -nE '\bZTEST_SUITE\(' <file>` 取套件集合；差集非空即 FAIL。同类于 #92/TEST_NAMES
  同步（"人记同步清单必漏，机器判据才拦得住"）。
- **归因陷阱（2026-09-19，G-CLEAN-1）**：报告里"看起来有结果"≠该项真跑过。8 项 `qspi_flash_id`
  从未注册，但仍列在 `tests/scripts/dump_results.py` 的 `TEST_NAMES` 里 ⇒
  `TRIAGE_QSPI_SPIF_2026-09-18.md` 曾据该套件的 "`size_is_32mib` PASS" 反推 `flash_size`
  上界 —— **该 PASS 在 batch-1 报告里根本不存在，推断不成立**。纪律：**任何跨报告归因，
  先确认所引报告项对应的套件在本轮确实注册执行过**；`nm` 见 `_ztest_unit_test_list`
  符号只证明"链进去了"，**不证明"会跑"**，唯一判据是 `ZTEST_SUITE`/`ZTEST` 集合差
  （§1.10s）。报告分析侧的同类纪律见 qsh-test-compare §5。
- **姊妹形态：token 宏 no-op ⇒ 批量里只能靠 mem（2026-09-20，G-R7-2 / O-13）**：与 #96 同一根因家族
  —— **"固件侧声明了逐项输出、实际一条不产"**。`i3c_i2c_switch_test` 协议头把 `I3C_I2C_*` token
  宏定义为 `do {} while (0)`、两个固件无 `ZTEST()` ⇒ 该 case 11 项在批量里**零串口逐项 token**、
  整份证据来自 GDB 内存兜底 ⇒ 命中批量判定 **G9/D6**（`SPEC_REFERENCE` §6.2.4）判 FAIL（弱证据）。
  **已根治**：token 宏恢复真 `printk` + `common/ztest_report.py::parse_ztest` 认领其原生 token 形态
  `I3C_I2C_TEST:<CTRL|TGT>:(PASS|FAIL|SKIP):<name>[:reason]`（**未实机验证**，见 OPEN_ITEMS O-13）。
  **通用教训**：「批量必须靠 token」的前提是**两条腿都通** —— a) 固件真的产 token；b) 判定链真的
  消费它。缺任一条，该套件在批量里就只剩弱证据（mem）。同类残留（**只报不改**）：`emmc_test` 的
  `emmc_app_cpu_guard`、`qspi_flash_test` 的 `qspi_flash_id` 空套件（即本 #96 本体）。

---

## i2c_pure_test 域（2026-09-19 登记）

**97. `i2c_pure_test` 串口采集窗口内拿不到"测试启动后"的逐项输出 —— 根因已定位：`dbg_run.py` capture 轮 gdb stdout 管道未排空**（2026-09-19）
`projects/i2c_pure_test/tests/scripts/dbg_run.py --case case_pure_i2c_400k` 在 **40 / 60 / 90 / 150s**
四档采集窗下，两核都只采到「boot banner + `START - i2c_wave` + 启动 LOG」，**无任何 `PASS/FAIL/INFO` 行**，
SUMMARY 恒为 `ZT_BOOT seen`。同轮 GDB 读 RAM：`test_results[0] = TR_PASS`（两核）、
`test_results_valid = 0x5A`、TGT 计数 `tgt_write_cnt=100 / tgt_write_bytes=1600`、
**PC = `arch_cpu_idle`（测试已结束）** ⇒ **用例确实跑完了，只是证据没采到**（该 case 并不慢，见 #99）。
- **根因（2026-09-19 定位）**：capture 轮 `Popen(gdb, stdout=subprocess.PIPE)` 之后**整个采集窗内不读该管道**。
  Windows 匿名管道缓冲写满 ⇒ gdb 阻塞在写 stdout ⇒ DebugServer 会话停摆 ⇒ 目标被中途 halt ⇒
  长用例尾部的 `PASS - i2c_wave` / `PROJECT EXECUTION` 永远采不到。窗口结束调 `communicate()` 排空管道后
  gdb 才解阻塞并处理 `\x03`+detach，detach 恢复目标 ⇒ 固件跑完、RAM 结果正确，**只有证据丢了**。
  **本质 = 采集工具缺陷，不是固件/驱动问题**。
- **隔离实验（确定性）**：复刻该结构（`stdout=PIPE` 不读）⇒ gdb 进程挂死（`stdin write failed`）、
  串口 **0 行**（`tmp/hw4/cap_ts2.txt` + `cap_ts2_run.log`）；改 `stdout=DEVNULL` 的采集脚本一次拿到
  完整输出（`tmp/hw4/cap_ts_r1.txt`，两核 PASS）。
- **修法（已落地 `i2c_pure_test`）**：capture 轮补 drain 线程（`for ln in proc.stdout: gdb_out.append(ln)`），
  用 `proc.wait()` 替代 `communicate()` 取回文本。修后重跑两核 PASS + 完整串口输出
  （`tmp/hw4/i2c_fix_r1.log` 插桩版 / `i2c_final_r1.log` 无插桩最终复验）。
- **残留（待 runner 推广）**：其余工程多由同一模板复制、**同缺陷**；`run_test.py`（正式回归）自带
  stdout drain 线程、**不受影响**。未修的工程仍以 `dump_results.py --json` 内存兜底为准
  （qsh-test-compare §3），修好串口判读前勿据此判驱动挂死。
- **证据文件说明（2026-09-24）**：本条引用的 `tmp/hw4/`（`cap_ts2.txt` / `cap_ts2_run.log` /
  `cap_ts_r1.txt` / `i2c_fix_r1.log` / `i2c_final_r1.log`）**已随 2026-09-24 的 `tmp/` 清理移除**；
  关键数值已**内联于正文**，结论以正文为准。
- 关联：qsh-test-run §3/§6 · qsh-debug §7 · qsh-test-compare §3 · qsh-test-gen §1.5（模板须带 drain）。

**99. `i2c_pure_test` 的 `i2c_wave` 整轮实测 ≈ 10.28s —— 源码注释正确（原"≥40s"结论已撤回）**（2026-09-19，**2026-09-19 更正**）
插桩 `k_uptime_get_32()` 逐轮毫秒 + 墙钟时间戳对照：单轮 **102.76 ms**（100ms 延迟 + ~2.8ms 传输）
⇒ 100 轮 = **10.276 s**；固件时钟与墙钟 **1:1**（HB0..99 = 10.175s 固件 / 10.14s 墙钟）；
gdb `load` 仅 **4s**（写包提速已生效）。
- **更正**：原"实测整轮 **≥40s**、40s 窗只跑到 85/100 轮"**撤回** —— 那是**采集窗口从 gdb 启动起算**
  造成的测量假象（窗口被 `load` 占用），**不是固件变慢**。源码注释 `~10s` / `≈10.1s` **正确，不改**。
- 处置：`--timeout` 不必因"变慢"加大，但**采集窗须留够 = 用例时长 + `load`（~4s）**；
  该 case 拿不到尾部证据的真因是 #97 的管道缺陷（与耗时正交），修 `dbg_run.py` 后串口可正常判读。
- 证据：`tmp/hw4/cap_ts_r1.txt`（逐轮时间戳）· `tmp/hw4/load_timing_nopkt.log`（load 4s）。（**均已随
  2026-09-24 的 `tmp/` 清理移除**；数值已内联于正文。）
- 关联：qsh-test-run §6 · KNOWN_ISSUES #97。

---

## i2cf_test 域（2026-09-22 登记）

**110. 单点 ACK 不能定位器件 —— 地址选择性对照能排除"驱动幻影"，但不能区分"真器件"与"滤波器拦截"（两者都 ACK）**（2026-09-22 实测；**当日两轮实验推翻了早期推论**；⚠️ **本条"拦截也 ACK"的核心推论已作废·待非撞址复验 —— 见文末更正段**）
- **早期推论（同日早些时候，已推翻）**：i2cf_test 上游用例以 `i2c_read(up, &v, 1, 0x50)` 的返回判"被拦"；
  首轮实测 `ret=0`（未拦）而滤波器寄存器全零（`INTR_RAW=0 / SMBF_NONWHITELIST=0 / SMF_FSM=0`），
  曾据"相邻地址扫描（`0x50→ret=0`、`0x51/0x08→-EIO`）"推出 **"0x50 是真器件"**。
- **推翻（同日稍晚实测）**：接线更正后，同一 `i2c_transfer` 调用点只改地址做对照 ——
  读 **`0x33`（无器件）→ `ret=-5(-EIO)`**；读 **`0x50`（被滤波器拦截）→ `ret=0` + 数据 `0xFF`**。
  ⇒ **被拦下的地址照样 ACK**（但不转发 ⇒ 数据段浮空 `0xFF`）⇒ **地址选择性对照也不能区分二者**
  —— 两者都表现为"该地址 `ret=0`、邻址 `-EIO`"这同一形态。
- **判定（要区分"真器件"与"被拦截"，须再取本级证据）**：读本级锁存寄存器 —— `INTR_RAW` 非零且
  `SMBF_NONWHITELIST` 锁存了该违规地址 ⇒ 是**被拦截**，不是真器件应答；或核**数据段内容**
  （浮空 `0xFF`/与真实内容不符 = 未转发）。
- **来源**：`ret` 对照与寄存器值 = **实测**；"接线接错"= **用户更正**（其现象为实测）。
- 可复用招式（含"地址选择性对照仍不充分"这一层）已入 `debug-knowledge/mcu/addr-ack-not-device-evidence.md`。
- **与 #83 / #78 的区别（勿混为一谈）**：#83 = 引脚让渡（`pinctrl-noinit`），现象是**总线无波形 / 设备静默不就绪**；
  本例现象是**有 ACK、也有数据段（`0xFF`）**，是完全不同的根因。#78 = `pfr_tpm` 的 `spif_add_cmd()` 表外野写
  （另一个模块的 P0 缺陷）。
- **⚠️ 更正（2026-09-22 本轮实测；上述"拦截也 ACK"的核心推论作废、待非撞址复验）**：本条核心观测
  "被拦地址 `0x50` → `ret=0` + 数据 `0xFF`"是在**片内 target 与板载器件同址（二者都是 `0x50`）**的
  配置下取得的 ⇒ **同一地址上有两个应答者，观测不可归因**。把片内 target 改到 `0x33`（避开 EEPROM
  惯例地址、改址前两侧实测空闲）后复测：**被拦地址 `0x33` 返回 `ret=-5`(NACK)**（`SMBF_NONWHITELIST`
  锁存 `nw_addr=0x33`、ISR 触发 1 次）⇒ 旧的 `ret=0` 极可能是**主机自己总线上的 `0x50` 器件先应答**，
  **不是滤波器行为**。⇒ 本条"地址选择性对照也不能区分真器件与拦截（两者都 ACK）"**作废、待非撞址复验**；
  重验完成前**不得**据此写"被拦 ⇒ ACK + `0xFF`"的断言，或据此改 `ret<0` 类判据。
  **仍然成立（与撞址无关）**：单点 ACK 不足以证明器件在**指定的那条链路**上 —— 本轮正是它的实例
  （同址邻居替 DUT 应答，骗过了基于单点 ACK 的归因）。判别法已入
  `debug-knowledge/mcu/bus-addr-collision-pass-not-attributable.md`。
  **连带纠正**：早期推论"`0x50` 上有真器件"**方向是对的**（板上确有器件）—— 被否掉的只是它的**归因理由**
  （把它当成片内 target / 或当成滤波器假 ACK）；而"推翻段"里"`0x50` 是被滤波器拦截"的**归因**才是撞址产物。
- **来源标注**：`ret` / 寄存器值 / 扫描值 = **实测**；"板上确实挂了 `0x50`/`0x51` 器件（EEPROM 惯例地址）"
  = **用户告知**（其导致的现象为实测）。

**111. i2cf4 "滤波器零流量"的真因是接线接错（非"上游 master 实例选错"）；滤波器拦非白名单地址时主机看到 ACK(`ret=0`)+数据 `0xFF`，不是 NACK**（2026-09-22 实测；"接线错"为用户更正；⚠️ **后半"被拦 = ACK"已作废·待非撞址复验 —— 见文末更正段；前半"接线错"仍成立**）
- **纠偏（四要素）**：上一轮把 `i2cf4` 寄存器全零（`INTR_RAW=0`）归因为"**上游 master 实例选错**
  （`i2c2`/`i2c5` 都不是接在 i2cf4 SCLI/SDAI 上的那个）"，准备改 overlay 的 `i2cfupstream` alias ——
  **用户更正："刚 IO 接错了"**，链路本就是 `i2c2(主机)→i2cf4 输入(SCLI/SDAI)→i2cf4→i2c3(从机)`，
  **配置一行都不改，只改物理接线**；overlay alias 保持 `i2cfupstream = &i2c2`。
- **实测（接线更正后）**：GDB 读 `i2cf4@0x40091800` ISR 进入前 `INTR_RAW=0x2`(ADDRESS_BEYOND_WHITELIST)、
  `SMBF_NONWHITELIST=0x5000`(锁存地址 0x50)、`SMBUS_FILTER_SET=0x0` ⇒ **流量确实进了滤波器**；
  串口新出现 `i2c_pfr_filter: ... address beyond whitelist: i2c@0x50` ⇒ "上游实例错"归因**被证伪**，
  **"接线错"才是正解**。结果 `FAIL 14/18` → **`15/18`**（`master_eeprom_rw` FAIL→PASS）。
- **新技术事实**：滤波器拦下非白名单地址时，上游 `i2c_read` 返回 **`ret=0`（成功）**但数据字节 `0xFF`
  （无器件驱动），**不是 NACK** ⇒ `filter_blocks_nonwhitelist` / `isr_addr_beyond_whitelist` 的
  `ret_blocked < 0`（"被拦必须 NACK"）判据在本 HW 上**不成立**（详见 #110）。
  **⚠️ 2026-09-22 更正：以上"新技术事实"与由其推出的判据结论均已作废、待非撞址复验**（见文末更正段）。
- **残项（交驱动侧裁定，勿擅自放宽判据）**：① 上述 A 类判据归属；② `filter_passthrough_whitelist`
  授权 0x50 透传读回**尾部 2 字节错**（pattern `5a..5f 60 61`，实读 `5a..5f 40 40`），而**旁路同址
  16B 读回完全正确** ⇒ 指向**滤波器数据通路**（非从设备/非总线）。完整交接单 `tmp/i2cf_final/CONCLUSION.md`，
  跟踪项 `OPEN_ITEMS.md` O-30。
- 关联：#110 · `debug-knowledge/mcu/addr-ack-not-device-evidence.md` · qsh-debug §7 · qsh-test-compare §2。
- **⚠️ 更正（2026-09-22 本轮实测；后半作废、待非撞址复验）**：上条"新技术事实"（被拦 ⇒ `ret=0` + `0xFF`、
  不是 NACK）取自**同址（`0x50`）**配置 ⇒ **该技术事实作废**；换址（`0x33`）后实测**被拦地址 `ret=-5`(NACK)**
  （见 #110 更正段）⇒ 由它推出的"`ret_blocked < 0` 判据在本 HW 上不成立"亦**待重验**。重验前
  **不得放宽** `ret_blocked` / `ret_free` 类断言（两种可能处置相反，**裁定权在驱动侧**，跟踪项 `OPEN_ITEMS` O-30）。
  **本条前半不受影响、仍成立**：`i2cf4` "滤波器零流量"的真因是**接线接错**（非"上游实例选错"），
  `SMBF_NONWHITELIST=0x5000` 锁存证明流量确实进了滤波器。
- 关联（新增）：`debug-knowledge/mcu/bus-addr-collision-pass-not-attributable.md`（同址判别法）。

**112. `i2cf_test` 的 `filter_blocks_nonwhitelist` 自证步稳定 FAIL（**归因未定；2026-09-23 曾据判别实验判为 H1，该判据已撤回**）**（09-22 登记；09-23 定性 → 同日撤回）
- **现象（两轮复现，非 flake）**：`filter off` 后上游读 `0x33` 仍 `ret=-5`；而 **filter on + 白名单授权
  `0x33`** 时同一条上游读**成功**（即 `filter_passthrough_whitelist` PASS）。
- **两条假设（**归因未定**）**：① 违规事务后滤波器停在 blocking/stuck 态，直到 `INTR_CLR`
  （`isr_addr_beyond_whitelist` 每笔事务前都清，故该用例过）或 `cold_reset`（`i2c_filter.c` 走
  `reset_line_toggle` 硬复位）才恢复 —— 本用例的自证步**不清中断**；② 本 HW 的 `en(false,false,·)`
  （monitor 态）本身就不转发。**2026-09-23 曾据判别实验判②成立、①证伪，该判据已撤回 ⇒ 两假设重新并列**
  （理由见文末"★ 判据撤回"段）。
- **纪律**：**`ret_free` / `ret_blocked` 类断言不得放宽**（放宽会掩盖**其它**问题）；**处置方向（改用例）
  已由 `OPEN_ITEMS` O-30 结案 —— 该案依据已换成 #116 规律 1（位置不可测），不是本条的 H1/H2**。
- 关联：#110 更正段 · `filter_passthrough_whitelist`（同 case 内 PASS）· `projects/i2cf_test/tests/COVERAGE.md`。
- **增补（2026-09-23，期望的来源等级核查 —— 只查"判据来源"，不改变"结论待定"）**：该自证步
  "关滤波器后同址应可达"的期望，全仓**查不到**硬件/厂商级依据 —— `reg_i2c_filter.h:39-49` 只给
  字段名/位域、**无行为语义**；厂商样例 `modules/lib/pfr_tpm/samples/i2c_filter_loopback/src/main.c:160-205`
  只在 `fill_bitmap` 前后**临时关开**、**从不"关着做一次读"**；全仓其它 `filter_en(...)` 调用点
  （含本工程自身）**没有第二处**在关断态下发起上游读 ⇒ 该期望**仅由驱动源码的负逻辑三元式
  （`i2c_filter.c:231-236`）推断**，且**在本次之前从未被独立验证**。⇒ 按"期望来源分级"
  （`qsh-test-gen` §1.6 第 4 条 (g)），它属**③级 = 软件位映射推断 = 待验假设**：**不得当作驱动行为的事实**、
  **更不得据此判驱动缺陷**。⇒ **H1/H2 两条假设仍并列、归因未定**，判别实验见
  `reports/2026-09-23-1/i2c_failure_triage.md` §3.4。
  （**入 skill 的只是这条通用纪律（来源分级），不是本条的结论**。⚠ 2026-09-23 曾一度把"H1 已实测定性"
  写入 `qsh-test-gen` §1.6 第 4 条 (g) 与 `qsh-driver-review` §1.11 —— **该判据已撤回**，两处已改为
  "**证据不足却当结论用**"的角度，见 (g)/(h) 与 §1.11。）
- **增补来源**：三份会话（09-21-1 / 09-22-1 / 09-23-1）的源码与归档核查，未上板。
- **★ 判据撤回（2026-09-23 同日）—— 判别实验的两条读数都不能作证据 ⇒ H1 既未证实、也未否定**：
  按 `i2c_failure_triage.md` §3.4 试验 1 上板（序列 `reset_all → switch_to_filter → filter_off → 读 0x33`，
  **前置无任何 `filter_on`、无违规拦截**）得 `fresh_off_ret=-5`，曾据此判"H1 成立、H2 证伪"。**该判据不成立**：
  1. **判读样本用裸 `i2c_read`** —— 裸读在本通路上本身不可靠（#116 规律 2：同配置 3 次得 `0,-5,-5`），
     且该步**紧跟在一次失败事务之后**，按 #116 规律 1 该位置本来就多半失败 ⇒ **单点 `-5` 不构成 H1 的证据**。
  2. **"旁路对照"步构造有误** —— 用上游 `i2c2` 发波、却把 SCLO/SDAO 交给 `i2c4`（`switch_to_master`），
     输出侧不在 i2cf4 手上 ⇒ 该步**恒 `-5`**，不构成"链路可达"的对照（`qsh-debug` §5「拓扑层」）。
  3. **同轮 `filter_passthrough_whitelist` PASS 也不能当 H1/H2 的对照** —— 它走的是 **filter on + 已授权**，
     只能证明"路径与从设备正常"，**不能区分"关滤波不转发"与"残留态"**。
  ⇒ **读数各自无效，判据整体作废**；"关滤波器 ≠ 直通"**不是已证结论**。
  - **通用教训（入 skill 的是这条，不是本条的 H1/H2）**：**"实验读数"本身也是判据，也要过"判据三问"** ——
    量具是否可靠（裸读 vs burst）？这一步是否落在可测位置（紧接被拒事务之后）？对照是否真构成对照
    （发波方/引脚所有权是否对得上）？**三条任一不成立 ⇒ 读数无判别力，不得用来给假设定性**，
    哪怕结论方向"看起来对"（改用例确实对，但依据不是 H1，而是"位置不可测"）。
    落点：`qsh-test-gen` §1.6 第 4 条 (g)/(h)、`qsh-driver-review` §1.11；同族招式见 `debug-knowledge`
    common 域 `probe-scan-selfcheck-invariants.md`。
  - 完整复核：`reports/2026-09-23-1/i2c_failure_triage.md` §9.4。实验码为**一次性诊断**
    （Kconfig `I2CF_FILTEROFF_PROBE` + `#ifdef` 块 + `tests/case_filteroff_probe/`），**未登记、不影响回归**，保留供复现。

**116. 被滤波器拦下（NACK）的一笔之后，紧接的下一笔在同通路上多半会丢；裸 `i2c_read` 不如 `i2c_burst_read` 可靠**（2026-09-23 实测）
- **规律 1（①级实测，可作结论）**：**一笔被滤波器拦下的事务之后，紧接的下一笔事务在本通路上多半会失败。**
  实测该位置 **10 次里 9 次 `-EIO`**（`post_plain`/`d_plain`/`e_plain`/`a_plain`/`p_plain`×2 + 实现 ①②⑤⑥ 的判据读），
  **中间隔一次冷复位也一样**；唯一成功的是单轮 k=0。而**再下一笔**（隔开一笔后）实测 **5/5 正常**
  （`post_burst`/`e_burst`/`q_burst`×3 全为 0）。
  - **机制未定**（滤波器 FSM / 下游从机 resync / 上游控制器，三者皆可能）——未取寄存器证据，
    **不得当结论引用**；若要定性，最小实验 = 拦截后对**同一笔读**做重试并观察寄存器/总线状态。
- **规律 2（①级实测）**：**取证/断言优先 `i2c_burst_read`，不要用裸 `i2c_read`。** 同一配置
  （冷复位 + 只授权该地址 + filter on）重复 3 轮：裸读 `0, -5, -5`；burst 在"非紧接位置" **8/8 为 0**。
  旁证：既有的 `filter_passthrough_whitelist`（PASS）用的正是 burst 形态。
- **用例设计纪律（本条的沉淀价值）**：
  1. **断言不得放在"刚被拒的事务"紧邻之后** —— 要么与失败笔**隔开**，要么**先热身一笔**（结果丢弃、不进判据）。
     原自证步"怎么写都过不了"的**真正原因**就是它恰好落在这个位置上（**不是判据写错**，见 #112）。
  2. **取证 API 选 burst 形态**（规律 2）。
  3. 该位置**也是 #112 那条③级期望判别实验的坑**：`fresh_off_ret` 与 `bypass_ret` 两条读数都落在无效位置/
     无效构造上 ⇒ 判据作废（见 #112「★ 判据撤回」）。
- 关联：#112 · `qsh-test-gen` §1.6 第 4 条 (g)/(h) · `debug-knowledge` common 域
  `probe-scan-selfcheck-invariants.md`（读数/对照必须先自证有效）。
- 证据：`reports/2026-09-23-1/i2c_failure_triage.md` §9.2 上板记录 / §9.3 规律。

**换址后的陈旧文案（残留清查，2026-09-22）**：脚本侧**已同步**（`run_test.py:151` / `test.sh:19,94` /
`i2cf_test.c:969` 均已改为"零外部器件 + 片内 target `eeprom@33`"，本次核实）；**已生成的报告未修** ——
`reports/2026-09-21-1/` 与 `reports/2026-09-22-1/` 的 `i2cf_test_report.md` 头部"接线: 外接 EEPROM@0x50…"
及 `serial_*.log` 内 `0x50` 断言文案仍是旧配置 ⇒ **归因前先核产物新鲜度**（#88 / G-CLEAN-2），
旧报告**不得**用于归因。
- 关联：`qsh-test-gen` §1.6（报告头"接线"行须与实测配置一致）· KNOWN_ISSUES #88。

---

## i3c_dualcore_test 域（2026-09-22 登记）

**113. `i3c_dualcore_test case_mixed_1m_12m5` 的 `multi_msg_4` 瞬态 FAIL（flaky 观测，非驱动回归）**（2026-09-22 实测）
- 现象：全量回归（`reports/2026-09-22-2`）中该 case 的 **`multi_msg_4` 首次观测 FAIL**，
  reason = `msg3 num_xfer 0 != 8`（`i3c_controller_test.c:201`）。
- **瞬态证据（四条）**：① 同批次其余 **6 个 i3c case 的同名项 `multi_msg_4` 全 PASS**；
  ② 基线（`reports/2026-09-21-1`）该 case **PASS 35/35**；③ `reports/` 全库历史**无此前
  `FAIL - multi_msg_4`**；④ **单 case 复跑 = PASS 35/35**。⇒ 记 **flaky 观测**，**不是**驱动回归
  （本轮改动 `acquire.py` / `test_common.sh` / `run_test.py` 均不碰 i3c 固件）。
- 失效形态 = **用例内部单项瞬态数据错**（`num_xfer` 读到 0）。
- **⚠ 与 #87 是不同项，不得互相引用为同一事**：#87 = 该工程 `case_mixed_400k_6m25` / `case_pure_i3c_6m25`
  的 **prime 首轮零输出 / 套件中后段停摆**（环境 / DebugServer 层）；本条 = **用例内部单项瞬态数据错**
  （断言层）。同工程、**症状不同源**，排查时**不得并案**。
- 处置：单次 FAIL 先**重跑排除瞬态**（`qsh-test-compare` §5 第 4 条）；跨轮对比须**同 timeout**（#63）。

---

## mbedtls 配置互斥（2026-10-10 登记）

**120. `MBEDTLS_ECP_RESTARTABLE` 与 Linkedsemi ECC ALT 硬冲突，构建必失败**（2026-10-10，客户报障）
客户在自己侧 `#define MBEDTLS_ECP_RESTARTABLE` 后，构建报：
`check_config.h:195: #error "MBEDTLS_ECP_RESTARTABLE defined, but it cannot coexist with an alternative ECP implementation"`。
- **冲突两边**：`zephyr/modules/mbedtls/configs/config-linkedsemi-alt.h:9-11` 在
  `CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT` **或**
  `CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT` 为 y 时定义
  `MBEDTLS_ECDSA_{SIGN,VERIFY,GENKEY}_ALT`；而 `check_config.h:187-195` 禁止它与 `ECP_RESTARTABLE` 共存。
- **不可调和，不是 bug**：`RESTARTABLE` 要求 ECP 实现**可中断/可恢复**；Linkedsemi 的 OTBN 硬件路径
  与代理路径**都是阻塞式**（本地直接等 OTBN；代理 `k_sem_take(..., K_FOREVER)`）。
- ⚠ **易错点（已实测五组组合）**：`config-linkedsemi-alt.h:11` 那个 `||DELEGATION_CLIENT` ⇒
  **只关 `..._OTBN_ALT` 不够**，只要还在用代理客户端就照样触发，且报错点一样，容易误判成"我已经关了加速"。
  - 实测：`RESTARTABLE`+`DELEGATION_CLIENT` → 报错；`RESTARTABLE`+`..._OTBN_ALT` → 报错；
    `RESTARTABLE` 单独 → 不报错；`DELEGATION_CLIENT` 单独 → 不报错；`DELEGATION_SERVER` 单独 → 不报错。
  - `DELEGATION_SERVER` 本身不触发，但 server 的板级 conf 通常**同时**开了 `..._OTBN_ALT=y`
    （见 `samples/crypto/otbn/delegation/mbedtls_server/boards/lsqsh_evb_cpu1.conf`）⇒ server 侧也会撞。
- **处置（二选一，无第三条路）**：去掉客户自己的 `MBEDTLS_ECP_RESTARTABLE`；或关掉上述两个 Linkedsemi 开关
  走纯软件 ECP（**失去 OTBN 加速**，ECDSA 性能大幅下降）。
- **已加提前拦截**：`config-linkedsemi-tls.h` 末（`MBEDTLS_CONFIG_FILE` 链末端）新增 `#error`，
  会**指名冲突开关**，比 mbedtls 的通用报错好定位；触发范围见上五组实测。
  ⚠ 局限：`MBEDTLS_USER_CONFIG_FILE` 在本文件**之后**才处理（`build_info.h:115/130/192`），
  放在那里的定义只会被 `check_config.h` 拦下 —— 两种放置**最终都会被拦，不会漏过**。
- 备份：`tmp/config-linkedsemi-tls.h.orig-2026-10-10`、`tmp/config-linkedsemi-alt.h.orig-2026-10-10`。
