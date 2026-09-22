# 810_driver_test 缺陷定位分析（2026-09-17）

> **本轮范围**：只定位、不修。**未改动任何代码**（`zephyr/` / `modules/` / 任何测试工程均只读）。
> **执行方式**：由单个 agent 完成全部 5 项分析，未派子 agent。
> **证据分级**：`实测`（构建产物 / host 复现 / 运行输出） > `源码推导`（逐行复核） > `推测`。
> 凡"判断该场景不会发生"与"验证过没问题"分开表述，不混写（`OPEN_ITEMS.md` O-8 教训）。

---

## 0. 总表（缺陷 × 严重度 × 影响实例 × 能否实测复现）

| # | 位置（复核后） | 函数 | 严重度 | 影响实例 / 调用方 | 本轮证据等级 | 能否实测复现 |
|---|---|---|---|---|---|---|
| 1 | `zephyr/drivers/watchdog/wdt_iwdg_ls.c:45`（函数 `:43-48`） | `iwdt_calculate_load()` | **P0**（静默提前复位） | `app_iwdg` / `sec_iwdg` / `pmu_iwdg` 三实例共用；调用点 `:121`(install_timeout)、`:178`(init quick_enable) | **实测**（host 复现）+ 源码推导 | **可**，且已复现（host + 工程内 boundary case 已按回绕语义编码） |
| 2 | `modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.c:884`（`spif_add_cmd`，函数 `:854`）与 `:938`（`spif_add_cmd_with_dummy`，函数 `:906`）；分支体 `:879-887` / `:932-941` | `spif_add_cmd()` / `spif_add_cmd_with_dummy()` | **P0**（表外野写 / 跨 IP 寄存器污染） | `spif1..4` 四实例；`spif1` 落点 = **RTC 寄存器块** | 源码推导 + **实测**（偏移算术 host 复现、SPIF 基址与 picolibc 常量实测） | **不可**（会野写，禁止构造触发用例）；只能静态/构建产物验证 |
| 3 | `zephyr/drivers/mdio/mdio_dwmac.c:274-276`（`mdio_dwmac_init()`，函数 `:265`） | `mdio_dwmac_init()` 丢弃 `mdio_dwmac_ethphy_reset()` 返回值 | **P1**（错误掩盖 / 误导性归因） | `mdio1`/`eth1`（所有带 eth1 工程）；本板门为真 | **实测**（构建产物 `.i` 证明 `.port != NULL`）+ 源码推导 | **可**（配置可达性已实测确认）；失败注入未做 |
| 4 | `zephyr/drivers/sdhc/sdhci-of-linkedsemi.c:475-478`（`default: return -ENOTSUP;`）锁于 `:422`、解锁于 `:505` | `linkedsemi_sdhci_request()` | **P0**（mutex 永久泄漏 → 永久阻塞 + 次生 coredump 丢弃） | eMMC 栈；`g_sdhci_busy` 次生效应波及 coredump 后端 | 源码推导（**上一轮子 agent，父会话转达**）；无实测 | 不可（需触发非法 opcode + data≠NULL 的真实请求） |
| 5 | `zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c:156-162` 与 `:166-174`（`phy_rt_rtl8211f_reset()`，函数 `:115-177`） | `phy_rt_rtl8211f_reset()` 两处 `do{read}while` 无超时 | **P0**（POST_KERNEL 挂死 → 串口 0 行） | 所有带 eth1 工程；本板生效的是 `:166-174` | **实测**（构建产物 `.i` 确认两处 while + reset_gpio 非 NULL）+ 源码推导 | **可**（gdb 读 PC 落点）；本轮未做实机复现 |

---

## 1. 缺陷 #1 —— watchdog 换算 32 位回绕（`wdt_iwdg_ls.c:45`）

### 1.1 精确位置（已逐行复核）

- 文件：`d:/zephyr4_0/zephyr/drivers/watchdog/wdt_iwdg_ls.c`
- 函数定义：`:43-48` `static uint32_t iwdt_calculate_load(uint32_t timeout_ms, uint32_t frequency)`
- **缺陷行：`:45`**  `uint64_t ticks = timeout_ms * frequency / 1000;`
- 饱和判据（死代码）：`:47` `return (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;`
- 调用点：
  - `:121` `uint32_t ticks = iwdt_calculate_load(cfg->window.max, config->hclk_hz);`（`iwdt_ls_install_timeout`）
  - `:178` `uint32_t ticks = iwdt_calculate_load(cfg->quick_timeout_ms, cfg->hclk_hz);`（`iwdt_init` 的 quick-enable 分支）
- 题面行号 `:45` **准确**。

### 1.2 根因（C 语义层）

`timeout_ms` 与 `frequency` 均为 `uint32_t`。按 C 的 **usual arithmetic conversions**，`timeout_ms * frequency` 在 **`uint32_t` 域内**求值（mod 2^32），**先回绕**，结果才被转换为 `uint64_t` 赋给 `ticks`。因此：

- `ticks` 的实际上界 = `0xFFFFFFFF / 1000` = **4 294 967**；
- 饱和判据 `ticks > UINT32_MAX`（4 294 967 295）**恒假** → `:47` 的饱和分支是**死代码**；
- 回绕周期：`timeout_ms * 32768 ≡ 2^32` 时归零 ⇒ **131072 ms**（`2^32 / 32768`）。

频率 32768 的来源（DT 事实）：`zephyr/dts/riscv/linkedsemi/lsqsh.dtsi` 三个 watchdog 节点均 `clock-frequency = <32768>`（`sec_iwdg:590`、`app_iwdg:599`、`pmu_iwdg:607`），驱动取 `DT_INST_PROP(inst, clock_frequency)`（`wdt_iwdg_ls.c:207`）。

### 1.3 触发条件

`timeout_ms ≥ 131072` ms（`frequency = 32768` 时）。即：任一路径传入 `cfg->window.max ≥ 131072` 或 `quick_timeout_ms ≥ 131072`。

### 1.4 影响面 / 爆炸半径

- **三实例全中**：`iwdt_calculate_load()` 是 `wdt_iwdg_ls.c` 内部静态函数，被 `DT_INST_FOREACH_STATUS_OKAY(IWDT_LS_DEFINE)`（`:215`）为**每一个 `linkedsemi,ls-watchdog` 节点**生成的设备共用 ⇒ `app_iwdg` / `sec_iwdg` / `pmu_iwdg` **共享同一实现**。
- 覆盖这些实例的测试工程：`wdt_test`（`sec_iwdg` + `pmu_iwdg`，见其 `app.overlay:13` 与 `wdt_boundary_test.c:67-68`）、`app_iwdg_cpu2_test`（`app_iwdg`，见其 `cpu2_controller_app_iwdg_test/app.overlay:10`）。
- 语义后果：请求 ≥131072 ms 的超时，得到的是一个**远小于请求**的 load ⇒ 看门狗**远早于预期**触发复位（安全性方向是"提前复位"而非"不复位"，但对安全域 WDT 而言是**完全错误的超时语义**，且使 `quick_enable` 的"快速启动"预期失真）。

### 1.5 证据（分级）

**实测（host 复现，本轮）**：把 `:43-48` 原文逐字复制，用 clang/gcc 编译运行（源码见 `/tmp/defect_repro.c` 的 `iwdt_calculate_load_BUG` / `_INTENDED`），`frequency = 32768`：

```
timeout_ms   BUG(32b mul)   INTENDED(64b)  verdict
131071999    4294934        4294967263     *** MISMATCH ***
131072000    0              4294967295     *** MISMATCH ***
4294967295   4294934        4294967295     *** MISMATCH ***
1000         32768          32768          same
5000         163840         163840         same

saturation condition (ticks > UINT32_MAX) with 32-bit product:
  max possible ticks after wrap = 0xFFFFFFFF/1000 = 4294967
  -> condition is ALWAYS FALSE (dead code)
wrap period = 2^32/32768 = 131072 ms
```

**实测（工程内既有，交叉印证）**：`projects/wdt_test/cpu1_wdt_test/src/wdt_boundary_test.c:49-54` 已把该回绕语义写成确定性预期值（`TICKS_131071999MS 4294934u` / `TICKS_131072000MS 0u` / `TICKS_UINT32_MAX_MS 4294934u`），注释明确指出"按当前实际行为（32 位回绕）手算……驱动若修为 64 位乘法，本节回绕区三项须改判"。与本次 host 复现**逐值一致**。

**源码推导**：`OPEN_ITEMS.md` O-15、`KNOWN_ISSUES.md` #74 已记录同一结论。

### 1.6 修复建议（本轮不实施）

- **改 `:45`**：`uint64_t ticks = (uint64_t)timeout_ms * frequency / 1000;`
- **改后必须同步改判的用例**（`projects/wdt_test`，3 项）：
  | 用例 | 现值（回绕语义） | 修后应改判 |
  |---|---|---|
  | `wdt_boundary::test_load_ticks_just_below_saturation`（`:376`） | 4294934 | **4294967263** |
  | `wdt_boundary::test_load_ticks_saturation_boundary`（`:399`） | 0 | **0xFFFFFFFF** |
  | `wdt_boundary::test_load_ticks_uint32_max_timeout`（`:422`） | 4294934 | **0xFFFFFFFF** |
  同时更新 `wdt_boundary_test.c:49-54` 的字面量与注释。
  `app_iwdg_cpu2_test` 只覆盖不回绕区 ⇒ **无需改判**。
- 注意：修后仍存在**语义上限**——请求 **>≈131072000 ms（≈36.4 h）** 会被饱和到 `0xFFFFFFFF`（即 ≈131072 s），这是"饱和到硬件最大值"的**本意行为**，不是缺陷；测试注释应说明这一点。
  > ⚠️ **2026-09-18 数值更正**：本行原写 `>≈131072 ms`，**把"回绕起点"与"饱和阈值"混为一谈**。二者差 1000 倍：
  > - **回绕起点** = `131072 ms`（`2^32 / 32768` = 131072 个 tick ⇒ `ticks` 归零），从此点起换算即失真
  > - **饱和阈值** = `UINT32_MAX / 32.768` ≈ **131072000 ms**（≈36.4 h），到此后才饱和到 `0xFFFFFFFF`
  > 由修复实施方在改判时实测发现并指出。§1.3 的"触发条件 ≥131072 ms"是**回绕起点**，本身无误。

### 1.7 验证方法

- host：已复现（本报告 §1.5）。
- 实机：对 `sec_iwdg` 用 `window.max = 131072000` 装 timeout，读 `IWDT_LOAD`（实例寄存器块偏移 0）——修前应为 **0**、修后应为 **0xFFFFFFFF**；或用 `window.max = 131200` 观察实际复位时间是否 ≈131072 ms。
- 判定纪律：`test_load_ticks_*` 三项的"预期 PASS"是**对当前缺陷语义的编码**，不是"换算正确"。修复后这三项**必须改判**，否则会把修复判成 FAIL。

---

## 2. 缺陷 #2 —— `spif_add_cmd*` 过期索引致表外野写（P0）

### 2.1 精确位置（已逐行复核）

- 文件：`d:/zephyr4_0/modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.c`
- `int spif_add_cmd(const struct device *dev, uint8_t cmd)` —— 函数 `:854-904`
  - 过期索引来源（第一个扫描循环，失败即 `break`）：`:866-877`（`idx = spif_get_cmd_slot(dev, cmd, off);` `:867`）
  - **缺陷行：`:884`** —— 固定表命中分支体内 `sys_write32(spif_cmd.value, table_base + idx * 4);`（分支体 `:879-887`）
- `int spif_add_cmd_with_dummy(const struct device *dev, uint8_t cmd, uint8_t dummy_cycle)` —— 函数 `:906-958`
  - 过期索引来源：`:918-930`
  - **缺陷行：`:938`** —— 分支体 `:932-941`

> 行号更正：题面给的 `:934-942` 尾行偏 1；实际 `spif_add_cmd_with_dummy` 的固定表分支体为 **`:932-941`**（`for` 在 `:932`，写址在 `:938`，`goto end` 在 `:939`，右花括号 `:941`）。`:879-887` 段**准确**。

辅助函数（复核用）：`spif_get_cmd_slot()` `:356-371`（未命中 `return -ENOSR` `:370`）；`spif_get_empty_cmd_slot()` `:322-337`；常量 `SPIF_FIXED_CMD_TABLE_NUM=30` / `SPIF_CMD_TABLE_NUM=70`（`spi_filter.h:17-19`）。

### 2.2 根因（C 语义 + 控制流）

- 第一个循环 `for (off = 0; off < SPIF_CMD_TABLE_NUM; off++) { idx = spif_get_cmd_slot(...); if (idx>=0) {...goto end;} else break; }` 在 **`off = 0` 即失败**时立刻 `break`，**`idx` 被留在 `spif_get_cmd_slot()` 的失败返回值 `-ENOSR`**。
- 随后的固定表分支用 `table_base + idx * 4` 计算写地址 —— 用的是这个**过期的负值**，而正确索引是**循环变量 `off`**（固定表槽位号，∈[0,30)）。
- ⇒ `sys_write32(spif_cmd.value, table_base + (-ENOSR) * 4)` —— **命令表基址之下的表外寄存器写**。同时"使能该固定槽（`EN=1`）"的**原始意图完全失效**。

**关键澄清（本轮复核更正了我自己的初判）**：该分支**并非死代码，是可触达的**。理由：
- `linkedsemi_spi_filter_init()`（`:1769-1788`）只调用 `spif_fixed_cmd_tab_restore()`（`:1780`），而该函数在**冷启动**（硬件固定表全 0）时**只把 DT 默认值拷进 SW 镜像 `dev_data->fixed_cmd_tab[]`**（`:1761-1766`），**不写硬件表**；
- 硬件固定表要等 `linkedsemi_spi_filter_cold_reset()`（`:1629-1726`）里的 `:1680-1687` 才写入；
- 因此在 cold_reset 之前调用 `spif_add_cmd(dev, <DT 默认固定命令>)`：第一个循环在硬件表（全 0）中查不到 ⇒ `idx = -ENOSR`；固定表分支在 **SW 镜像**中却能命中 ⇒ **野写**。这正是"传一个 DT 默认命令值"的自然写法（如参考资产 `spif_add_cmd(spifilter, 0x03)`）。

### 2.3 触发条件（必须同时满足）

1. 传入的 `cmd` **不在硬件命令表**（`spif_get_cmd_slot()` 返回 `-ENOSR`）——典型：**boot 后、cold_reset 前**调用；
2. 该 `cmd` **在 SW 镜像 `dev_data->fixed_cmd_tab[]`** 中（即 DT 默认固定命令）。

### 2.4 影响面 / 爆炸半径

写地址 = `table_base + (-ENOSR) * 4`，其中 `table_base = dev_base + SPIF_CMD_BASE`，`SPIF_CMD_BASE = SPIF_PRG_CMD = 0x2C`（`reg_spi_filter.h:19`、`:106`）。

本项目 libc = **picolibc**（`projects/spif_test/cpu1_spif_test/prj.conf` 明确 `CONFIG_PICOLIBC=y` / `NEWLIB_LIB=n` / `MINIMAL_LIBC=n`；810 CLAUDE.md §4.2 "libc 三件套"），其 `ENOSR = 63`（`zephyr-sdk-0.17.0/arm-zephyr-eabi/picolibc/include/sys/errno.h:120`；Zephyr minimal/armstdc 亦为 63）。故 **`idx = -63`，偏移 = -252 字节**（`-0xFC`）。

SPIF 四实例 `reg = <base 0x2000>`（各 8 KB）：`spif1@0x40070000` / `spif2@0x40072000` / `spif3@0x40074000` / `spif4@0x40076000`（`modules/lib/pfr_tpm/include/lsqsh_pinctrl_pfr_tpm.dtsi:55-90`）。野写落点：

| 实例 | `dev_base` | 野写地址 | 落点 |
|---|---|---|---|
| spif1 | 0x40070000 | **0x4006FF30** | **RTC 寄存器块**（`rtc@4006fc00`, reg 0x400 ⇒ 0x4006FC00..0x4006FFFF，偏移 0x330） |
| spif2 | 0x40072000 | 0x40071F30 | 落在 **spif1** 块内（偏移 0x1F30） |
| spif3 | 0x40074000 | 0x40073F30 | 落在 **spif2** 块内 |
| spif4 | 0x40076000 | 0x40075F30 | 落在 **spif3** 块内 |

⇒ **跨 IP 寄存器污染**：相邻 SPIF 实例互相踩寄存器；`spif1` 直接踩 **RTC**。该文件另有累计 19 项问题（见 `projects/spif_test/tests/COVERAGE.md`「上游缺陷观察」#1-#19），本项为 **#1（P0）**。

### 2.5 证据（分级）

- **源码推导（本轮逐行复核）**：位置、控制流、`idx` 取值、正确索引应为 `off`。
- **实测（构建产物 / 常量，本轮）**：`SPIF_CMD_BASE = SPIF_PRG_CMD = 0x2C`（`reg_spi_filter.h:19,106`，源码事实）；picolibc `ENOSR = 63`（SDK 头文件**实测读取**）；SPIF 各实例 `reg = <… 0x2000>`（DT 源码）。
- **实测（host 复现）**：`-63 * 4 = -252`；`0x40070000 + 0x2C - 252 = 0x4006FF30`（见 `/tmp/defect_repro.c` 输出）。
- **既有记录**：`KNOWN_ISSUES.md` #78、`COVERAGE.md` 跳过项 —— 结论方向正确，**但数值 `-ENOSR = -28` / `SPIF_CMD_BASE - 112` / `0x4006ff90` 有误**（见 §6.1）。

### 2.6 修复建议（本轮不实施）

- **改 `:884`**：`sys_write32(spif_cmd.value, table_base + idx * 4);` → `sys_write32(spif_cmd.value, table_base + off * 4);`
- **改 `:938`**：同上，用 `off`。（`off` 是两处固定表分支的唯一正确槽位号来由。）
- 更稳健的写法：把两个函数的"更新已有槽 / 新建槽"逻辑合并到单出口，并在固定表分支内**显式断言 `off < SPIF_FIXED_CMD_TABLE_NUM`**（现由循环保证）。
- **改后测试用例预期值**：本工程对该分支**主动规避**（`COVERAGE.md`「跳过项」：一律改用 `spif_add_general_cmd()` + `spif_cmd_in_fixed_table()` 拦截），**未生成触发用例** ⇒ **无用例需改判**；跳过项条目本身保持不变（改为"上游已修后可选开启"需 commit 级决策）。

### 2.7 验证方法

- **禁止**为验证"野写"而在实机构造触发用例（COVERAGE.md 已明令）——会产生真实寄存器污染。
- 静态验证：改后对 `spif_add_cmd` 反汇编确认写址表达式为 `table_base + off*4`。
- host 单测：以桩函数替换 `acquire/release_spif_device` 与 `sys_read32/64`，断言写地址 == `table_base + off*4`。
- 若确要实机确认缺陷：在受控环境先 dump RTC 寄存器，触发后比对——**属高风险动作，本轮不做**。

---

## 3. 缺陷 #3 —— `mdio_dwmac_init` 丢弃 PHY 复位返回值（P1）

### 3.1 精确位置（已逐行复核）

- 文件：`d:/zephyr4_0/zephyr/drivers/mdio/mdio_dwmac.c`
- 被丢返回值的函数：`mdio_dwmac_ethphy_reset()` `:220-263`，返回点 `:229`（`gpio_pin_configure_dt` 失败）、`:235`（`gpio_pin_set_dt(...,0)` 失败）、`:249`（`gpio_pin_set_dt(...,1)` 失败）。
- **缺陷行：`:274-276`**
  ```c
  if (dev_config->reset_gpios.port) {
      mdio_dwmac_ethphy_reset(dev);      /* :275 返回值被丢弃 */
  }
  ```
- 题面行号 `:274-276` **准确**。

### 3.2 根因（错误路径吞掉）

典型的"硬件控制类返回值被丢弃"（`qsh-driver-review` §1.5）：复位例程可失败（GPIO 控制器未就绪 / 配置失败 / 电平设置失败），但 `mdio_dwmac_init()` 继续执行并最终 `return ret`（`ret` 此时仍为 `:270` 初始的 0）⇒ 设备被判定为 **ready**，复位失败被静默。

### 3.3 触发条件 + 可达性（本轮更正了一个既有错误判断）

`dev_config->reset_gpios.port != NULL` **且** 上述某次 GPIO 调用失败。

**可达性（实测）**：`reset_gpios` 取自 `mdio1` 的**任一 okay 子节点**中带 `reset-gpios` 者 —— `CHILD_RESET_GPIOS`（`:364`）+ `DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, CHILD_RESET_GPIOS)`（`:383`），**该宏没有 MAC 子节点过滤**（`CHECK_MAC_CHILD_` 只用于 `mac_enabled`）。本板 `eth1_phy` 带 `reset-gpios = <&gpiog 1 GPIO_ACTIVE_HIGH>`（`lsqsh_evb_cpu1.dts`）。

从 case_mdio 的**预处理构建产物** `…/mdio_dwmac.c.i` 读出实际初始化器：

```c
static struct mdio_dwmac_config mdio_dwmac_config_0 = {
    .base = (uint32_t)1074151424U,     /* 0x40064000 */
    …
    .reset_hold_ms = 0,
    .circuits_set_ms = 0,
    .reset_gpios = { .port = (&__device_dts_ord_172), .pin = 1, .dt_flags = 0 },
    .int_gpios   = { .port = (&__device_dts_ord_172), .pin = 5, .dt_flags = 1 },
};
```

⇒ `.reset_gpios.port` **非 NULL**，`if (dev_config->reset_gpios.port)` **为真**，`mdio_dwmac_ethphy_reset(dev)` **每次上电都会执行**。

**附注（不变式）**：`.reset_hold_ms = 0` 且 `.circuits_set_ms = 0`（这两个是 `mdio1` 自身的 DT 属性，`:381-382` `DT_INST_PROP_OR`，binding 缺省 0），故函数内两段"脉冲子块"（`:231-244` 拉低保持、`:252-259` 稳定等待）的门 `reset_hold_ms && circuits_set_ms` **恒假** ⇒ 每次上电只做 `gpio_pin_configure_dt`（`:227`）+ `gpio_pin_set_dt(...,1)`（`:247`），**从不把复位脚拉低**。因此被丢弃的返回值实际来自 `:227` / `:247` 两处，而非脉冲两处。

### 3.4 影响面 / 爆炸半径

- 复位失败被静默 → PHY 未被正确初始化 → 后续 MDIO 读回垃圾 → 现象被**误导性归因**为"PHY ID 不符 / PHY 型号不对"（`KNOWN_ISSUES.md` #73），而非"复位失败"。
- 与 **#5 串联放大**：`mdio1` init（POST_KERNEL，优先级 60）→ PHY init（POST_KERNEL，优先级 70）。复位未成功/PHY 不释放 ⇒ PHY 读回非 `0x1C` ⇒ **#5 的无超时自旋永久挂死** ⇒ 串口 0 行的"不可归因静默"。
- 波及所有带 `mdio1`/`eth1` 的工程（当前 `eth_test` 整体未注册进回归，见 §5）。

### 3.5 证据（分级）

- **实测（构建产物）**：上述 `.i` 的 `mdio_dwmac_config_0.reset_gpios.port != NULL`（本轮读取）。
- **源码推导**：`:274-276` 丢弃返回值；`:220-263` 的失败返回点；`:381-382` 的 `reset_hold_ms`/`circuits_set_ms` 来源。
- **既有记录**：`KNOWN_ISSUES.md` #73、`OPEN_ITEMS.md` O-16。

### 3.6 修复建议（本轮不实施）

- **改 `:274-276`**：
  ```c
  ret = mdio_dwmac_ethphy_reset(dev);
  if (ret) {
      DEV_ERR(dev, "PHY reset failed: %d", ret);
      goto done;      /* 复用既有 :340 的 done: 出口 */
  }
  ```
- **改后测试用例预期值**：`eth_test/tests/COVERAGE.md` 把该路径标为"跳过（不可达）"——该判断本身需更正（§6.3），但**当前无用例断言该分支** ⇒ 无预期值需改判；若今后为 `case_phy_reset` 增补失败注入用例，应按新的 `DEV_ERR` + 非 0 返回断言。

### 3.7 验证方法

- 实机：给 `mdio1` 补 `reset-hold-ms`/`circuits-set-ms` 后，人为使 `gpiog` 不可用（或在受控分支注入失败），观察 `mdio_dwmac_init()` 返回非 0。
- 静态：改后确认 `mdio_dwmac_ethphy_reset()` 返回值被接收并传播。
- **判定纪律**：本轮**未**做失败注入实测；"该门为真、返回值被丢弃"是实测/源码事实，"复位失败会导致何种实机表现"属源码推导，不标实测。

---

## 4. 缺陷 #4 —— `sdhci-of-linkedsemi.c` mutex 泄漏（P0）

> **来源：上一轮子 agent 分析，父会话转达（本轮未重新分析，仅收录）。证据等级：源码推导，无实测。**

### 4.1 精确位置

- `zephyr/drivers/sdhc/sdhci-of-linkedsemi.c`，函数 `linkedsemi_sdhci_request()`，函数体 `:406-508`。
- 加锁 `:422` `k_mutex_lock(&dev_data->access_mutex, K_FOREVER)`；解锁 `:505`；**泄漏点 `:477` `return -ENOTSUP;`**（`default:` 分支 `:475-478`，位于 `:449` 的 `if (data)` 块内）。
- 上游给的三个行号 **全部准确**。

### 4.2 ⚠️ 重要更正：有且仅有 1 个泄漏点

- 泄漏点**只有** `:477`。
- `:418`（CPU1 守卫 `return -EBUSY`）与 `:427`（`k_mutex_lock` 失败路径的 `return -EBUSY`）**都在锁外**（`:422` 之前 / 加锁失败），**不得为它们添加 `k_mutex_unlock`** —— 否则造成**二次解锁崩溃**。

### 4.3 根因（K_FOREVER 语义 + Zephyr 无 dead-owner 回收）

- `zephyr/kernel/mutex.c:117`：`owner == _current` 时**同线程重入直接放行**（不挂死，但 `lock_count++` ⇒ 互斥彻底失效）。
- `:156`：跨线程走 `z_pend_curr(K_FOREVER)` ⇒ **永不返回、不超时**。
- 表现：**跨线程永久阻塞；同线程静默失效 + 计数泄漏**。
- 附带：`:423` 的 `if (ret)` 在 `K_FOREVER` 下是**死代码**（`-EBUSY` 需 `K_NO_WAIT` 才可能出现）。

### 4.4 触发三元组（必须同时）

1. `data != NULL`（switch 在 `:449` 的 `if (data)` 块内）；
2. `cmd->opcode` 不在 `:453-474` 的 11 个白名单内；
3. 未命中 `:415-420` 的 CPU1 守卫。

### 4.5 次生效应（务必记录）

`atomic_set(&g_sdhci_busy, 1)` 在 `:432`（加锁后），清除语句 `:502` 被 `:477` 的提前 return 跳过 ⇒ `linkedsemi_sdhci_card_ready()`（`:537-547`）此后**恒返回 false** ⇒ `coredump_backend_emmc.c:521-524` **静默丢弃所有 eMMC coredump**（只留一行 `card not ready, skipping`）。**"故障现场恰好丢失崩溃现场"**。

### 4.6 修复建议（本轮不实施）

- **A**：单出口 —— 把 `:501-505` 做成公共尾部并加 `unlock:` 标签，`default` 分支改为 `ret = -ENOTSUP; goto unlock;`。**标签必须置于 `g_sdhci_busy` 清除（`:502`）之后**，否则引入上述次生缺陷。
- **B**：白名单校验前置到 `k_mutex_lock`（`:422`）**之前**（锁未持有，天然无泄漏）。
- **A+B 同时做**。**不要**单独把 `K_FOREVER` 换成有限超时（改变并发契约，需与 SD 栈一起评估）。

### 4.7 改后测试用例预期值

- `projects/emmc_test/tests/COVERAGE.md` 的 `request_mutex_not_leaked` / `request_data_invalid_opcode_notsup`：由 `❌预期FAIL` 改判为 `PASS`。
- **`set_io_clock_out_of_range_not_silent`（另一缺陷，`:228-232`）必须保持 `❌预期FAIL`**，不要串号。

### 4.8 验证方法

- 实机：连续两次调用 `sdhc_request()` 且第二次命中 `default` 分支 —— 修前第二次会阻塞（跨线程）或静默失效（同线程），修后应为 `-ENOTSUP` 且后续请求可正常加锁。
- **本轮未做**（无实测）。

---

## 5. 缺陷 #5 —— PHY 复位无超时自旋（P0）

### 5.1 精确位置（含路径更正）

> **路径更正**：题面写的 `zephyr/drivers/net/phy/phy_realtek_rtl8211f.c` **不存在**（该目录不存在）。实际文件为 **`d:/zephyr4_0/zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c`**。

- 函数：`phy_rt_rtl8211f_reset()` `:115-177`。
- **自旋 1（寄存器复位等待）**：`:156-162`
  ```c
  do {
      ret = phy_rt_rtl8211f_read(dev, MII_BMCR, &reg_val);
      if (ret) { LOG_ERR(...); return ret; }
  } while (reg_val & MII_BMCR_RESET);
  ```
- **自旋 2（PHY ID 等待）**：`:166-174`（`finalize_reset:` 标签之后）
  ```c
  do {
      ret = phy_rt_rtl8211f_read(dev, MII_PHYID1R, &reg_val);
      if (ret) { LOG_ERR(...); return ret; }
  } while (reg_val != REALTEK_OUI_MSB);
  ```
  `#define REALTEK_OUI_MSB (0x1CU)`（`:29`）。

### 5.2 根因（时序 / 无界循环）

两处循环的**退出条件依赖读回数据**：

- 若 `mdio_read` **返回错误**，循环体内 `if (ret)` 会 `return ret` —— 不挂死。
- 若 MDIO 事务"**成功**"但数据为垃圾（典型：PHY 不应答时 DWMAC MDIO 的 `GB` 位仍清零、`MAC_MDIO_DATA` 返回上次内容/0/0xFFFF —— 见 `eth_test/tests/COVERAGE.md` 跳过项第 5 条"本平台 MDIO 不校验从设备响应"），则 `reg_val` 永远不等于退出条件 ⇒ **无界自旋**。两处均**无 `k_timepoint` 超时兜底**。

### 5.3 触发条件 + 本板生效点

PHY 不应答 / 未成功复位 / 型号不符 / 总线异常。

本板走 **GPIO 复位分支**：`phy_rt_rtl8211f.c.i` 的 `rt_rtl8211f_0_config` 实测为
`.reset_gpio = { .port = (&__device_dts_ord_172), .pin = 1, .dt_flags = 0 }`（**非 NULL**）⇒ 运行时 `:122 if (config->reset_gpio.port)` 为真 → 走 `:124-139` → **`goto finalize_reset`（`:141`）** ⇒ **跳过自旋 1**，直接进入 **自旋 2（`:166-174`）**。

- **结论**：本板上**实际生效的是自旋 2**；自旋 1（`:156-162`）在本板为死代码，但换用「无 `reset-gpios` 的 PHY 节点」的板/overlay 即生效。两处都应修。

### 5.4 影响面 / 爆炸半径

- `phy_rt_rtl8211f_init()` 注册在 **POST_KERNEL**（`:705-709`，`CONFIG_NETWORKING_AUTO_INIT`）；`mdio1` init 亦在 POST_KERNEL。POST_KERNEL 阶段挂死 ⇒ **系统永不启动**、**串口 0 行**（不可归因的静默）。
- 波及所有带 eth1 的工程。810 CLAUDE.md §5 已把"带 eth1 工程 load 后串口 0 行 ⇒ 另疑 PHY 复位自旋挂死"列为速查项。

### 5.5 证据（分级）

- **实测（构建产物）**：预处理 `phy_realtek_rtl8211f.c.i` 中两处循环原样保留 —— `:29053 } while (reg_val & (1 << 15));`（自旋 1）与 `:29425 } while (reg_val != (0x1CU));`（自旋 2）；且 `rt_rtl8211f_0_config.reset_gpio.port` 非 NULL（本轮读取）。
- **源码推导**：`phy_rt_rtl8211f_read` 的失败会 return，故挂死前提是"读成功但值不对"；MDIO 不校验从设备响应的平台特性（引自 `eth_test/tests/COVERAGE.md` 跳过项）。
- **既有记录**：`KNOWN_ISSUES.md` #71（含 gdb PC 判据）。

### 5.6 修复建议（本轮不实施）

- 给两处自旋加超时，例如：
  ```c
  k_timepoint_t t = sys_timepoint_calc(K_MSEC(PHY_RT_RTL8211F_RESET_TIMEOUT_MS));
  do {
      ret = phy_rt_rtl8211f_read(dev, MII_PHYID1R, &reg_val);
      if (ret) { LOG_ERR(...); return ret; }
      if (sys_timepoint_expired(t)) { LOG_ERR("PHY %d not responding", config->addr); return -ETIMEDOUT; }
  } while (reg_val != REALTEK_OUI_MSB);
  ```
  自旋 1 同型处理。
- **改后测试用例预期值**：`eth_test` 现无对应该路径的"预期 FAIL"断言；`case_phy_reset`（见 §7）是路径冒烟（断言"系统仍能起来"），**修后断言不变**。若增加"PHY 不接线 ⇒ `-ETIMEDOUT`"用例，则按新错误码断言。

### 5.7 验证方法

- 实机（`KNOWN_ISSUES` #71 判据）：load 后串口 0 行 → gdb attach 读 PC，栈落在 `phy_rt_rtl8211f_reset` 的自旋循环内即确认。
- **本轮未执行**该实机复现——"构建产物证明循环存在 + reset_gpio 非 NULL"是实测；"PHY 不应答即挂死"是源码推导 + 平台特性引用，不标实测。

---

## 6. 复核中发现的与原判断不符之处

### 6.1 缺陷 #2 的数值：`-ENOSR` 不是 `-28`

- 原判断（`KNOWN_ISSUES.md` #78、`COVERAGE.md` #1/跳过项）："过期 `idx`（此时为 `-ENOSR` = **-28**）⇒ 写到 `SPIF_CMD_BASE - 112`（`0x4006ff90`）"。
- **复核结论：数值有误。** 本项目 libc 为 picolibc，`ENOSR = 63`（`zephyr-sdk-0.17.0/arm-zephyr-eabi/picolibc/include/sys/errno.h:120`；**-28 = `ENOSPC`**，`同文件 :90`）。Zephyr minimal/armstdc 的 `ENOSR` 亦为 63。
- 正确值：`idx = -63`，偏移 = **`-252` 字节**（`-0xFC`）；`table_base = dev_base + 0x2C`。
  - spif1 野写 = **`0x4006FF30`**（落在 **RTC** 块），非 `0x4006ff90`。
- 修复方向（`idx` → `off`）不变；仅数值与落点需更正。下游引用该值的文档（#78 / COVERAGE）应同步。

### 6.2 缺陷 #5 的文件路径不存在

- 题面：`zephyr/drivers/net/phy/phy_realtek_rtl8211f.c`。
- **复核结论：该路径不存在。** 实际为 `zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c`（`zephyr/drivers/net/` 下只有 `loopback.c`/`ppp.c`/`slip.c` 等，无 `phy/`）。

### 6.3 缺陷 #3 的可达性：`eth_test/tests/COVERAGE.md:99` 判断有误

- 原文："该路径的 `reset_gpios` 取自 `eth1` 的 `reset-gpios`（板级 dts **未给** `eth1` 该属性）⇒ 编译期 `reset_gpios.port == NULL`，函数体整段不执行，该分支**不可达**"。
- **复核结论：与构建产物矛盾。** `CHILD_RESET_GPIOS`（`mdio_dwmac.c:364`）对 mdio 节点的**所有 okay 子节点**生效、**无 MAC 过滤**；本板 PHY 子节点 `eth1_phy` 带 `reset-gpios`。case_mdio 构建产物 `mdio_dwmac_config_0.reset_gpios = { .port = &__device_dts_ord_172, .pin = 1 }`（**非 NULL**）⇒ **该门为真，函数每次上电都执行**（只是脉冲子块因 `reset_hold_ms`/`circuits_set_ms = 0` 而不执行 —— 这是另一个问题，不是"整段不执行"）。
- 佐证：同日新建的 `case_phy_reset/target.overlay` 自述"该门为真，函数每次上电都执行"，与该旧判断冲突（见 §7）。

### 6.4 缺陷 #2 行号尾行的微调

- 题面 `:934-942`：实际 `spif_add_cmd_with_dummy` 的固定表分支体为 **`:932-941`**（写址行 `:938`，与题面一致）。`:879-887` 段准确。

### 6.5 自我更正（本次分析过程中的判断修正，非对既有文档的更正）

初判时我一度认为 #2 分支**不可达**（因 `linkedsemi_spi_filter_init` 似乎会把固定命令写进硬件表）。复核 `spif_fixed_cmd_tab_restore()`（`:1740-1767`）后确认：**init 只在冷启动时把 DT 默认值拷入 SW 镜像，不写硬件表**；写硬件表的是 `cold_reset()`。因此 boot 后、cold_reset 前调用即命中 ⇒ **分支可触达**。此更正已在 §2.2 写明。

---

## 7. 新增发现

1. **`eth_test/tests/case_phy_reset/` 已存在但未登记**（`target.overlay` + `test.sh`，时间戳 2026-09-17 20:36–20:37）。它复用 `case_mdio` 源集，只改 overlay（给 `&mdio1` 补 `reset-hold-ms = <10>` / `circuits-set-ms = <30>`）以**打开 `mdio_dwmac_ethphy_reset()` 的脉冲子块**。其 overlay 注释明确写出"该门为真，函数每次上电都执行"——与 `COVERAGE.md:99` 的旧判断（§6.3）直接冲突，是本轮 #3 可达性更正的一手旁证。
   - 现状：**未登记进 `regression/cases.yaml`**（`eth_test` 整体未注册；`cases.yaml` 仅列 trng/rtc/otbn/i2c/i3c/mbedtls/adc/dma/flash/mbox/wdt/iopmp）。属"已建未启"状态，需后续决定是否纳管。

2. **缺陷 #3 与 #5 的串联放大链**（本轮新提出）：
   `mdio_dwmac_init` 丢弃复位返回值（#3） → PHY 未正确释放/复位 → PHY 读回非 `0x1C` → `phy_rt_rtl8211f_reset` 自旋 2 永久挂死（#5） → POST_KERNEL 不返回 → **串口 0 行**。单独看每项都"只是"错误掩盖 / 无超时；叠加后表现为"**完全不可归因的静默**"。调试时应先按 #5 的 gdb PC 判据定性，再回溯 #3 确认复位是否真的发生。

---

## 8. 哪些可实测复现、哪些只能源码推导

### 8.1 已实测（本轮产出）

| 项 | 实测方式 | 结论 |
|---|---|---|
| #1 回绕语义 | host 逐字复制函数编译运行 | 4294934 / 0 / 4294934；intended 4294967263 / 0xFFFFFFFF / 0xFFFFFFFF；饱和恒假；周期 131072 ms |
| #1 频率来源 | DT 源码读取 | 三实例 `clock-frequency = <32768>` |
| #2 `ENOSR` | SDK 头文件读取 | picolibc `ENOSR = 63`（`ENOSPC = 28`） |
| #2 偏移/落点 | host 算术 + DT/reg 头读取 | 偏移 -252；spif1→0x4006FF30（RTC 块） |
| #3 可达性 | 预处理构建产物 `.i` | `.reset_gpios.port != NULL` ⇒ 函数执行 |
| #5 循环与路径 | 预处理构建产物 `.i` | 两处 `while` 原样存在；`.reset_gpio.port != NULL` ⇒ 本板走自旋 2 |

### 8.2 只能源码推导（当前）

- **#2 的"野写"本身**：受"禁止构造触发用例"约束，未做实机观测；偏移为源码 + libc 常量推导。
- **#3 的"复位失败后果"**：未做失败注入。
- **#4 全部**：仅只读源码（上游子 agent），无实测。
- **#5 的"挂死"**：未做实机复现；依赖平台特性（MDIO 不校验从设备响应，引自工程内既有观察）。

### 8.3 "判断该场景不会发生" vs "验证过没问题"（分开陈述）

| 命题 | 性质 |
|---|---|
| #3 的 `if (reset_gpios.port)` 在本板为真、函数被执行 | **验证过没问题**（构建产物实测） |
| #3 的复位失败会在实机导致"PHY ID 不符" | **源码推导 + 既有记录**，本轮**未**做失败注入 |
| #5 的 PHY 不应答会导致 POST_KERNEL 挂死 | **源码推导**（+ 平台特性引用）；本轮**未**做实机挂死复现 |
| #2 的野写会污染 RTC/spifN 寄存器 | **源码推导 + 常量实测**；**未**实机观测 |
| #2 该分支"不可达" | **已证伪**：可达（§2.2 / §6.5） |
| #4 的 `:418` / `:427` 不需解锁 | **源码推导**（锁外路径）；**未**实机验证二次解锁崩溃 |
| 缺陷 1 不在"不回绕区"出现 | **验证过没问题**（host：1000/5000 ms 两例 BUG==INTENDED） |

---

## 9. 落盘确认

- **交付文件**：`d:\zephyr4_0\810_driver_test\DEFECT_ANALYSIS_2026-09-17.md`（本文件，新建）
- **未改动**：`zephyr/`、`modules/lib/pfr_tpm/`、任何测试工程、任何既有文档（`KNOWN_ISSUES.md` / `OPEN_ITEMS.md` / `COVERAGE.md` 等均只读）。
- **未派**任何子 agent。
- host 复现脚本：`C:\Users\26502\AppData\Local\Temp\defect_repro.c`（工程外临时文件，仅供复现，非交付物）。
