# I3C 传输路径错误处理与计数缺陷修复汇总（2026-08-24）

> 由 6 个新增主机临界测试（read_underrun/nack_bad_addr/back_to_back 等）暴露并驱动修复。
> 全部修复在 `zephyr/drivers/i3c/i3c_linkedsemi.c`。验证：6 case × 23 项双核双 PASS。
> 相关前篇：`I3C_CCC_SPURIOUS_EIO_NUM_XFER_UNINIT.md`（CCC num_xfer 栈垃圾根因）。

## 修复清单

### 1. CCC 路径固定 3ms 间隔（用户波形发现）

- **现象**：示波器上每条 CCC 之间固定 ~3ms 间隙。
- **根因**：`ls_i3c_do_ccc` 入口调 `ls_i3c_request_transfer_flag` →
  `k_sem_take(device_sync_sem, 3ms)`；该 sem 只在 FCF（帧完成）中断里 give，
  而 CCC 路径**不开 FC 中断**（开了反而会被 ISR 的 RXFNE 排空抢走 CCC 读数据），
  sem 永远没人给 → 每条 CCC 白等满 3ms。且调用处判断写错：
  `== ETIMEDOUT`（正值）vs 实际返回 `-ETIMEDOUT`，超时从未被检出，功能"碰巧正确"。
- **修复**：CCC 不再走 sem 握手（自含轮询路径，phase1/2 自己等 FCF/ERRF），
  入口只做 `msg_state = LS_I3C_MSG; LL_I3C_RequestTransfer(base);`。

### 2. device_sync_sem 错误帧双 give 残留（NACK 后连环假成功/卡死）

- **现象**：访问不存在地址后，后续正常传输交替出现"ret=0 但 actual=0"假成功与卡死。
- **根因**：错误帧时 ISR 的 ERR 分支和 FC 分支**都会** give device_sync_sem，
  take 只消费一个，残留计数使下一帧的 sem 等待立即返回（不等帧完成）。
- **修复**：①每帧开始前 `k_sem_reset`；②FC 分支只在 `msg_state != MSG_ERR`
  时 give（每帧至多 give 一次）。

### 3. FCF/ERRF 跨时钟域竞态 → NACK 漏检

- **现象**：地址 NACK 的传输有时返回 0（假成功），且时序相关（1MHz mixed 下
  ERRF 可能始终不可见）。
- **修复**：①ISR FC 分支 give 前复查 live ERRF；②`request_transfer_flag`
  等待返回后再查 **ERRF + SER（ANACK/DNACK/COVR/DOVR）**——SER 是错误详情
  寄存器，与 ERRF 独立同步，双通道兜底；③`ls_i3c_transfer` 对**写消息**
  增加完整性校验（`num_xfer != len` → -EIO，读消息允许短读——T-bit 提前
  终止是 I3C 合法行为）；④错误返回前 `ls_i3c_xfer_reset` 清残留 FIFO。

### 4. target 提前终止（T-bit）的读：num_xfer 被抹成 0 / 尾字节丢失

- **现象**：主机读超过 target 可供应量时，实际收到了字节但 actual=0（假空读），
  或尾字节丢失（要 16 得 12）。
- **根因**：①提前终止的消息不触发 `xfer_next`（没收满 len），num_xfer 只靠
  SFNE 中断里 `LL_I3C_GetXferDataCount`（读 **live SR.XDCNT**）落账，而 XDCNT
  跨 core(50MHz)/pbus(75MHz) 时钟域同步滞后，此刻常读到 0 → 覆盖成 0；
  ②FCF 置位时接收流水线最后几个字节尚未进 RX FIFO，ISR 内立即排空扑空。
- **修复**：①SFNE 落账改为 `max(xdcnt, cur_num_xfer)`（仅当状态项正是当前
  传输消息时）——软件实际从 RX FIFO 收进 buf 的字节数才是应用可用的 ground
  truth；②FC ISR 不再立即排空，帧末残余字节的排空与 num_xfer 落账移到
  `request_transfer_flag` 等待返回后（线程上下文）：驻留 5us 等同步 →
  RXFNE||RXCOUNT 有界排空（irq_lock 与 ISR 互斥）→ 在读消息落账。
  ③`ls_i3c_drain_rx_fifo` 加越界防护（超出 len 的字节丢弃不计数）。

### 4.5 isr_tx 给读消息填 TX FIFO（multi_msg_4 测试暴露，2026-08-25）

- **现象**：4 段复合帧（W+R8+W+R8）中，第二个写段发出了垃圾字节 0xAA
  （而非 0xA5），第一个读段数据整体错位。
- **根因**：`ls_i3c_event_isr_tx` 对当前 xfer 消息**不区分读写方向**。
  TXFNF（TX FIFO 未满）在读段期间恒置位，于是读段的 isr_tx 把**读缓冲区
  （未初始化）的内容**写进 TX FIFO：幻影字节在同帧下一个写段被先发出去；
  且 xfer_msg_idx 被 TX 路径误推进，导致该读的 RX 字节错位/丢失。
  2 段 write+read 测试未暴露是因为幻影字节只影响**同帧**的后续写段。
- **修复**：isr_tx 按 xfer 指针方向拦截读消息（i3c/i2c 两种 flags 都判）。

### 5. 审查发现的潜伏问题（顺手修复）

- `ls_i3c_event_isr_cf`：控制字发完后 CFNF 仍可能触发 → 越界读
  `i3c_msg_ctrl_ptr` 发幻影消息。加 `ctrl_msg_idx < num_msgs` 守卫。
- `ls_i3c_do_ccc` phase2 CFNFF 分支同理加 `num_target > 0` 守卫（防越界
  new_tgt 重发 + transfer_num 污染）。
- `out_ccc_stop` / `out_daa` 的帧收尾 `while` 死等无超时 → 加 4ms/有界超时。
- `ls_i3c_do_daa` 主循环无超时（无设备应答时永久挂死）→ 加 100ms 超时。
- `ls_i3c_transfer` 吞掉 `i3c_ls_curr_msg_init` 的返回值 → 失败时不再发起传输。

### 6. 已知残留（未闭环，记录在案）

- **ready IBI 偶发丢失**（慢速档约 1/6 轮，write_stop_read_32 位置最常见）：
  target 侧确实发出了握手 IBI（target 校验全过），主机侧 IBI 中断/workqueue
  环节偶发漏投。**测试框架层已加重发重试（最多 3 次）吸收**；驱动层根因
  （疑似 IBI workqueue 项忙时丢入队）未闭环，值得后续专项。
- 用户观测：`I3C_IBI_COMPLETE_TIMEOUT`（target 侧 ibi_raise 的 3ms 完成等待）
  疑似每次拉满——IBI 功能正常但完成信号路径可疑，与上一条可能同源。
- RXTHRES（CFGR bit10）运行时写 0 不生效（MASTER_EN 各种状态下实测均不行），
  word 阈值模式保持原厂值；短读残留靠 FCF 时硬件降阈值 + drain/补收窗口处理。
- RXCOUNT/SR.XDCNT 跨时钟域同步滞后：不可作热路径判据（CCC 热路径实测会
  全程读 0）；帧末线程上下文可用。
- `i3c_transfer` API 包装层（include/zephyr/drivers/i3c.h z_impl）先解引用
  target->bus 才进驱动：NULL desc 在包装层硬 fault（上游行为）；驱动入口
  已加 target/msgs NULL 检查做纵深防御（api_neg_paths 测试覆盖 msgs=NULL，
  不覆盖 desc=NULL——实测 Load access fault @mtval=4）。

## 通用教训

- **"功能一直是对的"可能是错误处理坏了**：CCC 的 3ms 等待里，`== ETIMEDOUT`
  符号错误让超时永远漏检——正值错误码与负值返回值的比较要写 `-ETIMEDOUT`
  或直接 `!= 0`/`ret < 0`。
- 中断 ISR 里"结束帧"的收尾动作（排空/落账）对跨时钟域滞后敏感：能移到
  等待线程上下文的就移过去，ISR 只做必须立即做的事。
- 信号量"give 可能多次、take 只一次"的模型必须有防残留设计（帧首 reset 或
  单 give 保证），否则错误帧的残留计数污染下一帧。
