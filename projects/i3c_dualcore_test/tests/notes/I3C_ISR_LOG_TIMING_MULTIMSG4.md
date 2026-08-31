# multi_msg_4 12.5MHz 失败：ISR 内 LOG 时序破坏（定位闭环）

- 日期: 2026-08-26
- 状态: 已修复并双时钟源验证（50MHz/25MHz 各 210/210 双 PASS）

## 现象

`case_pure_i3c_12m5` 的 `multi_msg_4`（4 段 RESTART 链：
W(1B) + R8 + W(1B) + R8+STOP）在 12.5MHz SCL 下稳定失败：
controller 侧 `msg3 num_xfer 0 != 8`（读回 0 字节）。
6.25MHz 及 mixed case 同测试 PASS。

## 定位（二分实验）

唯一变量 = target ISR 的 STOP/ERRWARN 分支里是否调用 `ls_i3c_log_slave_serrwarn()`：

| 实验 | 结果 |
|---|---|
| 屏蔽该 LOG 调用 | multi_msg_4 PASS，30/30 全 PASS，SERRWARN 日志 0 条 |
| 恢复该 LOG 调用 | FAIL（msg3 num_xfer=0），SERRWARN 4 次（raw=0x08=TERM, state=2=RD） |

## 根因

ISR 内 `LOG_ERR`（即使 `CONFIG_LOG_MODE_DEFERRED=y`）仍有
**msg 分配 + 参数拷贝 + 入队** 开销（几十 µs 级）：
deferred 只保证"不打印不阻塞"，不保证"不耗时"。

12.5MHz SCL 字节周期 0.8µs，RESTART 链段间空隙仅 ~2µs：
ISR 滞留几十 µs → `write_requested_cb` 的 prefill 延迟到读段开始之后
→ TX FIFO 空 → controller 提前终止（TERM, raw=0x08）→ 读回 0 字节。

6.25MHz（2µs/字节）与有 STOP 间隙的测试有足够余量吸收该耗时，故 PASS——
间隙短是放大器，LOG 耗时是根因（屏蔽后同间隔即 PASS，已排除间隔因素）。

## 修复（i3c_linkedsemi.c）

ISR 零 LOG，SERRWARN 记账后延迟打印：

1. `struct ls_i3c_data` 新增 `dev` 反向指针、`atomic_t serrwarn_pending`、`struct k_work serrwarn_work`
2. STOP/ERRWARN 分支：`atomic_or(&data->serrwarn_pending, base->SERRWARN); k_work_submit(&data->serrwarn_work);`
   （k_work_submit ~100-300ns，与既有寄存器操作同量级，安全）
3. 新增 `ls_i3c_serrwarn_work_handler()`：系统工作队列线程（k_sys_work_q，无条件编译）消费并打印
4. `ls_i3c_log_slave_serrwarn()` 签名改为传 raw 值（寄存器 W1C 后值已失）

打印时机 = 事件后的线程上下文（测试间隙），格式与内容完全保留。

## 验证

- 50MHz 时钟源（dpll 0x2 + dts 50M）：7 case 210/210 双 PASS
- 25MHz 时钟源（hsi 0x1 + dts 25M）：7 case 210/210 双 PASS
- SERRWARN 日志保留：read_underrun_8_16 / read_underrun_16_32 / read_master_abort
  的预期 TERM 日志照常输出（raw=0x08, state=0, txcount=16）
- 日志分散打印于测试间隙，不再破坏总线时序

## 沉淀

- ISR 内任何 LOG 调用（含 deferred 模式）在 <2µs/字节 的总线上都会破坏时序；
  deferred 只消除"阻塞"，不消除"耗时"。高速率驱动 ISR 必须零 LOG，
  记账 + 线程/工作队列延迟打印。
- 回归中偶发 `*_failed: -5` / 无 token case = DebugServer 连接残留
  （gdb 被杀后 reset/load 无效），detach 清理后消失，与驱动无关。
