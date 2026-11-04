# app_iwdg 测试覆盖矩阵（cpu2 侧）

> 被测驱动：`zephyr/drivers/watchdog/wdt_iwdg_ls.c`（binding `linkedsemi,ls-watchdog`）。
> 被测实例：**`app_iwdg`@0x40099000**（HART 复位域，cpu2 应用核；`lsqsh.dtsi:592-599`，
> 由 `lsqsh_evb_cpu2.dts` 使能，alias `watchdog0`）。
> **片内**外设，无引脚接线（SPEC_REFERENCE §6.6）。
> 同驱动的另两个实例 `sec_iwdg`(cpu1) / `pmu_iwdg`(cpu1) 由 `sec_iwdg_cpu1_test` 覆盖（单核 1os），
> 本案不重复——本工程存在的唯一理由就是 `app_iwdg` 只能从 cpu2 侧测
> （复位原因由 `soc_reset_reason.c` 的 `SYSC_APP_PER` 分支产生，cpu1 读不到）。
>
> 状态列口径：`✅ 待实测` = 用例已实现且编译通过，P6 首次实机回归后对表；
> `⬜ 未覆盖` = 该驱动路径/场景当前无用例触及（**未验证**，非"判断不会发生"）。

## 0. 工程分型与判定口径

| 项 | 值 | 依据 |
|---|---|---|
| 工程分型 | **双核（2os）** | SPEC_REFERENCE §6.1.10：分型按"是否存在 cpu1 之外的固件目录（`cpu0_*`/`cpu2_*`）"判；本工程有 `cpu2_controller_app_iwdg_test/` → 双核 |
| board（cpu1） | `lsqsh_evb@2os/lsqsh/cpu1` | 同上（双核行） |
| board（cpu2） | `lsqsh_evb@2os/lsqsh/cpu2` | 同上 |
| cpu1 入口 | `0x100c0000` | 同上（双核行） |
| 加载函数 | `build_test` / `load_and_run` | `scripts/test_common.sh`；双核**不写** cpu2 hold `0x4002200c` |
| 固件目录 | `cpu1_target_app_iwdg_test/`（常驻监督）+ `cpu2_controller_app_iwdg_test/`（被测核） | qsh-test-validate §1.1（双核必须有 `cpu1_*` 与第二核目录） |

**为什么不是"纯 cpu2 固件"分型**：SPEC_REFERENCE §6.1.10 只有"单核（只有 cpu1 侧固件）"
与"双核（有 cpu1 之外的固件目录）"两型，**没有"纯 cpu2"型**；且双核行的 board 记为
`lsqsh_evb@2os/lsqsh/{cpu1,cpu2}`——两侧都要出镜像（`load_and_run` 需要 target ELF +
controller bin 两份输入；单核路径反过来还要显式按住 cpu2 复位，说明 cpu1 才是引导核）。
故按规范走双核，不新造分型。

**判定口径（本项目专属，偏离 §6.2.1 通用双核判据）**：

```
case PASS = cpu2(CTRL)：ZT_BOOT + 至少 1 个测试项 + 全部项 PASS + PROJECT EXECUTION SUCCESSFUL
          且 cpu1(TGT)：ZT_BOOT + 全部项 PASS + PROJECT EXECUTION SUCCESSFUL
```

⚠ §6.2.1 的通用双核判据是"**同名**测试项两核各自 PASS"。本工程两核测试项结构上不可能
同名（cpu1 只有结构守卫项，`app_iwdg` 不存在于 cpu1 域），故与 `i3c_dualcore_test` 的
"双核例外"（§6.2.1 明确记载）同源。**该偏离需父会话确认**。

## 1. 用例清单与运行方式

| case | 固件源集 | case id | 破坏性 | 进批量回归 | 说明 |
|------|---------|---------|--------|-----------|------|
| `case_non_destructive` | `src/app_iwdg_test.c`（默认） | 0 | 否 | ✅ 可进（P6 登记） | app_iwdg API 通路 / 异常参数 / 边界 / 回调，16 项 |
| `case_quick_enable` | `src/app_iwdg_quick_test.c`（`CONFIG_APP_IWDG_TEST_CASE_ID=1`） | 1 | 否 | ✅ 可进（P6 登记） | `iwdt_init` quick-enable 正常分支，1 项 |
| `case_quick_invalid` | `src/app_iwdg_quick_invalid_test.c`（`=2`） | 2 | 否 | ✅ 可进（P6 登记） | `quick-timeout-ms=0` 拒绝分支，1 项 |
| `case_reset` | `src/app_iwdg_reset_test.c`（`=3`，自定义 main） | 3 | **是**（app_iwdg HART 复位 cpu2） | ❌ **不可** | 复位原因回读，状态机（非 ztest） |

case 变体只体现在 `tests/case_*/controller.conf` + `controller.overlay`（§6.1.8），
四个 case 共用同一份固件源码。

## 2. 覆盖矩阵

### 2.1 `case_non_destructive` —— app_iwdg 非破坏性通路（cpu2，16 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_install_accepts_reset_cpu_core_flag | `iwdt_ls_install_timeout` 正常路径；`HAL_IWDG_IS_MATCH(APP_IWDG, RESET_CPU_CORE)` == 1 | 功能通路 | ret == 0 | 驱动源码 + `ls_msp_iwdg.c:74-76` | ✅ 待实测 |
| test_install_rejects_reset_soc_flag | `iwdt_ls_install_timeout` `!HAL_IWDG_IS_MATCH` 分支（APP_IWDG 对 RESET_SOC 返回 0） | 异常参数 | ret == -EINVAL | 驱动源码 + `ls_msp_iwdg.c:77-79` | ✅ 待实测 |
| test_install_rejects_unmatched_flags | 同上，flags=0(NONE) 与 0xFF 两次 | 异常参数 | 两者 ret == -EINVAL | 驱动源码 + `ls_msp_iwdg.c:80` | ✅ 待实测 |
| test_install_invalid_window_max_zero | `iwdt_ls_install_timeout` `window.max == 0` → -EINVAL | 边界长度 | ret == -EINVAL | 驱动源码 `:100-103` | ✅ 待实测 |
| test_install_invalid_window_min_nonzero | `iwdt_ls_install_timeout` `window.min != 0` → -EINVAL | 边界长度 | ret == -EINVAL | 驱动源码 `:100-103` | ✅ 待实测 |
| test_install_with_callback_accepted | `iwdt_ls_install_timeout` `callback != NULL` 分支（irq_config_func + data->callback 赋值）+ 随后置空分支 | 中断时序 | 两次 ret == 0 | 驱动源码 `:112-117` | ✅ 待实测 |
| test_install_without_callback_accepted | `iwdt_ls_install_timeout` `callback == NULL` 分支 | 中断时序 | ret == 0 | 驱动源码 `:115-117` | ✅ 待实测 |
| test_setup_enables_ctrl_rst_en | `iwdt_ls_setup` 正常路径 + CTRL 位域白盒断言 | 功能通路 | ret == 0；IWDT_CTRL == 0x3（RST_EN\|EN）；disable 后 == 0 | 驱动源码 `:90` + `reg_iwdgv2_type.h` | ✅ 待实测 |
| test_setup_without_timeout_enotsup | `iwdt_ls_setup` `!IWDT_LOAD` → -ENOTSUP | 异常参数 | ret == -ENOTSUP | 驱动源码 `:68-71` | ✅ 待实测 |
| test_setup_pause_in_sleep_enotsup | `iwdt_ls_setup` `WDT_OPT_PAUSE_IN_SLEEP` → -ENOTSUP（且 CTRL 保持 0） | 异常参数 | ret == -ENOTSUP；CTRL == 0 | 驱动源码 `:78-81` | ✅ 待实测 |
| test_setup_twice_ebusy | `iwdt_ls_setup` `IWDT_CTRL != 0` → -EBUSY | 异常参数 | 首次 0，二次 -EBUSY | 驱动源码 `:73-76` | ✅ 待实测 |
| test_feed_within_window_no_reset | `iwdt_ls_feed` 主路径（CNT_CLR + INTR_CLR）；1200ms > 1000ms 超时窗口 | 功能通路 | 6 次 feed 全 == 0；CTRL 仍 == 0x3（未复位） | 驱动源码 `:50-61` | ✅ 待实测 |
| test_load_ticks_min_timeout | `iwdt_calculate_load` 最小有效超时 | 边界长度 | max=1ms → IWDT_LOAD == 32 | 驱动公式 `:43-48` × dts `clock-frequency=32768` | ✅ 待实测 |
| test_load_ticks_1000ms | 同上，常用值 | 边界长度 | max=1000ms → IWDT_LOAD == 32768 | 同上 | ✅ 待实测 |
| test_load_ticks_65535ms | 同上，商截断 | 边界长度 | max=65535ms → IWDT_LOAD == 2147450 | 同上（65535*32768/1000 = 2147450.88） | ✅ 待实测 |
| test_load_ticks_max_no_wrap | 同上，未饱和区上界 | 边界长度 | max=131071ms → IWDT_LOAD == 4294934 | 同上（131071*32768/1000 = 4294934.5 → 截断；远未到饱和阈值 131072000 ms） | ✅ 待实测 |

> `IWDT_LOAD` 通过 `DT_REG_ADDR(DT_NODELABEL(app_iwdg)) + 0x0` 读回（字偏移 0，
> 见 `reg_iwdgv2_type.h`）：驱动 `install_timeout` 只写不读，API 层取不到换算结果，
> 故换算类为白盒断言。`test_setup_without_timeout_enotsup` 亦直接写 `IWDT_LOAD/CTRL`
> 复现"未安装超时"前置态，使该项与套件内其它用例的执行顺序解耦。

### 2.2 `case_quick_enable` —— iwdt_init quick 正常分支（cpu2，1 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_quick_enable_boot_state_feed_disable | `iwdt_init` `quick_enable && quick_timeout_ms > 0` 分支 | 功能通路 | IWDT_LOAD == 1966080；IWDT_CTRL == 0x3；feed == 0；disable == 0 且 CTRL == 0 | 驱动源码 `:172-184` × overlay `quick-timeout-ms = <60000>` | ✅ 待实测 |

> 单个测试项内部固定顺序（观测 → feed → disable），避免依赖 ztest 链接序
> （SORT_BY_NAME，§10.27）——启动态只能在任何 `install_timeout` 之前观测
> （`install_timeout` 会写 CTRL=0）。

### 2.3 `case_quick_invalid` —— quick 参数拒绝分支（cpu2，1 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_quick_timeout_zero_leaves_device_unready | `iwdt_init` `quick_timeout_ms == 0` → LOG_ERR + return -EINVAL（`wdt_iwdg_ls.c:175-179`） | 异常参数 | ①`device_is_ready(app_iwdg) == false`；②**正控**（2026-09-18 补）：节点 `okay` + `CONFIG_WATCHDOG=y` + DT `quick-enable` 存在且 `quick-timeout-ms == 0`；③`dev->state->initialized == true`（init 确已调用，`init.c:311-326` 无条件置位）；④`dev->state->init_res == EINVAL(22)` —— `iwdt_init` 只有 `-ENODEV`/时钟使能错误/`-EINVAL` 三条返回路径，正值 errno 等于 EINVAL **唯一**指向 quick 参数拒绝分支 | 驱动源码 `:175-179` + `:152-169` × overlay `quick-timeout-ms = <0>` + `init.c:311-326`（init_res 正值 errno 语义） | ✅ 待实测 |

> **为什么补正控（F5 波，2026-09-18）**：原实现唯一断言是
> `zassert_false(device_is_ready(dev))` —— 设备未就绪的**任何**原因
> （节点 disabled / 驱动未编入 / 无关 init 失败 / overlay 没生效 / 根本没跑到）
> 都会让它 PASS，属"不能证伪的空壳"。现按 **前置条件 → init 已执行 →
> 失败原因精确匹配** 三级收窄，只有"quick-timeout-ms=0 被驱动拒绝"这一条
> 路径能同时满足全部断言。未走"同一 case 内构造 quick-timeout-ms>0 对照"的
> 原因：加第二设备实例需改 overlay，且会把本 case 的"单设备拒绝"语义复杂化；
> `case_quick_enable`（`quick-timeout-ms=60000`）已是现成的正面对照 case。

### 2.4 `case_reset` —— 复位路径（破坏性，单独运行）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| case_reset Phase 0→1 | app_iwdg + `WDT_FLAG_RESET_CPU_CORE` 超时 → `soc_reset_reason.c` **SYSC_APP_PER 分支**（`:122-131`） | 复位掉电 | `reset_reason_get() == APP_IWDT_HART_RESET` | 驱动源码 + soc_reset.h + soc_reset_reason.c | ✅ 待实测 |

判定标记（uart5）：`APP_IWDG_RESET_PASS` + `PROJECT EXECUTION SUCCESSFUL`。

#### 2.4.1 怎样判定 `case_reset` 通过（三 Phase 状态机口径）

本 case 是**跨复位状态机**（独立 main，非 ztest），判定只看 uart5 日志：

| Phase | 进入条件（noinit 状态） | 行为 | 判定标记 |
|-------|------------------------|------|----------|
| 0 | `test_phase == 0` | install(2000 ms, `RESET_CPU_CORE`) + setup，不喂狗，置状态 = 1 | 仅失败路径打 `PROJECT EXECUTION FAILED` |
| 1 | `test_phase == 1` | 回读 `reset_reason_get()` 与 `APP_IWDT_HART_RESET` 比对 | PASS → `APP_IWDG_RESET_PASS` + `PROJECT EXECUTION SUCCESSFUL`；FAIL → `APP_IWDG_RESET_FAIL` + `FAILED` |
| 2 | 其它值（上一轮遗留的 2，或上电随机值） | **不打任何判定标记**；清 noinit（`test_phase = 0`、`phase0_passed = 0`）后按 Phase 0 重跑一轮完整流程 | 无 |

**通过判据（`run_reset.sh` 现口径即此）**：同一次采集里 `APP_IWDG_RESET_PASS`
**且** `PROJECT EXECUTION SUCCESSFUL`，并且序列完整 ——
`PHASE 0` → (HART 复位) → `PHASE 1` → PASS 标记。只见到 Pass 标记而前面没有
`PHASE 0`/`PHASE 1`（例如只有 `PHASE 2` 后跟 SUCCESSFUL）不算通过。

**Phase 2 为何不能打 SUCCESSFUL**：`__noinit` 是 `(NOLOAD)` 段，GDB `load` 对它零写入，
`z_bss_zero()` 也不覆盖它 ⇒ 同一次上电会话内**第二次** load/run 必然读到上一轮留下的
`test_phase == 2`。若该分支照旧宣布成功，就得到"没装狗、没复位、没校验"的假 PASS，
且与真 PASS **交替出现**（第 1 轮真、第 2 轮假、第 3 轮真）。

**Phase 2 为何只"清状态 + 续跑"而不补判据**：Phase 2 这一次启动是 harness 的 `reset`
（或 flash 重新引导）带来的，`reset_reason_get()` 反映**最近一次**复位原因（非 app_iwdg
超时），拿它当判据会恒 FAIL；而**任何跨 load 保留的变量（含额外的 magic 字）都无法区分
"上一轮真跑过"与"上一轮没跑"**，加组合校验只是把同一个洞换个位置。故判定权唯一归
Phase 1，Phase 2 只负责让状态回到可跑 Phase 0。

> 预期副作用：重复运行时，第 2 轮会**真的**再走一遍 Phase 0 → HART 复位 → Phase 1，
> 多花一个超时周期（约 2~3 s），而不是秒回一个假 SUCCESS。`run_reset.sh` 的
> 20 s 采集窗口足以覆盖。

### 2.5 cpu1 侧（常驻监督核，结构守卫，2 项）

| 测试项 | 被测对象 | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|---------|------|----------|----------|------|
| test_cpu1_does_not_own_app_iwdg | cpu1 板级 dts（非驱动行为） | — | `DT_NODE_HAS_STATUS(DT_NODELABEL(app_iwdg), okay) == 0` | `lsqsh_evb_cpu1.dts`（未使能 app_iwdg） | ✅ 待实测 |
| test_cpu1_reset_reason_not_app_iwdt | `reset_reason_init` cpu1 分支（`:81-120`） | 复位掉电 | `reset_reason_get() != APP_IWDT_HART_RESET` | `soc_reset_reason.c` 编译期分支 | ✅ 待实测 |

> cpu1 侧**无被测对象**（`app_iwdg` 是 cpu2 外设），本套件锁定的是"本案必须拆成独立
> cpu2 工程"的结构事实，防后续有人把 app_iwdg 用例塞回单核工程（roadmap §4 通用规则）。
> 其项**不是行为验证**，等第 2.1~2.4 节。

## 3. 未覆盖项（跳过项清单）

> 口径：以下均为**未验证**条目，不是"判断该场景不会发生"（对照 `OPEN_ITEMS.md` O-8 教训）。
> **判据类型**列区分了"平台不可注入（已实测）"/"源码可证"/"寄存器语义未确认"/"范围划分"四类。

| 项 | 判据类型 | 未覆盖原因 | 需要什么才能做 |
|----|---------|-----------|---------------|
| ~~`iwdt_calculate_load` 饱和分支（`ticks > UINT32_MAX`）~~ | ~~**源码可证不可达**（非实测）~~ → **2026-09-18 已修：可达且已覆盖** | 2026-09-18 驱动加 `(uint64_t)` 提升（`wdt_iwdg_ls.c:43-51`），饱和判据**可达**：超限即钳到 `0xFFFFFFFF`。**该分支已由 cpu1 侧 `sec_iwdg_cpu1_test/case_boundary` 的 `test_load_ticks_saturation_boundary` / `test_load_ticks_uint32_max_timeout` 覆盖**（详见 `sec_iwdg_cpu1_test/tests/COVERAGE.md` §2.2）；本案（cpu2 / app_iwdg）不重复立项 —— 见 §3.2 评估 | 无（已闭环） |
| 回绕区间（`window.max` ≥ 131072 ms）的确定性预期 | **已由驱动修复消除歧义**（2026-09-18） | 修后语义按 64 位算式是否越 `UINT32_MAX` 分段：`< 131072000 ms` 为精确值，`>= 131072000 ms` 钳到 `0xFFFFFFFF`。⚠ 旧表述"驱动意图与实现不符 / 回绕起点 131072 ms"**已作废** —— 131072 ms 与 **131072000 ms** 差 1000 倍（饱和阈值，≈36.4 h；`DEFECT_ANALYSIS_2026-09-17.md §1.6` 已就地更正）。本案 cpu2 侧仍只在未饱和区间写断言，饱和区间由 `sec_iwdg_cpu1_test` 覆盖 | 无（已闭环） |
| `iwdt_ls_isr` 回调实际被调用 | **平台不可确定性判定**（未实测） | 驱动 `setup` 恒置 `IWDT_RST_EN=1`（`:90`），超时必走复位而非仅中断；回调是否在 HART 复位前被调用属硬件行为，无判据可写（写死断言 = 假断言） | P6 实机 + GDB 断点/示波器观测 |
| `IWDT_CNT` 计数方向与 `feed` 的重装载语义 | **寄存器语义未确认** | `reg_iwdgv2_type.h` 只有 `IWDT_CNT` 字段无行为说明；数据手册未在手。按 §1.9h「边界常量必须有实测/源码出处」，不凭猜写断言（如"feed 后 CNT 回到 LOAD"可能方向相反） | 数据手册，或 P6 实机写-回读实测确立语义 |
| 非 app_iwdg 实例（`sec_iwdg` / `pmu_iwdg`）的本驱动路径 | **范围划分** | 同驱动的另两实例在 cpu1 域，已由 `sec_iwdg_cpu1_test` 覆盖（单核 1os）；本案存在理由只是 app_iwdg 只能从 cpu2 测 | 无（不重复造） |
| `reset_reason` 其它枚举（PWR/SOFT/CPU/EXT/EMUL/SEC_IWDT/SEC_WWDT/SOFT_HART/APP_WWDT） | **范围划分** | 非 app_iwdg 触发路径；且 `lsqsh.dtsi` 无 app_wwdt 实例（WWDT 侧无 Zephyr 驱动，桶 C） | 无（不属本案范围） |
| `reset_reason_app_set/get`（`CONFIG_NUM_OS>1` 的 app 复位原因跨核传递） | **范围划分** | 属"cpu1 主动复位并跨核告知"的复位联动场景，需 mbox 类跨核通道，不在 app_iwdg 单外设范围 | 若需覆盖，另立跨核复位用例 |

### 3.1 跨工程发现（**未修改既有工程**，仅登记）

| 发现 | 位置 | 影响 |
|---|---|---|
| ~~驱动 `iwdt_calculate_load` 的 `timeout_ms * frequency` 是**两个 `uint32_t` 相乘**（32 位先回绕），使饱和判据 `ticks > UINT32_MAX` 成为**死分支**~~ → **2026-09-18 已修** | `zephyr/drivers/watchdog/wdt_iwdg_ls.c:43-51`（现为 `(uint64_t)timeout_ms * frequency / 1000`，饱和判据可达） | `OPEN_ITEMS.md` **O-15** 与 `KNOWN_ISSUES.md` **#74** 均已更新为**已修**；`sec_iwdg_cpu1_test/case_boundary` 的 3 项换算已同步改判为 64 位语义 |
| `sec_iwdg_cpu1_test/case_boundary` 3 项换算预期的**终态** | `projects/sec_iwdg_cpu1_test/cpu1_sec_iwdg_test/src/wdt_boundary_test.c:50-55`、`:371-435` | **2026-09-18 终态**：驱动修复为 64 位乘法后改判为 `131071999→4294967263`、`131072000→0xFFFFFFFF`、`0xFFFFFFFF→0xFFFFFFFF`，现为**预期 PASS 且语义正确**。历史：2026-09-17 曾按 32 位回绕值（`4294934 / 0 / 4294934`）改判过一次 —— 那次只与**当时的实现**一致（PASS ≠ 驱动正确），驱动修复后**已作废** |

### 3.2 饱和分支可测性评估（2026-09-18，**结论：无需在 cpu2 侧补项**）

> 驱动修复后，`iwdt_calculate_load` 的饱和分支由"死代码"变为**可测**。任务要求评估
> 是否在 `app_iwdg_cpu2_test` 补 1~2 项。结论与理由：

| 问题 | 结论 |
|------|------|
| 分支现在可测吗？ | **可测**。修后 `(uint64_t)timeout_ms * 32768 / 1000 > UINT32_MAX` 在 `window.max >= 131072000 ms` 时为真 ⇒ `IWDT_LOAD` 被钳到 `0xFFFFFFFF`；回读 `app_iwdg` 的 `IWDT_LOAD`（偏移 0x0）即可断言 |
| 是否应在本案补项？ | **不补**。`iwdt_calculate_load()` 是**纯函数**（入参只有 `timeout_ms` 与实例的 `clock_frequency`），与实例无关；该分支已由 cpu1 侧 `sec_iwdg_cpu1_test/case_boundary` 的两项覆盖（两实例 clock-frequency 同为 32768）。在本案再写一遍只是**同代码路径的重复覆盖**，不新增判别力，反而增加 P6 的实测与维护面 |
| 若父会话仍要 cpu2 侧独立覆盖 | 可行且成本低：加 1 项 `install_timeout(max=131072000)` 后回读 `app_iwdg` 的 `IWDT_LOAD == 0xFFFFFFFF`（白盒回读，同 `wdt_boundary_test.c` 手法）。**本轮未做**（无裁定 + 属重复覆盖） |
| 未饱和区是否已覆盖？ | 已由本案既有 `case_quick_enable`（quick 启动换算）与 cpu1 侧 `sec_iwdg_cpu1_test` 覆盖 |

## 4. ⚠ 不可进批量回归的 case

| case | 原因 | 单独跑法 |
|------|------|---------|
| `case_reset` | app_iwdg 超时触发 **cpu2 HART 复位**（真实复位本核）。若与其它 case 相邻/并发运行，会污染后续 case 的 PC / 外设状态与批量序列语义；且它必须走 **flash 固化**（复位后固件需从 flash 重新引导，RAM-load 镜像不会自行重启），不能走 `load_and_run` | `bash tests/case_reset/run_reset.sh`（构建 → 一次会话烧两核 → 清 noinit → 复位启动 → 采集 uart5）。⚠ 脚本未实机校准，见其头注释 4 项 P6 待核对 |

其余 3 个 case 非破坏性、可进 `regression/cases.yaml`（按 roadmap §4「执行模式」，
**登记推迟到 P6**，本阶段不改 `cases.yaml`）。

## 5. P6（首次实机）前置事项

1. **`case_reset` 的 flash 固化流程需校准**：`run_reset.sh` 按 SPEC_REFERENCE §6.7 编写，
   但 CPU1 侧是否需换 `@2os_xip` 变体（§6.7.4 的 XIP 标准组合）、`CTRL_UART` 实机端口、
   DebugServer 路径、noinit 符号在 flash 引导路径下的地址，均**未实机确认**。
2. **判定口径待父会话确认**：§0 的"cpu2 全项 + cpu1 全项"判据偏离 §6.2.1 通用双核判据
   （两核测试项不同名），需确认是否接受该例外。
3. **`dump_results.py` 的 TEST_NAMES**：已按构建后 `nm --numeric-sort` 的链接序回填
   三个 ztest case；`case_reset` 不在表内（脚本对该 case 直接报错退出，避免假 PASS）。
   后续增删测试项必须重新核对（§10.27）。
4. **串口**：cpu1 = uart1（CH342），cpu2 = uart5（MCP2200）；双核 run 脚本要求显式
   `--ctrl-uart/--tgt-uart`（§6.4.5）。
5. **`regression/cases.yaml` 登记**：按 roadmap §4「执行模式」，P6 统一做。
6. **`case_reset` 的 Phase 2 改动（未实机验证）**：改后 Phase 2 为"清状态并重跑"。
   P6 需按 §2.4.1 核对两件事 ——（a）首次跑（已清 noinit）为
   `PHASE 0 → PHASE 1 → APP_IWDG_RESET_PASS`；（b）**紧接着不清 noinit 再跑一次**为
   `PHASE 2 → PHASE 0 → PHASE 1 → APP_IWDG_RESET_PASS`，而不是旧行为那样
   `PHASE 2 → PROJECT EXECUTION SUCCESSFUL`。（b）即本次修复的验收点。
   （b）路径上还须看到 `wdt_install_timeout` 成功与 `wdt_setup` 成功（无 `ERROR:`
   行）；若出现 `wdt_setup failed` / `install_timeout failed`，说明 app_iwdg 寄存器
   状态未被复位清掉，属新发现，需登记。

---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
>
> ★ 键名：本表用**不带核前缀**的项名，`common/expect_check.py` 会自动剥掉报告里的
> `cpu2::` / `cpu1::` 前缀再匹配（同一项在两核上适用同一条预期）。
> ★ `IWDT_LOAD` 黄金值全部由 `ms × 32768Hz ÷ 1000` **手算截断**得来（DTS
> `clock-frequency=32768` 是独立输入）；若只断言"写进去了"，驱动算错公式
> （漏除 1000 / 用错时钟）照样 PASS。

```expect
# 测试项 | 断言 | 来源标签:说明
test_load_ticks_min_timeout | ms == 1; ticks == 32 | handcalc:1ms × 32768Hz ÷ 1000 = 32.768 → 截断 32
test_load_ticks_1000ms | ms == 1000; ticks == 32768 | handcalc:1000 × 32768 ÷ 1000 = 32768
test_load_ticks_65535ms | ms == 65535; ticks == 2147450 | handcalc:65535 × 32768 ÷ 1000 = 2147450.88 → 截断
test_load_ticks_max_no_wrap | ms == 131071; ticks == 4294934 | handcalc:131071 × 32768 ÷ 1000 = 4294934.53 → 截断
test_quick_enable_boot_state_feed_disable | ms == 60000; load == 1966080; ctrl == 0x3 | handcalc:60000 × 32768 ÷ 1000 = 1966080（overlay quick-timeout-ms=60000）
test_setup_enables_ctrl_rst_en | ctrl == 0x3 | spec:IWDT_CTRL 位定义 RST_EN(bit0) 与 EN(bit1) 同置 ⇒ 使能态恰为 0x3
test_quick_timeout_zero_leaves_device_unready | ready == 0 | spec:quick-timeout-ms=0 ⇒ 初始化失败 ⇒ device_is_ready 必为假
test_feed_within_window_no_reset | timeout_ms == 1000; elapsed_ms > 1000 | contract:喂狗窗口必须真的跨过一个看门狗周期，否则测不到"窗口内喂狗不减计数"
test_cpu1_reset_reason_not_app_iwdt | reason != 9 | spec:APP_IWDT_HART_RESET 在 soc_reset.h enum 中序号为 9；cpu1 侧不应出现该原因（★否定式断言，判别力天然弱）
test_cpu1_does_not_own_app_iwdg | !none | !none:编译期常量断言（DT 节点 status），运行期无可观测量
test_install_accepts_reset_cpu_core_flag | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
test_install_rejects_reset_soc_flag | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_install_rejects_unmatched_flags | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_install_invalid_window_max_zero | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_install_invalid_window_min_nonzero | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_install_with_callback_accepted | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
test_install_without_callback_accepted | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
test_setup_without_timeout_enotsup | ret == -134 | contract:API 契约 -ENOTSUP（★无 INFO 通道 ⇒ NO_ACTUAL）
test_setup_pause_in_sleep_enotsup | ret == -134 | contract:API 契约 -ENOTSUP（★无 INFO 通道 ⇒ NO_ACTUAL）
test_setup_twice_ebusy | ret == -16 | contract:API 契约 -EBUSY（★无 INFO 通道 ⇒ NO_ACTUAL）
case_reset Phase 0→1 | !none | !none:证据是 `PHASE` 打印（非 INFO token），预期由 CPU 侧固件比对，本检查器不可读
```
