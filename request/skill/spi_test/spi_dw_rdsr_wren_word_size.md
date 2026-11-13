# SPI DW RDSR/WREN 字宽缺陷（spi_flash 写失败根因）

- **日期**：2026-10-27
- **状态**：已在本地修复（**未提交**）
- **文件**：`zephyr/drivers/spi/spi_dw.c`
- **函数**：`spi_dw_wip_only()`、`spi_dw_wait_wip_and_wren()`

## 1. 现象

- `spi_test::spi_flash` 写扫描：对齐写 len ≥ 36、非对齐写 len ≥ 28 失败；
  失败点固定为数据 offset **32**（对齐）/ **24**（非对齐），后续字节为 `0xFF`。
- 读扫描 1..4096 全部 PASS；4096 字节写（256 字节整页）PASS。
- 用参考样
  `modules/lib/zephyr_pfr_tpm/samples/spi_master_flash_malloc_read_write_test`
  （0x100000、每次写前擦除、len=36）复现同样失败：`FAIL malloc verify len=36 ret=-5`。
  ⇒ 排除"测试地址/擦除/缓冲区写法"假设，确认是驱动写路径问题。

## 2. 根因

- `spi_nor_multi_dev.c` 通过 `spi_nor_op->transceive` 走 DW SPI 的自定义 NOR 路径。
- `spi_dw_write_multi_addr_align()` 把一次 page program 按 cache line 拆成
  head / mid / tail；tail（第二次 page program）前调用
  `spi_dw_wait_wip_and_wren()`。
- `spi_dw_wip_only()` / `spi_dw_wait_wip_and_wren()` 直接复制调用方的 `config`；
  page program 的 config 是 **32-bit 字宽**。RDSR(`0x05`)/WREN(`0x06`) 是
  **1 字节命令**，用 32-bit 字宽会发 4 字节帧，WREN 未正确生效 →
  第二次 page program 被 Flash 忽略 → tail 保持 `0xFF`。
- 第一次 page program 用的是 `spi_nor_wren()`（8-bit 配置），所以 head/mid 能写；
  只有第二次及以后失败。

## 3. 修复

在 `spi_dw_wip_only()` 和 `spi_dw_wait_wip_and_wren()` 的 `cfg_single` 上强制
`SPI_WORD_SIZE = 8`：

```c
cfg_single.operation &= ~SPI_WORD_SIZE_MASK;
cfg_single.operation |= (8U << SPI_WORD_SIZE_SHIFT);
```

只影响 RDSR/WREN 两个 1 字节命令，不改变数据 phase 的 32-bit 路径。

## 4. 验证

| 验证 | 修复前 | 修复后 |
|---|---|---|
| 参考样 len=36 | `FAIL malloc verify len=36 ret=-5` | 通过，继续到 37/38/39/40 |
| `spi_test` 29 项 | 写扫描 FAIL | **29/29 PASS**，`PROJECT EXECUTION SUCCESSFUL` |

证据：

- `810_driver_test/wsl_environment/wsl_new/logs/spi_flash_all_pass_20261027_2136.log`
- `810_driver_test/wsl_environment/wsl_new/logs/spi_flash_all_pass_final_20261028.log`（最终源码复跑）
- `810_driver_test/wsl_environment/wsl_new/logs/spi_flash_all_pass_28_20261028.log`（新增 malloc 用例后 28/28）
- `810_driver_test/wsl_environment/wsl_new/logs/spi_flash_all_pass_29_20261028.log`（新增 alloc_read_smoke 后 29/29）
- `810_driver_test/wsl_environment/wsl_new/logs/gdb_load_20261028_142920.log`
- `810_driver_test/wsl_environment/wsl_new/logs/gdb_load_20261027_213643.log`
- `810_driver_test/wsl_environment/wsl_new/logs/gdb_load_20261028_100650.log`
- `810_driver_test/wsl_environment/wsl_new/logs/gdb_load_20261028_104625.log`

> **证据缺口说明**：修复前参考样 `FAIL malloc verify len=36 ret=-5` 是 2026-10-27
> 的现场串口观察，当时未单独归档该次日志（串口日志随后被新一轮运行覆盖）；
> 修复后的 24/24 PASS 有归档日志；参考样 len=36 通过为现场观察，未单独归档。
> 若需逐字节证据链，可临时回退本修复、重建参考样并复跑 len=36 归档。

## 5. 注意

- 这是**最小驱动改动**；若上游已有修复，以本文件记录的现象/判据核对后再决定是否保留。
- 修改前已用参考样确认不是测试代码问题；修改后测试与参考样两条路径都通过。
