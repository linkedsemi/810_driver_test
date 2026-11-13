# spi_test 本轮结果（2026-10-28）

## 测试项（29 项）

### 套件 `spi`（18 项，片内，无外部接线）

`concurrent_two_threads`、`configure_datasize16`、`configure_freq_boundary`、
`configure_lsb_first`、`configure_mode_bits`、`configure_prescaler_table`、
`device_ready`、`error_half_duplex`、`error_ti_mode`、`hold_on_cs_spe`、
`lock_on_hold_release`、`post_reset_state`、`reconfigure_same_cfg_prescaler`、
`release_semantics`、`transceive_duplex_pair`、`transceive_length_sweep`、
`transceive_null_bufs`、`transceive_tx_only_rx_only`。

### 套件 `spi_flash`（11 项，spif3 master → spi4 → flash4_0，板载）

| 测试项 | 内容 | 结果 |
|---|---|---|
| `alloc_read_smoke` | `k_malloc(4096)` 只读 4096 B，只判 ret=0 | PASS |
| `erase_all_ff` | 擦除 4 KiB 后回读全 0xFF | PASS |
| `flash_ready` | JEDEC ID / 容量 / 参数（`ef4021`, 128 MiB） | PASS |
| `read_aligned_len_sweep` | 静态 32 B 对齐缓冲读 1..4096，逐字节比对 | PASS |
| `read_malloc_aligned_len_sweep` | `k_aligned_alloc(32,len)` 读 1..4096，逐字节比对 | PASS |
| `read_malloc_unaligned_len_sweep` | `k_malloc(len)` 读 1..4096，逐字节比对 | PASS |
| `read_unaligned_len_sweep` | 静态非对齐缓冲（+8）读 1..4096，逐字节比对 | PASS |
| `write_aligned_len_sweep` | 静态 32 B 对齐缓冲写 1..256，擦写读比对 | PASS |
| `write_malloc_aligned_len_sweep` | `k_aligned_alloc(32,len)` 写 1..256，擦写读比对 | PASS |
| `write_malloc_unaligned_len_sweep` | `k_malloc(len)` 写 1..256，擦写读比对 | PASS |
| `write_unaligned_len_sweep` | 静态非对齐缓冲写 1..256，擦写读比对 | PASS |

## 结果

**29/29 PASS，`PROJECT EXECUTION SUCCESSFUL`。**

证据：`810_driver_test/wsl_environment/wsl_new/logs/spi_flash_all_pass_29_20261028.log`
（29 PASS / 0 FAIL / 0 SKIP，两套件 succeeded）。

## 根因

写扫描失败由 DW SPI 的 RDSR/WREN 字宽引起：`spi_dw_wip_only()` /
`spi_dw_wait_wip_and_wren()` 复制了 page program 的 32-bit 配置去发 1 字节的
RDSR/WREN，第二次 page program 的 WREN 未生效，tail 数据未写入。
详见 [`spi_dw_rdsr_wren_word_size.md`](spi_dw_rdsr_wren_word_size.md)。

## 回环用例

`spi_loopback` 8 项已删除，设计/接线/失败现象归档在
[`spif_loopback_decision.md`](spif_loopback_decision.md)（附录），供迁移到
`spif_test` 使用。
