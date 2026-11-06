# SPI filter (PFR/TMP) 驱动测试报告

- 工程: spif_test
- case: case_default
- 日期: 2026-11-05 23:04:53
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=30s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (24/24)
- 接线: spif1(master) 经 spi3 CS0(GPIOI8) 读 flash3_0；spif3 filter 在 spi4 侧监视；spif3 master 读 spi4 CS0 flash；其余零接线

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| addr_mode_config | ✅ PASS | token | 4B 模式 → `ADDR_3B_4B_SEL==1`；3B 模式 → `==0` | sel_4b=1, sel_3b=0 |  |
| addr_privilege_guard | ✅ PASS | token | `addr == MB(256)` → `-EINVAL`；`addr > MB(256)` → `-EINVAL`；`… | guard_boundary=0x10000000, len_min=16384 |  |
| addr_privilege_valid_region | ✅ PASS | token | 前置整片清 0 后，`MB(1)..+KB(16)` 的 READ 窗口 `reg_off=2` 位 0 置 1（寄存器… | reg_off=2, len=16384 |  |
| addr_whitelist_fill | ✅ PASS | token | 读/写两个窗口（各 512 寄存器）首/尾寄存器与 num 对应；num=2 后寄存器保持不变 | reg_nun=512, reg_last=511 |  |
| bus_pairing | ✅ PASS | token | 4 实例：`spif_spi_dev(dev)` 指针 == `DT_PHANDLE(spifN, spi)` 解析出的… | - |  |
| cmd_idx_accessors | ✅ PASS | token | CMD 与 DUMMY_CYCLE 按 idx 读写往返一致（DUMMY 用槽 6 FAST_READ；槽 5 READ… | slot5_cmd=0x77, slot5_dummy=0, slot6_raw=0x90b, slot6_dummy=0x9 |  |
| cmd_table_add_remove | ✅ PASS | token | add 后槽位存在；remove 后 `get_general_cmd_slot()` 返回 `-ENOSR`；重复 r… | - |  |
| cmd_table_full_boundary | ✅ PASS | token | 前 40 个 add 全 0；**第 41 个 → `-ENOSR`**（容量 = `SPIF_GENERAL_CMD_… | added=40, capacity=40 |  |
| cold_reset_state | ✅ PASS | token | `CFG.EN==0`、`OPERATION_MODE==0`、`ADDR_3B_4B_SEL==0`、`TARGET_… | cfg=0x40, target_addr=0xffffffff, bcmd=0x7 |  |
| device_ready | ✅ PASS | token | 4 实例 ready；`ctrl_idx == 0..3`；配对 spi 设备非 NULL 且 ready；cs gpi… | - |  |
| filter_enable_bit | ✅ PASS | token | true → `CFG.EN==1`，false → `CFG.EN==0`（两种 API 各验一次） | - |  |
| flash_size_boundary | ✅ PASS | token | `0` → `-EINVAL`；`3`（不能整除）→ `-EINVAL`；`MB(256)+1` → `-EINVAL`… | max=0x10000000, min_ok=0x10000 |  |
| loopback_filter_bad_cmd_bad_addr | ✅ PASS | token | `CMD_ERR==1`、`ADDR_ERR==0`（先查命令，命中即上报）、`ERROR_CMD==0x03`；SPI… | before=0, ret=0, raw=0x3000002, cmd_err=1, addr_err=0, por_addr=0, error_cmd=0x3, error_addr=0 |  |
| loopback_filter_bad_cmd_legal_addr | ✅ PASS | token | `CMD_ERR==1`、`ADDR_ERR==0`、`ERROR_CMD==0x03`；SPI 事务返回 0 | before=0, ret=0, raw=0x3000002, cmd_err=1, addr_err=0, por_addr=0, error_cmd=0x3, error_addr=0 |  |
| loopback_filter_legal_both | ✅ PASS | token | `CMD_ERR==0`、`ADDR_ERR==0`；SPI 事务返回 0 | before=0, ret=0, raw=0, cmd_err=0, addr_err=0, por_addr=0, error_cmd=0, error_addr=0 |  |
| loopback_filter_legal_cmd_bad_addr | ✅ PASS | token | `CMD_ERR==0`、`ADDR_ERR==1`、`ERROR_CMD==0x03`、`ERROR_ADDR==0x… | before=0, ret=0, raw=0x3000045, cmd_err=0, addr_err=1, por_addr=1, error_cmd=0x3, error_addr=0x4000 |  |
| loopback_jedec | ✅ PASS | token | `spif3` filter/passthrough 态检查通过；`spif1` master 态检查通过；SPI 事务… | ret=0, jedec=204019 |  |
| loopback_read_addr | ✅ PASS | token | SPI 事务返回 0；读回字节经 `ZT_INFO` 上报（不预设器件值） | ret=0, data=ff89e7b7 |  |
| master_read_jedec_id | ✅ PASS | token | SPI 事务返回 0；**实测 3 字节 JEDEC ID 经 `ZT_INFO` 上报**（不预设厂商值，基准待 P6… | jedec=204019 |  |
| master_read_with_addr | ✅ PASS | token | SPI 事务返回 0；读回字节经 `ZT_INFO` 上报（不预设器件值） | read=ffffffff |  |
| passthrough_mode_switch | ✅ PASS | token | cold_reset 后 `filter_mode_check()==true` 且 `passthrough_mode… | after_cold_reset filter=1, passthrough=0 |  |
| pinctrl_state_switch | ✅ PASS | token | 先确保相邻 spif4 在 filter 态；`switch_to_filter → filter_mode_check… | - |  |
| reg_lock_unlock | ✅ PASS | token | `reg_unlock → IP_LOCK==1`；`reg_lock → IP_LOCK==0` | unlock_ip_lock=1, lock_ip_lock=0 |  |
| sck_threshold_config | ✅ PASS | token | `SCK_DIV==5`；`SCK_FQC_HI==2`；`SCK_FQC_LO==100`；`SCK_CHECK==1… | sck_div=5, hi=2, lo=100 |  |

## 汇总

OVERALL: PASS
