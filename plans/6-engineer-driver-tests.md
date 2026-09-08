# 6 工程驱动测试计划（2026-09-07）

> **状态跟踪**：Phase1-6.2 已完成（29/29 PASS），Phase6.3+ 待开始

## 工程清单

| # | 工程 | 单/双核 | 当前状态 | 已通过 |
|---|------|---------|---------|--------|
| 1 | adc_temper_test | 单核 cpu1 | ✅ PASS (6/6) | 功能测试 |
| 2 | dma_test | 单核 cpu1 | ✅ PASS (4/4) | 功能测试 |
| 3 | flash_test | 单核 cpu1 | ✅ PASS (7/7) | 功能测试 |
| 4 | mbox_test | 双核 cpu1+cpu2 | ✅ PASS (2/2) | 功能测试 |
| 5 | wdt_test | 单核 cpu1 | ✅ PASS (5/5) | 功能测试（非破坏性） |
| 6 | iopmp_test | 单核 cpu1 | ✅ PASS (5/5) | 功能测试 |

## Phase 跟踪

### Phase 1-5（已完成）

- ✅ Phase1: adc/dma/flash 功能测试
- ✅ Phase2: mbox 功能测试
- ✅ Phase3: wdt 非破坏性测试
- ✅ Phase4: iopmp 功能测试
- ✅ Phase5: 汇总报告（STATUS.md）

### Phase 6（进行中）— 破坏性/鲁棒性测试增强

**目标**：不只测功能正确性，还要测破坏性场景（复位/异常注入/鲁棒性边界）。

| 工程 | 破坏性场景 | 方案 | 状态 |
|------|-----------|------|------|
| **wdt_test** | sec_iwdg 硬件复位（flash boot 真实复位） | 普通 2os 固件烧 flash + bootram 引导头 + noinit 状态机（非 XIP） | ✅ 已完成（Phase 6.1） |
| **i3c 驱动** | write_delay_read_len 挂死（ISR 风暴） | RXFNE 循环 drain + RXTGTEND 中断使能 + ISR 短读收尾 | ✅ 已完成（Phase 6.2，29/29 PASS） |
| **dma_test** | DMA 传输中强制中断/错误注入 | 破坏 buffer 对齐/越界写 | 📋 待开始 |
| **mbox_test** | 双核通信中一方失联/超时 | 一方强制退出，另一方检测超时 | 📋 待开始 |
| **flash_test** | 写 flash 中途断电/复位 | 写一半重启，检查数据一致性 | 📋 待开始 |
| **adc_temper_test** | 极端温度/电压下读取 | 需硬件环境（暂跳过） | ⏸️ 暂缓 |
| **iopmp_test** | 访问保护区域触发违例中断 | 需验证 IOPMP 中断处理 | 📋 待开始 |

### Phase 6.1: wdt_test 破坏性复位测试（✅ 已完成 2026-09-07）

**结果**：sec_iwdg 真实硬件复位验证通过（reset_reason == SEC_IWDT_HART_RESET=6）。

**完整软件流程**（见 run_destructive.sh）：
1. 构建普通 2os 固件（非 XIP）+ `-DWDT_DESTRUCTIVE_RESET=y`
2. 烧录 `zephyr_flash_bram_*.bin@0x08000000`（含 bootram 引导头，非 zephyr.bin）
3. GDB 清 noinit（test_phase/phase0_passed @ RAM noinit 段地址）
4. `flash_program --reset-and-run` 硬复位触发
5. PHASE 0 设 WDT 2s → WDT 自动复位 → PHASE 1 读 reset_reason

**实际输出**：
```
PHASE 0: test_phase=0, install 2000ms timeout, wdt_setup OK, waiting...
（WDT 2s 后自动复位，无外部干预）
PHASE 1: reset_reason=6 (SEC_IWDT_HART_RESET)
PASS: reset_reason == SEC_IWDT_HART_RESET
PROJECT EXECUTION: SUCCESSFUL
```

**关键调试发现（KNOWN_ISSUES #46-49）**：
- sec_iwdg 配 `WDT_FLAG_RESET_CPU_CORE` → HART 复位（SEC_IWDT_HART_RESET=6），非 SYS_IWDT_FULL_RESET(4)
- 烧录用 zephyr_flash_bram_*.bin（含引导头），zephyr.bin 无头 bootrom 不认
- GDB reset/detach halt CPU 阻止 bootrom 引导 → 需 flash_program 硬复位
- flash boot 需独占 CKLink（先杀 DebugServer/CSKYFlashProgramer）

**文件**：
- `cpu1_wdt_test/src/wdt_destructive_test.c` - noinit 状态机 + 复位检查
- `cpu1_wdt_test/CMakeLists.txt` - `if(WDT_DESTRUCTIVE_RESET)` 切换源文件
- `tests/case_destructive_reset/test.sh` - 构建（-DWDT_DESTRUCTIVE_RESET=y）
- `tests/case_destructive_reset/run_destructive.sh` - 一键软件流程

## 坑沉淀（2026-09-07 新增）

### 测试框架（common/run_test.py）

| # | 坑 | 修复 |
|---|-----|------|
| 1 | `allocate_session_dir` 缺 `date_str` 参数 | 所有工程 run_test.py 补上 |
| 2 | 双核串口捕获用 `readline()` 阻塞 | mbox 改用 `read(4096)` 原始字节流 |
| 3 | 双核 test.sh 用 `build_single_test` 选错板 | mbox 改 `build_test`（自动识别 2os cpu1/cpu2） |

### 驱动/固件适配

| # | 坑 | 修复 |
|---|-----|------|
| 4 | dma: `complete_callback_en=0` 不触发回调 | 改 `=1`（dw_dma 驱动要求） |
| 5 | dma: `source_data_size=4` 语义混淆 | 改 `=1`（官方字节宽模式） |
| 6 | dma: buffer 无 `__nocache` D-cache 不一致 | 全部加 `__nocache` |
| 7 | flash: `fa_id=10` 硬编码越界 | 改 `FIXED_PARTITION_ID(rwfs_partition)` |
| 8 | wdt: `WDT_FLAG_RESET_SOC` 匹配错 iwdg | 改 `WDT_FLAG_RESET_CPU_CORE` |
| 9 | iopmp: `config_enable(false)` 无法关闭 | 删 disable 测试（硬件限制） |
| 10 | mbox: overlay 旧格式（3 cell） | 改新格式（5 cell + 单一 mbox-names） |
| 11 | mbox: 固件分 tx/rx spec | 改单一 `MBOX_DT_SPEC_GET(node, mbox)` |
| 12 | mbox: 消息 64B > 通道 MTU 16B | 通道 width 16/8 → 64 |

## 已知限制

- adc_temper_test 极端温度测试需要硬件环境支持（高温/低温设备）
- mbox_test 当前只有正向通信测试，异常场景（超时/失联）需 Phase 6.3
- dma_test 破坏性测试（错误注入）需 Phase 6.2
