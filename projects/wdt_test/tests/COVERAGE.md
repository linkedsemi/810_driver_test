# cpu1_iwdg 测试覆盖矩阵

> 被测驱动：`zephyr/drivers/watchdog/wdt_iwdg_ls.c`（`linkedsemi,ls-watchdog`）。
> 本工程主实例 = **cpu1_iwdg** `sec_iwdg`@0x400a1800（HART 复位域，cpu1）；
> `pmu_iwdg`@0x4002f800（SOC 复位域，cpu1）作为第二实例在 `case_boundary` /
> `case_pmu_iwdg_full_reset` 覆盖；`app_iwdg`@0x40099000（HART 复位域，cpu2）
> 由独立工程 `app_iwdg_cpu2_test` 覆盖。
> 全部为**片内**外设，无引脚接线（SPEC_REFERENCE §6.6）。
>
> 状态列口径：`✅ 待实测` = 用例已实现且编译通过，P6 首次实机回归后对表；
> `⬜ 未覆盖` = 本驱动路径当前无用例触及（**未验证**，非"判断不会发生"）；
> `⚠` = 已有用例但断言薄弱（2026-09-18 F1 波补齐后本节已无此类项；口径保留备用）。

## 1. 用例清单与运行方式

| case | 固件源集 | 破坏性 | 进批量回归 | 说明 |
|------|---------|--------|-----------|------|
| `case_non_destructive` | `src/wdt_test.c`（默认） | 否 | ✅ 已在 `cases.yaml` | sec_iwdg 基础 API 通路 |
| `case_boundary` | `src/wdt_boundary_test.c`（`-DWDT_BOUNDARY_TEST=y`） | 否 | ✅ 可进（P6 登记） | 边界 / 异常参数 / 第二实例 |
| `case_destructive_reset` | `src/wdt_destructive_test.c`（`-DWDT_DESTRUCTIVE_RESET=y`） | **是**（sec_iwdg HART 复位） | ❌ **不可**（会复位本 hart，需 flash 固化 + noinit 状态机） | Phase 2 已改为"只清状态并重跑"（不再打 SUCCESSFUL），判定见 §2.3.1 |
| `case_pmu_iwdg_full_reset` | `src/wdt_pmu_reset_test.c`（`-DWDT_PMU_RESET_TEST=y`） | **是**（pmu_iwdg **整芯片**复位） | ❌ **不可**（整芯片复位，连带重置另一核与全部外设） | 本次新增 |

### ⚠ 不可进批量回归的 case（必须在空白板环境中单独运行）

- `case_destructive_reset` —— `sec_iwdg` + `WDT_FLAG_RESET_CPU_CORE` 复位本 hart。
- `case_pmu_iwdg_full_reset` —— `pmu_iwdg` + `WDT_FLAG_RESET_SOC` **整芯片复位**：
  会连带复位另一核、全部外设与 bus 状态。若与其它 case 并发/相邻运行，会污染
  他人的 PC / 串口 / 外设状态，并让批量回归的中途序列语义不可解释。
  单独跑法：`tests/case_pmu_iwdg_full_reset/run_pmu_reset.sh`（构建 → flash 固化 →
  清 noinit → 复位启动 → 串口采集）。

## 2. 覆盖矩阵

### 2.1 sec_iwdg 基础 API（`case_non_destructive`，既有）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_wdt_invalid_timeout_min | `iwdt_ls_install_timeout` `window.max == 0` → -EINVAL | 异常参数 | ret == -EINVAL | API 契约 | ✅ 待实测 |
| test_wdt_invalid_flags | `iwdt_ls_install_timeout` `!HAL_IWDG_IS_MATCH` 分支 | 异常参数 | ret == -EINVAL | 驱动源码 + HAL `HAL_IWDG_IS_MATCH` | ✅ 待实测 |
| test_wdt_setup_then_feed | `iwdt_ls_setup` 正常使能 + `iwdt_ls_feed` | 功能通路 | install>=0 / setup==0 | API 契约 | ✅ 待实测 |
| test_wdt_disable | `iwdt_ls_disable`（CTRL=0 后不触发） | 功能通路 | setup==0；disable 后 `IWDT_CTRL == 0`（白盒回读）；1s 内无复位 | 驱动源码 `wdt_iwdg_ls.c:131-136` | ✅ 待实测 |
| test_wdt_two_timeouts_not_supported | `iwdt_ls_install_timeout` 二次安装 | 功能通路 | 两次均 `>= 0`；二次后 `IWDT_LOAD == 163840`（5000 ms，单通道覆盖） | 驱动源码 `wdt_iwdg_ls.c:122-126` + dts clock-frequency=32768 | ✅ 待实测 |

### 2.2 边界 / 异常参数 / 第二实例（`case_boundary`，本次新增，共 19 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_window_min_nonzero_rejected | `iwdt_ls_install_timeout` `window.min != 0` → -EINVAL | 边界长度 | ret == -EINVAL | 驱动源码 | ✅ 待实测 |
| test_window_min_equals_max_rejected | 同上（min == max != 0） | 边界长度 | ret == -EINVAL | 驱动源码 | ✅ 待实测 |
| test_window_max_zero_rejected | `iwdt_ls_install_timeout` `window.max == 0` → -EINVAL | 边界长度 | ret == -EINVAL | 驱动源码 | ✅ 待实测 |
| test_sec_iwdg_rejects_reset_soc_flag | `HAL_IWDG_IS_MATCH(SEC_IWDG, RESET_SOC)` → 0 | 异常参数 | ret == -EINVAL | 驱动源码 / HAL | ✅ 待实测 |
| test_pmu_iwdg_rejects_reset_cpu_core_flag | `HAL_IWDG_IS_MATCH(SEC_PMU_IWDG, RESET_CPU_CORE)` → 0 | 异常参数 | ret == -EINVAL | 驱动源码 / HAL | ✅ 待实测 |
| test_install_rejects_unmatched_flags | `HAL_IWDG_IS_MATCH` flags=0 与 0xFF 均不匹配 | 异常参数 | 两者 ret == -EINVAL | 驱动源码 / HAL | ✅ 待实测 |
| test_install_accepted_on_supported_flags | sec_iwdg←RESET_CPU_CORE / pmu_iwdg←RESET_SOC | 功能通路 | 两者 channel >= 0 | API 契约 | ✅ 待实测 |
| test_install_with_callback_accepted | `iwdt_ls_install_timeout` `callback != NULL` 分支（irq_config_func + data->callback 赋值） | 中断时序 | ret == 0 | 驱动源码 | ✅ 待实测 |
| test_install_without_callback_accepted | `iwdt_ls_install_timeout` `callback == NULL` 分支 | 中断时序 | ret == 0 | 驱动源码 | ✅ 待实测 |
| test_setup_without_timeout_enotsup | `iwdt_ls_setup` `!IWDT_LOAD` → -ENOTSUP | 异常参数 | ret == -ENOTSUP | 驱动源码 | ✅ 待实测 |
| test_setup_pause_in_sleep_enotsup | `iwdt_ls_setup` `WDT_OPT_PAUSE_IN_SLEEP` → -ENOTSUP | 异常参数 | ret == -ENOTSUP | 驱动源码 | ✅ 待实测 |
| test_setup_twice_ebusy | `iwdt_ls_setup` `IWDT_CTRL & (IWDT_EN\|IWDT_RST_EN) != 0` → -EBUSY | 异常参数 | 首次 0，二次 -EBUSY | 驱动源码 | ✅ 待实测 |
| test_setup_pause_halted_by_dbg_feed_disable | setup 使能 + `MSP_DEBUG(1)` 分支；feed / disable 返回 0 | 功能通路 | 三者均 == 0 | API 契约 | ✅ 待实测 |
| test_pmu_iwdg_lifecycle | pmu_iwdg：install(RESET_SOC) → setup → feed → disable | 功能通路 | 全链路 == 0 | API 契约 | ✅ 待实测 |
| test_load_ticks_min_timeout | `iwdt_calculate_load` 最小有效超时 | 边界长度 | max=1ms → IWDT_LOAD == 32 | 驱动公式 × dts clock-frequency=32768 | ✅ 待实测 |
| test_load_ticks_1000ms | 同上，常用值 | 边界长度 | max=1000ms → IWDT_LOAD == 32768 | 同上 | ✅ 待实测 |
| test_load_ticks_just_below_saturation | `iwdt_calculate_load` **未饱和区上界**（64 位乘法） | 边界长度 | max=131071999ms → IWDT_LOAD == **4294967263** | 驱动源码 `wdt_iwdg_ls.c:43-51` `(uint64_t)timeout_ms * frequency / 1000` + dts clock-frequency=32768 | ✅ 待实测 |
| test_load_ticks_saturation_boundary | `iwdt_calculate_load` **饱和分支**（`ticks > UINT32_MAX` → 钳到 `0xFFFFFFFF`） | 边界长度 | max=131072000ms → IWDT_LOAD == **0xFFFFFFFF** | 同上 | ✅ 待实测 |
| test_load_ticks_uint32_max_timeout | 同上，`window.max` 取 uint32 上限（深饱和区） | 边界长度 | max=4294967295ms → IWDT_LOAD == **0xFFFFFFFF** | 同上 | ✅ 待实测 |

> `IWDT_LOAD` 通过 `DT_REG_ADDR(DT_NODELABEL(sec_iwdg)) + 0x0` 读回：驱动
> `install_timeout` 只写不读，API 层无法取得换算结果，故该项为白盒断言
> （2026-09-18 起 `case_non_destructive` 的 `wdt_test.c` 亦用同一手法回读
> `IWDT_LOAD` / `IWDT_CTRL`，见 §4）。
> `test_setup_without_timeout_enotsup` 亦直接写 `IWDT_LOAD/CTRL` 复现"未安装超时"
> 前置态，使该项与套件内其它用例的执行顺序解耦。
>
> **换算语义（`wdt_iwdg_ls.c:43-51`，2026-09-18 修复后）**
>
> ```c
> uint64_t ticks = (uint64_t)timeout_ms * frequency / 1000;
> return (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;
> ```
>
> `(uint64_t)` 显式提升使 `timeout_ms * frequency` 在 **64 位**内运算 ⇒ 饱和判据
> `ticks > UINT32_MAX` **可达**：请求超时一旦使 `ticks` 越过 `UINT32_MAX`，即钳到
> `0xFFFFFFFF`（硬件寄存器最大值）。
>
> **饱和阈值 = 131072000 ms（≈ 36.4 h）** —— 使 `ticks` 首次超过 `UINT32_MAX` 的
> 整数毫秒值。⚠ **不要**与 **131072 ms** 混淆：后者是旧"32 位回绕起点"
> （= `2^32 / 32768`），二者**差 1000 倍**（档案 `DEFECT_ANALYSIS_2026-09-17.md §1.6`
> 已就地更正过这个错误）。
>
> | 输入 `window.max` | IWDT_LOAD（当前语义） | 备注 |
> |---|---|---|
> | 1 ms / 1000 ms | 32 / 32768 | 未饱和区 |
> | 131071999 ms | **4294967263** | 未饱和区上界（再 +1 ms 进饱和） |
> | 131072000 ms | **0xFFFFFFFF** | 饱和（钳到寄存器最大值） |
> | 4294967295 ms | **0xFFFFFFFF** | 深饱和区 |
>
> ⚠ **历史沿革（已作废的旧表述）**：修复前 `timeout_ms * frequency` 是两个
> `uint32_t` 先在 **32 位内回绕**、再赋给 `uint64_t`，饱和判据恒假（死分支），
> 实测值为 `4294934 / 0 / 4294934`。该缺陷（`wdt_iwdg_ls.c:45` 缺 `(uint64_t)` 提升）
> **已于 2026-09-18 修复**，本节 3 个饱和区用例同步改判为 64 位语义；
> 旧描述里的"死代码 / 不可达 / 未修 / 32 位回绕周期 131072 ms"**一并作废**。

### 2.3 复位路径（破坏性 case，单独运行）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| case_destructive_reset Phase 1 | sec_iwdg + `WDT_FLAG_RESET_CPU_CORE` → `soc_reset_reason.c` SEC_PER 分支 | 复位掉电 | `reset_reason_get() == SEC_IWDT_HART_RESET` | 驱动源码 + soc_reset.h | ✅ 待实测（既有） |
| case_pmu_iwdg_full_reset Phase 1 | pmu_iwdg + `WDT_FLAG_RESET_SOC` → `soc_reset_reason.c` SEC_PMU 分支 | 复位掉电 | `reset_reason_get() == SYS_IWDT_FULL_RESET` | 驱动源码 + soc_reset.h | ✅ 待实测（新增） |

存在第三方复位原因枚举（`PWR_FULL_RESET`/`SOFT_FULL_RESET`/`CPU_FULL_RESET`/
`EXT_FULL_RESET`/`EMUL_SOFT_RESET` 等）：本项目只覆盖与看门狗相关的两条，
其余**未覆盖**（见 §3）。

#### 2.3.1 这两个 case 怎样判定通过（三 Phase 状态机口径）

两个 case 都是**跨复位状态机**（独立 main，非 ztest），判定只看串口日志：

| Phase | 进入条件（noinit 状态） | 行为 | 裁决行 |
|-------|------------------------|------|--------|
| 0 | `*_test_phase == 0` | 装超时、不喂狗、置状态 = 1 | 仅失败路径打 `PROJECT EXECUTION: FAILED` |
| 1 | `*_test_phase == 1` | 回读 `reset_reason_get()` 与期望枚举比对 | PASS → `SUCCESSFUL`；FAIL → `FAILED` |
| 2 | 其它值（上一轮遗留的 2，或上电随机值） | **不打任何裁决行**；清 noinit（`*_test_phase = 0`、`*_phase0_passed = 0`）后按 Phase 0 重跑一轮完整流程 | 无 |

**通过判据**：同一份采集日志里出现 `PHASE 0` → `PHASE 1` → `PROJECT EXECUTION: SUCCESSFUL`
的**完整序列**。只见到 `SUCCESSFUL` 而序列不全（例如只有 `PHASE 2` 后跟 `SUCCESSFUL`）
不算通过 —— 这正是改前会出现的假 PASS。

**Phase 2 为何不能打 SUCCESSFUL**：`__noinit` 是 `(NOLOAD)` 段，GDB `load` 对它零写入，
`z_bss_zero()` 也不覆盖它 ⇒ 同一次上电会话内**第二次** load/run 必然读到上一轮留下的
`phase == 2`。若该分支照旧宣布成功，就得到"没装狗、没复位、没校验"的假 PASS，
且与真 PASS **交替出现**（第 1 轮真、第 2 轮假、第 3 轮真）。

**Phase 2 为何只"清状态 + 续跑"而不补判据**：Phase 2 这一次启动是 harness 的 `reset`
带来的，`reset_reason_get()` 反映**最近一次**复位原因（harness 复位，非 WDT），拿它当
判据会恒 FAIL；而**任何跨 load 保留的变量（含额外的 magic 字）都无法区分"上一轮真跑过"
与"上一轮没跑"**，加组合校验只是把同一个洞换个位置。故裁决权唯一归 Phase 1，
Phase 2 只负责让状态回到可跑 Phase 0。

> 预期副作用：重复手工运行时，第 2 轮会**真的**再走一遍 Phase 0 → 复位 → Phase 1，
> 多花一个 WDT 超时周期（约 2~3 s），而不是秒回一个假 SUCCESS。
> `run_pmu_reset.sh` / `run_destructive.sh` 的采集窗口（18 s）足以覆盖。

## 3. 未覆盖项（跳过的 case / 驱动分支）

> 口径：以下均为**未验证**条目，不是"判断该场景不会发生"。
> （对照 `OPEN_ITEMS.md` O-8 的教训：跳过 ≠ 验证过没问题。）

| 项 | 类型 | 未覆盖原因 | 需要什么才能做 |
|----|------|-----------|---------------|
| `app_iwdg_reset`（cpu2 侧 app_iwdg 复位并上报 `APP_IWDT_HART_RESET`） | 结构性 | `app_iwdg` 的复位原因由 **cpu2** 侧 `reset_reason_init()` 的 `SYSC_APP_PER` 分支产生，cpu1 读不到。要做需给 `wdt_test` 新增 `cpu2_*` 固件目录 → 工程分型由**单核变双核**（SPEC_REFERENCE §6.1.10），与既有 `case_non_destructive` 的 1os 分型（不得修改）冲突 | 用户裁定是否允许 `wdt_test` 转双核 |
| `app_iwdg` 的 `HAL_IWDG_IS_MATCH` / 生命周期 | 未覆盖 | 同上：`app_iwdg` 由 `lsqsh_evb_cpu2.dts` 使能，cpu1 板级未使能；从 cpu1 跨核操作 cpu2 外设属自造用法，未验证 | 同上的双核裁定 |
| `iwdt_ls_isr` 回调分支（`data->callback != NULL` 时调用回调） | 平台无法确定性判定 | 驱动 `setup` 恒置 `IWDT_RST_EN=1`，超时必走复位而非仅中断；回调是否在复位前被触发属硬件行为，无硬件无法确认。写死断言 = 假断言 | P6 实机 + 示波器/GDB 观测 |
| ~~`iwdt_calculate_load` 的 **饱和分支**（`ticks > UINT32_MAX`）~~ | ~~源码可证不可达~~ **已覆盖** | 2026-09-18 驱动修复为 64 位乘法后，该分支**可达**，由 `test_load_ticks_saturation_boundary` / `test_load_ticks_uint32_max_timeout` 覆盖（见 §2.2） | 无（已闭环） |
| `iwdt_calculate_load` 的换算主体（未饱和区 + 饱和区） | — | 已全覆盖（1 / 1000 / 131071999 / 131072000 / 0xFFFFFFFF ms） | — |
| `iwdt_init` `quick_enable` 分支（`quick_timeout_ms == 0` → -EINVAL；正常 quick 启动） | 未覆盖（**本工程内**） | 板级 dts 未设 `quick-enable`，且既有 `prj.conf` 不得修改（"只增不改"），本批不引入新配置。**已由新开的独立工程 `app_iwdg_cpu2_test` 在 cpu2 侧覆盖**（`case_quick_enable` / `case_quick_invalid`，本案新增工程） | 无需在本工程内做 |
| `iwdt_init` `CONFIG_WDT_DISABLE_AT_BOOT` 分支 | 未覆盖 | 既有 `prj.conf` 为 `=n`（不得修改）；改配置会影响既有 case | 单独 case + 自有 conf |
| `reset_reason_consistency`（独立 case：自造复位源校验上报一致性） | 无确定性判据 | 与 §2.3 两条破坏性 case 的复位原因断言重叠；独立 case 需自造复位源（`sys_reboot`），其映射到的枚举值（`SOFT_FULL_RESET` vs `SOFT_HART_RESET`）在无硬件条件下无法确认 | P6 实机确认映射后再决定是否立项 |
| 其余非看门狗复位原因枚举（PWR/SOFT/CPU/EXT/EMUL） | 未覆盖 | 不属 WDT 驱动职责范围；本工程只测看门狗触发的复位 | — |

## 4. `case_non_destructive` 的断言强化（2026-09-18 F1 波，已修）

> 背景：测试有效性审计发现 `case_non_destructive` 有 3 项"**不可能 FAIL**"的空壳
> （零断言或丢弃结果）。一个永远 PASS 的用例比没有更糟 —— 它给人虚假的安全感。
> 已按下表补上**源码可证**的判别性断言（逐项依据见 `wdt_test.c` 的行内注释与
> 文件头寄存器回读说明）。**未收紧任何契约**：每条新断言都对应驱动源码里
> 一个真实分支/不变式。

| 项 | 原病理 | 现判据 | 依据（驱动正确时必过 / 典型失效时必不过） |
|----|--------|--------|------------------------------------------|
| `test_wdt_invalid_flags` | 只有 `ARG_UNUSED(ret)`，**零断言** | `ret == -EINVAL` | `wdt_iwdg_ls.c:108-112` + `ls_msp_iwdg.c:72-81`：`0xFF` 两个合法 flags（1/2）皆非 ⇒ `HAL_IWDG_IS_MATCH` 返回 0。若驱动把未识别 flags 静默接受（返回 `>=0`）则必不过 |
| `test_wdt_disable` | 无"disable 生效"断言，仅靠"1s 内没复位"隐式判定（disable 未生效 + 超时窗口已错过时同样不会复位） | 新增：`wdt_disable()` 后回读 `IWDT_CTRL == 0`；保留 1s 无复位检查 | `wdt_iwdg_ls.c:131-136`：disable 即写 `IWDT_CTRL = 0x0` ⇒ 回读必为 0。若 disable 漏写或写错实例则必不过 |
| `test_wdt_two_timeouts_not_supported` | 只断 ch1，`ARG_UNUSED(ch2)` 丢弃第二路结果 | `ch2 >= 0` 且二次安装后 `IWDT_LOAD == 163840`（5000 ms 的 ticks，已覆盖第一次的 327680） | `wdt_iwdg_ls.c:122-126`：每次 install 无条件覆盖同一 `IWDT_LOAD` ⇒ 单通道覆盖语义。若驱动拒绝二次安装（返回负）或未覆盖装载值则必不过 |
| 两处 `wdt_disable` 收敛 | `case_non_destructive` 套件 teardown 只 disable，不清 `IWDT_LOAD` | **保留未改**：对既有用例无影响（`setup` 只要求 `LOAD != 0`） | — |

## 5. P6（首次实测）前置事项

1. **`app_iwdg` 是否需要**：若用户裁定 `wdt_test` 可转双核，再补 `cpu2_*` 固件 +
   `case_app_iwdg_reset`（见 §3）。
2. **mem 兜底 TEST_NAMES 未同步**：`tests/scripts/dump_results.py` 的 `TEST_NAMES`
   现仅含 `case_non_destructive` 的 5 项（`wdt` 套件链接序）。
   `case_boundary` 的 `ZTEST_SUITE` 名为 **`wdt_boundary`**，共 19 项
   （链接序见构建后 `nm --numeric-sort <elf> | grep _ztest_unit_test__wdt_boundary`）。
   在补上按 case 区分的 `TEST_NAMES` 之前，**不要**对 `case_boundary` 使用
   `dump_results.py` 内存兜底，否则会张冠李戴（KNOWN_ISSUES #59）。
   `tests/run_test.py`（回归入口）走串口 `parse_ztest`，不读 `TEST_NAMES`，不受影响。
3. **`case_pmu_iwdg_full_reset` / `case_destructive_reset` 单独跑**：先跑这两个，
   再跑批量回归（避免整芯片复位污染批量序列）。
4. **`run_pmu_reset.sh` 未实机校准**：`TGT_UART`（默认 COM16）、`DS_EXE` 路径
   首次运行时按实机确认。
5. **`cases.yaml` 登记推迟**：按路线图 §4「执行模式」裁定，入回归清单属 P6。
   本批新增 `case_boundary` 待 P6 与 `case_destructive_reset` 一并处置。
6. **两个破坏性 case 的 Phase 2 改动（未实机验证）**：改后 Phase 2 为"清状态并重跑"。
   P6 需按 §2.3.1 核对两件事 ——（a）首次跑（已清 noinit）为
   `PHASE 0 → PHASE 1 → SUCCESSFUL`；（b）**紧接着不清 noinit 再跑一次**为
   `PHASE 2 → PHASE 0 → PHASE 1 → SUCCESSFUL`，而不是旧行为那样 `PHASE 2 → SUCCESSFUL`。
   （b）即本次修复的验收点。
   （b）路径上还须看到 `channel >= 0` 与 `wdt_setup OK`；若出现 `wdt_setup failed`
   / `channel < 0`，说明 WDT 寄存器状态未被 harness 复位清掉，属新发现，需登记。
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
test_load_ticks_min_timeout | ticks == 32 | handcalc:1ms × 32768Hz ÷ 1000 = 32.768 → 截断 32（★无 INFO 通道 ⇒ NO_ACTUAL）
test_load_ticks_1000ms | ticks == 32768 | handcalc:1000 × 32768 ÷ 1000 = 32768（★无 INFO 通道 ⇒ NO_ACTUAL）
test_load_ticks_just_below_saturation | ticks == 4294967263 | handcalc:131071999 × 32.768 = 4294967263.2 → 截断（恰在饱和阈值下一格）（★无 INFO 通道 ⇒ NO_ACTUAL）
test_load_ticks_saturation_boundary | ticks == 4294967295 | handcalc:131072000 × 32.768 = 4294967296 → 溢出饱和到 0xFFFFFFFF（★无 INFO 通道 ⇒ NO_ACTUAL）
test_load_ticks_uint32_max_timeout | ticks == 4294967295 | handcalc:4.29e9 ms 远超饱和阈值 ⇒ 同样饱和（★无 INFO 通道 ⇒ NO_ACTUAL）
test_wdt_two_timeouts_not_supported | load == 163840 | handcalc:5000ms × 32768 ÷ 1000 = 163840（单通道覆盖后只留后者）（★无 INFO 通道 ⇒ NO_ACTUAL）
test_wdt_invalid_timeout_min | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_wdt_invalid_flags | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_wdt_disable | ctrl == 0 | spec:disable 后 IWDT_CTRL 必须为 0（白盒回读）（★无 INFO 通道 ⇒ NO_ACTUAL）
test_wdt_setup_then_feed | !none | !none:install>=0 / setup==0 / feed 成功 的合取，无单一标量
test_sec_iwdg_rejects_reset_soc_flag | !none | !none:-EINVAL，键名未在 INFO 中固定 ⇒ 不硬凑
test_pmu_iwdg_rejects_reset_cpu_core_flag | !none | !none:同上
test_install_rejects_unmatched_flags | !none | !none:同上
test_install_accepted_on_supported_flags | !none | !none:ret==0，键名未固定
test_install_with_callback_accepted | !none | !none:同上
test_install_without_callback_accepted | !none | !none:同上
test_setup_without_timeout_enotsup | !none | !none:-ENOTSUP，键名未固定
test_setup_pause_in_sleep_enotsup | !none | !none:同上
test_setup_twice_ebusy | !none | !none:-EBUSY，键名未固定
test_setup_pause_halted_by_dbg_feed_disable | !none | !none:多步寄存器读回的合取
test_pmu_iwdg_lifecycle | !none | !none:全生命周期多步合取
test_window_min_nonzero_rejected | !none | !none:-EINVAL，键名未固定
test_window_min_equals_max_rejected | !none | !none:同上
test_window_max_zero_rejected | !none | !none:同上
```
