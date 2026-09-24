# DW DMA 块长掩码变体漏划 + TFR 未解除屏蔽致状态机不复位 根因与修复

> 定位闭环：2026-09-16。现象：`dma_test/tests/case_destructive`（套件 `dma_destructive`）
> 稳定 3/6 FAIL。修复后 6/6 PASS（连跑两次一致），`case_default` 4/4 无回归，
> 全量回归 29/29 PASS 确认。
>
> 该 case 此前因 `test.sh` 硬编码 2os 板级、与单核加载路径 `$pc` 错配（见 KNOWN_ISSUES #57）
> **从未真正跑过**，这两个驱动缺陷因此一直被"固件根本没启动"掩盖。

## 现象

- `test_dma_block_too_large`：`dma_config(block_size=70000)` 未被拒绝。
- `test_dma_large_block`：4KB M2M 传输超时（`dma_done is false`）。
- `test_dma_self_copy`：`dma_config(src==dst)` 返回非 0。
  **注意：这不是"src==dst 被显式拒绝"** —— 真实返回是 `-EBUSY`（缺陷 2 的状态残留连带），
  驱动里**没有任何 src==dst 相等比较**（只有 `sar=`/`dar=` 赋值）。判定见下。

## 根因（两条独立缺陷）

### 缺陷 1：`DW_CTLH_BLOCK_TS_MASK` 被定义在变体 `#if` 之外

```
dma_dw_common.h：DW_CTLH_BLOCK_TS_MASK = MASK(16,0)   ← 非 2.20a 的 17 位掩码
                 #if defined(CONFIG_DMA_DW_2_20A) ...  ← 本应在其中按变体重划
```
提交 `efc728f67300`（add support for snps,designware-dma-2.20a）把 `DONE`/`CFGL_*`/`CFGH_*`
等多组宏按变体重划时**漏划了这一组**，2.20a 因此沿用错掩码，超限值被放行。
**实测该 IP 的 BLOCK_TS 只有 11 位有效**（见"关键证据"）。

后果是**静默的错误行为**，不是报错：
- `block_size = 2048` → 写进 CTL_HI 后被硬件截断为 0 → 通道使能但**永不产生完成中断**（挂死）；
- `block_size > 2048` → 只搬运 `block_size - 2048` 字节 → **静默丢数据**。

### 缺陷 2：`complete_callback_en=1` 时 TFR 未解除屏蔽 → `state` 永不复位

```
dw_dma_config(complete_callback_en=1)：只 unmask BLOCK / DST_TRAN，TFR 保持 masked
dw_dma_isr()：只在 TFR 分支执行  chan_data->state = DW_DMA_IDLE
```
→ **任何一次成功传输后 `state` 永远停在 `DW_DMA_ACTIVE`**，下一次 `dma_config` 必判
`-EBUSY`（现场串口原文：`channel 0 must be inactive to reconfigure, currently 3`，
3 = `DW_DMA_ACTIVE`）。

附带：`complete_callback_en=1` 分支从不置空 `dma_tfrcallback` —— TFR 一旦被别的配置
解除屏蔽，ISR 会调用**上一次配置的陈旧回调**。

### 为什么 `case_default` 是绿的（假稳固性）

`case_default` 按**字母序**先跑 `test_dma_channel_config_stop`（`complete_callback_en=0`），
该分支**永久 unmask 了 TFR**，后续用例于是"搭便车"通过。
`case_destructive` 全部用例都用 `complete_callback_en=1`，一跑即暴露。
**即：该工程的绿依赖用例顺序巧合，换顺序或换工程就会翻。**

## 关键证据（GDB 断点 + 寄存器现场）

**4KB 超时现场**（断在 `dma_wait_complete()` 超时分支，通道尚未 stop）：
```
dma_done = 0, dma_status = -1
CTRL_LO(0)  = 0x1            ← 与驱动 LLI ctrl_lo 一致（基址/偏移核对无误）
CTRL_HI(0)  = 0              ← 驱动写 4096，硬件截断为 0
DAR(0)      = 0x10080200     ← 目的地址未推进
CHAN_EN     = 0x1            ← 通道使能着
STATUS_TFR=0  STATUS_BLK=0  STATUS_DSTT=0  STATUS_ERR=0
RAW_TFR=0     RAW_BLOCK=0    INTR_STATUS=0      ← 一个中断都没产生
MASK_TFR    = 0              ← TFR 被屏蔽（缺陷 2 的直接证据）
dw_dma0_data.chan[0].state = DW_DMA_ACTIVE      ← 缺陷 2 的直接证据
lli[0] = { ctrl_lo=1, ctrl_hi=4096 }            ← 驱动侧确实写了 4096
```

**块长扫描**（临时代码，一次烧录内扫完，已清理）：
```
n≤2047  完整搬运                    n=2048  超时且 0 字节（截断为 0，挂死）
n=2049  只搬 1 字节                 n=4094  只搬 2046 字节
n=4095  只搬 2047 字节              n=3000  只搬 952 字节
```
→ "4KB 太大"与"16KB 太大"**同因**（`& 0x7FF` 皆为 0），缩容不可能修好。

**CTL1 位宽鉴别（写-回读，通道空闲时）**：
```
写 0xfff  后回读 = 0x7ff   ← bit11 被丢弃
写 0x800  后回读 = 0       ← bit11 不存在
写 0x1000 后回读 = 0x1000  ← bit12(DONE) 存在
```

**`test_dma_self_copy` 的判定依据**（为何"该收下"而非"该拒绝"）：
1. 驱动内**无 src==dst 检查** —— 全文只有 `dma_dw_common.c:147-148` 的 `sar=`/`dar=` 赋值；
2. 工程自述的驱动约束（`dma_destructive_test.c` 文件头）列了三条拒绝条件
   （`source_data_size ∈ {1,2,4,8,16}`、`block_size ≤ 2047`、忙碌通道 `-EBUSY`），**无此条**；
3. **既有基线 `case_default` 的 `test_dma_channel_config_stop` 本身就是 `src==dst` 且长期 PASS**
   （`.source_address = buf` / `.dest_address = buf` / `zassert_equal(ret, 0, ...)`）。同一驱动、
   同一参数组合在两个用例里一个通过一个被拒 ⇒ 该"拒绝"是副作用而非契约。

> **设计分歧说明**：若产品上确认为"自拷贝属调用方笔误、应提前拦下"，那是**正当的设计选择**——
> 但实现方式应当是**驱动内显式校验并返回 `-EINVAL`**，同时同步修改测试预期与 `case_default`
> 对应用例。**靠状态机漏复位产生的 `-EBUSY` 来"拒"不成立**：拒得不一致（换个用例顺序就不拒）、
> 错误码错误（`-EBUSY` 谎报"通道忙"）。**用副作用实现的校验不算校验。**

## 修复

| 文件 | 改动 |
|---|---|
| `zephyr/drivers/dma/dma_dw_common.h` | 把 `DW_CTLH_BLOCK_TS_MASK` 移入 `#if defined(CONFIG_DMA_DW_2_20A)` 分支，2.20a 用 `MASK(10,0)`；非 2.20a 保持 `MASK(16,0)` **行为零变化** |
| `zephyr/drivers/dma/dma_dw_common.c` | (a) `dw_write(DW_MASK_TFR, DW_CHAN_UNMASK(channel))` 移出 else，**两种回调模式都解除屏蔽**；(b) 按模式置空陈旧回调指针 |
| `projects/dma_test/.../dma_destructive_test.c` | TC-6 `block_size` 4096 → **2047**（单块上限），末字节校验同步；头注释约束由 `0xFFFF` 改为 `CTL_HI[10:0] / 2047` |

## 验证

- `case_destructive` **6/6 PASS**（连跑两次逐项一致），`PROJECT EXECUTION SUCCESSFUL`
- `case_default` **4/4 PASS**，无回归
- 全量回归 **29/29 PASS**（含 4 个 `CONFIG_DMA=y` 工程：dma_test / otbn_test / otbn_selftest / mbedtls_test）
- `qsh-driver-review` P0/P1 = 0

## 后续（O-5 / O-9，已结）

### O-5：`src == dst` 改为驱动内**显式**拒绝

本 note 上文"设计分歧说明"里留的裁定项，用户裁定为**显式拒绝**。实现：
`dw_dma_config()` 的 block 循环内逐 block 比较**原始**地址（在 `dw_dma_mask_address()`
之前比较，避免 M2P/P2M 只给一侧打 host mask 导致漏判），相同即 `-EINVAL` + `goto out`。

- 位置选**逐 block**而非只看 head_block：`block_count > 1` 时描述符链的每一段都要受约束。
- 选**一律**（不限 M2M）而非仅 M2M：三类方向的同址都无有效语义（M2M 原地拷贝、
  M2P/P2M 外设寄存器与内存同址均属误配），direction 分支不构成豁免理由。
- 错误码用 `-EINVAL` 不用 `-EBUSY`：`-EBUSY` 是"通道状态不对"，与参数错误语义不符。

测试同步（**保住各自测试主体**）：
| 用例 | 改动 |
|---|---|
| `case_default/test_dma_channel_config_stop` | `buf` 拆成 `src_buf`/`dst_buf`——它测的是 config→start→stop 生命周期，src==dst 只是顺手写的 |
| `case_destructive/test_dma_self_copy` → `test_dma_self_copy_rejected` | 主体就是同址，改为断言 `dma_config() == -EINVAL` + 缓冲区未被改动 |
| `case_destructive/test_dma_block_too_large` | 拆成两个缓冲区——否则会被同址校验提前拦下，**失去 block_size 拒绝这一测试主体** |
| `case_destructive/test_dma_invalid_data_size` / `_data_size_zero` | 同样拆缓冲区（其 data_size 校验在循环之前，行为不受影响，拆开只为语义自洽） |

`scripts/dump_results.py` 的 `TEST_NAMES_BY_CASE[case_destructive]` 同步改名
（链接序实测：改名后仍排末位，顺序不变）。

**验证**（会话 `reports/2026-09-17-dmaC` —— ⚠️ **该会话已不可得、且从未入库**，见 `SPEC_REFERENCE` §6.4.7「语料可用性声明」）：`case_default` **4/4 PASS**、
`case_destructive` **6/6 PASS**；mem 兜底（`dump_results.py --json`）与串口直采逐项一致
（表名同步正确，无错位假结果）。

### O-9：错误后硬件是否自动清 `CH_EN` —— 代理实测 + 补屏蔽

**问题**：ERR 分支把 `state` 置 `DW_DMA_IDLE` 后，若硬件**不**自动清 `ChEn`，
则 `dw_dma_start()` 会因 `dw_dma_is_enabled()` 为真而 `goto out`（`ret = 0`），
**静默不启动**——把"响的失败（`-EBUSY`）"换成"哑的失败"。

**实测（正常 TFR，直接读寄存器）**：dmac1 基址 `0x40041000`，`DW_DMA_CHAN_EN`
（+`0x3A0`）在一次 M2M 传输 TFR 完成后回读 = **`0x0`**，且随后 `dma_stop()` 返回 0
（走 `!dw_dma_is_enabled()` 早退分支）。⇒ **硬件在传输结束时自行禁用通道**。
（代理实验，非 ERR 路径直接实测——ERR 注入不可达，见本 note 与 COVERAGE 的缺口表。）

**结论与改动**：ERR 路径**补**显式禁用
`dw_write(dev_cfg->base, DW_DMA_CHAN_EN, DW_CHAN_MASK(channel))`，使硬件状态与
软件状态机强制一致。理由：`dw_dma_stop()` 的早退分支、`dw_dma_start()` 的
`is_enabled` 检查都以"state IDLE ⟺ 通道已禁用"为前提；ERR 分支破坏该不变量。
对已禁用的通道重复写禁用是幂等的，无副作用。**依据 = 代理实测 + 不变量论证，
ERR 路径本身仍无实测。**

## 未修 / 存疑（留给后续裁定）

1. **错误分支同族缺陷**：`dw_dma_isr()` 的 ERR 分支同样不复位 `state` —— 一次硬件错误传输后通道也会残留 ACTIVE（同样的 `-EBUSY` 症状）。本次三个失败未覆盖该路径，为控制爆炸半径**未动**。
2. **HAL 头文件与实测冲突**：`reg_dmacv3_type.h` / `ls_hal_dmacv3.h` 声明 `block_ts:12`，实测 bit11 无效。当前驱动取**实测**值（宁可拒绝，不静默丢数据）。若量产硅片确为 12 位，掩码应放宽回 `MASK(11,0)`。
3. **套件外爆炸半径未验证**：29 个 case 里只有 `dma_test` 直接压测 DMA 数据通路，套件外经 DMA API 的其它驱动未覆盖。

## 通用教训

1. **别把"基线绿"当可靠性**：`case_default` 的绿依赖用例执行顺序。判定一个驱动的完成路径是否健康，要看它**跨用例模式**是否一致，不能只看某工程整体通过。
2. **位宽/掩码以写-回读实测为准**，头文件可能宽于实机（见 `debug-knowledge` mcu 域 `reg-bitwidth-write-readback.md`）。
3. **"错误路径被静默吞掉"是本类缺陷的共同模式**：超限值不报错而是截断、完成事件不解除屏蔽而是永不触发 —— 表现都是"不报错的错误行为"，比直接报错危险得多。
4. **缩容 / 缩短超时 / 加兜底钩子 ≠ 修复**：本案曾把 TC-6 从 16KB 缩到 4KB、加 `dma_stop` 兜底钩子，两者都无效（同因；且 `dw_dma_stop()` 在通道未使能时提前 return，不复位 state）。
