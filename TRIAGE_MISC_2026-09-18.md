# C3–C6 专项定位：4 个孤立 FAIL 项性质判定（2026-09-18）

> **本轮范围**：只定位、不修。**未改动任何代码**（`zephyr/` / `modules/` / 任何测试工程 / `common/` / `regression/` / `scripts/` 均只读）。
> **执行者**：单个 runner agent，未派子 agent。
> **证据分级**：`实测`（实机烧录采集 / GDB 寄存器读回 / 驱动自身日志） > `源码推导`（逐行复核） > `推测`。
> 凡"判断该场景不会发生"与"验证过没问题"分开表述。
> **判据纪律**：每条 FAIL 先定性质（用例预期错 / 驱动缺陷 / 环境问题 / flaky），再谈动作。
>
> ⚠️ **语料可用性（2026-09-24）**：本文引用的 `reports/2026-09-17-3` / `2026-09-18-1` 会话
> **已不可得、且从未入库** ⇒ 相关引用**仅作留痕**；留存范围与纪律见 `SPEC_REFERENCE` §6.4.7
> 「语料可用性声明」。

## 0. 总表

| 测试项 | 现象 | 性质判定 | 证据 | 建议动作 |
|---|---|---|---|---|
| `wdt_boundary::test_setup_twice_ebusy`（`wdt_boundary_test.c:280`） | 首次 `wdt_setup` 返回 **-16(-EBUSY)**，期望 0 | **驱动缺陷**（`wdt_iwdg_ls.c:73` 守卫过宽）+ 用例顺序暴露 | 实测：驱动日志 `wdt_ls_setup: interrupt already configured`；GDB 读 sec_iwdg `IWDT_CTRL` 写 0 前 `0x0B` → 写后 `0x08` | 驱动：守卫改为只测 EN/RST_EN 位（见 §1.4） |
| `wdt_boundary::test_setup_pause_in_sleep_enotsup`（`:264`） | `wdt_setup(PAUSE_IN_SLEEP)` 返回 **-16(-EBUSY)**，期望 -ENOTSUP | 同上（同一次状态残留的两个受害者） | 同上；两项执行序紧邻其前的是 `test_setup_pause_halted_by_dbg_feed_disable`（唯一 feed→disable 的用例） | 同 §1.4；用例侧可选加隔离 |
| `spi::post_reset_state`（`spi_test.c:801`） | 复位后 `CR1.BR` 实测 `0x10`（=/8），期望 0；`SPE==0`/`MSTR==0` 均通过 | **用例预期错**（把"复位默认"假定为全 0） | 实测 `cr1_after=0x10`；`spi` suite 内无 SLAVE 配置且 `configure_mode_bits` 实测 `cr1=0x11f`(MSTR=1)、`hold_on_cs_spe` 用 1.5 MHz(BR=/64) ⇒ 复位后 MSTR=0 只能由复位造成 ⇒ 复位生效、`0x10` 即 IP 复位默认 | 断言改为 `SPI_LS_BR_DIV8`(=0x10)，或整寄存器 `== 0x10` |
| `adc_temper::test_adc_setup_variants`（`adc_temper_test.c:224`） | `restored read failed: 0`（`raw==0`，`adc_read` 返回 0 而非 -1） | **用例预期错**（变体触发采样错误、还原后未等待/未重试）；**同时暴露驱动缺陷**（采样错误静默降级为 0 数据） | 实测：同用例内 4× `<err> adc_ls: Sampling error`；`ADC_RegularGetValue()`(`adc_ls.c:89-104`) 错误分支清标志后直接 return、不写 buffer | 用例：还原后加建立延时/重试；驱动：采样错误应上抛错误码 |
| `trng::isr_nonblock` | 非阻塞取 64 B 实得 `got=0` | **用例预期错**（假设"池会自由回填"）；**非驱动缺陷** | 实测 `got=0`；源码链 `entropy_ls_trng.c:119-121 / :147 / :169-172`（见 §4） | 改用例：首调断言 `[0,len]`，再调一次断言 `==len`（见 §4.3） |

> 4 项**全部为确定性**，非 flaky：wdt 两项在 2026-09-17-4 / -5 / 2026-09-18-1 三轮 + 本轮复跑，结果逐项一致。

---

## 1. `wdt_boundary` 两项 —— 驱动缺陷（EBUSY 守卫过宽）

### 1.1 现象（实测）

`reports/2026-09-18-1/wdt_test_case_boundary/` 及本轮复跑（`dbg_run.py --raw`）：

```
START - test_setup_pause_in_sleep_enotsup
  <inf> wdt_iwdg_ls: wdt_ls_setup: interrupt already configured
  Assertion failed at ...wdt_boundary_test.c:264: ... (ret not equal to -ENOTSUP)
  setup(PAUSE_IN_SLEEP) returned -16, expected -ENOTSUP
START - test_setup_twice_ebusy
  <inf> wdt_iwdg_ls: wdt_ls_setup: interrupt already configured
  Assertion failed at ...wdt_boundary_test.c:280: ... (ret not equal to 0)
  first setup returned -16, expected 0
```

- `-16 = -EBUSY`；驱动自身日志指明走的是 **`IWDT_CTRL != 0`** 分支（`wdt_iwdg_ls.c:73-76`）。
- 注意 `test_setup_twice_ebusy` 失败在 **:280（第一次 setup）**，不是第二次 —— 即该用例连"前置使能"都建立不起来。

### 1.2 执行序（实测）

串口原始输出 + `nm --numeric-sort` 的 `ztest` 段符号序一致表明：**ztest 用例按 `SORT_BY_NAME`（字母序）执行**，且与本工程 COVERAGE 自述的口径相同（`app_iwdg_cpu2_test/tests/COVERAGE.md:88-90`）。关键相邻关系：

```
... test_sec_iwdg_rejects_reset_soc_flag
    test_setup_pause_halted_by_dbg_feed_disable   ← 唯一 setup→feed→disable 的用例（sec_iwdg）
    test_setup_pause_in_sleep_enotsup             ← FAIL
    test_setup_twice_ebusy                        ← FAIL
    test_setup_without_timeout_enotsup            ← PASS（LOAD==0 短路，见 1.3）
```

`test_setup_pause_halted_by_dbg_feed_disable` 是**该套件内唯一在 `wdt_disable()` 之前调用 `wdt_feed()`** 的用例（`wdt_boundary_test.c:306-309`）。

### 1.3 GDB 寄存器实测（定案）

在 `iwdt_ls_disable` 下断点（`dev=watchdog@400a1800` = `sec_iwdg`，即上述用例的 `wdt_disable(sec_dev)`）：

```
CTRL=0xb  LOAD=0x1e0000  CNT=0x1e0000      ← 进入 disable 时
   （0x0b = IWDT_EN | IWDT_RST_EN | IWDT_CNT_CLR；LOAD==CNT==1966080 = 60000ms）
   （执行 `IWDT_CTRL = 0x0` 之后）
CTRL_after_store=0x8                        ← 只清掉了 EN/RST_EN
CNT_after_store=0x1e0000
```

**关键：`IWDT_CNT_CLR`（bit3）写 0 清不掉，读回仍为 1。**
⇒ `iwdt_ls_disable()` 写入 0 后 `IWDT_CTRL = 0x08 ≠ 0`；下一次 `iwdt_ls_setup()` 的守卫 `if (config->iwdg_reg->IWDT_CTRL)` 是**整寄存器非零判定**，遂误判为"已配置" → `-EBUSY`。

对照 `wdt_boundary_test.c:239-250`（`:244-245` 直接写 `LOAD=0` / `CTRL=0`）后的 `test_setup_without_timeout_enotsup` **PASS**：它被 `!IWDT_LOAD` 先短路（`:68` 在 `:73` 之前），CTRL 是否干净无关，故不暴露本缺陷 —— 也说明"写 0 清不掉 bit3"不影响该用例。

### 1.4 根因与建议

- **根因（驱动缺陷）**：`zephyr/drivers/watchdog/wdt_iwdg_ls.c:73` 的守卫粒度错误 —— 它用 `IWDT_CTRL` 整寄存器判"已使能"，而该寄存器含 `IWDT_INTR_CLR`(bit2)/`IWDT_CNT_CLR`(bit3) 两个非"使能"语义位；`iwdt_ls_feed()`（`:56-58`）写 `CNT_CLR` 后硬件把该位留 1（写 0 清不掉），使守卫长期误触发。
- **建议动作（驱动，需用户裁定，本轮不改）**：
  ```c
  /* :73 */
  if (config->iwdg_reg->IWDT_CTRL & (IWDT_EN_MASK | IWDT_RST_EN_MASK)) {
      return -EBUSY;
  }
  ```
  `iwdt_ls_disable()` **不需要改** —— 实测它已正确清掉 EN/RST_EN。
- **为什么不判"用例预期错"**：两项用例的期望本身是对的（干净状态下第一次 `setup` 应返回 0、`PAUSE_IN_SLEEP` 应返回 -ENOTSUP）。只要守卫按位判定，两项在任何执行序下都成立。
- **用例侧可选加固（非必需）**：该套件 `ZTEST_SUITE` 的 `after_fn` 传了 `NULL`、teardown 只在套件末尾跑一次（见 §5.3），项间不清理；若要独立于驱动修复，可在断言前显式写 `IWDT_CTRL` 清 strobe 位，或把两项移入不依赖前序状态的独立 case。

### 1.5 与题面提示的关系（更正）

题面提示"`test_setup_twice_ebusy` 是否正撞 `install_timeout:119` 清 CTRL 绕过 EBUSY 守卫（P1-3）" —— **不是**：

- P1-3 的方向是"install 把 CTRL 清零 ⇒ 该报 -EBUSY 的反而返回 0"；本次两项的失败方向相反（该返回 0 的返回了 -EBUSY）。
- 且 `test_setup_twice_ebusy` 的两次 `setup` 之间**没有** `install_timeout` 调用，P1-3 的前提不成立。
- P1-3 是真实存在的问题（`KNOWN_ISSUES`/COVERAGE 已记），但与本轮两项 FAIL 无关。

### 1.6 交叉印证（同型驱动、不同实例）

- `app_iwdg_cpu2_test` 的同名两项 `test_setup_pause_in_sleep_enotsup` / `test_setup_twice_ebusy` **PASS**（`reports/2026-09-18-1/app_iwdg_cpu2_test_...`）。差异在于执行序：`app_iwdg` 套件里关键相邻的是 `test_setup_enables_ctrl_rst_en`（**setup→disable，无 feed**），它显式断言 `disable 后 CTRL==0` 并通过 —— 从反面印证"**feed 是引入残留 bit3 的必要条件**"。
- `case_non_destructive` 的 `test_wdt_disable`（`wdt_test.c:86-103`，500 ms + disable + 睡 1 s，**无 feed**）PASS ⇒ 无 feed 时 `disable` 确实生效（否则 500 ms 即触发 HART 复位，串口会出现重引导/超时，而非 5/5 PASS）。

---

## 2. `spi::post_reset_state` —— 用例预期错（BR 复位默认值假设错误）

### 2.1 现象（实测）

```
ret_rst=0, cr1_after=0x10, ret_recover=0, br_after=0x20
Assertion failed at spi_test.c:801: (cr1_after & SPI_CR1_BR_MASK not equal to 0U)
```

`SPI_CR1_BR_MASK=0x38`、`SPI_CR1_BR_POS=3`（`reg_spi_type.h:36-37`）⇒ `0x10` = BR 字段值 2 = **/8**。
`:799`(`SPE==0`)、`:800`(`MSTR==0`) 均**已通过**，失败只落在 BR。

### 2.2 判定链（源码 + 实测）

1. **复位确实生效**：`spi` suite（18 项）内**没有任何 SLAVE 配置**（`spi_test.c` 全文无 `SPI_OP_MODE_SLAVE`；`spi_ls.c:90` 是唯一写 `MSTR=0` 之处，只在从模式分支），且 `configure_mode_bits` 实测 `cr1=0x11f`（MSTR=1）。紧邻其前的 `hold_on_cs_spe` 用 spi1/1.5 MHz（BR=/64）且收尾 `SPE=0`，故复位前 spi1 `CR1` 的 MSTR 为 1。
   `spi_loopback::link_probe`（它把 spi1 当 SLAVE，`spi_loopback.c:105-106, 307`）**在本 suite 之后**执行 —— ELF 符号序显示 `spi` 段（`100133f8..1001354c`）整体位于 `spi_loopback` 段（`10013560..`）之前。
   ⇒ 复位后 `MSTR=0` 不可能来自驱动写入，**只能由 `reset_line_toggle_dt()` 造成** ⇒ 复位生效。
2. 复位生效后 `CR1 = 0x10`（SPE=0, MSTR=0, **BR=/8**）⇒ 这就是该 IP 的复位默认值。
3. 用例的 `:801` 断言把它假定为 0 —— **假设错误**。

> 旁证：驱动 `spi_ls.c:65-66` 在每次 `configure` 开头无条件写 `SPE=0` + `BR←scaler[0]`(= /8) —— 与"IP 复位默认 BR=/8"同值，是驱动作者选 `/8` 作安全默认的合理来由。

### 2.3 建议动作

- 用例（`projects/spi_test/cpu1_spi_test/src/spi_test.c:801`）改为断言复位默认档：
  ```c
  zassert_equal(cr1_after & SPI_CR1_BR_MASK, SPI_LS_BR_DIV8,
                "复位后 CR1.BR 应为复位默认档 /8");
  ```
  （`SPI_LS_BR_DIV8` 本文件 `:69` 已定义 = `2 << SPI_CR1_BR_POS` = `0x10`。）
- `COVERAGE.md:103` 的"预期数据"同步改为：复位后 `SPE==0`、`MSTR==0`、`BR==/8(0x10)`。
- **待定**：IP 的 CR1 复位默认值在仓库内**无文档背书**（HAL/寄存器头未标注 reset value）。本结论由"复位确已生效（MSTR 由 1→0）"+ 实测 `0x10` 推出，置信度高但非文档确证。

---

## 3. `adc_temper::test_adc_setup_variants` —— 用例预期错（主）+ 驱动静默降级（新发现）

### 3.1 现象（实测，本轮复跑）

```
START - test_adc_setup_variants
  <err> adc_ls: Sampling error      ×4（同一时间戳）
  Assertion failed at adc_temper_test.c:224: (raw > 0 is false)
  restored read failed: 0
 FAIL - test_adc_setup_variants
（其后 test_multiple_adc_reads / test_sampling_stability / test_temperature_in_range 全 PASS；
  test_adc_full_scale_bound 实测 raw=2217~2218, t_deg=24）
```

- `raw == 0`（不是 -1）：`adc_read()` 返回 0，但取到的数据是 0。
- 同一用例内驱动打了 **4 条 `Sampling error`**。

### 3.2 根因（源码）

`zephyr/drivers/adc/adc_ls.c` 的 `ADC_RegularGetValue()`（函数 `:89-164`）：

```c
if (reg->INTR_R & ADC_INTR_REG_ERR_MASK) {
    reg->INTR_C |= ADC_INTR_REG_ERR_MASK;
    LOG_ERR("Sampling error\n");
    return;                      /* ← 直接返回，不写 data->ctx.sequence.buffer */
}
```

⇒ 采样错误时调用方 buffer 保持 `memset(0)` 的 0，而 `adc_read()` 仍返回 0（成功）。
⇒ 用例的 `do_adc_read() >= 0` 前置断言**拦不住**（`0 >= 0` 成立），最终 `raw > 0` 必然落空。

触发源是**本用例自身的配置变体**：`ADC_REF_VDD_1` / `ADC_REF_EXTERNAL0` / `ADC_SAMPLETIME_1CYCLE` 对片内温感通道（ch13）而言并非有效工作点；`ADC_VrefType_SetConfig()`（`adc_ls.c:256-293`）切走内部参考后温感转换即报错。

### 3.3 定性

- **用例预期错（主）**：用例假设"四次变体 setup 均被接受 + 还原默认后首读即有效"，但变体切换期间/之后会实际产生采样错误，用例既未预期、也无等待/重试。
- **驱动缺陷（新，另记）**：`ADC_RegularGetValue()` 把采样错误**静默降级**为 data=0，调用方无法区分"读到 0"与"转换失败"。这与 `KNOWN_ISSUES`/COVERAGE 里"`adc_ls_read()` 未做 NULL 校验"属同一类"错误路径不健壮"。

### 3.4 建议动作

- **用例（本轮不改）**：还原默认后加入建立延时/有限次重试（如 `k_msleep(1)` 后重读，或"最多重读 5 次直到 `raw > 0`"）；或把对片内温感无意义的参考源档（`ADC_REF_VDD_1`/`ADC_REF_EXTERNAL0`）从"断言读数"降为"只断言 `adc_channel_setup()==0`"。
- **驱动（另记，需用户裁定）**：`ADC_RegularGetValue()` 采样错误时应把错误码上抛，令 `adc_read()` 返回非 0。

---

## 4. `trng::isr_nonblock` —— 复核结论：**你的判断成立**

### 4.1 实测事实

`reports/2026-09-17-3/trng_test_case_default/trng_test_report.json:34-37`：`"isr_nonblock": { "got": "0", "len": "64" }`（该用例的 FAIL 未进入 md 表，原因见 §5.5）。

### 4.2 源码逐行复核（与你的理由一致，并补强到"池空"）

`zephyr/drivers/entropy/entropy_ls_trng.c`：

| 位置 | 代码事实 |
|---|---|
| `:119-121` | ISR `k_pipe_put(..., K_NO_WAIT)`，`written == 0`（池内可用 < 16 B）时执行 `regs->IE &= ~DWTRNG_IE_DONE_Msk` —— **池满自关中断** |
| `:147` | 阻塞路径 `ls_trng_get_entropy()` 每轮取前 `trng_irq_enable()` |
| `:169-172` | 非阻塞路径 `k_pipe_get(K_NO_WAIT)` **先取**，之后才 `trng_irq_enable()` —— **对本次调用已太晚** |

**补强（机制闭环）**：池 = 128 B（`:37`），ISR 每次写 16 B。阻塞读 128 B 的过程中 producer 曾把池填满 ⇒ 第 9 次 `put` 失败 ⇒ **ISR 自关中断**；consumer 随后把池**排空至恰好 128 B**（`copied == len` 退出循环，`:144-152`）。⇒ 阻塞读返回时状态为 **池空 + DONE 中断已关**；`k_msleep(20)` 期间无人重新使能 ⇒ 池不回填；非阻塞首调 `k_pipe_get(K_NO_WAIT)` 取到 0（`:169`），返回 0。

若按"自由运行回填"假设，池在 20 ms 内会重新填满 128 B，该调用应返回 64 —— 这正是 `COVERAGE.md:36-37` 写下的假设（"`k_msleep(20)` 等 IRQ 链回填（@~35Mbps 只需约 30µs）"）。

### 4.3 修法：**改用例**（不改驱动）+ 建议断言

驱动的"按需驱动 + 池满自关中断"是**有意的省中断设计**（`written==0` 分支显式存在），非缺陷；本项应改用例。建议把 `trng_test.c:458-471` 改为：

```c
/* 驱动为「按需驱动 + 池满自关中断」：阻塞读满 128B 后排空池并使 ISR 自关，
 * 池不会自行回填。非阻塞首调返回池中余量（此处为 0），并（在返回前）重新
 * 使能 DONE 中断；此后池才回填。 */
ret = entropy_get_entropy_isr(trng_dev, buf_isr_nb, sizeof(buf_isr_nb), 0);
ZT_INFO("isr_nonblock", "first_got=%d,len=%u", ret, (unsigned)sizeof(buf_isr_nb));
zassert_true(ret >= 0 && (size_t)ret <= sizeof(buf_isr_nb),
             "isr nonblock first got %d out of [0, %u]", ret, (unsigned)sizeof(buf_isr_nb));

k_msleep(20);   /* 上一次调用已重新使能 DONE 中断，池此时回填 */

ret = entropy_get_entropy_isr(trng_dev, buf_isr_nb, sizeof(buf_isr_nb), 0);
ZT_INFO("isr_nonblock", "second_got=%d,len=%u", ret, (unsigned)sizeof(buf_isr_nb));
zassert_equal(ret, (int)sizeof(buf_isr_nb),
              "pool not refilled after re-arm: got %d != %u", ret, (unsigned)sizeof(buf_isr_nb));
zassert_false(buf_is_const(buf_isr_nb, sizeof(buf_isr_nb), 0x00), "all 0x00");
```

（要点：**删除**"非阻塞接口应能一次取满 64B"这一依赖自由回填的断言；把"重新使能中断"这一步变成用例的显式前置。`NULL`/`len==0` 两条 `-EINVAL` 断言保持不变。）

### 4.4 待定

- 首调返回值在当前实现下**恒为 0**（池被恰好排空）属"逻辑上确定"，但未在实机上统计多次；若采用上述改法，首调断言取 `[0, len]` 即可规避。

---

## 5. 与既有缺陷档案的对应 / 新发现

### 5.1 与 `DEFECT_ANALYSIS_2026-09-17.md` 的对应

| 档案条目 | 与本轮关系 |
|---|---|
| 缺陷 #1 `wdt_iwdg_ls.c:45` 32 位回绕 | **无关**。回绕区 3 项 `test_load_ticks_*` 本轮**全 PASS**（实测 `ticks = 4294934 / 0 / 4294934`，与档案预期一致）。 |
| 缺陷 #2 `spif_add_cmd` 野写 | 无关（本轮未触及）。 |
| 缺陷 #3/#5 mdio/phy | 无关。 |
| 缺陷 #4 sdhci mutex | 无关。 |
| P1-3（`install_timeout:119` 清 CTRL 绕过 EBUSY 守卫） | **本轮未命中**，且方向与本轮失败相反（见 §1.5）。 |

**本轮 4 项均不在档案中**；其中 1 项（wdt 守卫）与 1 项（adc 采样错误静默）为**新增缺陷**。

### 5.2 新发现

1. **`wdt_iwdg_ls.c:73` EBUSY 守卫粒度过宽（驱动缺陷，P1）**：整寄存器非零判定；`feed` 后硬件把 `IWDT_CNT_CLR`(bit3) 留 1（写 0 清不掉）⇒ 此后任何 `setup` 恒 `-EBUSY`。影响面：任何 `setup → feed → disable` 之后再次 `setup` 的序列（`sec_iwdg` 已实测；`pmu_iwdg`/`app_iwdg` 同实现，理论同样中招）。
2. **`adc_ls.c` `ADC_RegularGetValue()` 采样错误静默降级为 data=0（驱动缺陷）**：`adc_read()` 仍返回 0，调用方无法感知转换失败。
3. **`ZTEST_SUITE` 第 5/6 参语义在本工程内使用不一致（模板问题）**：
   - `wdt_boundary_test.c:439`：`ZTEST_SUITE(wdt_boundary, NULL, setup, NULL, NULL, teardown)` ⇒ teardown 在位 **6（套件末尾跑一次）**，`after_fn` 为 NULL ⇒ **项间不清理**。
   - `wdt_test.c:128`：`ZTEST_SUITE(wdt, NULL, setup, NULL, teardown, NULL)` ⇒ 把 teardown 写在了位 **5（after_fn，每项后跑）**。
   两处语义不同，容易误判"项间是否会清理"，直接影响用例隔离性设计。建议沉淀给 `qsh-test-gen`（模板必须显式注明 6 个形参）与 `qsh-test-validate`（检查项）。
4. **SPI IP 复位默认 `CR1=0x10`（BR=/8）**（实测，见 §2）。
5. **TRNG 报告缺项根因**：`projects/trng_test/tests/run_test.py:311` 的 `test_names` 硬编码 9 项，未随 P1a 新增的 6 个 `ZTEST`（共 15 项）同步 ⇒ 新用例（含 `isr_nonblock`）不出现在报告里、失败 reason 无处可归。已在 `.claude/gotchas/INBOX.md` G-P6-3 记录（本轮复核确认）。

### 5.3 明确"未验证"的项目（分开陈述）

- 本轮**未**验证 `pmu_iwdg` / `app_iwdg` 在"setup→feed→disable"后是否同样残留 `IWDT_CNT_CLR`（同实现、理论成立，**未实测**）。
- 本轮**未**修任何代码，故以上"建议动作"均为**未经实测验证的修改建议**。
- `spi` 的 IP 复位默认值**无仓库文档背书**（见 §2.3）。
- `adc` 的"哪一档变体触发采样错误"**未逐档定位**。

---

## 6. 分组待修清单

### A. 需改驱动（`zephyr/`，需用户裁定；本轮未改）

| # | 位置 | 改动 | 影响用例 |
|---|---|---|---|
| A1 | `zephyr/drivers/watchdog/wdt_iwdg_ls.c:73` | 守卫改按位：`if (IWDT_CTRL & (IWDT_EN_MASK \| IWDT_RST_EN_MASK)) return -EBUSY;` | 本 2 项 FAIL 转 PASS；无其它用例需改判（回绕区 3 项不受影响） |
| A2 | `zephyr/drivers/adc/adc_ls.c` `ADC_RegularGetValue()` | 采样错误时上抛错误码，而非静默返回 0 数据 | 无既有用例断言该行为；改后 `adc_read()` 会返回非 0，`test_adc_setup_variants` 的重试/延时需一并配合 |

### B. 只需改用例（测试工程源码）

| # | 位置 | 改动 |
|---|---|---|
| B1 | `projects/spi_test/cpu1_spi_test/src/spi_test.c:801` | 断言期望由 `0` 改为 `SPI_LS_BR_DIV8`(0x10)；同步 `tests/COVERAGE.md:103` 预期列 |
| B2 | `projects/trng_test/cpu1_trng_test/src/trng_test.c:458-471` | 删"一次取满 64B"断言；改"首调 `[0,len]` + 重使能 + 二次调 `==len`"（见 §4.3）；同步 `tests/COVERAGE.md:25,36-37` |
| B3 | `projects/adc_temper_test/cpu1_adc_test/src/adc_temper_test.c:222-229` | 还原默认后加建立延时/有限次重试；或把外部参考档降为"仅断言 `channel_setup()==0`" |

### C. 只需记录 / 不改码

| # | 项 |
|---|---|
| C1 | P1-3（`install_timeout` 清 CTRL 绕过守卫）本轮**未命中**，维持既有登记，勿与本次两项混号 |
| C2 | `wdt_iwdg_ls.c:45` 32 位回绕缺陷维持待裁定；本轮 3 项回绕断言全 PASS（预期值无需改） |

### D. 模板 / 框架（建议沉淀，不改码）

| # | 项 | 归处 |
|---|---|---|
| D1 | `ZTEST_SUITE` 第 5 参(after)/第 6 参(teardown)语义在本工程两处用法不一致 | `.claude/gotchas/INBOX.md` → keeper 沉淀到 `qsh-test-gen` 模板规则 + `qsh-test-validate` 检查项 |
| D2 | `trng_test/tests/run_test.py` 的 `test_names` 硬编码 9 项 ≠ 固件 15 项，导致新用例漏报 | 同上（INBOX G-P6-3 已记） |

---

## 7. 落盘确认

- **交付文件**：`d:\zephyr4_0\810_driver_test\TRIAGE_MISC_2026-09-18.md`（本文件，新建）
- **未改动**：`zephyr/`、`modules/`、任何测试工程、`common/`、`regression/`、`scripts/`、任何既有文档。
- **未派**任何子 agent；未运行 keeper。
- 本轮为取证执行的实机操作：`case_boundary`、`case_default`(adc) 各 1~2 次烧录采集（`dbg_run.py`），以及 1 次 GDB 只读寄存器探针（临时脚本 `C:\Users\26502\AppData\Local\Temp\wdt_probe.gdb`，工程外文件）。硬件操作**串行**完成，结束后已杀掉本次启动的 DebugServer 进程（恢复任务开始前的状态）。
