# I3C CCC 偶发 -EIO 根因：num_xfer 未初始化（栈垃圾计数器）

> 定位闭环日期：2026-08-24。涉及文件：`zephyr/drivers/i3c/i3c_linkedsemi.c`（ls_i3c_do_ccc）。

## 现象

- ccc_basic 测试偶发 FAIL：getpid/getbcr 返回 -EIO（-5），日志为
  `CCC[0x8d/0x8e] target 0x32 amount mismatch, remaining N` 或
  `received unexpected data`。
- 时好时坏，加调试 printk 后失败点会"漂移"（getpid 变好、getbcr/ENEC 反而开始失败），
  极具迷惑性。
- 示波器确认 target 侧波形正常响应（CCC 由 target 硬件直接回复，与软件无关）。

## 根因（三层叠加）

1. **主因——num_xfer 栈垃圾计数器**：
   `i3c_ccc.c` 各 helper（getpid/getbcr/getdcr/events_set...）的
   `struct i3c_ccc_target_payload ccc_tgt_payload` 是栈变量，只赋值
   addr/rnw/data/data_len，**从不初始化 num_xfer**（上游 Zephyr 历来如此，
   计数器清零责任在控制器驱动）。而 `ls_i3c_do_ccc` 把 `num_xfer` 当收/发
   计数器用却**从未清零**。
   - ccc_basic 中 getbcr 的 payload 栈槽继承前一次 getpid 的 num_xfer=6
     （实测探针值），DAA 时代甚至读到 0x10010004。
   - 后果：收数时 `num_xfer(6) >= data_len(1)` 被判"超出期望"，字节被丢弃，
     transfer_num 对不上 → amount mismatch -EIO。栈内容随代码/时序变化，
     所以表现为"偶发"，加 printk 改变栈布局后失败点漂移。

2. **次因——drain 循环分支条件写反（原厂 bug）**：
   `ls_i3c_do_ccc` 的 drain 循环（phase2 之后的残余字节收集）写成
   `if (num_xfer < data_len) → 报 unexpected data 错误` /
   `else → 继续读`，与正确逻辑完全相反：还缺字节时报错，已收满时反而
   越界多读（缓冲区越界写风险）。

3. **背景机制——RXTHRES=1（word 阈值模式）+ 标志滞后**：
   CFGR.RXTHRES=1（原厂设置，实测运行时写 0 不生效，保持原值）时
   RXFNEF 需 ≥4 字节才置位；短读残留字节（如 GETBCR 1 字节、GETPID 6 字节
   的最后 2 个）要靠帧结束时硬件降阈值置位 RXFNEF 后由 drain/补收窗口收走。
   另外 RXFNEF 标志相对 FIFO 实际深度有滞后（读空后瞬间仍可能置位），
   RXCOUNT（DATACTRL[28:24]）跨 core(50MHz)/pbus(75MHz) 时钟域同步，
   **热路径读到的值滞后不可用作判据**（实测 phase2 用 RXCOUNT 判定会
   全程读 0，6 字节全丢）。

## 关键证据

- 失败现场探针：getbcr `num_xfer=6, len=1`（继承 getpid 计数）/
  `num_xfer=0x10010004`（野垃圾）；`SR=0x2040001`（mid=2 两条控制字完成、
  dir=1 读、xdcnt=1 总线确实传了 1 字节）、`SER=0`（无总线错误）。
- i3c_ccc.c 全文 grep：`memset(&ccc_tgt_payload` 0 处命中。
- 修复后 num_xfer 清零，6 个 case × 17 项全部双核 PASS。

## 修复（全部在 ls_i3c_do_ccc，drivers/i3c/i3c_linkedsemi.c）

1. direct 分支入口：`for (i < num_target) payloads[i].num_xfer = 0;`（**主修复**）
2. drain 循环条件纠正：`num_xfer < data_len` → 收；收满后 RXCOUNT==0 →
   视为滞后标志退出；RXCOUNT>0 → 真残留，丢弃字节再报错（不再越界写）。
3. phase2 RX 分支加防越界：收满后多余字节读出即丢弃，不写缓冲区。
4. 保留 FCF 后补收窗口（~10us 驻留轮询）：FCF 先于最后字节落地的硬件
   流水线延迟是真实存在的，窗口内不能"连续 N 次空就提前退出"（两次寄存器
   读只隔几十 ns，远小于字节落地延迟）。

## 验证

- case_pure_i3c_12m5 连续 4 轮 17/17 双核双 PASS；
- 全量回归 6 个 case（mixed 1m/400k × 12.5m/6.25m + pure 12.5m/6.25m）
  全部 17/17 双 PASS、`I3C_TEST:DONE:PASS`。

## 通用教训

1. **"偶发 + 加 printk 就漂移" ≈ 栈/内存未初始化**，优先查计数器/长度字段的
   初始化链，而不是先怀疑硬件时序。
2. **轮询驱动里的调试 printk（115200 下一条 ≈ 数 ms）本身就是时序炸弹**：
   会掩盖竞态（本次 getbcr 在带探针固件上反而 PASS），也会制造新失败
   （探针阻塞 >1ms 超时窗口 → phase1 假超时 + COVR）。探针只放在失败路径
   上（帧已完成后读寄存器打印），热路径用记账变量。
3. 跨时钟域寄存器（本芯片 i3c core 50MHz / pbus 75MHz）：计数值
   （RXCOUNT/SR.XDCNT）同步滞后不可作热路径判据；标志类（RXFNEF）可作
   热路径触发，但判"真空"要用计数交叉验证。
4. CFGR.RXTHRES 运行时写 0 不生效（MASTER_EN 各种状态下实测均不生效），
   不要试图运行时改阈值；残留字节靠 FCF 时硬件降阈值 + drain/补收窗口处理。
