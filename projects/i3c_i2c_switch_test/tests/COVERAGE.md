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
| i3c_baseline | i3c_do_daa（DAA 分配 0x32）+ i3c_transfer 私有 WRITE/READ（I3C_MSG_STOP） | 功能通路 | 写 0x00..0x0F 16B；读 0xAA..0xB9 16B | 协议约定（pattern 定义） | ✅ |
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

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
> 来源标签：`handcalc`(手算) `spec`(外部规范/寄存器或 DT 定义) `golden`(黄金向量)
> `crosscheck`(跨来源交叉) `contract`(API 契约值) `driversrc`(转录自被测驱动实现，
> **只证"没变"不证"仍对"**) `samecode`(同源复算，**非独立**) `!none`(无确定预期)。

```expect
# 测试项 | 断言 | 来源标签:说明
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
