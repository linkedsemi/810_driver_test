# MCU/裸机调试知识

无 RTOS 的裸机/外设问题：GPIO/UART/时钟/中断向量/低功耗/总线外设（I2C/I3C/SPI）等。

## 收录条目

> 本域内容由 `debug-knowledge` §写入 在遇到可复用的领域调试法时自动填充。
> 框架结构：正文条目 = 本目录下一个主题一个 .md（slug：`<现象>-<简述>.md`）；下表为索引。

| 主题 | 场景 | 一句话 |
|---|---|---|
| [引脚复用双状态漏声明电气属性](pinmux-dual-state-electrical-residual.md) | 引脚复用动态切换后通信失效（寄存器"全对"） | 开漏残留 + 复位不覆盖该域；两状态都要显式声明完整电气属性 |
| [寄存器位域以实测为准](reg-bitwidth-write-readback.md) | 按头文件掩码设寄存器字段却不生效（无完成中断） | 头文件位宽可能宽于实机；**写-回读**定实际位宽（写 0xfff 回读 0x7ff → 只 11 位） |
| [总线对未解码/被拒访问静默 ACK](bus-error-response-not-injectable.md) | 想注入外设 ERR 分支（未映射地址 / IOPMP 拒绝规则），注入后均静默完成 | 总线不对未解码地址报错、IOPMP 被拒也回 OKAY + 无软件置位寄存器 ⇒ 错误路径不可动态触发；改**同族代理实验 + 不变量判据**（TFR 完成后 `dma_stop()` 返回 0 ⟺ `ChEn` 已清零）+ 静态审查 |
| [单点 ACK 不证明器件在链路上](addr-ack-not-device-evidence.md) | 某地址读 `ret=0` 就断定"器件在我以为的链路上" | 单点 ACK 只证明"**该地址有人应答**"（可能是同址邻居、别的链路或中间器件）；须再取**本级证据**（本级锁存寄存器 + 数据段内容）。⚠️ 原第二层"滤波器拦截也 ACK ⇒ 对照不充分"**已作废·待非撞址复验**（取自撞址观测） |
| [同址并存 ⇒ PASS/FAIL 不可归因](bus-addr-collision-pass-not-attributable.md) | 被测器件与板上既有器件**撞同一地址**；结果"大部分对、尾部错"，或 PASS 来路不明 | 地址是唯一寻址键，同址 = 并联应答/争总线 ⇒ 结果**不可归因**；判别法 = **挪到"改址前扫不到"的地址再复测** + 重扫核 `count/addrs` + 读 DUT 地址寄存器（target `OAR1`）；选地址须避开惯例段（I2C EEPROM `0x50`~`0x57`） |
| [打包寄存器的 setter 作用域](packed-register-setter-scope.md) | 调 HAL 改"第 N 个条目/字段"，相邻 3 条被连带改写/清零 | 一个寄存器打包 N 条目（固定 stride），`set(base, idx, val)` 写的是**整个寄存器** → 单条目改须用 `_by_idx`（读改写）；保存/恢复按整寄存器做 |
| [外设能力/归属/极性 的证据纪律](peripheral-capability-evidence.md) | 判"某实例归哪个驱动 / 有没有某复用 / 某 mask 属性什么极性" | 归属看 `compatible`；能力按关键字**全集**检索（`NSS\|CSN\|CS\|SS\|FSS\|片选`）；极性先看 binding 位表措辞（enable/disable），再对驱动表达式 |
| [配对控制器的引脚让渡](paired-controller-pin-ownership.md) | 把"配对控制器"挪作他用后总线毫无波形（不报错） | `pinctrl-noinit` / 配对控制器 `pinctrl-0` = 引脚归别人；本工程 overlay 显式解除（`/delete-property/`、`pinctrl-0 = <>`）。"配对者"DT 可证、"上游 master"只有接线能定 |
| [死配置宏 = 漏初始化烟雾枪](dead-config-macro-smoke-gun.md) | 一侧超时、另一侧返回 `0`（成功）的通信失败 | grep 配置宏只命中 `#define` ⇒ 配置对象全零；从设备返回 `0`（非有界超时）说明它**根本没在等**，不是等不到时钟 |
| [运行期改 GPIO 不必过 pinctrl](gpio-runtime-reconfigure-no-pinctrl.md) | 要反复切换 GPIO 方向/上下拉（扫描类用例） | 该驱动直接写寄存器、无 `pinctrl_apply_state` ⇒ 不必造上百个 pinctrl 状态；但它**抢不回**被外设复用占住的脚，且端口默认全 disabled 需显式使能 |
