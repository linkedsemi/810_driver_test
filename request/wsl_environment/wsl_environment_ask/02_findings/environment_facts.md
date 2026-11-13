# 环境事实与证据

标注规则：【实测】= 本机本轮验证并附命令/输出；【假设·待验】= 类比/历史资料，未在本轮直接验证。

## 1. 宿主与 WSL

| 事实 | 标注 | 证据 |
|---|---|---|
| WSL2 Ubuntu 22.04，内核 `6.6.87.2-microsoft-standard-WSL2` | 【实测】 | `uname -r`；`env_check.py` host 项 |
| systemd 开启，`generateResolvConf=false` | 【实测】 | `/etc/wslconfig` 与 `/etc/resolv.conf` |
| `.wslconfig`：memory=8GB / processors=4 / swap=16GB，**NAT 模式**（注释明确勿开 mirrored） | 【实测】 | `cat /mnt/c/Users/34726/.wslconfig` |
| `default via 172.24.48.1 dev eth0`，WSL IP `172.24.61.50/20` | 【实测】 | `ip route`；`ip -4 addr show eth0` |
| `/etc/resolv.conf` 为公网 DNS（223.5.5.5 等），不是 Windows 主机地址 | 【实测】 | `cat /etc/resolv.conf` |
| Clash TUN 的 `198.18.*` 网关不可作为 DebugServer 地址 | 【实测】 | 旧 GOTCHAS 结论；`common.detect_windows_host_ip()` 显式跳过 |

## 2. DebugServer

| 事实 | 标注 | 证据 |
|---|---|---|
| 本轮开始时 `DebugServerConsole.exe` **未运行**，PID 27644 已不存在，1025 端口拒绝连接 | 【实测】 | `tasklist`、`netstat -ano`、TCP `ConnectionRefusedError` |
| 为完成用户授权的上板验证，手动启动新实例，PID **16216** | 【实测】 | `tasklist`；`netstat -ano`：`0.0.0.0:1025 LISTENING 16216` |
| DS 监听 CPU0=`1025`、CPU1=`1026` | 【实测】 | DS 控制台输出 `GDB connection command for CPUs(CPU0/CPU1)` |
| DS 识别 CKLink-Lite（`CKLink_Lite_V2`，SN `T0000001363643307530794`），目标 `XT-E906FDP` | 【实测】 | `05_verification/raw/ds_console_head.txt` |
| DS 可执行文件 `D:\download\zephyr\debugserver\v5_18_10\bin\DebugServerConsole.exe` 存在 | 【实测】 | 映射到 `/mnt/d/...` 后 `is_file()` 为真 |
| GDB `target remote 172.24.48.1:1025` + `detach` 成功，PC 停在 `0x100cf4fe` | 【实测】 | `ds_status.py --probe` 输出 |
| GDB 端口 1025 对应 Zephyr `cpu1` | 【假设·待验】 | 沿用旧 `.vscode/launch.json` 与旧环境的用法，本轮 load 行为与预期一致 |

## 3. 工具链与 Zephyr

| 事实 | 标注 | 证据 |
|---|---|---|
| gcc `riscv64-unknown-elf-gcc 14.1.1`（Xuantie V3.0.1） | 【实测】 | `--version` |
| gdb `15.0.50.20240618-git` | 【实测】 | `--version` |
| west `v1.4.0` | 【实测】 | `west --version` |
| Zephyr `4.0.0`，`ZEPHYR_BASE=/home/eunice/zephyr_workspace/ls1010_port_spi_i2s/zephyr` | 【实测】 | `VERSION` 文件；`config.json` |
| `ZEPHYR_TOOLCHAIN_VARIANT=cross-compile`，`CROSS_COMPILE=/opt/toolchain/Xuantie-900-gcc-elf-newlib-x86_64-V3.0.1/bin/riscv64-unknown-elf-` | 【实测】 | 环境变量/CMakeCache |
| board `lsqsh_evb@2os/lsqsh/cpu1`，`CONFIG_BOARD_TARGET` 与之一致 | 【实测】 | `build/hello_world/zephyr/.config` |
| cpu1 ELF entry `0x100c0000` | 【实测】 | `readelf -h`；DS load `Start address 0x100c0000` |
| cpu2 ELF entry `0x10000000`（2os/cpu2） | 【实测】 | `readelf -h`；DS load `Start address 0x10000000` |

## 4. 串口

Windows 枚举（`win_helpers/scan_ports.py` + PowerShell WMI 交叉确认）：

| COM | VID:PID | 描述 | 角色判定 | 标注 |
|---|---|---|---|---|
| COM23 | 1A86:55D2 | USB-Enhanced-SERIAL-A CH342 | CH342 A | 【实测】 |
| COM24 | 1A86:55D2（MI_02） | USB-Enhanced-SERIAL-B CH342 | CH342 B；**cpu1 console**（已抓到启动日志） | 【实测】硬件身份 + 角色 |
| COM16 | 04D8:00DD | USB Serial Port（MCP2200） | 旧资料：controller/cpu2 | 【假设·待验】 |
| COM29 | 32BF:B210 | CKLink Serial Port | 调试器，排除 | 【实测】 |
| COM5 | 1A86:55D4 | CH9102 | 未知 | 【实测】存在 |
| COM3/COM4 | —— | 蓝牙串口 | 无关 | 【实测】 |

关键实测：

- COM24 被 WindTerm 占用时打开失败：`PermissionError(13, '拒绝访问。')`（`WindTerm.exe` PID 17520）。用户关闭该会话后，2026-10-27 16:03 采集成功，抓到 `*** Booting Zephyr OS build openbmc-zephyr-v0.1.0-886-gda8f191b0031 ***` + `Hello World! lsqsh_evb@2os/lsqsh/cpu1`（`05_verification/raw/uart_COM24_hello.log`）。【实测】
- COM23 在两次 cpu1 hello_world 启动期间均采集到 **0 字节**（`uart_COM23.log` 大小 0）。【实测】
- COM16/COM5 在 cpu2 hello_world 运行期间同样 0 字节。【实测】
- 旧仓库脚本 `810_driver_test/scripts/equiv_before/*/run_test.py` 注释：`Prefer CH342 Serial-B (target uart1) over Serial-A`；旧 `wsl_environment/config.yaml` 回退值 `COM24`。【实测·历史资料】
- 旧 `logs/discovered_tgt_uart.json`：`"tgt_uart": null, "hint": "COM24"`（历史扫描未成功，仅提示）。【实测·历史资料】

结论：

- CH342 B（COM24）是 cpu1 console。【实测】已抓到 `Hello World` 启动日志。
- CH342 A（COM23）可打开且无输出，可**排除**其为默认 cpu1 console。【实测】

## 5. 网络与互操作补充

| 事实 | 标注 | 证据 |
|---|---|---|
| `cmd.exe /c tasklist` 经 WSL interop 偶发只返回 1 行（stdout 截断） | 【实测】 | `/tmp/tl5.txt` 仅 1 行；同刻 Python 直连可拿到 DS PID |
| 由 WSL 用 `cmd.exe /c start ...` 拉起 DS 会挂起至超时 | 【实测】 | 30s `TimeoutExpired`，且 DS 未起来 |
| 从 WSL 直接 `Popen(["/mnt/d/.../DebugServerConsole.exe"])` 可正常拉起 DS | 【实测】 | PID 16216 与 1025/1026 监听 |
| WSL 当前目录为 `/mnt/c` 时 `cmd.exe` 不报 UNC 警告；脚本统一 `cwd=/mnt/c` | 【实测】 | `common.run_win_cmd` |
