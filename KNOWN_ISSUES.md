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

**50. run_test.py 与 dump_results.py 的 TEST_NAMES 双份硬编码，增删测试项漏同步 → mem 兜底错位/假 MISSING**（2026-09-09）
新增 broadcast_nack_recovery（第 11 项）时只同步了 run_test.py，漏了 dump_results.py（仍是旧 10 项）
→ broadcast 读回 MISSING 假 FAIL；且新项插在链接序**前部**，旧列表整体错位一格，把真实结果张冠李戴。
修法/防再犯：两份 TEST_NAMES 都是 **ztest 链接序**（不是 protocol.h 枚举序，两者不一致）；增删测试项后
两份同改，构建后 `nm --numeric-sort <elf> | grep z_ztest_unit_test` 复核全序，并同步更新各自注释里的实测序清单。
（本项漏同步实例的代码修复 = runner 跟进：dump_results.py TEST_NAMES 补 broadcast 到表头 + 更新其头部注释实测序。）

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
