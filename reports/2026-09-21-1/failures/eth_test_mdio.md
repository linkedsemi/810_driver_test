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

> **本 case 一个测试项都没有采到**（串口 0 行、内存兜底 0 项），故无「实测 vs 预期」对照可言。

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| <无测试项采集到> | 功能通路 / 异常参数 | COVERAGE.md 声明的 5 项（见下表） | 未采集到（串口 0 行，内存兜底 merged 0 item） | 固件 load 成功但串口 0 行：无 banner、无 ZT_BOOT、无 ztest 段落 ⇒ 结果判定退化为「无确定证据」记 FAIL | `eth_test_case_mdio/replay_meta.json:8`（`"lines": 0`）、`logs/eth_test_case_mdio.log:339`、`eth_test_case_mdio/serial_TGT.log`（0 字节） |

### 应采集的 5 项声明（预期来源 `projects/eth_test/tests/COVERAGE.md`，实测全部缺失）

| 测试项 | 维度 | 预期（COVERAGE.md 原文） | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| `mdio_phy_id` | 功能通路 | `PHYID1R == 0x001C`；`PHYID2R[15:8] == 0xC9`（Realtek OUI `0x001CC9`） | 未采集到 | 串口 0 行，token 与内存均无 | `COVERAGE.md:41`；`eth_test_case_mdio/eth_test_report.md:12` |
| `mdio_reg_rw` | 功能通路 | ①连续两次读相等 ②`BMCR.RESET==0` ③`ANAR` 写 `orig^0x0020` 回读相符 ④恢复后回读==原值 | 未采集到 | 同上 | `COVERAGE.md:42`；`eth_test_case_mdio/eth_test_report.md:12` |
| `mdio_invalid_args` | 异常参数 / 边界 | `mdio_read(32,0)==mdio_read(0,0)`；`mdio_read(0,32)==mdio_read(0,0)`；两者 `ret==0` | 未采集到 | 同上 | `COVERAGE.md:43`；`eth_test_case_mdio/eth_test_report.md:12` |
| `eth_dev_ready` | 功能通路 | `device_is_ready(eth1)==true`；`net_if_lookup_by_dev(eth1)!=NULL`；`net_if_get_mtu()==1500`；link addr `len==6`；MAC 非全 0/全 0xFF | 未采集到 | 同上 | `COVERAGE.md:44`；`eth_test_case_mdio/eth_test_report.md:12` |
| `eth_phy_link_state` | 功能通路（弱判据） | `device_is_ready(eth1_phy)==true`；`phy_get_link_state()==0`；INFO 上报 `link/speed_mbps/duplex/speed_code`（本 case `mdio_reset_hold_ms/circuits_set_ms` 应读 `0/0`） | 未采集到 | 同上 | `COVERAGE.md:45,66`；`eth_test_case_mdio/eth_test_report.md:12` |

## 归因

**结论：不是断言级失败，而是「证据缺失」——固件上电后串口 0 行、内存兜底哨兵未置位，本轮 FAIL 是判定框架对「无确定证据」的兜底记分（`exec=UNKNOWN`、`items=[]` ⇒ 分子分母同为 0）。无法归因到具体驱动分支。**

**「未采集到」的确切机制**（逐条附证据）：

1. **构建成功、烧录成功。** `logs/eth_test_case_mdio.log:336` 有 `Start address 0x10000000, load size 200702`，`:338` `Continuing.` ⇒ GDB 已把整包 ELF 写入并从入口 `0x10000000` 放行，**不是烧录失败或入口设错**（入口与单核 1os 约定一致）。
2. **放行后串口一行都没有。** `serial_TGT.log` 为 **0 字节**；`replay_meta.json:8` 明确记录 `"lines": 0`，端口 `COM16`（`:7`）。firmware 的**第一行**本应是 `main.c:15` 的 `LOG_INF("[CPU1 ETH] Ethernet test started")`，随后才是 ztest 的 `ZT_BOOT` / TESTSUITE 段落——**连首行都没有**（全日志 grep `ZT_BOOT|ztest|PROJECT EXECUTION|SUCCESSFUL` 无命中，`logs/eth_test_case_mdio.log`）。
3. **内存兜底也被拒绝，不能反证为「全 PASS」。** `logs/eth_test_case_mdio.log:339`：`[mem] mem_state=invalid … merged 0 item(s)`。即 `dump_results.py` 读到的 `test_results_valid` **符号存在但未置位** ⇒ 固件**没跑到 `tr_init`**（停在内核初始化 / `POST_KERNEL` / 早期崩溃），`test_results[]` 仍是 `.bss` 零值（零值语义 = TR_PASS）⇒ 兜底被判无效、不并入证据（`run_test.py` 的独立性门：`mem_state==ok` 且见 `ZT_BOOT` 方采信）。
4. **采集侧已排除。** 同一会话、同一 `COM16` 端口的其它单核 case 均正常采到输出：`flash_test_case_default` 50 行、`trng_test_case_default` 68 行、`adc_temper_test_case_default` 56 行、`wdt_test_case_non_destructive` 30 行（各自 `replay_meta.json` 的 `"lines"` 与 `serial_TGT.log` 行数一致）⇒ **端口/波特率/采集链路工作正常**，0 行是固件真的没吐字，而非采集工具没开或开错口。

**候选根因（按 KNOWN_ISSUES 优先级，均未证实）：**

- **首选：PHY 复位无超时自旋挂死（KNOWN_ISSUES #71）——症状吻合并有源码佐证，但缺 PC/backtrace 实证。** 依据链：
  - `KNOWN_ISSUES.md:171-180`：带 eth1 的工程 load 后串口 0 行 ⇒ **优先怀疑** `phy_realtek_rtl8211f_reset()` 的 `do{read}while` 无超时自旋在 `POST_KERNEL` 永久挂死，固件不启动、串口 0 行；**判据是 gdb attach 读 PC，栈落在该自旋循环内即确认**。
  - 本板上该路径**必经**：板级 `zephyr/boards/linkedsemi/lsqsh_evb/lsqsh_evb_cpu1.dts:264-276` 把 `&eth1`/`&mdio1`/`eth1_phy(realtek,rtl8211f)` 全部 `status="okay"`，`eth1_phy` 带 `reset-gpios=<&gpiog 1 …>`；`mdio_dwmac` 的 PHY 复位端口取自 mdio1 直接子节点 `eth1_phy` ⇒ 上电必经复位段（`COVERAGE.md:121`）。
  - 源码现状**仍无超时**：`zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c:156-162`（等 `MII_BMCR_RESET` 自清）与 `:168-174`（等 `PHYID1R==0x1C`）均为无界 `do{…}while`，全文件无 `k_timepoint` / 超时宏（grep `k_timepoint|TIMEOUT|ETIMEDOUT` 仅命中该两处 `do/while`）⇒ #71 描述的缺陷在**当前工作树上依然存在**。
  - ⚠️ **未证实**：`projects/eth_test/tests/dbg_gdb_last.log` 整个捕获 **止于 `:166` `Continuing.`**，全程没有 `$pc` / backtrace / `info registers` 记录 ⇒ 只是「症状与已知根因吻合」，**没有栈落在自旋循环内的直接证据**。
- **次选（同症状异根因族，#38/#39/#40/#56/#57）：崩溃 / `$pc` 设错 / overlay 缺 `chosen`。** 其中「入口设错（#57）」已由 `:336` 的 `Start address 0x10000000` 排除；「cpu2 未 hold（#39）」不适用（本工程单核，无 cpu2）。**overlay 缺 `chosen zephyr,console`（#40 同族）** 与**早期 load/store fault 崩溃**两者**均未排除**——固件 console 若编译期为 NULL，也会表现为「load 正常 + 串口 0 行」。

**还缺的证据（判定到单一根因所必需）**：① 挂死瞬间 gdb attach 读 `$pc` + backtrace（若落在 `phy_rt_rtl8211f_reset` 自旋内即锁定 #71）；② `readelf`/生成 `zephyr.dts` 核对 `chosen { zephyr,console }` 是否指向 uart1（排除 #40 族）；③ 独立确认 COM16 对应的 uart1 引脚与板载 RTL8211F / MDIO 硬件在位且上电（本次两 case 芯片均 0 行，不排除 PHY 器件本身异常）。

**关于 `执行标记=UNKNOWN`（非 SUCCESSFUL）：** 按规则须点明这可能是**采集截断/无采**而非真实断言失败。此处正是如此——`exec=UNKNOWN` + `items=[]`，**不是**采到一半被截（区间 0 字节，从头即无输出），也**不是**任何一项 `zassert` 触发。因此「FAIL (0/0)」**不可**解读为驱动某分支被证伪。

## 备注

- **本轮是 eth_test 的首次上板**：`COVERAGE.md:4` 声明本工程此前处于「只生成 + 编译通过」阶段，全部 5 项状态 ⏳（待 P6 首次实测）⇒ 这是一次首测即 0 行的上板问题，而非回归退化。
- **文档与源码不一致（需核实）**：`COVERAGE.md:200` 声称 PHY 自旋 P0「已修 2026-09-18」（加 `k_timepoint` 超时），但当前 `zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c:156-174` **无任何超时**，与 `KNOWN_ISSUES.md:171-184`（#71 标为未修）一致、与 COVERAGE 的「已修」相矛盾。
- **同族相关**：`KNOWN_ISSUES.md:200-206`（#73，`mdio_dwmac.c` 丢弃 PHY 复位返回值 → PHY 失效被误归因为「PHY ID 不符」）——若本次能起来，该缺陷会污染 `mdio_phy_id` 的归因顺序，排查时留意。
- **归档路径小异常（不影响结论）**：`logs/eth_test_case_mdio.log` 末行 `[run] archived -> …reports\2026-09-21-2\…`，但磁盘上**不存在** `reports/2026-09-21-2/`；被分析的报告位于 `reports/2026-09-21-1/`（`replay_meta.json` 时间戳 `14:44:48`，与报告头部日期一致）。以 `replay_meta.json` 的归档数据为准（`lines: 0`）。
