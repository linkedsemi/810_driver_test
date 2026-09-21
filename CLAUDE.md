# CLAUDE.md — 810_driver_test 快速上手指南

> **LSQSH 驱动批量自动化测试工程集**。`i3c_dualcore_test` 为首个样板工程。
>
> **分级路由**：碰硬件/构建 → runner agent；纯文档 → keeper agent；
> 纯代码分析 → 直接派 agent；纯查询不需要 agent。
>
> **强制 Skill 调用**：硬件/调试问题 → 先 Read `qsh-debug/SKILL.md`；测试问题 → `qsh-test-run`；
> 工程生成 → `qsh-test-gen`；调试方法论 → `embedded-debug`。禁止自行摸索。

## 1. 硬件环境

**板卡**：`lsqsh_evb@2os`（双核 AMP）
- cpu1 = target (uart1), cpu2 = controller (uart5)
- 接线：I3C5 ↔ I3C6 跨核对连；CKLink 连 JTAG

**串口**：非固化，需动态识别（MCP2200=cpu2, CH342=cpu1）。详见 [`HARDWARE_SETUP.md`](HARDWARE_SETUP.md)。

## 2. 测试工程

**双核**：i3c_dualcore_test(7), i2c_pure_test(1), i3c_i2c_switch_test(1), mbox_test(1)
**单核**：trng_test(1), rtc_ls_test(1), otbn_test(1), otbn_selftest(1), mbedtls_test(10), adc_temper_test(1), dma_test(1), flash_test(1), wdt_test(1), iopmp_test(1)

`(N)` = `regression/cases.yaml`（唯一权威源）中该工程**已启用**的 case 数。分型判定（单核 = 只有 cpu1 侧固件）
+ board/入口矩阵见 SPEC_REFERENCE §6.1.10；增删工程/case 的同步清单见 §6.4.5。
GPIO toggle 线程在 cpu1 侧自动编入（PA05/PA07，500ms），
配置集中于 `common/common_sources.cmake` + `common/gpio_toggle.c`。

## 3. 调用入口

```bash
# 全量回归
python regression/run_all.py                    # 预检 + 全部启用 case
python regression/run_all.py --no-precheck      # 跳过预检
python regression/run_all.py --dry-run          # 只列 case，不执行

# 单工程 / 单 case
cd 810_driver_test/projects/<driver>_test/tests
python run_test.py                                    # 单工程回归
python scripts/dbg_run.py --case case_<name> --timeout 35  # 单 case 调试
```

**输出归档**：`reports/<YYYY-MM-DD>-<idx>/`

## 4. 核心规范速查（必读）

> 完整规范见 [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md)。

### 4.1 固件编写

| 规则 | 说明 |
|------|------|
| **test_main() 不返回** | 必须 `k_thread_join(&dispatcher_thread, K_FOREVER)`，否则 LOG_PANIC → 8ms 死窗 |
| **ISR 禁打日志** | 只记账，dispatcher 线程汇总打印 |
| **统一 LOG_INF** | 走 `LOG_INF`（不用 printk），prj.conf 必带 `CONFIG_LOG_MODE_DEFERRED=y` |
| **公共结果模块** | 统一用 `common/test_results.c`（`tr_init/tr_record`），禁止私有 results |

### 4.2 构建与配置

| 规则 | 说明 |
|------|------|
| **统一 CMake 入口** | `include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)` |
| **工程分型选 board** | 单核 `@1os/lsqsh/cpu1`、双核 `@2os/lsqsh/cpu{1,2}`；入口走 `TARGET_ROM_START` 不硬编码（SPEC_REFERENCE §6.1.10） |
| **ztest 详细输出** | `CONFIG_ZTEST_VERBOSE_OUTPUT=y`，否则逐项 MISSING |
| **libc 三件套** | `CONFIG_PICOLIBC=y` + `CONFIG_NEWLIB_LIBC=n` + `CONFIG_MINIMAL_LIBC=n`（缺一构建失败，KNOWN_ISSUES #58） |
| **overlay 修改后** | 必须删 build 全量重建 |
| **复用收敛** | 多工程共用内容只改 `common/`（SPEC_REFERENCE §6.1.9） |

### 4.3 测试执行

| 规则 | 说明 |
|------|------|
| **结果判定** | 单核=全项 PASS + SUCCESSFUL；双核=同名项双核 PASS |
| **判定细节** | ⚠️ 各族口径**不完全相同**（24 份 `run_test.py` 分 **7 个**判定家族）。**改判定前先看 [SPEC_REFERENCE §6.2.4 判定规则总表（冻结）](SPEC_REFERENCE.md#624-判定规则总表冻结2026-09-18)** —— 那是唯一真源，含各族规则 + 「明确不做」清单 |
| **改哪一层要验什么** | 框架已分层（L3 编排 / L1 采集 / L0 契约 / L2 处理）。**改前先看 [FRAMEWORK_MAP.md](FRAMEWORK_MAP.md)** —— 它给出"改哪层只需离线重放、改哪层必须上板"的判据 |
| **串口无输出兜底** | `python scripts/dump_results.py --json` GDB 读内存。⚠️ 兜底有**独立性门**：`mem_state==ok` **且** 串口见 `ZT_BOOT`，缺一则 mem 项被剔除（防读到上一轮 RAM 残留） |
| **log 门禁** | load 后几秒内无输出直接判失败 |

## 5. 常见现象速查

| 现象 | 最可能原因 | 处理 |
|------|-----------|------|
| DebugServer 起不来 | FlashProgrammer 占用 | `taskkill` → 重启 DS |
| 串口只有 `\x00` | DS 连接残留 | gdb detach 一次 |
| **load 后串口 0 行** | 崩溃 / `$pc` 设错（单核脚本硬编码 2os 入口）/ cpu2 未 hold / overlay 缺 chosen；**带 eth1 工程**另疑 PHY 复位自旋挂死 | **先抓 CPU 再查配置**（KNOWN_ISSUES #38/#39/#40/#56/#57/#71） |
| load 极慢（~190s） | 写包参数未调大 | `set remote memory-write-packet-size 16384` |
| PASS/FAIL 相反 | ANSI 重绘前缀 | `re.search()` 不锚点 |
| 逐项 MISSING | verbose 输出关了 | `CONFIG_ZTEST_VERBOSE_OUTPUT=y` |

完整清单：[`KNOWN_ISSUES.md`](KNOWN_ISSUES.md)

## 6. 文档索引

| 主题 | 文档 | 何时查阅 |
|------|------|---------|
| **未决项台账** | [`OPEN_ITEMS.md`](OPEN_ITEMS.md) | **开工前先看**：待拍板/待验证项；每轮回复末尾须附其未决清单 |
| 硬件接线/环境配置 | [`HARDWARE_SETUP.md`](HARDWARE_SETUP.md) | 初次搭建 |
| **固件规范完整版** | [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) | **编写/修改固件时必读** |
| 已知坑完整清单 | [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) | 遇到问题时 |
| Supervisor agent | `.claude/agents/runner.md` | 理解路由逻辑 |
| Skill 清单 | `.claude/TOOLS_AND_SKILLS.md` | 需要特定 skill |
| 文档规范 | `.claude/DOC_EDIT_RULES.md` | 编辑 CLAUDE.md/skill 前必读 |
