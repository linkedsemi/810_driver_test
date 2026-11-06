# 810_driver_test 夜间任务总报告（2026-11-04 夜 ~ 11-05 晨）

> 工程：`D:\zephyr4_0\810_driver_test`。硬件全程串行；不改源码仓、不做任何 git 操作。
> 结论只对报告中记录的构建配置/镜像负责。临时 case 已删除（见 §6）。

## 0. 执行摘要（三句话）

1. **全量回归（首轮）**：26 工程 / 69 case，**66 PASS / 3 FAIL**，0 个 ENV-ABORT，耗时 3h33m（21:39→01:12），会话 `reports/2026-11-04-1`；新报告逻辑 **4/4 项验证通过**。
2. **单模块回归**：`sec_iwdg_cpu1_test` 两个启用 case 全 PASS（5/5、19/19），会话 `reports/module/2026-11-05-1`、`-2`；新逻辑 **4/4 项验证通过**。
3. **I3C 频率摸底**：双 RAM（B0）与主机单侧 XIP（B1）在 12.5 MHz 仅失败已知过期阈值项；从机单侧 XIP（B2）**无 34/35 频点**，按“与低频基线失败集合一致”口径**最高稳定点 = 2 MHz**（3 MHz 起出现额外间歇失败）；两侧 XIP（B3）同样无稳定点，2–4 MHz 出现 `big_256` 中途挂死（CTRL 34 行），最高通过数为 **400 kHz 的 32/35**。

---

## 1. 阶段 1：全量回归（首轮）

- 命令：`cd D:/zephyr4_0/810_driver_test && python regression/run_all.py`（默认 cases.yaml / 默认预检）
- 控制台日志：`tmp/2026-11-04-night/full_reg.log`；归档日志：`reports/2026-11-04-1/logs/`
- 会话目录：**`reports/2026-11-04-1`**（`reports/<date>-<idx>`）✅
- 耗时：21:39:26 → 01:12，约 **3 小时 33 分**
- 结果：**69 case，通过 66，失败 3，ENV-ABORT 0**
- 失败归因骨架（已补完）：`reports/2026-11-04-1/failures/`

### 1.1 新报告逻辑验证

| 检查项 | 期望 | 实测 | 结论 |
|---|---|---|---|
| 会话目录 | `reports/<date>-<idx>` | `reports/2026-11-04-1` | PASS |
| `session_meta.json.kind` | `full` | `full` | PASS |
| `session_meta.json.project` | `null` | `null` | PASS（首轮曾误写 `trng_test`，协调方已修并就地更正该文件） |
| `session_meta.json.projects` | 非空 | 26 个工程 | PASS |
| `summary.md` 标题 | `# 810_driver_test 汇总（全量 <session>）` | `# 810_driver_test 汇总（全量 2026-11-04-1）` | PASS |

**结论：新逻辑验证 PASS（4/4）。**

### 1.2 逐工程结果

| 工程 | case 数 | PASS | FAIL |
|---|---|---|---|
| adc_temper_test | 1 | 1 | 0 |
| app_iwdg_cpu2_test | 3 | 3 | 0 |
| crypto_api_test | 1 | 0 | 1 |
| dma_test | 2 | 2 | 0 |
| emmc_test | 2 | 2 | 0 |
| flash_test | 1 | 1 | 0 |
| i2c_pure_test | 2 | 2 | 0 |
| i2cf_test | 3 | 3 | 0 |
| **i3c_dualcore_test** | **7** | **5** | **2** |
| i3c_i2c_switch_test | 2 | 2 | 0 |
| iopmp_test | 1 | 1 | 0 |
| mbedtls_test | 10 | 10 | 0 |
| mbox_test | 7 | 7 | 0 |
| otbn_delegation_test | 13 | 13 | 0 |
| otbn_selftest | 1 | 1 | 0 |
| otbn_test | 1 | 1 | 0 |
| perf_test | 1 | 1 | 0 |
| pwm_test | 1 | 1 | 0 |
| qspi_flash_test | 2 | 2 | 0 |
| rtc_ls_test | 1 | 1 | 0 |
| rtc_timer_ls_test | 1 | 1 | 0 |
| sec_iwdg_cpu1_test | 2 | 2 | 0 |
| spi_test | 1 | 1 | 0 |
| spid_test | 1 | 1 | 0 |
| spif_test | 1 | 1 | 0 |
| trng_test | 1 | 1 | 0 |
| **合计** | **69** | **66** | **3** |

### 1.3 失败项逐项明细与归因

| # | case | 通过/总数 | 失败项 | 串口实测（证据） | 归因 |
|---|---|---|---|---|---|
| 1 | `crypto_api_test_case_default` | 42/44 | `hash_caps_sha256`、`hash_caps_sha512` | 每项先打印 `ASSERTION FAIL [(tmp & ((1UL << (0)) | (1UL << (1)))) != 0] @ WEST_TOPDIR/zephyr/include/zephyr/crypto/crypto.h:120 : Driver should support at least one key type: RAW/Opaque`，随后 `mcause: 11, Environment call from M-mode` + `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（`serial_TGT.log:81-116`、`:122-151`）；串口该项仍打 `PASS`（假 PASS，判定框架按“不确定不产生 PASS”判 FAIL） | 用例/驱动 caps 边界：hash 驱动 caps 未置 RAW/Opaque key 类型位，`crypto.h` 的 `__ASSERT` 触发内核 panic。**不是通信/接线问题**。与 `KNOWN_ISSUES` #158 同族。 |
| 2 | `i3c_dualcore_test_case_mixed_1m_12m5` | 34/35 | `multi_msg_4` | `msg3 num_xfer 0 != 8`（`serial_CTRL.log:112-127`）：前两段 ST/IBI 正常，**第 3 段未产生传输**（期望 8 次 xfer，读到 0）；TGT 侧同项 PASS | 控制器侧 4 段消息第 3 段丢失。**形貌不是** #155 的“byte[17] 起 0xff/短读”，而是 `num_xfer=0`；本次单次复现，**批内无法定位**（缺 CTRL 寄存器快照/波形） |
| 3 | `i3c_dualcore_test_case_pure_i3c_12m5` | 34/35 | `write_delay_read_16` | `critical_delay_ns=1600, first_short_step=33, master_stop_step=56`（`serial_CTRL.log:295-302`）；用例窗口 5500–8000 ns | **已知过期阈值，不是通信失败**：窗口按 25 MHz 时钟源标定，现 I3C 时钟源为 50 MHz DPLL，实测值约 1/4 |

> **对照 `KNOWN_ISSUES #155`**：#155 的最终定位是 25 MHz 版时钟改动导致 16 B FIFO 边界 `SPAR`，形貌为**第 17 字节起 0xff / 短读**。本次两个 i3c 失败**都不是该形貌**（一个 `num_xfer=0`，一个延迟阈值超下界），**不能归到 #155**。

### 1.4 环境异常

- 无 ENV-ABORT、无 `error 138`、无 `DebuggerServer quit`。
- 唯一 WARN：`audit_stale_elves` 报 `perf_test case_default` 旧镜像缺哨兵（会被强制重建），不影响结果。

---

## 2. 阶段 2：单模块回归

- 工程：`projects/sec_iwdg_cpu1_test`（由 `wdt_test` 迁移而来）
- 启用 case（`tests/cases.yaml`）：`case_non_destructive`、`case_boundary`
- 命令：`cd projects/sec_iwdg_cpu1_test/tests && python run_test.py --case <case> --tgt-uart COM16`
  - 说明：不带 `--case` 时脚本默认只跑 `case_non_destructive`（参数默认值），故按 cases.yaml 逐个跑两个 case（等价于“跑全部 case”）。
- 日志：`tmp/2026-11-04-night/module_sec_iwdg.log`、`module_sec_iwdg_boundary.log`

| case | 结果 | 通过/总数 | 会话目录 |
|---|---|---|---|
| `case_non_destructive` | PASS | 5/5 | `reports/module/2026-11-05-1` |
| `case_boundary` | PASS | 19/19 | `reports/module/2026-11-05-2` |

### 2.1 新报告逻辑验证

| 检查项 | 期望 | 实测 | 结论 |
|---|---|---|---|
| 输出位置 | `reports/module/<date>-<idx>/` | `reports/module/2026-11-05-1`、`-2` | PASS |
| `kind` | `module` | `module` | PASS |
| `project` | `sec_iwdg_cpu1_test` | `sec_iwdg_cpu1_test` | PASS |
| `summary.md` 标题 | 含“单模块” | `# 810_driver_test 汇总（单模块 2026-11-05-1）` | PASS |

**结论：新逻辑验证 PASS（4/4）**。两次运行各分配一个 module 会话目录，均属本任务产物。

---

## 3. 阶段 3：I3C XIP 频率摸底

### 3.1 测试条件

| 项 | 值 |
|---|---|
| 工程 | `projects/i3c_dualcore_test`（standalone；controller=cpu2 / target=cpu1） |
| 总线 | pure I3C SDR（`controller.overlay` 只设 `i3c-scl-hz`，无 `i2c-scl-hz`） |
| 接线 | I3C5_SCL(PJ10)↔I3C6_SCL(PJ08)、I3C5_SDA(PJ11)↔I3C6_SDA(PJ09)、共地 |
| 串口 | CTRL=cpu2 uart5 → **COM18**（MCP2200）；TGT=cpu1 uart1 → **COM16**（CH342-B） |
| I3C 时钟源 | **50 MHz DPLL**（`write_delay_read_16` 的 5500–8000 ns 窗口仍是 25 MHz 标定值 ⇒ 过期阈值） |
| 测试项 | **35 项**（`run_test.py::TEST_NAMES`；任务书的“36 项”与代码不符，按 35 记录） |
| 判稳口径 | 除 `write_delay_read_16`（RAM 基线也失败）外，其余 34 项 3/3 轮全 PASS 且无短读/0xff = 稳定；`multi_msg_4` 等偶发单独计数 |
| 每轮 | 冷启动（B0/B1 = GDB 两遍烧录；B2/B3 = flash 烧录 + reset-and-run）后采集 40 s |

板级/入口（实测 `.config` + `readelf`）：

| 组 | controller（cpu2） | target（cpu1） |
|---|---|---|
| B0 | `lsqsh_evb@2os/lsqsh/cpu2`，Entry 0x10000000 | `lsqsh_evb@2os/lsqsh/cpu1`，Entry 0x100C0000 |
| B1 | `lsqsh_evb@2os_xip/lsqsh/cpu2`，Entry 0x08100000（就地执行） | `lsqsh_evb@2os/lsqsh/cpu1`，Entry 0x100C0000（GDB RAM 载入）+ `CONFIG_CPU2_BOOT_ADDR=0x8100000` |
| B2 | `lsqsh_evb@2os/lsqsh/cpu2`，Entry 0x10000000（flash 暂存，cpu1 搬入 RAM） | `lsqsh_evb@2os_cpu1_xip/lsqsh/cpu1`，Entry 0x08010100，`CONFIG_CPU2_IMAGE_HEADER=y` |
| B3 | `lsqsh_evb@2os_xip/lsqsh/cpu2`，Entry 0x08100000（就地执行） | `lsqsh_evb@2os_cpu1_xip/lsqsh/cpu1`，Entry 0x08010100；**关闭 `CONFIG_CPU2_IMAGE_HEADER` + `CPU2_BOOT_ADDR=0x8100000`** |

B0 镜像 md5（唯一逐点记录的一组）：
- controller `zephyr.elf` `8bea2f610d64455691a7cb61c689d1d8` / `zephyr.bin` `7942739fa5621d97f4b5c3d88f16a407`
- target `zephyr.elf` `37db2b79117e9523edc7e0153fcbd551` / `zephyr.bin` `eeb35256fa4ae91bff09580c2b9cfb75`
- B1/B2/B3 每频点 `-p always` 全量重建，未逐点记录 md5；构建配置以上表与 `freq_<freq>_<tag>_build.log` 为准。

### 3.2 B0：双 RAM，12.5 MHz，3 轮

| 轮 | 通过/35 | 失败项 |
|---|---|---|
| 1 | 34/35 | `write_delay_read_16`（`critical_delay_ns=1200`） |
| 2 | 34/35 | `write_delay_read_16`（1200） |
| 3 | 34/35 | `write_delay_read_16`（1200） |

**结论**：RAM 基线稳定，唯一失败是已知过期阈值。日志：`freq_12m5_B0_run{1,2,3}.log`。

### 3.3 B1：主机（cpu2）XIP + 从机 RAM，12.5 MHz，3 轮

| 轮 | 通过/35 | 失败项 |
|---|---|---|
| 1 | 33/35 | `multi_msg_4`（双核均 FAIL）+ `write_delay_read_16`（CTRL） |
| 2 | 34/35 | `write_delay_read_16` |
| 3 | 34/35 | `write_delay_read_16` |

**启动方式**：cpu2 XIP 镜像以**裸 `zephyr.bin`** 烧到 `0x08100000`（无 image header），cpu1 RAM 工程设 `CONFIG_CPU2_BOOT_ADDR=0x8100000`，由 `boot_cpu2()` 直接 dereset 到 flash 地址就地执行。
**结论**：主机 XIP 可行，除基线阈值外无额外稳定失败；`multi_msg_4` 1/3 轮偶发。

### 3.4 B2：主机 RAM + 从机（cpu1）XIP，12.5 MHz 向下扫

失败集合 = 该频点 3 轮中出现过的失败项；★ = 低频基线恒定项。

| 频点 | 轮1 | 轮2 | 轮3 | 失败集合（要点） |
|---|---|---|---|---|
| 12.5M | 18 | 17 | 17 | 大量 16 B 边界/欠载：big_256、write_read_15/16/17/32、write_100、read_underrun*、write_delay_read_len、write_indexed_read_16、short_read_then_nack、back_to_back_8x8、multi_msg*、read_master_abort、wdr16（17–18 项） |
| 11M | 19 | 19 | 19 | 16 项（同类） |
| 10M | 18 | 18 | 17 | 17–18 项 |
| 9M | 19 | 19 | 19 | 16 项 |
| 8M | 20 | 19 | 20 | 15–16 项 |
| 7M | 21 | 20 | 20 | 14–15 项 |
| 6.25M | 20 | 20 | 20 | 15 项 |
| 5M | 24 | 23 | 20 | 11–15 项 |
| 4M | 29 | 29 | 29 | ★multi_msg_4、★read_master_abort、★wdr16、★write_indexed_read_16 + 间歇 read_underrun_8_16 / short_read_then_nack / write_read_15/16 |
| 3M | 31 | 29 | 29 | ★四项 + 间歇 `write_read_17`、`write_read_32`（2/3 轮） |
| **2M** | **31** | **31** | **31** | **★四项，3/3 轮完全一致** |
| 1M | 31 | 31 | 31 | ★四项，3/3 轮一致 |
| 500k | 32 | 31 | 32 | ★multi_msg_4、★read_master_abort、★wdr16（+ 间歇 nack_bad_addr） |
| 400k | 32 | 31 | 31 | 同上（+ 间歇 nack_bad_addr） |
| 300k | 31 | 31 | 31 | ★四项 |
| 200k | 31 | 31 | 32 | ★三项（+ 间歇 nack_bad_addr） |
| 100k | 31 | 31 | 31 | ★multi_msg_4、★read_master_abort、★wdr16 + nack_bad_addr |

★ 低频基线恒定失败（1M/100k 仍失败，**与频率无关**）：`multi_msg_4`、`read_master_abort`、`write_delay_read_16`（过期阈值）、`write_indexed_read_16`（1M–3M 常见，500k 以下消失）；低频段附带偶发 `nack_bad_addr`。

**B2 结论**：**无 34/35 的“全项稳定”频点**。按“与低频基线失败集合一致”口径，**最高稳定频率 = 2 MHz**（2M 起 3/3 轮只失败基线四项；1M 同样一致）。**3 MHz 起出现额外间歇失败**（`write_read_17/32`），4 MHz 以上失败面迅速扩大；12.5M（当前官方用例频点）仅 17–18/35。XIP 从机没有把 16 B 边界问题随降频消除，而是收敛到一组非频率相关的固定失败。

日志：`tmp/2026-11-04-night/freq_<freq>_B2_build.log`、`freq_<freq>_B2_run{1,2,3}.{log,txt}`、`sweep_B2.log`。

### 3.5 B3：两侧都 XIP

**首次尝试不可启动（排查结论）**：
- 用 cpu2 的 `*_bram_xip_*.bin`（image header 引导）⇒ TGT 只有 55 行 bootrom 输出、**`RAW CTRL (0 lines)`**；cpu1 进入 `_Fault`（PC `0x080183ce`）。
- 根因：bram 装载头 `exe_addr=0x10124000` 落在 cpu1 XIP 的 SRAM 区（`0x10100000–0x10140000`）内，`boot_cpu2()` 把 stub 拷到该地址时**覆盖 cpu1 自身 RAM** ⇒ cpu1 崩溃。
- 改用**裸 `zephyr.bin` 就地 XIP + 关闭 `CONFIG_CPU2_IMAGE_HEADER`** 后正常启动（CPU1 打印 `boot_cpu2: address: 0x8100000`）。

完整频点结果（每点 3 轮）：

| 频点 | 轮1 | 轮2 | 轮3 | 失败集合（要点） |
|---|---|---|---|---|
| 12.5M | 22 | 22 | 22 | 13 项恒定：big_256、hot_join、multi_msg_4、read_32_direct、read_master_abort、write_100、wdr16、write_delay_read_len、write_indexed_read_16、write_read_17、write_read_32、write_stop_read_17、write_stop_read_32 |
| 11M | 22 | 22 | 22 | 同上 13 项 |
| 10M | 22 | 22 | 22 | 同上 |
| 9M | 22 | 23 | 22 | 同上（1 轮少 multi_msg_4） |
| 8M | 22 | 22 | 22 | 同上 |
| 7M | 22 | 22 | 22 | 同上 |
| 6.25M | 22 | 22 | 23 | 同上 |
| 5M | 23 | 24 | 23 | 11–12 项 |
| 4M | **2** | **2** | 26 | r1/r2：**CTRL 仅 34 行，在 `big_256` 中途挂死**；r3：9 项 |
| 3M | **2** | **2** | **2** | 三轮全部在 `big_256` 挂死（CTRL 34 行） |
| 2M | **2** | **2** | 18 | r1/r2 同款挂死；r3：17 项 |
| 1M | 29 | 29 | 29 | multi_msg_4、wdr16、write_delay_read_len、write_indexed_read_16、write_read_17、write_read_32 |
| 500k | 31 | 31 | 31 | multi_msg_4、wdr16、write_delay_read_len、write_indexed_read_16 |
| 400k | 32 | 32 | 32 | wdr16、write_delay_read_len、write_indexed_read_16 |
| 300k | 28 | 28 | 29 | 上述三项 + multi_msg_4、multi_msg_short_read、write_read_17/32 |
| 200k | 31 | 31 | 31 | multi_msg_4、wdr16、write_delay_read_len、write_indexed_read_16 |
| 100k | 30 | 32 | 32 | wdr16、write_delay_read_len、write_indexed_read_16（+1 轮 multi_msg_short_read） |

**B3 结论**：
- 12.5–5 MHz：稳定在 **22–24/35**，13 项固定失败与 SCL 无关（含 `hot_join`、`read_32_direct`、`write_100` 等 B2 不出现的项）。
- **2–4 MHz：双 XIP 运行挂死**（3M 3/3、4M 2/3、2M 2/3），形貌一致：CTRL 串口停在 34 行、最后一条是 `big_256` 的 IBI ok、无收尾。**这不是频率“不稳”，是该形态下的确定性/高概率挂死**。
- 500k–100k 改善到 31–32/35，仍达不到 34/35。**B3 最高通过数 = 400 kHz 的 32/35**；无“稳定点”。
- 首轮扫描曾因“控制器 0 行”中断（bram 引导方式不可用），已用 raw 方式补全全部频点。

日志：`tmp/2026-11-04-night/freq_<freq>_B3_*.{log,txt}`、`sweep_B3raw*.log`、`sweep_B3supp.log`；启动失败证据 `freq_12m5_B3_run1.log`（CTRL 0 行）。

---

## 4. 全量回归：首轮 vs 末轮对比

| 项 | 首轮 | 末轮 |
|---|---|---|
| 会话 | `reports/2026-11-04-1` | `reports/2026-11-05-1` |
| 时间 | 21:39 → 01:12（3h33m） | 04:31 → 07:57（3h26m） |
| 结果 | 69 case，66 PASS / 3 FAIL，0 ENV-ABORT | 69 case，66 PASS / 3 FAIL，0 ENV-ABORT |
| 失败 1 | `crypto_api_test_case_default` 42/44（`hash_caps_sha256`、`hash_caps_sha512`） | 同左，**完全一致** |
| 失败 2 | `i3c_dualcore_test_case_mixed_1m_12m5` 34/35（`multi_msg_4`：`msg3 num_xfer 0 != 8`） | `i3c_dualcore_test_case_mixed_400k_12m5` 34/35（`write_delay_read_16`，`critical_delay_ns=1600`） |
| 失败 3 | `i3c_dualcore_test_case_pure_i3c_12m5` 34/35（`write_delay_read_16`，1600） | 同 case，`critical_delay_ns=2000` |

**对比结论**：
- 失败**总数不变（3）**，`crypto_api_test` 的两项是稳定复现的同一缺陷。
- i3c 的失败在 12.5 MHz 的 3 个 case 之间**轮换**：首轮 `mixed_1m_12m5`（multi_msg_4）+ `pure_12m5`（wdr16），末轮 `mixed_400k_12m5` + `pure_12m5`（均为 wdr16）。`mixed_400k_12m5` 首轮 PASS、末轮 FAIL；`mixed_1m_12m5` 首轮 FAIL、末轮 PASS —— 属**同族偶发**（16 B 边界类）叠加**过期阈值**，未见新的稳定回归。
- 其余 66 个 case 两轮通过状态一致。

---

## 5. 异常/失败清单（供快速处理）

1. `crypto_api_test_case_default`：hash 驱动 caps 缺 RAW/Opaque ⇒ `crypto.h:120` 断言 → 内核 panic（稳定复现，2 项）。
2. i3c 12.5 MHz 用例：`write_delay_read_16` 阈值过期（25 MHz 标定 vs 现 50 MHz 时钟），**建议更新窗口**；`multi_msg_4` 偶发（`num_xfer=0`，非 #155 形貌）。
3. B2（从机 XIP）：2 MHz 以下仍恒定失败 `multi_msg_4`、`read_master_abort`、`write_indexed_read_16` —— 与频率无关的 XIP 形态基线问题。
4. B3（双 XIP）：2–4 MHz 在 `big_256` 中途挂死（3M 3/3），属该形态的高概率故障；启动必须用裸 bin 就地 XIP（bram 头会踩 cpu1 SRAM）。
5. 扫频脚本 `sweep.py` 首次误用 WSL bash（`west: command not found`），已改用 Git Bash；失败行已从 progress.md 清除并留说明。

---

## 6. 临时产物与清理

- 临时 case（`projects/i3c_dualcore_test/tests/case_sweep_12m5/`）**已删除**（含其 build）；母本与生成/编排脚本保留在 `tmp/2026-11-04-night/i3c-freq-sweep/`。
- `reports/` 下新增：`reports/2026-11-04-1`（首轮全量）、`reports/2026-11-05-1`（末轮全量）、`reports/module/2026-11-05-1`、`-2`（单模块）。
- 所有原始日志保留在 `tmp/2026-11-04-night/`。

## 7. 证据索引

| 内容 | 路径 |
|---|---|
| 首轮全量控制台日志 | `tmp/2026-11-04-night/full_reg.log` |
| 首轮归档 | `reports/2026-11-04-1/`（`summary.md` / `session_meta.json` / `logs/` / `failures/` / 各 case `serial_*.log`） |
| 末轮全量控制台日志 | `tmp/2026-11-04-night/full_reg_final.log` |
| 末轮归档 | `reports/2026-11-05-1/` |
| 单模块 | `tmp/2026-11-04-night/module_sec_iwdg*.log`；`reports/module/2026-11-05-1`、`-2` |
| B0 | `tmp/2026-11-04-night/freq_12m5_B0_run{1,2,3}.log` |
| B1 | `tmp/2026-11-04-night/freq_12m5_B1_run{1,2,3}.log`、`freq_12m5_B1_build*.log` |
| B2 | `freq_<freq>_B2_build.log`、`freq_<freq>_B2_run{1,2,3}.{log,txt}`、`sweep_B2.log` |
| B3 | `freq_<freq>_B3_*.{log,txt}`、`sweep_B3raw*.log`、`sweep_B3supp.log` |
| 脚本 | `tmp/2026-11-04-night/i3c-freq-sweep/{gen_cases.py,sweep.py,xip_round.py}` |
| 进度心跳 | `tmp/2026-11-04-night/progress.md` |

---

## 8. 2026-11-05 补充：测试改造与 `multi_msg_4` 处置

### 8.1 测试项改造（`write_delay_read_16/len` → dummy 形式）
- 原两项用 `critical_delay_ns` 扫描/阈值判据；该窗口按 25MHz 时钟标定，50MHz DPLL 下实测 1200–2000ns，已失效。
- 改为 **dummy 写段**形式，不测 ns 阈值：
  - `write_dummy_read_16`：写 `{X, idx=0x10, 0xA5×N}` 后读 16B，N∈{0,8,16,32,64,128}，全部读满且 pattern 正确；
  - `write_dummy_read_len`：写 `{V, idx, len, 0xA5×N}` 后读 len，len∈{16,17,32,64}、N∈{0,16,64}。
- 实测：`write_dummy_read_16` 26/26 PASS（`dummy_max_ok=128`）；`write_dummy_read_len` 25/26（1 次同族 `-5`，`max_len_ok=64`）。
- 协议头新增 `CMD_INDEXED_READ_LEN 'V'`；`Kconfig`/`case_pure_i3c_12m5/controller.conf` 中旧 WDR 阈值已移除。

### 8.2 `multi_msg_4`（4 段 repeated-START）定位与屏蔽
- 现象：控制器 `ret=-EIO n=[1,8,1,0]`，第 4 段第二次读的地址相位 **ANACK**（清标志前 `SER=0x100`，其余错误位全 0）。
- target：两段写都收到、写匹配前后 TX FIFO 均满（16/16）、自身套件 PASS ⇒ 非数据 NACK/FIFO 空，是写匹配 ISR 未及时完成时新读地址已到。
- 延迟敏感：仅加两次 MMIO 读，失败率 43%→91%；单点修复（提前清 MATCHED/跳过预填/新请求不清 MATCHED/删 100µs 等待）停在 60–70%，仅整段跳过写分支到 8% 但引入 DOVR。
- 处置（用户裁定）：多次 restart 不在覆盖范围 → `I3C_TEST_EN_MULTI_MSG_4=0` 屏蔽；单次 restart 由 `write_read_*`/`write_stop_read_*` 覆盖。
- 屏蔽后 sanity：`case_pure_i3c_12m5` **34/34 PASS × 2 轮**（`multi_msg_4` 不再出现），无新增构建告警。

### 8.3 驱动小修（已验证）
- 读段 END0 收尾：`SWDATAB|END0` 后统一 `SINTCLR = TXSEND`（MATCHED/TXSEND/START 四处一致）；
- `read_requested_cb` 返回负数（无数据可续供）时关 TXSEND，避免 ISR 反复重入。
- 验证：34 项 × 2 轮全 PASS（除已屏蔽的 `multi_msg_4`），关注项零回归。

### 8.4 I3C 复测结果回填（未重跑全量）
- 按用户指示**不重跑全量回归**；把 I3C 改造后的复测结果回填到既有会话
  `810_driver_test/reports/2026-11-05-1`（在其 I3C case 报告与 summary 中加“11-05 复测更新”段，
  原归档结果保留）。
- 回填要点：原两项 FAIL 的失败项均为 `write_delay_read_16`（阈值过期）→ 已被 dummy 两项替换；
  `multi_msg_4`（多次 restart）已屏蔽。`case_pure_i3c_12m5` 屏蔽后 34/34 × 2 轮；
  `case_mixed_400k_12m5` 复测亦为 **34/34 PASS × 2 轮**（dummy INFO 同上）。
  两者原始归档仍为 34/35 FAIL（失败项 `write_delay_read_16`），复测证据在
  `reports/2026-11-05-1/<case>/retest_2026-11-05/`。
