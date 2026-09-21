# I2C filter (PFR/TMP) 驱动测试报告

- 工程: i2cf_test
- case: case_default
- 日期: 2026-09-21 13:17:08
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=30s
- 执行标记: FAILED
- 内存兜底: 有效
- 结果: FAIL (14/18)
- 接线: 外接 EEPROM@0x50 于 PE02/PB00（I2C4<->I2CF4 同管脚复用）；其余项零接线

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| addr_7bit_field | ✅ PASS | token | addr `0x7f` 原样回读；addr `0x80` 回读被截断为 `0x00` | max_ok=0x7f, ovf_readback=0 |  |
| callback_register | ✅ PASS | token | **读回 `dev->data` 的 `cb` / `user_data` 字段**（按驱动 `struct linke… | - |  |
| cold_reset_state | ✅ PASS | token | `SMBF_REG_ENABLE==0x1`；`INTR_MSK==0x3`；`SCL_HOLD_TIME == DT… | reg_enable=0x1, intr_msk=0x3, scl_hold=0xfa |  |
| concurrent_fill | ✅ PASS | token | 双线程各管 8 通道 × 4 轮 fill+dump，全部 addr/bitmap 一致（无 INDEX 串扰） | threads=2, rounds=4, channels=16 |  |
| device_ready | ✅ PASS | token | 4 实例 `device_is_ready` 均真；绑定 i2c 设备非 NULL 且 ready；i2cf4 绑定 i… | - |  |
| duplicate_addr_rejected | ✅ PASS | token | ch0 填 `0x50` 后 ch1 同址 → `-EINVAL`；关掉 ch0 后同址 → `0`；同址填回自身 →… | - |  |
| en_clears_table | ✅ PASS | token | 16 通道填非零后 `_en(_,_,_,true)` → 全部 16×8 字回读为 0 | - |  |
| enable_channel_boundary | ✅ PASS | token | idx 15 → 0 且 `SMBF_ADDRESS_ENABLE[15]` 置位；idx **16** / **255… | - |  |
| fill_dump_roundtrip | ✅ PASS | token | 16 通道逐个：dump addr == fill addr；32 B bitmap `memcmp == 0`；填满后… | - |  |
| filter_blocks_nonwhitelist | ❌ FAIL | token | 上游访问 `slave`（白名单 = `authorized`）→ 负值；filter off 后同址 → 0。**需下… | - | WEST_TOPDIR/810_driver_test/projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c: 876: i2cf_filter_blocks_nonwhitelist: (ret_blocked < 0 is false) |
| filter_passthrough_whitelist | ❌ FAIL | token | 上游经滤波器读回 == 预先经旁路写入的 pattern（逐字节 `zassert_mem_equal`）；偏移 `0x… | - | WEST_TOPDIR/810_driver_test/projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c: 962: i2cf_filter_passthrough_whitelist: (readback not equal to pattern) |
| filter_set_bits | ✅ PASS | token | 4 种入参组合的 `SMBUS_FILTER_SET` 位回读符合负逻辑表；`MASTER_WRITE_MODE` 恒… | - |  |
| invalid_args | ✅ PASS | token | 四种非法入参均 `-EINVAL` | - |  |
| isr_addr_beyond_whitelist | ❌ FAIL | token | 上游 master 访问未授权地址 → ① 回调被调用（`isr_count>=1`）② `SMBF_NONWHITEL… | - | WEST_TOPDIR/810_driver_test/projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c: 806: i2cf_isr_addr_beyond_whitelist: (i2cf_isr_wait(100) is false) |
| master_eeprom_rw | ❌ FAIL | token | 从设备写入 pattern 后回读逐字节相等；偏移 `0x00..0x0f` 备份/恢复。**需下游从设备（旁路通路）；… | - | WEST_TOPDIR/810_driver_test/projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c: 626: i2cf_master_eeprom_rw: (i2c_burst_read(master, slave, 0x00, readback, sizeof(readback)) is non-zero) |
| scl_hold_time_roundtrip | ✅ PASS | token | `0x0000/0x0001/0x00ff/0xffff/0x1234` 均原样回读（16 位字段） | - |  |
| slave_addr_scan | ✅ PASS | token | ⚠ **观测项，无判别力**（2026-09-18 F1 波如实标注）：唯一真判据 = 旁路 master 就绪；应答清… | range=08..77, count=1, addrs=50 |  |
| switch_master_filter | ✅ PASS | token | 两组切换及重复同向切换均返回 0；**且 PE02（`i2c4.SCL` 与 `i2cf4.SCLO` 的共同物理 pa… | PE02_func_i2c4=0, PE02_func_i2cf4=1 |  |

## 汇总

OVERALL: FAIL
