# ISR 内禁止 LOG/printk（日志后端可能阻塞 → 中断饿死）

## 现象
偶发中断饿死 / RTOS 响应抖动 / 时间敏感外设行为异常；把 ISR 里的日志删掉即恢复。

## 根因
Zephyr log 后端可能阻塞：同步/UART shell 后端在 ISR 内打 `LOG_ERR`/`printk` 每条阻塞 ~ms 级
（实测 ~10ms/条），期间其他中断饿死 → 8ms 级死窗；`LOG_MODE_DEFERRED` 虽不阻塞 CPU，但
日志线程调度仍引入时序抖动，**时间敏感测量段同样应避免**。

## 判定
1. 代码审查：grep ISR 回调及其调用的函数里是否有 `LOG_INF/LOG_ERR/printk`；
2. A/B 验证：注释掉后行为/时序恢复 → 坐实。

## 处理
- ISR **只记账**（计数/时间戳/标志变量），由线程/测试结束时统一打印汇总行；
- 需要波形观察用 GPIO 标记，不打 LOG；调试完按探针纪律清理（embedded-debug §4）；
- 判定"ISR 内有日志"前，别忘检查被调函数（日志可能藏在 helper 里）。

## 关联
- 810_driver_test CLAUDE.md §6.1.2（ISR 禁 LOG）/ §6.1.3（时序测量）
- embedded-debug（方法学）；实例工程：i3c_dualcore_test
