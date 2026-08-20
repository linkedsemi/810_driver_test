# 810_driver_test — LSQSH 驱动测试工程集

本目录集中存放基于 **LSQSH EVB（双核 AMP，`lsqsh_evb@2os`）** 的驱动测试工程。
所有工程共用同一套硬件环境（板卡、接线、调试串口、GDB 加载方式），新增驱动测试时请遵循本文档约定，避免每个工程重复定义硬件配置。

## 目录结构约定

```text
810_driver_test/
├── README.md                       # 本文档（硬件环境记录）
├── common/                         # 板级通用配置（设备树 overlay / 公共脚本）
│   └── cpu2_console_uart5.overlay  # cpu2 控制台串口配置（所有 cpu2 工程共用）
├── i3c_dualcore_test/              # I3C 双核测试（首个样板工程）
│   ├── cpu0_i3c_controller/        # cpu2 侧固件（I3C 主机）
│   ├── cpu1_i3c_target/            # cpu1 侧固件（I3C 从机）
│   └── tests/                      # 自动化测试（case_*/ + agent）
└── <future_driver>_test/           # 后续驱动测试工程……
```

## 硬件环境

### 板卡与核

| 项 | 值 |
|---|---|
| 板卡 | `lsqsh_evb@2os`（双核双系统 AMP） |
| cpu1（安全核） | `lsqsh_evb@2os/lsqsh/cpu1`，固件运行于 SRAM |
| cpu2（应用核） | `lsqsh_evb@2os/lsqsh/cpu2`，固件加载到 RAM 执行 |

### 调试串口分配（两核各一路，不要混用）

| 核 | UART | TX / RX 引脚 | 波特率 | 配置来源 |
|---|---|---|---|---|
| cpu1 | uart1 | PN04 / PN03 | 115200 8N1 | 板级默认（`lsqsh_evb_cpu1.dts`） |
| cpu2 | **uart5** | **PN14 / PN13** | 115200 8N1 | `common/cpu2_console_uart5.overlay` |

> 注意：cpu2 板级默认控制台是 uart4（PA12/PA13），本目录统一改用 uart5。
> PN14 同时是 cap11 的可选输入脚，启用 `cap` 外设的工程需另行选择串口引脚。

### GDB 加载约定（双核 RAM 执行）

| 项 | 值 |
|---|---|
| GDB server | `localhost:1025`（OpenOCD / JLink，可用 `GDB_SERVER_HOST/PORT` 覆盖） |
| cpu1（target） | `load` ELF，入口 `0x100c0000`（`TARGET_ROM_START`） |
| cpu2（controller） | `restore <zephyr.bin> binary 0x10000000`（`CONTROLLER_LOAD_ADDR`），`set $pc=0x100c0000` |

参考实现：`i3c_dualcore_test/tests/test_common.sh` 的 `load_and_run()`。

### 外设接线记录

| 测试工程 | 接线 |
|---|---|
| i3c_dualcore_test | I3C5_SCL(PJ10) ↔ I3C6_SCL(PJ08)，I3C5_SDA(PJ11) ↔ I3C6_SDA(PJ09)，共地；两侧均配置上拉 |

新增驱动测试工程时，请在此表中追加一行接线说明。

## 新增驱动测试工程 checklist

1. 在本目录下新建 `<driver>_test/`，按需要分 `cpuX_xxx/` 子工程。
2. cpu2 侧工程在 `CMakeLists.txt` 的 `find_package(Zephyr)` **之前**加入：

   ```cmake
   set(EXTRA_DTC_OVERLAY_FILE
       ${CMAKE_CURRENT_SOURCE_DIR}/../../common/cpu2_console_uart5.overlay)
   ```

   这样控制台自动落在 uart5，无需再动 `chosen` 和 pinctrl。
3. cpu1 侧工程无需配置控制台（板级默认 uart1）。
4. 在本 README 的「外设接线记录」表中登记接线。
5. 如需一键回归，参考 `i3c_dualcore_test/tests/` 的 `test_common.sh` + `run_i3c_tests.py` 模式。

## 自动化测试 agent

`i3c_dualcore_test/tests/run_i3c_tests.py` 为双串口 token 采集 + Markdown/JSON 报告的样板：

```bash
export I3C_CTRL_UART=COMx   # cpu2 uart5 (PN14/PN13)
export I3C_TGT_UART=COMy    # cpu1 uart1 (PN04/PN03)
python tests/run_i3c_tests.py
```

后续工程可复用同一套 token 约定（`<PREFIX>:<CORE>:START/PASS/FAIL:<name>` + `<PREFIX>:DONE:<result>`）。
