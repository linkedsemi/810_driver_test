# 阶段二 — 上板验证本轮 6 驱动修复

- 日期：2026-09-23
- 执行者：runner（独占硬件锁，**严格串行**）
- 背景：本轮（7 路 agent）为纯离线定位 + 改源码，**一行未编译、未上板**。本报告 = 编译 + 烧录 + 回归的实测记录。
- 纪律：结果如实记录；**不为变绿放宽断言/改判定口径**。

## 环境预检（执行前手动逐项，qsh-debug ref-self-heal §1）

| # | 项 | 结果 |
|---|---|---|
| 1 | DebugServer 进程 | PASS（PID 9340） |
| 2 | Port 1025 LISTENING | PASS |
| 3 | GDB 能连（唯一在线判据） | PASS（detached，rc=0） |
| 4 | 串口数量 | PASS（非 CKLink 4 口：COM17/16/18/5） |
| 5 | 无 FlashProgrammer 冲突 | PASS（无 csky 进程） |
| 6 | 无 1025 ESTABLISHED 残留 | PASS |

- 端口角色：COM18=MCP2200(ctrl,uart5)，COM16/17=CH342(target,uart1)，COM5=CP210x（**无关桌面口**），COM14=CKLink（排除）。
- 注：COM5 为无关口，**只影响 `run_all.py` 预检的自动发现分支**；本轮逐驱动单跑，不受影响。

---

## 逐驱动记录

### 1. wdt —— `projects/wdt_test` — ✅ 已验证修复

改动：`zephyr/drivers/watchdog/wdt_iwdg_ls.c` 守卫由「整寄存器非零」改为 `& (IWDT_EN|IWDT_RST_EN)` 掩码。

| 项 | 命令 | 结果 | 预期 | 达成 |
|---|---|---|---|---|
| 主 | `python run_test.py --case case_boundary` | **PASS (19/19)** + `PROJECT EXECUTION SUCCESSFUL` | PASS (19/19) | ✅ |
| 回归 | `python run_test.py --case case_non_destructive` | **PASS (5/5)** | 5/5 | ✅ |

**基线（`reports/2026-09-23-1/wdt_test_case_boundary`，修复前）**：18/19，`PROJECT EXECUTION FAILED`，
2 项 FAIL：`test_setup_pause_in_sleep_enotsup`（期望 -ENOTSUP，实得 -16 -EBUSY）、
`test_setup_twice_ebusy`（**第一次** setup 就返回 -16，期望 0）。

**⚠️ 与预期的偏差（逐字记录，不美化）**：
- 任务书预期"日志**不再出现** `interrupt already configured`"。**实测仍出现 1 次**（`serial_TGT.log:65`）。
- 但该次出现**落在 `test_setup_twice_ebusy` 用例内**，是"第二次 setup 应得 -EBUSY"的**预期触发路径**，
  该用例 PASS。基线（修复前）出现 **2 次**且其中一次是 `test_setup_pause_in_sleep_enotsup` 的**误触发**。
- 结论：预期措辞应精确为"**不再出现误触发**"。实测 2→1，保留的 1 次为合法路径 ⇒ 修复有效，**不判定为偏差不达标**。

**其余锚点**：`PROJECT EXECUTION SUCCESSFUL` 已见（`:86`）。

原始证据：`reports/2026-09-23-1/wdt_test_case_boundary/serial_TGT.log`、
`reports/2026-09-23-1/wdt_test_case_non_destructive/serial_TGT.log`。

---

### 2. emmc —— `projects/emmc_test` — ✅ 已验证修复

改动：`zephyr/drivers/sdhc/sdhci-of-linkedsemi.c` —— ①非法 data-opcode 拒绝从锁内提前到锁外 + `unlock:` 标签；②`set_io` 越界时钟返回 `-EINVAL`。

| 项 | 命令 | 结果 | 预期 | 达成 |
|---|---|---|---|---|
| 主 | `run_test.py --case case_block_driver` | **PASS (10/10)** + `PROJECT EXECUTION SUCCESSFUL` | PASS (10/10) | ✅ |
| 主 | `run_test.py --case case_block_pio` | **PASS (10/10)** + `PROJECT EXECUTION SUCCESSFUL` | PASS (10/10) | ✅ |

**最有区分度的单点（"锁外拒绝"直接指纹）——全部达成：**

| 锚点 | 修复前（任务书给值） | 实测 | 达成 |
|---|---|---|---|
| `request_mutex_not_leaked` 的 `probe_ret` | `2147483647` | **`-134`** | ✅ |
| 同上 `join` / `done` | — | `join=0` / `done=1` | ✅ |
| `set_io_clock_out_of_range_not_silent` 的 `ret` | `0` | **`-22`** | ✅ |

- 两 case 的 INFO 行完全一致：`request_mutex_not_leaked: join=0,probe_ret=-134,probe_ret2=-134,done=1`、
  `set_io_clock_out_of_range_not_silent: f_min=50000,clock=1000,ret=-22`。
- `-134` = `-ENOTSUP`（非法 opcode 锁外拒绝）；`-22` = `-EINVAL`（越界时钟）。

**说明**：emmc 的**修复前基线已被本轮实测覆盖**（`--session-dir` 指向 23-1，同名 case 目录按复测覆盖规则被替换）；
上表"修复前"列取自**任务书给定的 before 值**（`2147483647` / `0`），非本轮重新取证。

原始证据：`reports/2026-09-23-1/emmc_test_case_block_driver/serial_TGT.log`（INFO 见 `:66,71,77`）、
`reports/2026-09-23-1/emmc_test_case_block_pio/serial_TGT.log`（INFO 见 `:71,77`）。

---

### 3. dma —— `projects/dma_test` — ✅ 已验证修复

改动：`zephyr/drivers/dma/dma_dw_common.c/.h` —— TFR 中断解除屏蔽移出 `else`（两种回调模式都 unblock；
配套清陈旧回调指针）；`stop()` 早退分支复位 state；恢复 `src==dst → -EINVAL`。

| 项 | 命令 | 结果 | 预期 | 达成 |
|---|---|---|---|---|
| 主 | `run_test.py --case case_default` | **PASS (7/7)** + `PROJECT EXECUTION SUCCESSFUL` | PASS (7/7) | ✅ |
| 主 | `run_test.py --case case_destructive` | **PASS (9/9)** + `PROJECT EXECUTION SUCCESSFUL` | PASS (9/9) | ✅ |

**判据锚点（逐条核，两 case 均达标）：**

| 判据 | 基线（23-1，修复前） | 本轮实测 | 达成 |
|---|---|---|---|
| `channel 0 must be inactive to reconfigure` 出现次数 | default **6** / destructive **7** | default **0** / destructive **0** | ✅ |
| `test_dma_self_copy_rejected` | 依赖 `-EBUSY`（基线 FAIL） | 新增日志 `source and destination address are identical (0x100848c0)`（`serial_TGT.log:41`），**无任何 `EBUSY` 字样**，断言 PASS | ✅ |
| `PROJECT EXECUTION SUCCESSFUL` | FAILED | 两 case 均见 | ✅ |

- **低风险残留项**：任务书提示 `test_dma_loop_transfer` 若 TFR 与 BLOCK 分两次 ISR 上报仍有偶发窗口。
  **本轮首跑即 PASS，未观察到偶发失败** ⇒ 按任务书"若偶发失败连跑两遍"的条件**未触发**，无需复跑。

原始证据：`reports/2026-09-23-1/dma_test_case_default/serial_TGT.log`、
`reports/2026-09-23-1/dma_test_case_destructive/serial_TGT.log`。

---

### 4. qspi_flash —— `projects/qspi_flash_test` — ✅ 已验证修复（含"改动从未编译过"这一未定项已消解）

改动：`zephyr/drivers/spi/qspi_ls.c` 新增 `CALIB_COMBO_REF(0xFE)` 哨兵 + 3 行 `BUILD_ASSERT`；
另有**用例侧**改动（器件容量改为运行期推导）。

| 项 | 命令 | 结果 | 预期 | 达成 |
|---|---|---|---|---|
| 主 | `run_test.py --case case_default`（`rm -rf case_default/build` 后全量重建） | **PASS (26/26)** + `PROJECT EXECUTION SUCCESSFUL` | 26/26 | ✅ |
| 主 | `run_test.py --case case_no_calib`（`rm -rf case_no_calib/build` 后全量重建） | **PASS (26/26)** + `PROJECT EXECUTION SUCCESSFUL` | 26/26 | ✅ |

**★ 最硬的未定项已消解 —— `BUILD_ASSERT` 编译通过**：
`qspi_ls.c` 在**两个 case 各自的干净 build 目录**下均成功编译
（`[1184/1198] Building C object zephyr/drivers/spi/CMakeFiles/drivers__spi.dir/qspi_ls.c.obj`、
`[1185/1198] …`），**无任何 `error:` / BUILD_ASSERT 失败** ⇒ 语法正确、条件在两个配置下均成立。

**判据锚点（`case_default/serial_TGT.log`）：**

| 锚点 | 基线（`qspi_flash_rootcause.md` 记录，修复前） | 本轮实测 | 达成 |
|---|---|---|---|
| `hw config`（6.25MHz 边界项，`:86`） | `cap_neg=1 cap_dly=0 (calib_combo=0x01)` | **`cap_neg=0 cap_dly=0 (calib_combo=0xfe)`** | ✅ |
| `set_freq_boundary_6m25_accepted` 的 `reinit`（`:90`） | `-19` | **`0`** | ✅ |
| `invalid jedec id 00 00 00` 出现次数 | 多次（`spi_nor` 重试 + `flash not present`） | **0** | ✅ |
| 用例总数 | **14/26**（`qspi_flash_rootcause.md` §头部） | **26/26** | ✅ |

- `case_no_calib` 侧 `calib_combo=0x00`（关闭校准模式，**与基线同值**，符合"该 case 不走校准"的语义）；
  `reinit=0`、无 jedec 错误。基线为 **24/26** → 本轮 **26/26**。
- 两 case 均 `grep -c "FAIL -"` = **0**，`PROJECT EXECUTION SUCCESSFUL` 均见（`:251` / `:242`）。

**过程说明**：本轮按 `run_test.py` 执行（其内部先做全量重建再跑），
比任务书给的 `test.sh -g`（**仅构建、不加载**）多覆盖了"运行 + 采集"两段；
构建段与 `test.sh -g` 等价。`case_*_build` 两个目录在跑前已 `rm -rf`，满足"驱动 `.c` 改了必须全量重建"。

原始证据：`reports/2026-09-23-1/qspi_flash_test_case_default/serial_TGT.log`、
`reports/2026-09-23-1/qspi_flash_test_case_no_calib/serial_TGT.log`、
背景依据 `reports/2026-09-23-1/qspi_flash_test_case_default/qspi_flash_rootcause.md`。

---

### 5. spif —— `projects/spif_test` — ✅ 已验证修复（主判据项）／⚠️ 1 项诊断未闭合（非本改动职责）

改动：`modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.c` 的 `spif_pinctrl_mode_check()` 增加
`gpio_ana_enabled(pin)` 比对；用例侧新增 2 处诊断 `ZT_INFO`（**判据未改**）。

| 项 | 命令 | 结果 | 预期 | 达成 |
|---|---|---|---|---|
| 主 | `rm -rf case_default/build` + `run_test.py --case case_default` | **17/18 PASS**，`execution=FAILED` | passthrough_mode_switch PASS + pinctrl_state_switch PASS | ✅（两项均 PASS） |

**判据锚点（`spif_test_case_default/serial_TGT.log`）：**

| 锚点 | 基线（16/18，`spif_rootcause.md`） | 本轮实测 | 达成 |
|---|---|---|---|
| `passthrough_mode_switch` | FAIL（`spif_test.c:776`，`passthrough_mode_check(dev) is true`） | **PASS**（`:167`） | ✅ |
| 该用例 INFO `after_cold_reset` | `filter=1,passthrough=1` | **`filter=1,passthrough=0`**（`:165`） | ✅ |
| `pinctrl_state_switch`（回归对照） | PASS | **PASS** | ✅ |

**★ 上板诊断项 `cmd_idx_accessors`（按任务书要求：只记录数值，不改判据）：**

```
INFO cmd_idx_accessors: slot5_raw=0x77,cmd=0x77,raw_dummy=0,get_dummy=0     (serial_TGT.log:38)
```

- `raw_dummy=0` ⇒ 命中假设 **(a)**：**该槽的 DUMMY 字段不可写/未实现**（`spif_rootcause.md` §2 判读表）。
- 该用例仍 **FAIL**（`spif_test.c:341`，`dummy not equal to 9`）；**判据未被放宽**，
  断言行号由基线 `:327` 移到 `:341`（= 新增 2 处 ZT_INFO 造成的行移，判据文本不变）。
- 边界：这 1 项 FAIL 的处置（改用例 vs 改驱动）**不在本轮改动范围**，属另案（`OPEN_ITEMS` O-31 类型）。

**构建证据**：`spi_filter.c` 源 mtime `04:28` < 其 `.obj` mtime `05:02` ⇒ 修改后的模块源码**确已重新编译**并入镜像
（跑前已 `rm -rf case_default/build` 全量重建）。

原始证据：`reports/2026-09-23-1/spif_test_case_default/serial_TGT.log`、
背景依据 `reports/2026-09-23-1/spif_test_case_default/spif_rootcause.md`。

---

### 6. i2c —— 两个 case 的判别实验

**前置**：已读 `reports/2026-09-23-1/i2c_failure_triage.md`，实验设计与判据取自该报告 §3.4 / §4.4，未重新调查。

#### B `i3c_i2c_switch_test::broadcast_nack_recovery` — ✅ 已验证（复现为**确定性失败**，非偶发）

命令（跑 3 次取众数）：
```bash
cd projects/i3c_i2c_switch_test/tests
python run_test.py --case case_switch_core --ctrl-uart COM18 --tgt-uart COM16 --session-dir <23-1>
```

| 轮次 | 结果 | broadcast_nack_recovery | 失败 reason |
|---|---|---|---|
| 1 | **FAIL** | FAIL | `CTRL:rstact_whole_should_nack` |
| 2 | **FAIL** | FAIL | `CTRL:rstact_whole_should_nack` |
| 3 | **FAIL** | FAIL | `CTRL:rstact_whole_should_nack` |

- **众数 = FAIL（3/3）**，三次 reason 串**逐字相同**，其余 10 项均 PASS（CTRL=FAILED / TGT=SUCCESSFUL）。
- ⇒ **稳定失败、零偶发通过**。与 `i2c_failure_triage.md` §4.2「稳定复现，不是环境 flake」一致；
  报告 §4.3 的归因（**用例自身时序竞态**：首帧 `k_sleep(6ms)` 抢 target 6~8ms NACK 窗口，
  PASS→FAIL 钉在单出口改造 `43b232e`）在本轮得到**独立复现支持**。
- **未改任何断言**。归属仍按报告结论：**非被测驱动缺陷**。

原始证据：`reports/2026-09-23-1/i3c_i2c_switch_test_case_switch_core/serial_CTRL.log`（第 3 轮覆盖同名目录）。

#### A `i2cf_test::filter_blocks_nonwhitelist` — ⚠️ 结论已撤回（同日复核）

> **⚠ 撤回（2026-09-23 同日）**：本节原判"结论 **H1 成立**"。复核发现**该判别实验的两条读数都不能作证据** ——
> ① 判读样本 `fresh_off_ret=-5` 用的是**裸 `i2c_read`**（本通路裸读本身不可靠），且该步**紧跟在一次失败事务之后**
> （该位置本来就多半失败，见 KNOWN_ISSUES #116）；② "旁路对照" `bypass_ret` 发波方是上游 `i2c2`、引脚却交给
> `i2c4` ⇒ **恒 `-5`**，不构成对照。⇒ **H1 既未证实、也未否定**（`KNOWN_ISSUES #112` 已回退"未定"）。
> 完整复核见 `i2c_failure_triage.md` §9.4。**以下原文保留作记录。**

**实验实现**（按 §3.4 试验 1，新增**只测不断言**的诊断用例；判据未改）：
`i2cf_test.c` 末尾新增 `#ifdef CONFIG_I2CF_FILTEROFF_PROBE` 保护的 `i2cf::filteroff_fresh_probe`，
新增 Kconfig `I2CF_FILTEROFF_PROBE`（默认 n），新增一次性 case 目录
`tests/case_filteroff_probe/{test.sh,filteroff_probe.conf}` —— 全部照 `case_scan_burst` 的既有"只增不改"范式，
**`case_default` 不置位 ⇒ 既有构建逐字节不变**。

实验序列（原文照 §3.4）：`i2cf_reset_all` → `switch_to_filter` → `filter_off` → 上游读 `0x33`，
**前置无任何 filter_on、无任何违规拦截**。

**实测结果**：

```
INFO filteroff_fresh_probe: target=0x33,bypass_ret=-5,fresh_off_ret=-5     (serial_TGT_raw.log:100)
```

| 量 | 值 | 含义 |
|---|---|---|
| `fresh_off_ret` | **-5**（`-EIO`，读**失败**） | ⇒ 按 §3.4 判据：**H1 成立** |
| `bypass_ret` | -5 | ⚠️ **该对照设计有误**，见下 |

**结论：H1 成立** —— 在**前置无任何违规**的前提下，`_en(false,false,·)`（"关滤波器"）
**依旧不转发**同一地址。拦截与否**无关**：既然从未拦截过，就不存在任何可残留的阻塞态，
H2（残留态、需显式清中断）**被证伪**。
⇒ `filter_blocks_nonwhitelist` 自证步"关掉滤波器后 0x33 应可达（`ret_free==0`）"
这个**期望本身在本 HW 上不成立**；**不是**"拦截留下残留态"。

**有效的对照（不是我加的，来自同一轮的其它用例）** —— 排除"链路/器件坏"：

- `filter_passthrough_whitelist` PASS：经**同一路**（上游 `i2c2` → i2cf4 → 下游）读 `0x33` 成功
  （`INFO ... addr=0x33,len=8,rd=5a5b5c5d`）⇒ 路径与从设备都正常。
- `slave_addr_scan` PASS：`count=2, addrs=33 50` ⇒ `0x33` 在总线上应答。
- 探针执行期间**无** `address beyond whitelist` 日志（`serial_TGT_raw.log` 该时段干净）⇒ 确实"没拦"。

**⚠️ 自我纠错（必须记录）**：我加的 `bypass_ret` 对照**无效** —— 它用**上游** `i2c2` 去读，
但 `switch_to_master()` 是把下游侧 pad(PE02) 切给旁路 master `i2c4`，
上游 `i2c2` 在该模式下**本就不在通路上**，故 `-5` 是设计错误导致的假阴性，
**不是**"器件不可达"的证据。该值**不得**用于任何推断；有效对照是上面的 `filter_passthrough_whitelist`。
（该对照不参与结论，`fresh_off_ret` 的独立性由同轮 passthrough 用例保证。）

**处置**：按任务书"**两种结论都不要去改断言**"，`filter_blocks_nonwhitelist` 的判据**一字未动**，
本项在该 case 中**仍为 FAIL**。本项的后续处置（改用例）属 `OPEN_ITEMS` O-30 的裁定范围，**不在本轮授权内**
（O-30 已结，实际改法 = **单变量对照 + 热身一笔**；依据是"位置不可测"，**非**本节判的 H1 —— 见上方撤回段）。

**遗留物（请裁决）**：为使实验可复现，以下为验证用途新增，**未被登记进任何 cases.yaml、不影响回归**；
按仓库"一次性诊断用完可删"的惯例保留在原地：
`projects/i2cf_test/cpu1_i2cf_test/Kconfig` 的 `I2CF_FILTEROFF_PROBE`、
`i2cf_test.c` 该 `#ifdef` 块、`tests/case_filteroff_probe/`。

原始证据：`reports/2026-09-23-1/i2cf_filteroff_probe/serial_TGT_raw.log`（`:99-101`）。

---

### 7. spi —— `projects/spi_test` — ⏸ 未执行（按任务书跳过）

- `spi_test::link_probe` 的根因是**外部回环跳线未接**（已确认为**非驱动 bug**）。
- 接线属**物理动作**，不在本轮授权范围 ⇒ **按任务书跳过，未烧录、未采集**。
- **待接线后复跑**（届时回归命令：`projects/spi_test/tests/` 下 `run_test.py --case case_default`）。

---

## 汇总表

| # | 驱动 | 结果三态 | 关键命令 | 关键证据 | 意外发现 |
|---|---|---|---|---|---|
| 1 | **wdt** | ✅ **已验证修复** | `run_test.py --case case_boundary` / `--case case_non_destructive` | **19/19** + **5/5**，`PROJECT EXECUTION SUCCESSFUL`；基线 18/19 FAILED | 见「意外 1」 |
| 2 | **emmc** | ✅ **已验证修复** | `--case case_block_driver` / `--case case_block_pio` | 两 case **10/10**；`probe_ret` 2147483647→**-134**、`join=0/done=1`、`set_io ret` 0→**-22** | 无 |
| 3 | **dma** | ✅ **已验证修复** | `--case case_default` / `--case case_destructive` | **7/7** + **9/9**；`must be inactive` 6/7 次→**0 次**；新增 `address are identical` 日志、**无 EBUSY** | `loop_transfer` 首跑即过，未触发复跑条件 |
| 4 | **qspi_flash** | ✅ **已验证修复** | `rm -rf build` ×2 + `--case case_default` / `--case case_no_calib` | 两 case **26/26**；combo **0xfe**、`reinit` -19→**0**、`invalid jedec` 消失；基线 14/26 / 24/26 | 见「意外 2」 |
| 5 | **spif** | ✅ **已验证修复**（主判据）／⚠️ 1 项诊断未闭合 | `rm -rf build` + `--case case_default` | **17/18**；`passthrough_mode_switch` FAIL→**PASS**（`passthrough=1`→**`0`**）；`pinctrl_state_switch` PASS | 见「意外 3」 |
| 6A | **i2c / i2cf** | ⚠️ **实验完成，判据已撤回**（**非"修复验证"**） | 新增 `case_filteroff_probe` 判别实验 | ~~`fresh_off_ret=**-5**` ⇒ H1~~ **撤回**（读数无判别力：裸读 + 紧接失败事务）⇒ H1 既未证实也未否定；改用例依据改为"位置不可测" | 见「意外 4」 |
| 6B | **i2c / i3c_i2c_switch** | ✅ **已验证（确定性失败，非偶发）** | `--case case_switch_core` ×3 | **3/3 FAIL**，reason 逐字相同 `CTRL:rstact_whole_should_nack` ⇒ 稳定复现，非 flake | 无（与报告预判一致） |
| 7 | **spi** | ⏸ **未执行（跳过）** | — | 根因是外部回环跳线未接（非驱动 bug），接线不在授权内 | — |

**总计**：5 项驱动修复 ✅ 验证通过（wdt / emmc / dma / qspi_flash / spif 主判据）；
1 项判别实验（i2c-A：**结论已撤回**）；1 项稳定性复现 ✅（i2c-B：确定性失败）；1 项 ⏸ 跳过（spi）。
**未出现"改了反而更差"的情况；无构建失败；无结果作废（`error 138` / `Debugserver quit` 全程未现）。**

---

## 意外结果高亮

> 只列**与任务书预期不符**或**需要裁决**的项。无"改坏了"类发现。

**意外 1（wdt）—— "日志不再出现"与实测不符（措辞偏差，非功能问题）**
任务书预期"日志不再出现 `interrupt already configured`"，**实测仍出现 1 次**。
核查后确认该次落在 `test_setup_twice_ebusy` 内、是"第二次 setup 应得 -EBUSY"的**合法触发路径**；
基线为 **2 次**（其中 1 次是 `test_setup_pause_in_sleep_enotsup` 的**误触发**，正是被修掉的 bug）。
⇒ 应表述为"不再出现**误触发**"。**不判定为修复不达标**。

**意外 2（qspi_flash）—— 任务书给的"修复前"标量与归档基线不完全对应**
任务书写"由 `cap_neg=1 cap_dly=0 (calib_combo=0x01)` 变 `0xfe`"；
归档基线里 `0x01` 只出现在**6.25MHz 边界**那一项（`:86`），其余时段基线是 `calib_combo=0x05`。
**结论不受影响**（修复后全时段一致为 `0xfe`，且 `reinit` -19→0、jedec 错误消失、26/26），
但"before 值"应以 `qspi_flash_rootcause.md` 的记录（14/26 / 24/26）为准。

**意外 3（spif）—— `cmd_idx_accessors` 是新 FAIL，但**不是**改坏的**
基线 16/18 中它就 FAIL；本轮 17/18（`passthrough_mode_switch` 修好，它照旧 FAIL）。
诊断读数 `raw_dummy=0, get_dummy=0` ⇒ 命中假设 **(a)：该槽 DUMMY 字段不可写/未实现**。
该用例的 FAIL **未被本次驱动改动触及**，处置（改用例 vs 改驱动）属另案（O-31 类型）。

**意外 4（i2c-A）—— ⚠️ 原记"判别实验推翻 H2、坐实 H1"，该结论已撤回（同日）；我加的对照无效（已自我纠错）**
- ~~结论面：`fresh_off_ret=-5` ⇒ **H1**（"关滤波器"在本 HW 上就是不转发），**H2 被证伪**。~~
  **撤回**：`fresh_off_ret` 是**裸读**且**紧接在失败事务之后**（该位置本就多半失败）⇒ 无判别力；
  连同下方那条无效的 `bypass_ret` 对照，**两条读数都不能作证据** ⇒ H1 既未证实也未否定
  （见本节 §A 顶部撤回段 / `i2c_failure_triage.md` §9.4）。**处置"改用例"仍对，但依据换成"位置不可测"。**
- 过程面：我附加的 `bypass_ret` 对照**设计错误**（master 模式下上游 `i2c2` 不在通路上），
  其 `-5` 是假阴性、**不得引用**；有效对照取自同轮的 `filter_passthrough_whitelist`。

**过程瑕疵（如实记录）**
- emmc / qspi_flash 的**部分修复前基线**在取证前被本轮实测按"复测覆盖同名 case 目录"规则覆盖
  （`reports/` 未纳入 git，不可回溯）。emmc 的 before 值改用**任务书给定值**；
  qspi 的 before 值改用**同目录 `qspi_flash_rootcause.md`** 的记录。**结论依据充分，但取证顺序应先读后跑。**
- 为实验 A 新增的是一次性诊断代码（Kconfig + `#ifdef` 块 + 未登记的 case 目录），
  **不登记、不影响回归**，保留在原地供复现；如需清理见 6A 节列出的三处路径。

