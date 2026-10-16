# I2CF 状态枚举矩阵（填满）— 2026-10-15-3

> 任务：把 I2CF 的状态枚举矩阵填满 —— 白名单内/外 × 读/写 × 有/无器件 × 滤波器开/关 → 反馈状态。
> 靶心 = #5「白名单内 + 无器件」：滤波器报不报？用以区分"按白名单判"与"按总线应答判"。
> 结论一句话见文末 §5。

## 1. 环境（本次实测）

| 项 | 值 |
|---|---|
| 板卡 | lsqsh_evb@1os/lsqsh/cpu1（单核） |
| 串口 | COM16（CH342-B，cpu1 uart1 console），115200 |
| 调试器 | DebugServerConsole.exe（重启后监听 1025；cpu2 未 dereset ⇒ 只监听 1025，本任务只需 cpu1） |
| 拓扑 | 上游 master i2c2 → i2cf4 输入 SCLI/SDAI(PE03/PB02) → 被测滤波器 i2cf4 → 输出 SCLO/SDAO(PE02/PB00) → i2c3 片内 target eeprom@33 |
| 旁路 master | i2c4（switch_to_master 时直驱 SCLO/SDAO，绕过滤波器） |
| 被测驱动 | `modules/lib/pfr_tpm/drivers/pfr/i2c/i2c_filter.c`（**未改动**，上游） |

**器件在位（实测复核）**：`EM_PRESCAN downstream_count=2 addrs=33 50`
⇒ 下游侧 0x33 有器件、0x2A **无**器件（0x50 板载器件，本任务不使用、不撞址）。
`EM_P0 addr=0x33 wr=0 rd=0 chk=a55a1122`（旁路写入 pattern 0xA55A1122 后回读一致）⇒ 0x33 器件在位 + 数据通路真。

## 2. 方法（单变量 + 组间复位 + 可归因 marker）

- **固件**：`cpu1_i2cf_test/src/i2cf_enum.c`（`CONFIG_I2CF_ENUM_MATRIX`，默认 n；不进 cases.yaml）。
  `test_main` 在置位时进 `i2cf_enum_matrix_run()`（不跑 ztest），循环跑整张矩阵。
- **单变量 + 组间复位**：每格前 `cold_reset`（**不清** `WHITELIST_ADDRESS_*`/`SMBF_ADDRESS_ENABLE`）
  → **显式关全部 16 通道** → 注册 ISR 回调 → `switch_to_filter` → 配白名单 / 滤波器开关。
- **清 ISR**：写 `INTR_CLR=0x3` 并把计数归零（逐笔记 **ISR 增量**）。
- **归因**：每格用成对 `LOG_INF` marker（`EM_BEGIN` / `EM_END`）包住 **3 笔**事务；
  驱动 ISR 内打的 `address beyond whitelist` ERR 行与 marker 同走 deferred 日志缓冲
  ⇒ 串口里落在 BEGIN 与 END **之间** ⇒ 可逐格数 ERR 行（实测 240 条 ERR **全部落格内、0 orphan**）。
- 读方向用 `i2c_burst_read`（本族既有经验：裸 `i2c_read` 更不可靠）；写方向 `i2c_write`。
- 每格 3 笔重复（对冲"紧接被拦事务之后一笔会丢"），**22 轮零方差**。
- **交叉一致性**：逐格 `isr 增量之和` == 该格 `ERR 日志行数`（逐格精确相等）。

## 3. 完整枚举表（本次单轮采集，22 轮全部一致）

| 格 | 滤波器 | 白名单 | 地址 | 器件 | 方向 | `ret` | `isr`(每笔) | ERR 日志 | `SMBF_NONWHITELIST` | 读回数据 |
|---|---|---|---|---|---|---|---|---|---|---|
| C1 | ON | 内 {0x33} | 0x33 | 有 | 读 | 0 | 0 | 无 | 0→0 | `a55a1122`（**真数据**） |
| C1 | ON | 内 {0x33} | 0x33 | 有 | 写 | 0 | 0 | 无 | 0→0 | — |
| **C2 ★** | **ON** | **内 {0x2A}** | **0x2A** | **无** | **读** | **-5** | **0** | **无** | **0→0** | 0 |
| **C2 ★** | **ON** | **内 {0x2A}** | **0x2A** | **无** | **写** | **-5** | **0** | **无** | **0→0** | — |
| C3 | ON | 外 {} | 0x33 | 有 | 读 | -5 | 1 | **有** | 0→`0x3300` | 0 |
| C3 | ON | 外 {} | 0x33 | 有 | 写 | -5 | 1 | **有** | 0→`0x3300` | — |
| C4 | ON | 外 {} | 0x2A | 无 | 读 | -5 | 1 | **有** | 0→`0x2a00` | 0 |
| C4 | ON | 外 {} | 0x2A | 无 | 写 | -5 | 1 | **有** | 0→`0x2a00` | — |
| C5 | OFF | — | 0x33 | 有 | 读 | -5 | 0 | 无 | 0→0 | 0 |
| C5 | OFF | — | 0x33 | 有 | 写 | -5 | 0 | 无 | 0→0 | — |
| C6 | OFF | — | 0x2A | 无 | 读 | -5 | 0 | 无 | 0→0 | 0 |
| C6 | OFF | — | 0x2A | 无 | 写 | -5 | 0 | 无 | 0→0 | — |
| C7 | OFF | 内 {0x33} | 0x33 | 有 | 读 | -5 | 0 | 无 | 0→0 | 0 |
| C7 | OFF | 内 {0x33} | 0x33 | 有 | 写 | -5 | 0 | 无 | 0→0 | — |
| C8 | 旁路(i2c4) | — | 0x33 | 有 | 读 | 0 | 0 | 无 | 0→0 | `a55a1122` |
| C8 | 旁路(i2c4) | — | 0x33 | 有 | 写 | 0 | 0 | 无 | 0→0 | — |
| C9 | 旁路(i2c4) | — | 0x2A | 无 | 读 | -5 | 0 | 无 | 0→0 | 0 |
| C9 | 旁路(i2c4) | — | 0x2A | 无 | 写 | -5 | 0 | 无 | 0→0 | — |

- `ret=-5` = `-EIO`。`isr` = 单笔 ISR **增量**（非累计）。
- `SMBF_NONWHITELIST` = `0xAABB00` 形，`AA`=`ERROR_ADDRESS`（[15:8]）。仅 C3/C4 有锁存（锁的是被拦地址）。
- 22 轮 × 每格 3 笔，`ret/isr/nw/rd` 全部零方差。

### 与任务书「已知格子」的对应（#1–#4 本轮**同批复现**，非复用旧值）

| 任务 # | 语义 | 本表格 | 实测 | 与任务书一致？ |
|---|---|---|---|---|
| 1 | 外/0x33/有/读写 | C3 | -5 / isr=1 / 有 ERR | ✓ |
| 2 | 外/0x2A/无/读写 | C4 | -5 / isr=1 / 有 ERR | ✓ |
| 3 | 内/0x33/有/读写 | C1 | 0 / isr=0 / 无 ERR | ✓ |
| 4 | filter OFF/0x33·0x2A/有·无 | C5/C6 | -5 / isr=0 / 无 ERR | ✓ |
| **5 ★** | **内/0x2A/无/读写** | **C2** | **-5 / isr=0 / 无 ERR** | **本轮靶心** |
| 6 | 内/0x33/有/读写 | C1（同 #3） | 0 / isr=0 / 无 ERR | ✓ |
| 7 | filter OFF/0x33/有/读写 | C7 | -5 / isr=0 / 无 ERR | ✓ |

来源报告：本表全部来自本轮（`reports/2026-10-15-3/`）；
旧会话数值见 `reports/2026-10-15-1`（addr 0x2A，相位 B 混 filter off，deferred 日志归因不清）
与 `reports/2026-10-15-2`（addr 0x33，两相位都 filter on）：#1/#3 与之逐值一致。

## 4. 关键观察

1. **靶心 #5（C2）**：地址在**白名单内**但下游**无器件** ⇒ `ret=-5`（总线 NACK），
   **`isr=0`、无 `address beyond whitelist` 日志、`SMBF_NONWHITELIST` 不锁存**。
   ⇒ 滤波器**没有**因为"总线无人应答"而上报。
   （对照 C1：白名单内 + 有器件 ⇒ `ret=0` 且读回**真数据** `a55a1122`，非假 ACK。）
2. **上报只出现在"白名单外"**（C3/C4）：`isr=1` + ERR 行 + `SMBF_NONWHITELIST` 锁存该地址。
3. **方向无关**：每一格里读/写结果**逐项相同**（`ret`/`isr`/ERR/`nw` 全同）⇒ 滤波器判定不看读写方向。
4. **滤波器关 ≠ 直通**（#4/#7 复现）：filter off 时 `0x33`（**有器件**）仍 `ret=-5`、`isr=0`、
   无 ERR —— 输出侧不转发，事务到不了器件。且 C7（off + 白名单授权 0x33）与 C5（off + 空白名单）
   **逐项相同** ⇒ filter off 下白名单内容无关。
   对照 C8（旁路 i2c4 直驱）同址 `ret=0` + 真数据 ⇒ **器件本身可达**，-5 是"滤波器关断不转发"所致。
5. **C8/C9 旁路对照**：i2c4 直驱下 0x33 → `ret=0`（真数据）、0x2A → `ret=-5`（无器件）
   ⇒ 独立证明"器件有/无"这一维确实是 0x33 有、0x2A 无。

## 5. ★ 结论（一句话）

**I2CF 滤波器的"是否上报"只由「地址是否在白名单内」决定，与总线上有没有器件应答（ACK/NACK）
无关、与读/写方向无关** —— 白名单外 ⇒ 拦 + 报（`ret=-5`, `isr=1`, ERR 行）；
白名单内 ⇒ 一律不报（`isr=0`, 无 ERR），`ret` 由总线决定（有器件 `0`+真数据 / 无器件 `-5`）。

## 6. 仍未测的格子（如有）

- 本矩阵覆盖了任务要求的全部维度组合（白名单内/外 × 读/写 × 有/无器件 × 滤波开/关）。
- **未覆盖**（超出本次重点、且需要额外构造）：
  1. 白名单**内**但把**命令码位图**关掉部分位（本任务全用全 1 位图，未测 `COMMAND_BEYOND_WHITELIST`）。
  2. `MASTER_WRITE_MODE`（`SMBUS_FILTER_SET.bit3`）置 1 对 read 帧上报的影响（另有 case_mwrite_probe 覆盖）。
  3. 白名单**内**的地址由**上游**直接写/读（不经配对 master），本矩阵已含（C1/C2 即经滤波器）——
     无缺口。

## 7. 产物

| 文件 | 内容 |
|---|---|
| `serial_COM16_raw.log` / `serial_COM16_timestamped.log` | 原始/带到达时间戳串口（本轮） |
| `aggregate.txt` | 逐格聚合（22 轮、零方差、isr-total==ERR） |
| `run_enum2.log` | 本轮 GDB 加载运行日志 |
| `build_enum2.log`(未留) / `ds_restart2.log` | 构建 / DS 重启日志 |
| 固件 | `projects/i2cf_test/cpu1_i2cf_test/src/i2cf_enum.{c,h}`、`Kconfig`(I2CF_ENUM_MATRIX)、`CMakeLists.txt`、`main.c`、`tests/case_enum_matrix/` |

**回归安全**：`case_default` 实测 RC=0、0 warning、`CONFIG_I2CF_ENUM_MATRIX` 未置、
`i2cf_enum.c` 未编入、`CONF_FILE` 只含 `prj.conf` ⇒ 新增开关默认 n，爆炸半径为 0。
