# eth_test — case_mdio 失败分析

| 项 | 值 |
|---|---|
| 工程 | eth_test（单核，`lsqsh_evb@1os/lsqsh/cpu1`，入口 `0x10000000`） |
| case | case_mdio |
| 被测驱动 | `zephyr/drivers/mdio/mdio_dwmac.c`（主）；旁及 `zephyr/drivers/ethernet/eth_dwmac.c` / `eth_dwmac_linkedsemi.c` / `zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c` |
| 报告 | [eth_test_report.md](../eth_test_case_mdio/eth_test_report.md) |
| 结果 | FAIL (0/0) |
| 执行标记 | CTRL=—（单核工程无 CTRL 核）, TGT=UNKNOWN |
| 内存兜底 | 无效（哨兵未置位）—— `test_results_valid` 符号存在但未置位 |

## 失败项明细

> **本 case 一个测试项都没有采到**（串口 **0 字节**、内存兜底 0 项）。原因**已知**（非「待补」）：固件挂在早期初始化（`POST_KERNEL`），ztest 从未启动（见下方归因）；`serial_TGT.log` 为 0 字节是因**挂死发生在日志线程有机会 flush 之前**，不是采集链路问题。故无「实测 vs 预期」对照可言。

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| <无测试项采集到> | 功能通路 / 异常参数 | COVERAGE.md 声明的 5 项（见下表） | 未采集到（串口 **0 字节**，内存兜底 merged 0 item） | **固件挂死**（非断言级失败）：`phy_rt_rtl8211f_reset()` 的 PHYID1R 无界自旋在 **POST_KERNEL** 永久挂起，`LOG_MODE_DEFERRED` 的日志线程拿不到调度 ⇒ 串口 0 字节。判定退化为「无确定证据」记 FAIL | 挂死栈 / 寄存器（实测）：`tmp/ethpc/CONCLUSION.md`、`probe2.out`（r8/r45 两轮）、`session2_attach.log`；串口 0 字节：`eth_test_case_mdio/replay_meta.json:8`（`"lines": 0`）、`logs/eth_test_case_mdio.log:339`、`eth_test_case_mdio/serial_TGT.log`（0 字节） |

### 应采集的 5 项声明（预期来源 `projects/eth_test/tests/COVERAGE.md`，实测全部缺失）

> 全部 5 项的「未采集到」同一个机制：固件挂死在 `POST_KERNEL`（`z_sys_init_run_level`，见归因），ztest 从未启动。`serial_TGT.log` 0 字节 = 挂死早于日志线程 flush。

| 测试项 | 维度 | 预期（COVERAGE.md 原文） | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| `mdio_phy_id` | 功能通路 | `PHYID1R == 0x001C`；`PHYID2R[15:8] == 0xC9`（Realtek OUI `0x001CC9`） | 未采集到（串口 0 字节）。**实测挂死点即该次 PHYID1R 读，读回 `0xFFFF`（≠ `0x001C`）** | 固件读到本项前已挂在复位尾段自旋；即便不挂，本项也会因 `0xFFFF` 判失败 | `CONCLUSION.md`；`MAC_MDIO_ADDRESS=0x0002060c` / `MAC_MDIO_DATA=0x0000ffff`（`probe2.out`）；`COVERAGE.md:41` |
| `mdio_reg_rw` | 功能通路 | ①连续两次读相等 ②`BMCR.RESET==0` ③`ANAR` 写 `orig^0x0020` 回读相符 ④恢复后回读==原值 | 未采集到（串口 0 字节）—— 未运行到本项 | 挂死在 `POST_KERNEL`，ztest 从未启动 | 栈 `#9 = z_sys_init_run_level(INIT_LEVEL_POST_KERNEL)`（`CONCLUSION.md`）；`COVERAGE.md:42` |
| `mdio_invalid_args` | 异常参数 / 边界 | `mdio_read(32,0)==mdio_read(0,0)`；`mdio_read(0,32)==mdio_read(0,0)`；两者 `ret==0` | 未采集到（串口 0 字节）—— 未运行到本项 | 同上 | `serial_TGT.log` 0 字节；`COVERAGE.md:43` |
| `eth_dev_ready` | 功能通路 | `device_is_ready(eth1)==true`；`net_if_lookup_by_dev(eth1)!=NULL`；`net_if_get_mtu()==1500`；link addr `len==6`；MAC 非全 0/全 0xFF | 未采集到（串口 0 字节）—— 未运行到本项 | 同上 | `serial_TGT.log` 0 字节；`COVERAGE.md:44` |
| `eth_phy_link_state` | 功能通路（弱判据） | `device_is_ready(eth1_phy)==true`；`phy_get_link_state()==0`；INFO 上报 `link/speed_mbps/duplex/speed_code`（本 case `mdio_reset_hold_ms/circuits_set_ms` 应读 `0/0`） | 未采集到（串口 0 字节）—— 未运行到本项 | 同上。**注**：本 case overlay 把 `reset-hold-ms`/`circuits-set-ms` 设为 `0/0` ⇒ 跳过脉冲子块（只走入口段），**仍 0 字节** | `serial_TGT.log` 0 字节；`COVERAGE.md:45,66`；`CONCLUSION.md` |

## 归因

**结论（2026-09-21 已定位，取代本文件早前「无法归因到单一根因」的写法）：固件挂死在 `phy_rt_rtl8211f_reset()` 的 PHYID1R 无界自旋内，属 POST_KERNEL 早期挂死 ⇒ 串口 0 字节。「FAIL (0/0)」是判定框架对「无确定证据」的兜底记分；其背后是一次**确定性的固件挂死**，已用挂死瞬间的栈与 MDIO 寄存器坐实（完整证据链见 `tmp/ethpc/CONCLUSION.md`；台账版见 `OPEN_ITEMS.md` O-26）。**

### 实测现场（GDB attach 抓 `$pc` + 栈）

```
#0  mtime ()                                        riscv_machine_timer.c:168
#1  sys_clock_cycle_get_32 ()                       riscv_machine_timer.c:240
#2  arch_k_cycle_get_32 ()                          arch/riscv/arch.h:284
#3  k_cycle_get_32 ()                               kernel.h:1902
#4  z_impl_k_busy_wait (0x64)                       busy_wait.c:27
#5  mdio_dwmac_transfer (op=MDIO_OP_C22_READ)        drivers/mdio/mdio_dwmac.c:177
#6  phy_rt_rtl8211f_reset (dev=...)                  drivers/ethernet/phy/phy_realtek_rtl8211f.c:169
...
#9  z_sys_init_run_level (INIT_LEVEL_POST_KERNEL)
```

- **实测（非推断）**：`$pc` 落在固件地址空间内，栈 `#6 = phy_rt_rtl8211f_reset`，`#9 = z_sys_init_run_level(INIT_LEVEL_POST_KERNEL)` ⇒ **挂在 POST_KERNEL**。
- **实测**：**r8（跑 8s）与 r45（跑 45s）两轮栈一致 ⇒ 持久态**，非瞬时抖动。
- **行号修正**：板级 dts 给了 `reset-gpios`（`lsqsh_evb_cpu1.dts:274`）⇒ `phy_rt_rtl8211f_reset()` 走 **GPIO 复位分支**，`:141` 直接 `goto finalize_reset` ⇒ **`:156-162` 的 BMCR 自旋被跳过**；实际卡的是 **`:168-174` 的 PHYID1R 自旋**（本文件旧文里 `:166-174` 的提法按此修正）。

### MDIO 寄存器实测（挂死瞬间）

| 寄存器 | 地址 | 值 |
|---|---|---|
| `MAC_MDIO_ADDRESS` | `0x40064200` | `0x0002060c` |
| `MAC_MDIO_DATA` | `0x40064204` | `0x0000ffff` |

解码（`mdio_dwmac.c:74-91` 位域）：`GB=0`（**事务已完成**，非总线忙）/ `C45E=0`（C22）/ `GOC=0b11`（C22 读）/ `RDA=2`（PHYID1R ✅）/ `PA=0`（`ethernet-phy@0` ✅）⇒ **驱动侧寻址完全正确，是 PHY 不应答**（`GD = 0xFFFF`）。

### 直接根因

`phy_realtek_rtl8211f.c` 两处自旋**无超时**：`:162` `while (reg_val & MII_BMCR_RESET)`、`:174` `while (reg_val != REALTEK_OUI_MSB)`——本次卡在 `:174` 的 PHYID1R 自旋。

**补充（实测）**：`mdio_dwmac_transfer` 自身是**有界**的（3 × `k_busy_wait(100us)` → `-EIO`）⇒ 能挂 20s+ 只可能来自 **read 成功返回但值 ≠ `0x1C`**（若 read 报错会 `return ret` 退出），与 MDIO 寄存器 `GB=0` 相互印证。

**文档漂移**：`projects/eth_test/tests/COVERAGE.md:200` 声称这两处已于 2026-09-18 加 `k_timepoint` 超时（`PHY_RT_RTL8211F_RESET_TIMEOUT_MS = 1000`），但 **2026-09-21 复核：该修复不在代码树里**（符号 `PHY_RT_RTL8211F_RESET_TIMEOUT_MS` 全仓只存在于该 COVERAGE 文档中；源码现状仍无任何超时）——见 `OPEN_ITEMS.md` O-16 / `KNOWN_ISSUES #108`。

### 未解决（需硬件 / 驱动侧判断，**非本报告结论**）

**PHY 为什么不应答？** 本报告只证明「不应答」，**未证明「为什么」**——这是**开放项**，已交驱动工程师。候选（均未验证）：PHY 供电 / 复位脚（`reset-gpios = <&gpiog 1>`）状态、MDIO 引脚 pinmux / 上拉、时钟分频。

> 本 case（`case_mdio`）的 overlay 把 `reset-hold-ms` / `circuits-set-ms` 设为 `0/0` ⇒ 会**跳过** `mdio_dwmac_ethphy_reset()` 的脉冲子块（只走入口段）；孪生 case `case_phy_reset` 设为 `10/30` 会**真发脉冲**。**两者都是 0 字节** ⇒ 两种配置下 PHY 均无应答。

**「未采集到」的确切机制**（逐条附证据，仍成立）：

1. **构建成功、烧录成功。** `logs/eth_test_case_mdio.log:336` 有 `Start address 0x10000000, load size 200702`，`:338` `Continuing.` ⇒ GDB 已把整包 ELF 写入并从入口 `0x10000000` 放行，**不是烧录失败或入口设错**（入口与单核 1os 约定一致）。
2. **放行后串口一行都没有。** `serial_TGT.log` 为 **0 字节**；`replay_meta.json:8` 明确记录 `"lines": 0`，端口 `COM16`（`:7`）。firmware 的**第一行**本应是 `main.c:15` 的 `LOG_INF("[CPU1 ETH] Ethernet test started")`，随后才是 ztest 的 `ZT_BOOT` / TESTSUITE 段落——**连首行都没有**（全日志 grep `ZT_BOOT|ztest|PROJECT EXECUTION|SUCCESSFUL` 无命中，`logs/eth_test_case_mdio.log`）。**原因已知**：挂死发生在早期初始化，`LOG_MODE_DEFERRED` 的日志线程没机会 flush。
3. **内存兜底也被拒绝，不能反证为「全 PASS」。** `logs/eth_test_case_mdio.log:339`：`[mem] mem_state=invalid … merged 0 item(s)`。即 `dump_results.py` 读到的 `test_results_valid` **符号存在但未置位** ⇒ 固件**没跑到 `tr_init`**（现已实测确认：停在 `POST_KERNEL`，见上），`test_results[]` 仍是 `.bss` 零值（零值语义 = TR_PASS）⇒ 兜底被判无效、不并入证据（`run_test.py` 的独立性门：`mem_state==ok` 且见 `ZT_BOOT` 方采信）。
4. **采集侧已排除。** 同一会话、同一 `COM16` 端口的其它单核 case 均正常采到输出：`flash_test_case_default` 50 行、`trng_test_case_default` 68 行、`adc_temper_test_case_default` 56 行、`wdt_test_case_non_destructive` 30 行（各自 `replay_meta.json` 的 `"lines"` 与 `serial_TGT.log` 行数一致）⇒ **端口/波特率/采集链路工作正常**，0 字节是固件真的没吐字，而非采集工具没开或开错口。

**关于 `执行标记=UNKNOWN`（非 SUCCESSFUL）：** `exec=UNKNOWN` + `items=[]`，**不是**采到一半被截（区间 0 字节，从头即无输出），也**不是**任何一项 `zassert` 触发。其本质是**固件挂死导致的无采**（现已定位，见上）。因此「FAIL (0/0)」**不可**解读为驱动某分支被证伪。

## 备注

- **本轮是 eth_test 的首次上板**：`COVERAGE.md:4` 声明本工程此前处于「只生成 + 编译通过」阶段，全部 5 项状态 ⏳（待 P6 首次实测）⇒ 这是一次首测即 0 行的上板问题，而非回归退化。
- **文档与源码不一致（2026-09-21 已复核坐实）**：`COVERAGE.md:200` 声称 PHY 自旋 P0「已修 2026-09-18」（加 `k_timepoint` 超时，`PHY_RT_RTL8211F_RESET_TIMEOUT_MS = 1000`），但该修复**不在代码树里**——符号 `PHY_RT_RTL8211F_RESET_TIMEOUT_MS` 全仓只存在于该 COVERAGE 文档中；当前 `zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c:156-174` **无任何超时**，与 `KNOWN_ISSUES #108` / `OPEN_ITEMS` O-16 一致。疑为未提交改动被 09-21 的 `checkout`/`reset` 冲掉。
- **同族相关**：`KNOWN_ISSUES.md:200-206`（#73，`mdio_dwmac.c` 丢弃 PHY 复位返回值 → PHY 失效被误归因为「PHY ID 不符」）——若本次能起来，该缺陷会污染 `mdio_phy_id` 的归因顺序，排查时留意。
- **归档路径小异常（不影响结论）**：`logs/eth_test_case_mdio.log` 末行 `[run] archived -> …reports\2026-09-21-2\…`，但磁盘上**不存在** `reports/2026-09-21-2/`；被分析的报告位于 `reports/2026-09-21-1/`（`replay_meta.json` 时间戳 `14:44:48`，与报告头部日期一致）。以 `replay_meta.json` 的归档数据为准（`lines: 0`）。

---

> **本轮复核（2026-09-22-2）**：失败项集合与本文件（源自 2026-09-21-1）**逐项一致**（自动比对报告 JSON 通过），结果数值亦一致 ⇒ 分析沿用，未复现新变化。
