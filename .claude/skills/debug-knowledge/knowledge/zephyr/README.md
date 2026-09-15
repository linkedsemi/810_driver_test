# Zephyr/RTOS调试知识

Zephyr 驱动/boot/调度/shell/内存；主战场 810_driver_test（对应 qsh-* 实操 skill）。

## 收录条目

> 本域为**空框架**——内容由 `debug-knowledge` §写入 在遇到可复用的领域调试法时自动填充。
> 框架结构：正文条目 = 本目录下一个主题一个 .md（slug：`<现象>-<简述>.md`）；下表为索引。

| 主题 | 场景 | 一句话 |
|---|---|---|
| [ISR 内禁止 LOG/printk](isr-no-log.md) | 中断饿死/时序异常/偶发卡顿 | ISR 只记账，日志与打印移线程；deferred 同样影响时序段 |
| [ztest 链接序=字典序](ztest-link-order.md) | GDB 内存兜底结果错位（全 PASS 潜伏） | 用 `nm --numeric-sort` 地址序对齐 TEST_NAMES/glue idx，勿用 plain nm |
| [Mutex 泄漏致永久死锁](mutex-leak-deadlock.md) | 驱动首次传输正常、后续永久阻塞 | k_mutex_lock 后 return 跳过 unlock → goto out 统一出口 |
