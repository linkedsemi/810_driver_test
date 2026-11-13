# 构建指南（WSL2）

## 前置

- 工具链与环境变量由 `~/.bashrc` 提供：`ZEPHYR_TOOLCHAIN_VARIANT=cross-compile`、`CROSS_COMPILE=...V3.0.1/bin/riscv64-unknown-elf-`
- 工作区根：`/home/eunice/zephyr_workspace/ls1010_port_spi_i2s/`（含 `.west/` 与 `zephyr/`）
- 机器相关路径：`wsl_new/config/config.json`（board / zephyr_base / 工具链）

## 构建 cpu1 hello_world（验证用）

```bash
cd /home/eunice/zephyr_workspace/ls1010_port_spi_i2s
west build -p always -b 'lsqsh_evb@2os/lsqsh/cpu1' \
  -d 810_driver_test/wsl_environment/wsl_new/build/hello_world \
  zephyr/samples/hello_world
```

产物：

- ELF：`810_driver_test/wsl_environment/wsl_new/build/hello_world/zephyr/zephyr.elf`
- entry：`0x100c0000`（`riscv64-unknown-elf-readelf -h <elf>`）
- 构建目录被 `build/.gitignore` 忽略，不会污染仓库

**禁止**覆盖用户现有构建：

- `/home/eunice/zephyr_workspace/ls1010_port_spi_i2s/build`
- `zephyr/samples/hello_world/build`

## 构建 cpu2（对照/扩展）

```bash
west build -p always -b 'lsqsh_evb@2os/lsqsh/cpu2' \
  -d 810_driver_test/wsl_environment/wsl_new/build/hello_world_cpu2 \
  zephyr/samples/hello_world
```

cpu2 ELF entry 为 `0x10000000`，GDB 端口 `1026`（见 [`debug.md`](debug.md)）。

## 验证构建结果

```bash
cd 810_driver_test/wsl_environment/wsl_new
python3 scripts/env_check.py
```

`build.elf` 与 `build.board` 两项应 PASS；`build.board` 从 `zephyr/.config` 的
`CONFIG_BOARD_TARGET` 读取，与 `config.json` 的 `board` 对比。

## 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| `west: command not found` | PATH 未包含 `~/.local/bin` | 登录 shell 重新加载 `~/.bashrc` |
| 找不到 `riscv64-unknown-elf-gcc` | `CROSS_COMPILE` 未生效 | 检查 `~/.bashrc` 的 `CROSS_COMPILE` 与 `ZEPHYR_TOOLCHAIN_VARIANT` |
| 构建成功但 load 后无串口输出 | 端口/console 映射问题，不是构建问题 | 见 [`serial.md`](serial.md) |
