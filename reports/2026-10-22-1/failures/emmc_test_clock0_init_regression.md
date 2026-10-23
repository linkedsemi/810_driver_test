# emmc_test 大面积失败：`set_io` 拒绝 `clock == 0` 导致卡初始化失败

> **✅ 已修复（HEAD `a9aebb55833` / `0668549e51f`）。** 新代码为
> `clock == 0 || 在 [min,max] 内 → mmc_clock_freq_change()`，`mmc_clock_freq_change()`
> 对 `clock == 0` 早退 ⇒ 初始化恢复。复测两 case 均 **PASS 12/12**，
> `disk_access_init=0`、`set_io` 越界项 `ret=-22` 转 PASS。本文保留为回归证据。

> 会话：`2026-10-22-1` · 被测版本：`zephyr` HEAD `f4fe474611477d04eeef529c97adcc18caded688`
> （分支 `dw_dma_ctlh_block_ts_mask`；banner 实测 `openbmc-zephyr-v0.1.0-954-gf4fe47461147`）
> 被测变更：`db0451aa304 fix(sdhc): reject out-of-range clock in set_io`
> 结论：**驱动真实回归（阻塞卡初始化），非采集/烧录问题，非测试侧问题**。

## 结果表（两个 case 逐项一致）

| case | 结果 | 通过/总数 |
|---|---|---|
| `case_block_driver`（SDMA=y） | **FAIL** | **3/12** |
| `case_block_pio`（SDMA=n） | **FAIL** | **3/12** |

| 测试项 | 结果 | 失败位置/原因 |
|---|---|---|
| `guard_matches_driver_behavior` | ✅ PASS | 不依赖卡初始化 |
| `host_props_match_devicetree` | ✅ PASS | 不依赖卡初始化 |
| `read_unknown_drive_rejected` | ✅ PASS | 只测非法盘名，不初始化卡 |
| `disk_present_and_init` | ❌ FAIL | `disk_access_init` 返回非 0 |
| `disk_ioctl_sector_count` | ❌ FAIL | `ensure_card()` 非 0 |
| `disk_ioctl_sector_size` | ❌ FAIL | `ensure_card()` 非 0 |
| `read_aligned_matches_unaligned` | ❌ FAIL | `ensure_card()` 非 0 |
| `write_read_scratch_single` | ❌ FAIL | `ensure_card()` 非 0 |
| `write_read_scratch_multiblock` | ❌ FAIL | `ensure_card()` 非 0 |
| `request_data_invalid_opcode_notsup` | ❌ FAIL | `ensure_card()` 非 0 |
| `request_mutex_not_leaked` | ❌ FAIL | `ensure_card()` 非 0 |
| `set_io_clock_out_of_range_not_silent` | ❌ FAIL | `ensure_card()` 非 0（**前置条件不可满足**，见下） |

## 关键串口证据（两 case 逐字相同）

`emmc_test_case_block_{driver,pio}/serial_TGT.log`：

```
[00:00:00.176,000] <err> linkedsemi_sdhci: sdhci@40049000: linkedsemi_sdhci_set_io:
                   unsupported clock 0 Hz (min 50000 Hz, max 200000000 Hz)
[00:00:00.176,000] <err> sd: Could not disable card power via SDHC
...
INFO disk_present_and_init: disk_access_init=-22,status=1,dev=SD2
```

`-22` = `-EINVAL`，与 `set_io` 新返回码一致；`status=1` = `DISK_STATUS_*` 非 OK。
该错误发生在 ztest 第一组块层用例之前（0.176 s），此后所有 `ensure_card()`
都直接返回缓存错误（`ensure_card()` 首次探测后缓存结果），故 9 项连锁失败。

## 机理

1. `zephyr/subsys/sd/sd.c::sd_init_io()` 明确把时钟“先关掉”：
   `bus_io->clock = 0;`（sd.c:129），随后为下电复位调用
   `sdhc_set_io(card->sdhc, bus_io)`（sd.c:153）。
2. 新驱动 `linkedsemi_sdhci_set_io()` 的判定改为
   `if (clock != 0 && clock 在 [min,max]) {改频} else { DEV_ERR(...); return -EINVAL; }`
   ⇒ `clock == 0` 落入 else，被拒绝。
3. `sd_init_io()` 见返回非 0 ⇒ `LOG_ERR("Could not disable card power via SDHC")`
   ⇒ 返回错误 ⇒ `sd_init()` / `disk_access_init()` 失败 ⇒ 整条卡通路不可用。
4. 提交说明自身已提示该风险（"a zero clock is rejected too, so sd_init_io() will fail
   on this host controller"），实测与说明一致。

**影响面**：只影响需要初始化卡片的项（9/12）；两个 guard 用例与
`read_unknown_drive_rejected` 不碰卡，仍 PASS。

## 与上一轮（各 11/12）的差异

| 项 | 上一轮 HEAD `b30f5d04223` | 本轮 HEAD `f4fe4746114` |
|---|---|---|
| 整体 | FAIL 11/12 | **FAIL 3/12** |
| `set_io_clock_out_of_range_not_silent` | FAIL，`ret=0`（缺陷靶点：越界被静默接受） | **仍 FAIL**，但失败点前移到 `ensure_card()` —— 该断言本轮**从未执行到**，不能据此宣称“已转 PASS” |
| 卡初始化链路 | 全 PASS | **全 FAIL（`disk_access_init=-22`）** |
| guard 2 项 | PASS | PASS（不受影响） |

即：驱动的越界时钟检查本身方向正确，但它把合法的 `clock == 0`（“保持/关闭时钟”语义）
一并拒绝，引入了一个**比原缺陷更严重的初始化回归**。测试断言未做任何放宽。

## 排除采集/烧录问题

- 两 case 均重新 pristine 构建（`-p always`），ELF 含新串 `unsupported clock`，
  mtime 19:41 / 19:45；串口 banner 与 HEAD 一致（`...-954-gf4fe47461147`）。
- 两轮烧录均见 `Start address 0x10000000` + `Transfer rate: 41 KB/sec`。
- 归档完整：`ZT_BOOT` 1 次、3 个 `Running TESTSUITE`、12 项 token 齐全、
  末行 `PROJECT EXECUTION FAILED`（真实失败，不是窗口截断）。
- `case_block_driver` **复跑一次**结果逐项一致（仍 3/12，错误行同时间点、同文本）⇒ 确定性复现。
- 无 `error 138` / `DebuggerServer quit`；DS 全程健康。

## 建议（未实施）

需要让 `clock == 0` 继续作为合法输入（例如“不改频/保持当前时钟”），只对
`clock != 0 && 越界` 返回 `-EINVAL`；或在 `sd_init_io()` 里对 `clock == 0`
跳过 `sdhc_set_io()`。本次按要求未修改任何驱动/测试/工程配置。
