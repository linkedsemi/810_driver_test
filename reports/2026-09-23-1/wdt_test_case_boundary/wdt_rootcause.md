# WDT boundary 两项失败 —— 根因定位与修复（2026-09-23）

- 现象来源：`reports/2026-09-23-1/wdt_test_case_boundary/`（FAIL 17/19，`lsqsh_evb@1os/lsqsh/cpu1`）
- 结论：**驱动缺陷（已登记 KNOWN_ISSUES #92，但修复一直未落树）**；**用例期望是对的**，未改用例
- 处置：已改驱动源码（1 处，7 行内）+ 同步 3 处描述该分支的文档串
- 本轮**只做源码级定位与修改**，未构建、未烧录、未碰任何硬件

---

## 1. 现象（回原始报告复核后更正）

| 测试项 | 失败行 | 断言 | 实测 |
|---|---|---|---|
| `test_setup_pause_in_sleep_enotsup` | `wdt_boundary_test.c:265` | `ret == -ENOTSUP` | `ret = -16 (-EBUSY)` |
| `test_setup_twice_ebusy` | `wdt_boundary_test.c:281` | **首次** `setup` `ret == 0` | `ret = -16 (-EBUSY)` |

题面给的行号与断言原文**复核无误**。两处补充更正：

1. `report.md` 的「实测(INFO)」列是空的 —— 但失败原因列已带原始串口文本。
   原始串口（`serial_TGT.log:59-71`）里两项**都**先打了同一条驱动日志：
   ```
   <inf> wdt_iwdg_ls: wdt_ls_setup: interrupt already configured
   ```
   这条日志来自 `wdt_iwdg_ls.c` 的 **`-EBUSY` 分支**，不是选项分支。
   ⇒ 题面推测「驱动静默接受了不支持的配置 / 返回了别的错误码」**不成立**：
   驱动返回 -EBUSY 是**选项检查之前**的状态守卫先命中，`WDT_OPT_PAUSE_IN_SLEEP`
   那一行代码（`wdt_iwdg_ls.c:86-89`）**根本没被执行到**。

2. `test_setup_twice_ebusy` 失败在**第一次** `setup`（`:281`），**不是**第二次（`:283-285`）。
   即该用例连「前置使能」都没建立起来 —— 这是题面已注意到的点，确认属实。

### 执行序（ztest `SORT_BY_NAME`，字母序）

失败两项**紧邻**在 `test_setup_pause_halted_by_dbg_feed_disable` 之后：

```
test_sec_iwdg_rejects_reset_soc_flag
test_setup_pause_halted_by_dbg_feed_disable   ← PASS；本套件内**唯一 feed→disable 的用例**
test_setup_pause_in_sleep_enotsup             ← FAIL (-EBUSY)
test_setup_twice_ebusy                        ← FAIL (-EBUSY)
test_setup_without_timeout_enotsup            ← PASS（被 !IWDT_LOAD 短路，见 §3.2）
```

---

## 2. 先判「正确期望是什么」（决定改驱动还是改用例）

**结论：改驱动。用例期望两条都符合 Zephyr API 契约。**

- `zephyr/include/zephyr/drivers/watchdog.h:158` —
  `@retval -ENOTSUP If any of the set options is not supported.`
  ⇒ 硬件不支持 `WDT_OPT_PAUSE_IN_SLEEP` 时就**应当**返回 `-ENOTSUP`。
  本驱动确实不支持（`wdt_iwdg_ls.c:86-89` 明确返回 -ENOTSUP），
  且 `HAL_IWDG_MSP_DEBUG()` 只支持「调试器 halt 时暂停」，没有 sleep 暂停的硬件通路。
- `watchdog.h:159` — `@retval -EBUSY If watchdog instance has been already setup.`
  ⇒ 「第二次 setup 返回 -EBUSY、第一次返回 0」是 API 明写的语义，用例没错。
- 交叉印证（同型驱动、不同实例）：`app_iwdg_cpu2_test` 的同名两项
  `test_setup_pause_in_sleep_enotsup` / `test_setup_twice_ebusy` **PASS**，
  期望值与判据完全一致。

⇒ **不是用例写错，也不是「二次 setup 该不该允许」的规范问题**（题面第 2 项疑问的回答：
API 要我拒绝，用例判对了）。

---

## 3. 根因

### 3.1 一句话

`wdt_iwdg_ls.c:76` 的「是否已 setup」守卫用**整寄存器非零**判定
（`if (config->iwdg_reg->IWDT_CTRL)`），而 `IWDT_CTRL` 里除 `IWDT_EN`(bit0)/
`IWDT_RST_EN`(bit1) 两个**使能语义位**外，还含 `IWDT_INTR_CLR`(bit2)/
`IWDT_CNT_CLR`(bit3) 两个**脉冲（strobe）位**。
`iwdt_ls_feed()`（`:59-61`）写 `IWDT_CNT_CLR` 后，**硬件把该位留 1、写 0 也清不掉**
⇒ 此后 `IWDT_CTRL` 恒非零 ⇒ 守卫把「已 disable、未使能」的实例误判为「已 setup」→ 误返 `-EBUSY`。

### 3.2 为什么恰好是这两项、别的项都不中招

| 用例 | 为什么不受影响 |
|---|---|
| `test_setup_pause_halted_by_dbg_feed_disable`（`:293`） | **它是污染源**。在它之前，sec_iwdg 从未被 feed 过 ⇒ `IWDT_CTRL` 干净 ⇒ 守卫不触发，setup 返回 0（PASS）。它内部 `wdt_feed()`（`:307`）第一次把 bit3 立起来，`wdt_disable()`（`:310`，写 CTRL=0）清不掉 ⇒ 残留 `0x8` 带到后续用例。 |
| `test_setup_without_timeout_enotsup`（`:240`） | 它在断言前**直接写** `LOAD=0 / CTRL=0`（`:245-246`），而 `setup` 的 `!IWDT_LOAD` 检查（`:71-74`）**排在 CTRL 守卫（`:76`）之前** ⇒ 被 LOAD 短路，CTRL 干不干净无关 ⇒ PASS。**这个 PASS 恰恰掩盖了缺陷**。 |
| 其余 13 项 | 只 install / 只断言 rejected / 不调 setup，或走的是 pmu_iwdg（其 feed 在 `test_pmu_iwdg_lifecycle` 里，且后续无人再 setup 它）。 |

### 3.3 判定性质：**陈年缺陷，不是新回归**

- `git log -S 'if (config->iwdg_reg->IWDT_CTRL)' -- drivers/watchdog/wdt_iwdg_ls.c`
  ⇒ 唯一命中 **`6e520de0854` 2025-07-23「新增看门狗驱动和测试代码」**（驱动诞生那天就有）。
- 该文件最近 6 次提交（2025-07-23 → 2026-09-02 `66878b809f8 Wdt disable at boot`）**均未触碰这一行**。
- 历史报告：`reports/2026-09-22-1/wdt_test_case_boundary/` 同样 FAIL 17/19、同样这 2 项、同样行号；
  更早的 `KNOWN_ISSUES #92`（2026-09-18）与 `TRIAGE_MISC_2026-09-18.md §1` 记明
  2026-09-17-4 / -5 / 2026-09-18-1 三轮复现一致，**非 flaky，确定性**。

⇒ **已登记问题（KNOWN_ISSUES #92）**，已定位、判定为驱动缺陷、给出了建议改法，
但**修复从未落进代码树**（本轮复核：工作区 `git status` 该文件只有 O-15 的 64 位乘法改动，
`git diff` 里没有 #92 的守卫修复）。

---

## 4. 证据

**硬件实测（决定性，来自 `TRIAGE_MISC_2026-09-18.md §1.3`，GDB 在 `iwdt_ls_disable` 下断点）**

```
进入 disable 时      CTRL=0x0b  LOAD=0x1e0000  CNT=0x1e0000
   （0x0b = IWDT_EN | IWDT_RST_EN | IWDT_CNT_CLR；LOAD==CNT==1966080 = 60000 ms）
执行 `IWDT_CTRL = 0x0` 之后
CTRL_after_store=0x8          ← 只清掉了 EN/RST_EN，bit3 写 0 清不掉
```

**独立复现（`OPEN_ITEMS.md` O-32，2026-09-22，另一实例 app_iwdg / cpu2）**

```
ctrl_after_disable=0x8   ← 仅 bit3 IWDT_CNT_CLR；EN/RST_EN 均为 0
```
O-32 已据此把 **测试侧**判据改成 `ctrl_after & IWDG_CTRL_ENABLED == 0`（`EN|RST_EN`），
并在全量回归实测 `FAIL 2/4 → PASS 3/4`。**同一硬件事实的驱动侧修复（#92）当时没做。**

**本轮源码复核（非硬件）**

- `reg_iwdgv2_type.h:28-38`：`IWDT_CTRL` 位定义 = `IWDT_EN`(0) / `IWDT_RST_EN`(1) /
  `IWDT_INTR_CLR`(2) / `IWDT_CNT_CLR`(3) ⇒ 后两位确为 strobe，非使能语义。
- `wdt_iwdg_ls.c:59-61` `iwdt_ls_feed()` 写 `IWDT_CNT_CLR|IWDT_INTR_CLR` 后写 0（回读留 bit3）。
- 串口日志：两项均打印 `wdt_ls_setup: interrupt already configured`（= -EBUSY 分支）。
- 反向交叉印证（`TRIAGE_MISC §1.6`）：`case_non_destructive` 的 `test_wdt_disable`
  （**无 feed**）PASS ⇒ **「feed」是引入残留 bit3 的必要条件**，与本根因吻合。

---

## 5. 改动清单

### 5.1 驱动修复（1 处，写域内）

**`zephyr/drivers/watchdog/wdt_iwdg_ls.c:76-84`**

```c
-	if (config->iwdg_reg->IWDT_CTRL) {
+	/* IWDT_CTRL also holds two strobe bits, IWDT_INTR_CLR(bit2) and
+	 * IWDT_CNT_CLR(bit3). IWDT_CNT_CLR is left set by the hardware after a
+	 * feed and can not be cleared by writing 0, so testing the whole
+	 * register for non-zero reports a running watchdog long after the
+	 * counter was cleared. Only IWDT_EN/IWDT_RST_EN say "already set up". */
+	if (config->iwdg_reg->IWDT_CTRL & (IWDT_EN_MASK | IWDT_RST_EN_MASK)) {
 		LOG_INF("wdt_ls_setup: interrupt already configured");
 		return -EBUSY;
 	}
```

- `IWDT_EN_MASK` / `IWDT_RST_EN_MASK` 来自已 include 的 `reg_iwdgv2_type.h`
  （经 `ls_msp_iwdg.h`），与同文件 `iwdt_ls_feed` 用 `IWDT_CNT_CLR_MASK` 的风格一致；
  全仓无同名 `#define` 遮蔽（已 grep 确认，是 enum 常量）。
- **为什么能修**：守卫语义应为「EN/RST_EN 已被置起 = 看门狗正在跑」。屏蔽掉两个 strobe 位后，
  残留 `bit3` 不再误判。**注意这不是放宽**：`IWDT_EN` 或 `IWDT_RST_EN` 任一仍为 1
  （即真的已 setup）**依旧会**命中 `-EBUSY` —— 与 O-32 在测试侧采用的
  `EN|RST_EN` 语义位判据完全同源。
- **逐项推演修复后 19 项**（按字母序，假设残留恒为 bit3=0x8）：
  - `test_setup_pause_in_sleep_enotsup`：install 写 CTRL=0（回读仍 0x8）→ 守卫
    `0x8 & 0x3 == 0` 通过 → 走到 `WDT_OPT_PAUSE_IN_SLEEP` 分支 → **-ENOTSUP** ✅
  - `test_setup_twice_ebusy`：首次 setup 通过并写 CTRL=0x3（回读 0xB）→ **ret 0** ✅；
    二次 setup 读到 `0xB & 0x3 = 0x3 ≠ 0` → **-EBUSY** ✅
  - 其余 17 项路径未被本改动触及（判据只影响 `setup` 的第一个守卫）⇒ 预期仍全 PASS。
- **副作用评估**：
  - 不再有「该 -EBUSY 的返回 0」——已逐条核对：全仓仅 `wdt_boundary_test.c:284` 与
    `app_iwdg_test.c` 期望 `-EBUSY`，两处都是**二次** setup（首次已置 EN/RST_EN），
    新判据照样命中。**无测试由 PASS 翻 FAIL。**
  - `iwdt_ls_disable()` **不改**：实测它写入 0 后 `EN/RST_EN` 确已清 0（`0x0B → 0x08`），
    残留的只有硬件 strobe 位，非缺陷。
  - 上电时 bootloader 若已使能看门狗，`EN=1` ⇒ 仍正确返回 `-EBUSY`，语义未弱化。
  - 未修正的问题（本轮不动，见 §7）：`iwdt_ls_install_timeout()` 仍会写 `CTRL=0x0`，
    从而**绕过** EBUSY 守卫（既记 P1-3）—— 与本轮两项失败**无关**，
    但**不得**把它当成这两项的解释（`TRIAGE_MISC §1.5` 已明确排除）。

### 5.2 文档同步（3 处，均为「描述该守卫分支」的字符串，写域内）

| 文件 | 改动 |
|---|---|
| `810_driver_test/projects/wdt_test/cpu1_wdt_test/src/wdt_boundary_test.c:9-10` | 头注释 `IWDT_CTRL != 0 -> -EBUSY` → `IWDT_CTRL & (IWDT_EN\|IWDT_RST_EN) != 0 -> -EBUSY` |
| `810_driver_test/projects/wdt_test/tests/case_boundary/test.sh:10-11` | 同上口径 |
| `810_driver_test/projects/wdt_test/tests/COVERAGE.md` §2.2 `test_setup_twice_ebusy` 行 | 驱动路径列同上口径 |

- **用例代码本身（`wdt_boundary_test.c` 的 zassert / ZTEST 体）一行未改** —— 期望正确。

---

## 6. 上板怎么验

**必须上板的原因**：根因是**硬件寄存器行为**（`IWDT_CNT_CLR` 写 0 清不掉），
且在 PC 上无法复现 —— 触发条件是「ztest 按字母序跑到 `..feed_disable` 之后」的
**运行期状态残留**。只编译不烧录，这两项与驱动分支的执行路径无法被证伪；
且本修复不改变任何编译期可见内容，静态检查给不出证据。

### 6.1 目标用例（最小验证，约 2 分钟 + 烧录）

```bash
cd D:/zephyr4_0/810_driver_test/projects/wdt_test/tests
python run_test.py --case case_boundary
```

（如需手动分步：`cd tests/case_boundary && ./test.sh -r`；
首次或改了源码须先 build：`./test.sh` 或 `./test.sh -g`。）

**预期结果**

- `wdt_test_report.md`：`结果: PASS (19/19)`、`执行标记: SUCCESSFUL`、`OVERALL: PASS`
- 串口原始日志（`serial_TGT.log`）中：
  - `test_setup_pause_in_sleep_enotsup` → `PASS`；
    **且不再出现** `wdt_ls_setup: interrupt already configured`（该行出现了就说明守卫又误命中）
  - `test_setup_twice_ebusy` → `PASS`（首次 setup 返回 0、二次返回 -16）
  - 末尾 `TESTSUITE wdt_boundary passed.` / `PROJECT EXECUTION SUCCESSFUL`
- **判别力**：若把修复回退（守卫改回整寄存器），这两项必重新 FAIL 且日志重新出现
  `interrupt already configured` ⇒ 该证据对本修复是**双向敏感**的，不是「跑绿了就算过」。

### 6.2 回归面（防「改一处塌一片」，建议同一轮跑）

```bash
# 同驱动、同工程的非破坏性 case（含白盒回读 IWDT_CTRL 的 test_wdt_disable）
cd D:/zephyr4_0/810_driver_test/projects/wdt_test/tests
python run_test.py --case case_non_destructive     # 期望仍 5/5 PASS

# 同驱动、另一实例（app_iwdg / cpu2），含期望二次 setup 返回 -EBUSY 的用例
cd D:/zephyr4_0/810_driver_test/projects/app_iwdg_cpu2_test/tests
python run_test.py --case case_default             # 期望与 2026-09-22-1 基线一致
python run_test.py --case case_quick_enable        # 期望仍 PASS 3/4（O-32 基线）
```

- `case_non_destructive` 期望**不变**（5/5）。它第 4 项 `wdt_test.c:129`
  断言 `IWDT_CTRL == 0` —— 该用例无 feed，不受本修复影响（详见 §7 未定项 ①）。
- **不必重跑破坏性 case**（`case_destructive_reset` / `case_pmu_iwdg_full_reset`）：
  本修复只在 `setup` 的第一个守卫上收窄判据，不触碰 `LOAD` 换算、复位域与中断配置。

---

## 7. 仍未定项 / 需写域外改动

① **`wdt_test.c:129` 的同类过强判据（潜伏，当前碰巧 PASS）** — 写域内但我**未改**
（避免无谓放宽已在跑的用例）：
```c
zassert_equal(SEC_IWDG_REGS[IWDG_CTRL_OFFSET], 0u,
              "IWDT_CTRL must read 0 right after wdt_disable()");
```
`case_non_destructive` 的 `test_wdt_disable` 只 setup+disable、**不 feed**，
所以现在过了；但该 suite 的 `test_wdt_setup_then_feed`（**会 feed 5 次**）也读同一个残留位 ——
一旦用例顺序变化或它被复制到别的 suite，这条 `== 0` 就会像本轮两项一样假 FAIL。
**建议**：按 O-32 已获用户裁定的同款改法改为 `& (IWDT_EN|IWDT_RST_EN) == 0u`
（这一改**不是放宽**：EN/RST_EN 仍为 1 时照样抓住 —— O-32 已在 cpu2 实测双向验证）。
需用户裁定后我再落。

② **`iwdt_ls_install_timeout()` 写 `CTRL=0x0` 绕过 EBUSY 守卫（P1-3）** — 写域内，
本轮**有意不动**：它与本两项失败无关（`TRIAGE_MISC §1.5` 已排除），
且单独修它需要先定「install 时是否允许重配」的语义，属另一裁定。

③ **[写域外] 文档状态未同步** —— 需要改动的地方（我无权限，**未改**）：
| 文件 | 需要什么 |
|---|---|
| `810_driver_test/KNOWN_ISSUES.md` #92 | 状态从「已定位/建议动作」改为「已修 + 本轮上板验证结果」（跑完 §6 后补实测） |
| `810_driver_test/OPEN_ITEMS.md` | 新增一条跟踪 #92 驱动侧修复的落地与验证（O-32 只覆盖了测试侧） |

④ **未做的验证（本轮铁律所限）**：未构建、未烧录、未串口、未 DebugServer。
因此「修复后 19/19」目前是**源码级推演结论，不是实测结论**；
§6 的判别力说明（回退即复现）是为上板时能一次判死而写的。

---

## 附：方法论留痕（供沉淀）

- 分层定位：先判「用例预期 or 驱动缺陷」（查 `watchdog.h` 契约 + 同型实例交叉印证），
  再回源码复核题面给的行号/断言 —— 本轮题面的行号全对，但**失败归因**（「驱动静默接受选项」）
  与实测相反，靠串口原始日志里的**驱动自身日志串**纠正。
- **「PASS 的等价类用例」会掩盖缺陷**：`test_setup_without_timeout_enotsup` 因检查顺序
  （LOAD 先于 CTRL）恒 PASS，掩盖了同一个守卫的缺陷 —— 与 `relaxing-a-check-silently-passes-bad-cases`
  同一族的反面：**恒真/恒假的邻居用例会让缺陷长期不可见**。
- **测试间的状态残留是「顺序依赖假 FAIL」的常见来源**：本例污染源是被测驱动+硬件共同造成的
  （feed 留下写不清的 strobe 位），判据该修的是驱动守卫，而非给用例加隔离。
