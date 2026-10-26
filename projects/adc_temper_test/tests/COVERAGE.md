# ADC（adc_ls）测试覆盖矩阵

> P1a 建档（2026-09-17）：本工程此前**无 COVERAGE.md**，按 `SPEC_REFERENCE.md` §6.5.3 模板新建。

- 驱动: `zephyr/drivers/adc/adc_ls.c`（`linkedsemi_ls_adc`；本板别名 `testadc` → `adc1`、`testadc2` → `adc2`）
- 工程: `810_driver_test/projects/adc_temper_test`
- 类型: **单核**（只有 `cpu1_adc_test/`）→ board `lsqsh_evb@1os/lsqsh/cpu1`，cpu1 入口 `0x10000000`
- 外部路径: **无**（`adc1` / `adc2` 各用片内温感通道 `ch13`，`ls_hal_adcv2.h`：internal channel、无外部引脚）
- 套件: `adc_temper`（`ZTEST_SUITE(adc_temper, NULL, adc_temper_setup, NULL, NULL, NULL)`，setup 为**套件级**）
- 接口: `adc_channel_setup()` / `adc_read()`

## 关键驱动语义（写用例前必读）

1. `adc_ls_channel_setup()` 每次调用 `data->conversion_rank++`；`rank > config->nbr_of_conversion`
  （本板 `nbr_of_conversion = 1`）即返回 `-EINVAL`（`adc_ls.c:644-681` 的 else 分支
  "Regular Channel Mode Illegal rank"）。
2. **只有一次成功的 `adc_read()`** 会经 `ADC_GetValue()` 把 `conversion_rank` 清 0
  （`adc_ls.c:253`）。所以"连续两次 channel_setup"必然第二次 `-EINVAL`，
  而当用例需要多次 setup 时必须**在每次 setup 前先做一次成功读取**来复位 rank。
3. `adc1` / `adc2` 的 `ch13`（温感）在 `adc_ls_channel_setup()` 里有专门快路径
  （`adc_ls.c:621-637`）：写 `MISC.ADC_REG_SMPHLD=1`、`BYP_CFG=0xffffffff`、
  `REG_CTRL0.SEQ00=13 / SEQ01=1`、`REG_CTRL1=0`、`CH_CFG` 与 `INJ_CTRL.REG_SEQLEN`。
4. 参考源/驱动类型分支在 `ADC_VrefType_SetConfig()`（`adc_ls.c:256-293`）：
  `ADC_REF_VDD_1` / `ADC_REF_EXTERNAL0` / `ADC_REF_INTERNAL`(default) → 不同 ADR 字段组合。
5. 温度换算（本工程自用公式）：`T = (2330.7 - raw) / 4.5807`（`raw_to_celsius()`）。

## 覆盖矩阵（套件 `adc_temper`，**10 项**；2026-09-18 删 1，2026-10-26 新增双温感 1）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_adc_read_returns_ok | `adc_ls_read()` 软触发 + 100ms 超时轮询（`adc_ls.c:721-745`） | 功能通路 | `adc_read() == 0` | API 契约 | ☐ |
| test_adc_raw_not_boundary | `ADC_RegularGetValue()` 取值（`adc_ls.c:89-164`） | 统计/性质 | `raw != 0 && raw != 0xFFFF` | 传感器非退化 | ☐ |
| test_temperature_in_range | 同上 + 换算公式 | 统计/性质 | `T ∈ [-40,125]` 且 `∈[10,40]` | 量程 + 室温 | ☐ |
| ~~test_adc_raw_to_celsius_pure_function~~ | **已删除（2026-09-18）**：测本工程自己的 `raw_to_celsius()`，期望值由同一条公式反解、**全程零驱动调用** ⇒ 驱动全坏也不 FAIL | — | — | — | ❌ 删除 |
| test_sampling_stability | 连续 20 次 `adc_read()` | 统计/性质 | **`min_raw > 0`**（堵死 N2 全零路径）且 `max−min < 50` counts | API 契约（有效转换必 > 0）+ 温感噪声上界（环境前提） | ☐ |
| test_multiple_adc_reads | 连续 5 次 `adc_read()` | 功能通路 | 每次 `ret == 0` | API 契约 | ☐ |
| **test_adc_channel_setup_invalid** | `adc_ls_channel_setup()` 入参校验分支（`adc_ls.c:609-612`） | 异常参数 | `NULL` / `channel_id=16` / `acquisition_time=4` → `-EINVAL`（三条均在 rank++ 前返回，不改寄存器/rank） | API 契约 + 驱动源码 | ⏳ |
| **test_adc_setup_variants** | `ADC_VrefType_SetConfig()` 三参考源分支（`:262/267/272`）+ `adc_ls_channel_setup()` 采样时间字段（`:640`）；**还原默认后经有限次建立重试取有效转换** | 功能通路+边界长度 | `acquisition_time ∈ {1cycle, 15cycles}` 与 `reference ∈ {INTERNAL, VDD_1, EXTERNAL0}` 四次 setup 均 `== 0`；还原默认后 ≤50×10 ms 重试窗口内取到 `raw > 0` 且 `T ∈[10,40]` | 驱动源码 + 量程；**`raw==0` 的语义 = 采样错误被驱动静默吞掉（缺陷 N2：`ADC_RegularGetValue()` `adc_ls.c:89-104` 的 ERR 分支只清标志、不写 buffer，而 `adc_read()` 仍返回 0），不是"电压真是 0"**；变体档（VDD_1/EXTERNAL0/1cycle）对片内温感非有效工作点，切换期间确实产生采样错误。驱动 N2 修复后本项需改判。见 TRIAGE_MISC_2026-09-18.md §3 | ⏳ |
| **test_adc_rank_guard** | `adc_ls_channel_setup()` 的 rank 守卫 else 分支（`:678-681`） | 异常参数 | 连续两次 setup（中间无 read）→ 第二次 `-EINVAL`；复位后可正常读出 | 驱动源码 `#2` | ⏳ |
| **test_adc_full_scale_bound** | `ADC_RegularGetValue()` + init 里的 `ADC_RES` 置位（`:478-482`） | 边界长度 | `raw ∈ (0, 4095]`（12 位满量程）；`T ∈ [-40,125]` | `reg_adcv2_type.h` `ADC_ADC_RES_MASK` | ⏳ |
| **test_dual_sensor_agree**（2026-10-26 新增） | 两个 ADC 实例的 `adc_ls_channel_setup()` + `adc_read()`（各自 `ch13` 温感快路径） | 功能通路 + 跨实例一致性 | ADC1/ADC2 各自的 `T ∈ [0,50]` ℃；两路差值 ≤ **5** ℃；`ZT_INFO` 上报 `raw1/t1/raw2/t2/diff` | 用户需求（合理范围 + 差值）；sample README 明确 “ADC1/ADC2 的 13 通道专用测温感” | ⏳ 待编译/实测 |

> 状态：`☐` = 既有项（历史未回填勾选）；`⏳` = **P1a 新增，仅编译通过，待 P6 首次实测**。

## P1a 新增项说明（2026-09-17，只生成+编译通过，未烧录）

- 新增 4 项：`test_adc_channel_setup_invalid` / `test_adc_setup_variants` /
  `test_adc_rank_guard` / `test_adc_full_scale_bound`；全部**追加**，未改任何既有用例。
- 唯一对既有文件的追加式改动：`adc_temper_test.c` 顶部加一行 `#include "ztest_glue.h"`
  （ZT_INFO 宏来源；既有用例未用 INFO 行，此前不需要该头）。
- **跳过项**：
  - `channel_scan`（片内可用通道扫描）—— 本板 `adc1` 只有片内温感 `ch13` 有确定读数；
    对其余 `channel_id` 调 `adc_channel_setup()` 会走 `ADC_RegularChannelConfig()` 写
    `REG_CTRL0/1` 的 SEQ 槽位（把温感所在的 SEQ00 改写掉），既**无外部电压源可判定数值**，
    又会破坏本套件后续用例的通道配置 → **不硬做**（"扫描 setup 是否返回 0" 也不构成有效断言：
    驱动对任意 `channel_id < 16` 都返回 0）。
  - `invalid_args` 中的"空 buffer / NULL sequence" —— 不放入：`adc_ls_read()` 未做
    `sequence` 空指针校验，`adc_context_start_read()` 会直接解引用 → 必崩（load access fault），
    属**驱动缺陷**而非可断言契约，已记入跳过清单交给用户裁定。
  - `sample_rate`（采样率边界）—— `adc_clk_div` / `clk_cfg` 是 DT 属性（overlay 固定 74 / DIV1），
    运行期不可改；运行期可改的只有 `acquisition_time`，其边界已并入 `test_adc_setup_variants`。
  - ADC 超时分支（`-ETIMEDOUT`，`:733-737`）—— 需制造"转换永不结束"，平台内无法注入。

## 修订（2026-09-18，测试有效性审计 A 组 P0-3）

### 删除 `test_adc_raw_to_celsius_pure_function`
- **原状**：测的是本工程自己的 `raw_to_celsius()`，期望值由**同一条公式**反解，
  全程**零驱动调用** ⇒ 驱动整体损坏也不会 FAIL（不可能证伪）。
- **处置**：删除该 ZTEST（不保留替代断言）。该公式仍作测试侧换算辅助被
  `test_temperature_in_range` / `test_adc_setup_variants` / `test_adc_full_scale_bound`
  使用，但**其自身正确性不再是覆盖声明**。项数 10 → 9。
- 未做降级为"编译期常量自检"的原因：C 的 `_Static_assert` 不接受含浮点的整型常量
  表达式，公式无法在编译期求值，写不出真正的编译期检查；留一个必然 PASS 的运行时
  自检等于保留空壳。

### `test_sampling_stability` 加"最小值必须 > 0"
- **原状**：只断 `max−min < 50` 与 `raw >= 0`。驱动走 **N2 静默降级**（每次写 0）时
  `min=max=0` ⇒ `range=0` **PASS**；`raw>=0` 对取值恒成立（`uint16` 语义上不可能 <0）。
- **改**：新增 `zassert_true(min_raw > 0, ...)`。
- **驱动正确时必过 / 典型失效时必不过**：温感在室温下有效转换必产生 > 0 的 12-bit 码
  （与 TC-2 `raw != 0`、TC-8 `raw > 0` 同一条不变量）⇒ 必过；驱动静默降级（返回 0 但
  不写 buffer，缺陷 N2）或传感器未工作时读数恒 0 ⇒ 必不过。
- 阈值 `range < 50` **保留并如实标注为环境前提**（恒温环境噪声上界，非实测标定值），
  仅拦"读数剧烈跳变"，不声称已标定。
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
test_adc_read_returns_ok | ret == 0 | contract:API 契约（★本工程归档无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
test_adc_raw_not_boundary | raw != 0; raw != 0xFFFF | contract:传感器非退化（★无 INFO 通道 ⇒ NO_ACTUAL）
test_temperature_in_range | T in [-40,125] | spec:温感量程（★无 INFO 通道 ⇒ NO_ACTUAL）
test_sampling_stability | min_raw > 0 | contract:有效转换必 > 0，堵死全零路径（★无 INFO 通道 ⇒ NO_ACTUAL）
test_multiple_adc_reads | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
test_adc_channel_setup_invalid | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_adc_setup_variants | raw > 0; t_deg in [10,40] | handcalc:还原默认后重试窗口内必取到有效转换（raw>0）且为室温（原文给定 [10,40]℃）
test_adc_rank_guard | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_adc_full_scale_bound | raw in [1,4095] | spec:12 位满量程 ADC_RES 掩码（★无 INFO 通道 ⇒ NO_ACTUAL）
```
