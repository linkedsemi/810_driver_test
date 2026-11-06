# I2C filter (PFR/TMP) 驱动测试报告

- 工程: i2cf_test
- case: case_enum_matrix
- 日期: 2026-11-05 23:10:58
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=30s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (19/19)
- 接线: **零外部器件** —— 从设备 = 片内 target `eeprom@33`（挂 i2c3）；上游主机 i2c2 跳线到 i2cf4 输入、i2c3 跳线到 i2cf4 输出

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| addr_7bit_field | ✅ PASS | token | addr `0x7f` 原样回读；addr `0x80` 回读被截断为 `0x00` | max_ok=0x7f, ovf_readback=0 |  |
| callback_register | ✅ PASS | token | **读回 `dev->data` 的 `cb` / `user_data` 字段**（按驱动 `struct linke… | - |  |
| cold_reset_state | ✅ PASS | token | `SMBF_REG_ENABLE==0x1`；`INTR_MSK==0x3`；`SCL_HOLD_TIME == DT… | reg_enable=0x1, intr_msk=0x3, scl_hold=0x155 |  |
| concurrent_fill | ✅ PASS | token | 双线程各管 8 通道 × 4 轮 fill+dump，全部 addr/bitmap 一致（无 INDEX 串扰） | threads=2, rounds=4, channels=16 |  |
| device_ready | ✅ PASS | token | 4 实例 `device_is_ready` 均真；绑定 i2c 设备非 NULL 且 ready；i2cf4 绑定 i… | - |  |
| duplicate_addr_rejected | ✅ PASS | token | ch0 填 `0x50` 后 ch1 同址 → `-EINVAL`；关掉 ch0 后同址 → `0`；同址填回自身 →… | - |  |
| en_clears_table | ✅ PASS | token | 16 通道填非零后 `_en(_,_,_,true)` → 全部 16×8 字回读为 0 | - |  |
| enable_channel_boundary | ✅ PASS | token | idx 15 → 0 且 `SMBF_ADDRESS_ENABLE[15]` 置位；idx **16** / **255… | - |  |
| enum_matrix | ✅ PASS | token | 9 格 × 读/写 各自 ret / ISR 增量合计 / `SMBF_NONWHITELIST` 锁存：C1(内/有)… | cells=9, dirs=2, repeat=3 |  |
| fill_dump_roundtrip | ✅ PASS | token | 16 通道逐个：dump addr == fill addr；32 B bitmap `memcmp == 0`；填满后… | - |  |
| filter_blocks_nonwhitelist | ✅ PASS | token | 上游访问 `slave`（白名单只授权 `authorized`）→ 负值；**单变量对照**：**只把 `slave`… | whitelist=0x2a, target=0x33, blocked_ret=-5, authorized_ret=0 |  |
| filter_passthrough_whitelist | ✅ PASS | token | 上游经滤波器读回 == 预先经旁路写入的 pattern（逐字节 `zassert_mem_equal`）；偏移 `0x… | addr=0x33, len=8, rd=5a5b5c5d |  |
| filter_set_bits | ✅ PASS | token | 4 种入参组合的 `SMBUS_FILTER_SET` 位回读符合负逻辑表；`MASTER_WRITE_MODE` 恒… | - |  |
| invalid_args | ✅ PASS | token | 四种非法入参均 `-EINVAL` | - |  |
| isr_addr_beyond_whitelist | ✅ PASS | token | 上游 master 访问未授权地址 → ① 回调被调用（`isr_count>=1`）② `SMBF_NONWHITEL… | target=0x33, blocked_ret=-5, isr_count=1, nw=0x3300, nw_addr=0x33, nw_cmd=0 |  |
| master_eeprom_rw | ✅ PASS | token | 从设备写入 pattern 后回读逐字节相等；偏移 `0x00..0x0f` 备份/恢复。**需下游从设备（旁路通路）；… | addr=0x33, len=16 |  |
| scl_hold_time_roundtrip | ✅ PASS | token | `0x0000/0x0001/0x00ff/0xffff/0x1234` 均原样回读（16 位字段） | - |  |
| slave_addr_scan | ✅ PASS | token | ⚠ **观测项，无判别力**（2026-09-18 F1 波如实标注）：唯一真判据 = 旁路 master 就绪；应答清… | range=08..77, count=2, addrs=33 50 |  |
| switch_master_filter | ✅ PASS | token | 两组切换及重复同向切换均返回 0；**且 PE02（`i2c4.SCL` 与 `i2cf4.SCLO` 的共同物理 pa… | PE02_func_i2c4=0, PE02_func_i2cf4=1 |  |

## 汇总

OVERALL: PASS
