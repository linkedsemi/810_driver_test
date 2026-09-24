# eMMC 测试覆盖矩阵

- 工程：`projects/emmc_test`
- 被测硬件：**板载 eMMC**（非外插卡），7.28 GiB（`TOTAL_SECTORS=15269888`），8-bit，
  `sdhci1@40049000`（别名 `sdhc0`），FatFS 盘名必须 `"SD2"`。**零外部接线**
- 被测驱动：
  - 块层：`zephyr/drivers/sdhc/sdhci-of-linkedsemi.c`（`linkedsemi,sdhci`）
  - 磁盘层：`zephyr/drivers/disk/mmc_subsys.c`（`zephyr,mmc-disk`）
  - 文件系统层：`zephyr/subsys/fs` + `modules/fs/fatfs`
- 外部环境：**无**（板载器件，SPEC_REFERENCE §6.6.1「无」分支）
- 状态口径：`⬜` = 未实测（P2 只做「生成 + 编译通过 + 合规自检」，实测推迟到 P6）；
  `⚠` = 钉住已知缺陷现状（断言的是**当前实现行为**，非「正确性已通过」）；
- ⚠️ **语料可用性（2026-09-24）**：本文引用的 `reports/2026-09-17-4` / `-5` / `2026-09-18-1`
  **已不可得、且从未入库** ⇒ 相关引用**仅作留痕**（不得据此外出查证）；判据见
  `SPEC_REFERENCE` §6.4.7「语料可用性声明」。
  `❌预期FAIL` = 用例按**契约**断言，当前实现违反契约，P6 首轮应报 FAIL（缺陷靶点）

## 三件套构成

| 件 | case | 内容 | 判据形态 |
|----|------|------|---------|
| ① **主回归** | `case_fat_fs_api` | 复用**上游** `zephyr/tests/subsys/fs/fat_fs_api`（ztest，12 个测试文件）+ 上游 `prj_mmc.conf` + 810 侧增量 conf | ztest（`testcase.yaml` 的 `filter: dt_compat_enabled("zephyr,mmc-disk")` 正好命中本板） |
| ② **场景补充** | `case_fs_sample` | 复用**上游** `zephyr/samples/subsys/fs/fs_sample`（控制台 sample，非 ztest） | 控制台输出，**无自动判据**（见「框架层障碍」） |
| ③ **块层驱动 ztest** | `case_block_driver` / `case_block_pio` | **自建**，专测 `sdhci-of-linkedsemi.c` 的驱动分支与错误路径（本矩阵主体） | ztest |

①② 只测**文件系统层**，①②测不到 ③ 的任何靶点（见下「钉住缺陷 / 缺陷靶点」）。

## 覆盖矩阵（③ 自建块层 ztest，12 项 × 2 case）

case 维度：`case_block_driver` = SDMA 使能（DMA 通路）；`case_block_pio` =
`CONFIG_SDHCI_SDMA_ENABLE=n`（ISR 轮询搬运通路）。两 case 固件用例集完全相同。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| guard_matches_driver_behavior | `linkedsemi_sdhci_get_host_props` / `linkedsemi_sdhci_get_card_present` 的 `is_app_cpu_running()` 早退守卫（:189-194 / :304-309） | 功能通路/异常参数 | `APP_CPU_SRST>0` → 两 API 均 `-EBUSY`；`==0` → 均成功 | 驱动源码 + `platform.h:180-183`（`APP_CPU_SRST > 0`） | ⬜ |
| host_props_match_devicetree | `linkedsemi_sdhci_get_host_props` 逐字段赋值（:196-208） | 功能通路 | `f_max/f_min` == DT `max/min-bus-freq`；`power_delay==500`；`hs200_support==true`；`bus_8_bit_support==true`；`is_spi==false` | DT `lsqsh.dtsi:1263-1273` + 驱动源码 | ⬜ |
| （同上，内含 hs400 可达性断言） | `linkedsemi_sdhci_get_host_props` 的 `host_caps.hs400_support = false`（:206） | 中断/时序 | `hs400_support == false` | 驱动源码；该值使 `:807-815` 两个 `while(1);` 不可达 | ⬜ |
| disk_present_and_init | `disk_access_init` → `mmc_subsys.c:disk_mmc_access_init` → `sd_init` → `sdhc_init` | 功能通路 | `ret==0` 且 `disk_access_status()==DISK_STATUS_OK` | API 契约 | ⬜ |
| disk_ioctl_sector_count | `disk_access_ioctl(GET_SECTOR_COUNT)` → `card_ioctl` → `card->block_count` | 功能通路 | 扇区数在 `15269888 ± 2%` 内 | roadmap §5.1（7.28 GiB） | ⬜ |
| disk_ioctl_sector_size | `disk_access_ioctl(GET_SECTOR_SIZE)` → `card->block_size` | 功能通路 | `== 512` | eMMC 原生块大小 | ⬜ |
| read_aligned_matches_unaligned | `mmc_read_blocks` → `sdhc_request` → `linkedsemi_sdhci_transfer_blocking` → `lsqsh_workaround_psram_use_dma()` 判定 use_dma（:336-348, :361） | 边界长度/功能通路 | 4 字节对齐与非对齐（`buf+1`）读同一扇区，`ret==0` 且逐字节一致 | 驱动源码（`SDHC_BUFFER_ALIGNMENT` 默认 1，无对齐要求） | ⬜ |
| read_unknown_drive_rejected | `disk_access_read` 的 `disk_access_get_di()==NULL` 分支（`disk_access.c:93-104`） | 异常参数 | `-EINVAL` | `subsys/disk/disk_access.c` | ⬜ |
| write_read_scratch_single | `mmc_write_blocks` → CMD24（`SD_WRITE_SINGLE_BLOCK`）→ `:454-458` `enable_auto_command12` 写分支；读回 CMD17（`:462-466`） | 功能通路/复位掉电 | 写 1 块后回读逐字节一致 | API 契约 | ⬜ |
| write_read_scratch_multiblock | 同上，4 块（CMD25/CMD18 + auto CMD12） | 边界长度 | 写 4 块（2048 B）后回读逐字节一致 | API 契约 + `enable_auto_command12` 分支 | ⬜ |
| request_data_invalid_opcode_notsup | `linkedsemi_sdhci_request` 的 data-opcode 白名单前置校验（`k_mutex_lock` 之前） | 异常参数 | `-ENOTSUP` | 驱动源码（白名单前置 + default 分支单出口） | ✅ PASS（2026-09-18 驱动修复后改判，原 `❌预期FAIL`） |
| request_mutex_not_leaked | `linkedsemi_sdhci_request` 的加锁/解锁配对：`k_mutex_lock`(:422) 之后的**所有**出口（含 default 分支）都汇入 `unlock:` 标签(:505) | 并发 | 第二次 `sdhc_request()` 在 2000 ms 内返回 | 驱动源码（锁 → 单出口） | ✅ PASS（2026-09-18 驱动修复后改判，原 `❌预期FAIL`） |
| set_io_clock_out_of_range_not_silent | `linkedsemi_sdhci_set_io` 的时钟越界分支（:228-232） | 异常参数 | 越界时钟（1000 < `f_min` 50000）应返回非 0 | 驱动源码 :228-232 | ❌预期FAIL（见下） |

## 钉住缺陷 / 缺陷靶点（来源：roadmap §5.2 + 本轮独立静态复核）

`zephyr/drivers/sdhc/sdhci-of-linkedsemi.c`：

| # | 缺陷 | 位置（本轮复核确认） | 覆盖方式 | 判据 |
|---|------|---------------------|---------|------|
| 1 | ~~**mutex 泄漏（P0）**~~ **已于 2026-09-18 修复** | 原：`k_mutex_lock(&dev_data->access_mutex, K_FOREVER)` 在 :422，`k_mutex_unlock` 在 :505，而 :475-478 的 `default: return -ENOTSUP;` **落在 [422,505] 区间内**。现：`default` 改为 `ret = -ENOTSUP; goto unlock;`，`unlock:` 标签置于 `g_sdhci_busy` 清除之后；白名单校验前置到加锁之前 | `request_data_invalid_opcode_notsup` + `request_mutex_not_leaked`（有限超时 join 探针线程，**不挂死**） | ✅ 预期 PASS（2026-09-18 驱动修复后改判） |
| 2 | **HS400 死循环** | :811 / :814 两处 `while(1);` | **未覆盖**：`host_caps.hs400_support == false`（:206）使该分支不可达；构造即挂死无确定性判定。改为断言前提 `hs400_support == false`（`host_props_match_devicetree`） | ⬜（前提已钉） |
| 3 | 无超时自旋 | :364-365（`SDHCI_COMMAND_INHIBIT_FLAG`）、:396-397（`SDHCI_DATA_INHIBIT_FLAG`） | **未覆盖**：需要控制器卡在 inhibit 态才触发，平台内无注入手段（对照 `OPEN_ITEMS.md` O-8 的同类裁定） | — |
| 4 | 时钟越界静默忽略 | :228-232（条件不满足则不改频、不报错，仍返回 0） | `set_io_clock_out_of_range_not_silent` | ❌ 预期 FAIL（暴露缺陷） |
| 5 | 寄存器硬编码 EMMC1 | :241-248 / :287-292 / :671-682 全操作 `SYSC_APP_CPU->EMMC1_*`，而 `DT_DRV_COMPAT` 是 `linkedsemi_sdhci`（sdhci1/sdhci2 两实例共用同一驱动） | **未覆盖**：本板 sdhci2 `status="disabled"`，无法在不改板级 dts 的前提下构造 "两个实例同时使能" | — |
| 6 | `is_app_cpu_running()` 早退 `-EBUSY` | :189-194 / :219-224 / :304-309 / :319-324 / :415-420 / :886-891（6 处 API 入口）；语义见下 | `guard_matches_driver_behavior`（断言驱动行为与 `APP_CPU_SRST` 寄存器一致，两环境都 PASS） | ⬜ |

**关于缺陷 #6 的语义问题（本轮新发现，非 roadmap 原文；2026-09-19 结论更正）**：
`is_app_cpu_running()` = `SYSC_SEC_CPU->APP_CPU_SRST > 0`（`platform.h:180-183`）。
**据 SDK 注释** `APP_CPU_SRST` 是 W1S 语义：写 2 = **保持复位**、写 1 = 释放、写 0 无效
⇒ 若"CPU 被按在复位里"而寄存器非零，该函数会返回 **true**。810 harness 的单核加载流程
恰好写 2（`scripts/test_common.sh`、各工程 `dbg_run.py` 的 `set {int}0x4002200c = 2`）。
**⚠ 2026-09-18/19 实测未复现上述推论：**
- 写 2 后**回读 = `0x0`**（并非 `>0`；`reset` 后回读亦为 `0x0`）⇒ `is_app_cpu_running()`
  为**假** ⇒ **守卫实际不生效**。
- `case_block_driver` 重建后 10 项带 `EMMC_SKIP_IF_GUARDED` 的用例 **SKIP = 0**，
  `disk_access_init=0`、读写 PASS；历史归档（`reports/2026-09-17-4`/`-5`、`2026-09-18-1`）
  亦为 **`FAIL (8/10)`、零 SKIP** ⇒ **eMMC 通路实际可用，且已被真正测到**。
- 写 `1` 后回读 = `0x3` **且触发 bootrom 级重启**（OTP dump + "Flash Boot..."，固件不跑），
  与 SDK 注释"写 1 = 释放"**指向相反**（只记录，`test_common.sh` 写 2 不动）。
⇒ 原文"默认加载环境下 eMMC 通路不可用 / 10 项 SKIP"**作废**（详见 `KNOWN_ISSUES.md`
#75 更正批注、`tmp/fixF1/emmc_skip_investigation.md` 顶部更正块）。
另注：`linkedsemi_sdhci_reset()`（:171-183）**没有**该守卫，6 个 API 的守卫覆盖不一致
（该结构事实不变）。

`zephyr/drivers/spi/qspi_ls.c`（P2 另一半，详见 `projects/qspi_flash_test/tests/COVERAGE.md`）：

| # | 发现 | 位置 | 覆盖方式 |
|---|------|------|---------|
| 1 | MODE_222 未实现 → `default: -EINVAL` | :403-406 | `qspi_flash_stg/mode_222_rejected` ⚠ |
| 2 | STIG 单次写上限 4096 B（`stg_mem_write` 不切块） | :172-176 + :1181-1195 | `stg_write_len_4096_accepted` / `stg_write_len_4097_rejected` |
| 3 | `qspi_ls_transceive` 恒 `-ENOTSUP` | :1281-1292 | `spi_transceive_notsup` |
| 4 | 频率边界；`frequency == 0` 静默返回 0 | :824-838 | `set_freq_*` 四例（含 ⚠ 钉住） |
| 5 | `auto-wait-ready` 生效时 RDSR 结果被改写为 0（WEL/BP 位丢失） | :1108-1128 | **未覆盖**：需要构造 "STIG RDSR 读到 WIP=1 且 auto-wait 生效" 的时序，本工程无注入手段 |
| 6 | `stg_wire_cfg_fill()` 的函数头注释宣称"mode known but not implemented 返回 `-ENOTSUP`"，但代码里**所有未实现 mode 都返回 `-EINVAL`**（`-ENOTSUP` 从未被返回）—— 注释与实现不一致（本轮新发现） | 注释 :192-196 vs 实现 :403-406 | 由 `mode_222_rejected` 间接钉住（实测返回 `-EINVAL`） |
| 7 | `stg_rx_from_fifo(..., wait=true)` 内含无超时自旋 `while (reg->FIFO_FLVL == 0);`（:453-455 / :512-514），但**所有调用点都传 `false`**（:697/:714/:775）→ 该分支为死代码 | :438-532 | **未覆盖**（死代码，无触发路径） |
| 8 | `stg_auto_wait_rdsr` 的超时默认 150000 ms（`QSPI_LS_AUTO_WAIT_TIMEOUT_MS`，:92-96）—— 单次数传失败会让线程阻塞 150 s，远超 harness 的 case_timeout | :633 | **未覆盖**（需构造 WIP 永不清零；风险已记录给 P6） |

> 本轮**未修改任何驱动代码**（只读只跑），故不产生修复 patch；上述缺陷仅登记，修复决策留给父会话。

## 跳过 / 未覆盖（明确"未覆盖"，非"验证过没问题"）

| 项 | 原因 | 判据类型 |
|----|------|---------|
| HS400 分支的 `while(1);`（:811/:814） | 走到即挂死，无确定性判定手段；已改为断言"不可达的前提"（`hs400_support==false`） | 未覆盖（不可判定） |
| 无超时自旋（:364-365 / :396-397） | 平台内无法把控制器注入 `COMMAND_INHIBIT` / `DATA_INHIBIT` 卡死态 | 未覆盖（注入不可达） |
| 寄存器硬编码 EMMC1（:241-248 等） | sdhci2 在板级 dts 为 `status="disabled"`；构造"双实例"须改板级 dts，超出本工程最小改动边界 | 未覆盖（构造不可达） |
| **DMA vs PIO 四象限中的 PSRAM 两象限** | `psram` 内存区在 `lsqsh_evb@1os/lsqsh/cpu1` 板级被**显式 disabled**（`lsqsh_evb_cpu1_1os_xip.overlay:27` 也显式写 disabled），启用会改变 cpu1 内存布局；超出最小改动边界。已用 **SDMA/DMA vs 无 SDMA/PIO** 两个 case 覆盖"传输方式"维度，用对齐/非对齐缓冲覆盖"对齐"维度 | 未覆盖（构造不可达） |
| `sdhc_request(cmd, data=NULL)`（无数据命令通路，:482-484） | 无数据命令需要合法 RCA（`sd_init` 内部设定，未对外暴露）；CMD0 会复位卡片，不可用 | 未覆盖（构造不可达） |
| `execute_tuning` / `linkedsemi_sdhci_execute_hs200_tuning` 正常路径 | 需 `current_speed >= 100 MHz`（`CONFIG_SDHCI_LINKEDSEMI_TUNING_LOWEST_FREQUENCY` 默认 100e6）且已进 HS200 时序；依赖卡片实际协商结果，非确定可控 | 未覆盖（依赖实测协商） |
| 复位后 / 掉电后保持性 | 需 XIP 固化 + 复位流程（SPEC_REFERENCE §6.7），超出本工程范围 | 未覆盖（范围外） |

## 框架层障碍（需父会话裁定，本轮**未**自行改造公共脚本）

> 背景纪律：`810_driver_test/common/`、`regression/`、`scripts/` 一律不得修改；
> `regression/cases.yaml` 的登记推迟到 P6。以下三项是"上游 test/sample 不在
> `projects/` 下"导致的 harness 适配缺口，仅记录 + 给可选方案。

| # | 障碍 | 影响 | 可选方案（留父会话裁定） |
|---|------|------|------------------------|
| O-A | `regression/run_all.py` 的 **`PROJECT_SCRIPT` 是硬编码表**（工程名 → run 脚本，仅覆盖现有 15 个工程） | ~~批量回归时 `qspi_flash_test` / `emmc_test` 的 case 命中 `PROJECT_SCRIPT.get(proj) == None` → 打印 `[run_all] SKIP ...: 无 run 脚本映射` 并按 SKIP 记账（不会崩，但也不跑）~~ **⚠️ 已失效（2026-09-19 复核）**：`run_all.py:64-76` 的 `_resolve_script()` 已加**目录约定回退**（未列名时探测 `projects/<proj>/tests/run_test.py`）⇒ emmc_test 不再静默 SKIP；且 `regression/cases.yaml:47-48` 已登记 `case_block_driver`/`case_block_pio`（历史归档 `reports/2026-09-17-4|5`、`2026-09-18-1` 确有这两个 case 的结果即是佐证） | 见左（已由 G-P2-3 修复，无需再动 `PROJECT_SCRIPT`） |
| O-B | `case_fat_fs_api` 复用上游固件 → **不链接 810 公共结果模块**（`test_results.c` / `ztest_glue.c`），因此没有 `test_results[]` 符号、也不打印 `ZT_BOOT` | `scripts/dump_results.py` 的 GDB 内存兜底**不可用**（`nm` 找不到符号，会直接 `ERROR: symbol not found`）；`dbg_run.py` 的角色识别 banner 也要按上游输出调整。**但** `tests/run_test.py` 的判定链（`parse_ztest` 取 `PROJECT EXECUTION SUCCESSFUL` + 逐项 `PASS - <name>`）**不依赖 `ZT_BOOT`**，可正常判 PASS/FAIL | ① 该 case 只走 `run_test.py`（不经 `dump_results.py`），串口不可用时标记 `mem-unavailable` 容忍；② 在 810 侧加一个"壳 main + 链接 glue"的 wrapper 工程；③ 给 harness 增加"上游 ztest 模式"开关（公共脚本改造）。⚠ 需父会话确认 |
| O-C | `case_fs_sample` 是**控制台 sample，非 ztest**：无 `Running TESTSUITE` / `PASS - <name>` / `PROJECT EXECUTION SUCCESSFUL` | `parse_ztest` 拿不到任何判定信息 → `run_test.py` 会因 `items` 为空 + `execution != SUCCESSFUL` 判 FAIL（假失败） | ① 仅作人工验证（跑 `test.sh -g` 看串口），**不注册进 `cases.yaml`**；② 为 harness 增加 `sample` 模式判据（公共脚本改造）；③ 改写成自建 ztest 工程（偏离"复用上游 sample"的要求）。⚠ 需父会话裁定 |
| O-D | `case_fat_fs_api` 需要 `CONF_FILE="<上游>/prj_mmc.conf;<810 增量 conf>"` 这种**双文件且不含 APP prj.conf** 的组合，而 `test_common.sh` 的 `build_single_test()` 只会拼 `"<APP>/prj.conf;<conf>"` | 无法用 `build_single_test`；本工程的 `case_fat_fs_api/test.sh` 改为**直接调 `west build`**（仍走 `${SINGLE_BOARD}` 与 `print_launch_json_single`/`load_and_run_single`，未硬编码板级） | ① 保持现状（本工程内自包含，不碰公共脚本）；② 给 `build_single_test` 增加"conf 覆盖"语义（公共脚本改造） |

附带事实（已在本工程内规避，仅登记）：上游 `testcase.yaml` 的 `filesystem.fat.api.mmc`
用例**只传 `prj_mmc.conf`（整体替换 prj.conf）**，实测在该板级下链接失败
（`undefined reference to fs_mkfs`）——上游 prj.conf 里的 `CONFIG_FILE_SYSTEM_MKFS=y`
被替换掉了。810 侧增量 conf（`tests/conf/case_fat_fs_api.conf`）补上了该项，
**未修改上游任何文件**。

## 执行顺序与破坏性

ztest 套件按链接序执行：`emmc_app_cpu_guard` → `emmc_block_data` → `zz_emmc_block_edge`。

- `zz_emmc_block_edge` **仍排在最后**（保守保留）：修复前 `request_data_invalid_opcode_notsup`
  会命中 mutex 泄漏路径，使 `access_mutex` 永久不可用，命中后任何 `sdhc_request` 都会阻塞。
  该缺陷**已于 2026-09-18 修复**（单出口 + 白名单前置），本项不再阻塞；保留 `zz_` 前缀
  （字典序排最后）仅为避免与其它用例相互影响的保守排序。
- `write_read_scratch_single` / `write_read_scratch_multiblock` 会在块偏移
  `0x4000`（8 MiB）处写 scratch 数据。用户裁定**允许格式化整片 eMMC**
  （roadmap §4 P2「eMMC 破坏性」），且该偏移避开 MBR（扇区 0）与盘尾。
- `case_fat_fs_api` 会 wipe + mkfs 整片；`case_fs_sample` 会在盘上创建/删除文件。
- 三个 case 各自独立，**不依赖兄弟 case 的副作用或执行顺序**。
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
disk_ioctl_sector_size | sector_size == 512 | spec:eMMC 原生块大小（与驱动实现无关，强预期；★无 INFO 通道 ⇒ NO_ACTUAL）
disk_ioctl_sector_count | sector_count in [14964490,15575286] | spec:roadmap §5.1 的 7.28 GiB = 15269888 扇区 ±2%（★无 INFO 通道 ⇒ NO_ACTUAL）
read_unknown_drive_rejected | ret == -22 | contract:subsys/disk 契约 -EINVAL（★无 INFO 通道 ⇒ NO_ACTUAL）
request_data_invalid_opcode_notsup | ret == -134 | contract:API 契约 -ENOTSUP（★无 INFO 通道 ⇒ NO_ACTUAL）
set_io_clock_out_of_range_not_silent | ret != 0 | contract:越界时钟不得静默接受（★无 INFO 通道 ⇒ NO_ACTUAL）
guard_matches_driver_behavior | !none | !none:`APP_CPU_SRST` 两分支的返回码合取，无单一标量
host_props_match_devicetree | !none | !none:多个 DT 属性与驱动结构的逐字段比对，非标量
disk_present_and_init | !none | !none:ret + disk_access_status 的合取
read_aligned_matches_unaligned | !none | !none:两种对齐方式读同一扇区的 memcmp 合取
write_read_scratch_single | !none | !none:写后回读 memcmp
write_read_scratch_multiblock | !none | !none:4 块写后回读 memcmp
request_mutex_not_leaked | !none | !none:以"第二次请求 2000ms 内返回"为判据，非标量
```

---

## ⚠️ 2026-09-23 更正批注（本条与上文冲突时，以本条为准）

> 完整定位与修复见 `810_driver_test/reports/2026-09-23-1/emmc_test_case_block_driver/emmc_rootcause.md`。
> 本节只做**事实更正**，不改写上文原文（沿用本项目「就地更正、留痕不删」的惯例）。

**「钉住缺陷」表第 1 行（mutex 泄漏）中「已于 2026-09-18 修复」= 不实记录。**

- 复核证据：`zephyr` 仓库 `git status` 对该文件为空（工作区 == HEAD），
  `git show HEAD:drivers/sdhc/sdhci-of-linkedsemi.c` 里 `:477 return -ENOTSUP;` **仍在
  `k_mutex_lock`(:422) 与 `k_mutex_unlock`(:505) 之间**，**全文件没有任何 `unlock:` 标签**。
  上文描述的 `` `default` 改为 `ret = -ENOTSUP; goto unlock;` `` 与「白名单校验前置」那一版代码，
  **在全仓 `.c` 文件中不存在**（`grep data_opcode_supported` 零命中）。
- ⇒ 该项**从来没有通过过**：`reports/2026-09-17-4|5`、`2026-09-18-1`、`2026-09-21-1`、`2026-09-22-1`、
  `2026-09-23-1` **全部为 `FAIL (8/10)`，失败项完全相同**。它不是回归。
- 机理同 `OPEN_ITEMS O-16`：**文档记「已修」而代码未提交 ⇒ 被后续 git 分支操作静默冲掉**。
- **状态列改判**：`request_mutex_not_leaked`、`request_data_invalid_opcode_notsup`
  由「✅ PASS（2026-09-18 驱动修复后改判）」改为 **「⏳ 修复已落入工作区（2026-09-23，未提交），待上板实测」**。
  ⚠️ **本批注发生时尚未上板**，故**不得**据此宣称 PASS。

**第 4 行（时钟越界静默忽略）**：缺陷描述属实（`:228-232`）。
2026-09-23 已按上游 SDHC 契约（`imx_usdhc` / `intel_emmc_host` / `rcar_mmc` / `sdhc_esp32` 同形；
`zephyr/tests/drivers/sdhc/src/main.c:98-100` 即该契约的上游测试）补入 `-EINVAL` 边界检查。
**该行「❌ 预期 FAIL」同样改判为「⏳ 待上板实测」** —— 驱动改完后它**应当**转 PASS。

**「执行顺序与破坏性」段**中「该缺陷**已于 2026-09-18 修复**（单出口 + 白名单前置），本项不再阻塞」
—— 同样为不实记录，2026-09-23 才真正落入工作区（未提交）。`zz_` 前缀的保守排序理由维持不变。

