# i3c `broadcast_nack_recovery` — B1 / B2 判别实验与处置

- **日期**：2026-09-24
- **板卡**：`lsqsh_evb@2os`（cpu1 = target / cpu2 = controller），本会话独占板子
- **用例**：`i3c_i2c_switch_test` → `case_switch_core` → `broadcast_nack_recovery`
- **串口**：CTRL = COM18（MCP2200 uart5）、TGT = COM16（CH342 uart1）
- **结论**：**B2 —— target 在窗口内确实不应答，是"判据不可达"侧的既有问题**；
  已按 B2 分支处置并上板验证 **case_switch_core 双核 11/11 PASS**。

---

## 0. 一句话结论

| | |
|---|---|
| **判别实验结果** | 窗口内（t≈13ms，实测窗口 `[5,15]ms`）的 16B 直接地址回读 **无任何数据被时钟出来**（缓冲区保持 `0xDE` 哨兵）；窗口前后同一读稳定回 target 的 `0xAA..0xB9` |
| **B1 还是 B2** | **B2** |
| **ISSUE/W 原始值** | 窗口内那次传输：`ret=0`、`EVR=0x00001103`、`SER=0x00000000`（**返回值与 EVR/SER 全部不携带 ACK/NACK 信息**） |
| **处置** | 改用例（不可达判据→可达口径）+ 修 `'E'` 可达性 | 
| **上板结果** | `case_switch_core` **11/11 PASS**（CTRL PASS / TGT PASS，双核 `PROJECT EXECUTION SUCCESSFUL`） |
| **登记位置** | `projects/i3c_i2c_switch_test/tests/COVERAGE.md` §「已知基线」（B-1 / B-2） |
| **诊断代码** | **已完整回退**（与实验前逐字节一致 + 残留 grep 为空，见 §7） |

---

## 1. 实验设计（为什么不是"发一笔就完事"）

原设计：窗口内发一笔**直接地址**（0x32）的 I3C 传输 —— 它走**通用传输路径**
（`i3c_linkedsemi.c:1157` 查 `SER.ANACK/DNACK`），这条路"报不报"有信息量。

**执行中发现两个必须补的洞，否则结论会被带偏：**

1. **`ret` 这个指标是瞎的。** 补了负对照"读/写无器件地址 `0x70`"：`ret` 同样是 **0**。
   ⇒ 单看 `ret` 无法区分"有人应答"与"没人应答"。
   **判别力只能来自回读的数据**：缓冲预置 `0xDE` 哨兵，有人应答才会被真实数据覆盖。
2. **静态落点会错过窄窗口。** 第一版把探测放在 `k_sleep(10ms)` 之后，得到
   `0xAA AB AC AD`，看起来像 B1。但给 target 加"窗口 enter/exit"标记后发现：
   **enter 与 exit 两条标记都已在探测之前到达** ⇒ 那次探测其实落在窗口**之后**，
   证明不了任何事。⇒ 改为**时间扫描**（每 ~3ms 一次，跨 [0,~185]ms）+ target 自报窗口标记。

> 教训：**把探测点钉在"窗口内"是本次实验最难的一步**；不做时间扫描 + 对端标记，
> 会得到一个方向完全相反、且看起来很有说服力的错觉结论。

---

## 2. 实测原始数据（判别轮，两次独立运行一致）

### 2.1 时间扫描（controller，原点 = 发完 `'N'`）

| t (ms) | `ret` | d0 d1 … d15 | 说明 |
|---|---|---|---|
| 4  | 0 | `AA AB .. B9` | 窗口尚未开始 |
| **13** | **0** | **`DE DE DE` （哨兵原样）** | **窗口内 —— 无数据被时钟出来** |
| 22 | 0 | `AA AB .. B9` | 窗口已结束 |
| 29…182 | 0 | `AA AB .. B9` | 窗口后，逐次全部正常 |
| NEG（读 `0x70`，无器件） | 0 | `DE DE DE`（哨兵原样） | **负对照：与窗口内形貌完全一致** |

### 2.2 target 自报的窗口（经 RLOG 单出口汇入同一串口流，同一时间轴）

```
B1B2PROBE:S t=4ms  ret=0 d0=AA d1=AB d15=B9
B1B2TGT:enter,t=5ms                  ← target 切到 I2C pinmux（窗口开）
B1B2PROBE:S t=13ms ret=0 d0=DE d1=DE d15=DE   ← ★ 窗口内：target 真的不应答
B1B2TGT:exit,t=15ms                  ← target 自动切回 I3C（窗口关）
B1B2PROBE:S t=22ms ret=0 d0=AA d1=AB d15=B9
```

- 实测窗口 = **`[t≈5ms, t≈15ms]`**，与协议头注释里的模型 `[6,16]ms` 基本吻合。
- 落在窗口内的**唯一**一次采样（t=13ms）**没有拿到任何数据**；窗口一结束立刻恢复。

### 2.3 寄存器读数（窗口内那次，`i3c5 = 0x40094000`，EVR offs `0x158` / SER offs `0x160`）

```
EVR = 0x00001103   (bit12 BUSY=1, bit8=1, bit1 TXFEF=1, bit0 CFEF=1；ERRF(0x800)=0、FCF(0x200)=0)
SER = 0x00000000   (ANACK(0x100)=0、DNACK(0x200)=0、PERR/DERR/COVR/DOVR/CODERR 全 0)
```

**窗口内 / 窗口外 / 无器件地址 三种情形的 `EVR`、`SER`、`ret` 逐位完全相同。**

### 2.4 旁证：~~控制器**确实会**锁存 ANACK，但晚到~~ **（本小节结论已撤回，见节末更正）**

同轮日志里，在**失败的窗口内读之后约 15ms** 才出现驱动错误行：

```
[00:00:00.435,000] <err> i3c: i3c@40094000: Address NACK
```

即硬件把 ANACK 置上了（`ls_i3c_log_err_type` 由 ISR/线程在标志同步到位后调用），
但 `ls_i3c_request_transfer_flag` 的**同步**复查已经错过 ⇒ API 早已返回 0。
这条与 KNOWN_ISSUES #98 增补 3 的"跨时钟域晚同步"是同一族现象，但对**直接地址**
同样成立（本次新增的观测）。

⚠️⚠️ **2026-09-24 更正 —— 上面这段"旁证"不成立，勿作依据**：本条把"日志**打印**晚了"读成了
"错误位**置位**晚了"。三条反证：
1. **日志模式**：本工程 `cpu0_i3c_controller/prj.conf:2` = `CONFIG_LOG_MODE_DEFERRED=y`
   ⇒ **打印时刻 ≠ 生成时刻**；
2. **实证**：`i3c-b1b2-probe-run6.log`（同 `-run7.log`）第 118–119 行，那条 `<err>`
   **被劈开、插进了后一行 `B1B2PROBE:S t=28ms …` 的中间** —— deferred 日志线程与 `printk`
   抢串口的典型形貌（**是打印晚，不是置位晚**）；
3. **矛盾**：该笔 **`ret = 0`**，而通用路径 `ls_i3c_request_transfer_flag()`
   (`i3c_linkedsemi.c:1161` 段) 检出错误即 **`return -EIO`** ⇒ **那条日志不是从通用路径来的**，
   其**来源与生成时刻均未确定**。
⇒ **ANACK 不是"晚到"**；**"硬件锁存了 ANACK"与"驱动窗口过窄"两侧均无据**。
**真问题**：那条 `Address NACK` 日志由**哪个调用点**产生、**生成于何时**（定性需**非 deferred
日志**或给错误位加打点时间戳）。见 `OPEN_ITEMS.md` O-39。

---

## 3. 判读（机械对应）

| 判读条件 | 实测 | 结论 |
|---|---|---|
| 窗口内 target **仍应答 I3C** ⇒ **B1** | 窗口内回读**无数据**，且与"无器件地址"形貌一致 | **不成立** |
| 窗口内 target **真不应答**，但控制器不报该帧 ACK/NACK ⇒ **B2** | 窗口内确实无应答；控制器对**广播帧**的同步检查（`ls_i3c_do_ccc` 只看 `EVR.FCF|ERRF`）不报错、`SER` 恒 0、返回值恒 0 | **成立** |

### 3.1 对第一版"看起来像 B1"的结论的撤回（如实记录）

第一版静态探测（`k_sleep(10ms)` 之后）得到 `read0x32 → ret=0, data=AA AB AC AD`，
我一度据此倾向 **B1**。补上 target 端窗口标记后证明：**那次探测落在窗口之外**
（enter/exit 两条标记都先于它到达），观测无判别力。**该结论已撤回**，
本文件所有结论以 §2 的时间扫描为准。

### 3.2 与既有 `i3c-erff-experiment.md` 的关系

那份实验用**同步断点**读 `EVR`/`SER`，结论是"无任何错误位"。本次补充的是：
~~**"无错误位"是同步检查窗口的产物**，不是"硬件从不置位"（§2.4 实测 ANACK 会晚到）。~~
⇒ `i3c-erff-experiment.md` 的观测本身成立，但它的**推论边界**比原文写的更窄。
该文件的结论方向（B2）被本次独立实验**证实**。

⚠️⚠️ **2026-09-24 更正**：上面这句"补充"所依据的 §2.4"ANACK 晚到"**已被推翻**（见 §2.4 更正：
deferred 日志 ⇒ 打印晚，非置位晚）⇒ **"推论边界更窄"这一修正随之失效**：
`i3c-erff-experiment.md` 的观测成立，但其**归因回到"未定"** —— 既不能读成"硬件不置位"，
也**不能**读成"驱动窗口过窄"。**B2（窗口内 target 确实不应答）不依赖此被推翻的旁证，仍然成立。**

---

## 4. 处置（按任务书 B2 分支）

### 4.1 判据改写（`i3c_i2c_controller_test.c`）

- **改掉**：`if (r1 == 0) ctrl_set_fail("rstact_whole_should_nack")` —— 原"窗口内广播**应被 NACK**"。
- **改为**：`if (r1 != 0) ctrl_set_fail("rstact_whole:%d", r1)` —— "窗口内发广播**不应报错、应能正常收尾**"。
- **新增（只上报、不进判据）**：`I3C_I2C_TEST:CTRL:INFO:broadcast_nack_recovery:win_broadcast_ret=<r1>`
  —— 把原始返回值留在串口与报告里，将来若驱动侧改成"异步复查 `SER/ERRF`"，可**直接收紧回 `r1 != 0`**。
- **一字未动**：RSTACT peripheral / RSTDAA 必须成功、帧末 `EVR.BUSY(bit12)` 必须清零。
- 注释里写清"这是上板实测裁定"（参照 #116 / O-31 的写法），并指向本文件。

### 4.2 额外修复：`'E'` 的可达性（**这一条不在原任务书里，是我在本轮定位出来的**）

本次上板发现：即使 CTRL 侧判 PASS，**双核判定仍为 FAIL** —— TGT 侧该行恒
`MISSING`。根因（新定位）：

> 第三帧 `RSTDAA` 清掉 target 的**硬件动态地址 `0x32`**，而 controller 侧
> dev list 的 `desc->dynamic_addr` 不同步清 ⇒ 紧随其后的结账命令 `'E'` 被打到
> **已不存在的地址**上、target 收不到 ⇒ TGT 侧该项**恒无记账**
> （`SUMMARY 10/11`）⇒ 该 case **结构上不可能双核 11/11**。
> 历史归档里该行 `来源=missing` 就是这个形貌（**既存缺陷，与本轮判据改动无关**）。

处置：结账前先 `i3c_do_daa()` 恢复 `0x32`（与 `test_main` 收尾处**本来就要做的同一套
动作**，仅提前到 `'E'` 之前；幂等，不新增任何被测行为，判据一字未改）。

⚠️ **如需回退这一条**：只影响 `broadcast_nack_recovery` 的 TGT 记账可达性，
回退后该 case 会退回"CTRL 11/11 但整体 FAIL(TGT MISSING)"的旧形貌。

### 4.3 上板验证

```
| broadcast_nack_recovery | PASS | PASS | PASS | token | CTRL:win_broadcast_ret=0 | |
EXECUTION: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
OVERALL: PASS
```
归档：`reports/2026-09-24-1/i3c_i2c_switch_test_case_switch_core/`
（TGT 侧 `SUMMARY:END:11/11 PASS` + `TGT:DONE:PASS`）
**连跑两次结果一致。**

### 4.4 登记

`projects/i3c_i2c_switch_test/tests/COVERAGE.md` 新增 **「已知基线（裁定项，不是待修缺陷）」**
一节，含 B-1（判据不可达，写明"**保持现状是裁定**、别当驱动 bug 重开调查"）与
B-2（`'E'` 可达性修复），并同步更新覆盖表该行的「预期数据 / 预期来源」两列。

---

## 5. 证据文件

| 文件 | 内容 |
|---|---|
| `reports/2026-09-24-1/i3c-b1b2-probe-run6.log` / `-run7.log` | 两次判别轮（时间扫描）的完整 `dbg_run --raw` 输出 |
| `reports/2026-09-24-1/i3c_i2c_switch_test_case_switch_core/` | 最终 11/11 PASS 的归档（`serial_CTRL.log` / `serial_TGT.log` / report） |
| `tmp/b1b2probe_backup/before_run/…` | **改动前**的 FAIL(10/11) 归档副本（证据不被复测覆盖） |
| `tmp/b1b2probe_backup/*.c` | 实验前的三份源文件副本（用于回退自证） |

> `tmp/` 在 `.gitignore` 内，属一次性调查产物。

---

## 6. 归档完整性自查（H1/H2/H3）

本工程是 **D2 token 型 + 单出口**，`scripts/check_archive.py`（现址，2026-09-24 迁出 `tmp/`；为 ztest 原生 D1 型写的）
对它一律报 BAD，属**误报**（qsh-test-compare §2 已登记"拿 ztest 正则数 D2 工程"这一坑）。
按本工程自己的文法核：

- **H1 头部完整**：CTRL 首条信号行 = `ZT_BOOT CTRL`；TGT 首条 = `I3C_I2C_TEST:TGT:BOOT` ✅
- **H2 唯一性**：两核 `BOOT` / `PROJECT EXECUTION` 各恰 1 次；无重复套件段 ✅
- **H3 尾部完整**：CTRL 末行 = `PROJECT EXECUTION SUCCESSFUL`；TGT 末行 = `I3C_I2C_TEST:TGT:PROJECT EXECUTION SUCCESSFUL` ✅

---

## 7. 探针纪律自证

- 诊断代码形态：controller 侧 `#define I3C_B1B2_PROBE` + `#if` 块（时间扫描），
  target 侧 `B1B2TGT` 窗口标记宏；**全部为一次性诊断**，实验后整体删除。
- **回退自证**：
  - `diff -u tmp/b1b2probe_backup/<file> <现行文件>` → **逐字节一致**（target 文件）/ 仅剩本轮**有意保留**的判据改动（controller 文件）。
  - 残留 grep（源码全树）：`grep -rn "I3C_B1B2_PROBE\|B1B2PROBE\|I3C_B1B2_WIN_MARK\|B1B2TGT\|tgt_nack_recv_ms"` → **无匹配**。
  - 构建产物：`strings build/controller/zephyr.bin | grep -c B1B2PROBE` → **0**；
    `strings build/target/zephyr.elf | grep -c B1B2TGT` → **0**。
- **未动**（任务书要求）：`NACK_WINDOW_MS` / `tgt_enter_nack_window` / controller 两处等待 —— 本实验全程依赖它们，一字未改。
- **git 只读**：全程未执行 `git checkout/restore/stash/reset`。

---

## 8. 遗留 / 交父会话的开放项

1. **驱动侧**：`ls_i3c_do_ccc` 的广播错误检查只看 `EVR.FCF|ERRF`，且该检查与
   `ls_i3c_request_transfer_flag` 的 `SER.ANACK` 复查都是**同步**的 —— ~~而实测 ANACK
   会**晚 ~15ms** 到位（§2.4）。⇒ "广播 NACK 不可达"更像是**驱动检查窗口过窄**，
   而不是控制器硬件没有能力。~~ **是否要改驱动，属驱动侧裁定**；本轮未动驱动。
   ⚠️⚠️ **2026-09-24 更正**：划线部分（§2.4"晚 ~15ms"）**依据不成立**（deferred 日志 ⇒ 打印晚，
   非置位晚；且该笔 `ret=0` 与通用路径 `return -EIO` 矛盾）⇒ **"驱动检查窗口过窄"这一倾向撤回，
   与"硬件无能力"一样无据 —— 归因回到"未定"**。**真问题**：那条 `Address NACK` 日志由哪个调用点
   产生、生成于何时（定性需非 deferred 日志 / 给错误位加打点时间戳）。见 `OPEN_ITEMS.md` O-39。
   若日后改了，**应把 `broadcast_nack_recovery` 的判据收紧回 `r1 != 0`**（INFO 行保留了现场）。
2. **工具**：`scripts/check_archive.py`（2026-09-24 迁出 `tmp/`）不认 D2 token 型（见 §6），建议扩一版或加族判定。
3. **遗留目录**：本轮误用相对路径，一度把归档写到 `810_driver_test/projects/reports/`；
   已整体移到 `tmp/b1b2probe_backup/stray_reports/`，`projects/` 下不再有 `reports/`。
