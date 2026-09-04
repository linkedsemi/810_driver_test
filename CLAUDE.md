# CLAUDE.md — 810_driver_test 快速上手指南

> **LSQSH 驱动批量自动化测试工程集**。`i3c_dualcore_test` 为首个样板工程。
> **⚠️ Supervisor 强制路由**：本目录下任何**非单次问答**的任务（回归、调试、开发、分析），
> **必须**通过 supervisor agent 执行（CLAUDE.md §自动路由规则）。
> 纯查询（"这个函数在哪"、"读一下这个文件"）不需要 supervisor。
> 详见 `.claude/agents/supervisor.md` + `.claude/TOOLS_AND_SKILLS.md`。

---

## 1. 板级要求

- 板卡：`lsqsh_evb@2os`（双核 AMP），cpu1=target(uart1)，cpu2=controller(uart5)
- 接线：I3C5 ↔ I3C6 跨核对连；CKLink 连 JTAG；两路 UART 115200 8N1

### COM 映射

| COM | 设备 | 角色 | 注意 |
|-----|------|------|------|
| COM14 | CKLink Serial (32BF:B210) | 调试器 | **排除**，不是目标口 |
| COM15 | MCP2200 (04D8:00DD) | controller (cpu2) | |
| COM16/17 | CH342 (1A86:55D2) | target (cpu1) | 口号以设备管理器为准 |

### DebugServer

| 项 | 值 |
|----|---|
| GDB | `riscv64-unknown-elf-gdb`（无 Python） |
| Server | `127.0.0.1:1025` ← PC 进程 `C:\uaer_app\debug_server\bin\DebugServerConsole.exe` |
| 互斥 | DebugServer ↔ FlashProgrammer 同时只能一个占 CKLink |

### 环境预检

跑测试前确认：无 FlashProgrammer 残留 → DebugServer 在线（GDB 连一次判） → 串口空闲 → 批量跑 `python regression/precheck/run_precheck.py`

---

## 2. 测试内容

| 工程 | 单/双核 | case 数 | 测什么 |
|------|---------|---------|--------|
| `i3c_dualcore_test` | 双核 | 8 | I3C 主从跨核通信（pure/mixed，400k~12.5M） |
| `i2c_pure_test` | 双核 | 1 | I2C 纯模式跨核对连 |
| `i3c_i2c_switch_test` | 双核 | 1 | I3C/I2C 动态切换 |
| `trng_test` | 单核 | 1 | 真随机数熵源 |
| `rtc_ls_test` | 单核 | 1 | RTC 设置/读取/闹钟 |
| `otbn_test` | 单核 | 1 | OTBN 硬件 ECC |
| `otbn_selftest` | 单核 | 1 | OTBN 底层自检 |
| `mbedtls_test` | 单/双核 | 10 | mbedtls/wolfssl + OTBN 加速 |
| `gpio_toggle_test` | 单核 | 1 | 示波器观察，**无自动化脚本** |

> case 清单：`regression/cases.yaml`（`#` 注释 = 屏蔽）

---

## 3. 调用入口

### 全量回归
```bash
python regression/run_all.py                # 预检 + 全部启用 case
python regression/run_all.py --no-precheck  # 跳过预检
python regression/run_all.py --dry-run      # 只列 case，不执行
```
流程：擦 flash → 预检 → 逐 case 调 run_test.py → 日志存 `session/logs/` → 汇总。单 case FAIL 不阻断。

### 单工程 / 单 case
```bash
cd 810_driver_test/projects/<driver>_test/tests
python run_test.py                                    # 单工程回归
python scripts/dbg_run.py --case case_<name> --timeout 35  # 单 case 调试
```

### 输出归档
- 会话目录：`reports/<YYYY-MM-DD>-<idx>/`
- 公共模块：`common/report_dir.py`（`allocate_session_dir` / `case_dir_for` / `update_summary`）

---

## 4. 常见现象速查表

| 现象 | 最可能原因 | 立即处理 |
|------|-----------|---------|
| DebugServer 起不来 "CKLINK may be used" | FlashProgrammer 占用 | `taskkill` FlashProgrammer → 重启 DS |
| 串口 0 行，只有 `\x00` | DebugServer 连接残留 | gdb detach 一次；不行重启 DS |
| load 极慢（~190s） | 写包参数未调大 | `set memory-write-packet-size 16384`（41x 提速） |
| 间歇性 WDR/时序异常 | DS 新实例不稳定期 | 重启后等 ~30s 稳定期 |
| PASS/FAIL 与预期相反 | ANSI 重绘 `uart:~$` 前缀 | `re.search()` 不锚点 |
| 改 overlay 后行为仍像旧的 | Kconfig 缓存 | 删 build 全量重建 |
| COM 口被占用 | 残留 python 进程 | `taskkill` 对应 PID |
| 逐项全 MISSING | `CONFIG_ZTEST_VERBOSE_OUTPUT=n` | prj.conf 设 `=y` |

---

## 5. 规范参考

| 主题 | 文档 |
|------|------|
| 固件规范 / 结果判定 / 覆盖矩阵 / XIP 烧录 | [`SPEC_REFERENCE.md`](SPEC_REFERENCE.md) |
| 已知坑完整清单（36 条） | [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) |
| Supervisor agent 定义 | `.claude/agents/supervisor.md` |
| Skill 清单 + 调用方式 | `.claude/TOOLS_AND_SKILLS.md` |
