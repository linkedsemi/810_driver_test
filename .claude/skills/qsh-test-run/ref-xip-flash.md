# XIP 固件固化与启动测试（flash 烧录）

把固件烧进板载 SPI flash 后从 flash 启动测试（CLAUDE.md §15）。与 RAM load
互补：持久化、断电不丢，适合 XIP 启动类测试/固化镜像。

## 烧录命令

```bash
# 单次烧录（读/写/校验都在 scripts/flash_program.py）
python 810_driver_test/scripts/flash_program.py --bin zephyr_flash_xxx.bin@0x08000000 --verify
# 双核 ★必须一次会话（全片擦会擦掉另一核，CLAUDE.md §10.14）
python 810_driver_test/scripts/flash_program.py --bin cpu1.bin@0x08000000 --bin cpu2.bin@0x08100000 --verify
# 读回 + 与源比对
python 810_driver_test/scripts/flash_program.py --dump-out rb.bin --dump-addr 0x08000000 --dump-len 0x1000 --compare cpu1.bin

# 烧录→启动→采集一键（token 前缀默认 I3C_TEST，按固件用 --prefix 指定）
python tests/scripts/dbg_run.py --xip --bins cpu1.bin@0x08000000,cpu2.bin@0x08100000 --timeout 15
python tests/scripts/dbg_run.py --xip --bins hello.bin@0x08000000 --timeout 10 --prefix HELLO
```

## 要点（实测，CLAUDE.md §15/§10）

- **默认测试（单核/双核）= 全 RAM**（现有流程，不烧 flash）
- **XIP 标准组合 = cpu1 XIP + cpu2 RAM**（★ 默认 XIP 场景）：
  - cpu1 用 `lsqsh_evb@2os_xip/lsqsh/cpu1` 构建（XIP 执行 + boot 引导）→ 烧 `0x08000000`
  - cpu2 用**标准 2os** `lsqsh_evb@2os/lsqsh/cpu2` 构建（RAM 固件）→ 产物
    `zephyr_flash_*.bin` 也烧进 flash `0x08100000`，由 cpu1 XIP boot 搬运启动
  - 两个 bin **一次会话**烧录（全片擦会擦掉另一核）；`--reset-and-run` 复位启动
- **CKLink 互斥**：烧 flash 时 DebugServer 必须退出（flash_program.py 检测并警告，
  `--kill-debugserver` 强制杀）；烧完要 GDB 再启 DebugServer。**无两遍烧录法、无 GDB load**
- 地址约定：cpu1@0x08000000、cpu2@0x08100000（**地址在 test_common.sh 环境变量
  `CPU1_XIP_ADDR`/`CPU2_XIP_ADDR` 集中掌握，test.sh 引用，不散落硬编码**）
- 启动方式：`--reset-and-run`（烧完自动复位从 flash 启动）；无效时
  `--no-reset-run --gdb-reset`（烧完后 DebugServer 已空闲，reset 启动）
- 结果判定：`FP_OK: <name>@<addr>` + 退出码 0；失败看 `Bins\flash-programer.log` 尾部
- 环境变量：`FP_EXE`/`FP_ALGO`/`FP_LOG` 可覆盖工具路径；**默认已用工程内
  打包副本**（`tools/flash_program/`，自包含），外部 `C:\user_file\...` 仅回退
