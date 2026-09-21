# TPM SPI slave (spid) 测试覆盖矩阵

- 驱动: `modules/lib/pfr_tpm/drivers/spid/spid_linkedsemi.c`
- 接口: `modules/lib/pfr_tpm/drivers/spid/spid_linkedsemi.h`；寄存器定义 `reg_spid.h`
- 实例: `tpm_spis1` @ `0x4000c000`、`tpm_spis2` @ `0x4000e000`（overlay 全 `okay`）
  - **地址一律取自 DT 节点**（`DT_NODELABEL(tpm_spis1/2)`），不使用模块
    `samples/spid/src/main.c:34` 硬编码的 `SPID2_BASE = 0x4000e000`
    —— 该值与 `aliases { spid = &tpm_spis1 }` 不一致（roadmap §7.2 S-3）
  - 中断：`interrupts = <56 3>, <57 3>` / `<58 3>, <59 3>`，`interrupt-names = "main","sysc"`
- 外部路径: **有** —— SoC 自己的 SPI master 与 TPM SPI slave 之间的自环跳线
  - `外部: tpm_spis1 的 MISO PI04 / MOSI PI05 / SCK PI06 / CSN PI07 与 SoC SPI master（本 overlay 选 spi1，管脚 PA00/PA01/PA02 + cs gpioa[0]）四线对连`
  - 其余寄存器级用例零外部条件
- 关键常量: `INTF_FIFO_MODE=0` / `INTF_CRB_MODE=1`（`spid_linkedsemi.h:13-16`）；
  `SPID_TPM_CFG` 写入值 FIFO `0x11` / CRB `0x13`（`spid_linkedsemi.c:88,90`）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| device_ready | `SPID_LINKEDSEMI_INIT` 宏（`spid_linkedsemi.c:174-192`）`DEVICE_DT_INST_DEFINE` + `PINCTRL_DT_INST_DEFINE` | 功能通路 | 两实例 `device_is_ready` 均真；DT reg 地址非 0 且**两实例地址不同** | DT 绑定契约（`linkedsemi,spid.yaml`） | ⏳ |
| cold_reset_state | `spid_linkedsemi_cold_reset()`（`:98-146`）：clock off → `reset_line_toggle` → clock on → `pinctrl_apply_state(DEFAULT)` → `SPID_INTR_ENABLE = 0xff`（`:143`） | 复位/掉电 | 两实例复位后 `SPID_INTR_ENABLE == 0xff` | 驱动源码常量 | ⏳ |
| cold_reset_idempotent | 同上整条时钟/复位/pinctrl 路径重复执行 | 复位/掉电 | 连续 3 轮 `cold_reset` 均返回 0 且 `INTR_ENABLE` 保持 `0xff` | 驱动源码（幂等性契约） | ⏳ |
| init_register_modes | `init_spid_registers()`（`:82-96`）：`INTF_FIFO_MODE → SPID_TPM_CFG=0x11`（`:88`）；`INTF_CRB_MODE → 0x13`（`:90`）；**else → 仅 printk，不写寄存器**（`:91-93`） | 功能通路+异常参数 | FIFO/CRB 写入后按掩码 `0x13` 回读分别为 `0x11`/`0x13`；两者差异位 = bit1；非法模式 `0x7f` 后寄存器**保持不变** | 驱动源码（掩码用于屏蔽保留位回读差异，原始值经 `ZT_INFO` 上报） | ⏳ |
| instance_isolation | 两实例各自独立寄存器窗；`cold_reset` 只作用于目标实例 | 功能通路 | spis1 写 FIFO、spis2 写 CRB 后各自保持；再 `cold_reset(spis1)` 不影响 spis2 的 `TPM_CFG` | 驱动源码 + DT（两节点不同 reg） | ⏳ |
| callback_register | `spid_linkedsemi_register_callback()`（`:66-77`）：纯指针落地，恒 `0`，`callback_idx` 不校验 | 功能通路 | **读回 `dev->data` 的 `cb`/`user_data` 字段**（按驱动 `struct spid_linkedsemi_data`（`spid_linkedsemi.c:32-37`）同布局复刻视图）：注册后 `cb == 注册的函数指针`、`user_data == 注册指针`；覆盖注册 NULL 后归零；两实例各自落地；`callback_idx` 传 `0xffffffff` 仍被接受 | 驱动源码（字段赋值）+ 白盒布局视图。**2026-09-18 F1 波改判**：原实现只断恒 `0` 的返回值 | ⏳ |
| interrupt_register | `spid_linkedsemi_interrupt_register()`（`:148-156`）→ `irq_config_func` → `IRQ_CONNECT(DT_INST_IRQ_BY_NAME(index, main, irq))` + `irq_enable`（`:163-172`） | 中断/时序 | ⚠ **无判别力**（2026-09-18 F1 波如实标注）：驱动恒返回 `0`，故"两实例重复安装均返回 `0`"是**恒真断言**。中断使能态改用 **`ZT_INFO` 上报**（`irq_is_enabled(main IRQ)` 前后 + 重复后），**不作断言** —— 该读回值在本 SoC 的语义**无法从仓库内确证**：`riscv_clic_irq_is_enabled()`（`soc/linkedsemi/common/e906/irq_driver.c:34`）走 `csi_vic_get_enabled_irq()`，而后者在 `CONFIG_INTC_CLIC` / `CONFIG_INTC_CLIC_APLIC` / `CONFIG_INTC_APLIC` **三者皆假时返回未初始化的 `ret`**（`core_rv32.h:521-546`），本板 `.config` 三者皆未置位。断言它会引入**假 FAIL 风险**。待 P6 用 INFO 值定论后再决定是否升级为断言 | 驱动源码 + DT `interrupt-names = "main"`（`lsqsh_pinctrl_pfr_tpm.dtsi:104`；tpm_spis1 main=56、tpm_spis2 main=58）。**待父会话裁定** | ⏳ |
| intr_state_clear_idiom | ISR 清中断写法：`sys_write32(0xff, reg + SPID_INTR_STATE)`（`:63`）⇒ 假定该寄存器为 **W1C** | 中断/时序 | 写 `0xff` 到 `SPID_INTR_STATE` 后读回 `0`；空闲初值经 `ZT_INFO` 上报 | 驱动 ISR 源码（`:63`）。⚠️ 若读回非 0 说明该寄存器是普通 RW（驱动清中断写法无效）——属需上报的发现，见「未决项」 | ⏳ |
| register_stability | TPM 子模块只读寄存器可访问性：`SPID_TPM_CAP`（0x800）、`SPID_TPM_DID_VID`（0x828）、`SPID_TPM_RID`（0x82c）、`SPID_TPM_STATUS`（0x808）、`SPID_TPM_ACCESS_0`（0x80c） | 功能通路 | `SPID_TPM_CAP` 既非全 0 也非全 1（IP 已时钟/响应）；连续两次读一致；**空闲时 `SPID_TPM_STATUS.cmdaddr_notempty(bit0) == 0`**（无主控下发）；各值经 `ZT_INFO` 上报 | 寄存器定义（`reg_spid.h`）+ 未时钟 IP 的回读特征 | ⏳ |
| master_send_tpm_frame | `cold_reset`（`:98`）+ `init_spid_registers(CRB)`（`:82`）+ `interrupt_register`（`:148`）后，由 SoC SPI master 发帧；观测 `SPID_TPM_STATUS` / `SPID_TPM_CMD_ADDR`（0x830） | 功能通路 | master 侧 `spi_transceive` 返回 0（**真断言**：跳线/引脚复用/CS/时钟任一坏掉都会 FAIL，但它只证明主机事务发出去了，**spid 从端收发全坏也不影响它 PASS**）；`SPID_TPM_STATUS`、`SPID_TPM_CMD_ADDR` 实测值经 `ZT_INFO` 上报 | 外部器件/接线契约。**从端无可靠判据**（2026-09-18 F1 波逐项查证）：`reg_spid.h:72/:82` 对两寄存器**无任何位域定义**，"bit0=cmdaddr_notempty" 仅见于 `samples/spid/src/main.c:42-44` 的注释；CRB 帧格式在仓库内**无出处**（驱动只把 `SPID_TPM_CFG` 写 0x11/0x13，从不触碰 `SPID_CMD_FILTER_*`/`SPID_CMD_INFO_*`，sample 也从不构造/发送帧）；`SPID_INTR_STATE` 是否 W1C 亦无文档（`intr_state_clear_idiom` 无法区分 W1C 与"未实现恒 0"）。⇒ 保留 INFO，**不编期望值**（假 FAIL 比空壳更糟）。帧格式确认后可升级为"`cmdaddr_notempty` 置起 + `CMD_ADDR` 比对" | ⏳ |

> 状态标记：`⏳` = **P4 新增，仅编译通过，待 P6 首次实测**。

## 跳过项

| 跳过的 case | 判据类型 | 原因 |
|---|---|---|
| ISR 回调实触发（`linkedsemi_spid_main_isr` 中 `data->cb()` 分支，`spid_linkedsemi.c:50-64`） | **缺硬件条件** | 要进 ISR 并让 `SPID_INTR_STATE` 非 0，必须由 SPI master 完成一次被 spid 识别的 TPM 事务；本工程只有 master 侧"事务成功"这一确定判据（见 `master_send_tpm_frame`），**中断是否置起依赖 CRB 帧格式与 spid 的命令过滤配置**（`SPID_CMD_FILTER_0..7` / `SPID_CMD_INFO_0..23` 的初始化在驱动里**没有实现**）。回调注册本身由 `callback_register` 覆盖 |
| `SPID_CMD_FILTER_*` / `SPID_CMD_INFO_*` / `SPID_JEDEC_CC` / `SPID_MAILBOX_ADDR` 等配置寄存器 | **驱动未实现** | `spid_linkedsemi.c` 全文只写 `SPID_INTR_ENABLE`（`:143`）、`SPID_INTR_STATE`（`:63`）、`SPID_TPM_CFG`（`:88,90`）三个寄存器；其余 TPM/命令过滤配置**无对应 API 与初始化代码**，无被测对象 |
| `sysc` 中断（`interrupt-names` 第二路，IRQ 57/59） | **驱动未实现** | `SPID_LINKEDSEMI_IRQ_HANDLER` 只 `IRQ_CONNECT` 了 `main` 一路（`:166-171`），`sysc` 无处理函数、无使能路径 |
| 背压 / WWDT 联动（模块 `samples/tpm_back_pressure_wwdt`） | **缺硬件条件** | 需要 spif2/spif4（SPI filter）与 tpm_spis2 同时参与的真实 TPM 背压场景；属跨 IP 集成场景，不在本工程范围（该 sample 依赖 spif 侧与外部 flash，需额外接线） |
| `samples/spid/src/main.c` 的 `poll_tpm_command()` 轮询语义 | **缺硬件条件** | 其轮询 `SPID_TPM_STATUS` bit0 等主控下发，与本工程 `master_send_tpm_frame` 同源；轮询本身无新分支（`while(true)` 忙等），已由 `register_stability` 的空闲状态断言覆盖 |

## 未决项（需父会话裁定 / P6 确认）

| 项 | 说明 |
|---|---|
| **SPI master 实例选择** | `master_send_tpm_frame` 用 overlay 的 alias `spidmaster = &spi1`。tpm_spis1 在 PI04-PI07，而 spi1 在 PA00-PA02 —— **没有同管脚复用的 master**，必须外部跳线；具体用哪路 SPI master（spi1/spi2/spif1..4 master 模式）**待用户确认**。改一处即可：overlay 的 `aliases` + `&spiX { status = "okay"; }` |
| **CRB 事务帧格式** | 占位帧为 `{0x00, 0x00,0x00,0x00,0x00}`（1 字节寄存器地址 + 4 字节数据）。真正的 TPM CRB 事务形态（地址字节含义、是否需先读 TPM_ACCESS 拿 locality）未确认；确认后可把判据从"事务成功"加强为"`cmdaddr_notempty` 置起 + `SPID_TPM_CMD_ADDR` 内容比对" |
| **`SPID_INTR_STATE` 是否 W1C** | `intr_state_clear_idiom` 断言写 `0xff` 后读回 `0`。若实测非 0，则驱动 ISR 的清中断写法无效（`spid_linkedsemi.c:63`），需按实测校正预期值并上报 |
| **spec S-3 的地址不一致** | 本工程已按 DT 实际节点取值（`tpm_spis1`），**未沿用** sample 的 `SPID2_BASE`。上游 `samples/spid/src/main.c:34,42,44` 是否要修正由父会话/上游裁决（铁律 2：不改上游） |

## 上游缺陷观察（只读记录，未修改 `modules/lib/pfr_tpm/`）

| # | 位置 | 问题 | 后果 |
|---|---|---|---|
| 1 | `spid_linkedsemi.c:50-64` | ISR 中 `data->cb()` 在**中断上下文**直接调用，且回调内无任何约束说明；`sys_write32(0xff, SPID_INTR_STATE)` 的清中断写法依赖该寄存器为 W1C（无文档确认） | 回调若调用会阻塞/加锁的 API 即构成 ISR 违规；清中断若不生效则中断风暴 |
| 2 | `spid_linkedsemi.c:82-96` | `init_spid_registers()` 非法模式的错误路径用 **`printk`**（而非 `LOG_ERR`/返回错误码），且函数返回 `void` | 调用方无法感知模式配置失败；且违反本仓 log 规范（`§6.1.7` 统一 LOG_INF）。**注意**：模块整体都用 printk，属上游风格，非本工程引入 |
| 3 | `spid_linkedsemi.c:163-172` | 只注册/使能 `main` 中断；DT 声明的 `sysc` 中断（IRQ 57/59）无处理路径 | `sysc` 事件无人处理（若硬件会置起，则可能反复进入未使能状态） |
| 4 | `spid_linkedsemi.c:148-156` | `interrupt_register` 无幂等保护，重复调用会重复 `IRQ_CONNECT` + `irq_enable` | 重复注册（`IRQ_CONNECT` 对同一 IRQ 重复调用在 Zephyr 里是编译期/运行期风险）。本工程 `interrupt_register` 用例刻意覆盖"重复安装均返回 0"以固化该行为 |
| 5 | `spid_linkedsemi.c:98-146` | `spid_linkedsemi_cold_reset` 里 `ret` 只在 `CONFIG_RESET`/`CONFIG_PINCTRL` 块内赋值（`:101` 声明 `int ret;` 未初始化） | 某些 Kconfig 组合下 `-Wmaybe-uninitialized` 风险；且 `CONFIG_PINCTRL` 缺失时 pinctrl 完全不生效 |
| 6 | `spid_linkedsemi.c:22` | `spid_linkedsemi_init()` 为空实现（`return 0`），未做设备自检 | "device ready" 不代表 TPM IP 已被时钟/复位——故 `register_stability` 单独做存在性判据 |
| 7 | `spid_linkedsemi.h:26` | `init_spid_registers()` 返回 `void`，且模式参数为 `int`（无枚举强约束） | 传任意 `int` 都编译通过，错误只能在运行期被 printk 发现 |

## 预期数据说明

- 错误码契约：`0`（驱动源码分支，行号见上表；本驱动几乎无错误返回路径）。
- 寄存器回读：按 `reg_spid.h` 的偏移与驱动写入值取预期；`SPID_TPM_CFG` 用掩码
  `0x13` 比较以屏蔽保留位回读差异，原始值一律 `ZT_INFO` 上报。
- 存在性判据（`SPID_TPM_CAP` 非全 0/非全 1）用于区分"IP 已时钟"与"IP 未响应"，
  阈值来源是未时钟 MMIO 的典型回读特征，非臆测的器件值。
- 数值实测经 `ZT_INFO` 上报（§6.5.4），报告"实际"列对比。
- 无外部黄金向量文件（见 `expected/README.md`）。

## P4 生成说明（2026-09-17，只生成 + 编译通过，未烧录）

- 新增 10 项，编译通过（`west build` rc=0，`lsqsh_evb@1os/lsqsh/cpu1`，无 warning）。
- **实例地址取自 DT**：`DT_REG_ADDR(DT_NODELABEL(tpm_spis1/tpm_spis2))`，
  直接回应 roadmap §7.2 S-3 的硬编码地址不一致问题（sample 用 0x4000e000 而 alias
  指向 0x4000c000），本工程不使用硬编码 base。
- 每项自带前置（`cold_reset` / `init_spid_registers`），两实例分派见 `device_ready`
  与 `instance_isolation`，用例间零残留。
- 外部路径仅 `master_send_tpm_frame` 一项，其 master 实例与帧格式列为未决项。
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
cold_reset_idempotent | intr_enable == 0xff | spec:连续 3 轮 cold_reset 不得改变复位默认值 0xff（幂等；★无 INFO 通道 ⇒ NO_ACTUAL）
device_ready | spis1 == 0x4000c000; spis2 == 0x4000e000 | spec:DT `spis1`/`spis2` reg 基址（与驱动实现无关，强预期）
cold_reset_state | intr_enable == 0xff | spec:复位后 SPID_INTR_ENABLE 全使能（0xff）
init_register_modes | fifo_cfg == 0x11; crb_cfg == 0x13 | driversrc:掩码 0x13 下的回读值转录自驱动模式表
instance_isolation | spis2 == 0x13 | driversrc:cold_reset(spis1) 不得动 spis2 的 TPM_CFG（原文给值 0x13）
intr_state_clear_idiom | after_w1c == 0 | spec:W1C 语义——写 0xff 后读回必为 0
register_stability | status == 0; cap != 0; cap != 0xFFFFFFFF | spec:空闲时 TPM_STATUS.cmdaddr_notempty(bit0)=0 ⇒ 整字 0；CAP 非全 0/全 1（IP 已响应）
master_send_tpm_frame | !none | !none:原文断言对象是 master 侧事务 + 从端帧解析的合取，本归档 INFO 不足以复原
callback_register | !none | !none:读回 dev->data 的 cb/user_data（白盒布局视图），非单个 INFO 键
interrupt_register | !none | !none:原表自述"⚠ 无判别力"（驱动恒返回 0 ⇒ 恒真断言），不登记
```
