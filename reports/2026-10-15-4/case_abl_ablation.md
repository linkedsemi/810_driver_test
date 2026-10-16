# case_abl 消融报告 —— 客户 200 ms/行 是否由日志/shell 配置引起？

- 会话：`reports/2026-10-15-4/`
- 工程：`810_driver_test/projects/i2cf_test/tests/case_abl/`
- 日期：2026-10-15
- 板：`lsqsh_evb@1os/lsqsh/cpu1`（单核 cpu1），串口 COM16/CH342，115200
- 口径：`SCAN_BURST_BEGIN` / `SCAN_BURST_END` 两条 `LOG_INF` marker 的**串口到达时间差** = 排空跨度；
  每行耗时 = 排空跨度 ÷ 两 marker 之间 `address beyond whitelist` 行数。

## 1. 结论（一句话）

**没有任何一项把每行耗时推高。** 全部组落在 **11.0 / 12.3 ms/行**（基线同批复现 12.26、12.26）。
B1/B2/B3/B4 甚至与基线**逐字节同一 ELF**（不是"接近"，是同一个二进制）——因为那些"漏掉的客户配置项"
在 810 的 Kconfig 里**本来就是客户的值（默认值）**，根本没有可消融的变量。**不能复现 200 ms/行。**

## 2. 消融表（同批基线）

| 组 | 新增项 | 业务日志行数 | 排空跨度 | 每行耗时 | bus_timeout | try_recovery | 备注 |
|---|---|---|---|---|---|---|---|
| **基线**（run1） | 上次"客户配置"conf | 112 | 1.373 s | **12.26 ms** | 0 | 0 | — |
| **基线**（run2） | 同上（复现） | 112 | 1.373 s | **12.26 ms** | 0 | 0 | 逐位一致 |
| **B1** | `LOG_RUNTIME_FILTERING=y` | 112 | 1.373 s | **12.26 ms** | 0 | 0 | **.config 与基线 0 行差异、ELF md5 相同** ⇒ 同一二进制，实测值=基线 |
| **B2** | `SHELL_VT100_COLORS=y`+`LOG_BACKEND_SHOW_COLOR=y` | 112 | 1.373 s | **12.26 ms** | 0 | 0 | 同上（本来就 =y） |
| **B3** | `SHELL_ECHO_STATUS=y`+`SHELL_PRINTF_BUFF_SIZE=30`+`LOG_TRACE_SHORT_TIMESTAMP=y` | 112 | 1.373 s | **12.26 ms** | 0 | 0 | 同上（本来就 =y / =30） |
| **B4** | B1+B2+B3 全一起 | 112 | 1.372 s | **12.25 ms** | 0 | 0 | 同上（ELF md5 = 基线） |
| **B5** | 基线 + **由 shell 命令触发扫描**（外壳忙） | 111 | 1.373 s | **12.37 ms** | 0 | 0 | 无升高 |
| **B6**(补充) | 基线 + B1..B3 + 其余可表达未照抄项全并集 | 112 | 1.230 s | **10.98 ms** | 0 | 0 | 略低（噪声内） |

`ELF md5`：基线/B1/B2/B3/B4 全部 = `bbd3d6347ad870fae5c380fc339d855d`（同一二进制）。

## 3. 为什么 B1-B4 是"空消融"（前提被实测推翻）

任务书假设这些是"上次照抄**漏掉**的客户配置项"。实测 **810 的默认值本来就等于客户的值**：

| 符号 | 客户值 | 810 基线（未显式设置）| Kconfig 默认 |
|---|---|---|---|
| `LOG_RUNTIME_FILTERING` | y | **y** | `default y`（`Kconfig.filtering`） |
| `SHELL_VT100_COLORS` | y | **y** | `default y if !SHELL_MINIMAL` |
| `LOG_BACKEND_SHOW_COLOR` | y | **y** | — |
| `SHELL_ECHO_STATUS` | y | **y** | `default y` |
| `LOG_TRACE_SHORT_TIMESTAMP` | y | **y** | `default y` |
| `SHELL_PRINTF_BUFF_SIZE` | 30 | **30** | `default 30` ← 客户值就是默认 |
| `LOG_PROCESS_TRIGGER_THRESHOLD` | 10 | **10** | `default 10` ← 客户值就是默认 |
| `SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE` | 64 | **64** | 默认 |
| `SHELL_BACKEND_SERIAL_{INTERRUPT,API_INTERRUPT}_DRIVEN` | y | **y** | 默认 |

⇒ 基线 conf（`case_abl/abl_00_baseline.conf`）**已经逐项等于客户**，加不加这几行 `.config` 一模一样
（`diff` 0 行、ELF md5 相同）。**"照抄漏掉"这个前提在日志/shell 配置上是假的。**

## 4. 客户 .config 与 810 基线的**真实**差异（LOG/SHELL/CONSOLE/UART 面）

对 `tmp/.config` 与基线 `.config` 做全量键比较（两边都有但取值不同者）：

- **`LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP=y`**（客户）vs `=n`（810）—— 客户自定义时间戳格式器。
  **本组留白**（见 §5）。
- `SHELL_PROMPT_UART`：`"pfr: "`（客户）vs `"uart:~$ "`（810）—— 已由 B6 覆盖，无影响。
- `SHELL_STACK_SIZE`：4096（客户）vs 2048 —— 已由 B6 覆盖，无影响。
- **`CONFIG_BOARD`：`prot2` + `lsqsh/cpu2`**（客户）vs `lsqsh_evb` + `lsqsh/cpu1`（810）——
  **客户根本在另一块板、另一颗核上**；`NUM_OS=2`、`SRAM_BASE=0x10000000`、`XIP=n` 等内存布局全不同。
- **UART 驱动不同**：客户 `UART_NS16550`，810 `UART_LINKEDSEMI`。
- 客户额外模块：`I2C_SHELL=y` / `SMBUS_SHELL=y` / `FLASH_SHELL=y` / `MBEDTLS` / `MBOX` / `NET_BUF` /
  `SDHC` / `HWINFO` / `LED` / `UDC/USB` / `EVENTS` / `POSIX_TIMERS`（`MAX_TIMER_COUNT=32`）。
- `TIMESLICE_SIZE=1`（客户，开启时间片轮转）vs 0（810）。
- `SPI_FILTER_INTERRUPT_LOG`：810=y / 客户=n（仅 SPI 滤波路径，与本 i2c ERR 日志无关，已排除）。
- 各模块 `*_LOG_LEVEL` 差异（客户把许多模块压到 WRN/ERR）。

## 5. `CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP` 留白说明

**未测。** 该项是客户**自定义的时间戳格式器**（`LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP=y` 需要客户
自己的 formatter 实现；810 无该实现，置 y 只能回落到默认格式）。复刻它需要客户的源码（已另向用户索取）。
⇒ 本项**留白**，不纳入消融表。但注意：它**只改时间戳的渲染文本**，不引入定时/节流，
在 810 的"生成时刻紧发、排空受带宽与配置约束"的测量里**结构上不可能把排空推慢到 200 ms/行**
（它最多改变每行字节数）。

## 6. `case_default` 未受影响（实测）

- 不加任何 case conf 构建 `case_default`：**rc=0、0 warning、0 error**。
- `.config`：`# CONFIG_I2CF_SCAN_BURST is not set`、`# CONFIG_I2CF_SCAN_BURST_SHELL`（SHELL=n 下不可见）、
  `# CONFIG_SHELL is not set`。
- `nm zephyr.elf | grep -c scanburst` = **0**（新命令符号不存在）。
- `CONF_FILE`（`CMakeCache.txt`）= 仅 `cpu1_i2cf_test/prj.conf`（无 case conf）。
- 各消融 case 的 `CONF_FILE` 均 = `prj.conf;case_abl/<该组自己的 conf>`（构建日志见 `raw/build_*.log`）。

## 7. 环境异常（必须记录）：硬件被另一 runner 并发占用

采集期间发现 **另一 runner 正在同一板子上跑回归**（`tmp/cap_thr_1015.py` → `tmp/build_thr_1015`
的 mbedtls throughput；随后 `810_driver_test/projects/mbedtls_test/tests/run_test.py --case case_hw_all
--session-dir reports/2026-10-15-5`）。期间：

- 一次基线采集（首次 `run_abl_00_baseline_2`）**只采到 32 行、无 marker**（对方 gdb 抢 DS 1025 /
  把 CPU halt）——该轮作废，已在板子空档重采，得 12.26 ms（`run_abl_00_baseline_2.log` 为**重采成功**那轮）。
- 这与 CLAUDE.md「同一时刻只允许一个 runner 占用硬件」冲突，**双方结果都有被污染风险**。
  本报告的硬件轮次均在确认对方 gdb 不在线、board 空档时进行（`gdb=0 python=0` 轮询）。

## 8. 仍未排除的因素（200 ms/行 的真正来源）

排空跨度口径下，日志/shell 配置**全部排除**。仍可能造成客户"200.2 ms/行"的因素：

1. **不是排空、而是扫描速率**（最可疑）：客户 200.2 ms ≈ `CONFIG_LS_I2C_BUS_TIMEOUT_MS` 默认 **200**。
   若客户**每条被拦事务**等满总线超时，则 112 条 ERR **本身就是 200 ms 间隔生成**的（112×200 ms ≈ 22.4 s）。
   注意：810 已用 **`case_quick_scan`** 直接测过（`reports/2026-10-14-3`）——**810 上被拦的 0 长度
   quick command 仅 0.14 ms/笔、0 次 bus timeout**，且在 810 上**未复现** 200 ms。⇒ 若成立，是**客户板/通路**
   的差异（NACK/完成信号行为），非 810 可配出。
2. **客户平台整体**（`prot2` 板 / cpu2 / 双 OS / 全套 BMC 固件 + 时间片轮转）：系统负载、线程优先级、
   `TIMESLICE_SIZE=1` 抢占等可能改变 log 线程被调度到的频率。810 单核 minimal 应用无法表达。
3. **客户 UART 驱动不同**（NS16550 vs linkedsemi）：TX 中断/流控行为不同，可能改变 shell backend 逐行节流。
4. **`CONFIG_LOG_OUTPUT_FORMAT_CUSTOM_TIMESTAMP`**（留白，§5）——只改文本，理论上不引入 200 ms。
5. **客户测量口径本身**：客户给的是"日志时间戳"间隔。若那是**生成时刻**（deferred 下自带时间戳=生成时刻），
   则 200.2 ms 就是 §8.1 的**扫描速率**，与日志排空无关。**需要客户提供原始日志（含到达时间戳）才能定案。**

## 9. 复现命令

```bash
cd 810_driver_test
bash projects/i2cf_test/tests/case_abl/test.sh abl_00_baseline.conf      # 基线
(cd projects/i2cf_test/tests && python scripts/dbg_run.py --case case_abl --timeout 45 --raw --tgt-uart COM16)
# B5：python projects/i2cf_test/tests/case_abl/run_b5.py --timeout 60 --tgt-uart COM16 --cmd scanburst
# 指标：python projects/i2cf_test/tests/case_abl/parse_abl.py <log>
```

## 10. 产物

`raw/`：`run_abl_*.log`（带 `+秒` 到达时间戳）、`build_abl_*.log`、`config_abl_*.config`、
`elfmd5_abl_*.txt`、`abl_summary.txt`。
