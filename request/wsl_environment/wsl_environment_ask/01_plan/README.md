# WSL2 Zephyr 开发环境适配 — 方案与状态

## 目标

在现有 `qsh` 环境上完成 **WSL2 宿主轴适配**（Runbook A §3.5，不新开环境）：

1. WSL2 内构建 Zephyr（`lsqsh_evb@2os/lsqsh/cpu1`）
2. WSL → Windows DebugServer 的 TCP/GDB 连通与上板 load
3. Windows 串口采集 → 共享文件 → WSL 读取
4. 经验按类型落盘；工具在 [`../../../../wsl_environment/wsl_new/`](../../../../wsl_environment/wsl_new/README.md) 全新实现

## 边界（本轮用户裁定）

- 旧 `810_driver_test/wsl_environment/` **只读参考，不修改**；`wsl_new/` 为全新实现
- 不改宿主工程源码（`zephyr/`、`modules/`、`projects/` 等）
- 不执行 git 提交/推送
- DebugServer 生命周期由用户掌握：脚本不自动启动/停止/重启 DS
- 上板验证允许复位板子

## 阶段与状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | 目录骨架（`00_request` … `05_verification`；`wsl_new/{config,scripts,win_helpers,build,logs}`） | 完成 |
| P1 | 环境事实采集与标注 | 完成，见 [`02_findings/environment_facts.md`](../02_findings/environment_facts.md) |
| P2 | 工具实现（env_check / ds_status / gdb_load / serial_read / Windows 采集） | 完成 |
| P3 | 构建 + GDB 连通 + 上板 load | 完成，见 [`05_verification/verification_report.md`](../05_verification/verification_report.md) |
| P4 | 串口启动日志抓取 | 完成（用户关闭 WindTerm 的 COM24 会话后补抓成功，COM24 确认 cpu1 console） |
| P5 | 经验沉淀（gotchas / guides） | 完成 |

## 当前状态（本轮结束时）

- 构建：`wsl_new/build/hello_world`（cpu1）与 `build/hello_world_cpu2`（cpu2）均成功
- DebugServer：新实例 PID 16216，监听 `0.0.0.0:1025`（CPU0）/`1026`（CPU1），TCP OK
- 上板：cpu1 ELF 下载 13 个 section、`Start address 0x100c0000`、continue 后 SIGINT 停在 `arch_cpu_idle`；板子最终处于 cpu1 hello_world 运行态
- 串口：COM24（CH342-B）= cpu1 console 已确认（抓到 `*** Booting Zephyr` + `Hello World! lsqsh_evb@2os/lsqsh/cpu1`，见 `05_verification/raw/uart_COM24_hello.log`）；COM23（CH342-A）两次 cpu1 启动均 0 字节，已排除

## 待办 / 未决

| # | 事项 | 需要谁 |
|---|---|---|
| 1 | DebugServer 是否长期由用户手动启动（脚本不代管） | 用户拍板 |
| 2 | 是否把 WSL2 宿主差异沉淀为 `qsh-debug/ref-host-wsl2.md` | 用户拍板（本轮不改 skill 库） |

## 单一信息源

| 信息 | 位置 |
|---|---|
| 机器相关配置 | [`wsl_new/config/config.json`](../../../../wsl_environment/wsl_new/config/config.json) |
| 环境事实与证据 | [`02_findings/environment_facts.md`](../02_findings/environment_facts.md) |
| 坑与规避 | [`03_gotchas/wsl_debugserver.md`](../03_gotchas/wsl_debugserver.md) |
| 使用指南 | [`04_guides/README.md`](../04_guides/README.md) |
| 验证证据 | [`05_verification/verification_report.md`](../05_verification/verification_report.md) |
| 原始需求 | [`00_request/original_ask.md`](../00_request/original_ask.md) |
| 可复用流程 skill（workspace 根） | [`skill/wsl-zephyr-dev/SKILL.md`](../../../../../skill/wsl-zephyr-dev/SKILL.md) |
| 宿主差异参考（workspace 根） | [`skill/qsh-debug/ref-host-wsl2.md`](../../../../../skill/qsh-debug/ref-host-wsl2.md) |
| 文档补充提案（不改原文） | [`request/skill/README.md`](../../../skill/README.md) |
