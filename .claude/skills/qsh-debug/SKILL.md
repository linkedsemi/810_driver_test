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
1. 确认环境   §1-3：DebugServer 在跑、能烧录、串口有 banner（全 ? 先清连接残留）；
              ★ 环境检查**有界**：烧录成功但串口静默 ⇒ 按固件挂死处理，先抓 CPU
              （embedded-debug §2.1 / debug-knowledge riscv 域），**不循环查串口**
2. 复现抓现象  qsh-test-run §3：dbg_run.py + token/SUMMARY；--raw 看原始日志
3. 分层定位   embedded-debug §1（自上而下应用→驱动→硬件；QSH 补充见 §5 注）
4. 断点抓现场 embedded-debug §2/§2.1 方法 + 本 skill §6 QSH 命令模板
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

### 2.2a 启动失败：`No XuanTie CKLINK has been connected to PC…` —— **板子物理不在线**（★ 与 §2.2 是两码事）

报错链：`ERROR: No XuanTie CKLINK has been connected to PC or XuanTie CKLINK driver isn't installed
correctly!` → `Cklink connection failed.` → `Input enter to exit...`，TCP 1025 不监听；
随后 `dbg_run.py` 报 `could not connect (error 138)`。

- **区分判据**：§2.2 的 `CKLINK may be used by other program` = 探针在、被别的进程占用；
  本条 = **根本找不到探针** ⇒ 自愈阶梯（[ref-self-heal.md](ref-self-heal.md) §3）第一步即不可达。
- **一条命令判定**：`powershell -c "Get-PnpDevice -PresentOnly"` 只枚举到 JLINK / COM 等、
  **无任何 XuanTie/CKLink 设备**（同轮 cpu1/cpu2 的 CH342/MCP2200 串口也可能已不在枚举中）
  ⇒ **板子物理不在线**（未上电 / 掉线 / 线松），**不是** DebugServer 配置问题、也不是 FlashProgrammer
  抢占（`tasklist` 无 `cskyflash`/`flash_program`/`python` 占用可佐证）。
- **处置**：**直接中止并上报，不要反复重启 DS / 杀进程空转**（空转只会掩盖真正原因）。

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
> 单斜杠在 MSYS subprocess 下不转换（KNOWN_ISSUES #2 经典坑），杀不掉；
> 双斜杠在 Git Bash 和 subprocess 下均正确。若仍杀不掉 → 拔插 CKLink USB。

> **★ 残留的常见来源：脚本里的 `communicate(timeout=)`（2026-09-20）**：`Popen.communicate()`
> 在 `input=None` 时会 **close stdin 并置 `self.stdin=None`**，此时再 `proc.stdin.write("\x03")`
> 抛 `ValueError` ⇒ 落进 `except: proc.kill()`。`dbg_run.py::flash_once` 的 **prime 轮**曾如此被
> kill（**2026-09-20 已修**：23 份 live 模板改为 drain 线程 + `proc.wait()` + 定时写 `\x03`/`detach`）。
> 纪律：**任何"超时后写 `\x03`+detach"的脚本都不得依赖 `communicate(timeout=)` 保留 stdin**
> （KNOWN_ISSUES #101）。

> **★ 同族第二种残留：对 cpu2 的 1026 端口做过任何 gdb 访问（含 `detach`）⇒ cpu2 被留在 halted**：
> 此后不重启 DS，后续回归的 **cpu2 侧全部不运行**（CTRL 串口 0 字节、cpu2 停在 `__start`）。
> 判据：cpu2 侧"0 字节 / 值像没跑过"时，先问"本会话有没有碰过 1026"。
> **纪律：凡碰过 1026，下一次批跑前必须重启 DebugServer。** 细节见 §6「cpu2 通道」
> （KNOWN_ISSUES #98 / `qsh-test-run` §4.1）。

> **★ 1025 是单端口、不支持并发 gdb 客户端（2026-09-22 实测，G-PCSNAP-1）**：旧 gdb 仍在
> `continue` 时新 gdb 连接 ⇒ 立即报 `Remote communication error. Target disconnected: … No such
> file or directory.` + `No stack.`，**拿不到现场**。⇒ **批量路径不做自动 attach / 抓快照**
> （2026-09-22 用户裁定「批量不为单个失败额外花时间与调试风险去定位」，见 `SPEC_REFERENCE`
> §6.4.10）；`common/acquire.py::capture_pc_snapshot()` **已不在批量路径调用**，**仅供人工 /
> 调试按需调用**（怎么人工抓 + 两条约束见 §6）。

### 2.3a 运行时 DebugServer **自杀**（CKLink 瞬时故障）→ **该批次结果作废**（★）

与 §2.2a「板子物理不在线」**相反**：探针**始终被 PnP 枚举到**（`Get-PnpDevice -PresentOnly`
可见 CKLink-Lite），但 DebugServer 在**跑的过程中自己退出**。典型日志（DS 自身输出）：

```text
at 2026-09-21 04:37:33  Get connection from 127.0.0.1 for CPU 0
ERROR: Fail to read from CKLINK2(ret=-7).
CKLINK failed to execute debug operation, please try to reconnect it(USB).
Connection to CPU 0 is lost, at 2026-09-21 04:37:46
DebuggerServer quit
```

- **区分判据**：PnP **仍枚举到 CKLink** ⇒ **不是物理掉线**（§2.2a 是枚举不到探针），
  是 CKLink **瞬时通信故障**（`ret=-7`）触发 DS 自退。批次结束后手动重启 DS **一次即成功**。
- **★ 处置纪律**：任一日志出现 **`error 138`** 或 **`DebuggerServer quit`** ⇒
  **该批次后续 case 的结果整段作废，不得引用** —— DS 一旦死掉，之后每个 case 都只会以
  `could not connect (error 138)` 收场，它们是**同一次单点故障的级联假 FAIL**，
  不是各自的回归。处置：结束本批 → 重启 DS → 整批重跑。
- 批量入口下的识别与放大效应见 `qsh-test-run` §4.1。

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
- 地址变更需同步 4 处：overlay、CONFIG_CPU2_BOOT_ADDR、
  test_common.sh、dbg_run.py/*.gdbcmd
- GDB **无 Python**：别写 python 脚本；`continue &`+`interrupt` 不可靠，
  用同步 continue + 外部超时。
- 串口分配与角色识别见 §4.1。
- run_test.py 必须显式传口号：`--ctrl-uart COM15 --tgt-uart COM16`
  （传错 target 口（如 COM17）会 token 全失、走 GDB 内存兜底，仍 210/210 但
  报告 source 全 mem——需重跑）

### 3.1 单核工程烧录（otbn_test/otbn_selftest，★ `KNOWN_ISSUES.md` #23/#24）

单核工程用**单遍烧录**（reset 已刷 I-cache，无"慢一拍"），dbg_run.py 已固化；
写包提速默认开（16384，真实口径见下注）；`--no-load` 免烧重跑。**会话内禁止二次 load**
（静默挂死）。命令与完整要点见 [ref-flash-variants.md](ref-flash-variants.md) §1。

> **★ 写包提速的真实口径（2026-09-21 实测）**：请求 `memory-write-packet-size`
> 16384 / 2457 / 1024 → gdb **协商** `2493 / 1820 / 875 bytes/write`，**但加载总吞吐恒
> 41 KB/s、墙钟恒 3~4s（与包长无关）** ⇒ **41 KB/s 才是链路瓶颈**。**判据看 gdb 的
> `Transfer rate: N KB/sec`**（真实吞吐），**不得从 `N bytes/write`（协商包长）反推提速
> 倍数**（父会话当天据此误读一次）。双核 `load_and_run` 原注释"不设时慢约 6.7 倍 / 单核
> 41x 提速"**已按实测撤回**（工作区已无 `GDB_WRITE_PKT`）；单核路径的两行 `set` 仍在。

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
- PermissionError → 端口被占用（`tasklist | grep python` 取 **PID** →
  `taskkill //F //PID <pid>`；**禁 `//IM python.exe`**，见 §7 / KNOWN_ISSUES #62）

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
  "master 提前终止"还是"硬件欠载"）；**位宽/掩码以写-回读实测为准**（头文件可能宽于
  实机，见 debug-knowledge mcu 域）；寄存器读全对但波形异常 → 示波器（embedded-debug §3.3）
- 应用层（token reason/记账变量）线索见 `qsh-test-compare`（假 PASS 识别 §2）

## 6. 断点抓现场（QSH 命令模板；方法 → embedded-debug §2）

**断在"状态被破坏之前"、一轮只抓一个时刻**（方法见 embedded-debug §2）。
**★ 统一 GDB 接口（优先于手写 .gdb batch 脚本）**：`.claude/tools/gdb/gdb_proxy.py`
（JSON 输出；三后端自动降级 MCP → pygdbmi → batch），完整用法见
`.claude/tools/gdb/README.md`；无 Python 依赖时用裸 gdb 双核断点模板。
两者均见 [ref-gdb-breakpoint.md](ref-gdb-breakpoint.md)。

- **单 gdb 会话只控一核**：会话控 cpu1 时，对侧核（cpu2）**只能读内存**（`x/`、`x/s`），
  **下不了断点** —— 断在 cpu2 地址上永不命中（实测 `BP@0x10000b2a` 75s 未命中）⇒ 抓对侧核
  执行现场须**切到控该核的会话**（见下「cpu2 通道」）。
- **1025 单端口 / 单客户端**：同一时刻只容许一个 gdb 连接。抓 `$pc` 快照或事后 attach 前
  **必须确认旧 gdb 已 kill**（否则新连接立即报 `Remote communication error … No stack.`，
  拿到的是错误串不是现场；单核 `RUN_TIMEOUT=0` 下必现）。约束见 `SPEC_REFERENCE` §6.4.10。
- **批量路径已不自动抓快照**（2026-09-22 用户裁定）⇒ 抓 `$pc` 现场**改由人工 / 调试发起**（下条）。

> **人工抓 `$pc` 快照（★ 2026-09-22：批量已不自动抓，这是唯一用法）**：`common/acquire.py::
> capture_pc_snapshot()` **已不在批量路径调用**（用户裁定「批量不为单个失败自动定位」，
> `SPEC_REFERENCE` §6.4.10）；0 输出挂死 case 想拿现场时由人工发起：
>
> ```python
> # 在 810_driver_test/common 下执行（cwd 决定 import）
> import acquire
> print(acquire.capture_pc_snapshot(elf=r"<用例 build 的 zephyr.elf>"))
> ```
>
> 两条约束（详见函数 docstring，勿照抄）：① **必须先把加载用的 gdb 杀干净** —— DS 1025
> 不支持并发客户端，否则只拿到 `Remote communication error`；② **碰过之后目标会留在
> halted 态**，后续要跑 case 前**重启 DebugServer**。

#### cpu2 通道 = 第二个 TCP 端口 `1026`（★ 2026-09-21 实测，消解 #98「缺手段」）

DebugServer 为 cpu2 另开一个 GDB 端口：启动日志并列打
`GDB connection command for CPUs(CPU0): …:1025` 与 `CPUs(CPU1): …:1026`。
```bash
riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1026" -ex "info threads"
# → Thread 2 (CPU#1)，PC 落在 cpu2 地址域
```
⇒ 这是**对 cpu2 下断点 / 看 cpu2 现场**的可用手段。要点：

- **前提：DS 启动时 cpu2 必须已脱离复位**。否则启动日志打
  `ERROR: CPU_1: Fail to enter debug mode … AllUnavail: 1`，且**只监听 1025、不监听 1026**。
  单核路径 `load_and_run_single` 会写 `APP_CPU_SRST=2` 把 cpu2 按住，**紧接的 DS 重启就会命中此形态**；
  处置 = 先 `set {int}0x4002200c = 1` 释放 cpu2，再重启 DS。
- **★ 让 1026 出现的操作配方（2026-09-22，用户提供——用户拥有该硬件，硬件域权威；本次未实测复现）**：
  ① **先把 cpu2 跑起来**（cpu2 处于运行态；单核路径写 `APP_CPU_SRST=2` 按住 cpu2 时**没有 1026**）；
  ② **重启 DebugServer**，让调试器**重新枚举核**；③ 之后 **1026 端口才出现**（= cpu2 那个核）。
  **是软件动作、无任何物理操作、可自动化**（框架已有 `restart_debugserver_stable()`），与
  "C 需人工物理操作"相反。
  此式与上条「DS 启动时 cpu2 已脱离复位」是**同一件事的两种说法**：上条说**状态**（已 dereset，否则打
  `AllUnavail`），本条说**怎么做**（跑起来 + 重启 DS 重枚举）——**唯一路径，不存在"二者其一"**；
  上条记的实测失败形态（DS 启动时 cpu2 未脱离复位 ⇒ `AllUnavail: 1`、只监听 1025）与本配方**恰好自洽**：
  都归结为"**DS 重启时 cpu2 必须在运行态**"。
- **硬件写监视可用**：目标信息报 `HWBKPT number is 3, HWWP number is 3` ⇒ gdb `watch`（HWWP）
  能用，实测能停在写者处并给出 backtrace（定位"谁写坏了这块 RAM"）。
- **★ 凡碰过 1026，下一次批跑前必须重启 DebugServer**：任何对 1026 的 gdb 访问（含 `detach`）
  都把 cpu2 留在 halted —— `detach` 只"恢复连接前状态"，实测 cpu2 停在 `__start`(0x10000000)
  不再前进。此后不重启 DS，**后续回归的 cpu2 侧全部不运行**（CTRL 串口 0 字节；cpu2 `.bss`
  是陈旧值或全 0）。操作对照见 §2.3、速查表 §7；批量侧见 `qsh-test-run` §4.1。

> **★ 读 cpu2 RAM 的不变量：先确认 cpu2 本轮真的跑过** —— 在 cpu2 没跑过的会话里读它的
> `.bss`，得到的是**全 0（`.bss` 初值）或上一轮残留**，不是本轮结果。判"某变量被谁清零"
> 之前先核 cpu2 是否运行（CTRL 串口有流量 / PC 不在 `__start`），否则会把"没跑"读成"被抹"
> （`KNOWN_ISSUES.md` #98 后半即此坑；判定侧见 `qsh-test-compare` §3）。

## 7. 常见问题速查

| 现象 | 根因 | 处理 |
|---|---|---|
| `target remote` 报 error 138 / 连不上 | DebugServer 没跑 | 启动 DebugServer（§2.1） |
| DebugServer 报 `No XuanTie CKLINK has been connected to PC…`、1025 不监听 | **板子物理不在线**（与 §2.2"被占用"相反：根本找不到探针） | 一条命令确认：`Get-PnpDevice -PresentOnly` 无任何 XuanTie/CKLink ⇒ 直接中止上报，**别**反复重启 DS/杀进程空转（§2.2a） |
| 跑批途中日志出现 `error 138` / `DebuggerServer quit`，其后 case 整片连 FAIL | CKLink **瞬时故障** → DebugServer 自杀（探针**仍在 PnP 枚举中**，非物理掉线）；DS 死后所有 case 都 `error 138` | **该批次后续 case 结果整段作废**，别逐 case 排查；重启 DS（一次即可）后整批重跑（§2.3a） |
| DebugServer 起不来报 CKLINK used | CSKYFlashProgrammer 占用 | 杀它重启（§2.2） |
| flash 输出正常但串口 0 行 / 只有 \x00 | gdb 被杀后连接残留，reset/load 无效 | detach 清理或重启 DebugServer（§2.3） |
| banner 全 `?` | 固件没跑或口号错 | 先 §2.3 清理；再手动验证串口（§4.2） |
| PermissionError 开串口 | 端口被占用 | `tasklist \| grep python` 取 **PID** → `taskkill //F //PID <pid>`；**禁 `//IM python.exe`**（连坐 west/ninja/脚本本体，KNOWN_ISSUES #62） |
| 单遍烧录行为"慢一拍"（双核） | I-cache 残留 | 两遍烧录法（§3）；单核工程无此问题（reset 已刷 I-cache，§3.1） |
| 目标停死、异常反复 | 调试期 while(1) 陷阱残留 | 清理探针（§8 / `KNOWN_ISSUES.md` #6） |
| 断点抓到的是"测试后"状态 | 断点时机太晚（记账处） | 断在状态被破坏之前（embedded-debug §2） |
| 想对 **cpu2** 下断 / 看 cpu2 现场，但控 cpu1 的会话断不上 | cpu2 走**另一个 GDB 端口** | `target remote 127.0.0.1:1026`（`info threads` 见 `Thread 2 (CPU#1)`）；前提是 DS 启动时 cpu2 已脱离复位 —— **怎么弄：先跑起 cpu2 → 重启 DebugServer 重枚举（软件动作，可自动化），1026 才出现（§6「cpu2 通道」，配方为用户提供）**；否则只监听 1025 |
| **碰过 1026 端口**后 cpu2 侧全不运行（CTRL 串口 0 字节 / cpu2 `.bss` 陈旧或全 0） | 任何对 1026 的 gdb 访问（含 `detach`）都把 cpu2 留在 halted（PC 停 `__start`） | **下一次批跑前必须重启 DebugServer**；读 cpu2 RAM 前先确认 cpu2 本轮真的跑过（§6 / §2.3） |
| DebugServer 重启后立即连跑间歇性 load_fail/断连/WDR 异常 | 新实例不稳定期（`KNOWN_ISSUES.md` #20） | 重启后等 ~5s 稳定期再烧录；run_test.py 已自动重试 |
| 构建报 `ninja: error: failed recompaction: Permission denied` | 同一 build 目录被并发 `west build` 抢（多为**自己上一轮 `nohup west build &` 没退干净**，误判其已死又起前台构建） | 先查进程树（nohup→west→python→cmake）再重跑；按 PID `taskkill //PID <pid> //T` 杀自己那条链，**禁 `//IM python.exe`**（连坐，KNOWN_ISSUES #62）。见 qsh-test-run §6 / qsh-test-validate §2 |
| 用 dbg_run 链跑判 PASS 但日志有 FAIL token | dbg_run 不做判定（退出码恒 0） | 判 PASS 前 grep `CTRL:FAIL\|TGT:FAIL`；正式判定走 run_i3c_tests 报告（`KNOWN_ISSUES.md` #21） |
| load 极慢（~1KB/s，192KB 镜像要 ~190s） | 没设写包参数 | `set remote memory-write-packet-size 16384` + `set remotewritesize 16384`；单核 dbg_run.py 默认已开。**★ 一旦设了就恒 41 KB/s（链路瓶颈），包长再调不更快；判吞吐看 `Transfer rate: N KB/sec`，别看 `N bytes/write`（§3.1）** |
| `-x xxx.gdbcmd` 中途报 `Error in sourced command file` 后 load/$pc/continue 全没跑（像烧录失败） | `-x` 源文件模式**遇错即中止整脚本**（`-ex` 链只跳过该条继续） | 手写 .gdbcmd 核命令名（如 `set memory-write-packet-size` 无效 → `set remote memory-write-packet-size`）；排障优先 `-ex` 链（KNOWN_ISSUES #61） |
| `file` 用 MSYS 路径（`/d/...`）→ gdb 只回一行 `No such file` 却不中止；`load` 照常"成功"、断点命中却读到旧镜像 | Windows gdb 不认 `/d/...`，`file` 失败 → `load` 无可加载段静默 no-op，RAM 仍是上次会话旧固件（比 #61 更隐蔽：连报错都没有） | `file` 一律 `D:/...`（`cygpath -m` 亦可）；下断点后 `x/3i $pc` 对照 `objdump -d`，指令对不上 = 镜像不对（KNOWN_ISSUES #28） |
| 会话内第二个 load 无任何输出（无 "Loading section"）静默挂死 | 二次 load 挂死（`KNOWN_ISSUES.md` #24） | 单核改单遍烧录（§3.1）；免烧迭代用 --no-load；挂死后按 §2.3 重启 DebugServer |
| 串口 0 行 + 系统完全无输出（★ 先查崩溃再查配置） | k_malloc 返回 NULL → memset(NULL) → store fault 挂死（迷惑性：看似 console/log 配置问题） | ★ 排查顺序：gdb attach `bt` 查 fault → 查 k_malloc/k_calloc（改静态数组 `static uint8_t buf[N]`）→ 最后才查配置（KNOWN_ISSUES #40） |
| **带 eth1 的工程** load 后串口 0 行（PHY 不应答） | `phy_rt_rtl8211f_reset` 的 `do{read(PHYID1R)}while(!=0x1C)` **无超时自旋** → POST_KERNEL 永久挂死（同症状、**不同根因**） | ★ 带 eth1 工程**优先** gdb attach 读 PC，栈落在自旋循环内即确认；**别**先走上面 #38/#39/#40 那组（会白查一轮）（KNOWN_ISSUES #71） |
| **烧录成功 + 串口静默**（无 banner、无 `\x00`） | 固件挂在异常入口的 `while(1)`（永久兜底，或调试期临时陷阱 `git diff` 可辨） | ★ **别再查串口**：先用**早标记分界**（embedded-debug §2.2）切开"链路 vs 固件"；判为固件则 gdb attach 读 PC，卡 trap 入口再读 `mcause`/`mepc`/`mtval`（§2.1 / debug-knowledge riscv 域） |
| 烧 flash 后 bootrom 不引导（无固件输出） | 烧错 bin（zephyr.bin 无引导头）；或 GDB reset/detach halt CPU 阻止 bootrom | 见 §3.2：烧 `zephyr_flash_bram_*.bin` + `--reset-and-run`，别用 GDB（KNOWN_ISSUES #48/#49） |
| 配 IOPMP 拒绝规则拦 DMA/外设访问：被拒页确实未写，但传输仍报"完成"、无 ERR | 该 SoC 对被拒/未解码访问**一律回 OKAY**，IOPMP 静默丢弃（**不是错误源**） | 别再用 IOPMP/未映射地址注入总线错误；改**代理实验 + 不变量论证**（KNOWN_ISSUES #65 / debug-knowledge mcu 域） |
| `i2c_read` 某地址返回 `ret=0`（成功）但数据全 `0xFF`；或"被滤波器拦截"用例断言 `ret_blocked<0` 却 FAIL | **单点 ACK 不证明器件在链路上**（应答者可能是**同址邻居**、别的链路或中间器件） | 取**本级证据**：GDB 读滤波器 `INTR_RAW`/`SMBF_NONWHITELIST` 是否锁存该违规地址 + 看数据段内容。**⚠️ 2026-09-22 更正**：原"被拦地址照常 ACK(`ret=0`)+`0xFF` ⇒ 判据别写 `ret<0`"取自**同址冲突**（DUT 与板载器件撞址 `0x50`）的观测 ⇒ **作废、待非撞址复验**（换址后被拦地址实测 NACK）；重验前**不得**放宽/改写 `ret<0` 类断言（裁定权在驱动侧）（KNOWN_ISSUES #110/#111 更正段 · debug-knowledge mcu 域 `addr-ack-not-device-evidence.md` / `bus-addr-collision-pass-not-attributable.md`） |
| 带 `is_app_cpu_running()` 双核运行态守卫的驱动 API **全部 `-EBUSY`**（通路静默不可用，用例全 FAIL） | **⚠️ 2026-09-18 更正：该因果链未获实测支持。** 实测 `set {int}0x4002200c = 2` 后**回读 = `0x0`**（GDB 与固件自读两路一致），emmc 用例 SKIP=0、`disk_access_init=0`、读写 PASS ⇒ **守卫未生效**；写 1 回读 `0x3` 且触发 bootrom 级重启。"写 2 保持复位 ⇒ `>0` ⇒ 守卫误判"基于 SDK 注释，未获实测支持 | 被测驱动有该守卫时仍**先核 `APP_CPU_SRST` 的实测回读值**（别只按 SDK 注释推断），用例按寄存器值断言/`ztest_test_skip`，别误判硬件（KNOWN_ISSUES #75 更正批注） |
| 串口只见 boot banner + `START - <name>`、**无后续 PASS/FAIL/INFO**，SUMMARY 恒 `ZT_BOOT seen` | `dbg_run.py` capture 轮 gdb stdout 管道未排空 ⇒ gdb 写阻塞、采集停摆（**工具缺陷，根因已定位**；用例其实已跑完：RAM `TR_PASS` + `test_results_valid=0x5A`） | 先 `dump_results.py --json` 读 RAM 确认跑没跑完，**别按"固件挂死"继续查串口**；修法 = 补 drain 线程 + `proc.wait()`（qsh-test-run §3），`i2c_pure_test` 已修、其余工程待推广（KNOWN_ISSUES #97） |
| 2os flash 固化后 **cpu1 正常、cpu2 串口全静默（0 行）** | **本板 bootrom 只引导 cpu1**；cpu2 烧进 flash `0x08100000` ≠ 会自动起——镜像需先 staging 进 RAM（GDB `restore` 或 cpu1 boot 搬运代码）；该 build 的 `boot_cpu2()` 只 dereset、不搬运 | 先 GDB 读 `0x10000000` 是否为本 case 的 cpu2 镜像（ELF magic + `e_entry` 对 objdump）⇒ 否即"镜像从未上电"（KNOWN_ISSUES #102 / `ref-flash-variants.md` §2 / SPEC_REFERENCE §6.7.3） |
| 破坏性复位 case 窗口内没重启 ⇒ 疑"没复位" | 采集窗按**标称超时**设；实际复位延迟受 IWDT 时钟/复位域影响可达 ~34s，**"窗口内没看到"≠"没发生"** | 判据取跨复位 `__noinit` 终态（如 `test_phase==2`），或窗口 **≥60s**（KNOWN_ISSUES #100 / `ref-flash-variants.md` §2） |
| 插了临时 `k_oops()`/陷阱探针后**整板静默**（两核 COM 全 0 行） | 探针插在**引导核启动早期** ⇒ cpu1 很早处就 oops ⇒ **连从核都起不来**；现场是"**没启动**"不是"崩溃现场"，极易误读 | 探针放到**被测要点之后**（双核工程放在从核已起来之后，如 `target_test_init()` 之后）；复现失败**先自问"是不是探针把整板按死了"**（§8） |
| 命令行工具莫名停摆 / 长用例尾部证据永远采不到 | 某处 `Popen(stdout=PIPE)` 后**整段窗口不读该管道** ⇒ 缓冲写满 ⇒ 子进程写 stdout 阻塞（gdb 卡住即 DebugServer 会话停摆）。**同族已第 4 次**（`dbg_run.py` capture 轮 / `flash_once` / `run_precheck.py::_gdb_session_b_proc`） | 判据：凡 `Popen(stdout=PIPE)`，同函数内必须有 drain 线程、收尾 `proc.wait()`，**禁用 `communicate()` 驱动**（它 close stdin）；新增 `Popen` 后跑防线脚本 `tmp/b2fix/scan_pipes.py`（非零退出即缺陷）。见 `SPEC_REFERENCE` §6.11.1 |

## 8. 探针纪律（方法 → embedded-debug §4；本段留 QSH 实例）

通用纪律（插桩必清、功能修复 vs 调试残留区分）见 **embedded-debug §4**。QSH 实例：

- 被插桩的共享文件用 `git checkout` 恢复（`log_core.c`/`kernel/timeout.c`/`__assert.h`
  等）；驱动/固件手工删除后 `grep` 确认无残留
- **功能修复必须保留**（不是调试残留）：MPIL workaround（soc_isr_stacking_e906.c）、
  pmp.c 的 PMP_W（XIP 区可写）；调试残留一律删（`log_dbg_*`/`to_dbg_*`/GPIO 标记/
  while(1) 陷阱/probe 脚本），用 `git diff` 逐文件甄别
- 注释边界规范（只写本层语义）见 embedded-debug §5 与 `SPEC_REFERENCE.md` §6.1.6
- **探针的位置本身会改变现场**：`k_oops()`/`while(1)` 陷阱若放在**引导核启动早期**，整板（含从核）
  都起不来 ⇒ 现场是"没启动"而非"崩溃现场"；探针须放在**被测要点之后**（双核工程放在从核已起来
  之后），复现失败先自问"是不是探针把整板按死了"（§7 速查表）

## 9. 坑沉淀

- 只沉淀会反复遇到的通用坑 → embedded-debug §6 + `KNOWN_ISSUES.md`/顶层 CLAUDE.md 速查表；
  一次性定位闭环的 bug → 问题总结 .md 存档（`SPEC_REFERENCE.md` §6.3），不进速查

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
