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
新实例立即连跑 → 间歇性 load_fail/断连/WDR 异常。实例稳定 ~30s 后正常。
处理：run_test.py 自动重试 1 次（含 30s 稳定期）。

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
load 前发 `set memory-write-packet-size 16384` + `set remotewritesize 16384`。
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

**28. native gdb 拒 MSYS 路径**
`cygpath -m` 转 Windows 路径再传 gdb。

**29. test.sh -g 退出码 124 是设计行为**
timeout 杀 continue 收场，判成败看日志不看退出码。

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
- 修复：`wmic process where "name='python.exe'" delete`

**41. 新工程固件加载成功但串口无输出**（2026-09-07，待解决）
- 现象：固件加载正常（Transfer rate: 41 KB/sec），GDB 断点可达 zt_init/test_main，但串口 COM10 无任何输出
- 影响：adc/dma/flash/wdt/iopmp 等新工程均受影响
- 排查方向：uart1 初始化、时钟配置、overlay 配置
