# SPI (spi_ls + spif3 flash) 驱动测试报告

- 工程: spi_test
- case: case_default
- 日期: 2026-10-29 18:29:45
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=auto, timeout=60.0s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (29/29)
- 接线: 套件 `spi`（18 项）无外部接线 —— spi1/spi2 片内引脚组 + 板级 CS GPIO；套件 `spi_flash`（10 项）无外部接线 —— 板载 spif3 master -> spi4 -> flash4_0

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| concurrent_two_threads | ✅ PASS | token | 两线程各 4 轮 × 16 B 全部 `ret==0`（无 `-ETIMEDOUT`、无交错破坏） | arm=0, ret_a=0, ret_b=0, iter=4 |  |
| configure_datasize16 | ✅ PASS | token | `SPI_WORD_SET(16)` → `CR2.DS==0xF`；4 字节（2 帧）传输 `ret==0` | ret=0, ds=0xf00 |  |
| configure_freq_boundary | ✅ PASS | token | `frequency == 9375000` → `ret==0` 且 `BR==/8`；`== 9375001` →… | arm=0, ret_ok=0, br_ok=0x10, ret_bad=-22, spe_after_bad=0 |  |
| configure_lsb_first | ✅ PASS | token | `SPI_TRANSFER_LSB` → `CR1.LSBFIRST==1`；随后 MSB 配置 → `==0`；两次… | ret_lsb=0, lsb_bit=128, ret_msb=0, msb_bit=0 |  |
| configure_mode_bits | ✅ PASS | token | 一次合法传输后回读：`CR1.MSTR==1`、`CPOL==1`、`CPHA==1`、`LSBFIRST==0`、`B… | ret=0, cr1=0x11f, cr2=0x40704 |  |
| configure_prescaler_table | ✅ PASS | token | 取 `clock>>(i+3) <= frequency` 的最小 `i`：`9375000→/8`、`5000000→… | f0=9375000/br=0x10, f1=5000000/br=0x18, f2=3000000/br=0x20, f3=1500000/br=0x28, f4=800000/br=0x30, f5=400000/br=0x38, f6=292968/br=0x38 |  |
| device_ready | ✅ PASS | token | 两实例 `device_is_ready` 真；reg 基址 == `0x4008a000`/`0x4008a400`；… | spi1_base=0x4008a000, spi2_base=0x4008a400, cs1_pin=3, cs2_pin=15 |  |
| error_half_duplex | ✅ PASS | token | `ret == -ENOTSUP` | arm=0, ret=-134 |  |
| error_ti_mode | ✅ PASS | token | `ret == -ENOTSUP` | arm=0, ret=-134 |  |
| hold_on_cs_spe | ✅ PASS | token | HOLD 传输后 `SPE==1`；随后普通传输 → `SPE==0`；`ret` 均 0；`cs_hold` 经 `Z… | ret_hold=0, spe_hold=64, cs_hold=1, ret_norm=0, spe_norm=0 |  |
| lock_on_hold_release | ✅ PASS | token | LOCK_ON 连发两次 `ret` 均 0（第二次走 lock 短路）；`spi_release` → 0 | ret1=0, ret2=0, ret_rel=0 |  |
| post_reset_state | ✅ PASS | token | `reset_line_toggle_dt()` == 0；复位后 `CR1.SPE==0`、`MSTR==0`、**`… | ret_rst=0, cr1_after=0x10, ret_recover=0, br_after=0x20 |  |
| reconfigure_same_cfg_prescaler | ✅ PASS | token | 首次 400 kHz → `BR==/256`；同一 `spi_config` 指针二次调用 → `ret==0` 但… | ret1=0, br_first=0x38, ret2=0, br_second=0x10, spe=0 |  |
| release_semantics | ✅ PASS | token | 未配置 cfg → `-EINVAL`；已配置 → `0` 且 `SPE==0` | ret_unused=-22, ret_tx=0, ret_release=0, spe=0 |  |
| transceive_duplex_pair | ✅ PASS | token | 32 B 全双工 `ret==0`；RX 字节实测经 `ZT_INFO` 上报（**不断言内容**，MISO 悬空） | ret=0, rx0=ff, rx1=ff, rx31=ff |  |
| transceive_length_sweep | ✅ PASS | token | len ∈ {1,2,3,7,8,15,16,31,32,63,64,127,128,255,256} 每次 `ret=… | n=15, min_len=1, max_len=256, last_ret=0 |  |
| transceive_null_bufs | ✅ PASS | token | `spi_transceive(dev,cfg,NULL,NULL) == 0`；随后 `spi_release(dev… | ret_null=0, ret_release=-22 |  |
| transceive_tx_only_rx_only | ✅ PASS | token | tx-only（`rx_bufs=NULL`）`ret==0`；rx-only（`tx_bufs=NULL`，`tx_f… | ret_tx=0, ret_rx=0 |  |
| alloc_read_smoke | ✅ PASS | token | ret=0（不比对内容） | len=4096, ret=0 |  |
| erase_all_ff | ✅ PASS | token | 擦除后回读全 0xFF | ff_bytes=4096, total=4096 |  |
| flash_ready | ✅ PASS | token | JEDEC 非 0/FF；容量覆盖测试区 | jedec=204019, size=33554432, erase=ff |  |
| read_aligned_len_sweep | ✅ PASS | token | 每档 ret=0 且逐字节等于写入 pattern | min=1, max=4096, fail=0, first=0 |  |
| read_malloc_aligned_len_sweep | ✅ PASS | token | 每档 ret=0 且逐字节相等 | min=1, max=4096, fail=0, first=0 |  |
| read_malloc_unaligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=4096, fail=0, first=0 |  |
| read_unaligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=4096, fail=0, first=0 |  |
| write_aligned_len_sweep | ✅ PASS | token | 每档 ret=0 且逐字节相等 | min=1, max=256, fail=0, first=0, stage=0, ret=0, dlen=0, idx=-1, exp=00, act=00 |  |
| write_malloc_aligned_len_sweep | ✅ PASS | token | 每档 ret=0 且逐字节相等 | min=1, max=256, fail=0, first=0, stage=0, ret=0, dlen=0, idx=-1, exp=00, act=00 |  |
| write_malloc_unaligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=256, fail=0, first=0, stage=0, ret=0, dlen=0, idx=-1, exp=00, act=00 |  |
| write_unaligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=256, fail=0, first=0, stage=0, ret=0, dlen=0, idx=-1, exp=00, act=00 |  |

## 汇总

OVERALL: PASS
