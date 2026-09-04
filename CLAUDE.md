# CLAUDE.md — 810_driver_test 快速上手指南

> **LSQSH 驱动批量自动化测试工程集**。`i3c_dualcore_test` 为首个样板工程。
>
> **⚠️ 分级路由**：**碰硬件/碰回归构建目录**的任务（烧录、串口、调试、回归、
> 固件开发）**必须**通过 supervisor agent 执行（硬件串行化 + 断点续跑）；
> **纯文档、纯代码分析**等不碰硬件、不碰构建目录的任务，主会话可直接派独立
> agent 并行执行。纯查询（"这个函数在哪"）不需要 supervisor。

---

## 1. 硬件环境

**板卡**：`lsqsh_evb@2os`（双核 AMP）
- cpu1 = target (uart1)
- cpu2 = controller (uart5)
- 接线：I3C5 ↔ I3C6 跨核对连；CKLink 连 JTAG

**串口映射**：**非固化**，需动态识别
- 排除：CKLink Serial（调试器）
- 识别：MCP2200=controller(cpu2), CH342=target(cpu1)
- 方法：按 VID:PID 识别，详见 [`HARDWARE_SETUP.md`](HARDWARE_SETUP.md)

详细配置：[`HARDWARE_SETUP.md`](HARDWARE_SETUP.md)

---

## 2. 测试工程

**双核**：i3c_dualcore_test(8), i2c_pure_test(1), i3c_i2c_switch_test(1)
**单核**：trng_test(1), rtc_ls_test(1), otbn_test(1), otbn_selftest(1), mbedtls_test(10)

完整 case 清单：`regression/cases.yaml`（`#` 注释 = 屏蔽）

**公共功能**：cpu1 侧（单核工程 + 双核 target 侧）自动包含 GPIO toggle 线程（PA05 高优先级 + PA07 低优先级，500ms 周期），
用于示波器观察系统负载和时序；cpu2 侧不编入（AMP 共享 GPIOA 防双核并发驱动）。
配置集中在 `common/common_sources.cmake`（按 BOARD_QUALIFIERS 条件注入）、
`common/gpio_toggle.c`、`common/gpio_toggle.dtsi`（overlay 使能块唯一来源，app.overlay 用 `#include` 引用）。

---

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

---

## 4. 核心规范速查（必读）

> 完整规范见 [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md)，以下为**最常用、最易踩坑**的核心规则：

### 4.1 固件编写（血泪教训）

| 规则 | 说明 |
|------|------|
| **test_main() 不返回** | 必须 `k_thread_join(&dispatcher_thread, K_FOREVER)`，否则 LOG_PANIC → 8ms 死窗 |
| **ISR 禁打日志** | ISR 内禁止 `LOG_INF/LOG_ERR/printk`，只记账，dispatcher 线程汇总打印 |
| **时序测量禁日志** | 用 `k_cyc_to_us_floor32`，不打 LOG 不拉 IO |
| **统一 LOG_INF** | 所有 log/token 输出走 `LOG_INF`（不用 printk），prj.conf 必带 `CONFIG_LOG_MODE_DEFERRED=y` |
| **公共结果模块** | 禁止私有 results 数组，统一用 `common/test_results.c`（`tr_init/tr_record`） |

### 4.2 构建与配置

| 规则 | 说明 |
|------|------|
| **统一 CMake 入口** | 用 `include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)` |
| **ztest 详细输出** | prj.conf 必须 `CONFIG_ZTEST_VERBOSE_OUTPUT=y`，否则逐项结果全 MISSING |
| **libc 选择** | 必须 `CONFIG_NEWLIB_LIBC=n`（与 Zephyr POSIX types 冲突） |
| **overlay 修改后** | 必须删 build 全量重建（Kconfig 缓存坑） |
| **复用收敛** | 多工程共用内容只改 `common/` 公共层，禁止私拷副本（SPEC_REFERENCE §6.1.9） |

### 4.3 测试执行

| 规则 | 说明 |
|------|------|
| **结果判定** | 单核=全项 PASS + `PROJECT EXECUTION SUCCESSFUL`；双核=同名项双核 PASS |
| **串口无输出兜底** | 用 `python scripts/dump_results.py --json` GDB 读内存取结果 |
| **log 门禁** | load 后几秒内必须出 log/ztest 输出，否则直接判失败 |

---

## 5. 常见现象速查

| 现象 | 最可能原因 | 立即处理 |
|------|-----------|---------|
| DebugServer 起不来 "CKLINK may be used" | FlashProgrammer 占用 | `taskkill` FlashProgrammer → 重启 DS |
| 串口 0 行，只有 `\x00` | DebugServer 连接残留 | gdb detach 一次；不行重启 DS |
| load 极慢（~190s） | 写包参数未调大 | `set memory-write-packet-size 16384`（41x 提速） |
| PASS/FAIL 与预期相反 | ANSI 重绘 `uart:~$` 前缀 | `re.search()` 不锚点 |
| 逐项全 MISSING | `CONFIG_ZTEST_VERBOSE_OUTPUT=n` | prj.conf 设 `=y` |

完整速查表（36条）：[`KNOWN_ISSUES.md`](KNOWN_ISSUES.md)

---

## 6. 规范文档索引

| 主题 | 文档 | 何时查阅 |
|------|------|---------|
| 硬件接线/环境配置 | [`HARDWARE_SETUP.md`](HARDWARE_SETUP.md) | 初次搭建环境 |
| **固件规范完整版** | [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) | **编写/修改固件时必读** |
| 已知坑完整清单 | [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) | 遇到问题时 |
| Supervisor agent 定义 | `.claude/agents/supervisor.md` | 需要理解路由逻辑 |
| Skill 清单 + 调用方式 | `.claude/TOOLS_AND_SKILLS.md` | 需要特定 skill |

---

## 7. 文档修改规则

**📖 完整规范**：[`.claude/DOC_EDIT_RULES.md`](.claude/DOC_EDIT_RULES.md) - 文档编辑规范（通用，必读）

**🔒 保护文档**（仅结构变更时可改）：
- `.claude/CLAUDE.md` - skill 库便携说明
- `common/common_sources.cmake` - 公共源文件配置

**📝 可更新文档**：
- 本文件、`SPEC_REFERENCE.md`、各 `SKILL.md`

**核心原则**：
- CLAUDE.md 保持精简（100-150行），详细内容用链接引用
- 单一信息源：同一信息只在一个地方维护
- 核心规范速查：CLAUDE.md §4 只列最常用/易踩坑的规则

修改前确认影响范围，避免破坏跨环境兼容性。
