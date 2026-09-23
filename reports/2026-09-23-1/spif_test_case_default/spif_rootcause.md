# spif_test 两项 FAIL —— 源码级定位与修复

- 会话：`reports/2026-09-23-1` / `spif_test_case_default`（16/18）
- 范围：**只读源码 + 改源码**。**未构建、未烧录、未碰串口/DebugServer**（板子由另一路任务占用）。
- 结论先行：两项**不是同一类根因**。`passthrough_mode_switch` 已定位为**驱动缺陷并已修**；
  `cmd_idx_accessors` 的失败点是**寄存器字段的硬件行为问题**，源码层无解，已加**不削弱判据**的诊断探针，待上板一刀切开。

---

## 0. 先更正任务书里的两处描述（grep 截断所致）

| 任务书原话 | 复核结果 |
|---|---|
| `cmd_idx_accessors` 失败于「DUMMY_CYCLE 为 5 bit → `0xff` 应被截断」 | **不成立**。真正失败的是 **`:327 zassert_equal(dummy, 9, ...)`**（普通往返，9 是 5 bit 内合法值）。任务书指的截断断言在 **`:341`（`dummy == 0xff & 0x1f`）**，**从未执行到**。 |
| 「两个失败项疑似同一类根因（位宽掩码 / state 未按 cold_reset 复位）」 | **两项均被推翻**：① 截断那条根本没跑到（见上）；② `passthrough` 的根因是判据函数**漏比 `analog` 属性**，与「state 未复位」无关 —— 复位**确实**把状态置回了 default。 |

### 关键前置事实（影响读日志）：**ztest 断言失败会 abort 掉用例线程，后续代码一律不执行**

`zephyr/subsys/testsuite/ztest/src/ztest.c:571`（`KERNEL` 分支）的 `ztest_test_fail()` →
`test_finalize()` → `k_thread_abort(&ztest_thread)` + `k_thread_abort(k_current_get())`。
（另一支 `#ifndef KERNEL` 的 `longjmp` 版才会续跑；本工程 `CONFIG_MULTITHREADING=y`，走 abort 支。）

⇒ 每项 FAIL 只报**第一条**失败断言；其后的 `ZT_INFO` **一行都不会打**（这解释了报告 INFO 列为空）。
⇒ 两项 FAIL 都只暴露了各自用例的**第一条**违规，实际违规条数更多（见 §1）。

---

## 1. `passthrough_mode_switch`（`:776`）—— 驱动缺陷，**已修**

### 现象

`cold_reset` 后 `spif_pinctrl_filter_mode_check(dev)==true`（`:774` 通过）、
`spif_pinctrl_passthrough_mode_check(dev)==true`（`:776` 期望 false，**倒**）⇒ **两态判据同时为真、不互斥**。

### 根因（源码级，可逐行核对）

`spif2` 的三档 pinmux（`modules/lib/pfr_tpm/include/lsqsh_pinctrl_pfr_tpm.dtsi:1011-1057`）：

| 状态 | 引脚组 | 与 default 的差异 |
|---|---|---|
| `default`（filter，`pinctrl-0`） | 12 脚，如 `spif2_clk_in_pg08` = `SPIF2_CLK_IN_FUNC4_PG08_PINMUX` | — |
| `passthrough`（`pinctrl-1`） | **同 12 脚、同 pinmux 值**，其中 10 脚带 **`analog;`**（如 `spif2_clk_in_pg08_analog`） | **只差 `analog` 属性** |
| `master`（`pinctrl-2`） | 另一组：`spif2_*_input`（`..._PIN`，func_valid=0）+ `spi2_mst_*` | 与 default 确有 pinmux 差异 |

而判据函数 `spif_pinctrl_mode_check()`（`modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.c:1429-1464`）
**只比对 `pins->pinmux`**（`func_valid` 与实管脚 func），**从不读 `pins->pin_attr`**：

```c
if ((func_valid != pins->pinmux.func_valid)
    || ((pins->pinmux.func_valid) && (func != pins->pinmux.func))) { check = false; }
```

⇒ 对 default / passthrough，**每一次比较的结果都相同** ⇒ 两个 check 恒等 ⇒
「复位后 passthrough 应为假」在**构造上必然失败**（与硬件停在哪个态无关）。
**这不是用例预期错，是判据函数不能履行它的契约**（"当前处于哪一态"）：

1. 三态是同一外设的互斥配置，判据必须能区分；default 与 passthrough 唯一差异就是 `analog`。
2. `analog` 不是"无语义装饰"——pinctrl 把它落到**焊盘的模拟使能位**
   （`zephyr/drivers/pinctrl/pinctrl_lsqsh.c:82-86` → `gpio_ana_init/deinit(pin)`
   → `APP_PMU->IO_CFG[port].AE_DS2` 高半区 bit(16+num)，`ls_soc_gpio.c:2574-2590`）。
3. HAL **本就提供了该位的回读**：`gpio_ana_enabled(pin)`（`ls_soc_gpio.h:291`）。判据函数只是没用它。
4. 该判据还被驱动自身当**冲突守卫**用：`spif_switch_to_master_handle()`（`:1524`）用
   `spif_pinctrl_master_mode_check(sibling)` 判"相邻 out 脚是否在 master 态"。
   判据不可分辨 ⇒ 守卫可能误判。**属真缺陷，不只是测试好看不好看。**

**旁证（有区分度的对照）**：同目录 `pinctrl_state_switch` 本轮 PASS —— 它比的是 **master 态**（不同 pinmux 单元），
说明判据在"pinmux 确实不同"时有效，问题只出在"pinmux 相同、仅差 `analog`"这一处。

**附带证据（说明违规不止一条）**：`:785 zassert_false(spif_pinctrl_filter_mode_check(dev))`（切到 passthrough
之后）在旧驱动下**也会失败**（此时 default 判据仍为 true）—— 只因 `:776` 先失败、线程被 abort，
这条从未打印出来。

### 修复

`modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.c:1445-1464`，比较条件加一项 ——
把状态声明的 `analog` 与焊盘实际的模拟使能位对齐：

```c
        /* `default` (filter) and `passthrough` drive the same pads with the same
         * pinmux unit and differ only by the `analog` attribute, which pinctrl
         * latches in the pad's analog-enable bit. Comparing pinmux alone cannot
         * tell the two states apart, so the pad's analog state is part of the
         * comparison. */
        if ((func_valid != pins->pinmux.func_valid)
            || ((pins->pinmux.func_valid) && (func != pins->pinmux.func))
            || (gpio_ana_enabled(pin) != (pins->pin_attr.analog != 0))) {
```

**为什么能修**：`pinctrl_configure_pin()` 对每个引脚**无条件**执行
`if (analog) gpio_ana_init(pin); else gpio_ana_deinit(pin);`（`pinctrl_lsqsh.c:82-86`），
所以"最后一次 apply 的 state"与 `gpio_ana_enabled(pin)` 严格一一对应。
**副作用评估（逐调用点）**：

| 调用点 | 影响 |
|---|---|
| `passthrough_mode_switch` 四条断言（`:774/:776/:783/:785/:790/:791`） | 全部转为正确行为（见 §1 验证） |
| `pinctrl_state_switch`（现 PASS） | master 态引脚**无** `analog`，且 apply master 时会 `gpio_ana_deinit` ⇒ 新增项恒成立，**无回归** |
| `spif_switch_to_master_handle()` 的 sibling 守卫 | 判据更严格→更准；sibling 在 default/master 时结论不变（func 项已足以判 false） |
| vendor samples（`spi_filter_1out_3in` / `spi_master_communication` 等 11 个） | 它们只断言**正向**（apply 之后 check==true），新条件在各态 apply 后恒成立，**无回归** |

判据**未被放宽**（这是本轮最要紧的纪律）：新增的是一项**额外**比较，只会让判据更严，不会放过异常态。

---

## 2. `cmd_idx_accessors`（`:327`）—— **待上板定性**（已加不削弱判据的诊断）

### 现象

失败断言是 **`:327 zassert_equal(dummy, 9, "set/get dummy by idx roundtrip")`**：
`spif_set_dummy_by_idx(dev, 9, 5)` → `spif_get_dummy_by_idx(dev, &dummy, 5)` → `dummy != 9`。

**同用例、同寄存器、相邻两条**：
- `:321-323` `spif_set_cmd_by_idx(dev, 0x77, 5)` → 回读 `0x77` ⇒ **通过**（CMD[7:0] 可写可读）；
- `cmd_table_add_remove` 的固定表段（`:426-432`）实测 `INFO slot=5,cmd=0x3,en=1` ⇒ **EN[31] 可写可读**。

⇒ 同一个 32 位寄存器（idx 5 → `base + SPIF_CMD_BASE(0x2c) + 20 = 0x40` = **`SPIF_READ_CMD`**）上：
**CMD[7:0] ✓、EN[31] ✓、DUMMY_CYCLE[12:8] ✗**。

### 已排除的层

- **driver**：`spif_set_dummy_by_idx` / `spif_get_dummy_by_idx`（`spi_filter.c:1015-1079`）是标准
  read-modify-write，字段读写对称，无掩码/截断缺陷；`idx=5` 在界内（`SPIF_FIXED_CMD_TABLE_NUM=30`）、
  出参非 NULL ⇒ 上游缺陷 #4/#5 不适用。
- **测试期望**：`spif_cmd_t` 布局 `CMD[7:0] / DUMMY_CYCLE[12:8] / reserved[30:13] / EN[31]`
  （`reg_spi_filter.h:177-186`）与用例的 5 bit 预期一致；同文件 `spif_cfg_t` 的位序已被
  `cold_reset_state` 实测（`cfg=0x40` = `IP_LOCK` bit6）按住 ⇒ **位域打包（LSB-first）无误**。
  用例的两条断言（`:327` 9 往返、`:341` `0xff→0x1f`）**都不是放宽型判据**，不能删。
- **非 flaky**：`reports/2026-09-21-1` / `2026-09-22-1` / `2026-09-23-1` **三轮全 FAIL**，确定性。

### 剩下的两种可能（源码层无法二选一）

| 假设 | 现象 | 处置方向 |
|---|---|---|
| **(a) HW 该槽 DUMMY 位不可写/未实现** | 写后原始寄存器 [12:8] 仍为 0 | 判据层问题：该槽（= 普通 READ，SPI 语义上**无 dummy 相**）本就不带 dummy；应改测**确有 dummy 相**的槽（`idx 6 = SPIF_READ_FAST_CMD`、`idx 7 = SPIF_READ_QDATA_CMD`，用例 `:334-341` 已用这两个槽）。**改用例需先有回读证据**，不得先改。 |
| **(b) 位确实落进寄存器、只是回读通路问题** | 原始寄存器 [12:8] == 9，而 API 回读 != 9 | 驱动/寄存器访问缺陷，另案定位 |

**不采信的反向证据**：驱动自身 `spif_fixed_cmd_tab_restore()`（`:1744-1771`）与 vendor 的
`cmd_dummy_table` 枚举都把 30 个槽一律当作"有 DUMMY 字段"（机械对称），**只能说明 SW 模型如此，
不能证明 HW 位存在**。

### 本轮动作：**加诊断，不动判据**

`projects/spif_test/cpu1_spif_test/src/spif_test.c:325-340`，在 `:327` 断言**之前**插入一条
`ZT_INFO`（必须先于断言 —— 断言失败会 abort 线程，见 §0）：

```c
	spif_set_dummy_by_idx(dev, 9, 5);
	{
		spif_cmd_t raw = { .value = sys_read32(spif_reg_base(1) + SPIF_CMD_BASE + 5 * 4) };

		spif_get_dummy_by_idx(dev, &dummy, 5);
		ZT_INFO("cmd_idx_accessors",
			"slot5_raw=%#x,cmd=%#x,raw_dummy=%#x,get_dummy=%#x",
			raw.value, raw.CMD, raw.DUMMY_CYCLE, dummy);
	}
	zassert_equal(dummy, 9, "set/get dummy by idx roundtrip");
```

判据文字与断言**一字未改**（不削弱），只是让失败轮也能拿到回读值。
另在 `passthrough_mode_switch` 的断言前加了同型诊断 `after_cold_reset filter=%d,passthrough=%d`
（`:787-790`），作为 §1 修复的上板书面证据。

---

## 3. 改动文件清单（全部在写域内，**与 `spi_test` / `spid_test` 无共用文件**）

| 文件 | 改动 | 性质 |
|---|---|---|
| `modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.c:1445-1464` | `spif_pinctrl_mode_check()` 增加 `gpio_ana_enabled(pin)` vs `pin_attr.analog` 比较（+7 行，含注释） | **真修复**（§1） |
| `810_driver_test/projects/spif_test/cpu1_spif_test/src/spif_test.c:325-340` | 新增 1 条 `ZT_INFO`（诊断） | 不削弱判据（§2） |
| `810_driver_test/projects/spif_test/cpu1_spif_test/src/spif_test.c:787-790` | 新增 1 条 `ZT_INFO`（诊断/修复证据） | 同上 |

- **无需协调**：`spi_test`/`spid_test` 走的是 `zephyr/drivers/spi/spi_ls.c`、`spi_dw.c` 与
  `modules/lib/pfr_tpm/drivers/spid/spid_linkedsemi.c`；本轮**一个字节都没碰**。
  `modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.c` 仅被 spif 使用（`i2c_filter.c` 是另一文件）。
- `spi_filter.c` 工作区里**另有一处既存未提交改动**（`:881` / `:937` 的 `idx*4 → off*4`，
  = `KNOWN_ISSUES #78` 的 P0 修复 + `cmd_table_add_remove` 回归护栏）。**非本轮引入，未动。**
  ⚠ 按 `KNOWN_ISSUES #108` 提醒：这处改动仍未提交，任何 `checkout/reset/west update` 都会静默丢掉。
- 行尾：改动后两文件各自行尾仍**单一一致**（`spif_test.c` 全 LF；`spi_filter.c` 全 CRLF），无混合
  （防 #103 那类 diff 爆炸）。

---

## 4. 上板怎么验（**必需**）

**为什么必须上板**：
- §1 的修复正确性依赖"pinctrl apply 后焊盘 `AE_DS2` 高半区随 `analog` 置/清"这一**硬件行为**，
  只有回读实测（`after_cold_reset filter=1,passthrough=0` + 各断言通过）能证。
- §2 的两种假设**处置相反**（改用例 vs 改驱动），唯一判据是**原始 32 位寄存器在真机上的值** ——
  仿真/静态分析都给不出。

### 命令（改过源码 ⇒ 必须重新构建）

```bash
# 1) 构建 + 烧录 + 采集（一条命令；test.sh 内含同步构建，符合 KNOWN_ISSUES #55）
cd D:/zephyr4_0/810_driver_test/projects/spif_test/tests/case_default
bash test.sh -g

# 若行为与源码不符（怕命中 #88 陈旧产物），先清该 case 自己的 build 再跑：
#   rm -rf build && bash test.sh -g
```

（若走全量入口：`python regression/run_all.py`，本 case 仍在 `cases.yaml` 内。）

### 预期结果

1. **`passthrough_mode_switch` → PASS**，且串口应见
   `INFO passthrough_mode_switch: after_cold_reset filter=1,passthrough=0`。
   —— 这是 §1 修复的直接书面证据（修复前该行为 `filter=1,passthrough=1`）。
   同时 `pinctrl_state_switch` 必须**仍然 PASS**（回归对照）。
2. **`cmd_idx_accessors`**：看新 INFO 的 `slot5_raw` / `raw_dummy` / `get_dummy`，按 §2 表判读：
   - `raw_dummy=0, get_dummy=0` ⇒ 假设 **(a)** 成立（该槽 DUMMY 位不可写/未实现）
     ⇒ 后续把往返/截断断言迁到确实带 dummy 相的槽（`idx 6` / `idx 7`，SPI 语义上 FAST_READ / READ_QD 才有 dummy），
       并把"idx 5 = 普通 READ 无 dummy 字段"记入 COVERAGE；
   - `raw_dummy=9, get_dummy!=9` ⇒ 假设 **(b)**（字段在寄存器里、读不通）⇒ 驱动/寄存器访问缺陷，另案；
   - `raw_dummy=9, get_dummy=9` ⇒ 两者都对 ⇒ 失败另有其因（重跑确认是否 flaky）。
3. 全绿判据：`18/18 PASS` + `PROJECT EXECUTION SUCCESSFUL`（单核 TGT，token 来源）。

---

## 5. 仍未定项

| ID | 事项 | 卡什么 | 建议 |
|---|---|---|---|
| 本轮-1 | `cmd_idx_accessors`：`SPIF_READ_CMD`（idx 5）的 `DUMMY_CYCLE[12:8]` 是"写不进"还是"读不回" | 需一次上板回读（§4.2） | 上板后按 §2 表分流；在此之前的**唯一动作就是不动作**（判据不得放宽） |
| 本轮-2 | 若判为 (a)：该槽 DUMMY 往返断言应迁到 `idx 6`/`idx 7` | 依赖 本轮-1 结论 | 不在无证据时改 |
| 关联 | `OPEN_ITEMS O-31` 登记本项为"待驱动工程师裁定（用例 vs 驱动）" | —— | **本轮已按"判据函数不充分 ⇒ 驱动缺陷"裁定并落地代码**；若上游不认，回退只需删掉 `spi_filter.c` 中新加的那一个 `||` 条件 |

### 与既有登记的关系

- `passthrough_mode_switch`：**已登记**（`OPEN_ITEMS O-31` + `tmp/spif_final/CONCLUSION.md`），本轮在其给出的
  二选一中**选择了"驱动缺陷"支**并给出源码级依据（`gpio_ana_enabled()` 回读存在 + 守卫用途 + 三态互斥契约）。
- `cmd_idx_accessors`：**已登记**（`TRIAGE_QSPI_SPIF_2026-09-18.md` §2 S1 / §5 / §6 B5），状态仍为"待实机回读值"。
  本轮未推翻、未擅自改判，只补了能一次定性的探针。
