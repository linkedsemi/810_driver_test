# dma_test 测试覆盖矩阵

工程定位：**DesignWare DMA（dmac1，8 通道）内存到内存（M2M）驱动测试**。被测对象 =
`zephyr/drivers/dma/dma_dw_common.c`（`dw_dma_config` / `dw_dma_start` /
`dw_dma_stop` / `dw_dma_isr`）与 `dma_dw_common.h`（CTL_HI/CTL_LO 字段布局）。
驱动路径列写被触发的函数 + 分支；判定 = `memcmp` 数据比对 + 返回码契约 +
ztest 逐项 PASS + `PROJECT EXECUTION SUCCESSFUL`。

**环境：无外部接线**（片内 SRAM 自拷贝，CLAUDE.md §1）。**单核工程**：
board `lsqsh_evb@1os/lsqsh/cpu1`，入口 `0x10000000`（`SINGLE_BOARD` /
`load_and_run_single`，SPEC_REFERENCE §6.1.10）。

## case 与固件开关

两个 case 共用同一固件 `cpu1_dma_test/`，由 CMake 开关 `-DDMA_DESTRUCTIVE` 二选一
编入**一套** ztest 套件（`CMakeLists.txt` L8-16），因此两 case 是**两个独立构建**，
各自 ELF、各自 ztest 链接序：

| case | 固件开关 | 测试套件 | 测试项集合 |
|---|---|---|---|
| case_default | 默认 | `dma` | 功能通路 ×2 + 通道生命周期 ×1 + 参数校验 ×1 + LLI 链 ×1 + 多通道并发 ×1 + **dmac2 实例 ×1** = 7 |
| case_destructive | `-DDMA_DESTRUCTIVE=1` | `dma_destructive` | 参数校验 ×3 + 边界传输 ×2 + 自拷贝 ×1 + **LLI pool 耗尽 ×1 + 传输中止 ×1 + suspend/resume 守卫 ×1** = 9 |

> **P1a（2026-09-17）新增**：`dma` +3 项、`dma_destructive` +3 项（详见文末「P1a 新增项说明」）。
> `app.overlay` 同时追加 `&dmac2 { status = "okay"; };`（`dmac2@40042000`，与 dmac1 同
> compatible / 同 8 通道）——这是**唯一新增的 overlay 配置项**，供 dmac2 用例使用；
> 两 case 共用该 overlay，故 case_destructive 也会初始化 dmac2（多一个闲置控制器实例，不影响其用例）。

`test.sh` 两 case 均走 `test_common.sh` 的 `SINGLE_BOARD`（1os）+ `load_and_run_single`，
board 与加载入口同源（validate §1.9f / KNOWN_ISSUES #57）。

## 覆盖矩阵

### case_default（套件 `dma`，7 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_dma_loop_transfer | `dw_dma_config`（M2M 分支）→ `dw_dma_start`（CTL/SAR/DAR 写入）→ `dw_dma_isr`（BLOCK/DST_TRAN/TFR 分支） | 功能通路 + 重复重载 | 4 轮 256B M2M 逐字节相等 | `zephyr/tests/drivers/dma/loop_transfer` 官方样例语义 | ✅ |
| test_dma_transfer_ones_zeros | 同上，`complete_callback_en=1` 连续两次 config/start | 功能通路 + 数据模式边界 | 全 0x00 / 全 0xFF 256B 逐字节相等 | 同上 | ✅ |
| test_dma_channel_config_stop | `dw_dma_config` → `dw_dma_start` → **等完成 + memcmp** → `dw_dma_stop` → **重新 config/start 并再次 memcmp** | 通道生命周期 | `dma_config/start/stop == 0` **且两次传输数据逐字节相等**（迁移可观测） | `dma_driver_api` 契约 + 数据不变量 | ✅ |
| test_dma_invalid_channel | `dw_dma_config` 入口 `channel >= DW_CHAN_COUNT` 分支 | 异常参数 | **正对照**：同一 cfg 在 ch0 被接受（== 0）；ch99 返回**精确 `-EINVAL`** | `dma_dw_common.c` L181 | ✅ |
| **test_dma_lli_chain** | `dw_dma_config` 多块循环（`cfg->block_count=2`，逐块写 CTL_LO/CTL_HI + `lli_desc->llp=lli_desc+1`，L265-506）→ `dw_dma_start` 的 HW-LLI LLP 写回（L611-624） | 功能通路 | 两块各 64B（共 128B）逐字节相等 + 回调触发 | `DMA_DW_HW_LLI` + `dma_dw_common.c` L252-506 | ⏳ |
| **test_dma_multi_channel_concurrent** | 两通道（ch0/ch1）并发 `dw_dma_config`+`dw_dma_start`，`dw_dma_isr` 按 channel 分发（L115-152） | 并发 | 两路各自 `memcmp` 相等（不串扰）+ 两回调都触发 | `dma_driver_api` 契约 + 驱动 per-channel 状态 | ⏳ |
| **test_dma_dmac2_transfer** | 同一驱动的**第二个实例** `dmac2@40042000`（base 由 DT 注入，验证无 base 硬编码） | 功能通路 | 256B 逐字节相等 + 回调触发 | DT `dmac2` 节点（app.overlay 使能） | ⏳ |

### case_destructive（套件 `dma_destructive`，9 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_dma_invalid_data_size | `dw_dma_config` 的 `source_data_size` ∈ {1,2,4,8,16} 校验分支 | 异常参数 | 返回非 0（`-EINVAL`） | `dma_dw_common.c` L204 | ✅ |
| test_dma_data_size_zero | 同上（data_size=0 落非法分支） | 异常参数 | 返回非 0（`-EINVAL`） | `dma_dw_common.c` L204 | ✅ |
| test_dma_block_too_large | `dw_dma_config` 的 `block_size > DW_CTLH_BLOCK_TS_MASK` 拒绝分支 | 异常参数（边界常量） | 返回非 0（`-EINVAL`） | 常量 = **实测写-回读上限 0x7FF**（`dma_dw_common.h` `CONFIG_DMA_DW_2_20A` 分支；HAL 头 0xfff 与实测不符，见 notes） | ✅ |
| test_dma_self_copy_rejected | `dw_dma_config` 逐 block 的 `source_address == dest_address` 拒绝分支 | 异常参数（同址退化配置） | `dma_config` 返回 `-EINVAL`，缓冲区内容未被改动 | `dma_dw_common.c` 同址显式拒绝（设计选择：由驱动显式校验，不靠状态机副作用） | ✅ |
| test_dma_one_byte | config → start → isr TFR | 边界（最小传输） | 1 字节 0xAB 送达 + 回调触发 | `dma_driver_api` 契约 | ✅ |
| test_dma_large_block | config → start → isr TFR | 边界（单块上限） | 2047B（`BLOCK_TS[10:0]` 最大值）首尾字节正确 + 回调触发 | 上限实测：>2047 只搬 `n & 0x7FF` 次，2048 截断为 0 → 永久挂住 | ✅ |
| **test_dma_lli_pool_exhaust** | `dw_dma_config` 的 pool 容量检查（`block_count > CONFIG_DMA_DW_LLI_POOL_SIZE`，L226-232） | 异常参数（边界长度） | `block_count=3`（pool=2）→ `-EINVAL`；拒绝发生在任何状态/描述符改写前，缓冲区不得被改动；拒绝后通道仍可正常 config/start | `dma_dw_common.c` L226 + 本板 `.config` 实测 `DMA_DW_LLI_POOL_SIZE=2` | ⏳ |
| **test_dma_abort_mid_transfer** | `dw_dma_start` → 立即 `dw_dma_stop`：**实际只走 `!dw_dma_is_enabled()` 早退分支**（2047B µs 级完成 + 硬件自清 ChEn） | 中断/时序（**早退路径**） | `dma_stop` 返回 0 **且重复 stop 仍返回 0**（幂等早退）；随后同一通道可 `config+start` 并完成 256B 传输 | `dma_dw_common.c` L677-764；在途 disable 分支**未证** | ⏳ |
| **test_dma_suspend_resume_guards** | `dw_dma_suspend`/`dw_dma_resume` 的 `state` 守卫（L811-815 / L780-784）+ 两函数的 `channel >= DW_CHAN_COUNT`（L804 / L773）；**ch99 半段在 ch0=ACTIVE 下执行** | 中断/时序（状态守卫） | 非 ACTIVE 时 suspend → `-EINVAL`；非 SUSPENDED 时 resume → `-EINVAL`；ch99 两 API → `-EINVAL` **且 ch0 状态未被破坏（随后 `dma_stop(ch0)` == 0）** | `dma_dw_common.c` L766-829 | ⏳ |

## 判定说明

- **数据正确性**：`memcmp` 逐字节比对，失败输出首个不符字节位置（`zassert_mem_equal`）。
- **返回码契约**：负例项以 `zassert_not_equal(ret, 0)` 判，不绑定具体 errno——驱动
  对非法参数统一走 `-EINVAL`，但契约只要求"拒绝"。
- **状态机复位（TFR/ERR 路径）**：`dw_dma_isr` 是驱动唯一的"传输结束"事件源。
  传输以 TFR 或 ERR 结束后 `chan_data->state` 必须离开 `DW_DMA_ACTIVE`，否则下一次
  `dw_dma_config` 被状态检查判 `-EBUSY`。TFR 路径由 test_dma_loop_transfer（4 轮连续
  config+start）/ one_byte / large_block 覆盖；**ERR 路径无合法用例可构造**（见下）。
- **同址传输（src == dst）**：驱动在 `dw_dma_config` 逐 block 显式拒绝（`-EINVAL`），
  **不依赖状态机副作用**。原文案的"拒绝"其实是 `-EBUSY`（状态残留连带：语义错、且
  换个用例顺序就不拒），已改为显式校验。负例用例的 src/dst 一律用两个不同缓冲区，
  避免被同址校验提前拦下而失去各自的测试主体。
- **`test_dma_before` 钩子**：每项前调 `dma_stop(CHANNEL)` 释放通道，供上一项
  超时/异常后恢复；这是隔离手段，不是对驱动缺陷的兜底。

## 未覆盖 / 已知缺口

| 缺口 | 原因 | 影响 |
|---|---|---|
| **ERR 中断路径**（`dw_dma_isr` `status_err` 分支、`dma_errcallback`、错误路径 `state` 复位） | **实测不可达（两轮排除）**。①未解码地址：6 地址（`0x12000000`/`0x20000000`/`0x30000000`/`0x50000000`/`0x80000000`/`0xC0000000`）× 读/写 = 12 次注入全部 `DMA_STATUS_COMPLETE`；②**IOPMP 错误源**（2026-09-17 补测）：IOPMP3（`0x40001800`，守 DMAC1/ETH1/EMMC1）拒绝规则**确实生效**（被拒页的写实测未落地），但 DMA 仍报 `status=1`"完成"、`err_cb` 恒 0、`DW_RAW_ERR` 恒 `0x0`——即使拒绝整个 1MB SRAM（含描述符池）；③非法传输配置（非对齐 / 尺寸非整倍数 / burst 失配 / 打外设区）亦无 ERR。⇒ 该 SoC 互联对被拒/未解码访问**一律回 OKAY**，DW DMAC 无可感知错误事件；DW DMAC 无软件置位 ERR 的寄存器（`MASK_ERR`/`CLEAR_ERR` 只能屏蔽/清除）。详见 `notes/DMA_DW_ERR_PATH_INJECTION_ATTEMPT.md` | ERR 分支未实测。其 `state = DW_DMA_IDLE` 依赖"错误后硬件自行清 ChEn"；该假设**未直接实测**，但已用**代理实验**佐证：正常 TFR 完成后读 `DW_DMA_CHAN_EN`（`0x400413A0`）= **0x0**，即硬件在传输结束时自行禁用通道（`dma_stop()` 随即走 `!dw_dma_is_enabled()` 早退返回 0）。错误路径据此显式补 `dw_write(DW_DMA_CHAN_EN, DW_CHAN_MASK(channel))`，使硬件状态与软件状态机强制一致——依据是**代理实测 + 不变量论证，非 ERR 路径直接实测** |
| 外设握手方向（M2P/P2M） | 本工程只覆盖 M2M；M2P/P2M 需要真实外设 consumer（SPI3/4 未使能） | 未覆盖（**SG 多块已由 test_dma_lli_chain / test_dma_lli_pool_exhaust 覆盖**） |
| `dw_dma_suspend` / `dw_dma_resume` 的**成功路径**（ACTIVE→SUSPENDED→ACTIVE） | 需在传输**在途**时命中 suspend；2047B M2M 传输仅 ~µs 级，无法确定性地"在途"命中（会 flaky）→ 只覆盖可确定的状态守卫分支 | 成功路径未覆盖（守卫分支已由 test_dma_suspend_resume_guards 覆盖） |
| `dmac2` 之外的其余 DW 实例 | 本板只有 dmac1/dmac2 两个实例 | 已全覆 |

## 参考

- 根因与实测数据：`tests/notes/DMA_DW_BLOCK_TS_MASK_AND_TFR_STATE_ROOTCAUSE.md`
- **ERR 路径注入尝试（IOPMP 触发器）实测记录**：`tests/notes/DMA_DW_ERR_PATH_INJECTION_ATTEMPT.md`
- 驱动源码：`zephyr/drivers/dma/dma_dw_common.{c,h}`

## P1a 新增项说明（2026-09-17，只生成+编译通过，未烧录）

新增 6 项（`dma` 套件 +3、`dma_destructive` 套件 +3），全部**追加**，既有 10 项一行未改。

- **新增配置项 1 处**：`app.overlay` 追加 `&dmac2 { status = "okay"; };`（`dmac2@40042000`）。
  该节点 soc dtsi 默认 `disabled`，与 dmac1 同 `compatible`（`linkedsemi,dma` +
  `snps,designware-dma-2.20a`）与同 `dma-channels=8`。**不改**既有 `&dmac1` 配置、
  **不改** `prj.conf`、**不改** `CMakeLists.txt`。
- **块长边界仍按 11 位**（承 `OPEN_ITEMS.md` O-1）：新增项 `block_size` 最大用 2047
  （`DW_CTLH_BLOCK_TS_MASK` 实测上限），**未擅自改 HAL**。
- **跳过项**：
  - M2P/P2M 握手方向用例 —— 需真实外设 consumer（SPI3/4 在本 board dts 里是注释态、未使能），
    本工程无可用从设备；且该方向需要 `soc_dma_channel_handshake_set()`，属外设通路（O-4 已知盲区）。
  - `dw_dma_suspend` / `dw_dma_resume` 成功路径 —— 见"未覆盖"表：在途窗口 µs 级，
    无法做出**确定性**判定（会 flaky），只覆盖状态守卫分支。
  - ERR 中断路径 —— 承既有结论（本 SoC 互联对被拒/未解码访问一律回 OKAY，实测不可注入），**不重复挖**。

## 修订（2026-09-18，测试有效性审计 A 组 P1-4）

四项"不可归因"断言的收口；每项均给出"驱动正确时必过 / 典型失效时必不过"的依据。

1. **`test_dma_invalid_channel`**（`dma_test.c`）
   - **原状**：cfg 用 `src == dst`，驱动**同址校验也返 `-EINVAL`** ⇒ 删掉通道越界守卫后
     本项**仍 PASS**，断言无法归因到"越界"这一维。且只要求"非 0"。
   - **改**：改用两个不同缓冲区；加**正对照**（同一 cfg 在合法 ch0 上必须被接受 == 0）；
     断言 ch99 返回**精确 `-EINVAL`**。
   - **论证**：正确 ⇒ 正对照 0 + ch99 `-EINVAL`，必过；越界守卫被删/失效 ⇒ ch99 会落到
     配置路径（返回 0 或别的 errno）⇒ 必不过。正对照挡住"cfg 本身非法"的误归因。
2. **`test_dma_channel_config_stop`**（`dma_test.c`）
   - **原状**：只断 3 个 `ret==0`；宣称的 `ACTIVE→IDLE 迁移`**不可观测**
     （256B 传输 µs 级完成 + 硬件自清 ChEn ⇒ `dma_stop` 走早退分支）。
   - **改**：开 `complete_callback_en`、等回调完成、`memcmp` 数据；`stop` 后再
     **重新 config/start + 再次 memcmp**（状态确已回到可配置态）。
   - **论证**：正确 ⇒ 两次传输数据相等且 stop/reconfig/start 均 0，必过；
     通道状态机不释放（reconfig `-EBUSY`）/数据串扰 ⇒ 必不过。
3. **`test_dma_abort_mid_transfer`**（`dma_destructive_test.c`）
   - **原状**："中途 abort"不可证 —— 早退与在途 disable 同返 0，`big_dst` 不查。
   - **改**：**如实标注只覆盖早退路径**（代码注释 + 本表路径列），并新增
     "重复 `dma_stop` 仍返回 0"（幂等早退路径的可证伪断言）。
   - **未做"断言 dst 未搬完"**：2047B M2M 传输在 stop 前多半已完成，该断言会 flaky；
     按纪律不编造。**未做"断言 ChEn 已清"**：`dw_dma_stop` 返回 0（非 `-ETIMEDOUT`）
     已蕴含 ChEn 清，直读寄存器需硬编码 DMAC 寄存器偏移，收益不抵脆弱性。
4. **`test_dma_suspend_resume_guards` 的 ch99 半段**（`dma_destructive_test.c`）
   - **原状**：删越界守卫后越界 `chan_data->state` 读为 0(=IDLE) 仍返 `-EINVAL`，
     断言照过、不可归因。
   - **改**：先令 **ch0 = ACTIVE**（config + start 2047B），再调 `suspend/resume(99)`，
     最后断言 ch0 状态未被破坏（`dma_stop(ch0) == 0`，该返回值要求 ch0 仍为 ACTIVE）。
   - **论证**：正确 ⇒ ch99 两调用 `-EINVAL` 且 ch0 不受影响 ⇒ 必过；若越界守卫缺失且
     `dev_data->chan[99]` 的越界访问落到 ch0 状态（同 cache line / 相邻布局）⇒
     `dma_stop` 不再返回 0 ⇒ 必不过。**局限（如实标注）**：越界地址是否恰好与 ch0 状态
     重叠取决于结构体布局，本项**提高**了可归因性但**不保证**在所有布局下都能抓到。
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
test_dma_invalid_channel | ret == -22 | contract:API 契约——ch99 返回精确 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_dma_invalid_data_size | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_dma_data_size_zero | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_dma_block_too_large | ret == -22 | contract:API 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
test_dma_self_copy_rejected | ret == -22 | contract:API 契约——同址显式拒绝（★无 INFO 通道 ⇒ NO_ACTUAL）
test_dma_loop_transfer | !none | !none:断言是 4 轮 256B 的 memcmp 合取，无单一标量
test_dma_transfer_ones_zeros | !none | !none:memcmp 合取
test_dma_channel_config_stop | !none | !none:三个返回码 + 数据逐字节相等的合取
test_dma_one_byte | !none | !none:1 字节送达 + 回调触发，非标量
test_dma_large_block | !none | !none:首尾字节 + 回调触发，非标量
```
