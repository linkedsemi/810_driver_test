# i3c_i2c_switch_test 测试覆盖矩阵

**被测对象**：I3C5/I3C6 硬件模块在 I3C 与 I2C 两种模式间的切换，模式由**引脚复用
（pinctrl）**区分（外部接线复用 i3c_dualcore_test 的 I3C5↔I3C6 两根线）。
驱动路径参照 `zephyr/drivers/i3c/i3c_linkedsemi.c`（I3C 控制器）+ `i3c_target_linkedsemi.c`（I3C 目标）、
`zephyr/drivers/i2c/i2c_ls.c`（I2C 主机/从机）与 pinctrl 动态切换
（`pinctrl_apply_state()`，CONFIG_PINCTRL_DYNAMIC）。

**预期数据约定**（协议头 `tests/include/i3c_i2c_switch_test_protocol.h` 为单一事实源）：
- 写 pattern：`W <len> <0x00,0x01,...,0xlen-1>`（递增序列，target 逐字节校验）
- 读 pattern：`TGT_READ_PATTERN_BASE(0xAA) + i`（target 预填 `tgt_tx_buf[256]`）
- 命令通道：I3C 模式经 i3c5 私有写（target 0x32）；I2C 模式经 i2c5 写（slave 0x58）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| pinctrl_states | pinctrl_apply_state 配置表（i3c5/i3c6 节点 states[0]=default、states[1]=pinmux_i2c） | 功能通路 | state_count==2 且 states[1].id==PINCTRL_STATE_PRIV_START | 例程 i3c_i2c_exchange 配置约定（app.overlay pinctrl-names） | ✅ |
| i3c_baseline | i3c_do_daa（DAA 分配 0x32）+ i3c_transfer 私有 WRITE/READ（I3C_MSG_STOP）；**case_no_bus_init 另加**：i3c_dev_list_find（PID 查表）+ ls_i3c_transfer 的 `dynamic_addr==0 → -EINVAL` 守卫 + 应用侧 i3c_bus_init() | 功能通路 / 复位掉电（未枚举态） | 写 0x00..0x0F 16B；读 0xAA..0xB9 16B；case_no_bus_init 另需 nbi_neg_dyn=0、nbi_neg_xfer=-22、nbi_init_ret=0、nbi_after_dyn=50（=0x32） | 协议约定（pattern 定义）；case_no_bus_init 的 A/B 判据见下方「no-bus-init 开关验证」节 | ✅ |
| switch_i3c_to_i2c | pinctrl_apply_state(PINMUX_I2C)（i3c5 节点 pinctrl-1 配置 I2C5_scl/sda_PJ10/11） | 中断/时序 | 切换后 i2c5↔i2c6 回连 16B 写读（target 侧 100ms 延时先行） | 例程验证 + 协议时序（CTRL_SWITCH_DELAY>TGT） | ✅ |
| i2c_rw_1 | i2c_ls_transfer / i2c_ls_write_fifo / RXNE 读（单字节路径） | 边界长度 | 写 1B 读 1B（0xAA） | API 契约 | ✅ |
| i2c_rw_16 | i2c_ls 写读 FIFO 主路径（16=FIFO 深度上限） | 边界长度 | 写 0x00..0x0F，读 0xAA..0xB9 | API 契约 | ✅ |
| i2c_rw_17 | i2c_ls 写读跨 FIFO 深度边界（16→17） | 边界长度 | 写 0x00..0x10，读 0xAA..0xBA | API 契约 | ✅ |
| i2c_rw_100 | i2c_ls 大块传输（多段 FIFO 装填循环） | 边界长度 | 写 0x00..0x63，读 0xAA..0x109 模 256 | API 契约 | ✅ |
| i2c_nack_bad_addr | i2c_ls_transfer NACK 分支（MASTER_NACK_RECEIVED → 返回错误） | 异常参数 | 访问 0x70 返回非 0（预期 NACK） | API 契约 | ✅ |
| switch_i2c_to_i3c | pinctrl_apply_state(DEFAULT) + 重新 i3c_do_daa + i3c_transfer 复验 | 中断/时序 | 切回后 DAA 重新发现 target，16B 写读一致 | 例程验证 | ✅ |
| switch_stress | pinctrl_apply_state 双状态循环 ×5 轮（每轮 I3C→I2C→I3C 各一次 16B 写读） | 并发/时序 | 5 轮全部通过（**双核均同 id 粘滞累计**：CTRL `ctrl_failed` 与 TGT `tgt_failed`/`tgt_data_mismatch` 都只在换测试项时清，同 id 子轮不清） | 协议约定（SWITCH_STRESS_ROUNDS=5） | ✅ |
| broadcast_nack_recovery | i3c_linkedsemi.c ls_i3c_do_ccc 广播帧（RSTACT whole/peripheral、RSTDAA）+ ls_i3c_xfer_reset 残留清理 + EVR BUSY(bit12) 判空 | 异常/恢复 | 'N' 窗口内首帧 RSTACT whole **不报错**（原「应 NACK」判据不可达，见下方「已知基线」）；target 自动切回后 RSTACT peripheral / RSTDAA 应正常；帧末 EVR BUSY=0 | 驱动修复回归（2026-09-09）+ **2026-09-24 上板实测裁定** | ✅ |

> 维度对照 §13.1：功能通路（pinctrl_states/i3c_baseline）、边界长度（i2c_rw_1/16/17/100）、
> 异常参数（i2c_nack_bad_addr）、异常/恢复（broadcast_nack_recovery）、中断/时序（switch 两向 + stress）。
> 外部路径（§14）：I3C5/I3C6 跨核对连，接线见顶层 README「外设接线记录」，
> I2C 模式复用同一组线。切换时序依赖（TGT 100ms < CTRL 250ms）已在协议头固化。
>
> **2026-09-18 修复（F5 波）**：
> - `switch_stress` TGT 侧**粘滞口径**：协议每轮发 3 次 `'B'`（I3C→I2C→I3C
>   三个子轮）却只在轮末 `'E'` 判账。原 `tgt_handle_begin` 每次都清
>   `tgt_data_mismatch` ⇒ 前 2 个子轮的写通道损坏被后一次 begin 抹掉，
>   TGT 侧永远抓不到（CTRL 侧 `ctrl_failed` 本就同 id 粘滞，两侧口径不一致
>   会让"双核双 PASS"里 TGT 一半失去判别力）。现改为**仅在换测试项
>   （`id != tgt_last_id`）时清**，本项内所有子轮的失配累积到 `'E'` 一并判账。
> - `pinctrl_states` 失败分支**越界读守卫**：原实现无条件打印
>   `states[0].id / states[1].id`，`state_cnt < 2` 时越界（内存安全缺陷）。
>   `ok` 的短路求值本身安全，越界只在失败分支的格式化参数里；
>   现两核（`i3c_i2c_controller_test.c` / `i3c_i2c_target_test.c`）各加
>   `state_cnt >= 2` 守卫，不足时只报 `state_cnt:N (<2)`。
> - **2026-09-20 修复（R7 波）**：**no-op token 宏已根治** —— 协议头
>   `i3c_i2c_switch_test_protocol.h` 的 `I3C_I2C_*` 宏从 `do {} while (0)` 恢复成
>   真 `printk`（`I3C_I2C_TEST:<CTRL|TGT>:(PASS|FAIL|SKIP):<name>[:reason]`），
>   `common/ztest_report.py::parse_ztest` 认领该形态 ⇒ 11 项在两核各有一条**串口逐项
>   证据**（此前只有 `ZT_BOOT` + `PROJECT EXECUTION`，11 项全靠 GDB 内存兜底，是
>   O-13「弱证据 PASS」的根因）。同时批量判定链加 G9/D6 守卫：整个 case 一条 token
>   都没有 ⇒ 不得判 PASS（`common/judge.py::mem_only_evidence`）。
>   **未实机验证**（本轮不占硬件）：改动经宿主 gcc 宏展开 + `parse_ztest` 端到端合成
>   回放验证，真机复跑留待下一次硬件回归。
> - **仅记录、本轮未改**（见父会话报告 §3）：`broadcast_nack_recovery` 靠固定
>   `k_sleep(6ms)` 抢窗口（`i3c_i2c_controller_test.c:549`，时序抖动直接判
>   FAIL 且归因给 DUT）；硬编码 `0x40094000`（`i3c_i2c_controller_test.c:533`，
>   同文件其它处均用 `DT_REG_ADDR`）；`PROJECT EXECUTION` 标记只反映最后一项
>   （`ctrl_failed` 每个新 id 的 begin 时被清）。
---

## 已知基线（裁定项，**不是待修缺陷**）

> 目的：让后来者**不要**把下面这条当成"测试写法有误 / 驱动回归"去反复重开调查。
> 与 `spif_test::cmd_idx_accessors`、`spi_test::link_probe` 同性质 —— **保持现状是裁定**。

### B-1. `broadcast_nack_recovery` 的「窗口内广播 RSTACT whole 应被 NACK」断言 **在本硬件上不可达**

- **裁定日期**：2026-09-24（上板判别实验，证据见
  `reports/2026-09-24-1/i3c-b1b2-experiment.md`）。
- **实测事实（时间扫描 + target 自报窗口标记，两个独立轮次一致）**：
  NACK 窗口实测为 `[t≈5ms, t≈15ms]`（target 自报 `win=enter/exit`）；**落在窗口内的
  16B 回读无任何数据被时钟出来**（缓冲区保持 `0xDE` 哨兵），形貌与"读无器件地址
  `0x70`"完全一致 —— ⇒ **窗口内 target 确实已离开 I3C 总线**（不是"没切走"）。
  窗口前后同一读稳定回 target 的 `0xAA..0xB9`。
- **⇒ 该帧确实是"无人应答"，但控制器/驱动对广播帧的同步错误检查不报它**
  （`ls_i3c_do_ccc` 只看 `EVR.FCF|ERRF`；实测 `SER=0x0`、返回值恒 `0`）。
  「`r1 != 0`」这一写法**结构上不可能成立**，**不是**用例写错、**不是**驱动回归。
- **现判据**：窗口内发广播 **不报错、能正常收尾**（`r1 == 0` 判 PASS）；原始返回
  值仍以 `INFO ... win_broadcast_ret=` 上报串口与报告，**不进判据** —— 保留现场，
  便于将来若驱动侧改成"异步复查 `SER/ERRF`"时把它重新收紧回 `r1 != 0`。
- **未动**的断言：RSTACT peripheral / RSTDAA 必须成功、帧末 `EVR.BUSY(bit12)` 必须清零。
- ⚠️ **不要**为了让这条变绿去放宽其它断言，也不要把它当驱动 bug 重开调查。

### B-2. `broadcast_nack_recovery` 结账前必须重跑一次 DAA（`'E'` 可达性）

- **现象（2026-09-24 定位，同样的既存缺陷）**：第三帧 `RSTDAA` 会清掉 target 的
  **硬件动态地址 `0x32`**，而 controller 侧 dev list 的 `desc->dynamic_addr` 不同步清
  ⇒ 紧随其后的结账命令 `'E'` 被打到已不存在的地址上、**target 收不到** ⇒ TGT 侧该项
  **恒无记账**（`SUMMARY 10/11`、双核判定里 TGT 恒 `MISSING`）⇒ 该 case **结构上不可能
  双核 11/11**（历史归档中该行 `来源=missing` 即此形貌）。
- **现处置**：结账前先 `i3c_do_daa()` 恢复 `0x32`（与 `test_main` 收尾处**本来就要做的
  同一套动作**，只是提前到 `'E'` 之前；幂等、不新增被测行为、判据一字未改）。
- 实测：`reports/2026-09-24-1/i3c_i2c_switch_test_case_switch_core/` **双核 11/11 PASS**。

---

## no-bus-init 开关验证（`case_no_bus_init`，2026-09-28 新增）

**被测改动**：`zephyr/drivers/i3c/i3c_linkedsemi.c` 的 DTS 布尔属性 `no-bus-init`
（binding：`zephyr/dts/bindings/i3c/linkedsemi,i3c.yaml`）。**opt-out 语义** —— 控制器写了它，
`ls_i3c_init()` 里 `if(!dev_config->no_bus_init && dev_config->common.dev_list.num_i3c > 0)`
守卫为假 ⇒ 驱动 init **不调** `i3c_bus_init()`（跳过 RSTDAA / DISEC / SETDASA / DAA / SETAASA），
改由应用自行调用。不写该属性时行为与改动前逐字相同。

**前提（用例判别力的必要条件，固件里显式自检）**：`&i3c5` 有 `i3ctarget6` 子节点
⇒ `dev_list.num_i3c == 1` ⇒ 守卫里的 `num_i3c > 0` 成立。若为 0，驱动本来就不调
`i3c_bus_init()` ⇒ 开关无可观测差异，用例无判别力（固件判 `nbi:premise_num_i3c_0`）。

**落点与形状**：case 差异只在 `controller.overlay`（`&i3c5 { no-bus-init; }`）+ 固件里
一段受 `#if DT_PROP(DT_NODELABEL(i3c5), no_bus_init)` 门控的 A/B 代码。
**不用 Kconfig 门控**：本工程 cpu0 无应用 Kconfig 根，新增一个会改 `KCONFIG_ROOT`
（qsh-test-gen §2「变体」注里点名的爆炸半径）；DT 属性门控天然与"开关被配置"同频，
且既有 case_switch_core 的固件**逐字节不变**。
⚠️ 门控**必须**用 `DT_PROP` 而非 `DT_NODE_HAS_PROP`：boolean 属性一旦在 binding 声明，
所有实例的 `_EXISTS` 恒为 1（实测同族未设值的 `is_secondary` 也是 `_EXISTS 1`）
⇒ `DT_NODE_HAS_PROP` 门控恒真，A/B 会在既有 case 里也跑起来并误判 FAIL。
真正的"写了没有"在值宏里（`DT_PROP` = 0/1）。首次实现即踩此坑，静态复核 ELF 后修正
（见「已知基线」上方本节的实现说明）。

**A/B 折进 `i3c_baseline` 项**（不新增测试项名）：`run_test.py::TEST_NAMES` 是**全局**表，
新增项会让既有 case_switch_core 因该项在两核皆 `MISSING` 而判 FAIL（`evaluate_case` 里
`test_pass = CTRL==PASS and TGT==PASS`）。本工程 target 侧校验是**通用**的
（'B'/'E' + 写 pattern 逐字节），故 ② 之后的正常 'B'/16B 写读/'E' 直接给出 TGT 侧证据，
**target 侧固件零改动**。

**判据（两半缺一不可；分开跑无意义 —— 只有"同一硬件 ①失败 / ②成功"把结论钉在开关上）**：

| 半 | 断言 | 形态 | 为什么可辩护 |
|----|------|------|--------------|
| ① | `nbi_neg_dyn == 0` | 驱动 desc 的 `dynamic_addr` 为 0 | `dynamic_addr` 只由 DAA / SETDASA 写入，二者都在被跳过的 `i3c_bus_init()` 里 ⇒ 非 0 即"开关没生效"。**确定性**判据 |
| ① | `nbi_neg_xfer == -22`（`-EINVAL`） | `i3c_transfer()` 对 `dynamic_addr==0` 的 desc | 驱动 `ls_i3c_transfer` 里 `if (target->dynamic_addr == 0U) return -EINVAL;` —— **确定性**、不碰总线，不依赖"NACK 是否被上报"。原样上报，串口可见 0 值即失败 |
| ① | 附着表为空 | `i3c_dev_list_i3c_addr_find(dev, 0x32) == NULL` | 与上一条互为独立证据（附着表在 DAA 期间填充） |
| ① | `nbi_probe32_ret` / `nbi_probe32_act` | 动态地址临时设成 `0x32` 后发一笔 16B 读 | **总线级**旁证：此刻 target 硬件侧 `DAVALID=0`（从未被赋址）⇒ 无人应答。判据取**安全形** `ret != 0 \|\| actual == 0`（与 `i3c_dualcore_test::nack_bad_addr` 同口径 —— 本硬件上"地址 NACK 是否被上报"随总线时序变化），原始值走 INFO 上报 |
| ② | `nbi_init_ret == 0` | 应用显式调 `i3c_bus_init(dev, dev_list)` | API 契约；`dev_list` 经 `i3c.h` 明文契约取（`struct i3c_driver_config` 是所有 I3C 驱动 config 的**首成员**） |
| ② | `nbi_after_dyn == 50`（`0x32`） | `i3c_bus_init()` 后 desc 的动态地址 | DT `assigned-address = <0x32>` + DAA 赋址；随后 `ctrl_basic_rw()` 的 16B 写读必须通过（这也给出 TGT 侧证据） |

**判失败的理由串**（进 CTRL 侧 `i3c_baseline` 的 FAIL token，前缀 `nbi:`）：
`premise_num_i3c_0` / `neg_desc_not_found` / `neg_dynaddr_<n>` / `neg_attached` /
`neg_xfer_ret_<n>` / `neg_probe32_data` / `init_ret_<n>` / `recover_dynaddr_<n>` /
`recover_not_attached`。

**证据来源标注（2026-09-28 上板实测后更新）**：① 的"应失败"与 ② 的"应恢复"原为 ③级软件位映射
推断（从驱动守卫与 DT 属性读出），**已在 `reports/2026-09-28-1/` 上板实测坐实**：`case_no_bus_init`
双核 11/11 PASS，CTRL INFO 实测 `num_i3c=1,nbi_neg_dyn=0,nbi_neg_xfer=-22,nbi_probe32_ret=-5,
nbi_probe32_act=0,nbi_init_ret=0,nbi_after_dyn=50`。判读规则与三次问答见该会话目录的
`i3c-no-bus-init-ab.md`（含"探针混淆"standing 判读规则）。**本次 n=1**，长期结论未成立。

**已知风险（standing，须保留）**：① 的总线级探针会向无人地址发一笔读 —— 若该笔 NACK
留在控制器里影响紧接的 ② `i3c_bus_init()`，则 ② 失败会**误归因给驱动**。
**本次未触发**（② 通过）。将来若 ② 转红，**先按会话目录 §0 的 standing 规则排除探针**
（把 ① 的探针段 `#if 0` 掉重跑，两次结果都进报告），再谈驱动。

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
# 注：i3c_baseline 在 case_no_bus_init 下另含 A/B 的标量键（nbi_neg_dyn / nbi_neg_xfer /
#     nbi_init_ret / nbi_after_dyn）。本表**按测试项名**登记且被两个 case 共用，而
#     case_switch_core 不产出这些键 ⇒ 登记在此会把既有 case 从 NO_EXPECTATION 翻成
#     NO_ACTUAL（同类覆盖缺口、纯噪音）。故暂留 !none，A/B 的预期值记在上文
#     「no-bus-init 开关验证」节（含义、来源与可辩护性逐条写明）。
pinctrl_states | !none | !none:state_count/states[1].id 的合取（结构性质）
i3c_baseline | !none | !none:16B pattern 写读，非标量
switch_i3c_to_i2c | !none | !none:切换后回连 16B 写读，非标量
i2c_rw_1 | !none | !none:1B 写读，非标量
i2c_rw_16 | !none | !none:16B pattern 写读，非标量
i2c_rw_17 | !none | !none:17B pattern 写读，非标量
i2c_rw_100 | !none | !none:100B pattern 写读，非标量
i2c_nack_bad_addr | !none | !none:访问 0x70 返回非 0，单值但键名未在 INFO 中固定 ⇒ 不硬凑
switch_i2c_to_i3c | !none | !none:切回后 DAA 重新发现 + 16B 写读一致，非标量
switch_stress | !none | !none:5 轮双核粘滞计数的合取
broadcast_nack_recovery | !none | !none:三处协议断言的合取
```
