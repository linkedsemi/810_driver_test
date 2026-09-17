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
| [打包寄存器的 setter 作用域](packed-register-setter-scope.md) | 调 HAL 改"第 N 个条目/字段"，相邻 3 条被连带改写/清零 | 一个寄存器打包 N 条目（固定 stride），`set(base, idx, val)` 写的是**整个寄存器** → 单条目改须用 `_by_idx`（读改写）；保存/恢复按整寄存器做 |
