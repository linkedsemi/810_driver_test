# Ethernet（MDIO / PHY / MAC）测试覆盖矩阵

> P3 新建工程（2026-09-17）：按 `SPEC_REFERENCE.md` §6.5.3 模板建档。
> **本工程处于「只生成 + 编译通过」阶段，未烧录、未采集、未判定 PASS/FAIL；全部用例状态 = ⏳（待 P6 首次实测）。**

## 被测对象

| 项 | 值 |
|---|---|
| MDIO 驱动 | `zephyr/drivers/mdio/mdio_dwmac.c`（`compatible = "snps,dwmac-mdio"`，实例 `mdio1@0x40064000`） |
| 以太网驱动 | `zephyr/drivers/ethernet/eth_dwmac_linkedsemi.c`（平台层）+ `zephyr/drivers/ethernet/eth_dwmac.c`（MAC/DMA 主体），`compatible = "linkedsemi,ethernet"`，实例 `eth1@0x40064000` |
| PHY 驱动 | `zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c`（`compatible = "realtek,rtl8211f"`，实例 `eth1_phy` = `mdio1` 下 `ethernet-phy@0`） |
| 板级使能 | `zephyr/boards/linkedsemi/lsqsh_evb/lsqsh_evb_cpu1.dts:263-276`（`&eth1` / `&mdio1` / PHY 均 `status="okay"`） |
| 硬件 | 板载 RTL8211F PHY（`reg=<0>`，`reset-gpios=<&gpiog 1>`，`int-gpios=<&gpiog 5>`），RJ45 经 RGMII 接 PHY |
| 工程 | `810_driver_test/projects/eth_test` |
| 类型 | **单核**（只有 `cpu1_eth_test/`）→ board `lsqsh_evb@1os/lsqsh/cpu1`，cpu1 入口 `0x10000000` |
| 套件 | `eth_mdio`（case_mdio）/ `eth_mdio`（case_phy_reset，同源集）/ `eth_link`（case_link）/ `eth_arp_icmp`（case_arp_icmp）/ `eth_udp_echo`（case_udp_echo） |

## case 变体（同一源目录，`-D` 选源集）

| case | CMake 开关 | 源文件 | 项数 | 对端要求 | P6 登记 |
|---|---|---|---|---|---|
| `case_mdio` | 默认 | `src/eth_mdio_test.c` | **5** | **零对端**（不依赖网线状态） | **默认启用** |
| `case_phy_reset` | 默认源集 + **专用 overlay** | `src/eth_mdio_test.c` | **5** | **零对端** | **默认启用** |
| `case_link` | `-DETH_LINK_TEST=y` | `src/eth_link_test.c` | 1 | 网线 + PC 网口激活 | `#` 屏蔽 |
| `case_arp_icmp` | `-DETH_ARP_ICMP_TEST=y` | `src/eth_arp_icmp_test.c` | 1 | 网线 + PC 同网段 IP + 放行 ICMP | `#` 屏蔽 |
| `case_udp_echo` | `-DETH_UDP_ECHO_TEST=y` | `src/eth_udp_echo_test.c` | 1 | 网线 + PC 同网段 IP + UDP 回声程序 | `#` 屏蔽 |

> **A 组含 5 项**：其中 `eth_phy_link_state`（弱判据）**已按父会话裁定（2026-09-17）
> 从 `case_link` 挪入 A 组** —— 它只读 PHY 状态、不断言 link 必须 up ⇒ 无对端也能确定判定，
> 能白拿一点覆盖且不影响全量回归稳定性。两个源集共享的 PHY 辅助函数已收敛到
> `src/eth_phy_common.h`（§6.1.9，static inline，不产生跨单元符号）。
> A 组单独构建、单独判定，**不依赖任何 B 组 case、不依赖网线状态**。
> 三个 B 组 case 在 `regression/cases.yaml` 中**一律带 `#` 屏蔽登记**（依赖 PC 对端，
> 不应影响全量回归稳定性）；何时取消屏蔽由父会话在 P6 决定。**本轮未改 `cases.yaml`。**

## 覆盖矩阵（case_mdio，套件 `eth_mdio`，5 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| `mdio_phy_id` | `mdio_dwmac_read` → `mdio_dwmac_transfer`（`MDIO_OP_C22_READ` 分支：`C45E=0` / `GOC_1=1` / `GOC_0=1`，写完地址轮询 `GB` 位，`ret==0` 后读 `MAC_MDIO_DATA.GD`） | 功能通路 | `PHYID1R == 0x001C`；`PHYID2R[15:8] == 0xC9`（Realtek OUI `0x001CC9`）；非全 0x0000 / 全 0xFFFF | 802.3 clause 22 寄存器定义 + RTL8211F 数据手册 + 驱动源码 `mdio_dwmac.c:131-198` | ⏳ |
| `mdio_reg_rw` | `mdio_dwmac_read`/`mdio_dwmac_write`（`is_write` 写分支先写 `MAC_MDIO_DATA` 再写 `MAC_MDIO_ADDRESS`；读分支同事务反向）+ `check_busy()` 重复读 | 功能通路 | ① 同一寄存器连续两次读值相等；② `BMCR.RESET(bit15)` 已自清 == 0；③ `ANAR` 写入 `orig^0x0020` 后回读 == 写入值；④ 恢复原值后回读 == 原值 | 802.3 clause 22（ANAR 16 位全 RW；写 ANAR 不重启自协商，仅写 BMCR 才重启）+ 驱动源码 `:200-208` | ⏳ |
| `mdio_invalid_args` | `mdio_dwmac_transfer` 的地址组装：`mdio_address_t.PA[4:0]` / `.RDA[4:0]` 位域赋值（**驱动无范围校验**） | 异常参数 / 边界长度 | phy addr 32 与 reg 32 均按 5 位**静默回绕到 0**：`mdio_read(dev, 32, 0)` == `mdio_read(dev, 0, 0)`；`mdio_read(dev, 0, 32)` == `mdio_read(dev, 0, 0)`；两者 `ret == 0` | C 位域截断语义 + 驱动源码 `mdio_dwmac.c:72-91, 144-159` | ⏳ |
| `eth_dev_ready` | `ETH_NET_DEVICE_DT_INST_DEFINE` 产物（`eth_dwmac_linkedsemi.c:133-166`）+ `dwmac_probe` 初始化 + ethernet L2 绑定 | 功能通路 | `device_is_ready(eth1)==true`；`net_if_lookup_by_dev(eth1) != NULL`；`net_if_get_device()==eth1`；`net_if_get_mtu()==1500`；link addr `len==6`；MAC 非全 0/全 0xFF 且首字节组播位 == 0 | Zephyr net_if / ethernet API 契约（`net_if.h:1237/1296`、`ethernet.h:50,116`）+ 驱动 DT 绑定 | ⏳ |
| `eth_phy_link_state` | `phy_rt_rtl8211f_get_link`（加锁 → 读 `PHYSR(0x1A)` → 解 `LINKSTATUS(bit2)` / `LINKDUPLEX(bit3)` / `LINKSPEED(bit4-5)`；`!is_up` 提前 `goto result` 分支）+ `phy_rt_rtl8211f_read` → `mdio_read` | 功能通路（**弱判据**） | `device_is_ready(eth1_phy)==true`；`phy_get_link_state() == 0`；INFO 上报 `link=up\|down`、`speed_mbps`、`duplex`、`speed_code`；**若** `link==up` **则**速率码 ∈ {10/100/1000 Mbps} × {half, full} | `phy_realtek_rtl8211f.c:186-262` + 802.3 链路语义 | ⏳ |

> `eth_phy_link_state` 原在 `case_link`（套件 `eth_link`）内，**2026-09-17 按父会话裁定挪入 A 组**
> —— 它是**弱判据**：不断言链路必须 up，只验证「驱动读链路状态这条路径可用且解码合法」，
> 对端不存在时同样可确定判定 ⇒ 随 A 组默认启用不影响全量回归稳定性。

## 覆盖矩阵（case_phy_reset，套件 `eth_mdio`，5 项）

**与 `case_mdio` 完全相同的 5 个测试项与断言**（链接序也相同），差别**只在 overlay**：
`tests/case_phy_reset/target.overlay` `#include` 了 `cpu1_eth_test/app.overlay`，并给 `&mdio1` 补
`reset-hold-ms = <10>` / `circuits-set-ms = <30>` —— 取值来源：驱动 `mdio_dwmac.c:242`
自述 "the minimum time specified by datasheet"（10 ms）与 `:257` 自述 "another 30 ms
(circuits settling time)"。

| 目的 | 覆盖驱动的哪个分支 |
|---|---|
| 让 `mdio_dwmac_ethphy_reset()` 的**脉冲子块**在本板上电时真正执行 | `mdio_dwmac.c:231-244`（`gpio_pin_set_dt(reset, 0)` + `k_busy_wait(reset_hold_ms)`）与 `:252-259`（拉高后 `k_busy_wait(circuits_set_ms)`）。默认板级下两处门 `reset_hold_ms && circuits_set_ms` 恒假（两属性缺省 0）⇒ 复位脚从未被拉低 |

**判据**：与 `case_mdio` 相同的 5 项断言 —— 属**路径冒烟**：开启该分支后系统仍能启动、
MDIO/PHY 仍工作（若脉冲把 PHY 打挂或该分支有缺陷，`device_is_ready(mdio1)` / PHY ID / 链路状态会先失败）。
变体自识别：`eth_phy_link_state` 的 INFO 行多两个字段 `mdio_reset_hold_ms` / `mdio_circuits_set_ms`，
本 case 读数为 `10` / `30`，`case_mdio` 为 `0` / `0`（取 `DT_PROP_OR(DT_NODELABEL(mdio1), ...)`）。

> ⚠️ **判据边界（不夸大）**：复位脚**电平瞬态本身无法在固件内观测** —— 要确认"确实拉低过 10 ms"
> 需示波器/逻辑分析仪量 GPIOG pin1。本 case 能证明的是「该路径执行后系统与 PHY 仍正常」，
> 不能证明脉冲波形符合 RTL8211F 时序。该限制已同时登记为跳过项 #11 的说明。

## 覆盖矩阵（case_link，套件 `eth_link`，1 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| `eth_link_up` | `phy_rt_rtl8211f_get_link` 的链路建立路径（PHY 自协商完成 → `PHYSR.LINKSTATUS==1` → 解码 speed/duplex） | 功能通路 | **10000 ms** 轮询窗口内 `phy_get_link_state().is_up == true`（100 ms 采样间隔） | RTL8211F 自协商时序（秒级；对端省电模式/固定速率下可能 >5 s）+ 硬件事实 | ⏳ 外部: 板载 RJ45 ↔ PC 网口（网线），对端网口须上电并开启自协商 |

> 本 case 只保留**强判据** `eth_link_up`：明确依赖对端（外部路径），失败即真实链路问题
> （对端网口未激活 / 网线未插好 / 自协商失败），不是时序抖动。
> 窗口取 **10000 ms**（父会话裁定，2026-09-17，由 5000 ms 上调）：对端网卡处于省电模式
> 或被固定速率时自协商可能超过 5 s，防 flaky 优先。

## 覆盖矩阵（case_arp_icmp，套件 `eth_arp_icmp`，1 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| `eth_arp_icmp` | `net_config` 的 `setup_ipv4()`（`net_if_ipv4_addr_add`，`NET_ADDR_MANUAL`）→ `net_icmp_send_echo_request`（`icmp.c` 上下文注册 + `icmpv4.c` 报文构造）→ IPv4 输出路径触发 ARP 解析/缓存（`l2/ethernet/arp.c`）→ `icmpv4.c` 收包后回调 handler | 功能通路 + 统计性质 | ① 本机静态 IPv4 已绑定（`net_if_ipv4_addr_lookup` 非 NULL）；② 5 次尝试（每次 2000 ms）内**至少 1 个回包**；③ 命中回包的**源 IPv4 == 对端地址**（等价于 ARP 解析到了 PC）；④ INFO 上报 `sent`/`recv`/`loss_pct`/`rtt_us`/`rtt_min_us`/`rtt_max_us` | ICMPv4 Echo（RFC 792）+ ARP（RFC 826）+ 板级 dts/DT 绑定 + 约定地址（`CONFIG_NET_CONFIG_*`） | ⏳ 外部: 板载 RJ45 ↔ PC 网口（网线），PC 侧同网段 IP + 放行 ICMPv4 回显请求 |

> **为什么不断言"每次都应答"**：ARP 首包解析、ICMP 首包丢失是常态，全量应答断言必然 flaky。
> 判据取「至少 1 次成功」——**一次带正确源地址的成功请求/应答，已同时证明 ARP 解析与 ICMP 往返都通**；
> 丢包率/RTT 走 INFO 上报供人工观察，不做精确值断言。
> **ARP 是否解析成功**以「回包源地址 == 对端地址」为证据（本版本 Zephyr 无公开 ARP 表查询 API，无 `net/arp.h`）。

## 覆盖矩阵（case_udp_echo，套件 `eth_udp_echo`，1 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| `eth_udp_echo` | `eth_dwmac` 真实收发路径：`dwmac_send`（描述符环形队列 + `free_tx_descs` 信号量 + `abort` 回滚分支）→ `dwmac_isr` → `dwmac_receive` + `dwmac_rx_refill_thread`（`free_rx_descs` K_FOREVER 等待路径）→ zsock UDP（`zsock_sendto`/`zsock_poll`/`zsock_recvfrom`） | 功能通路 + 统计性质 | ① 本机静态 IPv4 已绑定；② 5 次尝试（每次 2000 ms）内**至少 1 个逐字节相符的回包**（长度 == 32 且 `memcmp` == 0）；③ INFO 上报 `sent`/`send_fail`/`rx_total`/`rx_bad`/`verified`/`rtt_*` | UDP（RFC 768）+ 驱动源码 `eth_dwmac.c:145-223, 262-340` + 约定端口 5001 | ⏳ 外部: 板载 RJ45 ↔ PC 网口（网线），PC 侧 UDP 回声程序（端口 5001） |

> **防假成功**：每次载荷带**序号**（`magic 'ET' + seq + 滚动字节`），回包必须与本次发出的 32 字节**逐字节相等**才计入成功
> ⇒ 上一轮迟到的回包会被 `memcmp` 拒绝（计入 `rx_bad`），不会造成假 PASS。
> **sendto 失败不硬判**：UDP 无连接，首包因 ARP 未解析返回 `EHOSTUNREACH` 属正常，只计入 `send_fail` 并继续重试。

## §6.5.1 七维扫描：驱动分支覆盖与处置

| 维度 | 分支/路径 | 处置 |
|---|---|---|
| 功能通路 | C22 读（`mdio_dwmac_read`） | ✅ `mdio_phy_id` |
| 功能通路 | C22 写（`mdio_dwmac_write`） | ✅ `mdio_reg_rw` |
| 功能通路 | `eth_dwmac` 设备/`net_if` 契约 | ✅ `eth_dev_ready` |
| 功能通路 | PHY 链路状态解码（`phy_rt_rtl8211f_get_link`） | ✅ `eth_phy_link_state` / `eth_link_up` |
| 边界长度 | MDIO 地址/寄存器索引 5 位字段截断（±1、越界） | ✅ `mdio_invalid_args`（32 → 0 回绕） |
| 边界长度 | 链路速率边界（10/100/1000、half/full） | ⚠️ 部分：`eth_phy_link_state` 只在 `link==up` 时断言速率码合法；**具体速率值取决于对端**，不做精确断言 |
| 异常参数 | `mdio_read/write` 传 NULL data 指针 | ❌ 跳过 —— 驱动 `mdio_dwmac_transfer` 无空指针校验，读分支 `*data = mdio_data.GD` 直接解引用 → 必崩（load/store access fault），不可测（见跳过项） |
| 异常参数 | 无 PHY 的地址（1..31）必须报错 | ❌ 跳过 —— DW MAC 的 MDIO 事务**不区分从设备是否响应**，`GB` 位照样清零并返回上一次数据 ⇒ 本平台不存在该判据（见跳过项） |
| 异常参数 | MDIO 事务超时（`-ETIMEDOUT` → `-EIO`，`mdio_dwmac.c:171-195`） | ❌ 跳过 —— 需制造 `GB` 位永不清零，平台无法注入 |
| 并发 | `mdio_sem` 信号量保护的并发访问（`k_sem_take K_FOREVER`） | ❌ 跳过 —— 判据只能靠"两线程同时读不出错"，而驱动持锁串行化后行为与单线程完全相同，**无可观测差异**（见跳过项） |
| 中断/时序 | PHY `INTB`（`gpiog 5`）中断路径 `phy_rt_rtl8211f_interrupt_handler` → link 变化回调 | ❌ 跳过 —— 需对端制造 link 变化事件（插拔网线），无法在单次固件运行内确定性触发 |
| 中断/时序 | MDIO 时钟分频表选择（`mdio_dwmac.c:332-338`，8 档按 `clock_frequency` 落档） | ❌ 跳过 —— `clock_frequency` 是 DT 属性（`PBUS0123_CLOCK`），运行期不可变；本工程不改 DT |
| 复位/掉电 | `mdio_dwmac_init` 的时钟 on/off + `reset_line_toggle(ETH1_RESET)` + pinctrl 应用 | ⚠️ 间接：`eth_dev_ready` / `mdio_phy_id` 能通过即证明 init 路径成功（失败则 `device_is_ready` 报假、MDIO 事务超时），无独立用例 |
| 复位/掉电 | `mdio_dwmac_ethphy_reset` 的**入口段**：`:225` `reset_gpios.port` 门 → `:227` `gpio_pin_configure_dt(OUTPUT_ACTIVE)` → `:247` `gpio_pin_set_dt(1)` | ✅ **可达，且每次上电必执行**。`reset_gpios` 取自 **mdio1 的直接子节点**中带 `reset-gpios` 的那个（`mdio_dwmac.c:364` `CHILD_RESET_GPIOS` + `:383` `DT_INST_FOREACH_CHILD_STATUS_OKAY`）：mdio1 的子节点是 `eth1`（**无**该属性，宏展开为空）与 **`eth1_phy`（板级 dts 给了 `reset-gpios`，被采用）**。实证 = 生成 dts 中 `eth1_phy` 带 `reset-gpios = < &gpiog 0x1 0x0 >`（`case_mdio/build/target/zephyr/zephyr.dts:2089`）。⚠️ **原判断"取自 `eth1` / 编译期 `port==NULL` / 函数体整段不执行"是错的，已更正** | `mdio_phy_id` / `eth_dev_ready` 通过即间接证明入口段执行成功 |
| 复位/掉电 | `mdio_dwmac_ethphy_reset` 的**脉冲子块**：`:231-244`（拉低 + `k_busy_wait(reset_hold_ms)`）、`:252-259`（拉高后 `k_busy_wait(circuits_set_ms)`） | ✅ `case_phy_reset`（overlay 变体）。**可达，但默认板级不发脉冲** —— 两处门均为 `reset_hold_ms && circuits_set_ms`，而 `reset-hold-ms` / `circuits-set-ms` 是 **mdio1 自己的属性**（`:381-382` `DT_INST_PROP_OR(inst, ...)`；binding `zephyr/dts/bindings/mdio/snps,dwmac-mdio.yaml:12,18` 已声明，缺省 0）⇒ 门恒假、复位脚从未被拉低。工程 overlay 补属性后即执行 | 判据 = 与 A 组相同的 5 项（路径冒烟）；脉冲电平波形本身需示波器，见该 case 说明 |
| 功能通路 | C45（clause 45）读/写（`mdio_read_c45` / `mdio_write_c45`） | ❌ 跳过 —— 板载 RTL8211F 是纯 C22 器件，C45 事务无有效响应数据 ⇒ 无确定性数据判据（见跳过项） |
| 功能通路 | `eth_dwmac` 的 `dwmac_send` / `dwmac_isr` / `dwmac_receive` / `dwmac_rx_refill_thread`（真实收发路径 + 描述符环 + TX/RX 信号量） | ✅ `eth_udp_echo`（逐字节回包校验）；ICMP 方向另由 `eth_arp_icmp` 覆盖 |
| 功能通路 | IPv4 ARP 解析/缓存（`l2/ethernet/arp.c`） | ✅ `eth_arp_icmp` 判据③（回包源地址 == 对端 ⇔ ARP 解析到 PC） |
| 功能通路 | ICMPv4 echo 往返（`icmp.c` 上下文注册 + `icmpv4.c` 报文构造/分发） | ✅ `eth_arp_icmp` |
| 功能通路 | 静态 IPv4 地址写入（`net_config` 的 `setup_ipv4`，`NET_ADDR_MANUAL`） | ✅ `eth_arp_icmp` / `eth_udp_echo` 的判据① |
| 统计性质 | 网络数据面丢包率 / RTT | ⚠️ 只做 `ZT_INFO` 上报，**不设死门限**（防 flaky） |
| 边界长度 | UDP 载荷大小（含 > MTU 的 IP 分片） | ❌ 跳过 —— 见跳过项 14 |
| 并发 | 多 socket / 多线程并发收发 | ❌ 跳过 —— 见跳过项 15 |
| 功能通路 | `eth_dwmac` 的 `set_config` / `get_capabilities` / `get_phy` | ❌ 跳过 —— `set_config` 涉及多播/VLAN 过滤等会改变收发行为；后两者无对端时可断言的面很薄 |

## 跳过项清单（P3）

> 判据类型 = 该候选无法成项的**根本原因分类**。

| # | case 名（候选） | 归属组 | 判据类型 | 原因 |
|---|---|---|---|---|
| — | `eth_arp_icmp` | B | **本轮已补做** | 父会话裁定「B 组必须含真实 ARP/ICMP/UDP 用例」⇒ 已于本轮实现为 `case_arp_icmp`（见上文覆盖矩阵）。原跳过原因（PC 侧对端未确认）改由「PC 侧前置条件清单」在 P6 实测前落实 |
| — | `eth_udp_echo` | B | **本轮已补做** | 同上，已实现为 `case_udp_echo`（见上文覆盖矩阵）；对端 UDP 回声程序要求写入「PC 侧前置条件清单」 |
| 3 | DHCP 获取地址 / IPv6（ND、SLAAC） | B | **需外部条件 + 与本板用例目标不符** | 需 DHCP server；且本板 `zperf` 实测配置亦为 `CONFIG_NET_DHCPV4=n` 静态地址 ⇒ 不引入 |
| 4 | `mdio_read/write` 传 NULL `data` 指针 | A | **不可测（会崩）** | `mdio_dwmac_transfer` 读分支 `*data = mdio_data.GD`（`:187`）无空指针校验 ⇒ NULL 必触发 load/store access fault，SoC 异常入口 `while(1)` ⇒ 串口静默，且不产生可归因 FAIL。属驱动缺陷，**不写用例**（写进本表即为记录） |
| 5 | 无 PHY 地址（1..31）应返回错误 | A | **平台不存在该判据** | DW MAC 的 MDIO 事务不校验从设备响应：`GB` 位按帧结束清零，数据寄存器返回上一次内容 ⇒ "读到错误码"这一事件在本平台不发生。已在 `mdio_invalid_args` 中仅做 INFO 观测上报（不断言） |
| 6 | MDIO 事务超时（`-ETIMEDOUT` / `-EIO`） | A | **平台无法注入** | 需 `GB` 位永不清零；正常硬件下该位必然按时清零，无注入手段（承 `embedded-debug` 判据纪律：区分"判断不会发生"与"验证过没问题"） |
| 7 | C45 读/写（`mdio_read_c45` / `mdio_write_c45`） | A | **器件不支持，无确定性判据** | RTL8211F 是纯 clause 22 PHY；C45 事务返回的数据不具确定性（无从设备驱动数据线），断言任何值都会 flaky |
| 8 | MDIO 并发访问（`mdio_sem`） | A | **无可观测差异** | 驱动用 `k_sem_take(K_FOREVER)` 把并发串行化，双线程并发与单线程串行行为完全一致；无差异则无判据（避免造"必然 PASS 的伪用例"） |
| 9 | PHY `INTB` 中断 / link-change 回调 | B | **需外部条件 + 不可确定性触发** | 需插拔网线或对端 link 变化；单次固件运行内无法确定性制造 |
| 10 | MDIO 时钟分频表落档（8 档） | A | **运行期不可变** | `clock-frequency` 为 DT 属性；运行期不可改。若需覆盖应另建 overlay 变体（本 Phase 不做） |
| — | `mdio_dwmac_ethphy_reset` 分支（原条目：**"分支不可达"，机理已更正**） | A | **本轮已补做（overlay 变体）** | **更正**：函数 `:225` 的门在本板为真、**每次上电都执行**（`reset_gpios` 取自 mdio1 的**直接子节点 `eth1_phy`**，见「七维扫描」入口段行；原写"取自 `eth1` / `port==NULL`"是错的）。真正不发脉冲的是 `:231` / `:252` 两个 `reset_hold_ms && circuits_set_ms` 门；`reset-hold-ms` / `circuits-set-ms` 是 **mdio1 自己的 DT 属性**且 binding 已声明 ⇒ 工程 overlay 可补 ⇒ 已建 `case_phy_reset` 覆盖。**残余不可测部分**：脉冲的**电平瞬态**在固件内无判据，需示波器/逻辑分析仪量 GPIOG pin1 |
| 12 | `eth_dev_ready` 中 `eth_get_config` 类 API 契约 | A | **API 不存在** | Zephyr 4.0 无 `eth_get_config`；等价契约已由 `net_if_lookup_by_dev` / `net_if_get_mtu` / `net_if_get_link_addr` / `net_if_get_device` 覆盖 |
| 13 | `phy_rt_rtl8211f_cfg_link`（速率强制/自协商配置） | B | **会改变 PHY 配置且需对端验证** | 配置结果只有在对端存在时才能从协商结果观测；且改 PHY 配置会破坏同 case 内 `eth_link_up` 的前置状态（用例间副作用），与"每个 case 独立"冲突 |
| 14 | UDP 大载荷 / IP 分片（载荷 > MTU） | B | **需对端支持重组 + flaky 面放大** | 需 PC 侧回声程序支持并允许 IP 分片重组；分片丢失会显著抬高丢包率，判据只能退化为"至少 1 次成功"，与 32 B 载荷用例的证明力无实质差别 ⇒ 本轮不纳入（可作 P6 后补的边界长度维度） |
| 15 | UDP 多 socket / 多线程并发收发 | B | **判据不可归因** | 并发下丢包无法区分"驱动 TX 描述符队列/信号量问题"与"对端回声程序处理不过来"；写"无丢包"断言必 flaky ⇒ 不做假断言 |
| 16 | ARP 表内容直接断言（对端 MAC 是否在表内） | B | **无公开 API** | Zephyr 4.0 已移除 `net/arp.h` 与 `net_arp_lookup()`，ARP 表无公开查询接口 ⇒ 以「回包源 IPv4 == 对端地址」等价覆盖（见 `case_arp_icmp` 判据 ③） |

## 外部路径登记（SPEC_REFERENCE §6.6）

| case / 测试项 | 外部连接路径 | 对端要求 |
|---|---|---|
| `case_mdio`（全部 5 项，含 `eth_phy_link_state`） | **无** —— 只与板载 RTL8211F PHY 通话 | 无（有对端时 `eth_phy_link_state` 的 INFO 会显示 `link=up`） |
| `case_phy_reset`（全部 5 项，同源集） | **无** —— 只与板载 RTL8211F PHY 通话（复位脚 GPIOG pin1 是板内网络，非外部接线） | 无 |
| `case_link` / `eth_link_up` | **外部: 板载 RJ45 ↔ PC 网口（网线）** | PC 网口须**上电且自协商开启**（速率 10/100/1000 自适应，本板 RGMII 已配 Tx/Rx delay） |
| `case_arp_icmp` / `eth_arp_icmp` | **外部: 板载 RJ45 ↔ PC 网口（网线）** | PC 网口上电激活 + **同网段静态 IPv4**（约定 PC `192.168.1.1/24`）+ **放行 ICMPv4 回显请求** |
| `case_udp_echo` / `eth_udp_echo` | **外部: 板载 RJ45 ↔ PC 网口（网线）** | PC 网口上电激活 + **同网段静态 IPv4** + **运行 UDP 回声程序**（约定端口 `5001`，原样回送） |

## B 组 PC 侧前置条件清单（最终版，供 P6 实测前准备）

> **地址为约定值**：板卡 `192.168.1.100/24`、PC `192.168.1.1/24`、UDP 回声端口 `5001`。
> **P6 可按实际网络调整** —— 板卡侧只改 `cpu1_eth_test/prj.conf` 的两行
> （`CONFIG_NET_CONFIG_MY_IPV4_ADDR` / `CONFIG_NET_CONFIG_PEER_IPV4_ADDR`），
> UDP 端口改 `src/eth_udp_echo_test.c` 的 `UDP_ECHO_PORT`（改完必须重编译）。
> 板卡与 PC 必须**同网段**，否则 ARP 解析不到 PC，`case_arp_icmp` / `case_udp_echo` 必 FAIL。

| # | 前置项 | 具体要求 | 用途 |
|---|---|---|---|
| 1 | 物理连接 | 板载 RJ45 ↔ PC 网口，网线两端插紧；PC 网口指示灯亮 | `eth_link_up` 及两个数据面 case 的公共前提 |
| 2 | PC 网口状态 | **上电且"已启用"**（Windows：`控制面板 → 网络连接` 中该网卡为"已启用"，非"已禁用"）；自协商开启（默认） | `eth_link_up`（PHY 只看 link 有无，与 IP 无关） |
| 3 | 速率/双工 | 无需手工固定；PHY 与 PC 自适应。若 PC 侧被固定为 1000M 全双工，板卡侧也能协商成功 | `eth_link_up` 的 INFO 上报 `speed_mbps` / `duplex` |
| 4 | PC 侧静态 IP | 该网卡配 `192.168.1.1`、掩码 `255.255.255.0`（**与板卡同网段**）；不要同时启用 DHCP 导致地址漂移 | `eth_arp_icmp`（ARP 解析目标）、`eth_udp_echo` |
| 5 | 防火墙放行 ICMP | Windows Defender 防火墙放行「文件和打印机共享 (回显请求 - ICMPv4-In)」，或临时关闭该网卡的入站拦截 | `eth_arp_icmp` 的 ping 回包 |
| 6 | UDP 回声程序 | PC 侧运行 UDP 回声服务，把收到的报文**原样送回源端口/源地址**，监听端口 `5001`。示例：<br>`socat -v UDP4-RECVFROM:5001,fork UDP4-SENDTO:<板卡IP>:5001` | `eth_udp_echo` 的逐字节回包校验 |
| 7 | 隔离性 | 测试期间该网卡不要接到会主动发 ARP/广播流量的网络；PC 上不要有其它程序往 `192.168.1.100` 发 UDP 5001（避免 `rx_bad` 干扰） | 结果可复现、判据可归因 |
| 8 | 同一网段无第二个 `192.168.1.1` | 该网段内不得有其它设备占用 `192.168.1.1`（否则 ARP 可能解析到别的 MAC） | `eth_arp_icmp` 判据③（回包源地址校验）的有效性 |

> 说明：板卡侧静态 IP 由 `CONFIG_NET_CONFIG_SETTINGS` + `CONFIG_NET_CONFIG_MY_IPV4_ADDR` 在开机时写入，
> 且 `CONFIG_NET_CONFIG_INIT_TIMEOUT=0`（不在启动阶段等链路，避免"网线未插"时开机阻塞吃掉 case 超时窗口）。
> A 组 `case_mdio` / `case_phy_reset`（各 5 项，含 `eth_phy_link_state`）**不需要任何 PC 侧前置**；
> `case_link` 只需第 1~3 项；`case_arp_icmp` 需 1~5、8；`case_udp_echo` 需 1~4、6、7。

## 驱动审核发现（qsh-driver-review，2026-09-17 记录；两处 P0 已于 2026-09-18 修复）

审核范围：被测三驱动。**Mutex/Semaphore 泄漏扫描全部通过**：
`mdio_dwmac_transfer`（`k_sem_take :161` → 唯一出口 `done: k_sem_give :191`）、
`phy_rt_rtl8211f_{get_link,cfg_link,clear_interrupt}`（lock/unlock 成对，错误路径均走 `goto done`/显式 unlock）、
`dwmac_send` 的 TX 描述符信号量（`abort:` 分支按 `d_idx` 差值逐个 `k_sem_give`，与取出数相等）。

| 级别 | 位置 | 问题 | 对本工程判定的影响 |
|---|---|---|---|
| **P0（已修 2026-09-18）** | `mdio_dwmac.c:274-276` | `mdio_dwmac_ethphy_reset(dev);` **返回值被丢弃**（§1.5 硬件控制类返回值丢弃）。⚠️ 该调用位于 `:274` 的 `if (dev_config->reset_gpios.port)` 内，**本板该门为真 ⇒ 每次上电必经此路**（见七维扫描入口段行），丢弃返回值的影响是 **100% 生效路径**，不是理论分支 | PHY 复位失败被静默忽略 → 后续 MDIO 读回垃圾值，`mdio_phy_id` 会报成"PHY ID 不符"这种**误导性归因**；P6 失败时须先手工确认 PHY 复位是否成功（`eth_mdio_test.c` 的 `mdio_phy_id` 已写归因顺序注释）。**修复**：接收返回值，非 0 则 `DEV_ERR` + `goto done`（复用既有出口） |
| **P0（已修 2026-09-18）** | `phy_realtek_rtl8211f.c:166-174`（及 `:156-162`） | `do { read(PHYID1R) } while (reg_val != 0x1C);` **无超时自旋** | PHY 不应答 → POST_KERNEL 永久挂死 → **串口 0 行**（不是可归因 FAIL）；P6 遇到"0 行"须优先怀疑此处，而非烧录/入口。**修复**：两处自旋均加 `k_timepoint` 超时（`PHY_RT_RTL8211F_RESET_TIMEOUT_MS = 1000`），超时 `LOG_ERR` + 返回 `-ETIMEDOUT` |
| **P1** | `mdio_dwmac.c:200-208` | `mdio_dwmac_read` 未校验 `data != NULL`，读分支直接 `*data =` | 不可测（见跳过项 #4） |
| **P1** | `eth_dwmac.c:800-832` | 无条件 `SHELL_CMD_ARG_REGISTER` + `shell_error` ⇒ `CONFIG_SHELL=n` 无法链接 | 已由 prj.conf 折中规避（见下方备注） |
| **P2** | `mdio_dwmac.c:190-197` | 超时后返回 `-EIO`，`ret` 的 `-ETIMEDOUT` 被覆盖（错误码信息丢失） | 失败 reason 无法区分"超时"与"总线错"，仅影响可诊断性 |
| **P2** | `mdio_dwmac.c:286, 308` | `clock_control_off/on` 返回值未检查 | 无直接影响 |

> 处置（2026-09-18 更新）：P0 两处已在 `zephyr/` 驱动内修复（本轮经父会话授权修改驱动）。
> `case_phy_reset` 是路径冒烟，**修后断言不变** —— 修复只在 PHY 复位失败 / PHY 不应答时改变行为
> （`mdio_dwmac_init` 返回非 0；自旋返回 `-ETIMEDOUT`），正常路径的 5 项断言与 INFO 上报不受影响。

## 备注：本工程的非默认配置

| 配置 | 原因 |
|---|---|
| `CONFIG_NET_TCP=y` | **编译必需**：`eth_dwmac.c:73-75` 的 `RX_REFILL_THREAD_PRIORITY` 取 `CONFIG_NET_TCP_WORKER_PRIO-1`；`NET_TCP=n` 时该符号未定义，驱动编不过 |
| `CONFIG_SHELL=y` + `CONFIG_SHELL_BACKEND_SERIAL=n` + `CONFIG_SHELL_BACKEND_DUMMY=y` | **链接必需**：`eth_dwmac.c` 文件末尾**无条件**注册 shell 命令（`SHELL_CMD_ARG_REGISTER` 在 `CONFIG_SHELL=n` 下不是空宏），handler 引用 `shell_fprintf_error` ⇒ `SHELL=n` 必然链接失败。折中：开 SHELL 取符号，同时**关掉串口 backend 只留 dummy**，shell 无传输通道，不产生提示符、不占用 uart1 RX，ztest 串口流不受污染 |
| `CONFIG_NET_SOCKETS=y`（B 组数据面追加） | `case_udp_echo` 用 `zsock_*` UDP socket 数据面 |
| `CONFIG_NET_CONFIG_SETTINGS=y` + `CONFIG_NET_CONFIG_MY_IPV4_ADDR` / `_NETMASK` / `PEER_IPV4_ADDR` + `CONFIG_NET_CONFIG_INIT_TIMEOUT=0`（B 组数据面追加） | `case_arp_icmp` / `case_udp_echo` 需要静态 IPv4 与对端地址；`INIT_TIMEOUT=0` 避免"网线未插"时开机在 `net_config_init` 里等链路（默认 30 s），吃掉 case 超时窗口。**地址是约定值，P6 按实际网络只改这两行** |
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
mdio_phy_id | id1 == 0x001C | spec:RTL8211F 数据手册 OUI 0x001CC9 高字节（归档键名为 id1/id2/oui）（与驱动实现无关，强预期；★无 INFO 通道 ⇒ NO_ACTUAL）
eth_arp_icmp | !none | !none:静态地址绑定 + 5 次尝试回包的合取，非单一标量
eth_udp_echo | !none | !none:同上（含 32B memcmp）
eth_link_up | !none | !none:10000ms 轮询窗口内链路 up，观测项
eth_phy_link_state | !none | !none:device_is_ready + phy_get_link_state + 两条 INFO 的合取
eth_dev_ready | !none | !none:device_is_ready/net_if 绑定关系的合取
mdio_reg_rw | !none | !none:四条子断言（连续读一致 / RESET 自清 / ANAR 回读 / 恢复）的合取
mdio_invalid_args | !none | !none:断言形式是"两次不同入参的读值相等"（位域回绕），非单值
```
