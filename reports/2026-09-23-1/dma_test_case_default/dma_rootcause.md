# DMA 驱动回归失败根因（case_default 6/7 FAIL + case_destructive 5/9 FAIL）

- 日期：2026-09-23
- 范围：`zephyr/drivers/dma/dma_dw_common.c` / `.h`（Linkedsemi `linkedsemi_dma` 走的就是这两个文件，
  `dma_linkedsemi.c` 直接 include `dma_dw_common.h`）
- 结论：**单根因**（+1 个被同一根因掩盖的独立缺口），**不是 11 个独立 bug**
- 性质：**陈年未修的上游遗留缺陷**（Intel 2022 引入），不是本轮新引入的回归；
  但**测试侧 09-18 加严**（`complete_callback_en` 从 0 改 1）使其首次暴露
- 证据来源：串口原文（3 份会话 09-21/09-22/09-23 结果**逐项完全一致**）+ 源码 + git 历史 + 既有现场记录
  `projects/dma_test/tests/notes/DMA_DW_BLOCK_TS_MASK_AND_TFR_STATE_ROOTCAUSE.md`

---

## 0. 对任务书事实输入的复核（纠正）

| 任务书写法 | 复核结果 |
|---|---|
| case_default 1/7 通过 | ✅ 正确 |
| case_destructive 4/9 通过 | ✅ 正确 |
| 11 个失败项行号 | ✅ 全部核对一致（`dma_test.c:81/134/210/246/337/395`、`dma_destructive_test.c:193/230/271/332/444`） |
| 「多个不相关的正向用例都返回非零，疑似单根因」 | ✅ **假设成立**，且比预想更彻底：11 项里 10 项同因 |
| 「dts 配置缺项 / 时钟未使能 / probe 失败」 | ❌ **推翻**。串口第 1~2 行 `dma_dw: Device dmac@40041000/40042000 initialized` ⇒ 两个 DMAC 实例 probe 成功、时钟/复位已就绪；失败发生在 `dw_dma_config()` 运行期的状态校验，与 probe 无关 |
| 报告里的 `dma_config ... is non-zero` 措辞 | ⚠️ 实为 `zassert_ok()` 的展开文案，实际返回值是 **`-EBUSY`（-16）**，串口原始日志每一条都带 `... currently 3` |

**关键未在任务书里的事实**：每一条 `dma_config` 失败前，串口都先打一行
`dma_dw_common: dmac@40041000: channel 0 must be inactive to reconfigure, currently 3`。
`3` = `DW_DMA_ACTIVE`。这是唯一根因的指纹。

---

## 1. 现象

三个会话（`2026-09-21-1` / `2026-09-22-1` / `2026-09-23-1`）的 DMA 报告**逐项完全相同**，
不是 flaky，是确定性挂。

```
START - test_dma_channel_config_stop                 ← default 套件按字母序的第一个用例
<err> dma_dw_common: dmac@40041000: channel 0 must be inactive to reconfigure, currently 3
    Assertion failed at .../dma_test.c:210: (dma_config(dma_dev, DMA_CHANNEL, &cfg) is non-zero)
 FAIL - test_dma_channel_config_stop
```

`test_dma_channel_config_stop` 的前半段（config → start → 等完成 → memcmp → stop）**全部成功**，
只在 `stop` 之后**重新 config** 时挂。此后同套件所有用例的第一次 `dma_config` 都挂 —— 通道被永久占住。

`case_destructive` 的 `test_dma_suspend_resume_guards` 给了**独立佐证**：它断言
`dma_suspend(ch0) == -EINVAL`（前提是通道处于 IDLE），实测 `suspend_idle_ret=0` ——
说明进该用例时通道**确实**是 ACTIVE。这条与「串口日志 currently 3」互为独立证据链。

---

## 2. 根因（分层）

### 2.1 主根因：`complete_callback_en=1` 时 TFR 中断被永久屏蔽 → 状态机永不复位

**软件状态机的唯一复位点是 TFR 中断分支**（`dma_dw_common.c:130`）：

```c
	while (status_tfr) {                       /* 只有这里 */
		...
		chan_data->state = DW_DMA_IDLE;
		if (chan_data->dma_tfrcallback) { ... }
	}
```

而 `dw_dma_config()` 匹配回调模式时**只给一个模式解除 TFR 屏蔽**（改动前，`:502-513`）：

```c
	if (cfg->complete_callback_en) {
		...
		dw_write(dev_cfg->base, DW_MASK_BLOCK,    DW_CHAN_UNMASK(channel));
		dw_write(dev_cfg->base, DW_MASK_DST_TRAN, DW_CHAN_UNMASK(channel));
		/* ← TFR 保持 masked（dw_dma_setup():830 初始化时全屏蔽） */
	} else {
		...
		dw_write(dev_cfg->base, DW_MASK_TFR, DW_CHAN_UNMASK(channel));
	}
```

⇒ **任何 `complete_callback_en=1` 的传输完成后，`state` 永远停在 `DW_DMA_ACTIVE`。**
下一次 `dw_dma_config()` 撞上 `:189` 的守卫：

```c
	if (chan_data->state != DW_DMA_IDLE && chan_data->state != DW_DMA_PREPARED) {
		LOG_ERR("%s: channel %d must be inactive to reconfigure, currently %d", ...);
		ret = -EBUSY;    /* ← -16，即 11 条失败里的每一条 */
		goto out;
	}
```

本工程 09-18 之后**两个套件的每个用例都用 `complete_callback_en=1`**
（`dma_test.c:64/118/174/325/372`、`dma_destructive_test.c:84/115/146/214/255/319/377/474`），
所以一跑即中。**「稳定挂」与「用例顺序无关」正是因为：谁排在第一个谁先挂，然后污染后面所有人。**

#### 为什么 09-17 是绿的（假稳固性，此处可验证）

`notes/DMA_DW_BLOCK_TS_MASK_AND_TFR_STATE_ROOTCAUSE.md` 记载过同一根因，当时 `case_default`
的第一个用例 `test_dma_channel_config_stop` 用 **`complete_callback_en=0`**，该分支会
**永久 unmask TFR**，后面用例「搭便车」。09-18 把它改成 `1`（现文件 `:174` 注释
`使完成可观测（旧版没开，无从等完成）`）—— **加严测试把潜伏缺陷翻出来了**。

#### 附带的第二重伤害：`dw_dma_stop()` 早退分支不复位状态

```c
	if (!dw_dma_is_enabled(dev, channel) && chan_data->state != DW_DMA_SUSPENDED) {
		ret = 0;
		goto out;        /* ← 硬件已自清 ChEn（传输完成），直接返回，state 仍是 ACTIVE */
	}
```

DW DMAC 在传输完成时**自清 ChEn**（这一点 09-17 已实测：`DW_DMA_CHAN_EN(0x400413A0)` 回读 `0x0`）。
于是「传输完成后调 stop 想复位状态」这条唯一的人工补救**也没生效**。
这解释了 `test_dma_channel_config_stop:210` 为什么连 `dma_stop()` 之后都还是 ACTIVE。

**分层定位结论：缺陷在 Zephyr driver 层（`dma_dw_common.c` 的状态机/中断屏蔽一致性），
不在测试用例期望、不在 HAL、不在 dts、不在时钟。**

### 2.2 被主根因掩盖的独立缺口：`src == dst` 无显式校验

`test_dma_self_copy_rejected:193` 断言 `dma_config(src==dst) == -EINVAL`。
驱动里**没有任何 src==dst 比较**（`dma_dw_config` 只有 `dw_dma_mask_address()` 的 `sar=`/`dar=` 赋值）。
今天它返回 `-EBUSY` 纯属主根因的副作用（**换个用例顺序就不拒**）。
这条已有用户裁定（`OPEN_ITEMS.md` **O-5**：显式拒绝、用 `-EINVAL`），实现后再丢失 —— 本次恢复。

### 2.3 同源的第三处（与本次 11 条失败无直接因果，但同一缺陷族且同为丢失的修复）

`DW_CTLH_BLOCK_TS_MASK` 被写在变体 `#if` **之外**，2.20a 沿用 `MASK(16,0)`（17 位），
而实测该 IP 的 BLOCK_TS 只有 **11 位**（写回读：`0xfff→0x7ff`、`0x800→0`）。
⇒ `block_size ∈ [2048, 65535]` 被放行后被硬件截断（2048 → 0 → **永不产生完成中断，通道挂死**）。
现有用例最大用 2047，故本轮未直接命中；`OPEN_ITEMS` **O-1** 用户裁定「掩码按实测 11 位」，
实现后再丢失 —— 本次恢复。

---

## 3. 证据

### 3.1 串口（决定性）
```
<err> dma_dw_common: dmac@40041000: channel 0 must be inactive to reconfigure, currently 3
```
- `3` = `enum dw_dma_state { IDLE=0, PREPARED=1, SUSPENDED=2, ACTIVE=3 }`（`dma_dw_common.h:263-268`）
- 三个会话的 `serial_TGT.log` 里这行出现 **5 次（default）/ 6 次（destructive）**，每次紧接一条 assert 失败
- `test_dma_suspend_resume_guards` 的 `suspend_idle_ret=0`（期望 `-EINVAL`）= 通道实际是 ACTIVE 的独立旁证

### 3.2 源码（可复核）
- `dma_dw_common.c:121-139` —— TFR 分支是 `state = DW_DMA_IDLE` 的**唯一**写入点（`grep -n "= DW_DMA_IDLE" ` 只此一处 + `dw_dma_resume/stop`）
- `dma_dw_common.c:502-513`（改动前）—— TFR 仅在 `else` 分支被 unmask
- `dma_dw_common.c:830` —— `dw_dma_setup()` 初始化时 `DW_MASK_TFR = DW_CHAN_MASK_ALL`（全屏蔽）
- `dma_dw_common.c:189-194` —— `-EBUSY` 守卫
- `dma_dw_common.c:664-667`（改动前）—— `dw_dma_stop()` 早退不复位

### 3.3 回归窗口判定：`git log -S`

| 问题 | 引入点 | 判定 |
|---|---|---|
| TFR 仅在非 block 模式 unmask | `f9acacfd118 dma: dw: Common dw dma driver functionality`（**Intel 上游原始实现**） | **陈年未修**（不是本仓库新引入） |
| `config` 的 `-EBUSY` 状态守卫 | `2f792c2de59 dma/dw_common: Update dma_dw_common from sof`（2022-03，上游 SOF 同步） | 同上，两者组合成缺陷 |
| `stop` 早退 | `58312e61043 dma: Repeated start/stop support for gpdma`（2023-01，Intel Tom Burdick） | 同上 |
| 本目录最后一次触碰驱动 | `8e862f73321`（**2026-04-29**，`add dma error status callback`） | 近期**没有** DMA 改动 ⇒ **不是回归窗口** |

真正的「窗口」在测试侧：`dma_test.c` / `dma_destructive_test.c` 于 **2026-09-18 21:04** 被加严
（`complete_callback_en` 0→1、新增 3 个 default 用例 / 3 个 destructive 用例、加正对照与精确错误码）。

### 3.4 一个必须先讲清的历史事实：09-17 的驱动修复**没有进代码**

`OPEN_ITEMS.md` 把 **O-5 / O-6 / O-9 / O-c 标为「已修」**，但全部注了「**代码未提交**」。
复核当前工作树 + HEAD：**四处修复一个都不在**——
`grep -rn "source_address ==" drivers/dma/` 无 DW 命中；`DW_MASK_TFR` 仍只在 `else` 分支
（`dma_dw_common.c:512`，改动前行号）；`DW_CTLH_BLOCK_TS_MASK` 仍在 `#if` 之外（`.h:187`）。

21 个 `git stash` 全部扫过，**没有任何一个含 `drivers/dma` 改动**；reflog 显示
`2026-09-21 11:32 checkout: moving from otbn_irq to driver_test`，`dma_dw_common.c/.h`
的 mtime（Sep 21 11:30）与邻文件（Aug 27 / Nov 13）不同 ⇒ 这两个文件在切分支时被重置，
工作树里的未提交修复随之丢失。

⇒ **`OPEN_ITEMS.md` 中 O-5 / O-6 / O-9 / O-c 的「已修」状态与当前代码不符**（文档漂移，
建议由有写权限的一侧更正；本报告不改 `OPEN_ITEMS.md`，它不在本次写域内）。

---

## 4. 改动（全部在写域内）

### 4.1 `zephyr/drivers/dma/dma_dw_common.c`

**(a) 主根因修复：两种回调模式都 unmask TFR + 按模式置空陈旧回调指针**

位置：`dw_dma_config()` 回调配置段（原 `:502-513`）。改动：
- 把 `dw_write(DW_MASK_TFR, DW_CHAN_UNMASK(channel))` **移出 `else`，在 if/else 之后无条件执行**；
- `complete_callback_en=1` 分支把 `dma_tfrcallback`/`tfruser_data` 置 NULL；
- `else` 分支把 `dma_blkcallback`/`dma_dsttrancallback` 及其 user_data 置 NULL。

**为什么能修**：状态机的复位点（TFR 分支）从此在**任何**回调模式下都会被触发
⇒ 传输结束后 `state` 自动回到 `DW_DMA_IDLE`，下一次 `dma_config` 不再撞 `-EBUSY`。

**为什么必须同时置空回调**：TFR 一旦在 block 模式也解除屏蔽，ISR 的 TFR 分支会调用
`dma_tfrcallback`。该指针在 block 模式下**从不更新**，若上一次配置是 block 模式
（`complete_callback_en=0`），就会回调**上一次配置的陈旧函数指针/用户数据** ——
这是修 (a) 必须成对做的原因，单独做会引入新 bug。ISR 侧已有 NULL 判空
（`:132` / `:107` / `:88`），置 NULL 是安全且语义正确的表达。

**副作用评估**：
- `complete_callback_en=0` 路径**零行为变化**（原来就 unmask TFR），仅多两条 NULL 赋值；
- `complete_callback_en=1` 路径新增一次 TFR 中断服务，且因回调为 NULL **不派发任何用户回调**
  ⇒ 对外可见行为只有「状态机复位」这一条，正是 API 契约所需；
- 本 SoC 上唯一在树内的 DW DMA 消费者是 `drivers/spi/spi_dw.c`（`complete_callback_en=0`，
  `:968`/`:1280`）与 `drivers/i2c/i2c_dw.c`（`:125`/`:172`，同样为 0）⇒ **均不受影响**；
- 树内 `cyclic=1 && complete_callback_en=1` 的组合只出现在 NXP/其他厂商驱动（`i2s_mcux_sai`、
  `uart_mcux_lpuart`），与本 IP 无关。

**(b) 安全网：`dw_dma_stop()` 早退分支不再「假装复位」**

位置：`dw_dma_stop()` 的 `!dw_dma_is_enabled(...)` 早退分支（原 `:664-667`）。改动：
在 `goto out` 之前，若 `state == DW_DMA_ACTIVE` 则置 `DW_DMA_IDLE`。

**为什么**：该分支的语义前提是「硬件通道已停」，那就必须顺带把软件状态对齐，否则
「stop 之后即可 reconfigure」这一 Zephyr DMA API 契约不成立。同时它是**兜底**：
- 覆盖 TFR 中断尚未被服务到的窗口（`dma_stop` 紧跟传输完成）；
- 覆盖 ERR 路径 —— 硬件若在错误时清了 ChEn，本兜底即可让通道恢复；若没清，
  则走原长路径（disable + `WAIT_FOR(ChEn)` + 置 IDLE）同样恢复。
  **两条子情形都收敛到「stop 后通道可用」**，因此不必再去赌 ERR 路径的硬件行为
  （`KNOWN_ISSUES #65`：本平台 ERR 无法注入）。

**为什么不误伤 `DW_DMA_PREPARED`**：只对 `ACTIVE` 迁移。只 config 未 start 的通道
保持 `PREPARED`，`config → stop → start` 序列仍可用（改动前的既有行为）。

**副作用评估**：对已停通道，本改动只影响 `chan_data->state` 一个字段；
`dw_dma_start()` 的前置检查是 `dw_dma_is_enabled()`（硬件）与 `state == PREPARED`（软件），
本改动把状态从「不可用」改成「可 config」，不会让任何合法序列失效。

**(c) 恢复 O-5：逐 block 显式拒绝 `src == dst`（`-EINVAL`）**

位置：`dw_dma_config()` 中 **`block_count` 容量检查之后、任何 `chan_data` 字段被改写之前**
（新增一个独立的小循环遍历描述符链，新增局部变量 `int idx;`）。
比较**原始**地址（在 `dw_dma_mask_address()` 给单侧打 host mask 之前，否则 M2P/P2M 会漏判），
相同即 `LOG_ERR` + `-EINVAL` + `goto out`。

**为什么**：这是用户已裁定的契约（O-5），且测试与文件头注释都已按此写
（`dma_destructive_test.c:14`「source_address 与 dest_address 不能相同」）。
逐 block 而非只看 head_block：`block_count>1` 时每一段都受约束。

**为什么用「前置独立循环」而不是塞进主循环**（审核 §1.2「错误路径跳过 cleanup」触发）：
主循环跑之前已经改过 `chan_data->direction/cfg_lo/cfg_hi/lli/lli_count` 并把
LLI 数组 `memset` 清零。若把拒绝放在主循环里，一次被拒的 config 会把**上一次成功 config
留下的 PREPARED 描述符抹掉**（通道仍标 PREPARED，后续 `dma_start` 会以全 0 描述符启动）。
放到主循环之前，拒绝路径**不产生任何副作用** —— 与 `test_dma_self_copy_rejected` /
`test_dma_lli_pool_exhaust` 断言的「拒绝发生在参数校验阶段，通道未被占用、缓冲区不应被改动」
严格一致。放在 `block_count` 容量检查**之后**，是为了保证遍历链的长度一定 ≤
`CONFIG_DMA_DW_LLI_POOL_SIZE`，不会因超长链提前踩空指针。

**副作用**：对既有合法用例无影响（所有 M2M/M2P/P2M 配置的 src/dst 本就不同）；
新增一次 O(block_count) 的链遍历（本板 pool=2，可忽略）。

### 4.2 `zephyr/drivers/dma/dma_dw_common.h`

**(d) 恢复 O-1 裁定 + 丢失的缺陷 1 修复**：把 `DW_CTLH_BLOCK_TS_MASK` 移入变体 `#if`：
- `CONFIG_DMA_DW_2_20A`（**本板即此配置**，`build .config:810 CONFIG_DMA_DW_2_20A=y`）：`MASK(10, 0)`
- 非 2.20a：保持 `MASK(16, 0)`，**行为零变化**

**为什么**：实测 BLOCK_TS 仅 11 位；掩码放宽会让 2048~65535 静默被硬件截断
（2048 → 0 → 通道永不完成）。修后 `dw_dma_config()` 的
`block_cfg->block_size > DW_CTLH_BLOCK_TS_MASK` 守卫给出显式 `-EINVAL`，而不是挂死/丢数据。
**副作用**：对本板的影响是「`block_size > 2047` 从『静默错误』变为『显式报错』」。
现有全部用例最大 2047 ⇒ 无回归。（`DW_CTLH_BLOCK_TS_MASK` 的另外两处用法在
`dw_dma_start()` 的 `D_SCAT_EN` 分支与 `dma_intel_lpss.c`，后者与该变体互斥、不受影响。）

### 4.3 改动后的静态验证（**未构建、未烧录、未碰硬件**）

用**现有构建目录里的编译参数**（`tests/case_default/build/target/compile_commands.json`）
对改动后的 TU 做 `-fsyntax-only` 检查（不产生任何目标文件、不写构建目录）：

| TU | 结果 |
|---|---|
| `zephyr/drivers/dma/dma_dw_common.c` | **EXIT 0**，0 error（仅既有的 `SET_BIT` 宏重定义 warning，来自 HAL/soc 头链，与本次改动无关） |
| `zephyr/drivers/dma/dma_linkedsemi.c`（头文件改动的消费者） | **EXIT 0**，0 error |
| `soc_dma.h` / `soc.h` 等被引入 | 无新增告警 |

⇒ 改动在语法/语义层通过（含新局部变量 `idx` 的作用域、`goto out` 路径的变量可达性）。
**这不能替代上板**：编译通过 ≠ 寄存器时序正确（见 §5.1）。

### 4.4 自审（`qsh-driver-review` 维度）

| 维度 | 结论 |
|---|---|
| §1.1 mutex/sem 泄漏 | **不适用**：`dma_dw_common.c` 全程无 `k_mutex/k_sem`（`grep` 无命中） |
| §1.2 错误路径跳过 cleanup | **本次命中 1 处并已修正**：`src==dst` 守卫最初放在主循环内（此时 LLI 已被 memset、方向/配置字段已被改写）⇒ 改为主循环前的前置校验（见 4.1(c)）。同函数的其余 8 处循环内 `-EINVAL` 为**既有**形态，本次未扩大 |
| §1.3 中断安全 | ISR 未改。新增的 TFR 分支在 block 模式下会写 `chan_data->state`（非 volatile），与线程态 API 并发；属**上游既有的「调用方不得在传输中重配」契约**（Zephyr DMA API 明文），本 IP 上无 cyclic 消费者，判 **P2 / 记录不改**（见 §6.5） |
| §1.5 返回值未检查 | 本次改动无 `|=` 累积、无丢弃返回值 |
| §1.6 错误码一致性 | 新增错误码 `-EINVAL`（负 errno）✓（注：`-EBUSY` 只用于状态错，不用作参数错） |
| §1.9 同步路径重复完成 | 已核对：block 模式下 TFR 被解除屏蔽后，用户回调仍**只有** BLOCK/DST_TRAN 一次（TFR 回调被显式置 NULL）⇒ **不会造成双回调** |
| §1.10 撤销位 ≠ 使能位 | 已核对 `DW_CHAN_UNMASK(channel)` 与既有 unmask 写法逐位一致（write-enable 半区 + enable 半区） |

**P0 = 0；P1 = 0（1 处 P1 形态已在改动中规避）；P2 = 1（§6.5，记录不改）。**

### 4.5 未改动项（有意为之）

- **未动 BLOCK / DST_TRAN 的 unmask 逻辑**：block 模式同时解除这两个屏蔽，与本次根因无关，
  动它只会扩大爆炸半径（本次只在 block 模式下补了「置空陈旧回调指针」，是修 TFR 的必要配对项）。
- **未改 `dw_dma_isr()` 的 ERR 分支状态复位**（`OPEN_ITEMS` O-6/O-9 的实现已丢失）：
  该路径在本平台**不可注入**（`KNOWN_ISSUES #65`：IOPMP 拒绝规则打不出 DMA ERR），
  改它属于不可验证改动；而 4.1(b) 的兜底已在**两种**硬件行为下都能恢复通道 ⇒ 不需要它。
  见 §6 未定项。
- **未改 HAL 头文件**：`modules/hal/linkedsemi/hal_driver/inc/reg_dmacv3_type.h:135`
  仍声明 `DMAC_BLOCK_TS_MASK = 0xfff`（12 位）、`hal_driver/api/ls_hal_dmacv3.h:65`
  仍写 `block_ts:12`，与实测 11 位冲突。这是 `OPEN_ITEMS` **O-1** 的用户裁定
  （「只在文档里备注，不修、不追」），且驱动侧已按实测取 11 位（4.2(d)）⇒ 本次**不动 HAL**。
- **未改任何测试用例**：本轮结论是「用例期望正确、驱动不满足期望」，改期望即掩盖缺陷。
  （唯一需要留意的是 `test_dma_lli_chain` / `test_dma_multi_channel_concurrent` 等 09-18
  新增、**从未跑绿过**的用例，见 §6。）

---

## 5. 上板怎么验（必须上板）

### 5.1 为什么必须上板
代码阅读**不能**定死以下三件事，只有上板才有答案：
1. `complete_callback_en=1` 时 TFR 是否真的在传输完成时置位、且与 BLOCK 在同一次 ISR 内被读到
   （若硬件分两次上报，用例会出现「第一次 config 仍撞 -EBUSY」的窗口 —— 见 §6.1）；
2. 修好后 `state` 是否真的回到 IDLE —— 唯一直接观测量是下一次 `dma_config` 的返回值；
3. 09-18 新增、从未执行到「真跑逻辑」的那几个用例（LLI 链、多通道并发）的硬件行为。

能靠代码阅读定死的（`-EBUSY` 来源、TFR 屏蔽位、掩码截断、早退分支）不需要上板，已在 §3 闭合。

### 5.2 命令（确切的）

```bash
# 单 case（推荐先跑这两个，顺序无关）
cd /d/zephyr4_0/810_driver_test
python projects/dma_test/tests/run_test.py --case case_default
python projects/dma_test/tests/run_test.py --case case_destructive

# 或走批量总控（会先做环境预检）
python regression/run_all.py
```

> 驱动在 `zephyr/drivers/dma/` 下，**必须**重新构建（`run_test.py` 默认会 build；
> 不要加 `--skip-build`）。

### 5.3 预期结果

| case | 现在 | 预期 |
|---|---|---|
| `case_default` | FAIL 1/7 | **PASS 7/7**，`PROJECT EXECUTION SUCCESSFUL` |
| `case_destructive` | FAIL 4/9 | **PASS 9/9**，`PROJECT EXECUTION SUCCESSFUL` |

串口（`reports/<date>/dma_test_case_*/serial_TGT.log`）应当满足：
- **`channel 0 must be inactive to reconfigure` 一行都不再出现**（这是最直接的判据）；
- 不再出现 `Assertion failed at .../dma_test.c:210` 等 11 处；
- `test_dma_self_copy_rejected` 应看到新的 `LOG_ERR`
  `channel 0 block 0 source and destination address are identical (0x...)`，
  且断言**通过**（不是靠 `-EBUSY` 通过）；
- `test_dma_suspend_resume_guards` 的 `INFO ... suspend_idle_ret=-22`（`-EINVAL`），
  **不再是 `0`**。

### 5.4 若仍有 FAIL，怎么归因（二分）
1. 若**只有**某一项还带 `currently 3` ⇒ 说明存在「TFR 与 BLOCK 分两次 ISR 上报」的窗口，
   再加 `dw_dma_config()` 的「ACTIVE 且硬件已禁用 ⇒ 视为 IDLE」自愈（见 §6.1），**不要**改用例。
2. 若 `test_dma_lli_chain` / `test_dma_multi_channel_concurrent` 以**数据不匹配 / 回调超时**
   （而不是 `-EBUSY`）失败 ⇒ 是**另一条独立问题**（LLP 链/多通道路径首次被真正执行），
   按 `embedded-debug` 分层再定位；本次**未**触碰这两条路径。
3. 若 `test_dma_large_block`（2047B）超时 ⇒ 与 4.2(d) 相关，先 `gdb` 读
   `CTRL_HI(0)` / `CHAN_EN` / `MASK_TFR` 三件套抓现场。

---

## 6. 未定项（不编，给出下一步）

### 6.1 `TFR` 与 `BLOCK` 是否同一次 ISR 上报 —— **未定（低风险）**
`dw_dma_isr()` 在**一次**调用里先处理 BLOCK、后处理 TFR，而 `dma_done` 由 BLOCK 分支置位。
若硬件把两者分两次上报，线程可能在 TFR 分支执行前就被唤醒 ⇒ `test_dma_loop_transfer`
（同一用例内连续 4 次 config）理论上存在窗口。
判断依据缺失：本次禁止上板，串口日志也看不出中断次数。
**下一步**：5.2 连跑两遍，看是否稳定 7/7；若偶发 `-EBUSY`，在 `dw_dma_config()`
的守卫里加「`state == ACTIVE` 且 `!dw_dma_is_enabled()` ⇒ 视为 IDLE」自愈（约 5 行）。
**不建议**为此改 `dma_done` 的置位条件（那是拿用例掩盖时序）。

### 6.2 ERR 路径的 `state` —— **未定（不可验证）**
`dw_dma_isr()` 的 ERR 分支不复位 `state`（O-6/O-9 的实现已丢失）。本平台 ERR 无法注入
（`KNOWN_ISSUES #65`），任何改动都只能是「推论级」。
本报告的 4.1(b) 在两种硬件行为下都能让通道从 ERR 中恢复，故**当前不改**。
**下一步**：若产品线上确有 DMA 错误注入手段，再补 O-6+O-9 并在报告中标注为实测；
在那之前不要把它记成「已验证」。

### 6.3 09-18 新增用例从未跑绿 —— **未定**
`test_dma_lli_chain`、`test_dma_multi_channel_concurrent`、`test_dma_dmac2_transfer` 之后的
LLI/并发路径从未执行到真正的数据阶段（一直卡在第一次 `dma_config`）。
本报告只保证「不再卡在 `-EBUSY`」，**不保证**这两条的硬件行为正确。
**下一步**：上板后若这两条仍 FAIL（非 `-EBUSY`），另开一次定位。

### 6.4 「同址拒绝」的语义边界 —— **未定（设计裁定）**
4.1(c) 恢复的是 O-5 的用户裁定（一律拒绝，不限 M2M）。该裁定对「原地 memcpy」这一
**合法**用法的取舍未被本次复核推翻，故按裁定执行。若将来需要支持原地拷贝，应改为
「仅 M2M 且 src==dst 时降级为 no-op 返回 0」，并按 `report-single-authority` 的方式
同步改 `dma_destructive_test.c:193` 的期望（**不要**只改驱动）。

### 6.5 ISR 在 block 模式下现在也会写 `chan_data->state` —— **记录不改（P2）**
改动前 `complete_callback_en=1` 模式下 ISR 从不触碰 `state`，线程态 API 独占该字段；
改动后 TFR 分支会在中断上下文写它。风险面：
- **无实际危害的方向**：ISR 只会把它写向 `IDLE`（更宽松），不会凭空产生 `-EBUSY`；
- **有理论风险的方向**：若出现「通道仍在跑却收到 TFR」的场景（**cyclic/auto-reload 传输**），
  `state` 会提前变 IDLE，允许在传输中重配。
  **本 IP 在本仓库内没有 cyclic 消费者**（树内 `cyclic=1 && complete_callback_en=1` 只出现在
  NXP 系驱动），`spi_dw.c`/`i2c_dw.c` 均为 `complete_callback_en=0`（TFR 本就解除屏蔽）
  ⇒ 现状下不可达，故不改（改 ISR 会扩大已验证代码面）。
**下一步**：若将来接入 cyclic 消费者（I2S/DMIC 类），把 TFR 分支的复位条件收紧为
`if (chan_data->state == DW_DMA_ACTIVE)`，并在 cyclic 用例上实测。

---

## 7. 一句话总结

`complete_callback_en=1` 时 TFR 中断没被解除屏蔽，而状态机的唯一复位点就在 TFR 分支 ——
于是每次传输成功后通道都永久停在 `DW_DMA_ACTIVE`，后续 `dma_config()` 一律 `-EBUSY`；
09-18 把测试从「默认不观测完成」加严成「观测完成」后，这条 2022 年就从 Intel 上游继承过来的
潜伏缺陷第一次全面暴露。修复 = 两种模式都 unmask TFR（并同步清陈旧回调指针）
+ `dma_stop` 早退分支复位状态；另恢复两处 09-17 已被裁定、但从未提交进代码的修复
（逐 block 拒绝 `src==dst`；BLOCK_TS 掩码按实测 11 位）。
