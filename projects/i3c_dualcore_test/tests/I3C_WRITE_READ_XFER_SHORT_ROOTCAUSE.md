# I3C write_read 16/17/32 读段差 1/1/15 字节根因与修复

> 定位闭环：2026-08-24。现象：`write_read_1/15` PASS，`write_read_16/17/32` FAIL
> （controller "read actual 0"——读不足，`num_xfer` 保持 0）。修复后 5/5 双核双 PASS。

## 现象

- controller 通过 `i3c_write_read_actual` 执行 [write 0xA5]+restart+read，
  target 响应。1/15 字节 PASS；16/17/32 字节 FAIL（"read actual 0"）。
- "read actual 0" 语义：transfer 返回 0（无错误）但 `msg[1].num_xfer=0`——
  `num_xfer` 只在读满 len 时由 SFNE（Status FIFO）分支更新；读不足时
  Status FIFO 无状态字 → SFNE 不触发 → `num_xfer` 保持初值 0。

## 根因（两条独立缺陷叠加）

### 缺陷 1：STOP 分支 FLUSHTB 被屏蔽 → TX FIFO 残留 + prefill 数据流错位
- 驱动 STOP 分支原实验性屏蔽了 `SDATACTRL.FLUSHTB`（怀疑导致 MATCHED/STOP
  中断丢失），FIFO 内容靠"TXCOUNT 续写"模型维持。
- 后果：上一笔传输的残留字节与新 prefill 拼接，数据流不再恒为 `buf[0..]`
  干净前缀；`i3c_target_tx_write` 的 `END0` 标记位置漂移（标在 `buf[len-1]`
  的绝对位置，但发送顺序被残留打乱）→ 读段提前终止，差 1 字节。
- 推演链（测试顺序 write_read_1 → 15 → 16 → 17 → 32）：
  - wr16：wr15 后残留 `buf[15]` + prefill 续写 → END0 落在数据流第 15 字节
    → 发 15 字节终止 → 收 15 差 1；
  - wr17：wr16 后残留 END0@buf[15] → END0 在数据流最后 → 发 16 收 16 差 1；
  - wr32：prefill 32 字节只装满 FIFO 16（剩余 16 靠 TXSEND 续装）。

### 缺陷 2：TXSEND 续装循环逐字节限制 → 32 字节读段只续 1 字节
- 实验性修改把续装条件限制为"FIFO 空（TXCOUNT==0）才续装 1 字节"。
- 实测 TXSEND 触发模式：FIFO 非空时也反复触发（29 次触发序列
  `13,12,10,...|14,12,10,8,6,4,2,0|...`，每组以剩 1 或发空结束）——
  即"FIFO 空电平"触发假设错误。
- 后果：FIFO 发空 → TXSEND → 续 1 字节 → FIFO=1 → 下次 TXSEND 时非空不续
  → 每轮只续 1 字节，32 字节读段只续 1 → 收 17 差 15。

## 关键证据（GDB 探针/寄存器现场）

- **FC 帧 SR 值逐位解析**（controller 侧 FC 分支探针）：
  - 帧 2 (wr15 读段) `0x02040010`：DIR=1, **ABT=0**, XDCNT=16（读满 15 ✓）
  - 帧 6 (wr16 读段) `0x02060010`：DIR=1, **ABT=1**, XDCNT=16（读 15 差 1）
  - 帧 10 (wr17 读段) `0x02060011`：DIR=1, **ABT=1**, XDCNT=17（读 16 差 1）
  - 帧 14 (wr1 读段) `0x02040002`：ABT=0, XDCNT=2（读满 1 ✓）
  - 帧 18 (wr32 读段) `0x02060012`：DIR=1, **ABT=1**, XDCNT=18（读 17 差 15）
  - **FAIL 帧读段 ABT=1（传输中止）→ 无状态字压入 Status FIFO（SFNE 不触发）
    + ERR 不触发 → FC 走 else（k_sem_give）→ transfer 返回成功但 num_xfer=0。**
- TXSEND 触发分布（target 侧探针）：29 次触发，TXCOUNT 序列
  `13,12,10,8,5,3,1 | 13,11,9,7,5,3,1 | 14,12,10,8,6,4,2,0 | 13,11,9,7,5,3,1`。

## 修复（i3c_linkedsemi.c，功能修复保留）

1. **修改 A**：STOP 分支恢复 `SDATACTRL |= I3C_SDATACTRL_FLUSHTB_MASK`
   —— STOP 时清空 TX FIFO，stop_cb 的 prefill 从 buf[0] 起写，数据流恒为
   buf 干净前缀，END0 位置正确。
   - 风险验证：曾怀疑 STOP 时 FLUSHTB 导致 MATCHED/STOP 中断丢失（'S'-stop
     实验场景），恢复后 5/5 测试全 PASS（大量 STOP/MATCHED 正常），未复现。
2. **修改 B**：TXSEND 续装循环重构——TXCOUNT==0 检查移出循环（仅入口查一次），
   循环内 `read_processed_cb` 续装到 FIFO 满（TXNOTFULL 自停）；
   `status==1`（END0 注入）后 break（防后续字节覆盖 END0 位置）；
   `status<0`（无数据）SINTCLR TXSEND + break。
3. **保留既有修复**：MATCHED/TXSEND/START 读分支 `TXCOUNT==0` 才调
   read_requested_cb（FIFO 有预填时不注入）；MATCHSS=0（restart 读请求
   中断可捕获）。
4. **target 固件**：prefill 三处保留（write_requested_cb 写匹配时 /
   write_received_cb default 分支 0xA5 字节 T-bit 前 3us 窗口 / stop_cb），
   均为 TXCOUNT 续写幂等；用户硬件规则："读请求前 FIFO 必须已有数据，
   否则从机直接触发错误结束通信"。

## 修复后推演验证（烧录实测 5/5 双 PASS，两次独立回归）

- wr1 读 buf[0] ✓；wr15 读 buf[0..14]（END0@14）✓；
- wr16 读 buf[0..15]（END0@15 最后）✓；
- wr17 读 buf[0..16]（续装 1 字节 buf[16]|END0）✓；
- wr32 读 buf[0..31]（续装 16 字节 buf[16..31]，buf[31]|END0 最后）✓。

## 通用教训（沉淀到驱动维护认知，不重复踩坑）

1. **TXSEND 触发模式不是"FIFO 空电平"**：FIFO 非空时也反复触发（每发 2-3
   字节一次），触发序列以 TXCOUNT=1（剩 1 不发完）或 0（发空=续装点）结束。
   续装逻辑必须以"写到 FIFO 满（TXNOTFULL 自停）"为目标，不能假设 TXSEND
   只在空时触发。
2. **FLUSHTB 的放置时机**：STOP 分支 FLUSHTB 是安全且必须的（传输已结束、
   总线已释放）；曾怀疑的中断丢失来自' S'-stop 场景（前置手动 flush + 预填），
   与 STOP 分支语义不同。屏蔽 FLUSHTB 换来的"TXCOUNT 续写"模型会被
   prefill/残留的拼接破坏 END0 位置，是隐性 bug 源。
3. **"read actual 0" 的判定**：transfer ret==0 不等于读满；读段 ABT（SR.ABT=1）
   时无状态字 → num_xfer 保持 0 → 上层误报"读 0 字节"。判读读段结果必须看
   FC 帧 SR 的 ABT/XDCNT，不能只看 ret。
