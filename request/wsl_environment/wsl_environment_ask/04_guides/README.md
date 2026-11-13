# 使用指南索引

| 指南 | 内容 |
|---|---|
| [`build.md`](build.md) | WSL 内构建 Zephyr（cpu1/cpu2，产物隔离） |
| [`debug.md`](debug.md) | DebugServer 状态检查、GDB 下载/运行、日志与退出码 |
| [`serial.md`](serial.md) | Windows 串口枚举/采集、WSL 读取/等待、占用处理 |

配置唯一来源：[`../../../../wsl_environment/wsl_new/config/config.json`](../../../../wsl_environment/wsl_new/config/config.json)。

所有命令默认在 `810_driver_test/wsl_environment/wsl_new/` 下执行；`west build` 在
`ls1010_port_spi_i2s/` 工作区根执行。
