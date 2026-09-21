# io_probe_test —— 实测 IO 互联矩阵

> **一句话**：不靠 DT 猜接线，而是把候选脚逐个拉低、看别的脚跟不跟着变低，
> 产出一张**实测**的互联矩阵，用来判定用户手工接的线到底通没通。

- **分型**：单核（只有 `cpu1_io_probe_test/`），board `lsqsh_evb@1os/lsqsh/cpu1`
- **入口**：`0x10000000`（1os）
- **被测**：无外设驱动 —— 只用 GPIO 做"拉低 + 读回"
- **输入串口**：cpu1 uart1（CH342，COM16/17），banner `[CPU1 IOPROBE]`
- **本轮状态**：✅ 已实机跑通（2026-09-18 修复轮，3 次一致）。判定结果
  `7/8 PASS`，唯一 FAIL 是 `pin_controllable`（2 根脚确不受控，见 §8）。

---

## 1. 为什么需要它

用户手动接了线，但"不确定是真的接对了没有"。用户声称：

| 声称 | 参考 |
|---|---|
| **I2C2 / I2C3 / I2C4 的 IO 接在一起** | — |
| **SPI 和 SPIF 的 IO 接在一起** | `modules/lib/pfr_tpm/samples/spi_master_communication` |
| （另有历史口述三版：`只连 I2C4↔I2CF4` → `I2C4 → I2CF4 → I2C3` → `I2C2 > I2CF4 > I2C3`） | `projects/i2cf_test/tests/COVERAGE.md` §1.2 |

DT 只能证明"某信号**本该**在哪根脚"和"哪些信号**天生**共用一根脚"，
**证明不了**两根线之间有没有跳线。`i2cf_test` 的 COVERAGE 已经把这句话写死了：

> "上游 master 的真实实例由实测判定（DT 只能证明'不是 i2c4'）"

本工程就是那个"实测"。它直接决定后续 `spi_test` / `spif_test` / `i2cf_test` /
`i2c_pure_test` 的用例能不能成立。

**本轮实测裁定见 §7。**

---

## 2. 方法：邻接扫描（adjacency scan）

```
Phase 0  全部候选脚 → 高阻输入 + 内部上拉（走已验证的还原序列，见下）
Phase 1  基线：读每一根（此时读到 LOW = 被外部拉低，不是我们干的）
Phase 2  能力探测：逐脚驱动 LOW → 轮询读回自身稳定为低（= 受控）；
                    再还原 → 轮询读回自身稳定为高（= 可还原）
         不可用脚（基线低 / 不受控 / 不可还原）整行整列不产生证据
Phase 3  逐脚驱动 LOW → **轮询到整行读数连续两次一致**（不是等一个猜死的常数）
         → 采样：
             j = HIGH                      ⇒ 与 i 不连通
             j = LOW 且 j 基线为 HIGH      ⇒ **实测连通**
             其它（j 基线为 LOW / j 不可用 / 读失败）⇒ 无证据，不参与计数
         还原 + 读回验证
Phase 4  对称性检查：i→j 与 j→i 判定不一致的对单独列出（计数）
Phase 5  上限自检：links ≤ N*(N-1)/2（N=75 ⇒ 2775），超过 = 必有假阳性
```

**关键：只驱动 LOW，永不驱动 HIGH。**

1. I2C 总线上有外部上拉。只有"拉低"能赢过外部上拉，"推高"赢不了。
2. "拉低对着外部上拉"是 0.7mA 量级的无害状态；
   **"推高撞上别的芯片输出"才是危险方向**（驱动能力对灌，几十 mA）。
3. 板上的 NOR flash 只有在 CS 有效**且有时钟沿**时才驱动它的数据输出。
   本固件不使能任何 SPI/flash 驱动，同一时刻只驱动一根脚 ——
   拉低 CS 时不可能同时给出时钟，flash 不会输出，无对灌。

代码级不变量（改 `io_probe_test.c` 时必须保持）：**文件里不存在任何
`GPIO_OUTPUT_HIGH` / `GPIO_OUTPUT_INIT_HIGH` / `GPIO_OUTPUT_ACTIVE` 路径。**

### 2.0 三个致命坑（首跑实测暴露，修复轮堵死）

**坑 A：OUTPUT→INPUT 撤不掉输出驱动（HAL 缺陷）。**
`ls_soc_gpio.c::io_cfg_input()` 清的是 `OE_DIN` 的**低半区**（DIN 回读位），
而输出使能位在**高半区**（`io_cfg_output()` 置 `1<<pin<<16`、
`io_cfg_disable_output()` 清 `1<<pin<<16`）。所以只调
`gpio_pin_configure(GPIO_INPUT|GPIO_PULL_UP)` **不会**让 pad 松手 ——
它继续保持驱动在上一次的值（低）。而驱动读回
（`gpio_lsqsh.c::gpio_ls_port_get_raw()` = `OE_DIN & 0xffff`）读的正是 pad 值
⇒ **一根脚一旦被拉低，此后永远读低** ⇒ 矩阵呈"完美下三角"、`links` 超过理论上限。

> 对策（**不改驱动**）：还原走 `GPIO_DISCONNECTED`（内部 `io_cfg_disable()`
> → `io_cfg_disable_output()`，那一条是**正确**的清高半区）再配
> `GPIO_INPUT|GPIO_PULL_UP`，然后**读回验证**。失败 ⇒ `not_restored`
> ⇒ 剔出可驱动集，且不产生连线证据。缺陷已登记 INBOX。

**坑 B：基线就低的脚 vs "被我们驱低"不可区分。**
一个本来就被外部压低的脚，被驱动时也读低 —— 两者无法区分。
⇒ 基线门：**只有"基线为高、本行读低"才构成一条连线证据**；基线低的脚单列
`BASELINE_LOW` 一节，不参与任何计数。

**坑 C：固定 settle 时间是猜的。**
不写死 300µs，而是**轮询直到整行读数连续两次一致**（上限 5ms），把**实测**
稳定耗时上报为 INFO。当前实测：`settle_us_max ≈ 40µs`、`restore_us_max ≤ 22µs`。

### 2.1 候选集（75 根物理脚）

`i2c2/3/4` + `i2cf2/3/4` + `spi1/3/4` + `spif1..4` + `i3c5/6` 的全部信号脚。
逐脚表、来源宏、排除项与理由见 **[`tests/COVERAGE.md`](tests/COVERAGE.md)** §2 / §3。

被排除的脚（理由）：`PA05/PA07`（公共层 gpio_toggle 每 500ms 翻转）、
`PN03/PN04`（uart1 控制台）、`PV00..PV05` 与 `PD10..PD15`（qspi1 启动 flash）、
以及本次范围外的外设网（eth PHY / SD / USB / 未列出的 spi2 / i2c5..9 等）——
**不做全网扫描**，避免碰到引导 strap 与带外部驱动的网。

---

## 3. 自检：怎么知道"方法本身没坏"

一个探测工具最坏的失败模式是**坏掉了却吐一张"看起来正常"的空矩阵**。
所以本工程设了六道闸：

| 闸 | 内容 | 判据 |
|---|---|---|
| **a. 受控自检** | 驱动低后**轮询**读回自身稳定为低 | 任一非低 ⇒ `pin_controllable` **FAIL**（该脚没被 GPIO 接管） |
| **b. 还原自检 ★** | 基线为高的脚，驱动后必须**还原回高**（读回轮询验证） | 任一失败 ⇒ **`pin_restorable` FAIL**，且该脚从证据集中剔除（堵坑 A） |
| **c. 基线门 ★** | 全输入 + 内部上拉时读回 | 读低 ⇒ 被外部压住，该脚**不产生任何连线证据**，单列 `BASELINE_LOW`（堵坑 B） |
| **d. 阳性对照 ★** | `I3C5_SCL(PJ10) ↔ I3C6_SCL(PJ08)`、`I3C5_SDA(PJ11) ↔ I3C6_SDA(PJ09)` —— **已确认接好的跨核跳线** | 扫描**必须**报出这两对。报不出 ⇒ **方法失效 ⇒ FAIL**，本次矩阵**不可采信** |
| **e. 对称性** | `i→j` 与 `j→i` 应一致 | 不一致**单独列出**（中间可能有二极管/驱动器），不判死 |
| **f. 上限自检 ★** | `links ≤ N*(N-1)/2`（无向对上限） | 超过在数学上不可能 ⇒ 必有假阳性 ⇒ `symmetry_report` **FAIL** |

阳性对照的"已确认"来源 = `810_driver_test/README.md`「外部接线记录」，
且 `i3c_dualcore_test` 多轮 PASS 佐证该跳线在位。

> **闸 b / c / f 是首跑之后新加的**：首跑 `links=2862 > 2775` 是一眼可见的
> "测量坏了"，而闸 d 却仍然 PASS —— 说明**光有阳性对照不够**。

---

## 4. 怎么读结果

### 4.1 串口输出的几段（全 ASCII）

```
IOPROBE ================ IO INTERCONNECT PROBE REPORT ================
IOPROBE method: drive-LOW adjacency scan (never drive HIGH); ...
IOPROBE pins=75 links=22 bound=2775 ambiguous=72 asym=0 same_pin_pairs=0
IOPROBE baseline_low=1 not_restored=0 not_controlled=2 settle_us_max=39 restore_us_max=22
IOPROBE SELFCHECK links<=bound : OK

IOPROBE [SAME_PIN] ...            ← DT 固有同脚声明（不是跳线）
IOPROBE [LEGEND] idx label ctl base rest restore_us names
IOPROBE L00 PA00  c1 bH rH 22   spi1.SCK
IOPROBE COLS 0:PA00 1:PA01 ...
IOPROBE M 00/75 PA00  000000...   ← 矩阵：紧凑 2bit/格（见下）
IOPROBE MATRIX end: 75 rows printed (= pins), idx/total on each row
IOPROBE LINK i2cf2.SCLI(PB05) <-> i2c4.SCL|i2cf4.SCLO(PE02)
IOPROBE total measured links (undirected) = 11 (upper_bound=2775)
IOPROBE [BASELINE_LOW] ...
IOPROBE ANOMALY pin-not-controlled ...
IOPROBE ASYM ...                  ← 最多打 16 条，总数一定打
IOPROBE ===== SUMMARY: pins=75 links=22 bound=2775 asym=0 baseline_low=1 not_restored=0 =====
IOPROBE ====================== END OF REPORT ======================
```

### 4.2 紧凑矩阵格式

矩阵每行 38 个十六进制字符：**每格 2 bit，一个十六进制字符装 2 格**
（低 2 位 = 靠前的列）：

| 码 | 含义 |
|---|---|
| `0` | open（不连通；对角 `/` 同脚也是 0） |
| `1` | **LINK —— 实测连通** |
| `2` | AMBIGUOUS（该列基线就低 ⇒ 无法归因） |
| `3` | UNKNOWN（无证据：读失败 / 该列不可用） |

行首 `M <行号>/<总数> <label>` 让"丢没丢行"一眼可判。列序 = `LEGEND` 的 `idx` 序。

**为什么改成紧凑格式**：首跑用 150 字符/行的块状矩阵 + 340 条不对称逐条打印，
把 deferred log ring 撑爆，丢了 `total measured links` 整行并造成行合并/乱码。
现在每行约 60 字符、行间让出 20ms，且不对称只打前 16 条。

### 4.3 关键数字

- `links` —— 有向格计数（对称时 = 无向连线数 × 2）。**必须 ≤ `bound`（2775）**。
- `pins` —— 应为 75。
- `asym` —— 不对称对数，应为 0（首跑是 2624）。
- `baseline_low` / `not_restored` / `not_controlled` —— 三个"不可用脚"计数。
- `settle_us_max` / `restore_us_max` —— **实测**的稳定/还原耗时。
- `IOPROBE ANOMALY ...` —— 逐条是：配不上、不受控、还原不回高。

### 4.4 判断"用户接线对不对"

看 `IOPROBE CLAIM ...` 行 + `IOPROBE LINK` 清单：

```
IOPROBE CLAIM i2c2/i2c3/i2c4 tied together:
IOPROBE   SCL {PB04(i2c2),PB15(i2c3),PE02(i2c4)} linked N/3  -> CONNECTED|PARTIAL|NOT_FOUND
IOPROBE   SDA {PB01(i2c2),PB14(i2c3),PB00(i2c4)} linked N/3  -> ...
IOPROBE CLAIM SPI <-> SPIF IOs jumpered together: K measured cross-links -> CONNECTED|NOT_FOUND
```

| 结论 | 含义 |
|---|---|
| `CONNECTED` | 该组脚两两导通，声称成立 |
| `PARTIAL` | 只通了一部分 —— 看 `IOPROBE LINK` 清单确认通了哪几根 |
| `NOT_FOUND` | 一根都没通 |

**注意 `NOT_FOUND` 不等于"坏了"** —— 可能是线接在别的脚上。看矩阵/`LINK` 清单
里那几根脚各自连到了谁，就能反推线实际落在哪。

---

## 5. 运行

```bash
# 单工程回归（编译 + 烧录 + 采集 + 判定 + 归档）
cd projects/io_probe_test/tests
python run_test.py                       # 全流程 + 归档到 reports/<date>-<idx>/
python run_test.py --skip-build          # 复用已有构建
python run_test.py --no-archive          # 只跑不写报告

# 单次调试采集（打印 ztest 汇总；--raw 打印全部串口原始行）
python scripts/dbg_run.py --timeout 45 --raw

# 串口不可用 / 需要交叉验证：GDB 读内存兜底
python scripts/dump_results.py --case case_default --json

# 或者直接走 test.sh
cd projects/io_probe_test/tests/case_default
./test.sh          # 只编译，打印 launch.json 片段
./test.sh -g       # 编译 + GDB 载入运行
./test.sh -r       # 只载入运行（复用已有 build）
```

⚠️ **运行期间不要并发跑其它工程** —— 本工程会把 spi3/spi4 的 CS 与 I2C 网的脚
短暂拉低。

---

## 6. harness 三件套（已补齐）

| 文件 | 状态 | 说明 |
|---|---|---|
| `tests/run_test.py` | ✅ | 单核版；`TEST_NAMES` = `nm --numeric-sort` 实测链接序（8 项）；判定用 `common/judge.py` 的 `truncation_evidence` / `missing_evidence` / `has_evidence` / `mem_state_of_payload` / `sentinel_state` |
| `tests/scripts/dump_results.py` | ✅ | 单核版；`TEST_NAMES` 与 `run_test.py` 逐字同序；带 `test_results_valid` 哨兵门 |
| `tests/scripts/dbg_run.py` | ✅ | 单核版；banner `CPU1 IOPROBE`；两遍烧录 + 干净退出 |

**仍留在写域外的同步项**（`projects/io_probe_test/**` 之外，未做）：
`regression/cases.yaml` 注册、`810_driver_test/CLAUDE.md` §2 `(N)`、
顶层 `README.md` §4 工程表与「外部接线记录」、`regression/run_all.py` 的
`PROJECT_SCRIPT` 表。

---

## 7. 接线裁定（用户声称 vs 实测）★ 核心交付

> 数据：2026-09-18 修复轮，3 次独立运行逐项一致，`asym=0`、`links=22 ≤ 2775`、
> 阳性对照 PASS。完整 11 条连线见 `tests/COVERAGE.md` §7。
> `claim_*` 两项只**报告**不判 FAIL（判据见 `tests/COVERAGE.md` §1）。

### 7.1 I2C2 / I2C3 / I2C4

| 用户声称 | 实测 | 结论 |
|---|---|---|
| I2C2 / I2C3 / I2C4 三根**互联** | `i2c2`(PB04/PB01)、`i2c3`(PB15/PB14)、`i2c4`(PE02/PB00) 的 SCL/SDA **焊盘两两之间没有任何连线**（SCL 0/3、SDA 0/3） | **证伪** |
| 历史口述「只连 I2C4↔I2CF4」/「I2C4 → I2CF4 → I2C3」/「I2C2 > I2CF4 > I2C3」 | 实测 I2C 侧只有 3 条线（下表），**不含 i2c2↔i2c3↔i2c4 之间任何一条** | **不吻合**（详见下） |

**实测到的 3 条 I2C 侧连线：**

| # | 脚 A | 脚 B | 逻辑名 |
|---|---|---|---|
| 1 | PE02 | PB05 | `i2c4.SCL\|i2cf4.SCLO` ↔ `i2cf2.SCLI` |
| 2 | PB00 | PB03 | `i2c4.SDA\|i2cf4.SDAO` ↔ `i2cf2.SDAI` |
| 3 | PB02 | PB12 | `i2cf4.SDAI` ↔ `i2cf3.SDAI` |

**怎么读**：第 1、2 条把 **i2c4 的 SCL/SDA 焊盘接到 i2cf2 的输入侧**。
（`i2cf2` 的输出侧是同脚的 `i2c2`：PB04/PB01 —— 见 `SAME_PIN` 声明。）
第 3 条只有 SDA 一侧（`i2cf4` 输入 SDA ↔ `i2cf3` 输入 SDA），**SCL 侧没有对应连线**。

⚠️ **末位提醒**：口述里的 "I2C4→I2CF4→I2C3" 那套说法与实测脚名对不上；
请**按脚名**（PB05/PB03/PB02/PB12/PE02/PB00）核对，不要按模块名推测。

### 7.2 SPI ↔ SPIF

| 用户声称 | 实测 | 结论 |
|---|---|---|
| SPI 和 SPIF 的 IO 接在一起 | **`spif1` ↔ `spif3` 六条线全通**（见下表）。其余 `spi1/spi3/spi4` ↔ `spif*` 的组合**全部无连线** | **证实，但口径要纠正**：实际是 **SPIF1↔SPIF3 回环**，不是 spi↔spif 的网状互联 |
| （首跑倾向）`SPI1 ↔ SPI3` 四根线全缺 | `PA00↔PG14` / `PA01↔PG15` / `PA02↔PG03` / `PA03↔PG13` **全是 open** | **维持**（该接线确实不存在） |

**实测到的 SPIF1↔SPIF3 六条线（教科书式回环）：**

| # | 脚 A | 脚 B | 逻辑名 |
|---|---|---|---|
| 1 | PI08 | PI02 | `spif1.CSNO` ↔ `spif3.CSNI` |
| 2 | PG00 | PF08 | `spif1.SCKO` ↔ `spif3.SCKI` |
| 3 | PF15 | PF04 | `spif1.SO` ↔ `spif3.SI` |
| 4 | PF13 | PF02 | `spif1.IO1_OUT` ↔ `spif3.IO1_IN` |
| 5 | PH01 | PF10 | `spif1.IO2_OUT` ↔ `spif3.IO2_IN` |
| 6 | PH03 | PF06 | `spif1.IO3_OUT` ↔ `spif3.IO3_IN` |

> `claim_spi_spif_interconnect` 报 `cross_links=1` —— 那 1 条就是 PI08↔PI02
> （PI08 被归进 SPI 组、PI02 被归进 SPIF 组）。真正的物理事实是上表 6 条，
> **`cross_links` 这个口径会低估**（同一对信号按分组可能落到组内），看表为准。

### 7.3 全部 11 条实测连线

`spif1↔spif3` 6 条 + I2C 侧 3 条 + 阳性对照 2 条：

```
PJ10 <-> PJ08   （阳性对照：i3c5.SCL <-> i3c6.SCL）
PJ11 <-> PJ09   （阳性对照：i3c5.SDA <-> i3c6.SDA）
```

---

## 8. 已知限制 / 未决项

| 项 | 说明 |
|---|---|
| `pin_controllable` **FAIL** | `PH12`(spif4.SCKI)、`PG07`(spif4.IO3_IN) 驱动为低后读回仍为高，**不受 GPIO 控制**。已按"轮询到 5ms 上限"排除"settle 太短"，首跑 + 修复轮共 3 次复现。生成 `zephyr.dts` 中无任何启用的 pinmux 占用它们。**真因未定**（疑默认 pinmux 非 GPIO / 焊盘特性 / 外部驱动），超出本工程写域。这两个脚已排除出证据集。 |
| `PF12`(spif4.IO2_IN) **基线即低** | 被外部压住，无法归一因 ⇒ 不产生连线证据，单列 `BASELINE_LOW`。 |
| spif4 输入侧异常集中 | `PH12` / `PF12` / `PG07` **三根全是 spif4 的输入侧**脚（`SI=PH10`、`IO1_IN=PH08` 正常）—— 同一分组 3/5 输入脚异常，指向那几根焊盘/跳线的外部条件，值得后续单独查。 |
| `regression/cases.yaml` 未注册 | 写域限制（见 §6）。 |
| HAL `io_cfg_input()` 缺陷 | 已登记 INBOX，**未修**（写域外，纪律只报不改）。 |

---

## 9. 一个已知的关联现象

`spif_test::master_read_jedec_id` 实测 `jedec=ffffff`（spif3 → spi4 侧外部 NOR
读 ID 全 0xFF，`TRIAGE_QSPI_SPIF_2026-09-18.md` §2 记为待查观察项）。
本轮实测 `spif1` 与 `spif3` 是**回环**关系（§7.2），因此 spif3 的"上游"不是
spi1/spi3/spi4 —— 与 `jedec=ffffff` 的因果假设需要按新拓扑重新审。
