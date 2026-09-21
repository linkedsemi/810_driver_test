# eth_test — case_phy_reset 失败分析

| 项 | 值 |
|---|---|
| 工程 | eth_test（单核，`lsqsh_evb@1os/lsqsh/cpu1`，入口 `0x10000000`） |
| case | case_phy_reset |
| 被测驱动 | `zephyr/drivers/mdio/mdio_dwmac.c`（复位脉冲段 `:231-259` 为本案新增覆盖）；旁及 `zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c`、`eth_dwmac*.c` |
| 报告 | [eth_test_report.md](../eth_test_case_phy_reset/eth_test_report.md) |
| 结果 | FAIL (0/0) |
| 执行标记 | CTRL=—（单核工程无 CTRL 核）, TGT=UNKNOWN |
| 内存兜底 | 无效（哨兵未置位）—— `test_results_valid` 符号存在但未置位 |

## 失败项明细

> **本 case 一个测试项都没有采到**（串口 0 行、内存兜底 0 项），故无「实测 vs 预期」对照可言。

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| <无测试项采集到> | 功能通路 / 异常参数 | COVERAGE.md 声明的 5 项（见下表），**与 case_mdio 同源同断言** | 未采集到（串口 0 行，内存兜底 merged 0 item） | 固件 load 成功但串口 0 行：无 banner、无 ZT_BOOT、无 ztest 段落 ⇒ 判定退化为「无确定证据」记 FAIL | `eth_test_case_phy_reset/replay_meta.json:8`（`"lines": 0`）、`logs/eth_test_case_phy_reset.log:1591`、`eth_test_case_phy_reset/serial_TGT.log`（0 字节） |

### 应采集的 5 项声明（预期来源 `projects/eth_test/tests/COVERAGE.md`，实测全部缺失）

| 测试项 | 维度 | 预期（COVERAGE.md 原文） | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| `mdio_phy_id` | 功能通路 | `PHYID1R == 0x001C`；`PHYID2R[15:8] == 0xC9` | 未采集到 | 串口 0 行，token 与内存均无 | `COVERAGE.md:41,53`；`eth_test_case_phy_reset/eth_test_report.md:12` |
| `mdio_reg_rw` | 功能通路 | ①连续两次读相等 ②`BMCR.RESET==0` ③`ANAR` 写 `orig^0x0020` 回读相符 ④恢复后回读==原值 | 未采集到 | 同上 | `COVERAGE.md:42,53`；`eth_test_case_phy_reset/eth_test_report.md:12` |
| `mdio_invalid_args` | 异常参数 / 边界 | `mdio_read(32,0)==mdio_read(0,0)`；`mdio_read(0,32)==mdio_read(0,0)`；两者 `ret==0` | 未采集到 | 同上 | `COVERAGE.md:43,53`；`eth_test_case_phy_reset/eth_test_report.md:12` |
| `eth_dev_ready` | 功能通路 | `device_is_ready(eth1)==true`；`net_if_lookup_by_dev(eth1)!=NULL`；`net_if_get_mtu()==1500`；link addr `len==6`；MAC 非全 0/全 0xFF | 未采集到 | 同上 | `COVERAGE.md:44,53`；`eth_test_case_phy_reset/eth_test_report.md:12` |
| `eth_phy_link_state` | 功能通路（弱判据） | `device_is_ready(eth1_phy)==true`；`phy_get_link_state()==0`；INFO 上报 `link/speed_mbps/duplex/speed_code`；**本 case 的 INFO 应含 `mdio_reset_hold_ms=10` / `mdio_circuits_set_ms=30`**（变体自识别，`case_mdio` 为 `0/0`） | 未采集到 | 同上 | `COVERAGE.md:45,65-66`；`eth_test_case_phy_reset/eth_test_report.md:12` |

## 归因

**结论：与 case_mdio 完全同型的「证据缺失」，不是断言级失败。** 固件上电后串口 0 行、内存兜底哨兵未置位，FAIL 是判定框架对「无确定证据」的兜底记分（`exec=UNKNOWN`、`items=[]` ⇒ 0/0）。**无法归因到本案专门覆盖的复位脉冲分支，也无法归因到任何一项 `zassert`。**

**「未采集到」的确切机制**（逐条附证据）：

1. **构建成功、烧录成功。** `logs/eth_test_case_phy_reset.log:1588` `Start address 0x10000000, load size 200700`、`:1590` `Continuing.` ⇒ 已从入口 `0x10000000` 放行，**不是烧录失败或入口设错**。（本 case 为全量重建——`:26-27` 有 `Configuration saved to` / `Kconfig header saved to`，故该日志体量 1596 行远大于 case_mdio 的 344 行，**是编译输出多，不是串口输出多**。）
2. **放行后串口一行都没有。** `serial_TGT.log` 为 **0 字节**；`replay_meta.json:8` 记录 `"lines": 0`，端口 `COM16`（`:7`）。固件首行本应是 `cpu1_eth_test/src/main.c:15` 的 `LOG_INF("[CPU1 ETH] Ethernet test started")`，随后才是 ztest 的 `ZT_BOOT`/TESTSUITE 段落——**连首行都没有**（全日志 grep `ZT_BOOT|ztest|PROJECT EXECUTION|SUCCESSFUL` 无命中）。
3. **内存兜底也被拒绝。** `logs/eth_test_case_phy_reset.log:1591`：`[mem] mem_state=invalid … merged 0 item(s)`。`test_results_valid` **符号存在但未置位** ⇒ 固件**没跑到 `tr_init`**（停在 `POST_KERNEL` / 早期崩溃），`test_results[]` 仍为 `.bss` 零值（零值语义 = TR_PASS）⇒ 兜底按独立性门被判无效（须 `mem_state==ok` 且见 `ZT_BOOT` 方采信），**不可当全 PASS**。
4. **采集侧已排除。** 同一会话、同一 `COM16` 端口，其它单核 case 正常采到输出：`flash_test_case_default` 50 行、`trng_test_case_default` 68 行、`adc_temper_test_case_default` 56 行、`wdt_test_case_non_destructive` 30 行（各自 `replay_meta.json` 与 `serial_TGT.log` 行数一致）⇒ 端口/采集链路工作正常，0 行是固件真没吐字。

**候选根因（均未证实）：**

- **首选：PHY 复位无超时自旋挂死（KNOWN_ISSUES #71）——症状吻合，且本案比 case_mdio 更「高概率」命中，但同样缺 PC 实证。** 依据链：
  - `KNOWN_ISSUES.md:171-180`：带 eth1 的工程 load 后串口 0 行 ⇒ 优先怀疑 `phy_realtek_rtl8211f_reset()` 的无界自旋挂死；**判据 = gdb attach 读 PC 栈落在自旋内**。
  - **本案的 overlay 恰好放大了触发面**：`tests/case_phy_reset/target.overlay` 给 `&mdio1` 补 `reset-hold-ms=10` / `circuits-set-ms=30`，使 `mdio_dwmac_ethphy_reset()` 的**脉冲子块**（`mdio_dwmac.c:231-244`/`:252-259`）在本板上电时真正执行（`COVERAGE.md:53-66,122`）⇒ 复位脚 `GPIOG pin1` 会实拉低 10 ms。若该脉冲把 PHY 打挂或未按时释放，紧接着的 PHY 设备 `init`（优先级 70，同一 GPIOG pin1，`case_phy_reset/test.sh` 头注释）会用自己的复位序列再次复位，其尾段正是 `phy_realtek_rtl8211f.c:168-174` 的 `do{read PHYID1R}while(!=0x1C)`——PHY 不应答即在 `POST_KERNEL` **永久挂死、串口 0 行**。
  - 源码现状**仍无超时**：`zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c:156-162` 与 `:168-174` 均为无界 `do{…}while`，全文件无 `k_timepoint`/超时宏。
  - ⚠️ **未证实**：`projects/eth_test/tests/dbg_gdb_last.log` 整段捕获止于 `:166` `Continuing.`，**无 `$pc`/backtrace** ⇒ 「疑似」而非「确认」。
  - ⚠️ **反证点**：**case_mdio（不发脉冲）同样 0 行**，说明挂死与本案的复位脉冲**非唯一相关**——若两 case 同根因，则更偏向「PHY/MDIO 本就无应答」，而非「脉冲把 PHY 打挂」；本案 overlay 是否**加剧**了挂死，目前无证据区分。
- **次选（同症状异根因族 #38/#39/#40/#56/#57）：早期崩溃 / overlay 缺 `chosen zephyr,console`（console 编译期为 NULL ⇒ 同样「load 正常 + 串口 0 行」）。** 入口设错（#57）已由 `:1588` 排除；cpu2 未 hold（#39）不适用（单核）。**两条均未排除。**

**还缺的证据（判定到单一根因所必需）**：① 挂死瞬间 gdb attach 读 `$pc` + backtrace（落在 `phy_rt_rtl8211f_reset` 自旋内 = 锁定 #71）；② 核对生成 `zephyr.dts` 的 `chosen { zephyr,console }`（排除 #40 族）；③ 示波器量 `GPIOG pin1` 复位电平（本案 overlay 专测项，`COVERAGE.md:68-70` 已声明固件内不可观测）；④ 确认板载 RTL8211F 硬件在位/上电（两 case 均 0 行，不排除器件异常）。**在拿到 ① 或 ② 之前，本 case 的「复位脉冲路径冒烟」既不成立也不证伪——本轮没有任何该分支的执行证据。**

**关于 `执行标记=UNKNOWN`（非 SUCCESSFUL）：** 按规则须点明这可能是**采集截断/无采**而非真实断言失败。此处即为**从头无采**（0 字节，非中途截断），无任何 `zassert` 触发 ⇒ 「FAIL (0/0)」**不可**解读为复位脉冲分支有缺陷。

## 备注

- **本轮是 eth_test 的首次上板**（`COVERAGE.md:4`：此前仅「生成 + 编译通过」，全部项 ⏳）。`case_phy_reset` 是首次上板即 0 行，非回归退化。
- **本案与 case_mdio 是同一份源码 + 仅 overlay 不同的孪生 case**：`run_test.py` 的 `TEST_NAMES_BY_CASE` 两 case 列同一套 5 项、链接序相同；两 case 在本会话均 0 行 ⇒ **合并看更支持「PHY 侧/早期启动的系统性问题」，而非单个 case 的 overlay 差异**。
- **驱动审核 P0 状态需核实**：`COVERAGE.md:199-208` 声称本案相关的两处 P0（PHY 自旋加超时、`mdio_dwmac.c:274-276` 复位返回值）已于 2026-09-18 修复；但当前 `phy_realtek_rtl8211f.c:156-174` **仍无超时**，与 `KNOWN_ISSUES.md:171-184` 的「未修」标记一致，与 COVERAGE 的「已修」矛盾。另 `KNOWN_ISSUES.md:200-206`（#73）与本案脉冲路径同源，若日后固件能起来需留意其误归因风险。
- **归档路径小异常（不影响结论）**：`logs/eth_test_case_phy_reset.log` 末行 `[run] archived -> …reports\2026-09-21-1\…`（与本次归档一致）；`replay_meta.json` 时间戳 `2026-09-21T13:23:00` 与报告头部日期一致，以 `replay_meta.json` 的 `lines: 0` 为准。
