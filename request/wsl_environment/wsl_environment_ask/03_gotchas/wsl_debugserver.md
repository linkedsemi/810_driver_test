# WSL ↔ Windows DebugServer / 串口 坑与规避

本文件记录本轮实际踩到、会反复出现的问题。一次性事故不写入工具或 skill。

## 1. WSL 调 `cmd.exe` 时引号被吞（`tasklist /FI` 报无效参数）

- 现象：`cmd.exe /c 'tasklist /FI "IMAGENAME eq DebugServerConsole.exe"'` 报 `无效参数/选项 - 'eq'`。
- 原因：WSL interop 会重写/剥离双引号，`/FI` 的参数被拆开。
- 规避：不要用带引号的过滤条件；直接 `tasklist` 后按名字解析（`common.tasklist_debugserver()`），或用 PowerShell `-File` 执行脚本文件。

## 2. WSL 捕获 `cmd.exe` stdout 偶发截断

- 现象：同一时刻 `cmd.exe /c tasklist` 经 WSL 只返回 1 行，导致 `ds_status.py` 误报 DS **STOPPED**；但 `netstat` 显示 1025 仍由 PID 16216 监听。
- 原因：WSL interop 的 stdout 管道偶发截断（本轮实测 `/tmp/tl5.txt` 仅 1 行）。
- 规避：关键判断需要**双证据**：`tasklist` + `netstat -ano` 监听 PID；`tasklist_debugserver()` 空结果自动重试 3 次。不要仅凭一次 `tasklist` 判定 DS 已退出。

## 3. NAT 模式地址：用 `172.24.48.1`，不要用 mirrored / Clash TUN 地址

- NAT：GDB target = WSL `default via`（本机 `172.24.48.1:1025`，以 DS 打印为准）。
- mirrored：才用 `127.0.0.1:1025`，且与 DS 打印的 `172.24.48.1` 不一致。
- Clash TUN 的 `198.18.*` / `198.19.*` 是伪网关，TCP 可能"开"但 GDB 会 reset。
- 规避：`common.detect_windows_host_ip()` 跳过 198.18/19；`.wslconfig` 保持 NAT。

## 4. 从 `/mnt/c` 以外目录调 `cmd.exe` 可能报 UNC 路径

- 规避：`common.run_win_cmd()` 固定 `cwd=/mnt/c`；Windows 助手脚本先 `cd /d` 到共享目录。

## 5. GDB probe/detach 可能留下 DS 连接残留

- 现象：连接成功但后续 load 报 `Connection reset` / 串口只有 `\x00`。
- 规避：`ds_status.py` 默认只查进程 + TCP；`--probe` 才做 `target remote`+`detach`。`gdb_load.py` 检测本机残留 gdb 只告警，清理需显式 `--kill-stale-gdb`（只杀本机 gdb，绝不动 DS）。

## 6. 脚本超时用 `communicate(timeout)` 后 stdin 已关闭

- 现象：本轮首版 `gdb_load.py` 在 `continue` 超时后写 `\x03` 抛 `ValueError: I/O operation on closed file`，gdb 变孤儿进程并占着 1025。
- 规避：用读取线程持续收 stdout，主线程 `SIGINT` 中断 gdb，再发 `detach`/`quit`。见 `scripts/gdb_load.py` 的 `GdbSession`。孤儿 gdb 用 `kill -INT` 清理。

## 7. COM24 被 WindTerm 独占（已解除；Windows 串口独占）

- 现象：`capture_uart.py --ports COM24` 报 `PermissionError(13, '拒绝访问。')`；`WindTerm.exe` PID 17520 在运行。COM24 = CH342 **Serial-B** = cpu1 console。
- 原因：WindTerm 已打开该串口（Windows 串口通常独占）。
- 规避：
  1. 在 WindTerm 中关闭该 COM24 会话（不要杀进程，除非用户确认）；
  2. 再按 [`../04_guides/serial.md`](../04_guides/serial.md) 采集；
  3. 或让 WindTerm 自己开启会话日志，从日志取启动文本。
- 本轮结果：用户手动关闭 WindTerm 的 COM24 会话后，2026-10-27 16:03 采集成功，抓到 `*** Booting Zephyr OS ...` + `Hello World! lsqsh_evb@2os/lsqsh/cpu1`（`05_verification/raw/uart_COM24_hello.log`）。
- 禁止：不要用脚本强杀 WindTerm 或 DS 来抢串口。

## 8. pyserial 枚举不到 CKLink（COM29）

- 现象：`serial.tools.list_ports` 只列出 6 个口，不含 COM29；PowerShell `Win32_PnPEntity` 能看到 `CKLink Serial Port (COM29)`。
- 影响：不能用 pyserial 的枚举结果判断 CKLink 是否在位；DS 是否可用应以 DS 进程/端口与 CKLink 设备管理器状态为准。
- 规避：需要 VID:PID 时用 `scan_ports.py`；需要 CKLink 在位证据时用 PowerShell WMI。

## 9. 任务开始时的 DS PID 会过期

- 现象：任务说明称 PID 27644 在运行，实际复核时进程不存在、1025 拒绝连接。
- 规避：每轮先跑 `ds_status.py`；不要沿用上一轮快照。DS 不存在时按用户授权决定是否手动拉起，脚本本身不自动启动。

## 10. cpu2 console 定义存在文档冲突（uart4 vs uart5）

- 现象：`lsqsh_evb_cpu2.dts` 的 `chosen zephyr,console = &uart4`，而 `810_driver_test/CLAUDE.md` 写 cpu2 = uart5。
- 实测：cpu2 hello_world（默认 uart4）运行到 `arch_cpu_idle`，COM16/COM5/COM23 均 0 字节；尝试用 overlay 把 console 改到 uart5 构建失败（board pinctrl/节点约束）。
- 规避：不要假设 cpu2 console 口；需要时先确认物理接线与 dts，再定义采集端口。

## 11. 从 WSL 用 `cmd.exe /c start ...` 拉起 DS 会挂起

- 现象：`subprocess.run(["cmd.exe","/c","start","DebugServer",...])` 30s 超时，DS 未启动。
- 规避：直接 `Popen(["/mnt/d/.../DebugServerConsole.exe"])`（本轮成功，PID 16216）；或让用户在 Windows 侧双击/批处理启动。工具脚本仍不自动管理 DS。

## 12. 工作区禁止 git 提交

- 本轮全程未执行 `git add/commit/push`；改动以文件形式留在工作区，由用户决定是否入库。
