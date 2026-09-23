# SPI filter (PFR/TMP SPI monitor) 测试覆盖矩阵

- 驱动: `modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.c`
- 接口: `modules/lib/pfr_tpm/drivers/pfr/spi/spi_filter.h`；寄存器定义 `reg_spi_filter.h`
- 实例: `spif1..4` @ `0x40070000 / 0x40072000 / 0x40074000 / 0x40076000`（overlay 全 `okay`）
  - 配对 SPI master（dtsi `spi` 属性）：spif1/spif2 → `spi3`；spif3/spif4 → `spi4`
  - `index` 属性 = 0/1/2/3（`spif_get_ctrl_idx()`）
- 外部路径: **有** —— master 模式直驱总线上挂的 NOR flash
  - `外部: SPIF3 在 master 模式下经 spi4 的 CS0(GPIOI 0) 驱动板上 flash`（需要该 flash 在位）
  - `外部: SPIF1 在 master 模式下经 spi3 的 CS0(GPIOI 8) 驱动板上 flash`（`master_read_with_addr`，参考资产 `spi_monitor_filter` 的 master 路径）
  - 其余寄存器级用例零外部条件
- 关键常量: `SPIF_FIXED_CMD_TABLE_NUM=30`、`SPIF_GENERAL_CMD_TABLE_NUM=40`、
  `SPIF_CMD_TABLE_NUM=70`（`spi_filter.h:17-19`）；`SPIF_ADDR_PRIV_REG_NUN=512`、
  `SPIF_ADDR_WHITELIST_SIZE=MB(256)`（`spi_filter.h:16,23`）

## ⚠️ 已知基线：`cmd_idx_accessors` 长期 FAIL（**保持现状是裁定，不是待修缺陷**）

> **一句话**：本工程 `case_default` 长期停在 **17/18**，唯一 FAIL 即 `cmd_idx_accessors`。
> 该断言在 **索引 5 槽位**上**不可满足** —— 属 **硬件/表结构事实**，**不是驱动缺陷**。
> **用户 2026-09-23 裁定：保持现状、登记为已知基线**（与 `spi_test::link_probe` 同性质）。
> **判据未改动是有意的**（不放宽断言是本项目纪律）—— 后来者**不要再去"修"它**。

| 项 | 内容 |
|---|---|
| 失败断言 | `cpu1_spif_test/src/spif_test.c:341` `zassert_equal(dummy, 9, "set/get dummy by idx roundtrip")`<br>（基线行号 `:327`；`:341` 只是新增诊断 `ZT_INFO` 造成的行移，**判据文本一字未改**） |
| 测什么 | 命令表**按索引访问**：命令行 / 使能位 / **DUMMY 周期数**都应正确往返（DUMMY 为 5 bit，`0xff` 应被截断为 `0x1f`） |
| 实测现象（上板） | 索引 5 槽位：**命令行 ✓、使能位 ✓、DUMMY 周期数 ✗**<br>`INFO cmd_idx_accessors: slot5_raw=0x77,cmd=0x77,raw_dummy=0,get_dummy=0`（`reports/2026-09-23-1/spif_test_case_default/serial_TGT.log:38`）<br>关键：**原始寄存器**的 `[12:8]` 位段就是 `0`（`raw_dummy=0`），不只是 API 回读不对 |
| 确定性 | `reports/2026-09-21-1` / `2026-09-22-1` / `2026-09-23-1` **三轮全 FAIL**，非 flaky |
| 判定依据 | `raw_dummy=0` ⇒ 命中"**该槽的 DUMMY 字段不可写/未实现**"（原始寄存器位段即 0），而非"位写进去了、只是回读通路不对" |

### 已排除（**不是**这些原因）

- **驱动掩码/截断缺陷**：`spif_set_dummy_by_idx()` / `spif_get_dummy_by_idx()`（`spi_filter.c:1015-1079`）是标准**读-改-写**、字段读写对称，无掩码缺陷。
- **索引或出参非法**：`idx=5` 在界内（`SPIF_FIXED_CMD_TABLE_NUM=30`）、出参非 NULL ⇒ 上游缺陷 #4 / #5 **不适用**。
- **位域布局（LSB-first）错**：`spif_cmd_t` 布局 `CMD[7:0] / DUMMY_CYCLE[12:8] / EN[31]` 正确；同文件 `spif_cfg_t` 的位序已被 `cold_reset_state` 实测按住（`cfg=0x40` = `IP_LOCK` bit6）。
- **同槽其它位也坏**：同用例 `:321-323` 的 **CMD** 往返（`0x77`）PASS；`cmd_table_add_remove` 固定表段的 **EN** 回读 PASS ⇒ 该槽 **CMD/EN 均正常，唯独 DUMMY 位段**。

### 为什么**不**改用例 / **不**删断言（重要，防后续误改）

- **保持 FAIL 是 2026-09-23 的裁定**（`OPEN_ITEMS.md` O-31），**不是疏漏、不是"待修"、不是"遗留缺陷"**。
- 放宽（改 `ztest_test_skip()` / 删断言 / 换槽位）会**掩盖这个真实读数**；本项目纪律 = **判定前不得放宽判据**。
- ⚠️ **最易踩的误判**（本项曾被误判过一轮）：它与同工程的 `passthrough_mode_switch` **形似而实反** —— 后者是**真驱动缺陷**、已修（`spi_filter.c` 增加 `gpio_ana_enabled` 比对，上板 `FAIL→PASS`，提交 `334ab30`）；**本项不是**驱动缺陷，**照那个思路去改驱动会白费**。

### 旁注（⚠ **推测，未核实**，仅作追查线索）

驱动有「固定命令占用表头前 `SPIF_FIXED_CMD_TABLE_NUM`(=30) 个槽位」的机制，**推测**索引 5 正是一个**固定命令槽**（SPIF 语义上普通 `READ` 无 dummy 相）。**此条未核实**，不作结论、不据此改动任何东西。

## 用例 → 实例分配（保证用例间零残留）

| 实例 | 用例 | 理由 |
|---|---|---|
| spif1 (idx 0) | cold_reset_state / filter_enable_bit / reg_lock_unlock / sck_threshold_config / addr_whitelist_fill / addr_privilege_guard / addr_privilege_valid_region / **master_read_with_addr** | 寄存器级；**从不设置 `flash_size`**，使 `addr_privilege_guard` 的 `flash_size==0` 前置恒成立。`master_read_with_addr` 用 spif1 作 master（配对 spi3） |
| spif2 (idx 1) | cmd_idx_accessors / cmd_table_add_remove / cmd_table_full_boundary / **passthrough_mode_switch** | 命令表类，互不干扰且自清理；passthrough 只动 pinctrl，收尾切回 filter |
| spif3 (idx 2) | pinctrl_state_switch / master_read_jedec_id | 管脚/总线类（master 模式） |
| spif4 (idx 3) | addr_mode_config / flash_size_boundary | 改变 `dev_data->flash_size`（不可逆：驱动无 set-0 接口），隔离到独立实例 |
| （只读遍历） | bus_pairing | 不改变任何实例状态，只比对 `spif_spi_dev()`/`spif_spi_cs()` 与 DT 声明 |

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| device_ready | `SPI_FILTER_INIT`（`spi_filter.c:1799-1901`）：`.spi = DEVICE_DT_GET(DT_INST_PHANDLE(inst, spi))`、`.cs = GPIO_DT_SPEC_INST_GET(inst, cs_gpios)`、`.index = DT_INST_PROP(inst, index)`；`spif_get_ctrl_idx()`（`:195`）、`spif_spi_dev()`（`:1407`）、`spif_spi_cs()`（`:1416`） | 功能通路 | 4 实例 ready；`ctrl_idx == 0..3`；配对 spi 设备非 NULL 且 ready；cs gpio 端口非 NULL 且 ready | DT 绑定契约（`linkedsemi,spi-filter.yaml` 的 `spi`/`cs-gpios`/`index`） | ⏳ |
| cold_reset_state | `linkedsemi_spi_filter_cold_reset()`（`:1629-1730`）：clock off → reset toggle → clock on → pinctrl → `spif_reg_unlock` → `spif_memset_addr_whitelist(1)` → 固定表 30 槽写入（`:1676-1682`）→ `SPIF_TARGET_ADDR=0xffffffff`、`SPIF_BCMD_RANGE=7`（`:1683-1684`）→ `SPIF_CFG` 初值（`:1685-1693`）→ `spif_clk_check_config(0,2,BIT(12)-1,false)`（`:1701`）→ `SPIF_INTR_MASK`（`:1702-1706`） | 复位/掉电 | `CFG.EN==0`、`OPERATION_MODE==0`、`ADDR_3B_4B_SEL==0`、`TARGET_ADDR_MODE_SEL==0`、`IP_LOCK==1`、`DMA_EN==0`；`SPIF_TARGET_ADDR==0xffffffff`；`SPIF_BCMD_RANGE==0x7`；`INTR_MASK.ERROR_OVERFLOW/ERROR==1`、`TARGET_ADDR==0`；固定表 30 槽 EN 全为 0；槽 0 的 CMD == DT `cmd-page-program` | 驱动源码常量 + DT 属性 | ⏳ |
| filter_enable_bit | `spif_enable()`（`:1617-1628`）与 `spif_filter_enable()`（`:1171-1185`）只改 `SPIF_CFG.EN` | 功能通路 | true → `CFG.EN==1`，false → `CFG.EN==0`（两种 API 各验一次） | 驱动源码（同一寄存器位） | ⏳ |
| reg_lock_unlock | `spif_reg_unlock()`（`:1187-1195`）写 `IP_LOCK=1`；`spif_reg_lock()`（`:1197-1205`）写 `IP_LOCK=0` | 功能通路 | `reg_unlock → IP_LOCK==1`；`reg_lock → IP_LOCK==0` | 驱动源码（**API 名与写入值反相**，见「上游缺陷观察」#6） | ⏳ |
| sck_threshold_config | `spif_clk_check_config()`（`:1207-1237`）：写 `SPIF_SCK_SET.SCK_DIV`（3 bit）、`SPIF_SCK_FQC_HI`、`SPIF_SCK_FQC_LO`、`SPIF_INTR_MASK.SCK_CHECK` | 功能通路 | `SCK_DIV==5`；`SCK_FQC_HI==2`；`SCK_FQC_LO==100`；`SCK_CHECK==1`，再以 `enable_intr=false` 置 0 | 寄存器字段位宽（`reg_spi_filter.h:170-206`） | ⏳ |
| addr_mode_config | `spif_4byte_mode_config()`（`:1372`）/ `spif_3byte_mode_config()`（`:1360`）写 `SPIF_CFG.ADDR_3B_4B_SEL` | 功能通路 | 4B 模式 → `ADDR_3B_4B_SEL==1`；3B 模式 → `==0` | 驱动源码。**注**：不用 `spif_addr_mode_peek()` —— 它返回的是 `ADDR_3B_4B_FLAG`（状态位，另一 bit），不是 SEL | ⏳ |
| cmd_idx_accessors | `spif_set_cmd_by_idx()`（`:960`）/ `spif_get_cmd_by_idx()`（`:1041`）/ `spif_set_dummy_by_idx()`（`:1011`）/ `spif_get_dummy_by_idx()`（`:1059`）/ `spif_replace_fixed_cmd()`（`:979`）/ `spif_replace_fixed_dummy()`（`:1030`） | 功能通路 | CMD 与 DUMMY_CYCLE 按 idx 读写往返一致；DUMMY_CYCLE 为 5 bit → `0xff` 回读 `0x1f` | 寄存器字段位宽（`spif_cmd_t`：CMD[7:0]、DUMMY_CYCLE[12:8]、EN[31]） | ⏳ |
| cmd_table_add_remove | `spif_add_general_cmd()`（`:813-850`）、`spif_get_cmd_slot()`（`:356`）、`spif_get_general_cmd_slot()`（`:339`）、`spif_remove_general_cmd()`（`:1077-1109`，未命中 → `-EINVAL`）；**＋固定表分支**：`spif_add_cmd()`（`:855-900`，第二循环 `:879-889`）、`spif_set_cmd_by_idx()`（`:964-981`，只改 HW、不动 SW 镜像）、`spif_remove_cmd()`（`:1115-1154`） | 功能通路+异常参数 | add 后槽位存在；remove 后 `get_general_cmd_slot()` 返回 `-ENOSR`；重复 remove → `-EINVAL`；**固定表段**（制造 HW/SW 不一致后）：`spif_get_cmd_slot(0x03)` 先返回 `-ENOSR`，`spif_add_cmd(0x03)` 后目标槽 `CMD==0x03` 且 **`EN==1`** | API 契约 + 驱动源码。固定表段是**缺陷 #1 修复的回归护栏**（修复前 EN 保持 0 ⇒ 该段必 FAIL），**只断言 `CMD`/`EN`**（union 未清零，缺陷 #20） | ⏳ |
| cmd_table_full_boundary | `spif_add_general_cmd()` 耗尽 general 表 → `-ENOSR`（`:838-841`） | 边界长度 | 前 40 个 add 全 0；**第 41 个 → `-ENOSR`**（容量 = `SPIF_GENERAL_CMD_TABLE_NUM=40`）；用例末尾清空 40 槽 | 驱动源码常量（±1 边界） | ⏳ |
| addr_whitelist_fill | `spif_memset_addr_whitelist()`（`:798-811`）→ `spif_memset_read/write_addr_whitelist()`（`:756-796`）：num=0 → 全 0；num=1 → 全 `0xffffffff`；非法 num → 仅 `DEV_ERR`、**不改寄存器**（`:802-806`）。回读经**间接 SRAM 读端口**（`:471-502` 时序：`SPIF_READ_SRAM_ADDR`→`SPIF_READ_ADDR_REQ`→轮询→`SPIF_READ_SRAM_DATA`） | 功能通路+异常参数 | 读/写两个窗口（各 512 寄存器）首/尾寄存器与 num 对应；num=2 后寄存器保持不变 | 驱动源码 + `SPIF_ADDR_PRIV_REG_NUN=512`；**回读路径**：该表对 CPU **直读恒 0**（写窗只写不读，驱动自身也从不直读：`CONFIG_SPI_FILTER_ADDR_WHITELIST_BUF=y` 时读 SW 镜像 `:454-469`）⇒ 用例改用间接端口复刻，见 TRIAGE_QSPI_SPIF_2026-09-18.md §2 S2 | ⏳ |
| addr_privilege_guard | `spif_address_privilege_config()`（`:738-754`）在 `flash_size==0` 时透传 `_op()` 返回值；`_op` 守卫：`addr >= MB(256) \|\| len == 0` → `-EINVAL`（`:653-656`） | 异常参数+边界长度 | `addr == MB(256)` → `-EINVAL`；`addr > MB(256)` → `-EINVAL`；`len == 0` → `-EINVAL`；`addr == MB(256) - KB(16)`（上界内）→ `0` | 驱动源码（±1 边界）。**前置**：spif1 的 `flash_size` 必须为 0（默认值；本工程从不在 spif1 上调 `spif_flash_size_set`）。⚠️ 守卫分支命中驱动缺陷 #1（未配对 release），本项安全性论证见下 | ⏳ |
| addr_privilege_valid_region | `spif_address_privilege_config_op()`（`:636-736`）镜像路径（`CONFIG_SPI_FILTER_ADDR_WHITELIST_BUF=y` → `spif_peek_rw_area()`（`:455-469`））；`reg_off = addr/KB(512)`、`bit_off = (addr%KB(512))/KB(16)`（`:678-679`）与 `total_bit_num` 循环（`:690-732`）。回读经**间接 SRAM 读端口**（同 `addr_whitelist_fill`） | 功能通路 | 前置整片清 0 后，`MB(1)..+KB(16)` 的 READ 窗口 `reg_off=2` 位 0 置 1（寄存器 == `0x1`）；同区间 DISABLE → 清 0；WRITE 窗口独立寄存器 == `0x1` | 驱动源码 + 地址/长度换算公式；**回读路径**同 `addr_whitelist_fill`（直读恒 0 ⇒ 改走间接端口），见 TRIAGE_QSPI_SPIF_2026-09-18.md §2 S3 | ⏳ |
| flash_size_boundary | `spif_flash_size_set()`（`:181-193`）守卫：`size == 0 \|\| !IS_ALIGNED(MB(256), size)` → `-EINVAL`；`spif_flash_size_get()`（`:174-179`） | 边界长度 | `0` → `-EINVAL`；`3`（不能整除）→ `-EINVAL`；`MB(256)+1` → `-EINVAL`；`MB(256)` → `0` 且回读一致；`KB(64)` → `0` 且回读一致 | 驱动源码（`IS_ALIGNED(a,b)` ≡ `a % b == 0`） | ⏳ |
| pinctrl_state_switch | `spif_switch_to_filter()`（`:1478-1487`，`pinctrl_apply_state(DEFAULT)` + `spif_enable(true)`）、`spif_switch_to_master()`（`:1540-1544` → `spif_switch_to_master_handle()` `:1505-1538`，相邻实例 master 态检查 → `-EBUSY`）、`spif_pinctrl_filter/master_mode_check()`（`:1456,1468`） | 功能通路 | 先确保相邻 spif4 在 filter 态；`switch_to_filter → filter_mode_check()==true`；`switch_to_master → 0 且 master_mode_check()==true`；切回后 `CFG.EN==1` | 驱动源码（pinctrl 状态表比对实管脚 func） | ⏳ |
| master_read_jedec_id | `spif_switch_to_master()`（`:1540`）+ `spif_spi_dev()`（`:1407`）/`spif_spi_cs()`（`:1416`）取配对 SPI 与 CS，`spi_transceive()` 发 `0x9F` | 功能通路 | SPI 事务返回 0；**实测 3 字节 JEDEC ID 经 `ZT_INFO` 上报**（不预设厂商值，基准待 P6 实测） | 外部器件（NOR flash JEDEC 读 ID 契约） | ⏳ |
| passthrough_mode_switch | `spif_passthrough_analog_mux_enable()`（`:1552-1561`：恒 `pinctrl_apply_state(PASSTHROUGH)`，**忽略 `enable`**）+ `spif_pinctrl_passthrough_mode_check()`（`:1468` → `spif_pinctrl_mode_check()` `:1424-1450`，比对 pinctrl 状态表与实管脚 func）+ `spif_switch_to_filter()`（`:1478-1487`：`pinctrl_apply_state(DEFAULT)` + `spif_enable(true)`） | 功能通路 | cold_reset 后 `filter_mode_check()==true` 且 `passthrough_mode_check()==false`；使能后 `passthrough_mode_check()==true` 且 `filter_mode_check()==false`（两态互斥）；切回 filter 后 `filter_mode_check()==true`、`passthrough_mode_check()==false` | 驱动源码（pinctrl 三态状态表 default/passthrough/master） | ⏳ |
| bus_pairing | DT 静态契约：`SPI_FILTER_INIT` 的 `.spi = DEVICE_DT_GET(DT_INST_PHANDLE(inst, spi))`、`.cs = GPIO_DT_SPEC_INST_GET(inst, cs_gpios)`；访问器 `spif_spi_dev()`（`:1407`）/ `spif_spi_cs()`（`:1416`） | 功能通路 | 4 实例：`spif_spi_dev(dev)` 指针 == `DT_PHANDLE(spifN, spi)` 解析出的设备；`spif_spi_cs(dev)` 的 port+pin == DT `cs-gpios`（spif1→spi3/GPIOI8、spif2→spi3/GPIOG2、spif3→spi4/GPIOI0、spif4→spi4/GPIOQ0） | DT 声明（模块 dtsi `&spifN { spi=…; cs-gpios=…; }`）。**零外部条件** | ⏳ |
| master_read_with_addr | `spif_switch_to_master()`（`:1540`）+ `spif_spi_dev()`/`spif_spi_cs()` 取配对 spi3 与 CS，`spi_transceive()` 发 `0x03 + 24bit 地址`（复刻参考资产 `spi_monitor_send_read_addr()` 的命令形态） | 功能通路 | SPI 事务返回 0；读回字节经 `ZT_INFO` 上报（不预设器件值） | 外部器件（spi3 总线上 NOR flash 读契约）。`spif1` 的相邻实例为 `spif2`（驱动内 `spif_spi_check_dev_name_arr`）。⚠️ **待实测确认器件存在**：参考资产有该路径但**无实测输出佐证**（其 `README.rst` 是 hello_world 模板）；**P6 登记时带 `#` 屏蔽，由父会话处理**（裁定 2026-09-17） | ⏳ |

> 状态标记：`⏳` = **P4 新增，仅编译通过，待 P6 首次实测**。

## 跳过项

| 跳过的 case | 判据类型 | 原因 |
|---|---|---|
| `spif_add_cmd` / `spif_add_cmd_with_dummy` 的"命令已在固定表"分支 | **可达性受限（2026-09-18 起已部分转为可断言）** | **触发前置**：仅当 **HW 命令表与 SW 镜像不一致**时才命中 —— 需同时 (A) `spif_get_cmd_slot(dev, cmd, 0) == -ENOSR`（HW 70 槽表内无该 CMD）**且** (B) `dev_data->fixed_cmd_tab[off] == cmd`（SW 镜像认为它是固定命令）。`linkedsemi_spi_filter_cold_reset()`（`:1680-1687`）把镜像逐槽写进 HW ⇒ **先调过 cold_reset 就 (A) 为假**；本仓三个 sample（`spi_monitor_bit_map` / `spi_monitor_target_addr_Interrupt` / `spi_monito_ip_locck_and_cmd_blacklist`）都先 cold_reset ⇒ **不触发**。可达路径是"**首次 cold_reset 之前**调 `spif_add_cmd*`"（如 `spim_add_allow_command(dev, 0x03)`、shell `spif add`）。命中时用过期 `idx`（此时为 `-ENOSR = -63`）算写地址（`spi_filter.c:884` / `:938`）：`sys_write32(..., table_base + idx*4)` ⇒ 写 **`base + SPIF_CMD_BASE - 252` = `base - 0xD0`**（spif1 = `0x4006FF30`，仍落在 RTC 块 `0x4006fc00..0x4006ffff`）—— 表外野写；且**目标槽 `EN` 保持 0**（"使能固定槽"的功能完全失效）。**✅ 2026-09-18 已修**（两处写址改固定表循环变量 `off`）。<br>**现可断言（本轮已落地）**：`cmd_table_add_remove` 末尾新增"固定表分支"段 —— 用 `spif_set_cmd_by_idx()`（只改 HW 的 CMD、**不动** SW 镜像 `fixed_cmd_tab[]`）主动制造 (A)，再调 `spif_add_cmd(0x03)`，断言**目标槽 `CMD` 被回写且 `EN==1`**。该段在修复前**必 FAIL**（EN 会保持 0）⇒ 是**真断言 / 回归护栏**，不是"缺陷规避"。⚠ 只断言 `CMD` 与 `EN` 两个字段：该路径的 `spif_cmd_t` union 未清零（缺陷 **#20**），`DUMMY_CYCLE`/`reserved` 是栈垃圾，比对整字会假 FAIL |
| DMA 日志通路（`spif_dma_start` / `spif_dma_work` / `spif_dma_callback`） | **判定不可靠** | `spif_dma_work()` 用**未初始化**的 `struct dma_status` 且忽略 `dma_get_status()` 返回值（`spi_filter.c:1576-1578`），终止条件依赖 DMA 计数变化（`while(1)` + `last_xfer_len == xfer_len`）；`spif_dma_start()` 先置 `DMA_EN=1` 再做可能 `-EINVAL` 的 DMA 设备检查（`:1305` vs `:1330-1333`）。判据不可确定 + 属 `OPEN_ITEMS.md` O-4「共享 DMA 改动爆炸半径」盲区 |
| 中断/错误回调触发（`linkedsemi_spi_filter_isr`、`spif_callback_t` 实触发） | **缺硬件条件** | 需要上游 SPI master（滤波 in 侧）制造非法命令/越界地址/SCK 异常事件；本工程只做 master 模式直驱 flash（out 侧），**in 侧 master 未接线**。回调注册本身由 `device_ready` 与 init 路径间接覆盖 |
| `spif_operation_mode_config()`（滤波 vs 监视模式行为） | **行为层判据缺失** | 该 API 只写 `SPIF_CFG.OPERATION_MODE`（`:1395`）；"滤波 vs 监视" 的行为差异必须在真实总线流量下观测（非法命令是否被放行），同「缺硬件条件」；寄存器位本身已由 `cold_reset_state` 覆盖默认值 |
| `spif_passthrough_analog_mux_enable()` 的 `enable` 参数 | **驱动未实现** | 函数体忽略 `enable`，恒应用 `PINCTRL_STATE_PASSTHROUGH`（`:1552-1561`），无对应分支可测（见「上游缺陷观察」#7） |
| `spif_dump_rw_addr_privilege_table()` 的错误传播 | **缺陷规避（不可断言）** | 该函数丢弃 `spif_protect_area_parser()` 返回值后检查**恒为 0 的 `ret`**（`:597-598`），`-EIO` 永不外传；且 `while (num_protect_blk != 0)` + `start = res.end`（`:606-609`）在解析出零宽块时无法推进。依赖该函数的错误语义不成立 ⇒ 只作观察记录 |
| `spif_address_privilege_config()` 的 `flash_size != 0` 镜像循环 | **驱动未实现完整语义** | 该分支**恒返回 0**（丢弃每次 `_op()` 的返回值，`:749-752`），且 `off` 从 `addr % flash_size` 起算（非 `addr`），`len` 不按 stride 裁剪 ⇒ 无可靠的"成功/失败"判据可写。故 `addr_privilege_guard` 只在 `flash_size==0` 的透传路径上取判据 |

### 参考资产导致的跳过项（P4.7，2026-09-17）

> 参考资产 `spi_monitor_filter`（出处见下方「参考资产出处」）的核心是"开滤波 + 主控发波 → 读监视日志"。
> 其中**依赖监视日志 / 地址镜像 / `spif_add_cmd` 的部分不可断言**，按判据类型跳过；可确定性断言的部分已落入上方
> 新增的 `passthrough_mode_switch` / `bus_pairing` / `master_read_with_addr` 三项。

| 跳过的 case | 判据类型 | 原因 |
|---|---|---|
| 参考资产的"监视日志"观测（`spif_dump_cmd_table()` / `spif_dump_cmd_bitmap_log()` / `linkedsemi_spi_filter_isr` 事件） | **行为层判据缺失 + 缺硬件条件** | 资产在 `spif3`(filter) 上开滤波、由 `spif1`(master) 发波，但其**唯一可判定输出是日志**；本工程无监视基线（`in` 侧 master 是否与 `filter` 同总线未确认），ISR/回调实触发更依赖上游主控制造非法事件（同上方既有跳过项）。`spif_dump_cmd_table()` 为 `void` 纯打印，无判据；`spif_dump_rw_addr_privilege_table()` 有缺陷 #2/#13，调用可能不推进 |
| 参考资产的 `spif_filter_enable` + `spif_flash_size_set(MB(32))` + `spif_address_privilege_config(0x4000, 16K)` 组合 | **驱动未实现完整语义** | `flash_size != 0` 时走镜像循环恒返回 0（缺陷 #3）—— 无可靠判据（同上方既有跳过项）。`flash_size` 边界本身已由 `flash_size_boundary`（MB(256)/KB(64)）覆盖，MB(32) 不新增分支 |
| 参考资产的 `spif_add_cmd(spifilter, 0x03)` | **可达性受限（2026-09-18 起可断言）** | 与上方既有跳过项同源。**该缺陷（#1）已修**（两处写址改 `off`），故参考资产这一写法本身现在**不再是"照搬即表外野写"**；但其自然形态（先 `cold_reset` 再 `spif_add_cmd(0x03)`）走的是**第一个循环**（HW 表此时已含 0x03 ⇒ 命中并置 EN），**覆盖不到**修复后的**第二个循环**。本轮改为在 `cmd_table_add_remove` 内**主动制造 HW/SW 不一致**来覆盖第二循环并断言 `EN==1`（见上方首行）；参考资产的写法仍不照搬 —— 它不制造不一致，覆盖的是另一条路径 |
| 参考资产的 `spif_dma_start()` / DMA 日志通路 | **判定不可靠** | 同上方既有跳过项（`spif_dma_work` 用未初始化 `struct dma_status`）。资产中该段被 `CONFIG_SPI_FILTER_DMA_LOG` 包住、本工程不复刻 |

## 参考资产出处（P4.7，2026-09-17）

> 用户 2026-09-17 提供的参考资产目录（**只读，未拷贝进本工程**）：
> `C:\Users\26502\xwechat_files\wxid_es6ucfm7wkri22_71d4\msg\file\2026-09\send shi(1)\`
>
> | 资产 | 与本工程的关系 | 处置 |
> |---|---|---|
> | `spi_monitor_filter\`（**模块内不存在**，`modules/lib/pfr_tpm/samples/` 无同名目录 —— 用户新给） | `src/main.c`：`spif3`(filter) + `spif1`(master)，`cold_reset → flash_size MB(32) → filter_enable → passthrough → switch_to_master(spif1) → 裸 spi_transceive(0x9f)/(0x03+addr) → 读 flash3_0 JEDEC`。与模块 `spi_monitor_*` 家族（11 个）同骨架，`LOG_MODULE` 名 `spi_monitor_01_003_005` 对应命令 0x01/0x03/0x05 白名单类 | **按 810 规范重写为 ztest**：断言可确定的部分（`passthrough_mode_switch` / `bus_pairing` / `master_read_with_addr`）；日志/镜像/`spif_add_cmd` 部分跳过（见上） |
> | `spi_master_communication\`（模块内**有**同名 sample，内容有差异） | 与模块版的差异见下方「模块版 vs 用户版差异」 | 差异**不影响**本工程既有用例的对外契约（`spif3` master → `flash4_0` 的配对关系两版一致）；已记录，**未修改既有用例**（铁律：先报告） |
>
> 资产目录内含 `spi_monitor_filter/main.c`（与 `src/main.c` 内容不同，疑似早期草稿）与 `sample.yaml`/`README.rst`
> （均为 hello_world 模板），本工程**不采纳**，仅以 `src/main.c` 的行为语义为准。

### 模块版 vs 用户版差异（`samples/spi_master_communication/`，P4.7 比对）

比对方式：`diff -w --strip-trailing-cr`（先剥离 CRLF/LF 差异）。结论：

| 文件 | 差异 |
|---|---|
| `CMakeLists.txt` / `README.rst` / `prj.conf` / `sample.yaml` / `boards/lsqsh_evb_cpu2.overlay` | **仅行尾（CRLF↔LF）差异**，内容逐字节等价 |
| `src/main.c` | ① `READ_LLI_DIS`：模块 `1` → 用户 `0`（改变 32KB 大块读的 LLI 路径，属"是否走 LLI 链表"的开关）；② `LOG_MODULE_REGISTER` 级别：模块 `LOG_LEVEL_INF` → 用户 `4`(DBG)；③ 用户把主体 bulk 测试用 `#if 1 … #else k_thread_create(flash_read_loop_thread, …) #endif` 包住 —— 模块版把压力线程那行**注释掉**，用户版用条件编译二选一 |
| `boards/lsqsh_evb_cpu1.overlay` | **仅一处**：`flash4_0: flash4@0` 的 `spi-max-buswidth = <4>;` 被注释掉（`//spi-max-buswidth = <4>;`）—— 即该节点不再声明四线总线宽度。**alias 形态两版一致**（`spifilter=&spif3; spifiltermaster=&spif3; spiflash=&flash4_0;`），`spif1..4` 的 QPI 命令属性（`cmd-page-program-quad-addr-quad-data=0xc2` / `enter=0x38` / `exit=0xff`）两版一致 |

**对既有用例的影响评估（结论：无）**：本工程 `spif_test` **不声明 `flash4_0` 等 `jedec,spi-nor` 子节点**（`app.overlay` 只使能 `spi3`/`spi4` 控制器并设 `cs-gpios`），master 用例走的是**裸 `spi_transceive`**，不经 `spi_nor.c` 栈 ⇒ `spi-max-buswidth` 的注释与否不影响本工程。`spifiltermaster=&spif3` + `spiflash=&flash4_0` 的配对与既有 `master_read_jedec_id` 一致（`spif3.spi = <&spi4>`，CS = GPIOI0）。

## 上游缺陷观察（#1 已于 2026-09-18 修复；其余仍只读记录）

> 依据 `qsh-driver-review` 的驱动审核维度逐条核对。**#1（表外野写）已修复**（两处写址
> `table_base + idx * 4` → `table_base + off * 4`）；其余条目本轮仍只记录、未修改，修复需上游裁决。
> **#1 已加回归护栏**：`cmd_table_add_remove` 末尾的"固定表分支"段主动制造 HW/SW 表
> 不一致后调 `spif_add_cmd()`，断言目标槽 `EN==1`（见上方用例表 + 「跳过项」首行）。

| # | 位置 | 问题 | 后果 |
|---|---|---|---|
| 1 | `spi_filter.c:879-887`、`:932-941` | `spif_add_cmd` / `spif_add_cmd_with_dummy` 在固定表命中分支用**过期 `idx`**（此时为 `-ENOSR = -63`）作写地址：`sys_write32(..., table_base + idx * 4)` | 写 **`base + SPIF_CMD_BASE - 252` = `base - 0xD0`**（spif1 = `0x4006FF30`，仍落在 RTC 块 `0x4006fc00..0x4006ffff`）—— 表外野写；且"使能固定槽"的意图完全失效（应写 `off * 4`）。**触发前置**：HW 命令表与 SW 镜像不一致（需 (A) `-ENOSR` 且 (B) 镜像命中；先 `cold_reset` 则 (A) 为假）—— 详见「跳过项」首行。**✅ 2026-09-18 已修**：两处写址改用固定表循环变量 `off`（`table_base + off * 4`，`off ∈ [0,30)`） |
| 2 | `spi_filter.c:597-598` | `spif_dump_rw_addr_privilege_table` 丢弃解析返回值后判断恒 0 的 `ret` | `spif_peek_rw_area()` 的 `-EIO` 永不外传，"cs line busy" 被静默吞掉 |
| 3 | `spi_filter.c:749-752` | `spif_address_privilege_config` 镜像循环忽略内层返回值、恒返回 0；`off` 从 `addr % flash_size` 起算 | 全部内层失败也报成功；`addr >= flash_size` 时保护的不是 `addr` 起始的区域 |
| 4 | `spi_filter.c:1052`、`:1070` | `spif_get_cmd_by_idx` / `spif_get_dummy_by_idx` 对出参 `*cmd` / `*dummy_cycle` **无 NULL 检查**（仅 `__ASSERT_NO_MSG(dev)`） | 传 NULL 即野写地址 0 |
| 5 | `spi_filter.c:970`、`:1021`、`:1162` | `spif_set_cmd_by_idx` / `set_dummy_by_idx` / `remove_cmd_by_idx` 的 `uint8_t idx` 无上界检查；`spif_replace_fixed_cmd/dummy` 只用 `__ASSERT_NO_MSG` 守卫 | `idx >= 70` 时 `table_base + idx*4` 越界写；release 构建下 assert 不生效 |
| 6 | `spi_filter.c:1187-1205` | `spif_reg_unlock()` 写 `IP_LOCK=1`、`spif_reg_lock()` 写 `IP_LOCK=0` —— 与命名反相 | API 误用风险（`spif_reg_lock` 实际是解锁） |
| 7 | `spi_filter.c:1552-1561` | `spif_passthrough_analog_mux_enable(dev, enable)` 忽略 `enable`，恒应用 passthrough 状态 | 接口语义虚假 |
| 8 | `spi_filter.c:1428-1441` | `spif_pinctrl_mode_check` 中 `pinctrl_lookup_state()` 失败后 `__ASSERT_NO_MSG(0)` 在 release 下为空操作，随后解引用**未初始化**的 `state` | 查找失败 → 空指针解引用 |
| 9 | `spi_filter.c:1305` vs `:1330-1333` | `spif_dma_start` 先置 `SPIF_CFG.DMA_EN=1`，再做可能 `return -EINVAL` 的 DMA 设备检查 | 失败返回时残留 `DMA_EN=1` 而 DMA 未启动；`dma_config`/`dma_start` 返回值亦被丢弃 |
| 10 | `spi_filter.c:1576-1585` | `spif_dma_work` 用未初始化的 `struct dma_status`、忽略 `dma_get_status()` 返回；`while(1)` 仅靠计数变化退出 | 查询失败时 `xfer_len` 为垃圾值并驱动打印循环 |
| 11 | `spi_filter.c:152-155` vs `:689,692,723,764,769,785,790` | 镜像数组 `read_addr_whitelist` / `write_addr_whitelist` 只在 `#if CONFIG_SPI_FILTER_ADDR_WHITELIST_BUF` 下声明，却在守卫外被解引用 | `CONFIG_SPI_FILTER_ADDR_WHITELIST_BUF=n` 时**编译失败**（Kconfig 允许该取值） |
| 12 | `spi_filter.h:123` | `spif_set_pinctrl_state()` **只有声明、全树无定义** | 任何调用方链接失败 |
| 13 | `spi_filter.c:550-551` | `spif_protect_area_parser` 的"跑到末尾"分支给 `end_bit_off = 32`，而扫描上界是 31（`:528`） | 越界位号（off-by-one） |

### 序列化（`sem_spif`）与返回值专项审核补充（qsh-driver-review，2026-09-17）

审核方式：对 15 个 `acquire_spif_device()` 调用点逐一追到函数出口，并对所有硬件/总线类调用做返回值检查。

| # | 位置 | 问题 | 后果 |
|---|---|---|---|
| 14 | `spi_filter.c:653-657` → `:732-733` | **未配对的 `release_spif_device()`**：`spif_address_privilege_config_op` 的非法入参守卫（`addr >= MB(256) \|\| len == 0`）在 **`:685` 的 `acquire` 之前**就 `goto end`，而 `end:` 无条件执行 `release_spif_device()` | 对未持有的信号量执行 `k_sem_give()`。**实机影响分级**：`sem_spif` 以 `k_sem_init(...,1,1)`（limit=1，`:1783`）初始化，且 Zephyr `z_impl_k_sem_give()` 在 `count == limit` 时不增加计数（`zephyr/kernel/sem.c:96-121`）⇒ **无等待者时等价空操作**（本工程 `addr_privilege_guard` 项正处此情形，安全性已在用例注释中论证）。**潜在危害**：若此时另一线程正阻塞在该 `k_sem_take(K_FOREVER)` 上，多余的 give 会把它唤醒并"免费"放行，使两个执行流同时进入寄存器临界区 ⇒ 互斥失效。该函数被 `spif_address_privilege_config()` 间接调用，调用面很广 |
| 15 | `spi_filter.c:597` / `:750` / `:1484` / `:1534` / `:1558` / `:1644` / `:1666` / `:1335` / `:1336` | 硬件/总线类调用返回值被丢弃：`spif_protect_area_parser`（且下一行 `if (ret)` 检查的是恒 0 的 `ret`）、镜像循环内的 `spif_address_privilege_config_op`、两处 `pinctrl_apply_state`（`:1484`/`:1534`）、`spif_passthrough_analog_mux_enable` 内的 `pinctrl_apply_state`、`clock_control_off`/`clock_control_on`（`:1644`/`:1666`）、`dma_config`/`dma_start`（`:1335`/`:1336`） | **"切换成功/时钟已开"被静默谎报**：引脚未切换、时钟未使能、DMA 未配置时上层仍看到 `0`。其中 `clock_control_on` 失败被吞掉与 `cold_reset` 的成功语义直接冲突 |
| 16 | `spi_filter.c:1576-1578` | `spif_dma_work` 中 `struct dma_status stat` **未初始化**且 `dma_get_status()` 返回值被丢弃 | 查询失败时 `stat.pending_length` 为垃圾值 → 假的 `xfer_len` 驱动日志扫描循环（`:1588/1603/1607`）与 `log_info->log_idx`（`:1613`）写入 |
| 17 | `spi_filter.c:248,251,254-263,268,277-280`（SPI filter ISR）、`:1288,1291,1295`（DMA 回调） | **中断上下文内打日志**（`DEV_ERR`/`DEV_DBG`） | `CONFIG_LOG_MODE_IMMEDIATE` 下阻塞；本仓规范 `SPEC_REFERENCE §6.1.2` 明确要求 ISR 内禁 log。本工程用 `CONFIG_LOG_MODE_DEFERRED=y`（缓冲），但上游代码本身违反规范 |
| 18 | `spi_filter.c:1624-1626`、`:1350-1352`、`:1367-1369`、`:1379-1381`、`:1402-1404`、`:1193-1194`、`:1203-1204`、`:1222-1236` | `SPIF_CFG` / 相关寄存器做**读-改-写但不在 `sem_spif` 保护内**（对比 `spif_filter_enable` `:1177-1184` 是加锁的） | 未加锁的 RMW 与加锁路径交错时可丢失更新（丢 `EN`/`IP_LOCK`/模式位） |
| 19 | `spi_filter.c:190`、`:745,749`、`:222`、`:233`、`:1312-1328`、`:987`、`:1038`、`:1683-1684` | `dev_data` 共享字段在 `sem_spif` 外被写：`flash_size`、`cb`、`dma_cb`、`spif_dma_config`、`fixed_cmd_tab[]`/`fixed_cmd_dummy_tab[]`（`spif_replace_fixed_*` 在 `spif_set_cmd_by_idx` **释放信号量之后**才更新 SW 镜像） | `cb`/`dma_cb` 被 ISR 读取却无屏障/原子保护（撕裂/陈旧指针）；硬件命令表与 SW 镜像非原子更新 → 并发 `spif_add_cmd` 读到陈旧镜像；两次并发 `spif_dma_start` 会用半更新的描述符编程 DMA |
| 20 | `spi_filter.c:949-952`（对比同函数固定表分支 `:936`） | `spif_add_cmd_with_dummy()` 的**新槽分配路径从不设置 `DUMMY_CYCLE`** —— 固定表分支 `:936` 有 `spif_cmd.DUMMY_CYCLE = dummy_cycle;`，新槽路径没有 | 走空槽新建时 dummy cycle 写入**栈垃圾**，`_with_dummy` API 语义失效。同型"未初始化 union"亦见 `spif_add_cmd():895-898`、`spif_add_general_cmd():843-846`。**正确对照**：`spif_add_general_cmd()`（`:813-852`）在 `break` 后于 `:837` 重新取 `idx = spif_get_empty_general_cmd_slot(dev)` ⇒ **不中招**。**补充（2026-09-18，断言安全性判定）**：`spif_cmd_t` 是 union，未清零时**只有 `DUMMY_CYCLE`(`[12:8]`)/`reserved`(`[30:13]`) 位是栈垃圾**；`CMD`(`[7:0]`) 与 `EN`(`[31]`) 在所有分支里都是**显式赋值** ⇒ 断言 `CMD`/`EN` 安全，**不得比对整字**（本轮新增的固定表段即按此约束写） |

> 缺陷 #14 的完整推理（含 Zephyr 侧 `k_sem_give` 的 limit 钳制行为）已写进
> `cpu1_spif_test/src/spif_test.c` 的 `addr_privilege_guard` 用例注释，便于 P6 实测时对照。

## 预期数据说明

- 错误码契约：`0` / `-EINVAL` / `-ENOSR`（驱动源码分支，行号见上表）。
- 寄存器回读：按 `reg_spi_filter.h` 的**位域声明**取字段（`spif_cfg_t` / `spif_cmd_t` /
  `spif_sck_set_t` / `spif_intr_t` 等），**不用猜位宽**。
- `sck_threshold_config` 与 `addr_mode_config` 一律**直接回读配置寄存器字段**，
  不用 `spif_clk_check_peek()`（返回状态字段 `SCK_FQC`）/ `spif_addr_mode_peek()`
  （返回状态位 `ADDR_3B_4B_FLAG`）—— 这两者的返回值都不是刚写入的配置值。
- 数值实测经 `ZT_INFO` 上报（§6.5.4），报告"实际"列对比。
- 无外部黄金向量文件（见 `expected/README.md`）。

## P4 生成说明（2026-09-17，只生成 + 编译通过，未烧录）

- 新增 15 项，编译通过（`west build` rc=0，`lsqsh_evb@1os/lsqsh/cpu1`）。
- **ZTEST 符号命名约束（实测踩坑）**：Zephyr 的 `ZTEST(suite, name)` 生成的实际 C 函数名为
  `<suite>_<name>`。因此测试名不得与同名驱动符号冲突 —— 本工程原定的
  `clk_check_config` / `memset_addr_whitelist` 会分别与驱动符号
  `spif_clk_check_config()` / `spif_memset_addr_whitelist()` 撞型，已改名为
  `sck_threshold_config` / `addr_whitelist_fill`。
- 每项自带前置 `linkedsemi_spi_filter_cold_reset()`；实例级可变状态
  （`dev_data->flash_size`，驱动**无 set-0 接口**故不可逆）隔离到独立实例，
  见上方「用例 → 实例分配」表。
- 固定命令表 30 槽容量（`SPIF_FIXED_CMD_TABLE_NUM`）由 `cold_reset_state` 的
  EN 全 0 + 槽 0 的 CMD 值覆盖，不额外造用例。

## P4.7 生成说明（2026-09-17，只生成 + 编译通过，未烧录）

- 纳入外部参考资产 `spi_monitor_filter`（出处见上）后**新增 3 项**：`passthrough_mode_switch`、
  `bus_pairing`、`master_read_with_addr`，共 **18 项**；编译通过
  （`west build -p auto -d …/tests/case_default/build/target -b lsqsh_evb@1os/lsqsh/cpu1`，rc=0，无 warning）。
- 三项的可断言性分级：`bus_pairing` / `passthrough_mode_switch` **零外部条件**；
  `master_read_with_addr` 走 `spif1 → spi3 → flash` 外部路径（与 `master_read_jedec_id` 同源）。
- 参考资产中**不可断言**的部分（监视日志、`flash_size != 0` 地址镜像、`spif_add_cmd`、DMA 日志）
  已登记进「参考资产导致的跳过项」表，**未照搬**。
- ⚠️ **配套 overlay 改动（P4.7 裁定 2026-09-17）**：`app.overlay` 对两个**配对控制器**
  `&spi3` / `&spi4` 显式加 `pinctrl-0 = <>; pinctrl-names = "default";`（只改本工程 overlay，
  不碰板级/模块文件）。**依据**：SPIF 切 master 模式走的是 **SPIF 自己的 `pinctrl-2`(master 态)**
  管脚（spif3 → PF09/PF05/PF03/PF11/PF07 + CSN PI00），而配对 DW SPI 控制器默认仍持板级 pinctrl
  （spi3 → PG14/PG15/PG03/PE01/PE00；spi4 → PQ00/PQ03/PQ04/PQ05/PQ06）；两组管脚不同 ⇒ 不清空
  就是"两个 owner 抢一组 net"。参考资产（实机跑通的 `spi_master_communication` / `spi_monitor_filter`）
  正是这么做的。
  **对既有用例的影响（结论：无回归，只消除冲突）**：
  | 既有项 | 是否受影响 | 论证 |
  |---|---|---|
  | `master_read_jedec_id` | **否** | 事务由 `spi_transceive(spif_spi_dev(spif3)=spi4, …)` 发起，**时钟/数据来自 spif3 的 master 态管脚**（不经 spi4 板级 pinctrl）；**CS 来自 `spif_spi_cs(spif3)` = spif3 自己的 `cs-gpios`(GPIOI0)**（模块 dtsi 定义），与 `&spi4 { cs-gpios }` 无关。清空 spi4 的 pinctrl-0 只是移除冗余/冲突的引脚归属，**不放宽任何前置** |
  | `master_read_with_addr` | **否**（同上，spif1 → spi3） | 同理，spi3 的板级 pinctrl 让渡给 spif1 的 master 态管脚 |
  | `bus_pairing` / `device_ready` | **否** | `device_is_ready(spif_spi_dev(dev))` 只要求配对控制器初始化成功；`pinctrl` 为空是其**合法**状态（Zephyr `pinctrl` 为可选），`status="okay"` 不变 |
  | 寄存器级用例 | **否** | 不触及 spi3/spi4 总线 |
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
device_ready | !none | !none:4 实例 ready + 配对 spi 设备 + cs gpio 端口，多子结果合取且本归档无 INFO
cold_reset_state | cfg == 0x40; target_addr == 0xffffffff; bcmd == 0x7 | driversrc:复位后 CFG/TARGET_ADDR/BCMD 整字（IP_LOCK=1 落在 CFG.bit6 ⇒ 0x40）
reg_lock_unlock | unlock_ip_lock == 1; lock_ip_lock == 0 | spec:IP_LOCK 位语义（unlock⇒1 / lock⇒0）
sck_threshold_config | sck_div == 5; hi == 2; lo == 100 | driversrc:SCK 阈值写入值转录自驱动配置
addr_mode_config | sel_4b == 1; sel_3b == 0 | driversrc:ADDR_3B_4B_SEL 位值
cmd_table_full_boundary | capacity == 40; added == 40 | spec:通用命令表容量 = SPIF_GENERAL_CMD_TABLE_NUM = 40（硬件常量）
addr_whitelist_fill | reg_nun == 512; reg_last == 511 | driversrc:读/写窗口各 512 个寄存器（原文给定）
addr_privilege_guard | guard_boundary == 0x10000000; len_min == 16384 | handcalc:MB(256)=256×2^20=0x10000000；KB(16)=16×1024=16384
addr_privilege_valid_region | len == 16384 | handcalc:KB(16)=16384（原文给定区间长度）
flash_size_boundary | max == 0x10000000; min_ok == 0x10000 | handcalc:MB(256)=0x10000000；KB(64)=65536=0x10000
master_read_jedec_id | !none | !none:原表自述"不预设厂商值，基准待实测"；观测项不设独立预期
master_read_with_addr | !none | !none:同上（"不预设器件值"）
filter_enable_bit | !none | !none:true/false 两种 API 各验一次的合取，无单一标量
cmd_idx_accessors | !none | !none:CMD/DUMMY_CYCLE 按 idx 往返的合取；本次归档该项 FAIL 且无 INFO
cmd_table_add_remove | !none | !none:add/remove/重复 remove/固定表段四条路径合取
pinctrl_state_switch | !none | !none:filter/master 两态 check 的合取，无单一标量
passthrough_mode_switch | !none | !none:同上
bus_pairing | !none | !none:4 实例指针与 DT phandle/cs-gpios 的比对，非标量
```
