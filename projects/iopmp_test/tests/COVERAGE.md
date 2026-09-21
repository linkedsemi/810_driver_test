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

单 case：`case_default`，固件 `cpu1_iopmp_test/`，套件 `iopmp`，共 **7** 项
（P0 的 5 项 + P1a 新增 2 项）。
`test.sh` 走 `test_common.sh` 的 `build_single_test` / `load_and_run_single`（board
与加载入口同源，validate §1.9f / KNOWN_ISSUES #57）。

## 覆盖矩阵（套件 `iopmp`，7 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_iopmp_is_enable_after_init | `iopmp_config_enable(base,true)` → `iopmp_is_enable` | HAL 使能路径 | `iopmp_is_enable` 返回 true | HAL 契约（`iopmp.h:89`） | ✅ |
| test_iopmp_idempotent_init | `iopmp_config_enable(true)` ×2；再调 `(false)` 并断言 | 幂等 + HAL 限制显式化 | 双调后仍使能；`(false)` 后**仍使能**（HAL 忽略 enable） | `iopmp.h:89` 恒写 `IOPMP_EN\|MD_EN` | ✅ |
| test_iopmp_napot_encode_decode | `addr2napot` → `napot_reg2addr` / `napot_reg2size` | 纯函数编解码往返 | `dec_addr ≤ addr` 且 `dec_size ≥ size` | `iopmp.h:64/69/75` | ✅ |
| test_iopmp_napot_invalid_params | `addr2napot`（纯位运算）+ `iopmp_config_region_napot` 校验分支 | 契约 + 非法参数拒绝 | `addr2napot(0x100000,3)==0x40000`（**不校验**）；非 2 幂 / 未对齐 → `-EINVAL` 且不写寄存器 | `iopmp.h:64`（无校验）/ `iopmp.h:159`（校验） | ✅ |
| test_iopmp_register_readback | `iopmp_set|get_pmpaddrx` / `iopmp_set|get_pmpxcfg`（条目 0） | 寄存器读写 + **非破坏性恢复** | 写 CFG0=0x03 回读一致；测完 ADDR0/CFG0 与**测试前一致** | 寄存器读写契约 + 非破坏性原则 | ✅ |
| **test_iopmp_multi_region** | `iopmp_config_region_napot`（经 `iopmp_set_pmpxcfg_by_idx` 按条目读改写）+ `iopmp_config_region_tor`（**此前两函数均零调用**）+ `iopmp_get_pmpxcfg_by_idx` | 功能通路 | 条目 1/2/3 的 ADDR 各自独立且等于 `addr2napot(addr,size)`/原值；cfg 低 5 位分别 == `0x1B`(NAPOT,R\|W) / `0x19`(NAPOT,R) / `0x0B`(TOR,R\|W)；**条目 1 不被条目 2/3 的写入破坏**（同一打包寄存器内隔离）；三条目 LOCK 位恒 0；测完 CFG0/ADDR1 与测试前一致 | `iopmp.h:145-191` 契约 + `iopmp.h:45-55` 位域 + 非破坏性原则 | ⏳ |
| **test_iopmp_napot_boundary** | `napot_reg2addr_size`（**此前零覆盖**）+ `addr2napot`/`napot_reg2size`/`napot_reg2addr` 最小粒度往返 + `iopmp_config_region_napot` 的 `addr % size` 校验（`iopmp.h:164`） | 边界长度 | `addr2napot(0x1000,8)==0x400`、`napot_reg2size(0x400)==8`、`napot_reg2addr(0x400)==0x1000`；`addr2napot(0x8000,0x100)` 经 `napot_reg2addr_size` 解回 `(0x8000,0x100)`；配置层 `(0x1000,0x1000)` → 0，`(0x0FFF/0x1001,0x1000)` → `-EINVAL` 且 ADDR0 未被改写 | `iopmp.h:64-87` 纯位运算 + `:159-178` 校验 | ⏳ |

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
| **多分区/N 条目（NAPOT 覆盖范围、条目 1~7）** | ✅ **P1a 已补**：`test_iopmp_multi_region` 覆盖条目 1/2/3（NAPOT + TOR）与打包寄存器内隔离 | 已覆盖（条目 4~7 仍未覆盖） |
| **其它 IOPMP 实例（SEC_IOPMP2..5，`0x40001400`+）** | 只测 SEC_IOPMP1 | 未覆盖 |
| **SoC init 集成（开机保护规则）** | 本工程无 SoC init 配置 IOPMP | 未覆盖；TC-5 改为保存/恢复后，真部署下不再误删 |

## P1a 新增项说明（2026-09-17，只生成+编译通过，未烧录）

新增 2 项（`test_iopmp_multi_region` / `test_iopmp_napot_boundary`），既有 5 项一行未改；
唯一对既有文件的追加式改动是 `iopmp_test.c` 顶部加一行 `#include "ztest_glue.h"`（ZT_INFO 来源）。

新增项覆盖了此前**零调用**的 HAL 函数：`iopmp_config_region_napot`、`iopmp_config_region_tor`、
`iopmp_set_pmpxcfg_by_idx`、`iopmp_get_pmpxcfg_by_idx`、`napot_reg2addr_size`。
仍严守本工程「非破坏性」契约：动到条目 1~3 / 条目 0 前后都做**整寄存器（CFG0）保存+写回+断言恢复**，
并把 `COMP_LOCK == 0` 显式断言（一旦置位只能靠复位解除，会直接破坏非破坏性契约）。

### 跳过项（P1a）

| 候选 | 跳过类型 | 原因 |
|---|---|---|
| **real_violation_trap**（真实越权访问触发异常，从寄存器层下沉到行为层） | **平台不可注入（已实测）** | 本 SoC 互联对**被拒/未解码访问一律回 OKAY**（不产生 HRESP/BRESP error），IOPMP 只**静默丢弃**、既不产生总线错误、也不触发中断。这是 `KNOWN_ISSUES.md #65` 的**实测结论**（用 IOPMP3 + 拒绝规则 + allow-all 兜底实测：被拒页的写未落地，但 DMA 仍报"完成"、err 恒 0）。⇒ **本平台不存在"违例异常"这一可观测事件**，行为层用例无判据可写。**这是"实测不可注入"，不是"判断不会发生"。** 另：O-11 里用 GDB 注入方式验证的是**寄存器层**配置与恢复，不是行为层异常。 |
| **dma_protection**（DMA 访问受保护区域被拦） | **可注入但判定不可靠 + 越出本工程范围（需用户裁定）** | ① 效应其实**可观测**（被拒写不落地 = 静默丢弃），但需把规则配到**另一个实例 IOPMP3（`0x40001800`，守 DMAC1/ETH1/EMMC1）**并加 allow-all 兜底——本工程被测对象是 **SEC_IOPMP1**，属跨实例改动；② 该场景是**破坏性**的（改的是别的 IP 的保护规则，误配会让 DMA 通路失效），与本工程「非破坏性」契约冲突，且无人值守回归下风险高；③ 单靠"内存未变"无法区分"IOPMP 拦截"与"DMA 本身没跑"——需额外对照实验。⇒ **判断为该做也不该在本工程做**，登记给用户裁定（机制本身已由 #65 的实测记录覆盖）。 |
| `iopmp_config_region_napot(size=0)` | 不可测（UB） | `iopmp.h:164` 先算 `addr % size` 再判 2 幂，`size==0` 是**除零**（RV32 上走软除，行为未定义）。属 HAL 缺陷，但测试会破坏进程/结果不可判定 ⇒ 不写用例。 |
| Zephyr SoC 驱动 `zephyr/soc/linkedsemi/lsqsh/iopmp.c`（`iopmp_init` 的 DT 分区→条目映射、TOR/NA4 选择、`dump_pmp_regs`） | 需新增 DT 节点 + 开 `CONFIG_IOPMP` | 平台 dtsi 里**没有 `linkedsemi,iopmp` 节点**；要测必须自建节点并让 `iopmp_init()` 在开机时写 PMP 条目、使能过滤 —— 会把"开机保护规则"写进被测固件，改变全局保护状态（破坏性）且影响未知 ⇒ 不硬做。 |
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
test_iopmp_is_enable_after_init | ret in [1,1] | spec:HAL 契约——`iopmp_is_enable` 必须返回 true(1)（★无 INFO 通道 ⇒ NO_ACTUAL）
test_iopmp_idempotent_init | !none | !none:双调后仍使能 + (false) 后仍使能的合取
test_iopmp_napot_encode_decode | !none | !none:不等式关系（dec_addr ≤ addr、dec_size ≥ size），非单值
test_iopmp_napot_invalid_params | !none | !none:一个精确值 + 若干 -EINVAL 的合取
test_iopmp_register_readback | !none | !none:逐条 get/set 回读 + 非破坏性恢复的合取
```
