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
| case_default | 默认 | `dma` | 功能通路 ×2 + 通道生命周期 ×1 + 参数校验 ×1 = 4 |
| case_destructive | `-DDMA_DESTRUCTIVE=1` | `dma_destructive` | 参数校验 ×3 + 边界传输 ×2 + 自拷贝 ×1 = 6 |

`test.sh` 两 case 均走 `test_common.sh` 的 `SINGLE_BOARD`（1os）+ `load_and_run_single`，
board 与加载入口同源（validate §1.9f / KNOWN_ISSUES #57）。

## 覆盖矩阵

### case_default（套件 `dma`，4 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_dma_loop_transfer | `dw_dma_config`（M2M 分支）→ `dw_dma_start`（CTL/SAR/DAR 写入）→ `dw_dma_isr`（BLOCK/DST_TRAN/TFR 分支） | 功能通路 + 重复重载 | 4 轮 256B M2M 逐字节相等 | `zephyr/tests/drivers/dma/loop_transfer` 官方样例语义 | ✅ |
| test_dma_transfer_ones_zeros | 同上，`complete_callback_en=1` 连续两次 config/start | 功能通路 + 数据模式边界 | 全 0x00 / 全 0xFF 256B 逐字节相等 | 同上 | ✅ |
| test_dma_channel_config_stop | `dw_dma_config` → `dw_dma_start` → `dw_dma_stop`（ACTIVE→IDLE 迁移） | 通道生命周期 | 三步返回码均 0 | `dma_driver_api` 契约 | ✅ |
| test_dma_invalid_channel | `dw_dma_config` 入口 `channel >= DW_CHAN_COUNT` 分支 | 异常参数 | 返回非 0（`-EINVAL`） | `dma_dw_common.c` L181 | ✅ |

### case_destructive（套件 `dma_destructive`，6 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_dma_invalid_data_size | `dw_dma_config` 的 `source_data_size` ∈ {1,2,4,8,16} 校验分支 | 异常参数 | 返回非 0（`-EINVAL`） | `dma_dw_common.c` L204 | ✅ |
| test_dma_data_size_zero | 同上（data_size=0 落非法分支） | 异常参数 | 返回非 0（`-EINVAL`） | `dma_dw_common.c` L204 | ✅ |
| test_dma_block_too_large | `dw_dma_config` 的 `block_size > DW_CTLH_BLOCK_TS_MASK` 拒绝分支 | 异常参数（边界常量） | 返回非 0（`-EINVAL`） | 常量 = **实测写-回读上限 0x7FF**（`dma_dw_common.h` `CONFIG_DMA_DW_2_20A` 分支；HAL 头 0xfff 与实测不符，见 notes） | ✅ |
| test_dma_self_copy_rejected | `dw_dma_config` 逐 block 的 `source_address == dest_address` 拒绝分支 | 异常参数（同址退化配置） | `dma_config` 返回 `-EINVAL`，缓冲区内容未被改动 | `dma_dw_common.c` 同址显式拒绝（设计选择：由驱动显式校验，不靠状态机副作用） | ✅ |
| test_dma_one_byte | config → start → isr TFR | 边界（最小传输） | 1 字节 0xAB 送达 + 回调触发 | `dma_driver_api` 契约 | ✅ |
| test_dma_large_block | config → start → isr TFR | 边界（单块上限） | 2047B（`BLOCK_TS[10:0]` 最大值）首尾字节正确 + 回调触发 | 上限实测：>2047 只搬 `n & 0x7FF` 次，2048 截断为 0 → 永久挂住 | ✅ |

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
| 外设握手方向（M2P/P2M）、SG 多块（`block_count>1`） | 本工程只覆盖 M2M 单块；`CONFIG_DMA_DW_LLI_POOL_SIZE=2` | 未覆盖 |
| `dw_dma_suspend` / `dw_dma_resume` | 无调用方，未构造用例 | 未覆盖 |

## 参考

- 根因与实测数据：`tests/notes/DMA_DW_BLOCK_TS_MASK_AND_TFR_STATE_ROOTCAUSE.md`
- **ERR 路径注入尝试（IOPMP 触发器）实测记录**：`tests/notes/DMA_DW_ERR_PATH_INJECTION_ATTEMPT.md`
- 驱动源码：`zephyr/drivers/dma/dma_dw_common.{c,h}`
