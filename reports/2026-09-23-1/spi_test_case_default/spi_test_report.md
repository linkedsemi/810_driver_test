# SPI master (spi_ls) 驱动测试报告

- 工程: spi_test
- case: case_default
- 日期: 2026-09-22 22:50:54
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=30s
- 执行标记: FAILED
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: FAIL (18/19)
- 接线: 无（spi1/spi2 片内引脚组 + 板级 CS GPIO；总线上无从设备，判据来自驱动源码/寄存器）

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
| hold_on_cs_spe | ✅ PASS | token | HOLD 传输后 `SPE==1`；随后普通传输 → `SPE==0`；`ret` 均 0；`cs_hold` 经 `Z… | ret_hold=0, spe_hold=64, cs_hold=0, ret_norm=0, spe_norm=0 |  |
| lock_on_hold_release | ✅ PASS | token | LOCK_ON 连发两次 `ret` 均 0（第二次走 lock 短路）；`spi_release` → 0 | ret1=0, ret2=0, ret_rel=0 |  |
| post_reset_state | ✅ PASS | token | `reset_line_toggle_dt()` == 0；复位后 `CR1.SPE==0`、`MSTR==0`、**`… | ret_rst=0, cr1_after=0x10, ret_recover=0, br_after=0x20 |  |
| reconfigure_same_cfg_prescaler | ✅ PASS | token | 首次 400 kHz → `BR==/256`；同一 `spi_config` 指针二次调用 → `ret==0` 但… | ret1=0, br_first=0x38, ret2=0, br_second=0x10, spe=0 |  |
| release_semantics | ✅ PASS | token | 未配置 cfg → `-EINVAL`；已配置 → `0` 且 `SPE==0` | ret_unused=-22, ret_tx=0, ret_release=0, spe=0 |  |
| transceive_duplex_pair | ✅ PASS | token | 32 B 全双工 `ret==0`；RX 字节实测经 `ZT_INFO` 上报（**不断言内容**，MISO 悬空） | ret=0, rx0=ff, rx1=ff, rx31=ff |  |
| transceive_length_sweep | ✅ PASS | token | len ∈ {1,2,3,7,8,15,16,31,32,63,64,127,128,255,256} 每次 `ret=… | n=15, min_len=1, max_len=256, last_ret=0 |  |
| transceive_null_bufs | ✅ PASS | token | `spi_transceive(dev,cfg,NULL,NULL) == 0`；随后 `spi_release(dev… | ret_null=0, ret_release=-22 |  |
| transceive_tx_only_rx_only | ✅ PASS | token | tx-only（`rx_bufs=NULL`）`ret==0`；rx-only（`tx_bufs=NULL`，`tx_f… | ret_tx=0, ret_rx=0 |  |
| link_probe | ❌ FAIL | token | 两设备 `device_is_ready` 真；8 B 双向逐字节相等；`master_ret==0`；`slave_r… | master_ret=0, slave_ret=-119, state=0 | WEST_TOPDIR/810_driver_test/projects/spi_test/cpu1_spi_test/src/spi_loopback.c: 454: spi_loopback_link_probe: (lb_link_state not equal to LB_LINK_OK) |
| repeat_roundtrips_state_isolation | ⏭️ SKIP | token | 8 轮连续回环全部双向逐字节相等（长度在 {1,2,3,8,17,64} 间循环） | - | 前置外部条件不满足（SPI1<->SPI3 回环跳线未接）；不计入分母 |
| roundtrip_fifo_boundary | ⏭️ SKIP | token | len ∈ {3,4,5,31,32,33} 双向逐字节相等、`slave_ret==len` | - | 前置外部条件不满足（SPI1<->SPI3 回环跳线未接）；不计入分母 |
| roundtrip_length_sweep | ⏭️ SKIP | token | len ∈ {1,2,3,4,5,8,16,32,63,64,127,128,255,256} 每档双向逐字节相等、`s… | - | 前置外部条件不满足（SPI1<->SPI3 回环跳线未接）；不计入分母 |
| roundtrip_master_tx_slave_rx | ⏭️ SKIP | token | 24 B：`slave_rx[] == master_tx[]`；`master_ret==0`；`slave_ret=… | - | 前置外部条件不满足（SPI1<->SPI3 回环跳线未接）；不计入分母 |
| roundtrip_max_length_256 | ⏭️ SKIP | token | 256 B 双向逐字节相等；`slave_ret==256` | - | 前置外部条件不满足（SPI1<->SPI3 回环跳线未接）；不计入分母 |
| roundtrip_pattern_matrix | ⏭️ SKIP | token | 6 组 seed（含 0x00/0xFF/0xAA/0x55），16 B 双向逐字节相等 | - | 前置外部条件不满足（SPI1<->SPI3 回环跳线未接）；不计入分母 |
| slave_ret_recv_frames | ⏭️ SKIP | token | len ∈ {1,7,64} 时 `slave_ret` 分别 == 1/7/64 | - | 前置外部条件不满足（SPI1<->SPI3 回环跳线未接）；不计入分母 |

## 汇总

OVERALL: FAIL
