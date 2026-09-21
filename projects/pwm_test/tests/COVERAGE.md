# pwm_test 测试覆盖矩阵

> ⚠️ **波形正确性未验证 —— 需用户用示波器 / 逻辑分析仪确认。**
>
> 本板无示波器/逻辑分析仪，本工程**只验 `zephyr/drivers/pwm/pwm_ls.c` 的接口可用性**
> （API 层契约：配置 / 使能 / 极性 / 频率查询 / 周期与脉宽寄存器回读）。
> 断言全部落在**返回值契约**与**被写寄存器的回读值**上，**不构成对输出波形（频率、
> 占空比、极性、电平、引脚映射）的任何结论**。波形是否与配置相符，须由用户用示波器/
> 逻辑分析仪测量 PA04 / PG02 / PH05 后确认。（用户裁定，roadmap §4 P5）

工程定位：**片内 PWM 控制器 `pwm@40082000`（compatible `linkedsemi,ls-pwm`）接口可用性测试**。
被测对象 = `zephyr/drivers/pwm/pwm_ls.c`（`ls_pwm_init` / `ls_pwm_set_cycles` /
`ls_pwm_get_cycles_per_sec`）。
驱动路径列写被触发的函数 + 分支；判定 = 返回码契约 + 寄存器回读值 + ztest 逐项 PASS
+ `PROJECT EXECUTION SUCCESSFUL`。

**环境（SPEC_REFERENCE §6.6）**：**片内，无外部接线**（PWM 输出引脚空载，不接任何对端）。
本工程**无外部路径测试项** ⇒ 无需向用户确认接线（§6.6.1 的"无"分支）。
**单核工程**：board `lsqsh_evb@1os/lsqsh/cpu1`，入口 `0x10000000`
（`SINGLE_BOARD` / `build_single_test` / `load_and_run_single`，SPEC_REFERENCE §6.1.10）。

## 1. 被测硬件事实（dtsi / binding）

| 项 | 值 | 来源 |
|---|---|---|
| 节点 | `pwm: pwm@40082000`，label `pwm` | `zephyr/dts/riscv/linkedsemi/lsqsh.dtsi:313-321` |
| compatible | `linkedsemi,ls-pwm` | 同上 |
| 通道数 | **16**（驱动 `CHAN_CONT`，合法通道 0..15） | `zephyr/drivers/pwm/pwm_ls.c:28-32` + `reg_pwm_type.h`：`CONFIG_PWM_LS_V2` 下 `CHAN_CONT=16`；实测 `.config` `CONFIG_PWM_LS_V2=y` |
| `reg` | `<0x40082000 0x100>` | dtsi |
| `clock-frequency` | `PBUS4_CLOCK` = 75 MHz | `lsqsh.dtsi:15,318` |
| `clocks` | `<PWM_CLOCK>` → 实解出 `&cctl_sysc_app_per 0x0 0x10 0x11` | dtsi + 生成 dts |
| `resets` | `<PWM_RESET>` → 实解出 `&rctl_sysc_app_per 0x8c000010` | dtsi + 生成 dts |
| binding 必填属性 | `reg` / `clock-frequency` / **`prescaler`** / **`pinctrl-0`** / **`pinctrl-names`** / `clocks` / `#pwm-cells` | `zephyr/dts/bindings/pwm/linkedsemi,ls-pwm.yaml` |
| 板级默认状态 | **`status = "disabled"`**；`lsqsh_evb_cpu1.dts:214-219` 里的 `&pwm` 使能块**是注释态** | dtsi / 板级 dts |

**overlay 改了什么**（`cpu1_pwm_test/app.overlay`，本工程自有，未改板级文件）：

1. `&pwm { status = "okay"; }` —— 使能控制器（板级为注释态）。
2. `prescaler = <3>` —— binding 必填。分频 4 ⇒ `get_cycles_per_sec` = 75 MHz / 4 = **18750000**。
3. `pinctrl-0 = <&pwm1_pa04 &pwm7_pg02 &fan_pwm10_ph05>` + `pinctrl-names = "default"` —— binding 必填；
   三条引脚**逐字沿用板级注释块里给出的同一组**（`lsqsh_evb_cpu1.dts:217`），是板卡自带
   pinctrl 定义（`lsqsh_evb_pinctrl.dtsi:214,225` + `lsqsh_pinctrl_fixed_pinmap.dtsi:477`），非本工程新造。
4. `chosen { zephyr,console = &uart1; ... }`（单核 overlay 必备，validate §1.9b）。
5. `#include "../../../common/gpio_toggle.dtsi"`（复用收敛，SPEC_REFERENCE §6.1.9）。

**引脚占用核对**（避免与其他已使能外设争同脚）：

| 引脚 | 板上其他占用 | 结论 |
|---|---|---|
| PA04 (`pwm1_pa04`) | 无 | 可用 |
| PG02 (`pwm7_pg02`) | `cap7_rx_pg02` 存在，但 `cap` 节点 dtsi 为 `status="disabled"` 且无人置 okay ⇒ 其 pinctrl 不应用 | 可用 |
| PH05 (`fan_pwm10_ph05`) | 另有 `pwm2_ph05` / `pwm4_ph05`（同脚不同 func/信号），三者的 pinctrl **只应用被引用的**，本工程只引 `fan_pwm10_ph05` | 可用 |

## 2. case 与固件开关

单一 case，共用一份固件；无 CMake 开关（单核单 case）。

| case | 固件 | 测试套件 | 测试项数 |
|---|---|---|---|
| `case_default` | `cpu1_pwm_test/` | `pwm` | **12** |

`test.sh` 走 `test_common.sh` 的 `SINGLE_BOARD`（1os）+ `load_and_run_single`，
board 与加载入口同源（validate §1.9f / KNOWN_ISSUES #57）。

## 3. 覆盖矩阵

回读方式说明：Zephyr PWM API **没有**"读回周期/脉宽"的接口，本工程回读直接读被写寄存器
（`PWM_PARAM[ch].PWM_CYC` / `PWM_HIGH`、`PWM_CTRL[ch]` 的 POL 位），寄存器偏移见
`modules/hal/linkedsemi/hal_driver/inc/reg_pwm_type.h` 的 `reg_pwm_t`
（`PWM_EN@0x00` / `PWM_PRE_DIV@0x04` / `PWM_CTRL@0x20` / `PWM_PARAM@0x60`，每通道 16 字节；POL = PWM_CTRL bit3）。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| `init_path_readback` | `ls_pwm_init`：`clock_control_off`→`reset_line_toggle`→`clock_control_on`→`pinctrl_apply_state`→`PWM_CTRL[i]=0`(i<16)→`PWM_PRE_DIV=prescaler`→`INTR_CLR`；`ls_pwm_get_cycles_per_sec` 主路径 | 复位/掉电 + 功能通路 | `device_is_ready`==true；`PWM_PRE_DIV` 回读==DT `prescaler`(3)；`cycles_per_sec`==75000000/(3+1)==18750000 | dtsi `clock-frequency`/`prescaler` + `pwm_ls.c:84,139` | ⏳ |
| `set_cycles_readback` | `ls_pwm_set_cycles` 成功路径：`PWM_CTRL[ch] &= ~PWM_POL_MASK`（NORMAL 分支）→ 写 `PWM_CYC` / `PWM_HIGH` → `PWM_EN \|= BIT(ch)` | 功能通路 | ret==0；`PWM_CYC`==1000；`PWM_HIGH`==400；`PWM_CTRL[0] & POL`==0 | `pwm_ls.c:56-75` | ⏳ |
| `polarity_flags` | `ls_pwm_set_cycles` 的 `flags & PWM_POLARITY_INVERTED` **置位分支**（`\|= PWM_POL_MASK`）与**清除分支**（`&= ~PWM_POL_MASK`） | 功能通路 | INVERTED→POL 位==1 且 `PWM_CYC/HIGH` 不变；随后 NORMAL→POL 位==0 | `pwm_ls.c:66-70` | ⏳ |
| `cycles_per_sec_contract` | `ls_pwm_get_cycles_per_sec` 主路径，ch∈{0,7,15} | 功能通路 | 三通道均为 18750000 | `pwm_ls.c:84` + DT | ⏳ |
| `ns_api_path` | pwm.h `pwm_set` 的 ns→cycles 换算（L545-560）→ `pwm_set_cycles` → `ls_pwm_set_cycles` | 功能通路 | 1,000,000 ns / 250,000 ns @18.75 MHz → `PWM_CYC`==18750、`PWM_HIGH`==4687 | 换算公式 `(period*cps)/NSEC_PER_SEC`（pwm.h L552-558） | ⏳ |
| `dt_spec_path` | pwm.h `pwm_set_dt` / `pwm_set_pulse_dt` → `pwm_set` → `ls_pwm_set_cycles` | 功能通路 | `pwm_set_dt(2000us,500us)`→37500/9375；`pwm_set_pulse_dt(125us)` 用 `spec.period`(1000us)→`PWM_CYC`==18750、`PWM_HIGH`==2343 | 同上 | ⏳ |
| `invalid_channel` | `ls_pwm_set_cycles` 的 `channel >= CHAN_CONT` 分支；`ls_pwm_get_cycles_per_sec` 的同名分支 | 异常参数（通道上下界） | ch∈{16,99,UINT32_MAX} 两 API 均 `-EINVAL`；**±1 边界合法侧 ch15 必须被接受**（ret==0） | `pwm_ls.c:53-55,81-83` + `CONFIG_PWM_LS_V2=y`(CHAN_CONT=16) | ⏳ |
| `invalid_period_pulse` | pwm.h 包装层 `pulse > period` 分支（L490）；`ls_pwm_set_cycles` 的 `(pulse==0)\|\|(period==0)` 分支 | 异常参数 | 三种非法组合均 `-EINVAL`；且**拒绝后 `PWM_CYC`/`PWM_HIGH` 保持原值**（拒绝发生在任何寄存器写入之前） | `pwm.h:490` + `pwm_ls.c:59-61` | ⏳ |
| `cycle_boundary` | `ls_pwm_set_cycles` 的 `period>0xFFFFu \|\| pulse>0xFFFFu` 分支（合法侧 + 越界侧）+ 包装层 `pulse>period` | 边界长度（16 位量程） | (1,1)→ret 0 且回读 1/1；(0xFFFF,0xFFFF)→ret 0 且回读 0xFFFF/0xFFFF；(0x10000,0x10000)→`-EINVAL` 且 `PWM_CYC` 仍 0xFFFF；(0xFFFF,0x10000)→`-EINVAL` | `pwm_ls.c:62-64`（驱动掩码常量直接取自源码） | ⏳ |
| `channel_isolation` | `ls_pwm_set_cycles` 的 per-channel 寄存器寻址（`PWM_PARAM[channel]` / `PWM_CTRL[channel]`），ch∈{2,3,4} 各自写入不同三元组 | 功能通路（多通道隔离） | 三通道回读各自等于自身配置，无串扰 | `pwm_ls.c:67-72` | ⏳ |
| `concurrent_same_channel` | `ls_pwm_set_cycles` 的 `k_spin_lock(&data->pwm_spinlock)` 临界区（同一通道"周期+脉宽+极性"三写必须原子） | 并发 | 两线程各 100 轮写同一 ch6 的不同三元组，join 后 `PWM_CYC`/`PWM_HIGH` 必须**恰好等于其中一个完整三元组**，不得出现撕裂组合 | `pwm_ls.c:65-74`（spinlock 覆盖全部寄存器写） | ⏳ |
| `no_disable_api` | 契约边界：`pwm_driver_api` 仅挂 `set_cycles`/`get_cycles_per_sec`（无 disable/stop）；`ls_pwm_set_cycles` 对 `period==0` / `pulse==0` 均拒绝 | 功能通路 + 契约边界 | 两次"以零值当关闭"的调用均 `-EINVAL`，且 `PWM_CYC` 保持原值；`PWM_EN` 原始值仅经 `INFO` 上报，**不作判据** | `pwm_ls.c:59-61,144-147` | ⏳ |

**状态列**：⏳ = 已生成 + 编译通过，**尚未烧录实测**（roadmap §4「执行模式」：P1–P5 只生成不测，
首次实测统一推迟到 P6）。实测后按回归结果改为 ✅。

## 4. 驱动分支可达性（逐分支扫描结论）

`ls_pwm_set_cycles` 的 5 个条件分支：

| # | 分支 | 是否被覆盖 | 说明 |
|---|------|-----------|------|
| B1 | `channel >= CHAN_CONT` → `-EINVAL` | ✅ `invalid_channel` | 两个 API 的同名分支都测到 |
| B2 | `pulse_cycles > period_cycles` → `-EINVAL` | ⚠️ **经公共 API 不可达** | pwm.h 包装层 `z_impl_pwm_set_cycles` 先在 L490 返回 `-EINVAL`，驱动该分支被短路。属驱动的**防御性冗余校验**，无独立入口可触发 ⇒ 见 §5 跳过清单 |
| B3 | `(pulse==0) \|\| (period==0)` → `-EINVAL` | ✅ `invalid_period_pulse` / `no_disable_api` | `pulse==0` 由本分支命中（包装层放行）；`period==0` 时包装层先命中 B2 的条件，两者返回值同为 `-EINVAL` |
| B4 | `period>0xFFFF \|\| pulse>0xFFFF` → `-EINVAL` | ✅ `cycle_boundary` | 用 `(0x10000,0x10000)` 使包装层放行、直达驱动分支 |
| B5 | 极性置位 / 清除两分支 | ✅ `polarity_flags` | 两分支各测一次 |

`ls_pwm_get_cycles_per_sec`：主路径 ✅（`cycles_per_sec_contract`）、`channel>=CHAN_CONT` ✅（`invalid_channel`）。

`ls_pwm_init`：全部子步骤由 `init_path_readback` 的 `device_is_ready` + `PWM_PRE_DIV` 回读覆盖
（`clock_control_off/on`、`reset_line_toggle`、`pinctrl_apply_state`、`PWM_CTRL` 清零、`PWM_PRE_DIV` 写入）。
注：`pwm_ls.c` 无 ISR、无回调、无 DMA 路径，故"中断时序"维度不适用。

## 5. 未覆盖 / 已知缺口（跳过项清单）

| 跳过项 | 判据类型 | 原因 |
|--------|---------|------|
| `pwm_disable`（候选清单项） | **API 不存在** | Zephyr 4.0 的 `pwm_driver_api` 无 disable/stop 入口（`pwm.h:437-441` 只有 `set_cycles`/`get_cycles_per_sec`），`pwm_ls.c` 也未实现任何关闭通路；候选清单里的 `pwm_pin_set_cycles` 同属此类 —— 该 API 在 Zephyr 4.0 **已被移除**（`pwm.h` 全树 grep 无此符号），其现代等价物 `pwm_set_dt`/`pwm_set_pulse_dt` 已由 `dt_spec_path` 覆盖。**契约边界已用 `no_disable_api` 记录**（零周期不能当关闭手段） |
| 波形正确性（频率 / 占空比 / 极性 / 电平 / 引脚映射） | **缺测量仪器** | 无示波器/逻辑分析仪。本工程全部断言只到"寄存器被正确写入"与"返回码契约"，**不构成波形结论**。见本文件顶部备注 |
| 驱动 `pulse_cycles > period_cycles` 分支（§4 B2） | **经公共 API 不可达** | pwm.h 包装层先返回 `-EINVAL`（`pwm.h:490`），驱动该分支被短路；无独立入口可触发。属防御性冗余校验，非缺陷 |
| `PWM_EN` 使能位**回读**断言 | **回读语义无文档** | `PWM_EN` 为 W1S/W1C 语义：使能写 `PWM_EN \|= BIT(ch)`、关闭写 `PWM_EN \|= BIT(ch+16)`（HAL `ls_hal_pwm.c` 的 `HAL_PWM_Start`/`HAL_PWM_Stop`），低半字回读值语义未文档化，无法建立确定判据 ⇒ 使能契约改以 `pwm_set_cycles` 返回码判，`PWM_EN` 原始值仅在 `no_disable_api` 的 `INFO` 行上报参考 |
| `pwm_set_cycles(NULL, ...)` 的 NULL dev | **会 fault，不可断言** | Zephyr PWM API 不做 `dev` 空指针校验，`z_impl_*` 直接解引用 `dev->api` ⇒ 必然 load access fault（同 adc_tester 的先例）。属框架契约（调用方保证），无有效判据 |
| DT `pwms` 属性解析（`PWM_DT_SPEC_GET`） | **需新增 DT 节点** | 本板无 `pwm-leds` 或其他 `pwms` 消费者节点，且 `zephyr,user` binding 在本树不存在 ⇒ 需自造节点/绑定。`pwm_set_dt`/`pwm_set_pulse_dt` **本体**已由 `dt_spec_path` 用手工构造的 `struct pwm_dt_spec` 覆盖；未覆盖的只是 DT 属性解析这一段 |
| 通道 12..15 之外的寄存器回读（`PWM_PARAM[12..15]` ≳ 0x100） | **DT reg 尺寸未覆盖** | 见 §6 驱动观察 2。`invalid_channel` 的 ±1 边界合法侧仍用 ch15（驱动声明其合法），因 `PWM_PARAM[15]` 落在 0x40082150，仍在 0x40082400（下一个外设 CAP）之前，不会误写其他外设 |

## 6. 驱动观察（只报不改；上游 `zephyr/` 未改动）

> 纪律：本工程**不修改** `zephyr/drivers/pwm/pwm_ls.c` 及任何上游文件，以下仅为生成期读码发现，
> 供后续裁定与登记。

1. **`CONFIG_RESET=n` 时该驱动编译失败（配置耦合缺陷）**：`ls_pwm_config` 的 `reset` 成员声明带
   `IF_ENABLED(CONFIG_RESET, (...))` 守卫，而实例初始化项
   `IF_ENABLED(DT_INST_NODE_HAS_PROP(n, resets), (.reset = RESET_DT_SPEC_INST_GET(n), ))`
   **未加同样守卫**（`pwm_ls.c:44 vs :158`）。lsqsh 的 `pwm` 节点带 `resets` 属性 ⇒
   `CONFIG_RESET=n` 时会为不存在的成员做指定初始化 → 编译失败。
   本工程 prj.conf 显式 `CONFIG_RESET=y` 规避（run-time 走 `reset_line_toggle` 路径，已实测 init 成功）。
2. **DT `reg` 尺寸 0x100 未覆盖 V2 的 16 通道寄存器区**：`reg_pwm_t` 在 `CHAN_CONT=16` 下
   `PWM_PARAM` 阵列跨 0x60..0x15F（每通道 16 字节），超出 dtsi 声明的 `<0x40082000 0x100>`；
   `ls_pwm_init` 的 `PWM_CTRL[i]=0`(i<16) 与 ch≥10 的 `set_cycles` 都会写到声明区之外。
   实际硬件按 HAL 结构体布局应到 0x160，且 0x40082150 < 下一个外设 CAP@0x40082400，
   不会误伤其他 IP。**未擅自改 dtsi/HAL**。
3. **`PWM_EN` 用 `|=` 做读-改-写**（`pwm_ls.c:73`）：对 W1S/W1C 语义寄存器做 RMW，
   若回读值包含高半字的"关闭"位则可能把别通道的关闭位一并重写。本次未观察到触发条件
   （驱动无关闭通路），仅在 `no_disable_api` 的 `INFO` 行上报原始值留档。
4. **`ls_pwm_set_cycles` 无时钟/频率一致性校验**：`period_cycles` 直接取 16 位掩码，
   调用方无法从 API 层得知实际可表达的最低频率（prescaler 3 时分频 4 ⇒ 最低约 286 Hz）。
   属可接受设计，仅记录。

## 7. 配置核对记录（P6 实测前的自检依据）

| 项 | 实测值（生成的 `.config`） | 含义 |
|---|---|---|
| `CONFIG_PWM` / `CONFIG_PWM_LS` | `y` / `y` | 驱动编入 |
| `CONFIG_PWM_LS_V2` | **`y`** | 驱动 `CHAN_CONT` = **16** ⇒ `invalid_channel` 的 ch15/ch16 ±1 边界成立 |
| `CONFIG_CLOCK_CONTROL` / `CONFIG_RESET` / `CONFIG_PINCTRL` | `y` / `y` / `y` | init 三条通路全部编入 |
| `CONFIG_RESET_LINKEDSEMI` | `y` | `rctl_sysc_app_per` 复位控制器驱动就位 |
| `CONFIG_PICOLIBC` | `y`（`NEWLIB_LIBC=n`、`MINIMAL_LIBC=n`） | libc 三件套（KNOWN_ISSUES #58） |
| `CONFIG_ZTEST_VERBOSE_OUTPUT` | `y` | 逐项 START/PASS/FAIL 输出不被抑制（§10.26） |
| 生成 dts 中 `pwm@40082000` | `status="okay"`，`prescaler=<0x3>`，`clock-frequency=<0x47868c0>`(75 MHz)，`pinctrl-0=<&pwm1_pa04 &pwm7_pg02 &fan_pwm10_ph05>` | overlay 生效 |

**ztest 链接序**（`nm --numeric-sort`，与 `dump_results.py` 的 `TEST_NAMES_BY_CASE["case_default"]`
逐项对齐，2026-09-17 实测）：`channel_isolation` → `concurrent_same_channel` → `cycle_boundary`
→ `cycles_per_sec_contract` → `dt_spec_path` → `init_path_readback` → `invalid_channel`
→ `invalid_period_pulse` → `no_disable_api` → `ns_api_path` → `polarity_flags` → `set_cycles_readback`。

## 参考

- 驱动源码：`zephyr/drivers/pwm/pwm_ls.c`、`zephyr/include/zephyr/drivers/pwm.h`
- 寄存器布局：`modules/hal/linkedsemi/hal_driver/inc/reg_pwm_type.h`、`.../src/ls_hal_pwm.c`
- DT binding：`zephyr/dts/bindings/pwm/linkedsemi,ls-pwm.yaml`
- 覆盖维度与模板：`810_driver_test/SPEC_REFERENCE.md` §6.5
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
init_path_readback | PWM_PRE_DIV == 3; cycles_per_sec == 18750000 | spec:DT `prescaler`(3) 与 `clock-frequency`(75MHz)/(3+1)（与驱动实现无关，强预期；★无 INFO 通道 ⇒ NO_ACTUAL）
cycles_per_sec_contract | cycles_per_sec == 18750000 | spec:75000000/(prescaler+1)=18750000（★无 INFO 通道 ⇒ NO_ACTUAL）
set_cycles_readback | PWM_CYC == 1000; PWM_HIGH == 400 | driversrc:回读值转录自驱动写入（★无 INFO 通道 ⇒ NO_ACTUAL）
polarity_flags | !none | !none:POL 位翻转 + CYC/HIGH 不变的合取
ns_api_path | !none | !none:换算公式结果，键名未在 INFO 中固定 ⇒ 不硬凑
dt_spec_path | !none | !none:同上
invalid_channel | !none | !none:三种非法通道 + ch15 合法侧的合取
invalid_period_pulse | !none | !none:三种非法组合 + 寄存器保持原值的合取
cycle_boundary | !none | !none:三档边界的合取
channel_isolation | !none | !none:三通道各自回读等于自身配置，非标量
concurrent_same_channel | !none | !none:不得出现撕裂组合（禁止性质）
no_disable_api | !none | !none:两次零值调用 -EINVAL + PWM_EN 仅上报不判，非标量
```
