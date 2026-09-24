# 全量回归过程记录 — 2026-09-24-1

> 本文件是 `reports/2026-09-24-1/` 的**过程记录**（区别于 `summary.md` 的结果汇总与
> `board-2026-09-24.md` 的板上验证记录）。

## 1. 任务与环境

| 项 | 值 |
|---|---|
| 任务 | 跑一遍全量回归（50 case），产出归到 `reports/2026-09-24-1/` |
| 策略 | `STRATEGY_TEST`（qsh-test-run → qsh-test-compare §4.1） |
| 硬件锁 | runner 独占，全程串行 |
| DebugServer | PID 14548（1025 LISTENING）；run_all 的 erase 步内**重启过一次**（正常流程） |
| 端口映射 | 预检自动发现：CTRL=**COM18**（MCP2200）、TGT=**COM16**（CH342） |
| 构建状态 | `projects/*/tests/*/build` 此前已全删 ⇒ 本批为**全量重建**（慢，属正常） |
| 采集窗口 | 各 case 使用其 `run_test.py` 默认 timeout（与 09-23-1 同口径） |

## 2. 手动环境预检（铁律 11，逐项手动，未委托脚本内置预检）

| # | 项 | 结果 |
|---|---|---|
| 1 | DebugServer 进程存在 | ✅ `DebugServerConsole.exe` PID 14548 |
| 2 | Port 1025 LISTENING | ✅ `0.0.0.0:1025 LISTENING 14548` |
| 3 | GDB 能连上（在线判定唯一依据） | ✅ `target remote 127.0.0.1:1025` → `[Inferior 1 (Remote target) detached]`，rc=0 |
| 4 | 串口数量够（排除 CKLink） | ✅ 4 个非 CKLink 口（COM5/16/17/18），**全部可打开**（G-ENV-COM5 不触发） |
| 5 | 无 FlashProgrammer 冲突 | ✅ 无 `cskyflash` |
| 6 | 无 1025 ESTABLISHED 残留 | ✅ 无 |

**6/6 通过**，方启动批量脚本。

## 3. 执行

```bash
cd D:/zephyr4_0/810_driver_test
python regression/run_all.py --date 2026-09-24     # 会话目录 = 2026-09-24-2（-1 已存在）
```

- 以 `nohup ... > tmp/full_reg_2026-09-24.log 2>&1 &` 启动（脱离 Bash 工具进程组，防长跑被回收，KNOWN_ISSUES #64）。
- **一次连续跑完**，无中断、无续跑（启动 22:25，结束 01:12，约 2h47m；全量重建 + 50 case）。
- `--dry-run` 先行确认清单 = **50 case**（49 + 新增 `rtc_timer_ls_test_case_default`）。
- 未使用 `--no-precheck`（预检该跑，脚本预检 PASS：DebugServer / CTRL boot COM18 / TGT boot COM16 均通过）。
- 预检产物已随会话保留：`precheck_report.md`、`port_mapping.json`。

### 心跳监控

- 每 ~10 min 检查一次：会话 `summary.md` 行数 + 日志尾部推进。
- **全程无 SUSPECT_STUCK**（无 >case_timeout 无增长）、无脚本崩溃、无外部中止。

## 4. 异常信号扫描（全部为空）

| 检查项 | 结果 |
|---|---|
| `error 138` / `DebuggerServer quit` / `could not connect` | 无 |
| `TRUNCATED_AFTER_ALL_ITEMS` | 无（**未出现**，采集窗口未偏紧） |
| `ENV-ABORT`（E-2 fail-closed） | 无 |
| `0/0` 计数 | 无 |
| `BUILD FAILED` / `ninja: error` | 无 |

- `audit_stale_elves` 对多个 case 打 WARN「ELF 缺哨兵符号 → 强制重建」—— 因 build 目录已删，
  **预期形貌**，非缺陷。

## 5. 结果

**50 个 case：PASS 47 / FAIL 3。**

| case | 结果 | 通过/总数 | 判定 |
|---|---|---|---|
| `i3c_i2c_switch_test_case_switch_core` | FAIL | 10/11 | **预期内**（`broadcast_nack_recovery`） |
| `spi_test_case_default` | FAIL | 18/19 | **预期内**（`link_probe`，外部回环跳线未接） |
| `spif_test_case_default` | FAIL | 17/18 | **预期内**（`cmd_idx_accessors`，已裁定已知基线） |

**无「预期之外」的 FAIL。** 3 个 FAIL 与 `reports/2026-09-23-1` **逐项逐字一致**（同项名、同 reason、同计数）。

### 重点观测项

| 项 | 结果 |
|---|---|
| `rtc_timer_ls_test_case_default`（新增工程首次进全量） | ✅ **PASS 15/15**，无异常（无首跑失败 ⇒ 未触发读 `$pc`/CSR 分支） |
| `qspi_flash_test_case_default` | ✅ **PASS 26/26** |
| `qspi_flash_test_case_no_calib` | ✅ **PASS 26/26** |

> qspi 两 case 均满分 —— 测试侧 `a576b98` + 驱动侧 `c0426596280` 的更新后，本批**未见回归**。

其余 46 个 case 结果与 `reports/2026-09-23-1` 修复后状态一致。

## 6. 失败归因（qsh-test-compare §4.1 固定收尾）

已跑 `python common/failures_report.py --session reports/2026-09-24-2` 生成骨架，agent 补齐
`⚠️` 处（实测值带 `文件:行号`），产物并入 `reports/2026-09-24-1/failures/`：

- `failures/i3c_i2c_switch_test_switch_core.md` — 归因「**无法判定（待驱动/硬件侧裁定）**」：
  「抢窗/时序」机制已被 2026-09-24 极端对照证伪；剩示波器 ACK 位 / SoC 广播无人应答是否置 ERRF
  两条线。证据 `serial_CTRL.log:46,59`。**未放宽判据**。
- `failures/spi_test_default.md` — 归因「**环境（物理接线）**」：`slave_ret=-119` + `timed_out=1`
  ⇒ 外部回环跳线未接。证据 `serial_TGT.log:91,95,96`。
- `failures/spif_test_default.md` — 归因「**已知基线（已裁定为设计）**」：`raw_dummy=0` vs 期望 9。
  证据 `serial_TGT.log:38,40,42`。

## 7. 会话目录收尾（当天单份权威惯例）

1. `2026-09-24-2/` 的 **50 个 case 目录** + `failures/` + `precheck_report.md` + `port_mapping.json`
   + `logs/` 并入 `2026-09-24-1/`（同名覆盖）。
2. 用框架入口 **`report_dir.update_summary`** 重算 `2026-09-24-1/summary.md`（union 旧 4 行 + 新 50 行）
   —— 结果 **50 行**，47 PASS / 3 FAIL。**未手写** summary。
3. **保留** `2026-09-24-1/board-2026-09-24.md`（今天板上验证记录，非回归产物）。
4. `2026-09-24-2/` 已删除。

> 注：`-1` 原有的 4 个 case 目录（`app_iwdg_cpu2_test_case_{non_destructive,quick_enable,quick_invalid}`
> 与 `i3c_i2c_switch_test_case_switch_core`）均属本批 50 case，已按「同名覆盖」被本轮回归结果替换。

## 8. 坑沉淀决策

- 本批**无新踩坑**：无失败恢复动作、无疑难定位、无人工纠偏 ⇒ **无 INBOX 条目**。
- 三个 FAIL 均为**已登记基线**，归因见 §6 与 `failures/`。
