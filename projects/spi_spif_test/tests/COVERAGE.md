# spi_spif_test 合并测试覆盖矩阵

> **工程定位**：把 `projects/spi_test`（套件 `spi` + `spi_flash`）与
> `projects/spif_test`（套件 `spif`）合并为一个单核固件 `cpu1_spi_spif_test`。
> 三套件互不共享状态；套件内每项自带前置。
>
> **模式/引脚切换口径（本工程统一）**：
> - master 切换一律走 `spif_switch_to_mode(dev, spif_master, spif_pinctrl_master)`
>   （该分支忽略 state 参数，由驱动切到 `pinctrl-2` master 引脚并清 `CFG.EN`）。
> - filter / monitor 切换走
>   `spif_switch_to_mode(dev, <mode>, <state>)`：
>   `spif_single_passthrough` → `PINCTRL_STATE_DEFAULT`，
>   `spif_multi_passthrough` → `PINCTRL_STATE_PASSTHROUGH`，
>   `spif_no_passthrough` → `PINCTRL_STATE_NOPASSTHROUGH`（filter 模式不支持）。
> - 切换结果与引脚状态一律用 `spif_pinctrl_state_get()` 回读，
>   不再使用 `spif_pinctrl_*_mode_check()`。

## 被测对象

- 驱动：
  - `zephyr/drivers/spi/spi_ls.c`（compatible `linkedsemi,ls-spi`；spi1/spi2）
  - `zephyr/drivers/spi/spi_dw.c`（`snps,designware-spi`；spi3/spi4，spif master 的底层控制器）
  - `modules/lib/zephyr_pfr_tpm/drivers/pfr/spi/spi_filter.c`（`linkedsemi,spi-filter`；spif1..4）
- 接口/寄存器：
  - `zephyr/drivers/spi.h` + `spi_context.h`；`modules/hal/linkedsemi/hal_driver/inc/reg_spi_type.h`
  - `modules/lib/zephyr_pfr_tpm/drivers/pfr/spi/spi_filter.h` + `reg_spi_filter.h`
- 实例：
  - `spi1` @ `0x4008a000`、`spi2` @ `0x4008a400`（LS SPI，CS = GPIOA3 / GPIOJ15）
  - `spif1..4` @ `0x40070000 / 0x40072000 / 0x40074000 / 0x40076000`，`index=0..3`
  - 配对关系：`spif1/spif2 → spi3`（CS0=GPIOI8、CS1=GPIOG2）；`spif3/spif4 → spi4`（CS0=GPIOI0、CS1=GPIOQ0）
- 外部条件：**部分需要**（与 `projects/spif_test` 同一套板级接线）
  - 套件 `spi`：片内引脚组，总线上无 DT 从设备；判据来自驱动源码/寄存器语义。
  - 套件 `spi_flash`：板载 `spif3 master → spi4 → flash4_0`，无外部接线。
  - 套件 `spif`：寄存器级用例与 master 直驱板载 flash 零外部条件；
    `loopback_*`（6 项）需 `spif1`(master 态) ↔ `spif3`(filter 态 in 侧) 跳线
    （1 MHz / 8 bit / 单线，最少 CS/CLK/IO0/IO1 四根；完整接线见
    `projects/spif_test/tests/COVERAGE.md`「参考资产出处」）。
- 关键常量：`SPIF_FIXED_CMD_TABLE_NUM=30`、`SPIF_GENERAL_CMD_TABLE_NUM=40`、
  `SPIF_CMD_TABLE_NUM=70`、`SPIF_ADDR_PRIV_REG_NUN=512`、`SPIF_ADDR_WHITELIST_SIZE=MB(256)`。

## 套件 `spi`（spi_ls，18 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| device_ready | `LS_SPI_INIT`（`spi_ls.c:474-503`）→ `spi_ls_init()`（`:415-472`）→ `spi_context_cs_configure_all()`（`:462`） | 功能通路 | 两实例 ready；基址 == `0x4008a000`/`0x4008a400`；`cs-gpios` 长度各 1；CS 端口 ready | DT 绑定契约 + 驱动 init | ⏳ |
| configure_mode_bits | `spi_ls_configure()`：MSTR/LSBFIRST/DS/CPOL/CPHA/SSOE/TXFTH；`spi_ls_complete()` 的 `SPE=0` | 功能通路 | `CR1.MSTR==1`、`CPOL==1`、`CPHA==1`、`LSBFIRST==0`、`BR==/16`、`SPE==0`；`CR2.DS==0x7`、`SSOE==1`、`TXFTH==4`；`ret==0` | 驱动源码 + `reg_spi_type.h` | ⏳ |
| configure_lsb_first | `spi_ls_configure()` `:93-97` | 功能通路 | LSB → `LSBFIRST==1`；随后 MSB → `==0`；`ret==0` | 驱动源码 | ⏳ |
| configure_datasize16 | `spi_ls_configure()` `:100-104`；`spi_ls_transceive()` `:324-328` | 功能通路 | `DS==0xF`；4 字节传输 `ret==0` | 驱动源码 + HAL 编码 | ⏳ |
| configure_prescaler_table | `spi_ls_configure()` `:123-129` + `scaler[]` | 边界长度/时序 | `9375000→/8`、`5000000→/16`、`3000000→/32`、`1500000→/64`、`800000→/128`、`400000→/256`、`292968→/256` | 驱动源码公式（clock=75 MHz） | ⏳ |
| configure_freq_boundary | `spi_ls_configure()` `:118-121` | 边界长度/异常参数 | `9375000 → ret==0, BR==/8`；`9375001 → ret==-EINVAL, SPE==0` | 驱动源码。⚠️ 先 `spi_arm_instance()` | ⏳ |
| transceive_null_bufs | `spi_ls_transceive()` `:312-314` | 异常参数 | `transceive(NULL,NULL)==0`；随后 `spi_release()==-EINVAL` | 驱动源码 | ⏳ |
| transceive_length_sweep | `spi_ls_transceive()` `:324-328` → `spi_ls_data_exchange_master()` | 边界长度 | 15 档长度全部 `ret==0` 且 `SPE==0` | 驱动源码 + `spi_context` 超时语义 | ⏳ |
| transceive_duplex_pair | `spi_ls_transceive()` `:337-343`；`spi_context_update_rx` | 功能通路 | 32 B 全双工 `ret==0`；RX 内容只上报不断言（MISO 悬空） | 驱动源码 | ⏳ |
| transceive_tx_only_rx_only | `spi_context_tx_buf_on()` / `spi_context_rx_buf_on()`（`:175-204`） | 功能通路 | tx-only `ret==0`；rx-only `ret==0` | 驱动源码 | ⏳ |
| error_half_duplex | `spi_ls_configure()` `:72-75` | 异常参数 | `ret == -ENOTSUP` | 驱动源码。⚠️ 先 `spi_arm_instance()` | ⏳ |
| error_ti_mode | `spi_ls_configure()` `:77-80` | 异常参数 | `ret == -ENOTSUP` | 驱动源码。⚠️ 先 `spi_arm_instance()` | ⏳ |
| release_semantics | `spi_ls_release()` `:373-393` | 异常参数/功能通路 | 未配置 → `-EINVAL`；已配置且空闲 → `0` 且 `SPE==0` | 驱动源码 | ⏳ |
| reconfigure_same_cfg_prescaler | `spi_ls_configure()` `:64-70`（早退前先写 `SPE=0`/`BR←scaler[0]`） | 异常参数（缺陷固化） | 首次 400 kHz → `BR==/256`；同指针二次 → `BR==/8` | 驱动源码（**刻意固化缺陷 2**） | ⏳ |
| hold_on_cs_spe | `spi_ls_complete()` `:269-272`；`_spi_context_cs_control()` | 功能通路 | HOLD 后 `SPE==1`；普通传输后 `SPE==0` | 驱动源码 + `spi_context` | ⏳ |
| lock_on_hold_release | `spi_context_lock()` 的 `SPI_LOCK_ON` 短路 + `spi_ls_release()` | 并发/时序 | LOCK_ON 连发两次 `ret==0`；`spi_release()==0` | `spi_context` 契约 | ⏳ |
| concurrent_two_threads | `spi_context_lock()`（`k_sem`，`K_FOREVER`） | 并发 | 两线程各 4 轮 × 16 B 全部 `ret==0` | 驱动 + `spi_context` 锁契约 | ⏳ |
| post_reset_state | `spi_ls_init()` 的 reset 路径 + `spi_ls_configure()` 全量重配 | 复位/掉电 | 复位后 `SPE==0`、`MSTR==0`、`BR==/8`；恢复传输 `ret==0` 且 `BR==/32` | 寄存器位域 + 驱动写入集合（BR 复位默认值实测） | ⏳ |

> 本套件上游缺陷（spi_ls.c 的 NULL 解引用、同指针重配漂移、分频越界等）
> 完整记录见 `projects/spi_test/tests/COVERAGE.md`「上游缺陷观察」，本工程不重复维护。

## 套件 `spi_flash`（11 项，spif3 master → spi4 → flash4_0）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|
| alloc_read_smoke | `k_malloc(4096)` 只读 | 功能通路 | `ret=0` | 板载 flash 契约 | ⏳ |
| flash_ready | `spi_nor_multi_dev.c` init / `flash_read_jedec_id()` | 功能通路 | JEDEC 非全 0/全 FF；容量覆盖测试区 | JEDEC/SFDP | ⏳ |
| erase_all_ff | `spi_nor_multi_dev.c` erase | 擦除语义 | 擦除后回读全 0xFF | 驱动源码 | ⏳ |
| read_aligned_len_sweep | read + DW SPI RX DMA | 边界长度 | 每档 `ret=0` 且逐字节等于 pattern | 驱动源码 | ⏳ |
| read_unaligned_len_sweep | 同上（缓冲区 +8） | 边界长度 | 同上 | 驱动源码 | ⏳ |
| read_malloc_aligned_len_sweep | `k_aligned_alloc(32,len)` 读 1..4096 | 边界长度 | 同上 | 驱动源码 | ⏳ |
| read_malloc_unaligned_len_sweep | `k_malloc(len)` 读 1..4096 | 边界长度 | 同上 | 驱动源码 | ⏳ |
| write_aligned_len_sweep | write + DW SPI TX | 边界长度 | 每档 `ret=0` 且逐字节相等 | 驱动源码 | ⏳ |
| write_unaligned_len_sweep | 同上（缓冲区 +8） | 边界长度 | 同上 | 驱动源码 | ⏳ |
| write_malloc_aligned_len_sweep | `k_aligned_alloc(32,len)` 写 1..256 | 边界长度 | 同上 | 驱动源码 | ⏳ |
| write_malloc_unaligned_len_sweep | `k_malloc(len)` 写 1..256 | 边界长度 | 同上 | 驱动源码 | ⏳ |

测试区：`0x100000`（读/擦）、`0x110000`（对齐写）、`0x120000`（非对齐写）。
套件 setup 用 `spif_switch_to_mode(spif3, spif_master, spif_pinctrl_master)` 建立
master 态，并断言 `spif_pinctrl_state_get(spif3) == spif_pinctrl_master`；
套件 teardown 用 `spif_switch_to_mode(spif3, spif_filter, spif_single_passthrough)` 恢复。

> **堆分配说明**：本套件 4 个 `*_malloc_*` 项刻意覆盖堆缓冲（`k_malloc` /
> `k_aligned_alloc`），属 qsh-test-validate §1.9c「测试代码禁用堆分配」的
> **有意例外**（沿用 `spi_test` 的既有设计）：`CONFIG_HEAP_MEM_POOL_SIZE=16384`
> 已在 prj.conf 配置，每次分配都检查 NULL 并计入失败，不会因空指针把串口打挂。
> 该维度（对齐/非对齐的堆缓冲）无法用静态数组等价替代，故保留。

## 套件 `spif`（spi_filter，25 项）

### 用例 → 实例分配（保证用例间零残留）

| 实例 | 用例 | 理由 |
|---|---|---|
| spif1 (idx 0) | cold_reset_state / filter_enable_bit / reg_lock_unlock / sck_threshold_config / addr_whitelist_fill / addr_privilege_guard / addr_privilege_valid_region / master_read_with_addr | 寄存器级；**从不设置 `flash_size`**，使 `addr_privilege_guard` 的 `flash_size==0` 前置恒成立 |
| spif2 (idx 1) | cmd_idx_accessors / cmd_table_add_remove / cmd_table_full_boundary / passthrough_mode_switch | 命令表类，自清理；passthrough 收尾切回 filter |
| spif3 (idx 2) | mode_switch_states / mode_switch_guard / master_read_jedec_id / loopback_*（filter 侧） | 管脚/总线类；`mode_switch_*` 用 spif4 作相邻实例 |
| spif4 (idx 3) | addr_mode_config / flash_size_boundary / mode_switch_*（相邻实例） | `flash_size` 改变不可逆，隔离到独立实例 |
| （只读遍历） | spif_device_ready / bus_pairing | 只读设备指针/DT 契约 |

### 用例表

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| spif_device_ready | `SPI_FILTER_INIT`：`.spi` / `.cs` / `.index`；`spif_get_ctrl_idx()` / `spif_spi_dev()` / `spif_spi_cs()` | 功能通路 | 4 实例 ready；`ctrl_idx == 0..3`；配对 spi ready；cs gpio 端口 ready | DT 绑定契约 | ⏳ |
| cold_reset_state | `linkedsemi_spi_filter_cold_reset()`：clock/reset/pinctrl/reg_unlock/白名单/固定表/CFG | 复位/掉电 | `CFG.EN==0`、`OPERATION_MODE==0`、`ADDR_3B_4B_SEL==0`、`TARGET_ADDR_MODE_SEL==0`、`IP_LOCK==1`、`DMA_EN==0`；`TARGET_ADDR==0xffffffff`；`BCMD_RANGE==0x7`；固定表 30 槽 EN 全 0 | 驱动源码 + DT 属性 | ⏳ |
| filter_enable_bit | `spif_enable()` / `spif_filter_enable()` 只改 `SPIF_CFG.EN` | 功能通路 | true → `EN==1`；false → `EN==0` | 驱动源码 | ⏳ |
| reg_lock_unlock | `spif_reg_unlock()` 写 `IP_LOCK=1`；`spif_reg_lock()` 写 `IP_LOCK=0` | 功能通路 | 与 API 名反相的寄存器语义 | 驱动源码 | ⏳ |
| sck_threshold_config | `spif_clk_check_config()`：`SCK_DIV`/`SCK_FQC_HI`/`SCK_FQC_LO`/`INTR_MASK.SCK_CHECK` | 功能通路 | `SCK_DIV==5`；`HI==2`；`LO==100`；`SCK_CHECK` 1→0 | 寄存器位宽 | ⏳ |
| addr_mode_config | `spif_4byte_mode_config()` / `spif_3byte_mode_config()` | 功能通路 | `ADDR_3B_4B_SEL` 1/0 | 驱动源码 | ⏳ |
| cmd_idx_accessors | `spif_set/get_cmd_by_idx()`、`set/get_dummy_by_idx()`、`replace_fixed_*` | 功能通路 | CMD 与 DUMMY 按 idx 往返（DUMMY 用槽 6 FAST_READ）；5 bit 截断 `0xff→0x1f` | 寄存器位宽 + DT 默认 dummy | ⏳ |
| cmd_table_add_remove | `spif_add_general_cmd()` / `spif_get_cmd_slot()` / `spif_remove_general_cmd()`；固定表分支 `spif_add_cmd()` | 功能通路+异常参数 | add/remove/-EINVAL；固定表段 `CMD==0x03` 且 `EN==1` | API 契约 + 驱动源码（缺陷 #1 回归护栏；只断 CMD/EN） | ⏳ |
| cmd_table_full_boundary | general 表耗尽 → `-ENOSR` | 边界长度 | 前 40 个 0；第 41 个 `-ENOSR` | 驱动源码常量 | ⏳ |
| addr_whitelist_fill | `spif_memset_addr_whitelist()`：num=0/1/非法；间接 SRAM 端口回读 | 功能通路+异常参数 | 0 → 全 0；1 → 全 `0xffffffff`；非法 num 不改寄存器 | 驱动源码 + `SPIF_ADDR_PRIV_REG_NUN=512` | ⏳ |
| addr_privilege_guard | `spif_address_privilege_config()` + `_op()` 守卫 | 异常参数+边界长度 | `addr==MB(256)`/`>MB(256)`/`len==0` → `-EINVAL`；`MB(256)-KB(16)` → 0 | 驱动源码（前置 `flash_size==0`） | ⏳ |
| addr_privilege_valid_region | `spif_address_privilege_config_op()` 镜像路径 + 间接端口回读 | 功能通路 | `MB(1)..+KB(16)` 的 READ/WRITE 位 1→0→1 | 驱动源码 + 地址换算 | ⏳ |
| flash_size_boundary | `spif_flash_size_set()` 守卫 / `_get()` | 边界长度 | `0`/`3`/`MB(256)+1` → `-EINVAL`；`MB(256)`/`KB(64)` → 0 且回读一致 | 驱动源码 | ⏳ |
| mode_switch_states | `spif_switch_to_mode()` 合法组合 + `spif_pinctrl_state_get()` | 功能通路 | filter+single：`EN=1,OPERATION_MODE=1,state=single`；filter+multi：`state=multi`；monitor+no：`EN=1,OPERATION_MODE=0,state=no`；monitor+single：`state=single`；master：`EN=0,state=master` | 驱动源码 + 模块 dtsi 四态 pinctrl | ⏳ |
| mode_switch_guard | `spif_switch_to_mode()` 守卫 + 相邻实例检查 | 异常参数 | filter+no → `-EINVAL`；query-only state → `-EINVAL`；非法 mode → `-EINVAL`；相邻实例 master → `-EBUSY`；释放后 master 切换成功 | 驱动源码 | ⏳ |
| passthrough_mode_switch | `spif_passthrough_analog_mux_enable()` + `spif_pinctrl_state_get()` | 功能通路 | cold_reset 后 `single`；enable 后 `multi`；切回 filter 后 `single` | 驱动源码 + pinctrl 状态表 | ⏳ |
| bus_pairing | `spif_spi_dev()` / `spif_spi_cs()` 与 DT phandle/cs-gpios 比对 | 功能通路 | spif1/2→spi3，spif3/4→spi4；CS 引脚逐一匹配 | DT 声明 | ⏳ |
| master_read_jedec_id | `spif_switch_to_mode(master)` + `spif_spi_dev()`/`spif_spi_cs()` + `spi_transceive(0x9F)` | 功能通路+外部器件 | 事务 `ret=0`；JEDEC 经 `ZT_INFO` 上报 | 板载 NOR JEDEC 契约 | ⏳ |
| master_read_with_addr | 同上 + `0x03 + 24bit addr` | 功能通路+外部器件 | 事务 `ret=0`；读回数据经 `ZT_INFO` 上报 | 板载 NOR 读契约 | ⏳ |
| loopback_jedec | `loopback_setup()`：spif3 filter/passthrough + spif1 master；`0x9F` | 功能通路+外部器件 | `ret=0`；JEDEC 非全 0/全 FF | 参考资产 `spi_monitor_filter`；外部: spif1 ↔ spif3 跳线 | ⏳ |
| loopback_read_addr | 同上，`0x03 + 0x004000` 读 8 B | 功能通路+外部器件 | `ret=0`；数据上报 | 参考资产；外部: spif1 ↔ spif3 跳线 | ⏳ |
| loopback_filter_legal_both | 命令合法 + 地址合法 | 过滤判据 | `CMD_ERR==0`、`ADDR_ERR==0` | 驱动/HW 语义 | ⏳ |
| loopback_filter_legal_cmd_bad_addr | 命令合法 + 地址非法 | 过滤判据 | `CMD_ERR==0`、`ADDR_ERR==1`、`POR_ADDR==1`、`ERROR_ADDR==0x4000` | 驱动/HW 语义 | ⏳ |
| loopback_filter_bad_cmd_legal_addr | 命令非法 + 地址合法 | 过滤判据 | `CMD_ERR==1`、`ADDR_ERR==0`、`ERROR_CMD==0x03` | 驱动/HW 语义 | ⏳ |
| loopback_filter_bad_cmd_bad_addr | 命令非法 + 地址非法 | 过滤判据 | `CMD_ERR==1`、`ADDR_ERR==0`、`ERROR_CMD==0x03` | 驱动/HW 语义（命令先查） | ⏳ |

> 本套件上游缺陷（`spif_add_cmd` 表外写已修、`spif_cmd_t` union 未清零、
> `spif_address_privilege_config` 镜像循环恒返回 0 等）完整记录见
> `projects/spif_test/tests/COVERAGE.md`「上游缺陷观察」，本工程不重复维护。

## 跳过项

| 跳过的项 | 判据类型 | 原因 |
|---|---|---|
| DMA 日志通路（`spif_dma_start` / `spif_dma_work` / 回调） | 判定不可靠 | `spif_dma_work()` 用未初始化 `struct dma_status` 且忽略返回值；终止条件依赖 DMA 计数变化 |
| 中断/错误回调实触发 | 缺硬件条件 | 需要上游 master 在滤波 in 侧制造非法命令/越界地址；本工程 in 侧未接线 |
| `spif_operation_mode_config()` 的行为差异 | 行为层判据缺失 | 滤波 vs 监视的行为差异需真实总线流量；寄存器位本身已由 `mode_switch_states` 覆盖 |
| `spif_passthrough_analog_mux_enable()` 的 `enable=false` 语义 | 驱动未实现 | 函数体忽略 `enable`，恒应用 passthrough（上游缺陷 #7） |
| `spif_dump_rw_addr_privilege_table()` 的错误传播 | 缺陷规避 | 丢弃解析返回值后检查恒 0 的 `ret`，`-EIO` 永不外传 |
| `spif_address_privilege_config()` 的 `flash_size != 0` 镜像循环 | 驱动未实现完整语义 | 恒返回 0，无可靠成功/失败判据 |
| `spif_switch_to_mode()` 的 force 路径（`spif_switch_to_master_force`） | 本工程未纳入 | 新统一入口使用非 force 语义；force 路径属旧 API，保留给驱动侧单独覆盖 |

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
> 来源标签：`handcalc`(手算) `spec`(外部规范/寄存器或 DT 定义) `golden`(黄金向量)
> `crosscheck`(跨来源交叉) `contract`(API 契约值) `driversrc`(转录自被测驱动实现，
> 只证"没变"不证"仍对") `samecode`(同源复算，非独立) `!none`(无确定预期)。

```expect
# 测试项 | 断言 | 来源标签:说明
device_ready | spi1_base == 0x4008a000; spi2_base == 0x4008a400 | spec:DT `spi1`/`spi2` reg 基址
configure_mode_bits | ret == 0; cr1 == 0x11f; cr2 == 0x40704 | driversrc:CR1/CR2 整字转录自驱动写入集合
configure_lsb_first | ret_lsb == 0; lsb_bit == 128; ret_msb == 0; msb_bit == 0 | driversrc:CR1.LSBFIRST 位值
configure_datasize16 | ret == 0; ds == 0xf00 | driversrc:CR2.DS 字段移位后的整字值
configure_freq_boundary | ret_ok == 0; ret_bad == -22; spe_after_bad == 0 | contract:边界档与 -EINVAL 契约
transceive_null_bufs | ret_null == 0; ret_release == -22 | contract:NULL 双向合法；未配置即 release ⇒ -EINVAL
transceive_length_sweep | n == 15; last_ret == 0 | contract:长度集共 15 档，最后档 ret==0
transceive_duplex_pair | ret == 0 | contract:全双工完成性（不判数据内容）
transceive_tx_only_rx_only | ret_tx == 0; ret_rx == 0 | contract:单向通路契约
error_half_duplex | ret == -134 | contract:API 契约 -ENOTSUP
error_ti_mode | ret == -134 | contract:API 契约 -ENOTSUP
release_semantics | ret_unused == -22; ret_tx == 0; spe == 0 | contract:未配置 ⇒ -EINVAL；已配置 ⇒ 0 且 SPE=0
reconfigure_same_cfg_prescaler | ret1 == 0; br_first == 0x38; ret2 == 0; br_second == 0x10 | driversrc:缺陷 2 的现状基线（同 cfg 二次调用 BR 被重置）
hold_on_cs_spe | ret_hold == 0; spe_hold == 64; ret_norm == 0; spe_norm == 0 | driversrc:HOLD 传输后 SPE 保持（SPI_CR1_SPE_MASK=0x40）、普通传输后被清
lock_on_hold_release | ret1 == 0; ret2 == 0; ret_rel == 0 | contract:SPI_LOCK_ON 短路契约
concurrent_two_threads | arm == 0; ret_a == 0; ret_b == 0; iter == 4 | contract:并发下两线程均无 -ETIMEDOUT
post_reset_state | ret_rst == 0; cr1_after == 0x10; ret_recover == 0; br_after == 0x20 | driversrc:复位默认 BR=/8、恢复后 /32
configure_prescaler_table | !none | !none:断言是分频选择规则的逐档合取，INFO 只有复合串
flash_ready | size > 0x130000 | spec:板载 NOR 容量覆盖测试区
erase_all_ff | ff_bytes == total | driversrc:擦除后回读必须全 0xFF
read_aligned_len_sweep | fail == 0; first == 0 | driversrc:对齐读逐字节比对
read_unaligned_len_sweep | fail == 0; first == 0 | driversrc:非对齐读逐字节比对
read_malloc_aligned_len_sweep | fail == 0; first == 0 | driversrc:malloc 对齐读逐字节比对
read_malloc_unaligned_len_sweep | fail == 0; first == 0 | driversrc:malloc 非对齐读逐字节比对
write_aligned_len_sweep | fail == 0; first == 0 | driversrc:对齐写逐字节比对
write_unaligned_len_sweep | fail == 0; first == 0 | driversrc:非对齐写逐字节比对
write_malloc_aligned_len_sweep | fail == 0; first == 0 | driversrc:malloc 对齐写逐字节比对
write_malloc_unaligned_len_sweep | fail == 0; first == 0 | driversrc:malloc 非对齐写逐字节比对
alloc_read_smoke | len == 4096; ret == 0 | contract:malloc 只读冒烟
spif_device_ready | !none | !none:4 实例 ready + 配对设备/CS 的合取，无 INFO
cold_reset_state | cfg == 0x40; target_addr == 0xffffffff; bcmd == 0x7 | driversrc:复位后 CFG/TARGET_ADDR/BCMD 整字
filter_enable_bit | !none | !none:true/false 两种 API 各验一次，无单一标量
reg_lock_unlock | unlock_ip_lock == 1; lock_ip_lock == 0 | spec:IP_LOCK 位语义（unlock⇒1 / lock⇒0）
sck_threshold_config | sck_div == 5; hi == 2; lo == 100 | driversrc:SCK 阈值写入值
addr_mode_config | sel_4b == 1; sel_3b == 0 | driversrc:ADDR_3B_4B_SEL 位值
cmd_idx_accessors | slot6_dummy == 9 | driversrc:槽 6 FAST_READ 的 DUMMY 往返值
cmd_table_add_remove | !none | !none:add/remove/固定表段多条路径合取
cmd_table_full_boundary | capacity == 40; added == 40 | spec:通用命令表容量 = SPIF_GENERAL_CMD_TABLE_NUM
addr_whitelist_fill | reg_nun == 512; reg_last == 511 | driversrc:读/写窗口各 512 个寄存器
addr_privilege_guard | guard_boundary == 0x10000000; len_min == 16384 | handcalc:MB(256)=0x10000000；KB(16)=16384
addr_privilege_valid_region | len == 16384 | handcalc:KB(16)=16384
flash_size_boundary | max == 0x10000000; min_ok == 0x10000 | handcalc:MB(256)=0x10000000；KB(64)=0x10000
mode_switch_states | filter_single == 0; filter_multi == 1; monitor_no == 2; master == 3 | spec:spif_pinctrl_state_t 枚举值（dtsi 四态）
mode_switch_guard | !none | !none:-EINVAL/-EBUSY 多分支合取，无 INFO 标量
passthrough_mode_switch | state == 0 | spec:cold_reset 后 PINCTRL_STATE_DEFAULT ⇒ spif_single_passthrough
bus_pairing | !none | !none:4 实例指针与 DT phandle/cs-gpios 的比对，非标量
master_read_jedec_id | !none | !none:观测项，不预设厂商值
master_read_with_addr | !none | !none:观测项，不预设器件值
loopback_jedec | !none | !none:JEDEC 读回值来自板载 flash，只判非全 0/全 FF
loopback_read_addr | !none | !none:读回数据来自板载 flash，仅判事务 ret=0
loopback_filter_legal_both | cmd_err == 0; addr_err == 0; por_addr == 0 | spec:命令合法 + 地址合法无 ERROR
loopback_filter_legal_cmd_bad_addr | cmd_err == 0; addr_err == 1; por_addr == 1; error_cmd == 0x03; error_addr == 0x4000 | spec:命令合法 + 地址非法只报 ADDR_ERR
loopback_filter_bad_cmd_legal_addr | cmd_err == 1; addr_err == 0; por_addr == 0; error_cmd == 0x03 | spec:命令非法 + 地址合法只报 CMD_ERR
loopback_filter_bad_cmd_bad_addr | cmd_err == 1; addr_err == 0; por_addr == 0; error_cmd == 0x03 | spec:命令先查，命中后不再上报地址错误
```
