# 串口采集指南（Windows 助手 → 共享文件 → WSL）

## 1. 同步 Windows 助手

```bash
cd 810_driver_test/wsl_environment/wsl_new
python3 scripts/sync_win_helpers.py
```

共享目录来自 `config.json` 的 `serial.share_win`（本机 `C:\Temp\wsl_810_new`），
WSL 侧对应 `/mnt/c/Temp/wsl_810_new`。Windows 助手**默认写脚本所在目录**，
所以同步后用该目录下的脚本即可。

## 2. 枚举 COM 口（Windows）

```bat
cd /d C:\Temp\wsl_810_new
python scan_ports.py
```

本机结果：

| COM | VID:PID | 描述 | 说明 |
|---|---|---|---|
| COM23 | 1A86:55D2 | CH342 **A** | 可打开；本机 cpu1 启动时为 0 字节 |
| COM24 | 1A86:55D2 | CH342 **B** | **cpu1 console（已实测抓到 Hello World）**；WindTerm 占用时需先关闭会话 |
| COM16 | 04D8:00DD | MCP2200 | 旧资料：cpu2 |
| COM29 | 32BF:B210 | CKLink | 调试器，排除 |
| COM5 | 1A86:55D4 | CH9102 | 角色未知 |
| COM3/COM4 | —— | 蓝牙 | 无关 |

> pyserial 可能枚举不到 CKLink（COM29），需要 CKLink 在位证据时用 PowerShell WMI。

## 3. 采集

Windows cmd（单口或多口）：

```bat
python C:\Temp\wsl_810_new\capture_uart.py --ports COM24 --seconds 30
python C:\Temp\wsl_810_new\capture_uart.py --ports COM23,COM24 --seconds 30
python C:\Temp\wsl_810_new\capture_uart.py --list
```

输出：

- `uart_<COM>.log`：采集文本
- `uart_<COM>.meta.json`：单口状态
- `uart_meta.json`：汇总状态（`opened` / `error`）

失败时退出码 1，并在 meta 中记录错误；`--seconds 0` 表示直到 Ctrl+C。

## 4. WSL 读取

```bash
cd 810_driver_test/wsl_environment/wsl_new

python3 scripts/serial_read.py --status              # 看端口是否打开/报错
python3 scripts/serial_read.py --list                # 列出日志
python3 scripts/serial_read.py --port COM24 --lines 80
python3 scripts/serial_read.py --port COM24 --wait-for "Hello World" --timeout 60
python3 scripts/serial_read.py --port COM24 --tail
```

`--wait-for` 命中返回 0，超时返回 1 并打印最后 20 行。

## 5. 抓 Zephyr 启动日志（推荐流程）

窗口 A（Windows cmd）先开始采集：

```bat
python C:\Temp\wsl_810_new\capture_uart.py --ports COM24 --seconds 40
```

窗口 B（WSL）触发一次 load：

```bash
cd 810_driver_test/wsl_environment/wsl_new
python3 scripts/gdb_load.py --run-secs 10
```

窗口 C（WSL）等待日志：

```bash
python3 scripts/serial_read.py --port COM24 --wait-for "Hello World|Booting Zephyr" --timeout 40
```

也可以把采集放后台：

```bash
cd /mnt/c && cmd.exe /c python C:\\Temp\\wsl_810_new\\capture_uart.py --ports COM24 --seconds 40 &
```

## 6. 端口被占用（已解除）

`COM24` 被 WindTerm（PID 17520）占用时，打开返回：

```text
PermissionError(13, '拒绝访问。')
```

处理顺序（本轮已按此解除并抓取成功）：

1. 在 WindTerm 中关闭 **COM24** 的串口会话（不要直接杀进程）；
2. 重新执行第 5 节流程；
3. 或使用 WindTerm 自身的会话日志功能，导出启动文本。

**不要**为了抢串口去 kill WindTerm 或 DebugServer；这类操作需用户确认。

## 7. 当前已知限制

- COM23/COM5/COM16 在 cpu1/cpu2 hello_world 运行期间均采集到 0 字节；COM23 可排除为默认 cpu1 console。
- CH342 Serial-B（COM24）= cpu1 console 已确认（2026-10-27 16:03 抓到 `Hello World`，见 [`../05_verification/raw/uart_COM24_hello.log`](../05_verification/raw/uart_COM24_hello.log)）。
- cpu2 console 存在 `dts uart4` 与 `CLAUDE.md uart5` 的文档冲突；uart5 overlay 构建失败，未继续深挖。
