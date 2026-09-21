# SPI (spi_dw / spi_ls) — case_default 失败分析

| 项 | 值 |
|---|---|
| 工程 | spi_test |
| case | spi_test_case_default（报告内记 `case_default`） |
| 被测驱动 | `zephyr/drivers/spi/spi_dw.c`（spi3, master）+ `zephyr/drivers/spi/spi_ls.c`（spi1, slave）；回环用例源码 `810_driver_test/projects/spi_test/cpu1_spi_test/src/spi_loopback.c` |
| 报告 | [spi_test_report.md](../spi_test_case_default/spi_test_report.md) |
| 结果 | FAIL (18/19) |
| 执行标记 | CTRL=COM18（本 case 单核，无 CTRL 日志落盘）, TGT=COM16 |
| 内存兜底 | 有效（`spi_test_report.json:8` `"mem_state": "ok"`） |

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| link_probe | 功能通路（DT 契约 + 外部跳线连通性） | 两设备 `device_is_ready` 真；8 B 双向逐字节相等；`master_ret==0`；`slave_ret==8` | `master_ret=0, slave_ret=-119, state=0`（state=0 即 `LB_LINK_BAD`） | `WEST_TOPDIR/810_driver_test/projects/spi_test/cpu1_spi_test/src/spi_loopback.c: 454: spi_loopback_link_probe: (lb_link_state not equal to LB_LINK_OK)` | `serial_TGT.log:91`（INFO 行）、`serial_TGT.log:93`（assert 原文）、`serial_TGT.log:95`（LOG_INF：`master_ret=0 slave_ret=-119 timed_out=1 mrx0=ff stx0=c0 srx0=00 mtx0=30 -> BAD`） |

补充（不进明细，仅作判读依据）：
- `slave_ret=-119` = `-EINPROGRESS`（`zephyr/lib/libc/minimal/include/errno.h:103`），即从设备线程尚未写回结果时被中止，`lb_slave_ret` 仍是初值（`spi_loopback.c:319` `lb_slave_ret = -EINPROGRESS;` 从未被覆盖）。
- `timed_out=1` ⇒ `k_thread_join()` 有界等待超时后 `k_thread_abort()`（`spi_loopback.c:343-353`），从设备线程仍阻塞在从模式 `spi_transceive()` 的 K_FOREVER 等待（`spi_loopback.c:199-205`）。
- 逐字节比对亦失败：`mrx0=ff`（期望 = `stx0=c0`）、`srx0=00`（期望 = `mtx0=30`），双向数据均未通（`serial_TGT.log:95`）。

## 归因

**无法在此次证据范围内判定为驱动缺陷；最可能是「外部回环跳线未接/未按 C1 逐 pin 对应接好」这一环境前置条件不满足。**

判读链：
1. 主机侧已正常完成：`master_ret=0`（对比上一轮 C1 前为 `master_ret=-116`，见 `COVERAGE.md:270`），说明 C1 修复的「从设备配置对象」问题已生效，主机侧配置/时序通路可用。
2. 从机侧始终未完成：`slave_ret=-119`（-EINPROGRESS 初值）+ `timed_out=1` ⇒ 从设备从未等到主机时钟而卡在 `spi_transceive()`（`spi_ls` 从模式为 K_FOREVER 等待，`COVERAGE.md:151`）。
3. 双向数据逐字节均不相等（`mrx0=ff` vs `stx0=c0`；`srx0=00` vs `mtx0=30`）。

这与 `COVERAGE.md:166` 的 R-5 判决一致：**物理跳线必须含 CS 一路（`spi3 PG13 ↔ spi1 PA03`）**，且「逐 pin 对应从未核实」，该文档明确要求「不要假设它已连着」。R-5 预言的现象正是 `link_probe` 仍 FAIL（`slave_ret` 变为有界超时值）——本次实测吻合。

但「跳线确实未接」这一条属于工程自述的**未核实假设**（`COVERAGE.md:158-166` §6 未决项 R-1/R-5，用户原话为推测语气），DT/电路图均无法证实。**要判定究竟是用例/驱动还是跳线，还缺以下证据**：
- 跳线的实物/连续性确认（PA00↔PG14 / PA01↔PG15 / PA02↔PG03 / PA03↔PG13，尤其 CS 一路）；
- 或示波器抓 `spi3` SCK/MOSI 是否有波形、`spi1` 是否被 NSS 选中，以区分「主机没发时钟」与「从机没收到/没被选中」；
- 或用 GDB 读 `spi_ls` 的 `CR1/CR2/SR` 现场（`SPE`/`MSTR`/`NSS` 状态）以区分配置通路问题。

在拿到上述任一证据前，**不判定为 `spi_dw.c`/`spi_ls.c` 驱动缺陷**。

## 备注

- **分流情况（不计入分母）**：本 case 另有 **7 项 SKIP** —— `repeat_roundtrips_state_isolation`、`roundtrip_fifo_boundary`、`roundtrip_length_sweep`、`roundtrip_max_length_256`、`roundtrip_pattern_matrix`、`roundtrip_master_tx_slave_rx`、`slave_ret_recv_frames`，全部为用例主动 `ztest_test_skip()`（`serial_TGT.log:98-117`），理由同一："SPI1↔SPI3 回环跳线未接"（报告 `spi_test_report.md:36-42`）。这是 `lb_link_ok()` 保护机制的预期行为（`COVERAGE.md:182`），见 `serial_TGT.log:119` `TESTSUITE spi_loopback failed.`、`:121` `PROJECT EXECUTION FAILED`。
- 与上一轮回归差异：上一轮 `master_ret=-116`(-ETIMEDOUT) / `slave_ret=0` / `state=0`（`COVERAGE.md:270`），本轮 `master_ret=0` / `slave_ret=-119`(-EINPROGRESS) / `state=0` —— 主机侧由「超时失败」变为「成功」，从机侧由「误按主模式自行收完」变为「等到超时被中止」，即 C1 配置修复生效后**暴露出下一层的前置条件（CS/跳线）**。
- 原有 `spi` 套件 18 项本轮 **全部 PASS**（`serial_TGT.log:87` `TESTSUITE spi succeeded`），失败仅新增的回环套件 `spi_loopback` 的首项 `link_probe`。
- 相关未决项编号：`COVERAGE.md` §6 R-1（跳线是否存在）、R-5（CS 一路 PA03↔PG13 未核实）。
