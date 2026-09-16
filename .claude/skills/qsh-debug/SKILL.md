---
name: qsh-debug
description: QSH 驱动 debug 调试 skill（三大类之一，QSH/810 环境实例化）：连接层坑（DebugServer/GDB 烧录/串口识别与清理）、双核两遍烧录法与单核单遍烧录+写包提速、串口动态识别采集、QSH 常见问题速查表。驱动有问题时的分层根因定位/断点抓现场/探针纪律等**通用方法论见 embedded-debug**，本 skill 只管 QSH 这套硬件环境的"怎么连/怎么烧/怎么抓"实操。测试执行见 qsh-test-run，代码生成见 qsh-test-gen。
---

# QSH 驱动 debug 调试（驱动有问题时，QSH 环境实操）

覆盖本环境（LSQSH EVB 双核）的**连得上、烧得进、抓得到**实操。与硬件无关的方法论
（分层定位/断点/探针/坑沉淀）统一在 `embedded-debug`；构建跑用例在 `qsh-test-run`，
工程规范以 `810_driver_test/CLAUDE.md` 为准。

**详细参考（按需读，不必全载）**：[ref-self-heal.md](ref-self-heal.md) 预检+自愈阶梯 ·
[ref-flash-variants.md](ref-flash-variants.md) 单核/flash boot 烧录变体 ·
[ref-gdb-breakpoint.md](ref-gdb-breakpoint.md) 断点模板 ·
[ref-scons-hal-verify.md](ref-scons-hal-verify.md) scons/HAL 验证

## 0. 调试总流程（先走流程，再动手）

```text
1. 确认环境   §1-3：DebugServer 在跑、能烧录、串口有 banner（全 ? 先清连接残留）
2. 复现抓现象  qsh-test-run §3：dbg_run.py + token/SUMMARY；--raw 看原始日志
3. 分层定位   embedded-debug §1（自上而下应用→驱动→硬件；QSH 补充见 §5 注）
4. 断点抓现场 embedded-debug §2 方法 + 本 skill §6 QSH 命令模板
5. 修复 → 验证 → 探针清理（embedded-debug §4 + 本 skill §8）→ 沉淀（embedded-debug §6）
```

## 1. 链路与真相（实测，多次纠正过，勿再写错）

```text
riscv64-unknown-elf-gdb ──TCP 127.0.0.1:1025──► XuanTie DebugServerConsole.exe（PC 进程）
                                                          │ JTAG
                                                          ▼
                                                    LSQSH EVB（cpu1 + cpu2）
```

| 结论 | 说明 |
|---|---|
| 1025 端口由 **PC 进程** 提供 | `C:\uaer_app\debug_server\bin\DebugServerConsole.exe`，端口在 `bin\default.ini` 的 `SOCKETPORT=1025`。**不是 CKLink 固件内置**，DebugServer 不启动就没有 1025 |
| 在线判定 = GDB 连一次 | `riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" ...` 出现 "Remote debugging" 即通。**不要用 netstat 判离线**（用户明确纠正过）。**★ -batch 模式不打印 "Remote debugging"**：改判 `detached` 或 `Remote target`（`re.search(r"detached|remote target", combined, re.I)`），或判退出码 0 即可（2026-09-02 实测） |
| 连接地址写 `127.0.0.1:1025` | 用户指定格式；脚本内 `GDB_HOST, GDB_PORT = "127.0.0.1", "1025"` |

## 2. DebugServer 启动 / 冲突 / 残留

### 2.1 启动
```bash
cd C:/uaer_app/debug_server/bin && ./DebugServerConsole.exe &
```

### 2.2 启动失败：CKLINK may be used by other program
FlashProgrammer（GUI `CSKYFlashProgrammer.exe` 或 CLI 烧录 `flash_program.py`）
占用了 CKLink。处理：
```bash
tasklist | grep -i -E "cskyflash|debugserver"   # 找占用进程
taskkill //F //PID <pid>       # 杀后重启 DebugServer
```
> 互斥是双向的：烧 flash 前 DebugServer 也必须退出（`--kill-debugserver` 强制杀）。
> **★ flash_program.py 必须 `ConnectType: 1`（硬复位连接）**：DebugServer 会话后
> 芯片处于调试 halt 状态，`ConnectType=0` 无法重连 → 卡死 80s（已改，勿改回）。
> **多 CKLink 设备**：枚举到异常设备会卡死 → 禁用异常设备，或 `--cklink` 指定 serial。

### 2.3 gdb 被杀后连接残留（★高频坑）
`continue` 超时被 kill（如 dbg_run.py 的 prime/capture 轮）后，DebugServer 仍
认为有连接。表现：下一次 `target remote` **能连上**、flash 输出正常，但
`reset`/`load` 实际无效 → **固件不跑**：串口 0 行，或只有 `\x00` 复位噪声，
角色 banner 全 `?`。

**处理顺序**（先轻后重）：
```bash
# 1) 干净连接后 detach，清理残留连接；2) 还不行则杀 DebugServer 重启
riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" -ex "detach" -ex "quit"
taskkill //F //IM DebugServerConsole.exe && cd C:/uaer_app/debug_server/bin && ./DebugServerConsole.exe &
# 3) 判断固件真的在跑：串口出现 "CPU2 MASTER"/"CPU1 SLAVE" banner，而不是靠 flash 输出
```

> **实测纠正**：detach 不能真正释放连接（任何"连接→detach→退出"的 gdb 都留
> 残留，**探测连接本身也制造残留**）→ 残留深时 detach 无效，唯一可靠 = 重启
> DebugServer 进程。脚本化自愈四步（run_test.py `clean_gdb_residue`）见
> [ref-self-heal.md](ref-self-heal.md) §2。
> **Python subprocess 中 taskkill 优先用双斜杠**（`//F //PID`）：
> 单斜杠在 MSYS subprocess 下不转换（KNOWN_ISSUES §16/#2 经典坑），杀不掉；
> 双斜杠在 Git Bash 和 subprocess 下均正确。若仍杀不掉 → 拔插 CKLink USB。

### 2.4 环境预检（Supervisor 执行前必做）

逐项清单与命令（六项）见 [ref-self-heal.md](ref-self-heal.md) §1（唯一来源）。
STRATEGY_TEST 启动 run_all.py 前 runner 必须逐项手动验证，不得委托 run_all.py
内置预检（其 erase 可静默成功、precheck 探测本身制造残留）。失败 → 自愈 → 重试 1 次 → 升级。

### 2.5 自愈升级策略（超时升级，2026-09-07 用户裁定）

**先穷尽自愈手段，超时后才升级；升级必须带结构化信息。** 自愈阶梯（环境/构建/烧录
三类，按序不跳步）、超时上限（30s/1min/30s）、升级消息模板与直接升级条件见
[ref-self-heal.md](ref-self-heal.md) §3。

## 3. GDB 双核烧录（两遍烧录法）

**为什么两遍**：gdb `reset` 后 I-cache 残留，单遍烧录 continue 会跑旧镜像
（"慢一拍"）。同一镜像烧两遍，第二遍抓取；prime 轮输出丢弃。

```bash
riscv64-unknown-elf-gdb \
  -ex "target remote 127.0.0.1:1025" \
  -ex "set confirm off" -ex "set pagination off" \
  -ex "file <build>/target/zephyr/zephyr.elf" \
  -ex "reset" -ex "load" \
  -ex "restore <build>/controller/zephyr/zephyr.bin binary 0x10000000" \
  -ex "set \$pc=0x100c0000" \
  -ex "continue"
```

- cpu1（target）：`load` ELF，入口 `0x100c0000`（TARGET_ROM_START）
- cpu2（controller）：`restore <bin> binary 0x10000000`（CONTROLLER_LOAD_ADDR）
  + `set $pc=0x100c0000`
- 地址变更需同步 4 处（见 CLAUDE.md §3）：overlay、CONFIG_CPU2_BOOT_ADDR、
  test_common.sh、dbg_run.py/*.gdbcmd
- GDB **无 Python**：别写 python 脚本；`continue &`+`interrupt` 不可靠，
  用同步 continue + 外部超时。
- 串口分配与角色识别见 §4.1。
- run_test.py 必须显式传口号：`--ctrl-uart COM15 --tgt-uart COM16`
  （传错 target 口（如 COM17）会 token 全失、走 GDB 内存兜底，仍 210/210 但
  报告 source 全 mem——需重跑）

### 3.1 单核工程烧录（otbn_test/otbn_selftest，★ §10.23/§10.24）

单核工程用**单遍烧录**（reset 已刷 I-cache，无"慢一拍"），dbg_run.py 已固化；
写包提速默认开（16384，41x）；`--no-load` 免烧重跑。**会话内禁止二次 load**
（静默挂死）。命令与完整要点见 [ref-flash-variants.md](ref-flash-variants.md) §1。

### 3.2 flash boot 破坏性复位测试（bootrom 引导非 XIP）

烧 `zephyr_flash_bram_*.bin`@0x08000000（**不是 zephyr.bin**，无引导头 bootrom 不认），
触发用 `flash_program.py --reset-and-run`（**禁用 GDB reset/detach**，halt 会阻止
bootrom 引导，KNOWN_ISSUES #49）。完整流程见
[ref-flash-variants.md](ref-flash-variants.md) §2 + KNOWN_ISSUES #46-49。

## 4. 串口动态识别与采集

### 4.1 角色识别（不硬编码口号）
- COM14 = CKLink Serial（VID 32BF:B210）—— 调试器自身口，**排除**
- COM15 = MCP2200（04D8:00DD）= controller uart5
- COM16/17 = CH342（1A86:55D2）= target uart1 + 备用口
- 口号以设备管理器为准；枚举排除 `32bf`/`cklink` 后按 **banner** 定角色：
  "CPU2 MASTER"/"i3c_master_test" → CTRL；"CPU1 SLAVE"/"i3c_slave_test" → TGT

### 4.2 手动验证串口链路
固件在跑但抓不到数据时，发回车探测：`serial.Serial('COM15', 115200, timeout=1)`
→ `write(b'\r\n')` → 等 1s 读 512 字节。
- 有 shell 响应 `uart:~$` → 链路 OK，问题在采集脚本时机
- 只有 `\x00` → 固件没跑（多半是 §2.3 连接残留）
- PermissionError → 端口被占用（`tasklist | grep python` → taskkill）

### 4.3 采集要点（dbg_run.py 已实现，写新脚本时照抄）
- reader 线程逐行读，`\x1b\[[0-9;?]*[A-Za-z]` 正则剥离 ANSI（shell 提示符
  重绘会污染行首）
- token 匹配用 `re.search()` 不用 `^`/`.match()`（行首可能有 `uart:~$ `）
- 串口只在 capture 轮开（prime 轮输出丢弃，否则两轮混排造成测试顺序错乱假象）

## 5. 分层根因定位（方法论 → embedded-debug §1；本段只留 QSH 补充）

QSH/810 语境下的补充线索：

- **驱动层**：I3C target ISR 的分发顺序 = 中断标志位判定顺序（RXPEND → STOP/
  ERRWARN → MATCHED → TXSEND），判定顺序决定事件优先级；"回调里无条件赋值/覆盖"
  类代码常是覆盖 bug 源头（把对侧设置的参数在 stop/end 回调里无条件重置）
- **硬件层寄存器**：基址/偏移查驱动 reg 头文件；关键位域含义查
  `<driver>/ls_<x>/reg_<x>.h`（如 SERRWARN 的 TERM/URUN/URUNNACK 位 → 区分
  "master 提前终止"还是"硬件欠载"）；寄存器读全对但波形异常 → 示波器（embedded-debug §3.3）
- 应用层（token reason/记账变量）线索见 `qsh-test-compare`（假 PASS 识别 §2）

## 6. 断点抓现场（QSH 命令模板；方法 → embedded-debug §2）

**断在"状态被破坏之前"、一轮只抓一个时刻**（方法见 embedded-debug §2）。
**★ 统一 GDB 接口（优先于手写 .gdb batch 脚本）**：`.claude/tools/gdb/gdb_proxy.py`
（JSON 输出；三后端自动降级 MCP → pygdbmi → batch），完整用法见
`.claude/tools/gdb/README.md`；无 Python 依赖时用裸 gdb 双核断点模板。
两者均见 [ref-gdb-breakpoint.md](ref-gdb-breakpoint.md)。

## 7. 常见问题速查

| 现象 | 根因 | 处理 |
|---|---|---|
| `target remote` 报 error 138 / 连不上 | DebugServer 没跑 | 启动 DebugServer（§2.1） |
| DebugServer 起不来报 CKLINK used | CSKYFlashProgrammer 占用 | 杀它重启（§2.2） |
| flash 输出正常但串口 0 行 / 只有 \x00 | gdb 被杀后连接残留，reset/load 无效 | detach 清理或重启 DebugServer（§2.3） |
| banner 全 `?` | 固件没跑或口号错 | 先 §2.3 清理；再手动验证串口（§4.2） |
| PermissionError 开串口 | 端口被占用 | `tasklist \| grep python` → taskkill |
| 单遍烧录行为"慢一拍"（双核） | I-cache 残留 | 两遍烧录法（§3）；单核工程无此问题（reset 已刷 I-cache，§3.1） |
| 目标停死、异常反复 | 调试期 while(1) 陷阱残留 | 清理探针（§8 / CLAUDE.md §10.6） |
| 断点抓到的是"测试后"状态 | 断点时机太晚（记账处） | 断在状态被破坏之前（embedded-debug §2） |
| DebugServer 重启后立即连跑间歇性 load_fail/断连/WDR 异常 | 新实例不稳定期（§10.20） | 重启后等 ~5s 稳定期再烧录；run_test.py 已自动重试 |
| 用 dbg_run 链跑判 PASS 但日志有 FAIL token | dbg_run 不做判定（退出码恒 0） | 判 PASS 前 grep `CTRL:FAIL\|TGT:FAIL`；正式判定走 run_i3c_tests 报告（§10.21） |
| load 极慢（~1KB/s，192KB 镜像要 ~190s） | 没设写包参数 | `set memory-write-packet-size 16384` + `set remotewritesize 16384`（41x，§3.1）；单核 dbg_run.py 默认已开 |
| 会话内第二个 load 无任何输出（无 "Loading section"）静默挂死 | 二次 load 挂死（§10.24） | 单核改单遍烧录（§3.1）；免烧迭代用 --no-load；挂死后按 §2.3 重启 DebugServer |
| 串口 0 行 + 系统完全无输出（★ 先查崩溃再查配置） | k_malloc 返回 NULL → memset(NULL) → store fault 挂死（迷惑性：看似 console/log 配置问题） | ★ 排查顺序：gdb attach `bt` 查 fault → 查 k_malloc/k_calloc（改静态数组 `static uint8_t buf[N]`）→ 最后才查配置（KNOWN_ISSUES #40） |
| 烧 flash 后 bootrom 不引导（无固件输出） | 烧错 bin（zephyr.bin 无引导头）；或 GDB reset/detach halt CPU 阻止 bootrom | 见 §3.2：烧 `zephyr_flash_bram_*.bin` + `--reset-and-run`，别用 GDB（KNOWN_ISSUES #48/#49） |

## 8. 探针纪律（方法 → embedded-debug §4；本段留 QSH 实例）

通用纪律（插桩必清、功能修复 vs 调试残留区分）见 **embedded-debug §4**。QSH 实例：

- 被插桩的共享文件用 `git checkout` 恢复（`log_core.c`/`kernel/timeout.c`/`__assert.h`
  等）；驱动/固件手工删除后 `grep` 确认无残留
- **功能修复必须保留**（不是调试残留）：MPIL workaround（soc_isr_stacking_e906.c）、
  pmp.c 的 PMP_W（XIP 区可写）；调试残留一律删（`log_dbg_*`/`to_dbg_*`/GPIO 标记/
  while(1) 陷阱/probe 脚本），用 `git diff` 逐文件甄别
- 注释边界规范（只写本层语义）见 embedded-debug §5 与 CLAUDE.md §4.6

## 9. 坑沉淀

- 只沉淀会反复遇到的通用坑 → embedded-debug §6 + CLAUDE.md §10/速查表；
  一次性定位闭环的 bug → 问题总结 .md 存档（CLAUDE.md §7），不进速查

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| 问题解决 | 回调用方（qsh-test-run / qsh-test-gen 等） | 恢复执行 |
| 发现环境坑 | KNOWN_ISSUES.md + qsh-debug §7 速查 | 反复踩 |
| 发现领域招式 | debug-knowledge §写入 | 非一次性 |
| 3 轮未闭环 | 升级（结构化报告） | 超时 |

## 附：scons / HAL 示例验证（低频非主环境）

见 [ref-scons-hal-verify.md](ref-scons-hal-verify.md)（uart_capture / `_run.gdb` / dump 判读）。

## 链路
> **相关**→ `embedded-debug` → `debug-knowledge` · **触发**← runner(STRATEGY_DEBUG)
