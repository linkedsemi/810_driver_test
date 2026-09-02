---
name: qsh-debug
description: QSH 驱动 debug 调试 skill（三大类之一，QSH/810 环境实例化）：连接层坑（DebugServer/GDB 烧录/串口识别与清理）、双核两遍烧录法与单核单遍烧录+写包提速、串口动态识别采集、QSH 常见问题速查表。驱动有问题时的分层根因定位/断点抓现场/探针纪律等**通用方法论见 embedded-debug**，本 skill 只管 QSH 这套硬件环境的"怎么连/怎么烧/怎么抓"实操。测试执行见 qsh-test-run，代码生成见 qsh-test-gen。
---

# QSH 驱动 debug 调试（三大类之一：驱动有问题时，QSH 环境实操）

**覆盖"驱动有问题时"在本环境（LSQSH EVB 双核）的全部实操**：环境链路（连得上、
烧得进、抓得到）+ 串口识别采集。分层根因定位、断点抓现场、探针纪律、坑沉淀准则等
**与硬件环境无关的方法论，统一在 `embedded-debug`**；本 skill 保留 QSH 专属的坑与
命令模板。构建/跑用例流程在 `qsh-test-run`，工程规范以 `810_driver_test/CLAUDE.md` 为准。

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
FlashProgrammer（GUI `CSKYFlashProgrammer.exe` 或 CLI 烧录 `flash_program.py`，
CLAUDE.md §15）占用了 CKLink。处理：
```bash
tasklist | grep -i -E "cskyflash|debugserver"   # 找占用进程
taskkill //F //PID <pid>       # 杀后重启 DebugServer
```
> 互斥是双向的：烧 flash 前 DebugServer 也必须退出（`flash_program.py` 会
> 检测警告，`--kill-debugserver` 强制杀）。
> **★ ConnectType=1（硬复位连接）**：DebugServer 会话后芯片处于调试 halt 状态，
> `ConnectType=0`（普通连接）无法重连 → flash_program.py 卡死 80s。已改为
> `ConnectType: 1`（硬复位），实测解决（2026-09-02）。
> **多 CKLink 设备**：Device Manager 出现多个 CKLink（含 Unknown 状态）时，
> flash programmer 可能枚举到异常设备卡死 → 禁用异常 USB 设备，或用 `--cklink`
> 指定正常设备的 serial number。

### 2.3 gdb 被杀后连接残留（★高频坑）
`continue` 超时被 kill（如 dbg_run.py 的 prime/capture 轮）后，DebugServer 仍
认为有连接。表现：下一次 `target remote` **能连上**、flash 输出正常，但
`reset`/`load` 实际无效 → **固件不跑**：串口 0 行，或只有 `\x00` 复位噪声，
角色 banner 全 `?`。

**处理顺序**（先轻后重）：
```bash
# 1) 干净连接后 detach，清理残留连接
riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" -ex "detach" -ex "quit"
# 2) 还不行：杀 DebugServer 重启
taskkill //F //IM DebugServerConsole.exe && cd C:/uaer_app/debug_server/bin && ./DebugServerConsole.exe &
# 3) 判断固件真的在跑：串口出现 "CPU2 MASTER"/"CPU1 SLAVE" banner，而不是靠 flash 输出
```

> **实测纠正**：detach 不能真正释放连接（任何"连接→detach→退出"的 gdb 都留
> 残留，**探测连接本身也制造残留**）→ 残留深时 detach 无效，唯一可靠 = 重启
> DebugServer 进程。**脚本化自愈四步**（run_test.py 的 `clean_gdb_residue`）：
> ① netstat 找 1025 ESTABLISHED 对端 PID 杀 gdb；② taskkill /F /IM
> DebugServerConsole.exe；③ netstat 精确杀 1025 LISTENING 兜底；④ Popen 重启 +
> stdout 重定向日志，**轮询日志出现 `DebuggerServer$` 提示符判就绪**（≤30s），
> 不能只固定 sleep（CKLink 初始化耗时随机）。
> **Python subprocess 里 taskkill/tasklist 必须用单斜杠**（`/F /PID /FI /FO`）：
> MSYS 的 `//F` 转换只对 Git Bash 有效，subprocess 无转换层，`//F` 原样传给
> taskkill 报"无效参数"（CLAUDE.md §10.16）。

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
- 双核串口：COM15=controller（MCP2200）、COM16=target（CH342，banner 实测口）、
  COM17=CH342 备用（无 banner，空口）、COM14=CKLink 自身（排除）；
  口号以设备管理器为准，按 banner 动态识别。
- run_test.py 必须显式传口号：`--ctrl-uart COM15 --tgt-uart COM16`
  （传错 target 口（如 COM17）会 token 全失、走 GDB 内存兜底，仍 210/210 但
  报告 source 全 mem——需重跑）

### 3.1 单核工程烧录（otbn_test/otbn_selftest，★ §10.23/§10.24）

与双核两遍烧录法**不同**，单核工程用**单遍烧录**（dbg_run.py 已固化）：

```bash
cd 810_driver_test/projects/otbn_test/tests
python scripts/dbg_run.py --case case_default --timeout 90   # 单遍：reset→load→continue 采首轮
python scripts/dbg_run.py --case case_default --no-load      # 免烧重跑（镜像已在 RAM，0 烧录）
python scripts/dbg_run.py --wpacket-size 0                   # 关闭写包提速（默认 16384）
```

- **单遍烧录成立**：reset 已冲刷 I-cache（两遍法在双核存在的"慢一拍"场景
  被覆盖），load 后 .data/.bss 天然干净；单遍与两遍法结果逐项一致（三连验证）
- **会话内二次 load 会静默挂死**（实测 2/2）：第一次 load 成功后 interrupt、
  再发 `load`，gdb 无任何输出（连 "Loading section" 都没有）→ 只能超时
  proc.kill()，且留 DebugServer 连接残留（§2.3 重启才干净）。**不要在任何
  单会话流程里 load 第二次**
- **写包提速（41x）**：load 前 `set memory-write-packet-size 16384` +
  `set remotewritesize 16384`，CKLink load 实测 1KB/s → 41KB/s（192KB 镜像
  ~190s → ~5s）；stub 不接受时回 `Remote failure`/`E22` 立即失败
- **交互式流程（★ §10.25）**：reset+load 后等 "Transfer rate" 再 continue。
  一次性 -ex 链的 continue 在 load 刚完成瞬间发出，DebugServer 状态不佳时
  被静默丢弃 → CPU 停 halt 无 BOOT，detach 恢复运行（回归 3/3 复现）；
  dbg_run.py / run_test.py 均为交互式（0 失败）。手工 GDB 也照此
- **免烧重跑 --no-load**：attach → `x/4wx 0x100c0000` sanity（镜像在？非全 0）
  → `set $pc=0x100c0000` 重跑复位向量（重新初始化 .bss/.data，等效软复位）

## 4. 串口动态识别与采集

### 4.1 角色识别（不硬编码口号）
- COM14 = CKLink Serial（VID 32BF:B210）—— 调试器自身口，**排除**
- COM15 = MCP2200（04D8:00DD）= controller uart5
- COM16/17 = CH342（1A86:55D2）= target uart1 + 备用口
- 口号以设备管理器为准；正确做法：枚举排除 `32bf`/`cklink`，烧录后按
  **banner** 定角色：
  - "CPU2 MASTER" / "i3c_master_test" → CTRL
  - "CPU1 SLAVE" / "i3c_slave_test" → TGT

### 4.2 手动验证串口链路
固件在跑但抓不到数据时，先发回车探测：
```python
import serial, time
s = serial.Serial('COM15', 115200, timeout=1)
time.sleep(0.2); s.write(b'\r\n'); time.sleep(1.0)
print(s.read(512))
```
- 有 shell 响应 `uart:~$` → 链路 OK，问题在采集脚本时机
- 只有 `\x00` → 固件没跑（多半是 §2.3 连接残留）
- PermissionError → 端口被占用（`tasklist | grep python` → taskkill）

### 4.3 采集要点（dbg_run.py 已实现，写新脚本时照抄）
- reader 线程逐行读，`\x1b\[[0-9;?]*[A-Za-z]` 正则剥离 ANSI（shell 提示符
  重绘会污染行首）
- token 匹配用 `re.search()` 不用 `^`/`.match()`（行首可能有 `uart:~$ `）
- 串口只在 capture 轮开（prime 轮输出丢弃，否则两轮混排造成测试顺序错乱假象）

## 5. 分层根因定位（方法论 → embedded-debug §1；本段只留 QSH 补充）

分层方法（自上而下应用→驱动→硬件、读源码勿猜、区分谁先错、警惕弱校验假 PASS）
见 **embedded-debug §1**。QSH/810 语境下的补充线索：

- **驱动层**：I3C target ISR 的分发顺序 = 中断标志位判定顺序（RXPEND → STOP/
  ERRWARN → MATCHED → TXSEND），判定顺序决定事件优先级；"回调里无条件赋值/覆盖"
  类代码常是覆盖 bug 源头（把对侧设置的参数在 stop/end 回调里无条件重置）
- **硬件层寄存器**：基址/偏移查驱动 reg 头文件；关键位域含义查
  `<driver>/ls_<x>/reg_<x>.h`（如 SERRWARN 的 TERM/URUN/URUNNACK 位 → 区分
  "master 提前终止"还是"硬件欠载"）；寄存器读全对但波形异常 → 示波器，见
  embedded-debug §3.3
- 应用层（token reason/记账变量）线索见 `qsh-test-compare`（假 PASS 识别 §2）

## 6. 断点抓现场（QSH 命令模板；方法 → embedded-debug §2）

**断在"状态被破坏之前"、一轮只抓一个时刻**（方法见 embedded-debug §2）。
QSH 双核固件的裸地址断点模板（目标固件无调试信息时用 nm 查符号地址，
链接地址 = 加载地址）：

```bash
# 找符号地址
riscv64-unknown-elf-nm <build>/target/zephyr/zephyr.elf | grep <symbol>
riscv64-unknown-elf-gdb -q \
  -ex "target remote 127.0.0.1:1025" -ex "file <target>/zephyr/zephyr.elf" \
  -ex "reset" -ex "load" \
  -ex "restore <controller>/zephyr/zephyr.bin binary 0x10000000" \
  -ex "set \$pc=0x100c0000" \
  -ex "break *0x<addr>" -ex "continue" \
  -ex "x/dw 0x<变量地址>" -ex "x/wx 0x<寄存器地址>" \
  -ex "detach" -ex "quit"
```

要点：
- 一轮断点只抓一个时刻；命中后读现场、`detach` 让目标继续跑，避免影响时序
- 变量用 `x/dw`/`x/dh`（有符号），寄存器用 `x/wx`，数组用 `x/Nbx`
- 临时调试脚本（如 probe_*.py）用完必须清理（§8）
- 需要验证"哪个分支被执行"时，直接断在该分支代码处而非回调入口

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
| DebugServer 重启后立即连跑间歇性 load_fail/断连/WDR 异常 | 新实例不稳定期（§10.20） | 重启后等 ~30s 稳定期再烧录；run_test.py 已自动重试 |
| 用 dbg_run 链跑判 PASS 但日志有 FAIL token | dbg_run 不做判定（退出码恒 0） | 判 PASS 前 grep `CTRL:FAIL\|TGT:FAIL`；正式判定走 run_i3c_tests 报告（§10.21） |
| load 极慢（~1KB/s，192KB 镜像要 ~190s） | 没设写包参数 | `set memory-write-packet-size 16384` + `set remotewritesize 16384`（41x，§3.1）；单核 dbg_run.py 默认已开 |
| 会话内第二个 load 无任何输出（无 "Loading section"）静默挂死 | 二次 load 挂死（§10.24） | 单核改单遍烧录（§3.1）；免烧迭代用 --no-load；挂死后按 §2.3 重启 DebugServer |

## 8. 探针纪律（方法 → embedded-debug §4；本段留 QSH 实例）

通用纪律（插桩必清、功能修复 vs 调试残留区分）见 **embedded-debug §4**。
QSH/810 实例：

- 被插桩的共享文件用 `git checkout` 恢复：`log_core.c` / `kernel/timeout.c` /
  `__assert.h` 等；驱动/固件手工删除后 `grep` 确认无残留
- **功能修复必须保留**（不是调试残留）：MPIL workaround（soc_isr_stacking_e906.c
  unstack 恢复 MPIL）、pmp.c 的 PMP_W（XIP 区可写）；调试残留一律删
  （`log_dbg_*`/`to_dbg_*`/GPIO 标记/while(1) 陷阱/临时 probe 脚本）。用 `git diff`
  逐文件甄别
- 注释边界规范（只写本层语义）见 embedded-debug §5 与 CLAUDE.md §4.6

## 9. 坑沉淀

- 只沉淀会反复遇到的通用坑 → embedded-debug §6 + CLAUDE.md §10/速查表；
  一次性定位闭环的 bug → 问题总结 .md 存档（CLAUDE.md §7），不进速查

## 附：scons / HAL 示例验证（低频非主环境）

主开发环境是 **Zephyr**；如确需验证 `modules/hal/linkedsemi/examples`（`scons ic=qsh`）的
HAL 示例（uart_capture 采集 / `_run.gdb` 冷启动 / dump ret-pass 判读），见同目录
[ref-scons-hal-verify.md](ref-scons-hal-verify.md)。本 skill 正文面向 zephyr 主环境调试。
