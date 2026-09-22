# QSPI NOR flash — qspi_flash_test_case_no_calib 失败分析

| 项 | 值 |
|---|---|
| 工程 | qspi_flash_test |
| case | case_no_calib（板 `lsqsh_evb@1os/lsqsh/cpu1`，单核） |
| 被测驱动 | `zephyr/drivers/spi/qspi_ls.c`（`linkedsemi,ls-qspi`）+ `zephyr/drivers/flash/spi_nor_multi_dev.c`（`jedec,spi-nor`） |
| 报告 | [qspi_flash_test_report.md](../qspi_flash_test_case_no_calib/qspi_flash_test_report.md) |
| 结果 | FAIL (24/26) |
| 执行标记 | CTRL=N/A（单核工程无 CTRL 侧）, TGT=FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |

本 case 与 `case_default` **同源集**：同一份固件与用例代码，`tests/case_no_calib/case_no_calib.overlay:1-20` 明确「与 case_default 的差异只差两处」——(1) `&qspi2` 加 `timing-calibration-disabled`（`:25`）；(2) `flash2` 的 `spi-max-frequency` 由 50 MHz 降到 `DT_FREQ_M(25)`（`:30`）。启动日志确认生效：`calib_disabled=1`、`status=0`（`serial_TGT.log:6`），`hw config: spi=25000000 Hz cap_neg=0 cap_dly=0 (calib_combo=0x00)`（`serial_TGT.log:14`）。

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| **read_past_end_rejected** | 异常参数 | `flash_read(flash_dev, 0x2000000, rbuf, 16) == -EINVAL`（`qspi_flash_test.c:397`；COVERAGE 预期 `-EINVAL`，来源 `spi_nor_multi_dev.c:1386-1389` `addr + size > flash_size` 检查） | `ret = 0`（越界读未被拒，驱动返回成功） | 报告原文 `qspi_flash_test.c:397: (ret not equal to -EINVAL)`；串口原文 `read past end should be -EINVAL, got 0` | `serial_TGT.log:136`、`:137`、`:138` |
| **write_past_end_rejected** | 异常参数 | `flash_write(flash_dev, 0x2000000, wbuf, 16) == -EINVAL`（`qspi_flash_test.c:405`；COVERAGE 预期 `-EINVAL`，来源 `spi_nor_multi_dev.c:1477-1480` `size + addr > flash_size` 检查） | `ret = 0`（越界写未被拒，驱动返回成功） | 报告原文 `qspi_flash_test.c:405: (ret not equal to -EINVAL)`；串口原文 `write past end should be -EINVAL, got 0` | `serial_TGT.log:142`、`:143`、`:144` |

> 其余 24 项全部 PASS，逐项依据见 `serial_TGT.log:211-241`（三套件 summary：calib 6/6 PASS、rw 8/10、stg 10/10）。报告 md 的「实测(INFO)」列全为 `-`，本表实际值取自 `serial_TGT.log` 的 `Assertion failed` 行。

## 归因

- 两项失败**与校准/频率配置无关**：断言的是 `spi_nor_multi_dev.c` 的 flash 末尾越界检查（`addr + size > flash_size`），在任何频率下都应返回 `-EINVAL`。本 case 下（25 MHz、关校准）驱动**返回 0 并继续执行**，即越界检查**未生效** ⇒ 指向**驱动侧参数校验缺陷**，而非用例断言口径或环境问题。
- 同一 run 内所有写 / 读 / 擦 / STIG / wire-mode 用例均 PASS（含 `write_read_len8192` diff=0、`raw_read_1_1_1_ok` diff=0、`stg_read_chunked_over_4096` diff=0 —— 见 `serial_TGT.log:159,171,190`），说明底层数据通路上无误，**失败面被严格限制在越界参数校验这一条分支**。
- **执行标记 FAILED 是否为采集截断**：不是。日志完整到达 `------ TESTSUITE SUMMARY END ------` 与 `PROJECT EXECUTION FAILED`（`serial_TGT.log:243-246`），26 项结果齐全，属**真实断言失败**。
- 未采集到的数据：无（两项的返回码 `0` 均由串口 `Assertion failed` 行直接给出）。

## 备注

- **与 `qspi_flash_test_case_default` 的差异对比（同源集，仅 overlay 两处不同：关校准 + 50→25 MHz）**：
  - **只在 default 挂、本 case 过**（10 项）：`set_freq_boundary_6m25_accepted`、`set_freq_zero_silent`、`erase_sector_all_ff`、`write_read_len1/255/256/4096/8192`、`raw_read_1_1_1_ok`、`stg_read_chunked_over_4096`。即 default 里 12 项失败中的 10 项，在本 case 下全部通过（本 case 这几项 diff 均为 0，`serial_TGT.log:159,171,190`）。
  - **两边都挂**（2 项）：`read_past_end_rejected`、`write_past_end_rejected`。
  - ⇒ 失败面差异（12 vs 2）**完全由「校准使能 + 50 MHz」这一轴解释**：关掉校准并把时钟降到 25 MHz 后，内容错位类与频率边界类失败全部消失，只剩与频率无关的越界校验缺陷。
- 关联文档：`810_driver_test/projects/qspi_flash_test/tests/COVERAGE.md`（覆盖矩阵；本 case 对应 `calib_status_expected` 的 `UNUSED(0)` 分支预期，见其「预期来源」列 `qspi_ls.c:952-967,1030-1039`）。
- 本次为 2026-09-21-1 会话首次归档该 case（`reports/2026-09-21-1/summary.md:48` 记 FAIL 24/26），无上一轮可直接比对的回归数据。

---

> **本轮复核（2026-09-22-2）**：失败项集合与本文件（源自 2026-09-21-1）**逐项一致**（自动比对报告 JSON 通过），结果数值亦一致 ⇒ 分析沿用，未复现新变化。
