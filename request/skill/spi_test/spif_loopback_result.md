# spif_test 回环用例结果（spif1 master + spif3 filter → flash3_0）

- **日期**：2026-10-29
- **状态**：已实现、已编译、已上板；`case_default` **24/24 PASS**
- **工程**：`810_driver_test/projects/spif_test`
- **板级/入口**：`lsqsh_evb@1os/lsqsh/cpu1`，`$pc=0x10000000`

## 1. 新增用例

| 用例 | 内容 | 结果 |
|---|---|---|
| `loopback_filter_legal_both` | 0x03 在白名单、0x4000 allow；spif1 发 `0x03 + 0x004000` | PASS，`before=0, raw=0, cmd_err=0, addr_err=0, por_addr=0` |
| `loopback_filter_legal_cmd_bad_addr` | 0x03 合法；`DISABLE 0x4000`；发 `0x03 + 0x004000` | PASS，`before=0, raw=0x3000045, cmd_err=0, addr_err=1, por_addr=1, error_cmd=0x3, error_addr=0x4000` |
| `loopback_filter_bad_cmd_legal_addr` | 移除 0x03；地址合法；发 `0x03 + 0x004000` | PASS，`before=0, raw=0x3000002, cmd_err=1, addr_err=0, por_addr=0, error_cmd=0x3` |
| `loopback_filter_bad_cmd_bad_addr` | 移除 0x03 + `DISABLE 0x4000`；发 `0x03 + 0x004000` | PASS，`before=0, raw=0x3000002, cmd_err=1, addr_err=0, por_addr=0, error_cmd=0x3`（命令先查） |

判据说明：

- 每个矩阵项在事务前都断言 `before=0`（`SPIF_DMA_DATA` 无上一项残留），事后快照才用于判定。
- `legal_both` 的“无 ERROR”不单独证明 filter 看到流量；它由 `before=0`、另三格
  （命令/地址拦截确实发生）以及 `loopback_jedec` / `loopback_read_addr` 的正向事务交叉支撑。
- `bad_cmd_bad_addr` 是命令优先的短路用例；“地址确实非法”由
  `legal_cmd_bad_addr`（同地址 DISABLE → `ADDR_ERR=1`）独立证明。
- 串口可能同时出现既有 `SCK_CHECK` 行（spif3 时钟阈值历史配置），不属于本矩阵判据；
  矩阵只认 `CMD_ERR` / `ADDR_ERR` / `POR_ADDR` / `ERROR_CMD` / `ERROR_ADDR`。
| `loopback_jedec` | spif3 置 filter→passthrough；spif1 置 master；经 spi3 CS0 发 `0x9F`，读 4B；断言 `ret==0` 且 JEDEC 非全 0/全 FF | PASS，`jedec=ef4021` |
| `loopback_read_addr` | 同上前置；发 `0x03 + 0x004000`，读 8B；断言 `ret==0`，数据经 `ZT_INFO` 上报 | PASS，`data=ff0a01ed` |

实现：`projects/spif_test/cpu1_spif_test/src/spif_loopback.c`；CMake 加入 `target_sources`。

## 2. 前置顺序（按参考资产 `samples/spi_monitor_filter/src/main.c`）

1. `linkedsemi_spi_filter_cold_reset(spif3)`
2. `spif_flash_size_set(spif3, MB(32))`
3. `spif_filter_enable(spif3, true)` → 判 `spif_pinctrl_filter_mode_check`
4. `spif_passthrough_analog_mux_enable(spif3, true)` → 判 `spif_pinctrl_passthrough_mode_check`
5. `spif_add_cmd(spif3, 0x9f)`、`spif_add_cmd(spif3, 0x03)`（cold_reset 后固定表 EN=0）
6. 0x4000..0x8000 地址用 `spif_address_privilege_config(..., FLAG_ADDR_PRIV_ENABLE, ...)`
7. `spif_switch_to_filter(spif2)`（spif1 的相邻实例必须先退出 master，否则 -EBUSY）
8. `spif_switch_to_master(spif1)` → 判 `spif_pinctrl_master_mode_check`
9. `spi_transceive(spif_spi_dev(spif1), …)`，CS 用 `spif_spi_cs(spif1)`（spi3 CS0 = GPIOI 8）

每项 teardown 都：spif1 回 filter → `spif_remove_cmd(spif3, 0x9f/0x03)` → spif3 回 filter。
失败断言前先 teardown，避免污染后续用例。
矩阵用例里被 `DISABLE` 的地址区间不单独恢复；下一项 `linkedsemi_spi_filter_cold_reset()`
会把地址白名单重新置为全 allow。

## 3. 地址权限的坑（矩阵用例显式使用）

参考资产调用 `configure_address_protection(..., FLAG_ADDR_PRIV_DISABLE, 0x4000, 0x4000)`。
驱动语义里 **DISABLE = 把该区间标为非法**（清 allow 位）：
- 命令合法时，filter 打印 `ADDR_ERR: 0x1 ... ERROR_ADDR: 0x00004000`（流量仍可透传）；
- 命令非法时，先报 `CMD_ERR`，地址不再检查（`ADDR_ERR=0`）。

矩阵用例的合法地址走 `FLAG_ADDR_PRIV_ENABLE`（setup 默认），非法地址走
`FLAG_ADDR_PRIV_DISABLE`；四种组合均以 `SPIF_DMA_DATA` 字段为准。

## 4. 上板证据

- 串口归档：`810_driver_test/reports/2026-10-29-1/spif_test_case_default/serial_TGT.log`
- 报告：同目录 `spif_test_report.md` / `.json`
- 原始 COM24 副本：`810_driver_test/wsl_environment/wsl_new/logs/spif_test_uart_COM24_20261029_1118.log`
- GDB load：`wsl_new/logs/gdb_load_20261029_111848.log`（`Loading section x18`）
- 官方离线重放：`regression/replay.py --session reports/2026-10-29-1 --case spif_test_case_default --l2-direct --freeze-time`
  → 6 列官方报告、`PASS (24/24)`、`TESTSUITE spif succeeded`；报告已安装到上述报告目录。
- 关键行：
  ```
  INFO loopback_filter_bad_cmd_bad_addr: before=0,ret=0,raw=0x3000002,cmd_err=1,addr_err=0,por_addr=0,error_cmd=0x3,error_addr=0
  INFO loopback_filter_bad_cmd_legal_addr: before=0,ret=0,raw=0x3000002,cmd_err=1,addr_err=0,por_addr=0,error_cmd=0x3,error_addr=0
  INFO loopback_filter_legal_both: before=0,ret=0,raw=0,cmd_err=0,addr_err=0,por_addr=0,error_cmd=0,error_addr=0
  INFO loopback_filter_legal_cmd_bad_addr: before=0,ret=0,raw=0x3000045,cmd_err=0,addr_err=1,por_addr=1,error_cmd=0x3,error_addr=0x4000
  PASS - loopback_filter_bad_cmd_bad_addr
  PASS - loopback_filter_bad_cmd_legal_addr
  PASS - loopback_filter_legal_both
  PASS - loopback_filter_legal_cmd_bad_addr
  INFO loopback_jedec: ret=0,jedec=ef4021
  PASS - loopback_jedec
  INFO loopback_read_addr: ret=0,data=ff0a01ed
  PASS - loopback_read_addr
  TESTSUITE spif succeeded
  PROJECT EXECUTION SUCCESSFUL
  ```

## 5. overlay alias 纠正

`cpu1_spif_test/app.overlay` 原 `spifmaster = &spif3;` 是 spif3-master 用例遗留；
回环路径已改为：

```dts
spif = &spif3;            /* monitor/filter */
spifilter = &spif3;
spifiltermaster = &spif1; /* 发命令的主机 */
spifmaster = &spif1;
```

## 6. 编译/运行命令

```bash
cd /home/eunice/zephyr_workspace/ls1010_port_spi_i2s
export ZEPHYR_BASE=$PWD/zephyr ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
  CROSS_COMPILE=/opt/toolchain/Xuantie-900-gcc-elf-newlib-x86_64-V3.0.1/bin/riscv64-unknown-elf- \
  PATH=/opt/toolchain/Xuantie-900-gcc-elf-newlib-x86_64-V3.0.1/bin:$PATH
CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build build_spif_1os -j 2

cd 810_driver_test/wsl_environment/wsl_new
python3 scripts/gdb_load.py \
  --elf /home/eunice/zephyr_workspace/ls1010_port_spi_i2s/build_spif_1os/zephyr/zephyr.elf \
  --pc 0x10000000 --hold-cpu2 --run-secs 20
```

COM24 采集须在 Windows 侧先启动（WSL 不能直接开 COM 口）：

```powershell
Set-Location C:\Temp\wsl_810_new
python capture_uart.py --ports COM24 --seconds 40 --share-dir C:\Temp\wsl_810_new
```
