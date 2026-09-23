# 未提交项核销审计 — OPEN_ITEMS「已修/已结」条目逐条验树

> **触发**：2026-09-23 排查回归失败项时发现 OPEN_ITEMS 多条标「已修（代码未提交）」的修复疑似不在仓库里。
> **本轮目标**：把「声称的修复」逐条落到树里，判定 **(a) 在树里(未提交) / (b) 已提交 / (c) 丢了 / 无法判定**。
> **纪律**：纯 git 考古 + 读源码。**未构建、未烧录、未碰硬件、未改动任何源码，未改 `OPEN_ITEMS.md`**。
> 只用了 `git log/show/diff/status/stash list/reflog/worktree list/for-each-ref/show <ref>:<path>` 等只读子命令。
>
> **审计日期**：2026-09-23 ｜ 仓库：`D:/zephyr4_0/zephyr`(driver_test)、`D:/zephyr4_0/810_driver_test`(linkedsemi)、
> `D:/zephyr4_0/modules/hal/linkedsemi`(qsh)、`D:/zephyr4_0/modules/crypto/mbedtls`(linkedsemi)

---

## 0. 结论速览（三态计数）

**第一轮（14 条明写「未提交」的）+ 第二轮（8 条「已修」无标记的）= 22 条**

| 判定 | 条数 | ID |
|---|---|---|
| **(a) 在树里（未提交）** | **0** | — |
| **(b) 已提交** | **17** | O-32, O-33, O-25, O-24, O-11, O-15, O-3, O-5, O-7, O-c, O-20, O-21, O-22, O-12, O-13, O-a, O-d |
| **(c) 丢了** | **4** | **O-18, O-9, O-6, O-16** |
| **无法判定** | **1** | O-b |

**一句话**：台账里「代码未提交」这个标记**大面积失真** —— 14 条明标「未提交」的里 **10 条其实早已提交**，
只有 4 条真丢。真正丢的 4 条中，**2 条（O-9/O-6）的缺失是「有意未做」被误记成「已修」**，另 2 条（O-18/O-16）是真丢。

---

## 1. 核销表 — 第一轮（14 条明写「未提交」）

| ID | 声称的修复 | 特征串（唯一可搜） | 判定 | 证据 | 建议动作 |
|---|---|---|---|---|---|
| **O-32** | `app_iwdg_cpu2_test` 断言改 `ctrl_after & IWDG_CTRL_ENABLED == 0` | `ctrl_after & IWDG_CTRL_ENABLED` | **(b) 已提交** | `810:a405816`(2026-09-22 17:22)；`git grep HEAD` → `projects/app_iwdg_cpu2_test/cpu2_controller_app_iwdg_test/src/app_iwdg_quick_test.c:103` | 备注「未提交」→ 改「已提交 a405816」 |
| **O-33** | 预检 `check_serial()` 放宽为「可开的口够 2 个」 | `need >= 2 openable` | **(b) 已提交** | `810:4902483`(2026-09-22 19:14)；HEAD `regression/precheck/run_precheck.py:488-489`(`ok = len(openable) >= 2`) | 同上 |
| **O-25** | `test.sh` `set -e` 下 timeout 死代码 → `raw="$(…)" \|\| rc=$?` | `\|\| rc=$?` | **(b) 已提交** | `810:a405816`；HEAD `scripts/test_common.sh:350`（另 :445/:473/:633） | 同上 |
| **O-24** | `find` 改 `-prune` + 外层 timeout 上限 + 调用点后移 | `-prune` / `STALE_SCAN_TIMEOUT_S` | **(b) 已提交** | `810:a405816`；HEAD `scripts/test_common.sh:343`(2 处 `-prune`)、`:326` timeout 默认值、调用点在加载横幅后 | 同上 |
| **O-18** | HAL `io_cfg_input()` 改清 **OE 高半区**（`OE_DIN &= ~(1<<pin<<16)`） | 函数体内 `OE_DIN.*<<16` | **(c) 丢了** | HAL **全部 88 个 ref** 的 `io_cfg_input` 体内**零命中**；工作区 `modules/hal/linkedsemi/soc/rv32/qsh/ls_soc_gpio.c:333` 仍是原空动作 `OE_DIN &= ~(1<<x->num);`；HAL `git status` 干净；10 个 stash 只改 `__ramfunc` 前缀、未动该行 | **重做**：改 `:333` 为清 OE 高半区 |
| **O-11** | `iopmp_test` 三处（TC-5 保存/恢复、TC-2 断言 no-op、TC-4 补断言） | `addr2napot is a pure bit-op` | **(b) 已提交** | `810:b879216`(2026-09-17 16:23)；`projects/iopmp_test/cpu1_iopmp_test/src/iopmp_test.c` 在 HEAD、含 TC-4 napot_invalid_params + TC-5 restore | 同上 |
| **O-15** | `wdt_iwdg_ls.c:48` 64 位乘法 + `wdt_boundary_test.c` 3 项改判 | `(uint64_t)timeout_ms` / `4294967263u` | **(b) 已提交** | zephyr `e862da23a40`(2026-09-23 10:49)；810 侧 `4294967263` 在 HEAD `wdt_boundary_test.c:53,372-373` | 同上 |
| **O-3** | `mbedtls` SM4 加本层入参校验 | `ilen % 16 != 0` → `-EINVAL` | **(b) 已提交** | mbedtls `16b20e851`(2026-09-17 13:27)；`library/sm4_alt.c:56-63`(ecb_encrypt)、`:107-114`(ecb_decrypt) | 同上 |
| **O-5** | `dw_dma_config()` 拒 `src == dst` → `-EINVAL` | `source and destination address are identical` | **(b) 已提交** ⚠️ | zephyr `84e1a82c26f`(2026-09-23 10:47:57)；`drivers/dma/dma_dw_common.c:231-247`（独立预扫 → 逐 block 比原始地址 → `LOG_ERR` + `ret=-EINVAL` + `goto out`） | **勿报为丢失**；台账备注改为「已提交 84e1a82c26f」 |
| **O-9** | err 分支补显式写 `DW_DMA_CHAN_EN` | err 分支内 `dw_write(…DW_DMA_CHAN_EN, DW_CHAN_MASK(channel))` | **(c) 丢了** | 含该文件的 **374 个 ref** 全部扫描：ISR err 分支（`dma_dw_common.c:54-76`）内 `DW_DMA_CHAN_EN` **零命中**。唯一同类写在 `:751`，属 `dw_dma_stop()` 常规路径，来自上游老提交 `2f792c2de59`，非本条 | **重做**（并先裁定是否仍需要 —— 见 §2.1） |
| **O-6** | `dw_dma_isr()` err 循环补 `state = DW_DMA_IDLE` | err 分支内 `state = DW_DMA_IDLE` | **(c) 丢了** | 同上 374 ref 扫描：err 分支内 `DW_DMA_IDLE` **零命中**。`84e1a82c26f` 只在 `dw_dma_stop()` 早退分支（`:707-710`）加了 state 复位 —— **同症状的另一种修法，不是本条** | **重做**（并先裁定 —— 见 §2.1） |
| **O-7** | `case_destructive` 入 `cases.yaml` + 新建 `dma_test/tests/COVERAGE.md` | `case_destructive` | **(b) 已提交** | `810:b879216` 引入、`43b232e` 更新；HEAD `regression/cases.yaml:34`；`git cat-file -e HEAD:projects/dma_test/tests/COVERAGE.md` → IN-HEAD | ⚠️ 工作区正把 cases.yaml 从「case 级」重构为「工程级」，dma 的 `case_destructive` 现由 `projects/dma_test/tests/cases.yaml`（**未跟踪**）声明，值仍在 |
| **O-c** | DW DMA 块长掩码漏划变体 + TFR 未解除屏蔽 | `MASK(10, 0)` / 无条件 `dw_write(…DW_MASK_TFR, DW_CHAN_UNMASK(channel));` | **(b) 已提交** ⚠️ | zephyr `84e1a82c26f`；`dma_dw_common.h:182-190`（BLOCK_TS_MASK 移入 per-variant `#if`，2.20a 取 `MASK(10,0)`）、`dma_dw_common.c:544-550`（TFR 无条件解除屏蔽） | **勿报为丢失** |
| **O-16** | eth 两处：`mdio_dwmac.c` 接返回值 + `phy_realtek_rtl8211f.c` 加超时 | `PHY_RT_RTL8211F_RESET_TIMEOUT_MS` / `k_timepoint` in phy | **(c) 丢了**（对照，已预期） | 符号**全分支零命中**；`drivers/mdio/mdio_dwmac.c:275` 仍是裸调用 `mdio_dwmac_ethphy_reset(dev);`（返回值丢弃）；phy 文件无 `k_timepoint`；21 个 stash 无此改动 | **重做**（随 O-26 搁置，见 §2.1） |

---

## 2. 丢失项清单（(c) 的 4 条）

### 2.1 ⚠️ 先分清「真丢」与「有意未做被误记为已修」

**这是本轮最重要的发现，会改变重做的范围：**

- **O-9 / O-6** —— 不是「被 git 冲掉」，而是**伴随的工程笔记明写「有意未动」**。
  `projects/dma_test/tests/notes/DMA_DW_BLOCK_TS_MASK_AND_TFR_STATE_ROOTCAUSE.md`（**在 HEAD，已提交**）
  的「未修 / 存疑（留给后续裁定）」第 1 条原文：

  > 错误分支同族缺陷：`dw_dma_isr()` 的 ERR 分支同样不复位 `state` —— …本次三个失败未覆盖该路径，**为控制爆炸半径未动**。

  即：**ERR 分支的加固是「有意不做」的裁定，不是丢失**。而 OPEN_ITEMS 的 `O-6` 记「已修」、`O-9` 记「已验证…据此在 err 分支补显式…」——
  二者**互相矛盾**。同一个 note 的 O-9 小节又说「ERR 路径补显式禁用」做了（也不在树里）。

  ⇒ **重做前必须先向用户/驱动侧裁定**：ERR 分支到底做不做？（`O-8` 已裁定「忽略」⇒ 与之相关的两条应当
  **改成「有意未做，随 O-8 忽略」而不是「重做」**。）
  **不要**因为看到 (c) 就闭着眼睛补代码 —— 那会推翻一个已提交的裁定。

- **O-18 / O-16** —— **真丢**，且修复内容明确、无争议裁定：
  - **O-18**：HAL `soc/rv32/qsh/ls_soc_gpio.c:333` 由 `OE_DIN &= ~(1<<x->num);`（清 DIN 回读位，空动作）
    改为清 **OE 高半区**（`~((1<<x->num)<<16)`）。语义依据（`reg_app_pmu_rg.h` 的 `IOE_DIN_OE`：DIN=0xffff@0 / OE=0xffff0000@16；
    同库 leo 版只清 OE）与实机前后值（`0x00100000`→`0x00000010`）已在 OPEN_ITEMS O-18 条记全，照做即可。
  - **O-16**：① `drivers/mdio/mdio_dwmac.c:275` 接住 `mdio_dwmac_ethphy_reset()` 返回值；
    ② `drivers/ethernet/phy/phy_realtek_rtl8211f.c` 的 `:162`/`:168-174` 两处无界 `do/while` 自旋改 `k_timepoint` 有界超时。
    随 `O-26` 搁置（eth 已在 `regression/cases.yaml` 屏蔽），**落不落由用户裁定**。

### 2.2 丢失项汇总

| ID | 结论 | 要重做什么 | 前置裁定 |
|---|---|---|---|
| **O-18** | 真丢 | HAL `ls_soc_gpio.c:333` 清 OE 高半区（1 行） | 无（用户已裁定「修」，只是丢了） |
| **O-16** | 真丢 | ① `mdio_dwmac.c:275` 接返回值 ② phy 两处自旋加 `k_timepoint` 超时 | 随 O-26 搁置，是否现在落待用户裁定 |
| **O-9** | 树里无，但 note 记为「有意未动」 | err 分支补 `dw_write(…DW_DMA_CHAN_EN, DW_CHAN_MASK(channel))` | **先裁定** —— 与 O-8「忽略」冲突 |
| **O-6** | 树里无，但 note 记为「有意未动」 | err 循环补 `state = DW_DMA_IDLE` | **先裁定** —— 同上 |

> 注：`84e1a82c26f` 已用 `dw_dma_stop()` 早退分支的 state 复位**替代性地覆盖了同一症状**
> （「TFR 完成后 ChEn 自清 ⇒ 通道残留 ACTIVE ⇒ 后续 config -EBUSY」）。
> 即 **O-6/O-9 想防的 bug 症状当前已被另一种机制挡住**，这也降低了「立刻补 ERR 分支」的紧迫性。

---

## 3. 无法判定项（1 条）

| ID | 卡点 |
|---|---|
| **O-b** | 条目内容为「6 工程入口地址硬编码 + **5 工程 `TEST_NAMES`/`TGT_BANNERS` 残留（假结果）**」。<br>**可验部分**：入口地址 —— 已修。HEAD 中 `0x10000000` 仅出现在**注释**（`xxx/run_test.py:42`「cpu1 入口 = 0x10000000」）与 `common/acquire.py` 的**参数默认值**（`:187` `target_rom_start: str = "0x10000000"`）中，无逐工程硬编码。<br>**不可验部分**：`TEST_NAMES`/`TGT_BANNERS`「残留」—— 本条的修复是**删除**，提不出「新增」特征串；且 `TGT_BANNERS` 在 HEAD 中**仍存在于多工程**（如 `projects/adc_temper_test/tests/scripts/dbg_run.py` 3 处），`TEST_NAMES` 也仍以合法的 `TEST_NAMES_BY_CASE` 表形式存在（dma 各 case 独立表，属设计）。**无从区分哪 5 个是「残留」、哪个常量是「对的」** —— 台账没写工程名、没写常量名。<br>**要判定需要**：台账补出「哪 5 个工程 / 哪个常量 / 原值是什么」，或直接对比 09-16 前后的 `dump_results.py` 产物与真值。 |

---

## 4. 核销表 — 第二轮（「已修」但**没写**「未提交」的条目）

> 委托方提示：**真正的风险是这些**（人们默认它们已提交，从不验）。结论：**本轮未发现新的丢失项 —— 8 条全部在树里。**

| ID | 声称的修复 | 特征串 | 判定 | 证据 |
|---|---|---|---|---|
| **O-11** | （同第一轮） | `addr2napot is a pure bit-op` | **(b) 已提交** | `810:b879216` |
| **O-15** | （同第一轮） | `(uint64_t)timeout_ms` / `4294967263u` | **(b) 已提交** | zephyr `e862da23a40` + 810 HEAD |
| **O-20** | `eth_test` `print_report()` 改走 `mem_state_line()` | `mem_state_line` | **(b) 已提交** | `810:43b232e`；HEAD `projects/eth_test/tests/run_test.py` 5 处 |
| **O-21** | `emmc_block_test.c` 三处注释与 O-19 证伪结论同口径 | `该推断已被实测证伪` | **(b) 已提交** | `810:43b232e`；`projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c:27-35`（`git status` 干净） |
| **O-22** | `dbg_run.py` capture 轮排空 gdb stdout（18 工程） | `_drain_stdout` | **(b) 已提交** | `810:43b232e` / `d073ddf`；`projects/eth_test/tests/scripts/dbg_run.py:160-166,205-212` |
| **O-12** | `run_all.py` 兜底行传 `passed=None,total=None` → 渲染「未知」 | `update_summary(…"FAIL", None, None,` + `SUMMARY_RE` 含 `未知` | **(b) 已提交** | HEAD `regression/run_all.py:749`（另 `:685` ENV-ABORT）、`common/report_dir.py:22-24`（`(?:(\d+)/(\d+)|未知)`）、`:25-27` `_num()` |
| **O-13** | token 宏从 `do{}while(0)` 恢复成真 `printk` | `I3C_I2C_TOKEN_PREFIX` + `printk(__VA_ARGS__)` | **(b) 已提交** | `810:43b232e`；HEAD `projects/i3c_i2c_switch_test/tests/include/i3c_i2c_switch_test_protocol.h`（`#define I3C_I2C_OUT(...) printk(__VA_ARGS__)`） |
| **O-a** | `flash_test` `ZTEST_SUITE` teardown 槽位 | `ZTEST_SUITE(flash, NULL, …, NULL, NULL, flash_test_teardown)` | **(b) 已提交** | `810:23213d0`；HEAD `projects/flash_test/cpu1_flash_test/src/flash_test.c:351`（第 6 参 = teardown，`after` 槽为 NULL） |
| **O-d** | mbedtls SM4 包装层吞掉 HAL 错误码 | — | **(b) 已提交** | mbedtls `9f511f8a0`(2026-09-17 10:42)，即台账自称的 hash，核实无误 |
| **O-b** | 6 工程入口地址 + 5 工程 TEST_NAMES/TGT_BANNERS 残留 | — | **无法判定** | 见 §3 |

---

## 5. 附带发现（不在待审清单内，但与本审计强相关）

### 5.1 eMMC mutex 泄漏 —— **已被重落并提交**，台账「丢失」结论已过期

OPEN_ITEMS 的 `O-16` 备注与 `projects/emmc_test/tests/COVERAGE.md`（09-23 **04:26** 写的更正批注）都断定
「mutex 泄漏 2026-09-18 已修那一版**零命中**、不在树里」。

**实测结论：已被重新实现并提交。**
- zephyr `df4bc2a46e7` (2026-09-23 **10:48:26**) 「sdhc: linkedsemi: fix mutex leak on invalid opcode, reject out-of-range clock」（+67/-4）
- 树里现可见：`drivers/sdhc/sdhci-of-linkedsemi.c:425` `linkedsemi_sdhci_data_opcode_supported()`、
  `:467` 前置校验（**在加锁之前**）、`:532` `goto unlock;`、`:562` `unlock:` 标签。
- `git log --all -S "data_opcode_supported"` → **唯一命中 `df4bc2a46e7`**（即 09-23 那次提交才引入）。

⇒ **`emmc_test/tests/COVERAGE.md` 那条 04:26 的批注（「未提交，待上板实测」）已经过期**（晚 6 小时的提交推翻了它）。
建议：更正该批注为「已提交 `df4bc2a46e7`」。⚠️ **但「是否上板实测通过」本审计无法判定** —— 只证了代码在树里。

### 5.2 「今天已重落」的四条中，两条被任务书描述错了位置

任务书说 O-5/O-6/O-9/O-c「应在**工作区 diff** 里可见」。实测：
- **O-5 / O-c**：**不在工作区**，而是**已提交**（`84e1a82c26f`）。`zephyr git status` 干净（仅 7 个无关文件）。
- **O-6 / O-9**：**工作区没有、提交里也没有** —— 即 §2.1 那两条。

⇒ 任务书这条前提是错的（**记忆规则「任务书事实须标为假设」再次命中**）。

### 5.3 那 4 个 driver 提交是同一批、同一人

`84e1a82c26f`(10:47:57, dma) → `df4bc2a46e7`(10:48:26, sdhc) → `c98b6eb21a1`(10:48:53, qspi) → `e862da23a40`(10:49:23, wdt)，
作者均为 `Jc Shi`，**全部在 HEAD 且已推到 `origin/driver_test`**。OPEN_ITEMS 记录这批时**未同步**状态。

### 5.4 丢工作的机制已由 reflog 二次确认

`git -C zephyr reflog`：`bc7729ae091 HEAD@{2026-09-21 11:32:03}: reset: moving to HEAD` +
`HEAD@{2026-09-21 11:32:30}: checkout: moving from otbn_irq to driver_test`。
**兜底排查已完成**：`git stash list`(21 个 zephyr / 1 个 810 / 10 个 HAL / 2 个 mbedtls) + `git worktree list`(仅主工作树)
+ 全 ref 树扫描 —— **丢失的 4 条不在 stash / 别的工作树 / 任何分支上**。
（`O-15` 之所以幸存，是因为它恰好在那天被写进了会保留的路径；**选择性丢失已由本轮逐条实测坐实**。）

---

## 6. 方法学观察

### 6.1 特征串可提取性：14 条里 13 条可提，1 条不可

| 类别 | 例子 | 可提取性 | 说明 |
|---|---|---|---|
| 新增**日志文本** | O-5 `"source and destination address are identical"` | ★★★ 最好 | 全库唯一，连 commit message 都能命中 |
| 新增**符号名/宏** | O-33 `need >= 2 openable`、O-3 `ilen % 16 != 0`、O-c `MASK(10,0)` | ★★★ | 稳，不受行号漂移影响 |
| 新增**断言写法** | O-32 `ctrl_after & IWDG_CTRL_ENABLED` | ★★ | 稳，但要小心同名符号出现在**文档**里（`git grep HEAD` 会同时命中 `OPEN_ITEMS.md` 本身 ⇒ 必须限定 `-- <path>`） |
| 新增**常量/魔数** | O-15 `4294967263u` | ★★ | 稳，但若同值在别处也合法出现会污染 |
| **纯删除**类 | **O-b**「TEST_NAMES/TGT_BANNERS **残留**」 | ☆ 不可提 | 修复 = 删东西 ⇒ 没有「新增」特征串；且被删目标本身可能合法存在 ⇒ **无法判定** |

### 6.2 哪类修复天然难验

1. **纯删除 / 去重类**（O-b、O-e）—— 只能靠「应该消失的东西消失了」反推，而「消失」与「合法存在」不可区分。**建议台账登记时要求写「删掉了哪个文件/哪个常量的哪个值」**。
2. **改判 / 改注释类**（O-21、O-15 的 810 侧）—— 特征串可搜，但「改对了」需读语义，**git 只能证「字在了」，证不了「判据对」**。
3. **文档声称的修复**（O-16）—— 只能验「符号是否存在」；「行为是否正确」必须上板。本轮 O-16 恰好是「符号根本不存在」⇒ 可确证；若符号在但行为错，本方法**完全失效**。
4. **`git -S` 的固有盲区** —— `-S` 数的是**出现次数变化**，改名/移动会误报命中，纯增删才是可靠信号。本轮所有「命中」都额外用 `git grep <HEAD> -- <path>` 复核了「**当前树里确实在**」，未只依赖 `-S`。

### 6.3 最该改的流程问题：**「未提交」是个会过期的标记**

- `810:a405816`(09-22 17:22) 一提交，O-32/O-25/O-24 就已经是「已提交」；`4902483`(09-22 19:14) 提交了 O-33；
  `84e1a82c26f` 等 4 个提交(09-23 10:47-10:49) 提交了 O-5/O-c/O-15 与 eMMC。**但 OPEN_ITEMS 至今全记「未提交」。**
- ⇒ **只信台账会误报「丢了一大片」**（本轮初始假设正是这样，结果 10/14 是假警报）；
  反过来，**只信台账也会漏报**（O-16 的备注说 DMA 四处「零命中」，其实 09-23 已补上）。
- **建议**：备注里写「已修」时**同时写当时的 `git rev-parse HEAD`**；提交后由 keeper 扫一遍把「未提交」升级为「已提交 `<hash>`」。
  本审计**未改 `OPEN_ITEMS.md`**（该文件纪律：只有用户能关闭）。

### 6.4 本审计覆盖不到的面（诚实标注）

- 只验「**代码在不在树里**」，**不验「有没有跑绿」**。17 条 (b) 里各条台账自称的实机验证结论（如 O-11「5/5 PASS」、O-5「4/4 + 6/6 PASS」）
  本审计**未复核**，也不在本审计的方法能力内（需上板，被铁律禁止）。
- `O-b` 的语义含糊导致无法判定，**不猜**。
- 未审 `O-1/O-2/O-4/O-8/O-10/O-14/O-19/O-23/O-26/O-27/O-28/O-29/O-30/O-31` —— 这些是**裁定/证伪/搁置**类，
  非「代码改动待验」类，不在本审计范围。
