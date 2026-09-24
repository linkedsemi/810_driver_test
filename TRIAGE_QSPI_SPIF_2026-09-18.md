# C1+C2 专项定位报告 —— `qspi_flash_test` / `spif_test` FAIL 性质判定

> **日期**：2026-09-18　**轮次**：`reports/2026-09-18-1`（另比对 `2026-09-17-4` / `-5` 两轮）
> —— ⚠️ **该批会话（`2026-09-17~18`）已不可得、且从未入库** ⇒ 文中引用**仅作留痕**；
> 留存范围与纪律见 `SPEC_REFERENCE` §6.4.7「语料可用性声明」。
> **范围**：只定位、不改代码。**未改动任何驱动、用例、`common/`、`regression/`、`scripts/`。**
> **执行方式**：单个 agent 完成全部分析，未派子 agent。
> **证据分级**：`实测`（反汇编 / DWARF / 构建产物 / DT / SDK 头文件） > `源码推导`（逐行复核） > `推测`。
> 凡"判断该场景不会发生"与"验证过没问题"分开表述。

## 0. 执行环境说明（影响证据可得性）

- **本轮未取得实机运行数据**：启动 `DebugServerConsole.exe` 失败 ——
  `ERROR: No XuanTie CKLINK has been connected to PC or XuanTie CKLINK driver isn't installed correctly!`
  （重试 2 次一致）。USB 枚举只有 `JLINK` / `JLink CDC UART (COM3)` / `USB 设备 (COM18)`，
  **无 CKLink**；同轮成功跑过的 `COM16/COM17`（CH342）也已不在枚举中。
  ⇒ 板子当前**物理不在线**（或由其它并发 runner 占用），本报告无法做烧录复现。
- 因此本轮所有结论来自：**驱动/芯片源码逐行复核 + 构建产物反汇编 + DWARF 结构布局 + 三轮历史运行结果对比**。
- 历史运行数据可用：`2026-09-17-4`、`2026-09-17-5`、`2026-09-18-1` 三轮 qspi 结果
  在**逐项 PASS/FAIL 上完全一致**（见 §1.0 表），可用于区分"确定性 / flaky"。
- 串口 ZT_INFO 在所有报告中**均未被采集**（harness 走 GDB 内存读结果，
  `report.json` 无 `info` 字段）⇒ 缺少数值型直接证据的那几项只能标 `待定`。

---

## 1. `qspi_flash_test`

配置差异（关键前提）：
- `case_default`：`spi-max-frequency = <DT_FREQ_M(50)>`，**时序校准开启**
- `case_no_calib`：`spi-max-frequency = <DT_FREQ_M(25)>`，`timing-calibration-disabled`
- 用例源码与固件**完全相同**，差异只在 overlay 与 `test.sh`

### 1.0 三轮结果一致性（用于 flaky 判定）

| 测试项 | 17-4 default | 17-5 default | 18-1 default | 17-4 no_calib | 17-5 no_calib | 18-1 no_calib | 判定 |
|---|---|---|---|---|---|---|---|
| set_freq_boundary_6m25_accepted | FAIL | FAIL | FAIL | PASS | PASS | PASS | 确定性（default 专有） |
| set_freq_zero_silent | FAIL | FAIL | FAIL | PASS | PASS | PASS | 确定性（default 专有） |
| erase_sector_all_ff | FAIL | FAIL | FAIL | PASS | PASS | PASS | 确定性（default 专有） |
| write_read_len1 | **PASS** | FAIL | FAIL | PASS | PASS | PASS | **flaky（default 专有）** |
| write_read_len255/256/4096/8192 | FAIL | FAIL | FAIL | PASS | PASS | PASS | 确定性（default 专有） |
| read_past_end_rejected | FAIL | FAIL | FAIL | FAIL | FAIL | FAIL | **确定性（两 case 皆挂）** |
| write_past_end_rejected | FAIL | FAIL | FAIL | FAIL | FAIL | FAIL | **确定性（两 case 皆挂）** |
| raw_read_1_1_1_ok | FAIL | FAIL | FAIL | FAIL | FAIL | FAIL | **确定性（两 case 皆挂）** |
| stg_read_chunked_over_4096 | FAIL | FAIL | FAIL | FAIL | FAIL | FAIL | **确定性（两 case 皆挂）** |

⇒ 本组**无 true-flaky**，唯一不稳定项是 `write_read_len1`。

### 1.1 逐项判定

| # | 测试项 | 现象 | 性质判定 | 证据 | 建议动作 |
|---|---|---|---|---|---|
| Q1 | `raw_read_1_1_1_ok` | `raw_op(JESD216_MODE_111, READ 0x03, addr_len=3, dummy=0)` 返回 0，但回读 256 B 与写入不符 | **用例预期错**（高置信） | ① `qspi2` 的 `spi-ctrl-caps-mask = <0x40000>`（zephyr.dts 实测）＝ `SPI_NOR_MODE_4_4_4_CAP`（`zephyr/include/zephyr/drivers/spi_nor.h:158`）；② `spi_nor_multi_dev.c:2644-2647` 在该位上电即调 `spi_nor_enter_qpi_mode()` ⇒ 器件进 **QPI 4-4-4**，`flag_qpi_mode=true`（`spi_nor_multi_dev.c:1058-1072` 实测）; ③ 容量 32 MiB > 16 MiB ⇒ `:2676` 调 `spi_nor_config_4byte_mode(dev,true)` ⇒ `flag_access_32bit=true`（`:2085-2087` 实测），flash 层存储访问一律 **4 字节地址**（`:1380` `data->flag_access_32bit ? 4 : 3`）；④ 用例却发 **1 线 + 3 字节地址**，模式与器件当前状态不符；⑤ 同族 `raw_read_1_1_4_ok`/`raw_read_1_4_4_ok` 只断言 `ret==0` 故 PASS —— 反证"驱动接受参数即可，内容不受保证" | 用例改判据：raw 读须与驱动实际模式一致（`MODE_444` + `addr_len=4`），或先 `spi_nor_exit_qpi_mode()` 并确认 3B 地址态，或明确标注该 raw 路径不适用于 QPI/4B 配置。**不改驱动** |
| Q2 | `stg_read_chunked_over_4096` | 同上（`MODE_111` + `0x03` + `addr_len=3` 读 8192 B） | **用例预期错**（同 Q1） | 同 Q1；失败断言是 `zassert_mem_equal`（内容），`zassert_ok(ret)` 已过 ⇒ 驱动"接受并完成"，内容错位 | 同 Q1。另注：该用例本意是验 `stg_mem_read` 的 4096 切块（`qspi_ls.c:1160-1176`），**切块逻辑本身未被证伪**，改判后可继续覆盖 |
| Q3 | `read_past_end_rejected` | `flash_read(dev, 0x2000000, buf, 16)` 返回 ≠ `-EINVAL` | **待定**（证据指向运行期 `flash_size > 32 MiB`；次可能：前置 `re_init` 返回其它 errno） | ① 源码 `spi_nor_multi_dev.c:1385` 确实有 `(addr<0)||(addr+size)>flash_size` 检查；② **反汇编证实该检查已编入固件**：`spi_nor_read` @0x10009b98 末段 `li s0,-22` / `bltz s3` / `add s3,s3,s4` / `bltu a3,s3`，`a3` 由 `lw a3,100(data)` 载入；③ **DWARF 实测** `struct spi_nor_data` 成员偏移：`flag_access_32bit=48, flag_qpi_mode=49, cmd_info=52, …, flash_size=100, layout=104` ⇒ 比较对象确为 `data->flash_size`；④ `api->read` 实测经 `device_area`/`rodata` 字节核对 = `0x10009b98`（spi_nor_read），调用链无误；⑤ **约束推断**：`size_is_32mib` PASS ⟹ `layout.pages_count=512`（`setup_pages_layout`：`pages_count = flash_size / CONFIG_SPI_NOR_FLASH_LAYOUT_PAGE_SIZE`，后者实测 = 65536）⟹ `flash_size < 0x2010000`；与本项 FAIL ⟹ `flash_size ≥ 0x2000010` ⇒ **`flash_size ∈ [0x2000010, 0x200FFFF]`**；⑥ 次可能机制：`init_ok==false` 时 `spi_nor_re_init` 的返回值会**先于**边界检查返回（反汇编 0x10009c1a 分支），若其返回非 `-EINVAL` 亦复现本 FAIL | 实机一行诊断即可定性：在用例中 `ZT_INFO` 打印 `ret` 与 `flash_get_parameters()->flash_size`（或打开驱动 `LOG_INF` 的 `"N MiB flash"`，`spi_nor_multi_dev.c:2110`/`:2715`）。**本轮不改代码** |
| Q4 | `write_past_end_rejected` | `flash_write(dev, 0x2000000, buf, 16)` 返回 ≠ `-EINVAL` | **待定**（同 Q3） | 同 Q3（检查在 `:1476`，结构同型）。注意 `flash_size` 为同一字段 | 同 Q3 |
| Q5 | `erase_sector_all_ff` | 擦除后回读 4096 B，首个非 `0xFF` 字节存在 | **疑似驱动缺陷**（50 MHz + 校准开启下 QPI 存储读数据损坏）；备选：50 MHz 超器件/板级能力（环境） | ① **仅在 `case_default`（50 MHz、校准开）复现**；`case_no_calib`（25 MHz、校准关）**全绿**（同固件同用例）；② 失败断言是"读出内容非 FF"，属**读侧**损坏（擦除完成由 `auto-wait-ready` 等 WIP，`:1108-1128`）；③ 驱动校准（`qspi_ls.c:937-1048`）在 50 MHz 下用 **RDID(0x9F，寄存器读、无地址/数据相)** 判定有效窗口并选点（`calib_pick_best` `:905-924`），再 `stg_capture_apply_calibrated()` 应用到**所有** STIG 事务 —— RDID 的采样窗口不必然覆盖**存储读（地址+数据相）**，这是"校准报 SUCCESS 但存储读损坏"的机制解释（**源码推导**，未实机观测） | 先做分离实验（见 §7 E1）：`50 MHz + timing-calibration-disabled`。若也失败 ⇒ 频率/器件侧；若通过 ⇒ 校准缺陷（驱动） |
| Q6 | `write_read_len255/256/4096/8192` | 擦→写→读，回读与写入不符 | **疑似驱动缺陷**（同 Q5） | 同 Q5。四项在 default 下 3/3 轮 FAIL，no_calib 下 3/3 轮 PASS | 同 Q5 |
| Q7 | `write_read_len1` | 同上，len=1 | **flaky**（同族驱动缺陷的边界表现） | 三轮：`17-4 PASS` / `17-5 FAIL` / `18-1 FAIL`。1 B 传输受 FIFO/采样边界影响最大 ⇒ 最接近临界点的用例先翻 | 同 Q5；该用例同时应登记为**回归稳定性观察项** |
| Q8 | `set_freq_boundary_6m25_accepted` | `spi_nor_re_init()` 返回非 0（期望 0） | **待定**（仅校准开启时复现） | ① **仅 `case_default` FAIL**，`case_no_calib` PASS（同用例同断言）；② 两项（Q8/Q9）共同点：`config->frequency < calib_hz(10 MHz)` ⇒ 命中 `qspi_ls.c:957-961` 分支，**把 `data->calib_combo` 强制改写为 1**（丢弃启动校准得到的 combo）；③ `apply_clk()` 本身对 6.25 MHz（`clk_cyc=31` ≤ 31）返回 0、对 0 Hz 静默返回 0（`:824-838`）⇒ 失败点在 `spi_nor_configure` 其它步骤（JEDEC/SFDP 读或 PM `exit_dpd`），**无运行时日志无法定位** | 实机抓驱动 `DEV_ERR`/`DEV_INF` 串口日志（本轮串口数据缺失）。**本轮不改代码** |
| Q9 | `set_freq_zero_silent` | 同上（`re_init` 非 0） | **待定**（同 Q8） | 同 Q8。注：该用例**本意是钉住驱动缺陷**（`frequency==0` 静默不改时钟），现因 `re_init != 0` 而 FAIL ⇒ 目前它既没钉住"静默"，也没暴露"报错" | 同 Q8 |

> 其余 17 项（含钉住缺陷的 `mode_222_rejected`、`stg_write_len_4097_rejected`、`spi_transceive_notsup`、
> `set_freq_above_max_rejected`、`set_freq_below_min_rejected`、`four_byte_mode_unknown_mfr`）全部 PASS，
> 见 §3 的"已登记缺陷实测复现"。

---

## 2. `spif_test`（3 项 FAIL / 15 项）

| # | 测试项 | 现象 | 性质判定 | 证据 | 建议动作 |
|---|---|---|---|---|---|
| S1 | `cmd_idx_accessors`（`spif_test.c:323`） | `spif_set_dummy_by_idx(dev, 9, 5)` 后 `spif_get_dummy_by_idx(dev,&dummy,5)` 回读 ≠ 9 | **疑似用例预期错 / 待定**（该寄存器 DUMMY_CYCLE 字段回读不可靠） | ① **同一寄存器**上的 CMD 往返**通过**（`:310-311` set 0x77 → get 0x77 PASS）⇒ 命令表寄存器可读写，问题只落在 `DUMMY_CYCLE` 字段；② 驱动 `spif_set_dummy_by_idx` / `spif_get_dummy_by_idx`（`spi_filter.c:1011-1075`）是标准 read-modify-write，无 NULL/边界缺陷（`idx=5` 在界内，COVERAGE 上游缺陷 #4/#5 不适用）；③ 字段定义 `spif_cmd_t{CMD[7:0], DUMMY_CYCLE[12:8], reserved[30:13], EN[31]}`（`reg_spi_filter.h:158-167`）与用例的 5-bit 预期一致 ⇒ **用例位宽预期没错，是回读值不符**；④ 结论只能落在"该 HW 字段是否可写/可回读"，需实机读回值定性（本轮无串口数据） | 实机打印回读值：若恒为 0 ⇒ 该字段只写不回读，用例应改判据（或改测 `SPIF_READ_DUMMY_NUM(0x10)` 通路），并把"命令表 DUMMY 字段不可回读"记入已知 HW 事实。**不改驱动** |
| S2 | `addr_whitelist_fill`（`spif_test.c:418`） | `spif_memset_addr_whitelist(dev,1)` 后直读 `base+0x800` ≠ `0xffffffff` | **用例预期错**（高置信）：地址有效表**不可直接 `sys_read32` 读回** | ① 同族 `num=0`（期望 0）的 4 条直读断言（`:411-414`）**全部通过** ⇒ 可反推"该窗口直读恒为 0"（若是"读回上次写入值"，`num=1` 应当通过）；② **驱动自身从不直接读该表**：`CONFIG_SPI_FILTER_ADDR_WHITELIST_BUF=y`（本工程 `.config:367` 实测）时走 SW 镜像（`spi_filter.c:455-469`）；非 BUF 时走**间接 SRAM 端口**（`:473-505`：写 `SPIF_READ_SRAM_ADDR=0x174` → 置 `SPIF_READ_ADDR_REQ=0x170` → 轮询 CS 空闲 → 读 `SPIF_READ_SRAM_DATA=0x178`）；③ 存在专用**间接读端口**（`reg_spi_filter.h:100-103`）⇒ 该 SRAM 表（写窗 `0x800-0xfff`、读窗 `0x1000-0x17ff`）不是普通可直读寄存器阵列；④ 写入侧驱动确为直写 `sys_write32` ⇒ "写有效、直读无效"自洽 | 用例改为经镜像/间接端口回读（或在测试侧复刻 `spif_peek_rw_area` 的 SRAM 读时序），或删除直读回读断言、改**行为层**判据。**不改驱动** |
| S3 | `addr_privilege_valid_region`（`spif_test.c:497`） | `spif_address_privilege_config(READ/ENABLE, MB(1), KB(16))` 后直读 `base+0x1000+2*4` ≠ `0x1` | **用例预期错**（同 S2，高置信） | 同 S2（同一族表、同一读法）。补：驱动写入路径 `spif_address_privilege_config_op` 在 `bit_off<32` 分支同时写 SW 镜像与 HW（`spi_filter.c:718-726`），地址换算 `reg_off=addr/KB(512)=2`、`bit_off=0` 与用例一致 ⇒ **换算与写入无歧义，分歧只在"能不能直读回来"** | 同 S2。若改为经镜像/间接端口回读，本用例可完整保留其"位图置 1/清 0"的判据价值 |

> **观察项（非 FAIL，但值得登记）**：`master_read_jedec_id` 的 INFO 实测为 `jedec=ffffff`
> （COVERAGE 声明"不预设厂商值"故 PASS）。spif3→spi4 外部 NOR 读 ID 返回全 `0xFF`，
> 疑似器件不在位 / CS(GPIOI0) / 线序问题，**建议登记为待查观察项**，不要当成通过。

---

## 3. 与既有缺陷档案（`DEFECT_ANALYSIS_2026-09-17.md`）的对应关系

### 3.1 已登记缺陷的**实测复现**（被测工程把缺口"钉"住了 → PASS）

| 档案条目 | 位置 | 本轮对应用例 | 结果 |
|---|---|---|---|
| qspi#1 `MODE_222` 未实现落 `default: -EINVAL` | `qspi_ls.c:403-406` | `mode_222_rejected` | **PASS**（实测复现，钉住） |
| qspi#2 STIG 单次数据相上限 4096 B | `qspi_ls.c:172-176` + `:1181-1195` | `stg_write_len_4096_accepted` / `stg_write_len_4097_rejected` / `stg_reg_read_len_4097_rejected` | **PASS**（实测复现，钉住） |
| qspi#3 `qspi_ls_transceive` 恒 `-ENOTSUP` | `qspi_ls.c:1281-1292` | `spi_transceive_notsup` | **PASS**（实测复现，钉住） |
| qspi#4 频率边界 `<6.25MHz`/`>200MHz` → `-EINVAL` | `qspi_ls.c:824-838` | `set_freq_above_max_rejected` / `set_freq_below_min_rejected` | **PASS**（实测复现，钉住） |
| qspi#4 的"`frequency==0` 静默"半边 | `qspi_ls.c:824-826` | `set_freq_zero_silent` | **本项 FAIL**（因 `re_init` 非 0 提前失败，未走到静默断言）—— 见 Q9 |
| `spi_nor_get_4byte_mode` 厂商白名单无 Puya | `spi_nor_multi_dev.c:1953-1990` | `four_byte_mode_unknown_mfr` | **PASS**（实测复现，钉住） |
| `spif_add_cmd*` 过期 idx 表外野写（P0） | `spi_filter.c:884`/`:938` | **本工程按 P0 纪律主动规避**（一律走 `spif_add_general_cmd` + `spif_cmd_in_fixed_table()`） | 未触发，防线有效；本轮 3 项 FAIL 与 #1 **无关** |
| `spif_set_cmd_by_idx` 等 `uint8_t idx` 无上界（#5） | `:970/:1021/:1162` | 用例 idx 均为 0..7（界内） | 未触发 |
| `spif_get_cmd_by_idx`/`get_dummy_by_idx` 出参无 NULL 检查（#4） | `:1052/:1070` | 用例出参均非 NULL | 未触发 |
| `addr_privilege_guard` 的未配对 `release_spif_device()`（#14） | `:653-657`→`:732-733` | `addr_privilege_guard` | **PASS**（该用例注释已论证 `k_sem` limit=1 下等价空操作） |

### 3.2 本轮 FAIL 与档案的对应

- **Q1 / Q2（raw 1-1-1 读）**：档案**未覆盖**。根因是 `spi-ctrl-caps-mask` + 容量共同触发的
  **QPI 4-4-4 + 4 字节地址**运行态，属**新发现**（§4）。
- **Q3 / Q4（past_end 未拒）**：档案**未覆盖**（`spi_nor_multi_dev.c` 的边界检查本身没被审过）。
- **Q5–Q7（default 下擦写读）**：档案只登记了 `qspi_ls.c` 的静态缺口，**未涉及校准正确性** ⇒ 新发现（§4）。
- **Q8 / Q9**：与档案 qspi#4 的"`freq==0` 静默"条目相关但**不是同一现象**（见 Q9）。
- **S1 / S2 / S3**：档案（`spi_filter.c` 19 项）+ COVERAGE（上游缺陷 #1–#20）**均未涉及**
  "地址有效表能否直读""DUMMY_CYCLE 能否回读" ⇒ 新发现（§4）。

---

## 4. 新发现（本轮新增，建议入 KNOWN_ISSUES / COVERAGE）

1. **qspi2 上电即进入 QPI 4-4-4 + 4 字节地址模式（组合触发）**
   `spi-ctrl-caps-mask = <0x40000>`（= `SPI_NOR_MODE_4_4_4_CAP`）使 `spi_nor_configure()`
   调 `spi_nor_enter_qpi_mode()`（`spi_nor_multi_dev.c:2644-2647`）；
   同时 `flash_size(32 MiB) > 0x1000000` 使 `:2676` 调 `spi_nor_config_4byte_mode(dev,true)`。
   ⇒ 任何**裸 `spi_nor_op`** 读写都必须是 `MODE_444` + `addr_len=4`。
   `qspi_flash_test` 的 `raw_op()` 辅助**未适配此运行态**（这是 Q1/Q2 的直接原因）。

2. **同族"弱断言"用例存在假 PASS 风险**
   `raw_read_1_1_4_ok` / `raw_read_1_4_4_ok` 只断言 `zassert_ok(ret)`（驱动接受参数），
   **不断言内容**。在 QPI/4B 运行态下它们同样发出与器件不匹配的 1 线/3 字节地址事务，
   却仍 PASS ⇒ 这两个"PASS"不构成"1-1-4 / 1-4-4 通路可用"的证据。

3. **时序校准的采样窗口判定只基于 RDID（寄存器读）**
   `qspi_ls_timing_calibration()`（`qspi_ls.c:997-1006`）以 `RDID(0x9F)` 的 3 字节回读
   作为 combo 有效性的唯一判据，选点后应用于所有 STIG 事务。RDID 无地址/数据相，
   与 `stg_mem_read()` 的采样路径不同 ⇒ 校准"成功"不蕴含存储读正确。
   （**源码推导**；是否属实需 §7 E1 分离实验。）

4. **`spif` 地址有效表（0x800-0x17ff）不可直接 `sys_read32` 读回**
   驱动自身提供专用间接 SRAM 读端口（`SPIF_READ_ADDR_REQ/SRAM_ADDR/SRAM_DATA`），
   并在 BUF 配置下改读 SW 镜像 ⇒ 该表对 CPU 直读无效（表现为恒 0）。

5. **`spif` 命令表 `DUMMY_CYCLE[12:8]` 字段回读异常**（CMD 字段正常）。
   待实机确认"写不进"还是"读不回"。

---

## 5. 明确不确定的项（宁可标"待定"，不猜）

| 项 | 不确定的是什么 | 缺什么证据 |
|---|---|---|
| Q3 `read_past_end_rejected` | 运行期 `data->flash_size` 实际值；或是否 `re_init` 先行返回了其它 errno | 一行 `ZT_INFO` / 驱动 `LOG_INF "N MiB flash"` |
| Q4 `write_past_end_rejected` | 同上 | 同上 |
| Q8 `set_freq_boundary_6m25_accepted` | `spi_nor_re_init` 在 6.25 MHz 下的具体失败点 | 驱动 `DEV_ERR` 串口日志 |
| Q9 `set_freq_zero_silent` | 同上（0 Hz 下总线停留在上一频率） | 同上 |
| Q5–Q7 | "驱动校准缺陷" vs "50 MHz 超器件/板级能力" 无法区分 | §7 E1 分离实验 |
| S1 `cmd_idx_accessors` | DUMMY_CYCLE 是"写不进"还是"读不回" | 实机回读值 |

> 以上六项**均未在本报告中给出确定性结论**。

---

## 6. 待修清单（按性质分组）

### A. 驱动缺陷（需上游裁决；本轮不改）

| 项 | 位置 | 建议 |
|---|---|---|
| A1（疑似，待 E1 确认）**时序校准采样窗口判据不足**：以 RDID 单点判据选 combo 并应用于存储读，导致 50 MHz 下 QPI 存储读数据损坏 | `qspi_ls.c:937-1048`（`calib_read_jedec_id` / `calib_pick_best` / `stg_capture_apply_calibrated`） | 校准判据改用**存储读回环**（写已知 pattern → 按 combo 读回比对），或对读/写分别校准 |
| A2 校准路径把 `calib_combo` 强制改写为 1，丢弃已校准结果 | `qspi_ls.c:957-961` | 低时钟（`calib_hz > config->frequency`）时不应破坏已校准 combo；或明确"低于校准时钟即回退默认采样" |

> A1/A2 的依据是 §7 E1 的预期结果；**在 E1 跑出来之前不应据此改代码**。

### B. 用例预期错（只报告"该改成什么"，**本轮不自行修改**）

| 项 | 位置 | 建议改法 |
|---|---|---|
| B1 | `qspi_flash_test.c` `raw_op()` + `raw_read_1_1_1_ok` / `stg_read_chunked_over_4096`（Q1/Q2） | 与驱动运行态对齐：`MODE_444` + `addr_len=4`；或在用例内显式 `spi_nor_exit_qpi_mode()` + 退出 4B 模式后再发 1-1-1/3B；并**补上内容断言** |
| B2 | 同上 `raw_read_1_1_4_ok` / `raw_read_1_4_4_ok` | 现为弱断言（假 PASS 风险），应补内容断言或明确标注"只验参数接受" |
| B3 | `spif_test.c:497` `addr_privilege_valid_region`（S3） | 回读改为经 SW 镜像或间接 SRAM 端口；位图判据本身保留 |
| B4 | `spif_test.c:418` `addr_whitelist_fill`（S2） | 同上；或删除"直读回读"型断言，改行为层判据 |
| B5（待 S1 定性后） | `spif_test.c:323` `cmd_idx_accessors`（S1） | 若确认 DUMMY_CYCLE 不可回读 ⇒ 改为断言"写入不报错 + 通过 `SPIF_READ_DUMMY_NUM` 通路验证"，并注明 HW 限制 |

### C. 环境问题

| 项 | 说明 |
|---|---|
| C1 | 本轮**板子不在线**（无 CKLink，串口 COM16/17 消失），未能实机复现。上述"待定"项应在板子恢复后第一时间补测 |
| C2（备选假设，待 E1 排除） | 50 MHz 可能超出该 Puya 器件（或板级走线）在 QPI 模式下的可靠工作范围 |

### D. flaky

| 项 | 表现 | 处置 |
|---|---|---|
| D1 | `qspi_flash_test::write_read_len1`（default 下）三轮 1 PASS / 2 FAIL | 登记为**回归稳定性观察项**；随 A1 修复后复测是否收敛 |

---

## 7. 建议的验证实验（板子恢复后，最小代价定性）

> 全部为**新增临时用例/临时 overlay**，不改既有用例；每项都能把 §5 的"待定"变成确定结论。

- **E1（分离校准 vs 频率）**：复制 `case_default` 为临时 case，仅把 `&qspi2` 加 `timing-calibration-disabled`
  （保持 50 MHz）。
  - 若 Q5–Q7 仍 FAIL ⇒ 问题在**频率/器件/板级**（C2），校准无罪；
  - 若转 PASS ⇒ 问题在**校准选点**（A1），且 §3.1 的 `set_freq_*` 两项也可一并解释。
- **E2（打印 `flash_size` 与返回值）**：在临时用例中输出
  `flash_get_parameters()->flash_size` 与 `flash_read(0x2000000,…)` 的 `ret`（Q3/Q4）。
- **E3（抓驱动日志）**：串口捕获 `qspi_ls.c` 的 `DEV_ERR`/`DEV_INF`
  （`clk_cyc out of range` / `timing calibration success` / `hw config: spi=… Hz cap_neg=… cap_dly=…`）
  与 `spi_nor_multi_dev.c` 的 `"N MiB flash"`（Q8/Q9 + Q3/Q4 一并解决）。
- **E4（spif 表回读）**：临时用例用 `spif_peek_rw_area` 等价路径（BUF=y 时读镜像）回读
  `write/read_addr_whitelist[]`，并在直读 `sys_read32` 处打印实际值（S2/S3 定性）。
- **E5（spif DUMMY 回读值）**：打印 `spif_get_dummy_by_idx()` 的实际值 + 直读命令表寄存器原始 32 bit（S1 定性）。

---

## 8. 落盘确认

- **交付文件**：`d:\zephyr4_0\810_driver_test\TRIAGE_QSPI_SPIF_2026-09-18.md`（本文件，新建）
- **未改动**：`zephyr/drivers/**`、`modules/lib/pfr_tpm/**`、`projects/qspi_flash_test/**`、
  `projects/spif_test/**`、`common/`、`regression/`、`scripts/`、任何既有文档（`KNOWN_ISSUES.md`
  / `OPEN_ITEMS.md` / `COVERAGE.md` / `DEFECT_ANALYSIS_2026-09-17.md` 均只读）。
- **未派**任何子 agent。
- **未启动**任何烧录（`DebugServerConsole.exe` 因无 CKLink 启动失败，未产生副作用进程）。
