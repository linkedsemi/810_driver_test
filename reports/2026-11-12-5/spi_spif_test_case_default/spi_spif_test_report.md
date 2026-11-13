# SPI + SPI filter (spi_ls + pfr_tpm) 驱动测试报告

- 工程: spi_spif_test
- case: case_default
- 日期: 2026-11-13 12:37:35
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM31, timeout=120s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (54/54)
- 接线: spi1/spi2 片内引脚组 + 板级 CS GPIO；spif3 master -> spi4 -> flash4_0 为板载；spif loopback_*（6 项）需 spif1(master) ↔ spif3(filter in) 跳线（明细见 projects/spif_test/tests/COVERAGE.md）

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| concurrent_two_threads | ✅ PASS | token | 两线程各 4 轮 × 16 B 全部 `ret==0` | arm=0, ret_a=0, ret_b=0, iter=4 |  |
| configure_datasize16 | ✅ PASS | token | `DS==0xF`；4 字节传输 `ret==0` | ret=0, ds=0xf00 |  |
| configure_freq_boundary | ✅ PASS | token | `9375000 → ret==0, BR==/8`；`9375001 → ret==-EINVAL, SPE==0` | arm=0, ret_ok=0, br_ok=0x10, ret_bad=-22, spe_after_bad=0 |  |
| configure_lsb_first | ✅ PASS | token | LSB → `LSBFIRST==1`；随后 MSB → `==0`；`ret==0` | ret_lsb=0, lsb_bit=128, ret_msb=0, msb_bit=0 |  |
| configure_mode_bits | ✅ PASS | token | `CR1.MSTR==1`、`CPOL==1`、`CPHA==1`、`LSBFIRST==0`、`BR==/16`、`S… | ret=0, cr1=0x11f, cr2=0x40704 |  |
| configure_prescaler_table | ✅ PASS | token | `9375000→/8`、`5000000→/16`、`3000000→/32`、`1500000→/64`、`8000… | f0=9375000/br=0x10, f1=5000000/br=0x18, f2=3000000/br=0x20, f3=1500000/br=0x28, f4=800000/br=0x30, f5=400000/br=0x38, f6=292968/br=0x38 |  |
| device_ready | ✅ PASS | token | 两实例 ready；基址 == `0x4008a000`/`0x4008a400`；`cs-gpios` 长度各 1；C… | spi1_base=0x4008a000, spi2_base=0x4008a400, cs1_pin=3, cs2_pin=15 |  |
| error_half_duplex | ✅ PASS | token | `ret == -ENOTSUP` | arm=0, ret=-134 |  |
| error_ti_mode | ✅ PASS | token | `ret == -ENOTSUP` | arm=0, ret=-134 |  |
| hold_on_cs_spe | ✅ PASS | token | HOLD 后 `SPE==1`；普通传输后 `SPE==0` | ret_hold=0, spe_hold=64, cs_hold=1, ret_norm=0, spe_norm=0 |  |
| lock_on_hold_release | ✅ PASS | token | LOCK_ON 连发两次 `ret==0`；`spi_release()==0` | ret1=0, ret2=0, ret_rel=0 |  |
| post_reset_state | ✅ PASS | token | 复位后 `SPE==0`、`MSTR==0`、`BR==/8`；恢复传输 `ret==0` 且 `BR==/32` | ret_rst=0, cr1_after=0x10, ret_recover=0, br_after=0x20 |  |
| reconfigure_same_cfg_prescaler | ✅ PASS | token | 首次 400 kHz → `BR==/256`；同指针二次 → `BR==/8` | ret1=0, br_first=0x38, ret2=0, br_second=0x10, spe=0 |  |
| release_semantics | ✅ PASS | token | 未配置 → `-EINVAL`；已配置且空闲 → `0` 且 `SPE==0` | ret_unused=-22, ret_tx=0, ret_release=0, spe=0 |  |
| transceive_duplex_pair | ✅ PASS | token | 32 B 全双工 `ret==0`；RX 内容只上报不断言（MISO 悬空） | ret=0, rx0=ff, rx1=ff, rx31=ff |  |
| transceive_length_sweep | ✅ PASS | token | 15 档长度全部 `ret==0` 且 `SPE==0` | n=15, min_len=1, max_len=256, last_ret=0 |  |
| transceive_null_bufs | ✅ PASS | token | `transceive(NULL,NULL)==0`；随后 `spi_release()==-EINVAL` | ret_null=0, ret_release=-22 |  |
| transceive_tx_only_rx_only | ✅ PASS | token | tx-only `ret==0`；rx-only `ret==0` | ret_tx=0, ret_rx=0 |  |
| alloc_read_smoke | ✅ PASS | token | `ret=0` | len=4096, ret=0 |  |
| erase_all_ff | ✅ PASS | token | 擦除后回读全 0xFF | ff_bytes=4096, total=4096 |  |
| flash_ready | ✅ PASS | token | JEDEC 非全 0/全 FF；容量覆盖测试区 | jedec=204021, size=134217728, erase=ff |  |
| read_aligned_len_sweep | ✅ PASS | token | 每档 `ret=0` 且逐字节等于 pattern | min=1, max=4096, fail=0, first=0 |  |
| read_malloc_aligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=4096, fail=0, first=0 |  |
| read_malloc_unaligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=4096, fail=0, first=0 |  |
| read_unaligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=4096, fail=0, first=0 |  |
| write_aligned_len_sweep | ✅ PASS | token | 每档 `ret=0` 且逐字节相等 | min=1, max=256, fail=0, first=0, stage=0, ret=0, dlen=0, idx=-1, exp=00, act=00 |  |
| write_malloc_aligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=256, fail=0, first=0, stage=0, ret=0, dlen=0, idx=-1, exp=00, act=00 |  |
| write_malloc_unaligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=256, fail=0, first=0, stage=0, ret=0, dlen=0, idx=-1, exp=00, act=00 |  |
| write_unaligned_len_sweep | ✅ PASS | token | 同上 | min=1, max=256, fail=0, first=0, stage=0, ret=0, dlen=0, idx=-1, exp=00, act=00 |  |
| addr_mode_config | ✅ PASS | token | `ADDR_3B_4B_SEL` 1/0 | sel_4b=1, sel_3b=0 |  |
| addr_privilege_guard | ✅ PASS | token | `addr==MB(256)`/`>MB(256)`/`len==0` → `-EINVAL`；`MB(256)-KB(… | guard_boundary=0x10000000, len_min=16384 |  |
| addr_privilege_valid_region | ✅ PASS | token | `MB(1)..+KB(16)` 的 READ/WRITE 位 1→0→1 | reg_off=2, len=16384 |  |
| addr_whitelist_fill | ✅ PASS | token | 0 → 全 0；1 → 全 `0xffffffff`；非法 num 不改寄存器 | reg_nun=512, reg_last=511 |  |
| bus_pairing | ✅ PASS | token | spif1/2→spi3，spif3/4→spi4；CS 引脚逐一匹配 | - |  |
| cmd_idx_accessors | ✅ PASS | token | CMD 与 DUMMY 按 idx 往返（DUMMY 用槽 6 FAST_READ）；5 bit 截断 `0xff→0x… | slot5_cmd=0x77, slot5_dummy=0, slot6_raw=0x90b, slot6_dummy=0x9 |  |
| cmd_table_add_remove | ✅ PASS | token | add/remove/-EINVAL；固定表段 `CMD==0x03` 且 `EN==1` | - |  |
| cmd_table_full_boundary | ✅ PASS | token | 前 40 个 0；第 41 个 `-ENOSR` | added=40, capacity=40 |  |
| cold_reset_state | ✅ PASS | token | `CFG.EN==0`、`OPERATION_MODE==0`、`ADDR_3B_4B_SEL==0`、`TARGET_… | cfg=0x40, target_addr=0xffffffff, bcmd=0x7 |  |
| filter_enable_bit | ✅ PASS | token | true → `EN==1`；false → `EN==0` | - |  |
| flash_size_boundary | ✅ PASS | token | `0`/`3`/`MB(256)+1` → `-EINVAL`；`MB(256)`/`KB(64)` → 0 且回读一致 | max=0x10000000, min_ok=0x10000 |  |
| loopback_filter_bad_cmd_bad_addr | ✅ PASS | token | `CMD_ERR==1`、`ADDR_ERR==0`、`ERROR_CMD==0x03` | before=0, ret=0, raw=0x3000002, cmd_err=1, addr_err=0, por_addr=0, error_cmd=0x3, error_addr=0 |  |
| loopback_filter_bad_cmd_legal_addr | ✅ PASS | token | `CMD_ERR==1`、`ADDR_ERR==0`、`ERROR_CMD==0x03` | before=0, ret=0, raw=0x3000002, cmd_err=1, addr_err=0, por_addr=0, error_cmd=0x3, error_addr=0 |  |
| loopback_filter_legal_both | ✅ PASS | token | `CMD_ERR==0`、`ADDR_ERR==0` | before=0, ret=0, raw=0, cmd_err=0, addr_err=0, por_addr=0, error_cmd=0, error_addr=0 |  |
| loopback_filter_legal_cmd_bad_addr | ✅ PASS | token | `CMD_ERR==0`、`ADDR_ERR==1`、`POR_ADDR==1`、`ERROR_ADDR==0x4000… | before=0, ret=0, raw=0x3000045, cmd_err=0, addr_err=1, por_addr=1, error_cmd=0x3, error_addr=0x4000 |  |
| loopback_jedec | ✅ PASS | token | `ret=0`；JEDEC 非全 0/全 FF | ret=0, jedec=204021 |  |
| loopback_read_addr | ✅ PASS | token | `ret=0`；数据上报 | ret=0, data=ffffffff |  |
| master_read_jedec_id | ✅ PASS | token | 事务 `ret=0`；JEDEC 经 `ZT_INFO` 上报 | jedec=000000 |  |
| master_read_with_addr | ✅ PASS | token | 事务 `ret=0`；读回数据经 `ZT_INFO` 上报 | read=ffffffff |  |
| mode_switch_guard | ✅ PASS | token | filter+no → `-EINVAL`；query-only state → `-EINVAL`；非法 mode →… | filter_no=-EINVAL, master_busy=-EBUSY, after_release=ok |  |
| mode_switch_states | ✅ PASS | token | filter+single：`EN=1,OPERATION_MODE=1,state=single`；filter+mu… | filter_single=0, filter_multi=1, monitor_no=2, master=3 |  |
| passthrough_mode_switch | ✅ PASS | token | cold_reset 后 `single`；enable 后 `multi`；切回 filter 后 `single` | after_cold_reset state=0 |  |
| reg_lock_unlock | ✅ PASS | token | 与 API 名反相的寄存器语义 | unlock_ip_lock=1, lock_ip_lock=0 |  |
| sck_threshold_config | ✅ PASS | token | `SCK_DIV==5`；`HI==2`；`LO==100`；`SCK_CHECK` 1→0 | sck_div=5, hi=2, lo=100 |  |
| spif_device_ready | ✅ PASS | token | 4 实例 ready；`ctrl_idx == 0..3`；配对 spi ready；cs gpio 端口 ready | - |  |

## 汇总

OVERALL: PASS
