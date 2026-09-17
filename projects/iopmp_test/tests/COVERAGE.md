# iopmp_test 测试覆盖矩阵

工程定位：**LinkedSemi IOPMP（I/O Physical Memory Protection）HAL 测试**。被测对象 =
`modules/hal/linkedsemi/soc/rv32/qsh/include/iopmp.h` 的 inline API（`addr2napot` /
`napot_reg2*` / `iopmp_config_enable` / `iopmp_is_enable` / `iopmp_get|set_pmpaddrx` /
`iopmp_get|set_pmpxcfg` / `iopmp_config_region_napot`）。**操作目标实例** =
`SEC_IOPMP1_ADDR`（`0x40001000`）。

判定 = ztest 逐项 `PASS - <name>` + 本核 `PROJECT EXECUTION SUCCESSFUL`；
另用 GDB 直读 `0x40001000` 寄存器做**独立对照**（见"寄存器对照证据"）。

**环境：无外部接线**。**单核工程**：board `lsqsh_evb@1os/lsqsh/cpu1`，
入口 `0x10000000`（`SINGLE_BOARD` / `load_and_run_single`，SPEC_REFERENCE §6.1.10）。

> **重要前提**：本工程**未**开启 `CONFIG_IOPMP`，`zephyr/soc/linkedsemi/lsqsh/iopmp.c`
> 驱动**未编入** —— 即**没有任何 SoC init 在开机时配置 IOPMP1**。实测复位默认
> `MD_CFG=0`、`CFG0..3=0`、`ADDR0=0`（GDB 直读）。因此"破坏开机保护"在**本工程**
> 不体现；风险针对的是**真实部署**（`CONFIG_IOPMP=y`、条目已配置）时同一份用例
> 会静默删除在用保护（TC-5 已按此修复为非破坏性）。

## case 与固件

单 case：`case_default`，固件 `cpu1_iopmp_test/`，套件 `iopmp`，共 **5** 项。
`test.sh` 走 `test_common.sh` 的 `build_single_test` / `load_and_run_single`（board
与加载入口同源，validate §1.9f / KNOWN_ISSUES #57）。

## 覆盖矩阵（套件 `iopmp`，5 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_iopmp_is_enable_after_init | `iopmp_config_enable(base,true)` → `iopmp_is_enable` | HAL 使能路径 | `iopmp_is_enable` 返回 true | HAL 契约（`iopmp.h:89`） | ✅ |
| test_iopmp_idempotent_init | `iopmp_config_enable(true)` ×2；再调 `(false)` 并断言 | 幂等 + HAL 限制显式化 | 双调后仍使能；`(false)` 后**仍使能**（HAL 忽略 enable） | `iopmp.h:89` 恒写 `IOPMP_EN\|MD_EN` | ✅ |
| test_iopmp_napot_encode_decode | `addr2napot` → `napot_reg2addr` / `napot_reg2size` | 纯函数编解码往返 | `dec_addr ≤ addr` 且 `dec_size ≥ size` | `iopmp.h:64/69/75` | ✅ |
| test_iopmp_napot_invalid_params | `addr2napot`（纯位运算）+ `iopmp_config_region_napot` 校验分支 | 契约 + 非法参数拒绝 | `addr2napot(0x100000,3)==0x40000`（**不校验**）；非 2 幂 / 未对齐 → `-EINVAL` 且不写寄存器 | `iopmp.h:64`（无校验）/ `iopmp.h:159`（校验） | ✅ |
| test_iopmp_register_readback | `iopmp_set|get_pmpaddrx` / `iopmp_set|get_pmpxcfg`（条目 0） | 寄存器读写 + **非破坏性恢复** | 写 CFG0=0x03 回读一致；测完 ADDR0/CFG0 与**测试前一致** | 寄存器读写契约 + 非破坏性原则 | ✅ |

## 寄存器对照证据（2026-09-17，GDB 直读 `0x40001000`）

在 `test_main` 入口注入模拟"上层已配置的保护"（`ADDR0=0x0ABCDEF0`、`CFG0=0x03`
→ 条目 0 与打包寄存器条目 1~3 全部置位），分别在 **TC-5 进入前**与**整套结束**读取：

| 时点 | MD_CFG | CFG0 | ADDR0 | 结论 |
|------|--------|------|-------|------|
| 复位默认 | 0x00000000 | 0x00000000 | 0x00000000 | 本工程无 SoC init 配置 IOPMP |
| 注入后 | 0x00000000 | 0x00000003 | 0x0ABCDEF0 | 模拟在用配置 |
| TC-5 前 | 0x00000003 | 0x00000003 | 0x0ABCDEF0 | 前序用例已使能 |
| **修复前**跑完 | 0x00000003 | **0x00000000** | **0x00000000** | ❌ 条目被清零且不恢复（保护被静默删除） |
| **修复后**跑完 | 0x00000003 | 0x00000003 | 0x0ABCDEF0 | ✅ 与 TC-5 前逐位一致 |

## 判定说明

- **非破坏性（TC-5 落实）**：先读 `ADDR0`/`CFG0` 原值保存，写-回读后**整体写回**
  并断言恢复。注意 `CFG0` 是**打包寄存器**（32 位含 4 个条目，各 1 字节），
  `iopmp_set_pmpxcfg(base,0,val)` 写的是整个寄存器 —— 原实现"清理"写 `CFG0=0`
  会连带把条目 1~3 一并清零（`CFG=0` 即 A=OFF，条目失效）。
- **HAL disable 限制（TC-2）**：`iopmp_config_enable(dev, enable)` **忽略 `enable`**
  （`iopmp.h:89` 恒写 `IOPMP_EN=1|MD_EN=1`）⇒ 传 false 是 no-op。这是 **HAL 软件
  限制，不是硬件限制**：实测 `MD_CFG` 可读写（写 `0x2/0x1/0x0` 均按值回读），
  直写 `MD_CFG=0` 即可真正关闭。用例把"关不掉"显式断言为事实，而非留裸调用。
- **addr2napot 契约（TC-4）**：`addr2napot()` 是纯位运算 `(addr>>2)|((size-1)>>3)`，
  **不校验**非 2 幂 size；拒绝契约在 `iopmp_config_region_napot()`（返回 `-EINVAL`
  且不写寄存器）。

## 未覆盖 / 已知缺口

| 缺口 | 原因 | 影响 |
|---|---|---|
| **真正关闭 IOPMP（`MD_CFG=0`）的运行时用例** | HAL 无 disable API（`iopmp.h:89` 忽略参数），只能直写寄存器 | 未覆盖；GDB 侧已证可关（见上），用例内故意不做（避免用例中途改变全局保护状态） |
| **违例注入 / 中断（INTR）路径** | 本 SoC 互联对未授权访问**一律回 OKAY**，IOPMP 静默丢弃、不报错（KNOWN_ISSUES #65） | 违例中断/`MD_CFG` bit4 粘滞位路径未覆盖 |
| **`MD_CFG` bit4（曾违例粘滞位）** | 需先制造违例（同上不可注入） | 未覆盖 |
| **COMP_LOCK / 自我封锁** | 破坏性场景，用例显式砍掉（锁定后只能复位解除） | 未覆盖（设计取舍） |
| **Zephyr SoC 驱动 `zephyr/soc/linkedsemi/lsqsh/iopmp.c`** | 本工程未开 `CONFIG_IOPMP`，该 driver 未编入 | 未覆盖：`iopmp_init()` 的 DT 分区→条目映射、TOR/NA4 选择、`dump_pmp_regs` 全未测 |
| **多分区/N條目（NAPOT 覆盖范围、条目 1~7）** | 用例只操作条目 0 | 未覆盖（TC-5 的 CFG0 打包寄存器写会触及条目 1~3，但已整体恢复） |
| **其它 IOPMP 实例（SEC_IOPMP2..5，`0x40001400`+）** | 只测 SEC_IOPMP1 | 未覆盖 |
| **SoC init 集成（开机保护规则）** | 本工程无 SoC init 配置 IOPMP | 未覆盖；TC-5 改为保存/恢复后，真部署下不再误删 |
