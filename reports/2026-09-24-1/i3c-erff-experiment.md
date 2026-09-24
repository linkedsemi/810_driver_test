# i3c `broadcast_nack_recovery` — EVR/SER 寄存器决定性实验

- **日期**：2026-09-24
- **板卡**：`lsqsh_evb@2os`（cpu1 = target / cpu2 = controller）
- **用例**：`i3c_i2c_switch_test` → `case_switch_core` → `broadcast_nack_recovery`
- **方法**：GDB 断点读寄存器，**零源码改动、零探针**（见「探针纪律」）
- **结论**：**情形 B —— 判据在此硬件上不可达**（控制器对"无人应答的广播"报 FCF，ERRF / SER 均无错误位）
  ⚠️ **2026-09-24 推论边界更正（见 §5）**：此处"无错误位"是**同步**读数，只证明"**同步检查
  看不到**"、**不**证明"硬件不置位"（实测 ANACK 晚 ~15 ms 到位）⇒ 正确理由 = "**驱动检查窗口过窄**"。
  ⚠️⚠️ **2026-09-24 二次更正（同日）**：上一句"实测 ANACK 晚 ~15 ms 到位"**依据不成立** ——
  `prj.conf:2` 为 `CONFIG_LOG_MODE_DEFERRED=y` ⇒ **打印时刻 ≠ 生成时刻**；且该笔 `ret=0`，与通用路径
  检出错误即 `return -EIO` 矛盾 ⇒ 那条日志来源与生成时刻均未定。⇒ **"驱动检查窗口过窄"与"硬件无
  置位能力"两侧均无据，回到"未定"**。详见 §5 与 `OPEN_ITEMS.md` O-39。

---

## 1. 被测寄存器与偏移（自证，非猜测）

基址 `i3c5 = 0x40094000`（来源：`cpu0_i3c_controller/src/i3c_i2c_controller_test.c:550`
`I3C_TypeDef *base = (I3C_TypeDef *)0x40094000`；与控制器 build 的 `zephyr.dts:2364`
`i3c5: i3c@40094000 { reg = <0x40094000 0x2000> }` 一致）。

偏移取自 `modules/hal/linkedsemi/hal_driver/inc/reg_i3c.h` 的 `I3C_TypeDef` 结构体
**字段排布本身**（非仅注释）：

| 字段 | offset | 绝对地址 |
|---|---|---|
| `CEVR` | 0x154 | 0x40094154 |
| `EVR` | 0x158 | **0x40094158** |
| `SER` | 0x160 | **0x40094160** |

位定义（`reg_i3c.h` §`I3C_REG_EVR_FIELD` / `I3C_REG_SER_FIELD`）：

- **EVR**：bit0 `CFEF` 0x1 · bit1 `TXFEF` 0x2 · bit2 `CFNFF` 0x4 · bit3 `SFNEF` 0x8 ·
  bit4 `TXFNFF` 0x10 · bit5 `RXFNEF` 0x20 · bit9 `FCF` 0x200 · bit10 `RXTGTENDF` 0x400 ·
  **bit11 `ERRF` 0x800** · bit12 `BUSY` 0x1000 · bit15 `IBIF` 0x8000
- **SER**：bit0-3 `CODERR` 0xf · bit4 `PERR` 0x10 · bit5 `STALL` 0x20 · bit6 `DOVR` 0x40 ·
  bit7 `COVR` 0x80 · **bit8 `ANACK` 0x100** · **bit9 `DNACK` 0x200** · bit10 `DERR` 0x400

**形貌自证（读到的值不是垃圾）**：独立一次运行中，某个 CCC 收尾处读到
`EVR=0x1903`（= ERRF 0x800 + BUSY 0x1000 + FCF 0x200 + …）+ `SER=0x10`（PERR）——
即本 SoC **确实会**在别的场景置 ERRF 与 SER 错误位，说明读数通道有效、EVR/SER 语义正确。

---

## 2. 断点位置与通道

断点打在 `zephyr/drivers/i3c/i3c_linkedsemi.c:1481`：

```c
1478:  exit_condition = ((base->EVR) & (I3C_EVR_FCF | I3C_EVR_ERRF));
1479: } while (exit_condition == 0U && timeout == false);
1481: if(exit_condition == I3C_EVR_ERRF)      // ← 断点 0x1000b4b8
```

**通道**：`ls_i3c_do_ccc` 属**控制器**固件（`cpu0_i3c_controller`，board
`@2os/lsqsh/cpu2`，ELF entry `0x10000000`）⇒ 运行在 **cpu2**，必须走 **GDB 端口 1026**。
（实测确认：1025 = `Thread 1 (CPU#0)` = cpu1/target；1026 = `Thread 2 (CPU#1)` = cpu2/controller。）

**变量可见性**：控制器固件带优化，`base` 在 0x1000b4b8 **已被优化掉**（`info scope` 证实
其活动区间仅 0x1000b25a–0x1000b2f1），故 EVR/SER 改为**绝对地址**读；
`payload` / `exit_condition` / `timeout` / `ret` 在该 PC 处仍可用。

---

## 3. 帧归属（决定性）

`broadcast_nack_recovery`（`i3c_i2c_controller_test.c:548-598`）发三帧广播：
`i3c_ccc_do_rstact_all(RESET_WHOLE_TARGET)` → `i3c_ccc_do_rstact_all(PERIPHERAL_ONLY)`
→ `i3c_ccc_do_rstdaa_all()`。
CCC id：`I3C_CCC_RSTACT(broadcast)=0x2A`、`I3C_CCC_RSTDAA=0x06`。

在**该用例入口**（`i3c_i2c_controller_test.c:554 ctrl_test_begin(TEST_BROADCAST_NACK_RECOVERY,…)`）
另打一个断点作分界标记，实测输出顺序：

```
EXP|HIT#4|payload=0x1006d938|id=0x0  |EVR=0x1303|SER=0x0
EXP|>>>>> TESTSTART broadcast_nack_recovery      ← 分界标记
EXP|HIT#5|payload=0x1006d998|id=0x2a |EVR=0x1303|SER=0x0   ← 帧1 RSTACT whole（NACK 窗内）
EXP|HIT#6|payload=0x1006d998|id=0x2a |EVR=0x1303|SER=0x0   ← 帧2 RSTACT peripheral
EXP|HIT#7|payload=0x1006d998|id=0x6  |EVR=0x1303|SER=0x0   ← 帧3 RSTDAA
```

**三重复核**：`HIT#1-4` 用 payload `0x1006d938`（同一个调用点，位于用例之前——
控制器固件中**只有 `broadcast_nack_recovery` 调用 `_all()`**，
`grep -n "i3c_ccc_do\|_all(" src/` 仅命中 573/581/587，故 `#1-4` 来自子系统层的初始化广播）；
`HIT#5-7` 用 payload `0x1006d998`（另一个调用点 = 本用例），id 序列 `2A,2A,06`
与源码三帧**逐一对位**。

---

## 4. 原始读数

### 4.1 命中点 `i3c_linkedsemi.c:1481`（控制器优化后的出口判定）

| 命中 | payload | `ccc.id` | `exit_condition` | `EVR` | `SER` | `timeout` | `ret` |
|---|---|---|---|---|---|---|---|
| #5 **帧1 RSTACT whole** | 0x1006d998 | 0x2a | **0x200** | **0x1303** | **0x00000000** | 0 | 0 |
| #6 帧2 RSTACT peripheral | 0x1006d998 | 0x2a | 0x200 | 0x1303 | 0x00000000 | 0 | 0 |
| #7 帧3 RSTDAA | 0x1006d998 | 0x06 | 0x200 | 0x1303 | 0x00000000 | 0 | 0 |
| #1–#4（用例前的广播） | 0x1006d938 | 2a/06/01/00 | 0x200 | 0x1303 | 0x00000000 | 0 | 0 |

### 4.2 位解码（帧1，HIT#5）

**`EVR = 0x1303`**
| 位 | 名 | 值 |
|---|---|---|
| bit12 | `BUSY` 0x1000 | **1** |
| bit11 | `ERRF` 0x800 | **0** ← 关键 |
| bit9 | `FCF` 0x200 | **1** ← 关键 |
| bit8 | （0x100，头文件未定义） | 1 |
| bit1 | `TXFEF` 0x2 | 1 |
| bit0 | `CFEF` 0x1 | 1 |

**`SER = 0x00000000`** —— `ANACK`(0x100)=0、`DNACK`(0x200)=0、`COVR`(0x80)=0、
`DOVR`(0x40)=0、`PERR`(0x10)=0、`DERR`(0x400)=0、`CODERR`(0xf)=0。
**无任何错误位。**

**`exit_condition = 0x200 = I3C_EVR_FCF`**（≠ `I3C_EVR_ERRF`）⇒ 走 else，
`ret` 保持 0 ⇒ `i3c_ccc_do_rstact_all()` 返回 0 ⇒ 用例 `r1 == 0` 判 FAIL。

### 4.3 驱动**第二个** ERRF 检查点（`i3c_linkedsemi.c:1759`，`out_ccc_stop`）

广播路径 `goto out_ccc_stop` 后，驱动还有一次兜底检查：

```c
1747: while((READ_REG(base->EVR) & (I3C_EVR_FCF | I3C_EVR_ERRF)) == 0) {...}  // 等帧收尾
1759: if (__LS_I3C_GET_FLAG(base, LL_I3C_EVR_ERRF) == SET) { ls_i3c_log_err_type(dev); ret = -EIO; }
```

同一帧（帧1）在该点的读数：**`EVR=0x1303`、`SER=0x0`、`CEVR=0x0`** —— **仍然无错误位**。
即"ERRF 只是晚同步、稍后可见"这一解释**在本帧上不成立**（驱动的两处 ERRF 检查都读到 0）。

（对照：#12 次 CCC 收尾里有 1 次读到 `EVR=0x1903` + `SER=0x10`，证明该检查点**能**观测到错误。）

---

## 5. 结论（按判读表机械对应）

| 判读表条件 | 实测 | 结论 |
|---|---|---|
| `SER` 的 `ANACK`/`DNACK` 有置位 → **情形 A** | SER = 0x0，未置位 | 不成立 |
| `EVR` 与 `SER` 都无错误位（`exit_condition == FCF`）→ **情形 B** | EVR 无 ERRF、SER 全 0、`exit_condition == 0x200 == FCF` | **成立** |
| 走了 `timeout` 分支 → **情形 C** | `timeout=0`、`ret=0` | 不成立 |

⇒ **情形 B：判据在此硬件上不可达。**

**对原假设的裁定**：原假设（"广播路径漏查 SER，SER 里其实有 ANACK，是驱动缺陷"）
**未被实测支持** —— 广播路径固然只比 ERRF（未查 SER），但**在断点处 SER 本身就是 0**；
即便把广播路径改成和通用路径一样"ERRF + SER 一起查"，也查不到任何东西。
两帧"本应被 NACK"（帧1）与"本应被正常 ACK"（帧2、帧3）的 EVR/SER 签名**完全相同**
（`0x1303` / `0x0`）⇒ 控制器报出的这个签名**不携带 ACK/NACK 信息**。

**推论（须与前提绑定）**：在"帧1 确实无人应答"这一前提（父会话已用
500ms 窗口 / 250ms 深插的极值对照排除时序）下，控制器的硬件在广播无人应答时
**既不置 ERRF、也不置 SER.ANACK**，只报 `FCF` ⇒ 测试的 `r1 != 0` 类判据
**在本硬件上不可达**。**处置权在驱动侧，本次未改动任何判据。**

⚠️ **推论边界更正（2026-09-24，随 B1/B2 判别实验的晚同步观测）**：上面"既不置 ERRF、
也不置 SER.ANACK"**只在"同步检查窗口内"成立**，**不得**读成"硬件从不置位"。同轮独立实验
（`i3c-b1b2-experiment.md` §2.4）实测：一次窗口内直接地址读失败后**约 15 ms** 才出现
`<err> i3c: … Address NACK`，而该笔的 **API 返回值早已是 0** ⇒ **硬件确实锁存了 ANACK，
只是跨时钟域晚同步**；而本实验（以及驱动两处检查点 1481 / 1759）都是**同步**读出，
**结构性错过**该位。⇒
- 本文 §4 的**观测**（三个命中点 EVR=`0x1303`、SER=`0x0`）**仍然成立**；
- 由其得出的**推论收窄为**："**同步检查窗口内**无错误位"，**不**等于"硬件不置位"；
- 因此"情形 B ⇒ 判据不可达"这一结论**保留**（判据经**现有同步驱动**确不可达是事实），
  但**其理由**须改为"**驱动检查窗口过窄/同步**"，而非"控制器硬件没有置位能力"。
  是否改驱动为**异步复查 `SER/ERRF`** 属驱动侧未决；若改，应把 `broadcast_nack_recovery`
  的判据**收紧回 `r1 != 0`**。

⚠️⚠️ **2026-09-24 二次更正（上面这条边界批注的依据已被推翻，"理由改为驱动检查窗口过窄"随之撤回）**：
- **依据不成立**：上文"约 15 ms 后出现 `<err> … Address NACK`"**不构成"错误位晚置位"的证据** ——
  ① `projects/i3c_i2c_switch_test/cpu0_i3c_controller/prj.conf:2` 为 `CONFIG_LOG_MODE_DEFERRED=y`
  ⇒ **打印时刻 ≠ 生成时刻**；② 实证 `reports/2026-09-24-1/i3c-b1b2-probe-run6.log` / `-run7.log`
  第 118–119 行，那条 `<err>` **被劈开、插进后一行中间**（deferred 线程与 `printk` 抢串口形貌）；
  ③ 该笔 **`ret = 0`**，而通用路径 `ls_i3c_request_transfer_flag()`（`i3c_linkedsemi.c:1161` 段）检出
  错误即 `msg_state = ERR` 并 **`return -EIO`** ⇒ **日志不是从通用路径来的**，来源与生成时刻均未定。
- **结论回退**：**"驱动检查窗口过窄"与"控制器硬件无锁存能力"两侧均无据 ⇒ 该问题回到"未定"**，
  **不得据此裁定驱动**。（§4 的**观测**仍成立；"判据经**现有同步驱动**确不可达"仍成立。）
- **真问题（重新界定）**：窗口内那笔直接读 `ret=0` 却有一条 `Address NACK` 日志 —— **它由哪个调用点
  产生、生成于何时？** 候选 `:1161` / `:1304` / `:1761`(`out_ccc_stop`，与广播 `:1481` 同款只查 ERRF) /
  `:2364` / `:2454`。**定性实验**：诊断构建改**非 deferred 日志**（或给错误位加打点时间戳），把
  "生成时刻"与"打印时刻"分开测。见 `OPEN_ITEMS.md` O-39。

---

## 6. 稳定性

| 轮次 | 附加观测 | HIT#5/#6/#7 的 EVR / SER |
|---|---|---|
| 第 1 轮 | 仅 1481 断点 | `0x1303 / 0x0` ×3 |
| 第 2 轮 | + 用例入口分界标记 | `0x1303 / 0x0` ×3 |
| 第 3 轮 | + `out_ccc_stop`(1759) 检查点 | `0x1303 / 0x0` ×3（1759 处亦为 `0x1303 / 0x0`） |

**三轮逐位一致 ⇒ 稳定复现，不是偶发。**

---

## 7. 边界与局限（如实标注）

1. 本实验**只产出观测**：未改判据、未改用例、未改驱动。
2. 测量窗口 = **驱动可观测窗口**（1481 与 1759 两个检查点）。此后
   `ls_i3c_xfer_reset()`（:1337-1353）会清 FCF/RXTGTEND/ERRF 残留标志（其注释明确说
   "跨时钟域同步有滞后"），故**无法**排除"驱动清标志之后**才**出现 ERRF"这一极端情形；
   但就驱动自身能看到的范围（含它自己的兜底检查）而言，错误位**始终为 0**。
3. 本实验**不能**独立证明"帧1 真的没被 ACK"（那由总线侧/父会话的极值对照负责）；
   它证明的是：**无论 ACK 还是 NACK，控制器给出的 EVR/SER 签名相同且无错误位**。
   在"帧1 无人应答"前提下 → 情形 B。
4. **推论边界（2026-09-24 更正，见 §5）**：本文所有"无错误位"读数都是**同步**读出的；
   ~~同轮独立实验实测 ANACK 晚 ~15 ms 才到位~~（`i3c-b1b2-experiment.md` §2.4）⇒
   "无错误位"**只证明同步检查窗口内看不到**，**不**证明"硬件从不置位"。~~故本文结论
   不是"控制器无置位能力"，而是"现有同步驱动的检查窗口过窄"。~~
   ⚠️⚠️ **2026-09-24 二次更正（见 §5 尾）**：上述"晚 ~15 ms"**依据不成立**（deferred 日志
   打印时刻 ≠ 生成时刻；且该笔 `ret=0` 与通用路径 `return -EIO` 矛盾）。⇒ **"驱动检查窗口过窄"
   与"硬件无置位能力"两侧均无据，本文结论回到"未定"** —— **本文只保留观测（同步窗口内 EVR/SER
   无错误位），不再主张任何一侧归因**。用户/后续会话引用本文时以此为准（不主张归因）。

---

## 8. 探针纪律 / 环境

- **零源码改动**：全程仅 GDB 断点 + 寄存器读；实验后
  `git -C zephyr status --short zephyr/drivers/i3c/i3c_linkedsemi.c` → **干净**。
- **环境**：按 `qsh-debug` §2.1 重启 DebugServer；实验期间独占板子。
  因触碰过 **1026**，收尾已**重启 DebugServer**（`凡碰过 1026，下一次批跑前必须重启 DS`），
  重启后 1025 实测可连（`pc=0x100c5d4c`）。
- 复现脚本与原始日志：`tmp/erff_exp/`（`c2_break.gdbcmd` / `c.log` / `t.log` / `ds.log`）
  —— 属一次性调查产物，位于 gitignore 的 `tmp/`。
