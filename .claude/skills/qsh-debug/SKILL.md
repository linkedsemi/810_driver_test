---
name: qsh-debug
description: QSH 驱动 debug 调试 skill（三大类之一）：驱动有问题时的完整调试方法学——连接层（DebugServer/GDB 烧录/串口识别与坑）、断点抓现场（nm 裸地址断点/寄存器/内存分析）、分层根因定位（应用层→驱动层→硬件层）、探针纪律与清理。烧录失败/固件不跑/串口无数据/驱动行为异常/定位根因时调用。测试执行见 qsh-test-run，代码生成见 qsh-test-gen。
---

# QSH 驱动 debug 调试（三大类之一：驱动有问题时）

**覆盖"驱动有问题时"的全部调试工作**：环境链路（连得上、烧得进、抓得到）
+ 现场抓取（断点/寄存器/内存）+ 根因定位（分层方法学）+ 探针清理。
构建/跑用例流程在 `qsh-test-run`，工程规范以 `810_driver_test/CLAUDE.md` 为准。

## 0. 调试总流程（先走流程，再动手）

```text
1. 确认环境   §1-3：DebugServer 在跑、能烧录、串口有 banner（全 ? 先清连接残留）
2. 复现抓现象  qsh-test-run §3：dbg_run.py + token/SUMMARY；--raw 看原始日志
3. 分层定位   §4（自上而下，每层排除后再下沉）：
   ├─ 应用层：测试固件回调/协议解析/记账（token reason 已给线索）
   ├─ 驱动层：ISR 分发、数据路径、状态机（读驱动源码确认机制，勿猜）
   └─ 硬件层：寄存器现场（断点+读寄存器）+ 总线波形（示波器/逻辑分析仪）
4. 断点抓现场 §5：断在"状态被破坏之前"，一轮只抓一个时刻
5. 修复 → 验证 → 探针清理（§8）→ 沉淀（§9 + CLAUDE.md §10）
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
| 在线判定 = GDB 连一次 | `riscv64-unknown-elf-gdb -ex "target remote 127.0.0.1:1025" ...` 出现 "Remote debugging" 即通。**不要用 netstat 判离线**（用户明确纠正过） |
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
> DebugServer 进程。**脚本化自愈四步**（run_*_tests.py 的 `clean_gdb_residue`）：
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
- run_i3c_tests.py 必须显式传口号：`--ctrl-uart COM15 --tgt-uart COM16`
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
  dbg_run.py / run_*_tests.py 均为交互式（0 失败）。手工 GDB 也照此
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

## 5. 分层根因定位方法学

**自上而下，每层确认排除后再下沉一层；驱动行为先读源码确认机制，不要猜。**

### 5.1 应用层（测试固件）
- token reason 已给线索（如 `read actual 0, URUNNACK`）→ 查固件回调/协议解析
- 固件记账变量（`dbg_*_cnt`/`TGT_DBG` 汇总行）区分"谁先错"：controller 读
  到 0 还是 target 没发数据
- 注意校验逻辑缺陷造成的假 PASS（target 只校验部分字段——见 qsh-test-compare §2）

### 5.2 驱动层（zephyr/drivers/<driver>/）
- 读 ISR 分发顺序：中断标志位判定顺序决定事件优先级（如 I3C target ISR 的
  RXPEND → STOP/ERRWARN → MATCHED → TXSEND 顺序）
- 读数据路径与状态机：数据缓冲模型、FIFO 续写、状态迁移守卫
- 关注"回调里无条件赋值/覆盖"类代码——常是覆盖 bug 源头（把对侧设置的
  参数在 stop/end 回调里无条件重置），用断点对比"请求时刻 vs 结束时刻"
  的变量值即可证实（§6）

### 5.3 硬件层（寄存器 + 波形）
- 外设寄存器直接 `x/wx <基址+偏移>` 读（基址/偏移从 reg 头文件确认）
- 关键寄存器位域含义查 `<driver>/ls_<x>/reg_<x>.h`（如 SERRWARN 的
  TERM/URUN/URUNNACK 位 → 能区分"master 提前终止"还是"硬件欠载"）
- 总线时序问题用示波器/逻辑分析仪配合（GPIO 标记法见 CLAUDE.md §4.3）

## 6. 断点抓现场法（★ 定位硬件/驱动状态的关键手段）

**核心原则：断在"状态被破坏之前"**。反例：断点在结果记账函数（如
`tr_record`）命中时，stop/end 回调早已把计数器清零、长度恢复默认——抓到的
是测试结束后的状态，不是现场。正例：断在请求到达回调（如 target 的读请求
回调），命中时 FIFO/长度变量还是请求时刻的真实值。

```bash
# 目标固件无调试信息时：nm 查符号地址（链接地址=加载地址），裸地址断点
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
- **一轮断点只抓一个时刻**；命中后读现场、`detach` 让目标继续跑，避免影响时序
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
| 断点抓到的是"测试后"状态 | 断点时机太晚（记账处） | 断在状态被破坏之前（§6） |
| DebugServer 重启后立即连跑间歇性 load_fail/断连/WDR 异常 | 新实例不稳定期（§10.20） | 重启后等 ~30s 稳定期再烧录；run_i3c_tests.py 已自动重试 |
| 用 dbg_run 链跑判 PASS 但日志有 FAIL token | dbg_run 不做判定（退出码恒 0） | 判 PASS 前 grep `CTRL:FAIL\|TGT:FAIL`；正式判定走 run_i3c_tests 报告（§10.21） |
| load 极慢（~1KB/s，192KB 镜像要 ~190s） | 没设写包参数 | `set memory-write-packet-size 16384` + `set remotewritesize 16384`（41x，§3.1）；单核 dbg_run.py 默认已开 |
| 会话内第二个 load 无任何输出（无 "Loading section"）静默挂死 | 二次 load 挂死（§10.24） | 单核改单遍烧录（§3.1）；免烧迭代用 --no-load；挂死后按 §2.3 重启 DebugServer |

## 8. 探针纪律（用户要求，防污染其他工程）

- 调试插桩（`log_dbg_*`/`to_dbg_*`/GPIO 标记/while(1) 陷阱/临时 probe 脚本）
  **调试完必须清理**：log_core.c / kernel/timeout.c / `__assert.h` 等被插桩
  文件用 `git checkout` 恢复；驱动/固件手工删除后 `grep` 确认无残留。
- **功能修复与调试残留要区分**：驱动/SoC 层的功能修复（如 MPIL workaround、
  PMP_W 写保护）必须保留；调试残留一律删。用 `git diff` 逐文件甄别。
- 详见 CLAUDE.md §10.6。

### 8.1 驱动注释规范（★ 职责边界：只写本层语义，不写外部历史）

调试驱动时**新增/修改注释必须遵守 CLAUDE.md §4.6**：只描述**本层（驱动）
的行为语义与契约**，不得把属于外部系统（Zephyr OS 框架、历史 bug、调试
过程）的原因写进驱动注释。判断标准："如果这段代码换到别的工程/别的 OS
配置下，这条注释还成立吗？"不成立就不该写在本层。

- **应该写**：协议语义（"TERM/URUN 在读欠载是 controller 主动提前终止的
  合法行为"）、硬件行为（"STOP 分支必须 FLUSHTB 清空 TX FIFO"）、设计约束
  （"opcode 用 ASCII 助记符避免与 CCC 命令码混淆"）。
- **不应该写**：OS 层问题（"panic_mode 已修，ztest 不触发 LOG_PANIC"）、
  调试历史（"之前 LOG_ERR 阻塞 10ms 造成 8ms 死窗"）、环境问题（"Windows
  gdb stdout 管道块缓冲"）。
- **外部原因的归处**：反复会踩的通用坑 → §7 速查表 / CLAUDE.md §10；
  一次性定位闭环的 bug → 问题总结 .md（CLAUDE.md §7）；调试临时注释 →
  按 §8 探针纪律清理，不得随功能代码提交。

## 9. 坑沉淀要求

按 CLAUDE.md §7 沉淀准则：**只沉淀会反复遇到的通用坑**（连接残留、I-cache、
ISR 禁 LOG 等）到 §7 速查表 + 顶层 CLAUDE.md §10；**一次性问题**（本次修复后
不会再遇到的具体 bug/现象）只写问题总结存档（.md），不加入速查表/§10。
定位闭环的问题按 CLAUDE.md §7 写问题总结存档。
