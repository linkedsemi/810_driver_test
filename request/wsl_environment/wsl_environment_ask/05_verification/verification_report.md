# 验证报告（本轮实测）

## 结论总览

| 项 | 结果 | 证据 |
|---|---|---|
| 构建 cpu1 hello_world | **PASS** | `raw/build_tail.txt`；ELF entry `0x100c0000` |
| 构建 cpu2 hello_world | **PASS** | ELF entry `0x10000000` |
| TCP 172.24.48.1:1025 | **PASS** | `ds_status.py`：`tcp: OK` |
| GDB 连接/probe | **PASS** | `0x100cf4fe in ?? ()` + `[Inferior 1 (Remote target) detached]` |
| 上板 load（cpu1） | **PASS** | `raw/gdb_load_cpu1.log`：13 个 section、`Start address 0x100c0000`、`Continuing.`、SIGINT 停 `arch_cpu_idle` |
| 上板 load（cpu2 对照） | **PASS** | `raw/gdb_load_cpu2.log`：`Start address 0x10000000`、`arch_cpu_idle` |
| 串口启动日志 | **PASS** | `raw/uart_COM24_hello.log`：`*** Booting Zephyr OS ...` + `Hello World! lsqsh_evb@2os/lsqsh/cpu1`；确认 COM24 = cpu1 console |

## 1. 构建

命令（在 `/home/eunice/zephyr_workspace/ls1010_port_spi_i2s` 执行）：

```bash
west build -p always -b 'lsqsh_evb@2os/lsqsh/cpu1' \
  -d 810_driver_test/wsl_environment/wsl_new/build/hello_world \
  zephyr/samples/hello_world
```

关键输出（`raw/build_tail.txt`）：

```text
[227/227] Generating ...zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin.gz
Memory region  Used Size  Region Size  %age Used
        ROM:      104 KB      256 KB     40.62%
        RAM:       36 KB      256 KB     14.06%
```

未覆盖用户现有 `build/` 与 `zephyr/samples/hello_world/build`。

## 2. DebugServer / GDB 连通

- 环境自检：`env_check.py` **20 PASS / 0 WARN / 0 FAIL**（`raw/env_check_final.txt`）。
- 任务开始时 PID 27644 已不存在，1025 拒绝连接（【实测】）。
- 用户授权上板验证后，手动启动新 DS 实例 **PID 16216**（未杀任何进程）；监听 `0.0.0.0:1025`/`1026`。
- `python3 scripts/ds_status.py`：

```text
process: RUNNING pids=[16216]
listen : 172.24.48.1:1025 pids=[16216]
tcp    : OK (connected)
```

- `python3 scripts/ds_status.py --probe`（原始输出见 `raw/ds_probe.txt`，2026-10-27 16:08 复跑）：

```text
gdb    : OK
0x100cce76 in ?? ()
[Inferior 1 (Remote target) detached]
```

## 3. 上板 load（cpu1）

`raw/gdb_load_cpu1.log` 节选：

```text
Loading section .text, ... 
...
Loading section .last_section, size 0x9d0 lma 0x100d9630
Start address 0x100c0000, load size 106494
Transfer rate: 45 KB/sec, 2878 bytes/write.
Continuing.

Program received signal SIGINT, Interrupt.
arch_cpu_idle ()
    at .../zephyr/arch/riscv/core/cpu_idle.c:15
[Inferior 1 (Remote target) detached]
```

`arch_cpu_idle` 表明 Zephyr 已完成启动并进入 idle。GDB 退出后 detach，目标继续运行。

## 4. 串口

| 尝试 | 结果 |
|---|---|
| COM24 单口采集 3s（WindTerm 占用时） | `PermissionError(13, '拒绝访问。')`，见 `raw/uart_meta_com24_error.json` |
| COM23 采集 + cpu1 load（第一次） | `uart_COM23.log` 0 字节 |
| COM23 采集 + cpu1 load（复测） | 0 字节，见 `raw/com23_empty.txt` |
| COM16/COM5/COM23 + cpu2 load | 全部 0 字节 |
| **COM24 采集 + cpu1 load（用户关闭 WindTerm 后）** | **PASS**：抓到 Zephyr 启动日志，见 `raw/uart_COM24_hello.log` |

成功抓取的日志（2026-10-27 16:03，`raw/uart_COM24_hello.log`）：

```text
*** Booting Zephyr OS build openbmc-zephyr-v0.1.0-886-gda8f191b0031 ***
Hello World! lsqsh_evb@2os/lsqsh/cpu1
```

采集命令（Windows 侧采集 60s，同时 WSL 触发一次 load）：

```text
cmd.exe /c 'python capture_uart.py --ports COM24 --seconds 60 --share-dir C:\Temp\wsl_810_new'
python3 scripts/gdb_load.py --run-secs 12
```

COM 身份（`raw/scan_ports.txt`）：

```text
COM23    VID:PID=1A86:55D2  USB-Enhanced-SERIAL-A CH342 (COM23)
COM24    VID:PID=1A86:55D2  USB-Enhanced-SERIAL-B CH342 (COM24)
COM16    VID:PID=04D8:00DD  USB Serial Port (COM16)
```

结论：

- CH342 **B = COM24** 是 cpu1 console（【实测】，已抓到 `Hello World` / `*** Booting Zephyr`）。
- CH342 **A = COM23** 可排除为默认 cpu1 console（两次启动 0 字节）【实测】。

## 5. 板子最终状态

- 最后一次操作为 cpu1 hello_world load（`gdb_load_20261027_160354.log`，16:03 串口验证），运行 12s 后 SIGINT + detach；同一次启动的串口日志已抓到。
- detach 后目标继续运行，板子处于 **cpu1 hello_world 运行态**；未做 flash 擦写。
- DebugServer PID 16216 保持运行，未停止/重启。

## 6. 未决项

1. DS 是否长期由用户手动管理（脚本不自动启停）。
2. cpu2 console 的 uart4/uart5 冲突需按硬件接线确认。
3. 是否把 WSL2 宿主差异沉淀为 `qsh-debug/ref-host-wsl2.md`（本轮未改 skill 库）。

## 7. 2026-10-29 SPIF 回环上板验证

用户授权继续后，本轮按既有 WSL 流程直接拉起 DS（未杀/未重启旧进程），
完成 `projects/spif_test` 的 1os 构建、RAM load 与 COM24 采集：

| 项 | 结果 | 证据 |
|---|---|---|
| 构建 `spif_test` 1os | PASS | `build_spif_1os/zephyr/zephyr.elf`；24 个 `_ztest_unit_test__spif__*` 符号 |
| DS 监听 | PASS | `ds_status.py`：`RUNNING pids=[21412]`、`172.24.48.1:1025`、`tcp OK` |
| RAM load | PASS | `wsl_new/logs/gdb_load_20261029_111848.log`：`Loading section x18` |
| 串口 | PASS | COM24：`ZT_BOOT TGT`、24×`PASS`、`TESTSUITE spif succeeded`、`PROJECT EXECUTION SUCCESSFUL` |
| 回环 | PASS | `loopback_jedec`：`ret=0, jedec=ef4021`；`loopback_read_addr`：`ret=0, data=ff0a01ed` |
| filter 矩阵 | PASS | 合法+合法 `raw=0`；合法命令+非法地址 `addr_err=1, por_addr=1, error_addr=0x4000`；非法命令+合法地址 `cmd_err=1, addr_err=0`；两者都非法 `cmd_err=1, addr_err=0`（命令先查） |
| 官方离线重放 | PASS | `regression/replay.py --l2-direct`：6 列表格、`PASS (24/24)`、`TESTSUITE spif succeeded` |

归档：

- 串口：`wsl_new/logs/spif_test_uart_COM24_20261029_1118.log`
- 报告：`810_driver_test/reports/2026-10-29-1/spif_test_case_default/`
- 结论：`spif_test case_default` **24/24 PASS**；`cmd_idx_accessors` 原 FAIL 为测试选槽问题
  （槽 5 READ 无 dummy 相），非驱动缺陷。
