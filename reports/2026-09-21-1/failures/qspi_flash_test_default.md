# QSPI NOR flash — qspi_flash_test_case_default 失败分析

| 项 | 值 |
|---|---|
| 工程 | qspi_flash_test |
| case | case_default（板 `lsqsh_evb@1os/lsqsh/cpu1`，单核） |
| 被测驱动 | `zephyr/drivers/spi/qspi_ls.c`（`linkedsemi,ls-qspi`）+ `zephyr/drivers/flash/spi_nor_multi_dev.c`（`jedec,spi-nor`） |
| 报告 | [qspi_flash_test_report.md](../qspi_flash_test_case_default/qspi_flash_test_report.md) |
| 结果 | FAIL (14/26) |
| 执行标记 | CTRL=N/A（单核工程无 CTRL 侧）, TGT=FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |

本 case 配置（证据 `tests/case_default/test.sh:78` 指向 `cpu1_qspi_flash_test/app.overlay`）：**时序校准使能**、flash2 `spi-max-frequency = <DT_FREQ_M(50)>`（`cpu1_qspi_flash_test/app.overlay:27`），启动日志确认 `spi=50000000 Hz cap_neg=1 cap_dly=2 (calib_combo=0x05)`（`serial_TGT.log:16`）。

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| calib_status_expected | 功能通路 | `status == QSPI_TIMING_CALIB_SUCCESS(1)`（校准使能分支，`qspi_flash_test.c:638`；COVERAGE 预期 `calib_status == 1`） | `status=1,calib_disabled=0` → PASS | — | `serial_TGT.log:6` |
| calib_status_null | 异常参数 | `qspi_timing_calibration_status(NULL) == -EINVAL`（`qspi_flash_test.c:620`） | PASS | — | `serial_TGT.log:44` |
| set_freq_above_max_rejected | 异常参数 | `ret_set==0` 且 `ret_init != 0`（`qspi_flash_test.c:653-656`） | `set=0,reinit=-19` → PASS | — | `serial_TGT.log:47`、`:66` |
| set_freq_below_min_rejected | 边界 | `ret_set==0` 且 `ret_init != 0`（`qspi_flash_test.c:667-668`） | `set=0,reinit=-19` → PASS | — | `serial_TGT.log:69`、`:87` |
| **set_freq_boundary_6m25_accepted** | 边界 | `spi_nor_re_init()` 在 6.25 MHz（`clk_cyc == 31`）返回 `0`（`qspi_flash_test.c:682`；COVERAGE 预期 `re_init == 0`） | `set=0,reinit=-19`（-ENODEV） | 报告原文 `qspi_flash_test.c:682: (ret_init is non-zero)`；串口附注 `re_init at 6.25 MHz (clk_cyc == 31) should succeed, got -19`。同 run 中 flash 已读不到：`invalid jedec id 00 00 00` / `flash not present ... (after 19 attempts)` | `serial_TGT.log:93`、`:95-97`、`:102` |
| **set_freq_zero_silent** | 异常参数 | `spi_nor_re_init()` 返回 `0`（钉住"frequency==0 静默不改时钟"现状，`qspi_flash_test.c:698`；COVERAGE 预期 `re_init == 0`） | `set=0,reinit=-19` | 报告原文 `qspi_flash_test.c:698: (ret_init is non-zero)`；附注 `frequency 0 is silently accepted by apply_clk() ...; got re_init=-19`；同 run flash 读不到 `jedec id 00 00 00` | `serial_TGT.log:108`、`:110-112`、`:117` |
| erase_non_sector_multiple_rejected | 异常参数 | `flash_erase(size=4097) == -EINVAL`（`qspi_flash_test.c:387`） | PASS | — | `serial_TGT.log:123` |
| **erase_sector_all_ff** | 功能通路 | 擦除后 4096 B 读出全 `0xFF`，`offender == -1`（`qspi_flash_test.c:271`；COVERAGE 预期 `ret==0；读出全 0xFF`） | `first offender @0 val=0xcf`（首字节 0xCF，非 0xFF） | 报告原文 `qspi_flash_test.c:271: (offender not equal to -1)` / `erased sector not all 0xFF (first offender @0 val=0xcf)` | `serial_TGT.log:127-128`、`:129` |
| erase_unaligned_addr_rejected | 异常参数 | `flash_erase(addr=base+1) == -EINVAL`（`qspi_flash_test.c:379`） | PASS | — | `serial_TGT.log:132` |
| **read_past_end_rejected** | 异常参数 | `flash_read(0x2000000, 16) == -EINVAL`（`qspi_flash_test.c:397`；COVERAGE 预期 `-EINVAL`，`spi_nor_multi_dev.c:1386-1389`） | `ret = 0`（越界读未被拒） | 报告原文 `qspi_flash_test.c:397: (ret not equal to -EINVAL)` / `read past end should be -EINVAL, got 0` | `serial_TGT.log:136-137`、`:138` |
| **write_past_end_rejected** | 异常参数 | `flash_write(0x2000000, 16) == -EINVAL`（`qspi_flash_test.c:405`；COVERAGE 预期 `-EINVAL`，`spi_nor_multi_dev.c:1477-1480`） | `ret = 0`（越界写未被拒） | 报告原文 `qspi_flash_test.c:405: (ret not equal to -EINVAL)` / `write past end should be -EINVAL, got 0` | `serial_TGT.log:142-143`、`:144` |
| **write_read_len1** | 功能通路 | `rbuf` 与 `wbuf` 逐字节一致（1 B，pattern 0x11，`qspi_flash_test.c:289`） | 内容不一致（用例未打印 diff，故差异字节数**未采集到**） | 报告原文 `qspi_flash_test.c:289: (rbuf not equal to wbuf)` / `len=1 write/read mismatch` | `serial_TGT.log:148-149`、`:150` |
| **write_read_len255** | 边界 | 255 B 回读逐字节一致（pattern 0x22，`qspi_flash_test.c:306`） | 239 / 255 字节不同 | 报告原文 `qspi_flash_test.c:306: (rbuf not equal to wbuf)` / `len=255 write/read mismatch (239 bytes differ)` | `serial_TGT.log:154-155`、`:156` |
| **write_read_len256** | 边界 | 256 B 回读逐字节一致（pattern 0x33，`qspi_flash_test.c:324`） | 242 / 256 字节不同 | 报告原文 `qspi_flash_test.c:324: (rbuf not equal to wbuf)` / `len=256 write/read mismatch (242 bytes differ)` | `serial_TGT.log:160-161`、`:162` |
| **write_read_len4096** | 边界 | 4096 B 回读逐字节一致（pattern 0x44，`qspi_flash_test.c:343`） | 3841 / 4096 字节不同 | 报告原文 `qspi_flash_test.c:343: (rbuf not equal to wbuf)` / `len=4096 write/read mismatch (3841 bytes differ)` | `serial_TGT.log:166-167`、`:168` |
| **write_read_len8192** | 边界 | 8192 B 回读逐字节一致（pattern 0x55，`qspi_flash_test.c:370`） | `len=8192,diff=7681`（7681 / 8192 字节不同） | 报告原文 `qspi_flash_test.c:370: (rbuf not equal to wbuf)` / 附 `len=8192 write/read mismatch (7681 bytes differ)` | `serial_TGT.log:171`、`:173-174`、`:175` |
| **mode_222_rejected** | 异常参数 | `raw_op(MODE_222) == -EINVAL`（钉住驱动缺口，`qspi_flash_test.c:503` 附近） | `ret=-22` → PASS | — | `serial_TGT.log:181`、`:183` |
| **raw_read_1_1_1_ok** | 功能通路 | `MODE_111`+`0x03`+`addr_len=4` 读 256 B 内容一致（pattern 0x66，`qspi_flash_test.c:455`） | `len=256,addr_len=4,diff=249`（249 / 256 字节不同） | 报告原文 `qspi_flash_test.c:455: (rbuf not equal to wbuf)` / `1-1-1 readback mismatch (249 bytes differ)` | `serial_TGT.log:186`、`:188-189`、`:190` |
| **raw_read_1_1_4_ok** | 功能通路 | `raw_op(MODE_114)` 返回 `0`（`qspi_flash_test.c:475`） | `ret=0` → PASS（仅断言返回码，不校验内容） | — | `serial_TGT.log:193`、`:194` |
| **raw_read_1_4_4_ok** | 功能通路 | `raw_op(MODE_144)` 返回 `0`（`qspi_flash_test.c:491`） | `ret=0` → PASS（同上） | — | `serial_TGT.log:197`、`:198` |
| **spi_transceive_notsup** | 异常参数 | `spi_transceive() == -ENOTSUP(-134)`（`qspi_flash_test.c:594`） | `ret=-134` → PASS | — | `serial_TGT.log:201`、`:202` |
| **stg_cmd_wren_ok** | 功能通路 | `raw_op` 纯命令 WREN 返回 0（`qspi_flash_test.c:422`） | PASS | — | `serial_TGT.log:205` |
| **stg_read_chunked_over_4096** | 边界 | `MODE_111`+`addr_len=4` 读 8192 B 内容一致（pattern 0x55，`qspi_flash_test.c:574`） | `len=8192,addr_len=4,diff=7960`（7960 / 8192 字节不同） | 报告原文 `qspi_flash_test.c:574: (rbuf not equal to wbuf)` / `chunked read mismatch (7960 bytes differ)` | `serial_TGT.log:208`、`:210-211`、`:212` |
| **stg_reg_read_len_4097_rejected** | 边界 | `raw_op` 读 4097 B == `-EINVAL(-22)`（`qspi_flash_test.c:541`） | `ret=-22` → PASS | — | `serial_TGT.log:215`、`:217` |
| **stg_write_len_4096_accepted** | 边界 | 写 4096 B 返回 0（`qspi_flash_test.c:516`） | `len=4096,ret=0` → PASS | — | `serial_TGT.log:220`、`:221` |
| **stg_write_len_4097_rejected** | 边界 | 写 4097 B == `-EINVAL(-22)`（`qspi_flash_test.c:529`） | `len=4097,ret=-22` → PASS | — | `serial_TGT.log:224`、`:226` |

> 表中通过项一并列出，便于与 no_calib 逐项对齐；**加粗**为失败项，共 12 项。
> 注：报告 md 的「实测(INFO)」列全为 `-`，实际值均从 `serial_TGT.log` 的 `ZT_INFO` / `Assertion failed` / summary 行取回。

## 归因

失败面可分两组，指向不同根因：

1. **数据内容类（8 项）**：`erase_sector_all_ff`、`write_read_len1/255/256/4096/8192`、`raw_read_1_1_1_ok`、`stg_read_chunked_over_4096`。
   全部表现为**驱动返回 0（参数被接受、无报错），但回读内容错位/错误**（`erase_sector_all_ff` 首字节 0xCF；其余 diff 比例极高，255 B 差 239、8192 B 差 7681、chunked 差 7960）。同一份固件在 **case_no_calib（25 MHz、关校准）下 8 项全过**，在 case_default（**50 MHz、校准使能**）下全挂 ⇒ 指向 **50 MHz 下的 QSPI 读时序/采样窗口问题**（校准结果是 `combo 5 cap_neg=1 cap_dly=2`，`serial_TGT.log:15`），即 **驱动侧 / 硬件时序**，而非用例或数据。

2. **频率边界类（2 项）**：`set_freq_boundary_6m25_accepted`、`set_freq_zero_silent`。
   两项预期都是 `spi_nor_re_init() == 0`，实际都是 `-19`，且**失败瞬间 flash 已读不到**（`invalid jedec id 00 00 00` / `flash not present after 19 attempts`，`serial_TGT.log:92,97 / 107,112`）。同样两项在 case_no_calib 下 `reinit=0` 通过。⇒ 指向 **`spi_nor_re_init()` 在"校准使能 + 50 MHz"配置下重跑 init 失败**（`apply_clk` 重构时钟后器件不可见），属驱动 / 配置交互，非断言口径问题。

3. **越界校验类（2 项，两 case 共有）**：`read_past_end_rejected`、`write_past_end_rejected` 均返回 `0` 而非 `-EINVAL`。**与校准/频率无关**（no_calib 同样失败），指向 `spi_nor_multi_dev.c` 的 `addr+size > flash_size` 边界检查（COVERAGE 标注 `:1386-1389` / `:1477-1480`）**未生效**——即**驱动侧校验缺陷**，可独立于上面两组定位。

**执行标记 FAILED 是否为采集截断**：不是。两份日志均完整到达 `------ TESTSUITE SUMMARY END ------` 与 `PROJECT EXECUTION FAILED`（`serial_TGT.log:264-267` / `:243-246`），26 项结果齐全，属**真实断言失败**。

**无法判定项**：`write_read_len1` 的实际差异字节数未采集到（用例该分支未打印 `diff`），但同族 4 个更长长度用例的 diff 已足以定性。

## 备注

- **与 `qspi_flash_test_case_no_calib` 的差异对比（同源集，仅 overlay 两处不同：关校准 + 50→25 MHz）**：
  - **只在 default 挂、no_calib 过**（10 项）：`set_freq_boundary_6m25_accepted`、`set_freq_zero_silent`、`erase_sector_all_ff`、`write_read_len1/255/256/4096/8192`、`raw_read_1_1_1_ok`、`stg_read_chunked_over_4096`。
  - **两边都挂**（2 项）：`read_past_end_rejected`、`write_past_end_rejected`（返回 0 而非 -EINVAL）。
  - **两边都过**（14 项）：其余全部，含 `calib_status_expected`（default 期望 1、no_calib 期望 0，各自命中分支）、`erase_unaligned_addr_rejected`、`erase_non_sector_multiple_rejected`、`raw_read_1_1_4_ok`、`raw_read_1_4_4_ok`（后两者只断言返回码，不校验内容，故掩盖了与 `raw_read_1_1_1_ok` 同样的内容错位）。
  - 结论：**失败面的巨大差异（12 vs 2）由"校准使能 + 50 MHz"这一轴驱动**；越界校验的 2 项是与该轴解耦的独立驱动缺陷。
- 关联文档：`810_driver_test/projects/qspi_flash_test/tests/COVERAGE.md`（覆盖矩阵 + 预期来源 + 边界常量来源）、`TRIAGE_QSPI_SPIF_2026-09-18.md`（raw 读运行态 `addr_len=4` 结论）。
- 本次为 2026-09-21-1 会话首次归档该 case（`reports/2026-09-21-1/summary.md:47` 记 FAIL 14/26），无上一轮可直接比对的回归数据。
