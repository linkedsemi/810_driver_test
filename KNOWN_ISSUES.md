# 810_driver_test 已知坑清单（完整版）

> 从 CLAUDE.md §10 拆出的完整清单。CLAUDE.md 只保留 Top 10 高频坑（精简版），
> 本文档记录所有实测沉淀的问题，含一次性/已修复的条目。
> 新坑先加这里，高频的再精简进 CLAUDE.md。

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
Python subprocess 里 taskkill 用单斜杠 `/F /PID`（§16）；若杀不掉，fallback = bash 层 `taskkill //F //PID`（见 #2）。

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
单核工程已改单遍烧录（reset 冲刷 I-cache，§10.24）。

**23. GDB 写包提速 41x**
load 前发 `set remote memory-write-packet-size 16384` + `set remotewritesize 16384`。
192KB 镜像 ~190s → ~5s。已固化进单核 dbg_run.py/run_test.py。

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

**62. 按名字杀 python.exe = 自杀：连坐 west/ninja/run_test.py 本体 → 全量回归每个 case 秒级 `BUILD FAILED`**（2026-09-16，极具迷惑性）
- 场景：回归中途想清残留构建进程，执行 `taskkill //F //IM python.exe`（连带 `west.exe`/`ninja.exe`）。
- 根因：`run_test.py`/`run_all.py`、`west`、`ninja` 本体**全是 python 进程** → 按名字杀把
  正在跑的构建/采集链路一起打死。
- 迷惑点：每个 case 在 1~60s 内**秒级整齐** `BUILD FAILED` / `rc=1`，看起来像代码回归或
  环境崩溃，**实际是自己杀出来的**。区分点：报错快而整齐（不像散点 FAIL），且时间点与"清进程"操作对齐。
- 正确做法：① 优先只 `TaskStop` 掉 runner 的父任务；② 清 gdb 残留：`netstat -ano | grep :1025`
  取对端 PID，只杀那一个；③ 清 west/ninja：`tasklist | grep -E "west|ninja"` 取 PID 后逐个
  `//F //PID` 杀。**永远不要 `//IM python.exe`**。
- 关联：#40 旧修复写法 `wmic ... name='python.exe' delete` 同属按名字杀，已一并纠正。
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

**64. 长时全量回归后台任务被外部中止（整棵 python 树消失、DebugServer 独活）→ 全量回归按"可续跑"设计**（2026-09-17）
- 现象：全量回归（29 case）两次都在跑到第 21 个 case（mbedtls `case_otbn_sha256_hw_sm3`）处被中止——
  `run_all.py` 后台任务被 kill（任务状态 `killed`，output 尾部 `[killed]`），**整棵 python 进程树
  同时消失、DebugServer 单独存活**；两次时间点高度一致（约 66 分钟处）。
- 根因：**长时（>1h）后台批量任务在本环境会被外部中止**，非脚本崩溃、非硬件。故
  **"单次连续跑完 29 case"不是可靠可达目标**。
- 判据（**与 #62 相反，勿混淆**）：本条 = 进程树**整体**消失、**无** `BUILD FAILED`、时间点与清进程操作无关；
  #62 = **自己** `//IM python.exe` 按名字杀，症状是每 case **秒级整齐** `BUILD FAILED`（时间点与清进程操作对齐）。
  处置相反：本条直接续跑；#62 要先改清理方式（只按精确 PID / `TaskStop`）。
- 处理：① 全量回归**默认按"可续跑"设计**——`run_all.py` 跑头部 + **幂等 resume 脚本**（写 done 标记、
  重跑自动跳过已完成 case）补齐尾部；**中断点与续跑情况必须写进报告**，禁止拼成"一次跑完"；
  ② 监控心跳见"整棵 python 树消失"即判**外部中止**，**直接走续跑，勿重复排查环境**。

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

**38. run_test.py allocate_session_dir 缺 date_str 参数**（2026-09-07）
- 现象：`TypeError: allocate_session_dir() missing 1 required positional argument: 'date_str'`
- 根因：新工程 run_test.py 生成时漏传 date_str
- 修复：`date_str = datetime.now().strftime("%Y-%m-%d"); session = allocate_session_dir(REPORTS_ROOT, date_str)`

**39. DebugServer/CKLink 被残留 GDB 进程占用**（2026-09-07）
- 现象：`ERROR: Your CKLINK may be used by other program`
- 根因：测试中断后 GDB 进程未退出，持有 CKLink 连接
- 修复：`wmic process where "name='riscv64-unknown-elf-gdb.exe'" delete`

**40. 串口被残留 Python 进程占用**（2026-09-07）
- 现象：`PermissionError(13, '拒绝访问')`
- 根因：测试中断后 Python 进程未退出，占用串口
- 修复：先按精确 **PID** 杀使用该串口的那个进程（`tasklist | grep python` 取 PID →
  `taskkill //F //PID <pid>`）。**禁用 `//IM python.exe` / `wmic ... name='python.exe' delete`
  之类按名字杀**——west/ninja/run_test.py 本体都是 python 进程，会连坐自杀（详见 #62）。

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

