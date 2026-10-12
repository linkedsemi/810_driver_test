# 810_driver_test — LSQSH 驱动测试工程集

基于 **LSQSH EVB（双核 AMP，`lsqsh_evb@2os`）** 的驱动批量自动化测试工程集。

- 工程规范与调试细节见 [`CLAUDE.md`](CLAUDE.md)。
- 根仓库背景见 `d:\zephyr4_0\CLAUDE.md`。

**文档分工**（同一事实只在一处维护，其余交叉链接）：

| 文档 | 定位 | 何时看 |
|---|---|---|
| `README.md`（本文件） | 工程集门面（人读）：工程清单、**外部接线登记**、构建/运行命令 | 上手 / 查工程 |
| [`CLAUDE.md`](CLAUDE.md) | agent 路由 + 核心规范速查 + 常见现象 | agent 每次会话 |
| [`HARDWARE_SETUP.md`](HARDWARE_SETUP.md) | **硬件环境唯一源**：板卡 / 接线 / COM 动态识别 / DebugServer / 环境预检 | 搭环境 |
| [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) | 完整技术规范（固件编写 / 判定 / 归档 / 覆盖矩阵） | 写固件 / 改判定 |
| [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) | 已知坑完整清单 | 排查 |
| [`FRAMEWORK_MAP.md`](FRAMEWORK_MAP.md) | 框架分层索引（改哪层要验什么） | 改框架 |

---

## 1. 板级要求

| 项 | 值 |
|---|---|
| 板卡 | `lsqsh_evb@2os`（双核双系统 AMP） |
| cpu1（安全核，target） | `lsqsh_evb@2os/lsqsh/cpu1`，SRAM 执行 |
| cpu2（应用核，controller） | `lsqsh_evb@2os/lsqsh/cpu2`，RAM 加载执行 |
| cpu1 串口 | uart1（PN04 TX / PN03 RX），115200 8N1 |
| cpu2 串口 | **uart5**（PN14 TX / PN13 RX），115200 8N1 |

> 板卡 / COM 映射 / DebugServer / 环境预检等硬件事实的**唯一源是 [`HARDWARE_SETUP.md`](HARDWARE_SETUP.md)**；
> 本表只保留构建相关的 board 名与串口引脚。
> cpu2 默认控制台为 uart4，本工程统一改用 uart5（`common/cpu2_console_uart5.overlay`）。
> **单核工程固件一律用 `lsqsh_evb@1os/lsqsh/cpu1`（cpu1 入口 `0x10000000`）编译测试**，不用 2os 变体；
> 分型判定与 board/入口矩阵见 `SPEC_REFERENCE.md` §6.1.10。
> 详细 GDB/Flash 配置见 `HARDWARE_SETUP.md`、`SPEC_REFERENCE.md` §6.7。

---

## 2. 环境准备（每次测试前）

规则与自检命令见 [`HARDWARE_SETUP.md`](HARDWARE_SETUP.md) §环境预检；
批量预检 `python regression/precheck/run_precheck.py`。
任一项异常，先按 `CLAUDE.md` §5 / `qsh-debug` skill 排查。

---

## 3. 快速开始

### 3.1 编译单个 case

```bash
cd 810_driver_test/projects/<driver>_test/tests/case_<name>
./test.sh                 # 只编译
./test.sh -g              # 编译 + GDB 加载运行
./test.sh -r              # 仅加载运行
```

### 3.2 单次烧录 + 采集 + 解析

- **单核工程**（trng / rtc / otbn / otbn_selftest / mbedtls / adc_temper / dma / flash / wdt / iopmp）：
  ```bash
  cd 810_driver_test/projects/<driver>_test/tests
  python scripts/dbg_run.py --case case_<name> --timeout 35
  ```

- **双核工程**（i2c_pure / i3c_i2c_switch / i3c_dualcore / mbox）：
  ```bash
  cd 810_driver_test/projects/<driver>_test/tests
  python run_test.py --case case_<name> --ctrl-uart COM15 --tgt-uart COM16
  ```
  双核建议用 `run_test.py`（交互式 GDB），`dbg_run.py` 的 `-ex` 命令链在
  `restore` cpu2 时容易卡住。

### 3.3 一键全量回归

```bash
cd 810_driver_test
python regression/run_all.py                # 全擦 flash + 预检 + 跑全部启用 case
python regression/run_all.py --no-precheck  # 跳过环境预检（调试用）
python regression/run_all.py --dry-run      # 只列启用 case，不执行
```

**两级清单**：`regression/cases.yaml` **只列工程**；每个工程跑哪些 case 由
`projects/<工程>/tests/cases.yaml` 自己声明（缺该文件 run_all 直接报错）。
两层都是 YAML list，`#` 开头即屏蔽。

---

## 4. 测试内容

| 工程 | 单/双核 | 是否需要外部接线 | 说明 |
|---|---|---|---|
| `trng_test` | 单核 | 否 | 片内 TRNG |
| `rtc_ls_test` | 单核 | 否 | RTC (rtc_ls) 计数/闹钟/日历进位 |
| `otbn_test` | 单核 | 否 | OTBN 点运算 / RSA / hash |
| `otbn_selftest` | 单核 | 否 | mbedtls selftest 全量套件 |
| `otbn_keygen_test` | 单核 | 否 | OTBN RSA 密钥生成（确定性/三尺寸/正确性/mbedtls hook） |
| `mbedtls_test` | 单核 | 否 | 软件/硬件/OTBN 混合加密配置 |
| `adc_temper_test` | 单核 | 否 | 片内 ADC 温感 (ch13) 读取 |
| `dma_test` | 单核 | 否 | DMA (dmac1) M2M 传输 |
| **`flash_test`** | 单核 | 否 | **`qspi1` 挂的引导 flash**（系统的一部分，4 MiB）—— 与 `qspi_flash_test` 是**两颗不同的 flash、两套驱动栈** |
| `wdt_test` | 单核 | 否 | WDT (sec_iwdg) 非破坏性用例 |
| `iopmp_test` | 单核 | 否 | IOPMP HAL 寄存器操作 |
| `i2c_pure_test` | 双核 | **是** | I2C5（cpu2 master）↔ I2C6（cpu1 slave） |
| `i3c_i2c_switch_test` | 双核 | **是** | I3C/I2C 动态切换 |
| `i3c_dualcore_test` | 双核 | **是** | I3C 主从跨核通信 |
| `mbox_test` | 双核 | 否 | cpu1↔cpu2 共享 SRAM 邮箱 |
| **`app_iwdg_cpu2_test`** | **双核** | 否 | cpu2 侧 app_iwdg 复位与上报 |
| **`spi_test`** | 单核 | **是** | `spi_ls.c`（`spi1`/`spi2`）；回环需 **`spi3`(PG13) ↔ `spi1`(PA03)** 跳线 |
| **`spif_test`** | 单核 | 否 | PFR `spi_filter`；master 模式经 `spi4` 驱动**板上 flash** |
| **`spid_test`** | 单核 | **是** | TPM SPI slave；用 SoC 自己的 SPI master 自环 |
| **`i2cf_test`** | 单核 | **是** | PFR `i2c_filter`；**I2C4 ↔ I2CF4** + 外接 EEPROM |
| **`eth_test`** | 单核 | 部分 | `case_mdio` **无需接线**；`case_link`/`arp_icmp`/`udp_echo` 需**网线接 PC** |
| **`emmc_test`** | 单核 | 否 | **板载** eMMC（`sdhci1`，8-bit）；破坏性 —— 允许格式化整片 |
| **`qspi_flash_test`** | 单核 | 否 | **`qspi2` 挂的板载 32 MiB NOR**（JEDEC `85 23 19`）；驱动 `qspi_ls.c` |
| **`pwm_test`** | 单核 | 否 | 仅验接口可用性；**波形正确性需示波器确认**（报告已备注） |
| ~~`io_probe_test`~~ | 单核 | 否 | **一次性诊断工具**（测板上有哪些 IO 互联），**不在回归套件内** |

> **各工程的权威自述在工程自己那里**：`projects/<工程>/tests/cases.yaml` 的 `meta:` 块
> （`board` / `cores` / `firmware` / `kconfig` / `env` 外部接线需求 / `title` 一句话定位）。
> 本表是人工维护的**总览**；两者不一致时**以工程侧为准**。

**注**：工程全貌与分层（L3 编排 / L1 采集 / L0 契约 / L2 处理）见 [FRAMEWORK_MAP.md](FRAMEWORK_MAP.md)。

外部接线见下表。

### 外部接线记录

| 工程 | 接线 |
|---|---|
| `i3c_dualcore_test` | I3C5_SCL(PJ10) ↔ I3C6_SCL(PJ08)，I3C5_SDA(PJ11) ↔ I3C6_SDA(PJ09)，共地，两侧上拉 |
| `i3c_i2c_switch_test` | 同 `i3c_dualcore_test` |
| `i2c_pure_test` | 同 `i3c_dualcore_test`（I2C 模式复用） |
| `eth_test` | `case_mdio`：**无**（片内 MDIO + 板载 RTL8211F PHY）<br>`case_link`：**板载 RJ45 ↔ PC 网口（网线）**，对端网口须上电且开启自协商 |

---

## 5. 结果判读与调试

ztest 原生输出格式：

```text
ZT_BOOT <core>
Running TESTSUITE <suite>
START - <name>
 PASS - <name> in X.XXX seconds
 FAIL - <name> in X.XXX seconds
PROJECT EXECUTION SUCCESSFUL|FAILED
```

- 单核：全项 `PASS` + 本核 `PROJECT EXECUTION SUCCESSFUL`。
- 双核：同名项两核各自 `PASS` + 两核各自 `SUCCESSFUL`（`i3c_dualcore_test` 例外见 `SPEC_REFERENCE.md` §6.2.1）。

失败时：

1. 看 `--raw` 原始日志中的 `Assertion failed at <file>:<line> (<func>): <msg>`。
2. 串口被刷掉/无输出时，用 `dump_results.py --case <name> --json` GDB 读内存兜底。
3. 连接问题先走 `qsh-debug` skill。

详细判据见 `SPEC_REFERENCE.md` §6.2。

---

## 6. 新增驱动测试工程

1. 在 `projects/` 下新建 `<driver>_test/`，需要时分子工程 `cpuX_xxx/`。
2. cpu2 侧 `CMakeLists.txt` 在 `find_package(Zephyr)` **之前**加入：
   ```cmake
   set(EXTRA_DTC_OVERLAY_FILE
       ${CMAKE_CURRENT_SOURCE_DIR}/../../common/cpu2_console_uart5.overlay)
   ```
3. 在本 README「外部接线记录」中登记接线（片内设备写"无"）。
4. 参考 `qsh-test-gen` skill 生成 case、`run_test.py` 回归脚本、`test.sh` 三模式。

---

## 7. XIP / Flash 固化测试

默认测试为 RAM 加载。需要把固件烧进板载 SPI flash 时：

```bash
python scripts/flash_program.py --bin cpu1.bin@0x08000000 --bin cpu2.bin@0x08100000 --verify
```

- 与 DebugServer 互斥，烧 flash 前需退出 DebugServer。
- 详细用法见 `SPEC_REFERENCE.md` §6.7。

---

## 8. 目录速览

```text
810_driver_test/
├── CLAUDE.md                # 工程规范、调试手册、坑沉淀
├── README.md                # 本文件（工程集门面）
├── common/                  # 公共 overlay、脚本、结果模块
├── projects/                # 各驱动测试工程
├── regression/              # 全量回归入口、工程清单、预检
│   ├── cases.yaml           #   只列工程；case 在各 projects/<工程>/tests/cases.yaml
│   ├── run_all.py
│   └── precheck/
├── reports/                 # 测试报告归档
└── scripts/                 # flash_program 等通用脚本
```
