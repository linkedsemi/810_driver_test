# GDB / DebugServer 调试指南

## 1. 检查 DebugServer 状态（只读优先）

```bash
cd 810_driver_test/wsl_environment/wsl_new
python3 scripts/ds_status.py          # 进程 + TCP，只读
python3 scripts/ds_status.py --probe  # 额外 GDB target remote + detach（可能留残留）
python3 scripts/ds_status.py --json
```

- 进程判断同时使用 `tasklist` 与 `netstat -ano` 的监听 PID，避免 WSL 管道截断误报。
- **本工具不启动、不停止、不重启 DebugServer。** DS 由用户启动：
  `D:\download\zephyr\debugserver\v5_18_10\bin\DebugServerConsole.exe`。
- DS 正常时会打印 `target remote 172.24.48.1:1025`（CPU0）与 `:1026`（CPU1）。

## 2. 下载并运行 ELF

```bash
cd 810_driver_test/wsl_environment/wsl_new

# 默认：config.paths.elf（cpu1 hello_world），run 15s
python3 scripts/gdb_load.py

# 指定 ELF / 端口 / 入口
python3 scripts/gdb_load.py --elf build/hello_world_cpu2/zephyr/zephyr.elf \
  --port 1026 --pc 0x10000000 --run-secs 10

# 只 load 不运行
python3 scripts/gdb_load.py --no-continue

# 只看命令
python3 scripts/gdb_load.py --dry-run
```

固定序列（与 `.vscode/launch.json` "cursor cpu1 load ram" 对齐）：

```text
target remote HOST:PORT
set confirm off
set pagination off
set mem inaccessible-by-default off
file <elf>
reset
set memory-write-packet-size 16384
set remotewritesize 16384
load
set $pc=<pc>
continue          # 可 --no-continue
```

运行 `--run-secs` 秒后发送 `SIGINT`，再 `detach`/`quit`；detach 会让目标继续运行，
板子停在可运行状态。完整输出写入 `wsl_new/logs/gdb_load_<时间戳>.log`。

## 3. 退出码

| 码 | 含义 |
|---|---|
| 0 | load 成功（输出含 `Loading section`） |
| 3 | 预检失败：ELF 不存在 / 找不到 gdb / DS TCP 不可达 |
| 4 | load 失败或出现连接/内存错误 |

## 4. 判读证据

成功时日志应包含：

```text
Loading section ... (13 个 section)
Start address 0x100c0000, load size ...
Transfer rate: ... KB/sec
Continuing.
Program received signal SIGINT, Interrupt.
arch_cpu_idle ()
[Inferior 1 (Remote target) detached]
```

`SIGINT` 停在 `arch_cpu_idle` 说明 Zephyr 已启动并进入 idle（内置用例已跑完）。

## 5. 安全约束与残留处理

- `gdb_load.py` **绝不** kill/重启 `DebugServerConsole.exe`。
- 检测到本机残留 gdb 只告警；清理需显式 `--kill-stale-gdb`（SIGTERM 本机 gdb）。
- 若 DS 不在线：先 `ds_status.py`，记录现场，由用户决定是否启动；脚本不会代管。
- 旧经验：完整 GDB probe/detach 可能造成 DS 连接残留；优先用 `gdb_load.py` 直接 load，少做 probe。
