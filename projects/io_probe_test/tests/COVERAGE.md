# IO 互联探测（io_probe_test）测试覆盖矩阵

> **工程定位**：不是"驱动功能测试"，而是**测量工具**。它回答的唯一问题是：
> 候选 IO 之间**实际**哪些是导通的。产出 = 互联矩阵 + 连线清单 + 声称对照表。
>
> **判据纪律**：只有"测量方法本身是否有效"才判 FAIL（`pin_controllable` /
> `positive_control_i3c_link`）。"用户声称的接线测不出来"**不判 FAIL** ——
> 那正是本工程要交付的信息，以串口里的 `IOPROBE CLAIM ...` 行为准。
>
> **状态**：✅/❌ 由实机回归回填（`reports/2026-09-18-6` / `-7`，2026-09-18 修复轮）。

---

## 1. 测试项（覆盖矩阵）

| # | 测试项（ZTEST `io_probe::<name>`） | 触达代码路径 | 维度 | 预期数据 | 预期来源 | 状态 |
|---|-----------------------------------|-------------|------|---------|---------|------|
| 1 | `pin_controllable` | `gpio_lsqsh.c` `gpio_pin_configure()`（IN\|PULL_UP / OUTPUT_LOW 两条）+ `port_get_raw()` 读回 | 功能通路 | 75 根脚全部 `cfg_ok=1` 且 `controlled=1`（驱动低后自身读回为低）；`not_controlled=0` | 驱动源码（直接写 OE_DIN / IEN1_IEN0，不依赖 pinctrl）+ 板级复位态为 GPIO | ❌ `not_controlled=2`（PH12 / PG07，见 §7.2） |
| 1b | `pin_restorable` | `gpio_pin_configure(GPIO_DISCONNECTED)` + `INPUT\|PULL_UP`（还原序列）+ 读回轮询 | 测量有效性（**硬判据**，首跑缺陷 A） | 基线为高的脚驱动后**必须还原回高**；`not_restored=0` | `ls_soc_gpio.c::io_cfg_input()` 有缺陷（清 `OE_DIN` 低半区，OE 高半区不清）⇒ 必须先 `GPIO_DISCONNECTED` 再配输入；见 §5 不变量 5 | ✅ `not_restored=0`，实测 `restore_us_max=22µs` |
| 2 | `positive_control_i3c_link` | 同上（矩阵单元 `[PJ10][PJ08]`、`[PJ11][PJ09]`） | 测量有效性（**硬判据**） | 两对均为 `IO_PROBE_LINK`（值 2） | `810_driver_test/README.md`「外部接线记录」：`I3C5_SCL(PJ10) ↔ I3C6_SCL(PJ08)`、`I3C5_SDA(PJ11) ↔ I3C6_SDA(PJ09)`，且 `i3c_dualcore_test` 多轮 PASS 佐证该跳线在位 | ✅ |
| 3 | `symmetry_report` | 矩阵对称性遍历 | 统计性质 | `asymmetric_pairs <= links`（不对称**只报不判死**：中间可能有二极管/驱动器） | 方法学：对称阵是纯导线互连的充要表现；不对称即异常，需单独归因 | ✅ |
| 4 | `claim_i2c234_bus` | 矩阵单元 `{PB04,PB15,PE02}` 两两、`{PB01,PB14,PB00}` 两两 | 用户声称对照 | **不预设**。期望 `verdict=CONNECTED`（用户口述"i2c2/i2c3/i2c4 的 IO 接在一起"），若为 PARTIAL / NOT_FOUND 即为**本次要交付的结论** | 用户口述（原话，未证实） | ✅ |
| 5 | `claim_spi_spif_interconnect` | 矩阵中 SPI 组（21 脚）× SPIF 组（40 脚）的 LINK 计数 | 用户声称对照 | **不预设**。期望 `cross_links >= 1`（用户口述"SPI 和 SPIF 的 IO 接在一起"） | 用户口述 + 参考样例 `modules/lib/pfr_tpm/samples/spi_master_communication` | ✅ |
| 6 | `same_pin_groups` | 静态声明表 `same_pin_groups[][]` | 报告完整性（区分"同脚"与"跳线"） | `dt_same_pad_pairs=16`；报告里逐条打印 `IOPROBE SAME_PIN a == b` | 本文件 §2「同脚复用清单」（DT 固有） | ✅ |
| 7 | `dump_full_matrix` | 报告层（同脚声明 / 图例 / 矩阵 / 连线清单 / 异常） | 报告完整性 | `pins=75`；输出含 `DT-INHERENT SAME-PAD PAIRS`、`PIN LEGEND`、`COLS`+`ROW` 矩阵块、`LINK` 清单、`MATRIX KEY` | 本文件 §2 引脚表 | ✅ |

**为什么"声称对照"不判 FAIL**：测量工具的首要职责是**报告真相**。若把
"实测与口述不符"做成 FAIL，回归门禁会长期飘红，最容易被后续 person 用放宽断言
的方式"修"掉，反而把真相抹掉。故这两项永远 PASS，结论走 `LOG_INF` 的
`IOPROBE CLAIM ... -> CONNECTED|PARTIAL|NOT_FOUND` 与 `ZT_INFO` 行。

---

## 2. 候选引脚表（75 根物理脚）

来源：生效 DTS。板级 `zephyr/boards/linkedsemi/lsqsh_evb/lsqsh_evb_pinctrl.dtsi`、
模块 `modules/lib/pfr_tpm/include/lsqsh_pinctrl_pfr_tpm.dtsi`，并用已构建工程的
`zephyr.dts` 复核（`projects/i2cf_test/tests/case_default/build/target` =
`@1os/lsqsh/cpu1`，`projects/dma_test/...` 取未被模块清空的 spi3/spi4 板级 pinctrl，
`projects/i3c_i2c_switch_test/.../{controller,target}` 取 i3c5/i3c6）。
每条 `pinmux` 宏都解码回"端口 + 脚号"与节点名做过一致性校验，无歧义项。

`names` 列出现 `A|B` = **同一根物理脚被多个信号复用（DT 固有）**。
**引脚表按物理脚建行（一根脚一行）**，所以这些复用关系在矩阵里根本不会以两个格子
出现 —— 报告另设 `DT-INHERENT SAME-PAD PAIRS` 节逐条声明它们（`IOPROBE SAME_PIN a == b`），
与实测的 `IOPROBE LINK a <-> b` 在版面上严格分开。
设计取舍见 §2.1。

| 分组 | 物理脚 | 逻辑信号（`|` 分隔 = 同脚复用） | 来源 |
|---|---|---|
| SPI1 | PA00 | `spi1.SCK` | `SPI1_SCK_FUNC1_IDX18_PINMUX_PA(00)` |
| SPI1 | PA01 | `spi1.MOSI` | `SPI1_MOSI_FUNC1_IDX20_PINMUX_PA(01)` |
| SPI1 | PA02 | `spi1.MISO` | `SPI1_MISO_FUNC1_IDX21_PINMUX_PA(02)` |
| SPI1 | PA03 | `spi1.CS`（cs-gpios `<&gpioa 3>`） | 板级 `zephyr.dts` 实读 |
| I2C2/I2CF2 | PB04 | `i2c2.SCL\|i2cf2.SCLO` | `I2C2_SCL_FUNC2_PB04_PINMUX` + `i2cf2_sclo_pb04` |
| I2C2/I2CF2 | PB01 | `i2c2.SDA\|i2cf2.SDAO` | `I2C2_SDA_FUNC2_PB01_PINMUX` + `i2cf2_sdao_pb01` |
| I2CF2 | PB05 | `i2cf2.SCLI` | `i2cf2_scli_pb05` |
| I2CF2 | PB03 | `i2cf2.SDAI` | `i2cf2_sdai_pb03` |
| I2C3/I2CF3 | PB15 | `i2c3.SCL\|i2cf3.SCLO` | `I2C3_SCL_FUNC2_PB15_PINMUX` + `i2cf3_sclo_pb15` |
| I2C3/I2CF3 | PB14 | `i2c3.SDA\|i2cf3.SDAO` | `I2C3_SDA_FUNC2_PB14_PINMUX` + `i2cf3_sdao_pb14` |
| I2CF3 | PB13 | `i2cf3.SCLI` | `i2cf3_scli_pb13` |
| I2CF3 | PB12 | `i2cf3.SDAI` | `i2cf3_sdai_pb12` |
| I2C4/I2CF4 | PE02 | `i2c4.SCL\|i2cf4.SCLO` | `I2C4_SCL_FUNC2_PE02_PINMUX` + `i2cf4_sclo_pe02` |
| I2C4/I2CF4 | PB00 | `i2c4.SDA\|i2cf4.SDAO` | `I2C4_SDA_FUNC2_PB00_PINMUX` + `i2cf4_sdao_pb00` |
| I2CF4 | PE03 | `i2cf4.SCLI` | `i2cf4_scli_pe03` |
| I2CF4 | PB02 | `i2cf4.SDAI` | `i2cf4_sdai_pb02` |
| SPI3 | PG14 | `spi3.SCK\|spif2.IO2_IN` | `spi3_clk_pg14` / `spif2_io2_in_pg14` |
| SPI3 | PG15 | `spi3.IO0\|spif2.IO2_OUT` | `spi3_io0_pg15` / `spif2_io2_out_pg15` |
| SPI3 | PG03 | `spi3.IO1\|spif2.CSNI` | `spi3_io1_pg03` / `spif2_csn_in_pg03` |
| SPI3 | PE01 | `spi3.IO2` | `spi3_io2_pe01` |
| SPI3 | PE00 | `spi3.IO3` | `spi3_io3_pe00` |
| SPI3 | PI08 | `spi3.CS0\|spif1.CSNO` | spif1 `cs-gpios=<&gpioi 8>` = `spif1_csn_out_pi08` |
| SPI3 | PG02 | `spi3.CS1\|spif2.CSNO` | spif2 `cs-gpios=<&gpiog 2>` = `spif2_csn_out_pg02` |
| SPI3 | PG13 | `spif2.IO3_OUT\|spi3.CS0(board)` | 板级 `spi3.cs-gpios=<&gpiog 13>,<&gpiog 12>` |
| SPI3 | PG12 | `spif2.IO3_IN\|spi3.CS1(board)` | 同上 |
| SPI4 | PQ00 | `spi4.SCK\|spif4.CSNO` | `spi4_clk_pq00` / spif4 `cs-gpios=<&gpioq 0>` |
| SPI4 | PQ03 | `spi4.IO0` | `spi4_io0_pq03` |
| SPI4 | PQ04 | `spi4.IO1` | `spi4_io1_pq04` |
| SPI4 | PQ05 | `spi4.IO2\|spif4.CSNI` | `spi4_io2_pq05` / `spif4_csn_in_pq05` |
| SPI4 | PQ06 | `spi4.IO3` | `spi4_io3_pq06` |
| SPI4 | PQ02 | `spi4.CS0(board)` | 板级 `spi4.cs-gpios=<&gpioq 2>,<&gpioq 1>` |
| SPI4 | PQ01 | `spi4.CS1(board)` | 同上 |
| SPI4 | PI00 | `spi4.CS0\|spif3.CSNO` | spif3 `cs-gpios=<&gpioi 0>` = `spif3_csn_out_pi00` |
| SPIF1 | PI10 | `spif1.CSNI` | `spif1_csn_in_pi10` |
| SPIF1 | PG04 | `spif1.SCKI` | `spif1_clk_in_pg04` |
| SPIF1 | PF14 | `spif1.SI`（IO0 in） | `spif1_io0_in_pf14` |
| SPIF1 | PG06 | `spif1.IO1_IN` | `spif1_io1_in_pg06` |
| SPIF1 | PH00 | `spif1.IO2_IN` | `spif1_io2_in_ph00` |
| SPIF1 | PH02 | `spif1.IO3_IN` | `spif1_io3_in_ph02` |
| SPIF1 | PG00 | `spif1.SCKO` | `spif1_clk_out_pg00` |
| SPIF1 | PF15 | `spif1.SO`（IO0 out） | `spif1_io0_out_pf15` |
| SPIF1 | PF13 | `spif1.IO1_OUT` | `spif1_io1_out_pf13` |
| SPIF1 | PH01 | `spif1.IO2_OUT` | `spif1_io2_out_ph01` |
| SPIF1 | PH03 | `spif1.IO3_OUT` | `spif1_io3_out_ph03` |
| SPIF2 | PG08 | `spif2.SCKI` | `spif2_clk_in_pg08` |
| SPIF2 | PH04 | `spif2.SI` | `spif2_io0_in_ph04` |
| SPIF2 | PH06 | `spif2.IO1_IN` | `spif2_io1_in_ph06` |
| SPIF2 | PG12 | `spif2.IO3_IN` | `spif2_io3_in_pg12` |
| SPIF2 | PG09 | `spif2.SCKO` | `spif2_clk_out_pg09` |
| SPIF2 | PH05 | `spif2.SO` | `spif2_io0_out_ph05` |
| SPIF2 | PH07 | `spif2.IO1_OUT` | `spif2_io1_out_ph07` |
| SPIF2 | PG13 | `spif2.IO3_OUT` | `spif2_io3_out_pg13` |
| SPIF3 | PI02 | `spif3.CSNI` | `spif3_csn_in_pi02` |
| SPIF3 | PF08 | `spif3.SCKI` | `spif3_clk_in_pf08` |
| SPIF3 | PF04 | `spif3.SI` | `spif3_io0_in_pf04` |
| SPIF3 | PF02 | `spif3.IO1_IN` | `spif3_io1_in_pf02` |
| SPIF3 | PF10 | `spif3.IO2_IN` | `spif3_io2_in_pf10` |
| SPIF3 | PF06 | `spif3.IO3_IN` | `spif3_io3_in_pf06` |
| SPIF3 | PF09 | `spif3.SCKO` | `spif3_clk_out_pf09` |
| SPIF3 | PF05 | `spif3.SO` | `spif3_io0_out_pf05` |
| SPIF3 | PF03 | `spif3.IO1_OUT` | `spif3_io1_out_pf03` |
| SPIF3 | PF11 | `spif3.IO2_OUT` | `spif3_io2_out_pf11` |
| SPIF3 | PF07 | `spif3.IO3_OUT` | `spif3_io3_out_pf07` |
| SPIF4 | PH12 | `spif4.SCKI` | `spif4_clk_in_ph12` |
| SPIF4 | PH10 | `spif4.SI` | `spif4_io0_in_ph10` |
| SPIF4 | PH08 | `spif4.IO1_IN` | `spif4_io1_in_ph08` |
| SPIF4 | PF12 | `spif4.IO2_IN` | `spif4_io2_in_pf12` |
| SPIF4 | PG07 | `spif4.IO3_IN` | `spif4_io3_in_pg07` |
| SPIF4 | PH13 | `spif4.SCKO` | `spif4_clk_out_ph13` |
| SPIF4 | PH11 | `spif4.SO` | `spif4_io0_out_ph11` |
| SPIF4 | PH09 | `spif4.IO1_OUT` | `spif4_io1_out_ph09` |
| SPIF4 | PG01 | `spif4.IO2_OUT` | `spif4_io2_out_pg01` |
| SPIF4 | PG05 | `spif4.IO3_OUT` | `spif4_io3_out_pg05` |
| I3C5 | PJ10 | `i3c5.SCL` | `i3c5_scl_pj10`（阳性对照） |
| I3C5 | PJ11 | `i3c5.SDA` | `i3c5_sda_pj11`（阳性对照） |
| I3C6 | PJ08 | `i3c6.SCL` | `i3c6_scl_pj08`（阳性对照） |
| I3C6 | PJ09 | `i3c6.SDA` | `i3c6_sda_pj09`（阳性对照） |

### 2.1 设计取舍：「一根脚一行」而不是「一个信号一行」

任务书写的是"矩阵里 i2cfN 与 i2cN 会天然表现为连通，必须区分"。本工程进一步把
**矩阵按物理脚建行**，于是这类"假的连通"根本不进矩阵。理由：

- 按信号建行会有 91 行（75 根脚 + 16 根被两个信号占用的脚各多一行），其中 32 个
  格子的 `=` 纯粹是 DT 事实的重复编码，把真正的跳线信息淹没。
- 按物理脚建行后，矩阵的每一个 `#` **都**是实测出来的导线 —— 语义干净，不会误读。
- "同脚"这条信息并没有丢：报告用独立的 `SAME_PIN` 声明节 + 图例的 `names` 列
  （`i2c2.SCL|i2cf2.SCLO`）双重呈现，且用词明确写了 "NOT measured / NEVER the jumper"。

`IO_PROBE_SAME_PIN` 状态与 `same_pin_pairs` 计数仍保留 —— 它们是**防御性不变量**：
万一将来有人往表里加了一根重复的脚（同一 `(dev,pin)` 出现两次），矩阵会立刻显示
`=`、该格子 **不会**被计入连线清单，避免把一根脚数成两条线。当前表无重复行，
故实测 `same_pin_pairs` 应为 **0**。

**DT 固有同脚清单（16 组，报告中以 `IOPROBE SAME_PIN` 逐条打印）**：

`i2c2.SCL=i2cf2.SCLO(PB04)`、`i2c2.SDA=i2cf2.SDAO(PB01)`、
`i2c3.SCL=i2cf3.SCLO(PB15)`、`i2c3.SDA=i2cf3.SDAO(PB14)`、
`i2c4.SCL=i2cf4.SCLO(PE02)`、`i2c4.SDA=i2cf4.SDAO(PB00)`、
`spi3.SCK=spif2.IO2_IN(PG14)`、`spi3.IO0=spif2.IO2_OUT(PG15)`、
`spi3.IO1=spif2.CSNI(PG03)`、`spi3.CS1=spif2.CSNO(PG02)`、
`spi3.CS0(board)=spif2.IO3_OUT(PG13)`、`spi3.CS1(board)=spif2.IO3_IN(PG12)`、
`spi4.SCK=spif4.CSNO(PQ00)`、`spi4.IO2=spif4.CSNI(PQ05)`、
`spi3.CS0(spif 语境)=spif1.CSNO(PI08)`、`spi4.CS0(spif 语境)=spif3.CSNO(PI00)`。

> ⚠️ SPI3/SPI4 的 CS 有**两套口径**：板级 `cs-gpios`（spi3→PG13/PG12，spi4→PQ02/PQ01）
> 与 pfr_tpm 语境（spif 与 `spi_master_communication` 样例）里覆写的
> （spi3→PI08/PG02，spi4→PI00/PQ00）。两套的脚都在表里，标签分别标 `(board)`
> 与无后缀；矩阵看脚，不看名字。

---

## 3. 排除项与理由（逐个写明）

| 排除 | 引脚 | 理由 |
|---|---|---|
| `gpio_toggle` 占用 | **PA05 / PA07** | `common/gpio_toggle.c` 的 SYS_INIT 建两个线程每 500ms 翻转一次。把它们放进候选集，扫描期读到的电平会被线程随时改掉 → 矩阵出现"幽灵连通"与"幽灵断开"。公共层强制编入 cpu1，无法在本工程关掉。**排除**（同端口 PA00..PA03 不冲突，保留） |
| 控制台 | **PN04 / PN03** | uart1 TX/RX。拉低 = 丢日志通道，等于把测量结果连同证据一起毁掉。**排除** |
| 启动 flash（在用） | **PV00..PV05** | `qspi1` 的 CSN/SCK/DAT0..3。cpu1 固件本体就靠它（XIP/RAM 载入），运行中在用。**排除** |
| 启动 flash 测试焊盘 | **PD10..PD15** | `qspi1_*_tst_*`。与上同属启动 flash 域，碰了没有收益只有风险。**排除** |
| 范围外外设 | eth PHY（MDIO/MDC/RGMII）、SD/eMMC、USB、uart3/4/6..11、cap1..13、spi2、i2c5..9、i3c1..4/7..9 等 | 本次请求只覆盖 i2c2/3/4、i2cf2/3/4、spi1/3/4、spif1..4、i3c5/6。**不做全网扫描** —— 未列出的脚里混着引导 strap 与带外部驱动的网（PHY/SD），"宁可不扫" |

**留在候选集里、但需要说明风险的脚**：
- `PI08 / PI00 / PG02 / PQ00`（spi3/spi4 的 flash 片选 / SPIF 的 CSN_O）。
  本固件**没有**使能 spi3/spi4/spif，也没有时钟；把 CS 拉低时被选中的 NOR flash
  没有任何时钟沿，不会进读周期、不会驱动它的数据输出 → 对灌风险可忽略。
  扫描一结束（≤300µs）该脚立即恢复为"输入 + 上拉"（CS 释放）。
  且**同一时刻只驱动一根脚**，永远不会同时拉低 CS 与 CLK。
  这正是"SPI 与 SPIF 是否互联"必须测的网，不能省。
- **内部上拉与外部上拉的相互作用**：I2C 网有外部上拉。本方法靠"驱动低"取胜，
  外部上拉只贡献 0.7mA 量级的无害电流。方向永不反（见 §5 不变量）。

---

## 4. 外部条件

- **必需**：I3C5_SCL(PJ10) ↔ I3C6_SCL(PJ08)、I3C5_SDA(PJ11) ↔ I3C6_SDA(PJ09)
  两根跳线 + 共地（阳性对照，来自 `README.md`「外部接线记录」）。
- **被测**：用户自称接好的 i2c2/i2c3/i2c4 互联、SPI 与 SPIF 互联。
- **无需**：任何从设备/EEPROM（本工程不做通信）。
- **注意**：本次运行期间**不要**并发跑其它工程 —— 本工程会把 spi3/spi4 的
  CS 与 I2C 网的脚短暂拉低。

---

## 5. 不变量（改代码时必须保持）

1. **只驱动 LOW**：`io_probe_test.c` 里不存在任何 `GPIO_OUTPUT_HIGH` /
   `GPIO_OUTPUT_INIT_HIGH` / `GPIO_OUTPUT_ACTIVE` 路径。
2. **同一时刻只驱动一根脚**：其余脚一律"输入 + 内部上拉"。
3. **每轮扫描后复位**：驱动过的脚立刻还原成"输入 + 上拉"。
4. **不使能任何被测外设**：候选脚必须停在 GPIO 态，否则 pinctrl 会把脚抢走。
5. **还原必须走 `GPIO_DISCONNECTED` → `GPIO_INPUT|GPIO_PULL_UP` 并读回验证**
   （★ 2026-09-18 修复轮）：
   - 直接 `gpio_pin_configure(GPIO_INPUT|GPIO_PULL_UP)` **不可用** ——
     HAL `io_cfg_input()` 清的是 `OE_DIN` 低半区（DIN 回读位），OE 使能位在
     高半区，故 OUTPUT→INPUT **不会**撤掉输出驱动，pad 保持驱动低。
     首跑实测由此产生"完美下三角"，`links=2862 > 上限 2775`。
   - 还原后**必须读回为高**才算成功；失败 => `not_restored` => 从可驱动集
     剔除 + 在连线计数中视为"无证据"。这是结构性要求，不是"等久一点"。
6. **基线门**：基线读低的脚在整个矩阵里**不产生连线证据**（不是标 `?` 就完事），
   单列 `BASELINE_LOW` 一节。只有"基线为高、本行读低"才构成一条连线证据。
7. **settle 用轮询而不是猜死常数**：驱动后反复整行采样直到连续两次读数一致
   （上限 5ms），把**实测**稳定耗时作为 INFO 上报。同理，驱动自检与还原验证
   也都要轮询（"连续两次一致"）。当前实测：`settle_us_max≈40µs`、`restore_us_max≤22µs`。
8. **矩阵自检**：`links ≤ N*(N-1)/2`（无向对上限，N=75 ⇒ 2775）。超过上限在
   数学上不可能 ⇒ 必有假阳性 ⇒ `symmetry_report` FAIL。

---

## 6. 预期数据

| 项 | 预期 | 实测（2026-09-18 修复轮，3 次一致） |
|---|---|---|
| `pins` | 75 | 75 |
| `pin_controllable` 的 `not_controlled` | 0 | **2**（PH12/PG07，真实缺陷，见 §7.2） |
| `pin_restorable` 的 `not_restored` | 0 | **0** ✅ |
| `positive_control_i3c_link` | `scl=PJ10-PJ08 LINK`、`sda=PJ11-PJ09 LINK` | **都是 LINK(2)** ✅ |
| `baseline=HIGH` | 绝大多数脚 | 74 高 / **1 低**（PF12，见 §7.2） |
| `same_pin_pairs` | **0** | 0 ✅ |
| `dt_same_pad_pairs`（`same_pin_groups`） | 16 | 16 ✅ |
| `links`（有向格） | 不预设，**必须 ≤ 2775** | **22**（= 11 条无向连线 × 2）✅ |
| `asym`（不对称对数） | 应远小于首跑的 2624 | **0** ✅ |
| `settle_us_max` | 不预设（上报真实值） | **39–40 µs** |
| `restore_us_max` | 不预设（上报真实值） | **22 µs** |
| `claim_i2c234_bus` | 不预设 | **NOT_FOUND（0/3 SCL、0/3 SDA）** |
| `claim_spi_spif_interconnect` | 不预设 | **CONNECTED（cross_links=1）** |

---

## 7. 实机结果（2026-09-18 修复轮）

### 7.0 构建证据

```bash
west build -p auto -b "lsqsh_evb@1os/lsqsh/cpu1" \
  -d "d:/zephyr4_0/810_driver_test/projects/io_probe_test/tests/case_default/build/target" \
  "d:/zephyr4_0/810_driver_test/projects/io_probe_test/cpu1_io_probe_test" \
  -DDTC_OVERLAY_FILE="d:/zephyr4_0/810_driver_test/projects/io_probe_test/cpu1_io_probe_test/app.overlay"
```

结果：**13/13 全部成功，0 error**（只有板级/工具链既有噪声：`'label' is marked
as deprecated` ×2、`unit address ... don't match` ×1、`zstd not found` ×2）。

```
Memory region         Used Size  Region Size  %age Used
             ROM:         84 KB       512 KB     16.41%
             RAM:         68 KB       760 KB      8.95%
```

ELF：`tests/case_default/build/target/zephyr/zephyr.elf`

ztest 链接序（`nm --numeric-sort` 实测，与 `SORT_BY_NAME` 一致，**共 8 项**）：

```
claim_i2c234_bus / claim_spi_spif_interconnect / dump_full_matrix / pin_controllable
/ pin_restorable / positive_control_i3c_link / same_pin_groups / symmetry_report
```

公共符号齐备：`test_results` / `test_reasons` / `test_results_valid` 均在 ELF 中
（GDB 内存兜底实测 `mem_state=ok`）。

### 7.1 首跑（坏）vs 修复轮（好）

| 指标 | 首跑 2026-09-18 | 修复轮（3 次一致） |
|---|---|---|
| `links`（有向格） | **2862**（> 上限 2775 ⇒ 必有假阳性） | **22** ≤ 2775 ✅ |
| `asym` | 2624 | **0** ✅ |
| `not_restored` | 无此概念（还原未验证） | **0** ✅ |
| `ambiguous` | 74 | 72（= 1 根基线低脚 × 72 行） |
| `settle` | 固定猜 300µs | **轮询实测 ≤ 39–40µs** |
| 矩阵形状 | 完美下三角（级联污染 artifact） | 稀疏、对称 |
| `total measured links` 行 | **丢失**（log ring 溢出，中文乱码/行合并） | 完整打印 ✅ |
| 阳性对照 | PASS（但被 artifact 掩盖） | PASS ✅ |

三次独立运行（`reports/2026-09-18-6`、`-7`、`tmp/iofix/run2/run3_dbg.txt`）结果逐项一致。

### 7.2 两个真实缺陷 / 未决项（**不是**测量 artifact）

1. **`pin_controllable` FAIL：PH12（spif4.SCKI）、PG07（spif4.IO3_IN）不受 GPIO 控制。**
   驱动为低后读回仍为高（已按不变量 7 轮询到 5ms 上限，排除了"settle 太短"）。
   首次出现在首跑，修复轮 3 次复现 ⇒ 稳定现象。
   排除项：生成 `zephyr.dts` 中没有任何启用的 pinmux 占用这两个脚。
   未定因（超出本任务写域 `projects/io_probe_test/**`）：疑为 SoC 默认 pinmux
   非 GPIO / 焊盘特性 / 外部驱动。这两个脚已被**排除出证据来源**（行与列都标 UNKNOWN），
   不影响其余 73 根脚的矩阵，但**不能**声称"75 根全部可测"。
2. **PF12（spif4.IO2_IN）基线即为低**（外部压住），无法归一因；
   已按不变量 6 排除出连线证据，单列 `BASELINE_LOW`。
   注意：PH12 / PF12 / PG07 三根**全部是 spif4 的输入侧**脚（SI=PH10、IO1_IN=PH08 正常）——
   同一分组内 3/5 输入脚异常，指向 spif4 那几根焊盘/跳线的外部条件，值得后续单独查。

### 7.3 接线结论（用户声称 vs 实测）

见 `README.md` §7「接线裁定」——那里是给用户的表；本节只记数据来源。

### 7.4 本轮未做 / 偏差

| 事项 | 状态 | 原因 |
|---|---|---|
| `regression/cases.yaml` 注册 | ❌ 未做 | 写域限定在 `projects/io_probe_test/**`，不含 `regression/` |
| `810_driver_test/CLAUDE.md` §2 `(N)` / 顶层工程表同步 | ❌ 未做 | 同上写域限制 |
| `pin_controllable` PASS | ❌ **未达成** | 2 根脚确不受控（真实缺陷，见 §7.2）。判据**未被放宽**，保持 FAIL |
| 驱动 / HAL 修复 | ❌ 未做 | 纪律：越出写域只报不改；`io_cfg_input()` 缺陷已写 INBOX |

### 7.5 早期调研结论（GPIO 运行期改向，仍有效）

**问题**：候选脚有 75 根，若每根都要一个静态 pinctrl 状态（input + bias-pull-up），
DT 会膨胀成数百行。运行期能不能直接改方向与上下拉？

**结论：能，且不需要任何 pinctrl 状态。** 证据（源码逐行复核）：

- `zephyr/drivers/gpio/gpio_lsqsh.c` 是 lsqsh 的 GPIO 驱动
  （`DT_DRV_COMPAT linkedsemi_lsqsh_gpio`，对应 `lsqsh.dtsi` 的 `gpio*` 节点）。
- `gpio_pin_configure()` 里**方向**与**上下拉**是两个独立 switch，都直接写寄存器：
  方向走 `io_cfg_output()/io_cfg_input()`（`ls_soc_gpio.c` 的 `OE_DIN` / `IEN1_IEN0`），
  上下拉走 `io_pull_write()`（`APP_PMU->IO_CFG[x].PU1_PU0` / `.PD_PU2`）。
  `GPIO_INPUT|GPIO_PULL_UP`、`GPIO_INPUT|GPIO_PULL_DOWN`、`GPIO_OUTPUT_LOW`
  全部有效；`GPIO_OUTPUT|GPIO_PULL_UP` 也有效。
- 该驱动**从不调用 `pinctrl_apply_state()`**（`grep pinctrl` 只命中 EXTI/IRQ 的
  节点引用），脚的身份来自实例的 reg 基址。**没有 pinctrl 状态也能驱动**。
- **一条重要限制**：驱动**不会**主动把被外设复用功能占走的脚抢回来
  （`io_cfg_input()` 只改方向，不改 pinmux）。这正是 `app.overlay` 里
  坚持"一个被测外设都不使能"的原因 —— 让候选脚停在复位态的 GPIO。

**顺带记录的观察（未改）**：
- `gpio_lsqsh.c` 的 drive-strength 分支被整段注释掉（[`gpio_lsqsh.c:240-252`]），
  传驱动能力位**静默无效**；`gpio_ls.c` 则实现了。与本工程无关，仅登记。
- `pinctrl_lsqsh.c` 的 `input-1v8-enable` 解析进结构体但应用段被注释掉，
  binding 里却声明着 —— 属性静默失效。与本工程无关，仅登记。

---

## 8. 实机验证清单（✅ 2026-09-18 修复轮已全部执行）

1. `cd projects/io_probe_test/tests/case_default && ./test.sh` —— 编译应零 error/warning。
2. `./test.sh -g`（或 `-r` 复用已有 build）—— 载入运行。
3. 串口（cpu1 uart1, CH342, banner `CPU1 IOPROBE`）应看到：
   - `IOPROBE scan done: pins=75 links=...`
   - `PIN LEGEND` 75 行（含 `ctl=1 base=HIGH`）
   - 矩阵（2 列块 × 75 行）
   - `IOPROBE LINK ...` 清单
   - `IOPROBE CLAIM i2c2/i2c3/i2c4 ...` / `IOPROBE CLAIM SPI <-> SPIF ...`
4. ztest 行：7 项全 `PASS - `，末行 `PROJECT EXECUTION SUCCESSFUL`。
   - 若 `positive_control_i3c_link` FAIL ⇒ **矩阵不可采信**，先查那两根跳线/共地。
   - 若 `pin_controllable` FAIL ⇒ 按 `IOPROBE ANOMALY pin-not-controlled` 行定位。
5. 回填本表状态列，并把实测矩阵摘要写进当轮 `reports/<date>-<idx>/`。

---

## 9. qsh-test-validate 合规自检结果

对照 `qsh-test-validate` §1 逐项自检（本工程为**单核**工程）。

| 检查项 | 结果 | 证据 |
|---|---|---|
| §1.1 目录结构：`tests/`、`tests/case_default/test.sh`、`tests/COVERAGE.md`、`cpu1_*/`、无 `cpu0_*`/`cpu2_*` | ✅ | 目录树 |
| §1.1 `tests/run_test.py` / `tests/scripts/dump_results.py` / `tests/scripts/dbg_run.py` | ✅ **已补（3 项）** | 2026-09-18 修复轮补齐：单核版，`TEST_NAMES` 按 `nm --numeric-sort` 实测链接序（8 项，逐字同序），判定走 `common/judge.py` 的 `truncation_evidence`/`missing_evidence`/`has_evidence` 与 `mem_state_of_payload`/`sentinel_state`；含 `sys.path` 指 `common/` |
| §1.1 `common/test_results.{h,c}` + `common/ztest_glue.c` 引用 | ✅ | `CMakeLists.txt` → `common_sources.cmake`（统一注入，未私拷）；ELF 中 `test_results` / `test_reasons` / `test_results_valid` 符号齐备 |
| §1.2 命名：`io_probe_test/`、`case_default/`、`cpu1_io_probe_test/` | ✅ | — |
| §1.2 ZTEST 项名 `io_probe_<name>` 不撞任何驱动导出符号 | ✅ | 7 项：`claim_i2c234_bus` / `claim_spi_spif_interconnect` / `dump_full_matrix` / `pin_controllable` / `positive_control_i3c_link` / `same_pin_groups` / `symmetry_report`；无 `io_probe_*` 驱动符号 |
| §1.3 prj.conf：`ZTEST_VERBOSE_OUTPUT=y` + LOG 六件套 + libc 三件套 | ✅ | 生效 `.config` 逐项核对：`ZTEST_VERBOSE_OUTPUT=y`、`LOG=y`、`LOG_MODE_DEFERRED=y`、`LOG_BUFFER_SIZE=16384`、`LOG_BACKEND_UART=y`、`PICOLIBC=y`（`NEWLIB_LIBC`/`MINIMAL_LIBC` 落 n） |
| §1.4 CMakeLists 注入公共模块 | ✅ | `include(.../common/common_sources.cmake)` |
| §1.4a 复用收敛：overlay 走 `#include common/gpio_toggle.dtsi` | ✅ | `app.overlay` 末尾 `#include "../../../common/gpio_toggle.dtsi"` |
| §1.4a grep `&gpio` 判据 | ⚠️ **WARN（判据过宽）** | `app.overlay` 里有 `&gpiob/&gpioe/&gpiof/&gpiog/&gpioh/&gpioi/&gpioj/&gpioq` 八个**工程专有端口**使能块（gpioa 仍走 dtsi）。判据原文针对的是"手写 `&gpioa` 翻转块"，而 `projects/spi_test/cpu1_spi_test/app.overlay:100` 早有 `&gpioj` 同型先例（已入回归）。**该判据缺陷已有既有条目** `.claude/gotchas/INBOX.md` **G-P4.5-3**，本工程作为复现实例并案（G-IOP-3 已撤回以免重复计数） |
| §1.5/§1.8 run_test.py sys.path / TEST_NAMES 双份同步 | ✅ | `run_test.py` 与 `scripts/dump_results.py` 的 `TEST_NAMES` 逐字同序（8 项），且与 `nm --numeric-sort` 实测链接序一致；`sys.path` 指向 `810_driver_test/common` |
| §1.6 ztest_glue 集成（`zt_init` + `zt_run_all`） | ✅ | `src/main.c:21-22` |
| §1.7 test.sh 三模式（`./`、`-g`、`-r` + `SKIP_LOAD`） | ✅ | `tests/case_default/test.sh:50/54/67/119` |
| §1.9a cpu2 hold（`set {int}0x4002200c = 2`） | ✅ | 走公共 `scripts/test_common.sh::load_and_run_single`，该函数内已含该步 |
| §1.9b overlay `chosen { zephyr,console = &uart1; }` | ✅ | 生效 `zephyr.dts:2637` |
| §1.9c 测试代码禁用堆分配 | ✅ | `grep k_malloc\|k_calloc\|k_realloc\|malloc(` 于 `src/*.c` = 0 命中 |
| §1.9d 归档成对 `.md`+`.json` | ✅ | `reports/2026-09-18-7/io_probe_test_case_default/io_probe_test_report.{md,json}` 成对生成，并写入 `summary.md` 索引行 |
| §1.9e `ZTEST_SUITE` 槽位 | ✅ | `ZTEST_SUITE(io_probe, NULL, io_probe_suite_setup, NULL, NULL, NULL)`：setup 建的全是静态数据，`teardown` 无需配对；共享的 `g_res` 是 `static` 全局，不落 `after` 槽 |
| §1.9f 分型与入口地址一致 | ✅ | 单核：`test.sh` 走 `build_single_test`（`SINGLE_BOARD`），`lsqsh_evb@` 硬编码命中数 = **0**；入口由公共脚本给 `0x10000000` |
| §1.9g 样板脚本残留 | ✅ | `scripts/` 内为本工程单核版（banner `CPU1 IOPROBE`、`TESTS_DIR` 指向本工程 `tests/`）；`test.sh` 文案同为本工程 |
| §1.9h 边界常量来源 | ✅ | 唯一常量 `IO_PROBE_SETTLE_US=300`（内部上拉 ~50kΩ × 走线 ~100pF ⇒ RC ~5µs，60× 余量），见源码注释 |
| §1.9i C/H 块注释内 `*/` | ✅ | `grep -E "[A-Za-z0-9_]\\*/" src/*.{c,h}` = 0 命中 |
| §1.9j `build_single_test` 的 `CONF_FILE` 语义 | N/A | 本工程自持完整 `prj.conf`，不追加 conf |

**结论**：静态检查通过；harness 三件套已补齐（原 3 项授权豁免已消除）；1 项判据
WARN（`§1.4a` 判据过宽，已按先例处理并记账）。**编译验证通过**（§7.0），
**实机回归已跑通**（§7.1；`pin_controllable` 因 2 根脚确不受控而 FAIL，**未放宽**）。
---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
> 来源标签：`handcalc`(手算) `spec`(外部规范/寄存器或 DT 定义) `golden`(黄金向量)
> `crosscheck`(跨来源交叉) `contract`(API 契约值) `driversrc`(转录自被测驱动实现，
> **只证"没变"不证"仍对"**) `samecode`(同源复算，**非独立**) `!none`(无确定预期)。

```expect
# 测试项 | 断言 | 来源标签:说明
pin_controllable | not_controlled == 0 | handcalc:测量有效性硬判据——75 根脚驱动后必须全部受控（pins=75 由 §2 DT 引脚表数得）
pin_restorable | not_restored == 0 | handcalc:测量有效性硬判据——基线为高的脚驱动后必须还原回高
positive_control_i3c_link | sda_PJ11_PJ09 == 2 | spec:README「外部接线记录」I3C5_SDA(PJ11)↔I3C6_SDA(PJ09) 且 i3c_dualcore 多轮 PASS 佐证 ⇒ 必为 LINK(2)
same_pin_groups | dt_same_pad_pairs == 16 | handcalc:§2「同脚复用清单」的 DT 固有对数
symmetry_report | !none | !none:预期是 `asymmetric_pairs <= links` 的跨键关系，但本归档 INFO 是空格分隔被并成一个键（`asymmetric_pairs="0 links=22 ..."`）⇒ 取不到两个独立键；修 INFO 解析后即可登记
claim_i2c234_bus | !none | !none:原表自述"**不预设**"（本次要交付的结论由外部接线决定）
claim_spi_spif_interconnect | !none | !none:同上（"不预设"；实际观测 CONNECTED）
dump_full_matrix | !none | !none:报告完整性（含若干段落的输出），非单个标量
```
