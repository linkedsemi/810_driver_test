# i3c_dualcore_test 测试覆盖矩阵（CLAUDE.md §13）

被测驱动：`zephyr/drivers/i3c/i3c_linkedsemi.c`（controller，cpu2）+ `i3c_target_linkedsemi.c`（target，cpu1）
- **外部连接路径**（CLAUDE.md §14）：I3C5_SCL(PJ10)↔I3C6_SCL(PJ08)，
  I3C5_SDA(PJ11)↔I3C6_SDA(PJ09)，共地；两侧均配置上拉。
  跑测试前需按 §14.2 向用户报告本接线状态。
- 状态机：`ls_i3c_curr_msg_*`（808-971 行）+ `ls_i3c_event_isr_*`（TX/RX/CF，831-992 行）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| daa_discovery | ls_i3c_do_daa (1052)：GETACCCR/ENTDAA/地址分配 | 功能通路 | 发现成功，target 分配到动态地址 | 协议文档 | ✅ |
| write_8 | ls_i3c_transfer (1514) 写方向 + curr_msg 状态机 | 功能通路 | 8B 写入无 NACK（SERRWARN 无） | 协议文档 | ✅ |
| write_100 | ls_i3c_transfer 长写（>FIFO 缓冲） | 边界长度 | 100B 全部写入无 NACK | 协议文档 | ✅ |
| read_32_direct | ls_i3c_transfer 读方向 + event_isr_rx (861) | 功能通路 | 读回 32B == 预期 pattern | 协议文档 | ✅ |
| write_read_1 | ls_i3c_transfer 写+读组合（1B 最小粒度） | 功能通路 | 读回 1B == 写入值；target 侧另校验写段占位字节 0xA5 原样到达 | 协议文档 | ✅ |
| write_read_15 | ls_i3c_transfer 写+读组合（15B，FIFO 深度−1 边界） | 边界长度 | 读回 15B == 写入值；target 侧校验写段占位字节 0xA5 | 协议文档 | ✅ |
| write_read_16 | 写+读组合（16B 对齐） | 边界长度 | 读回 16B == 写入值；target 侧校验写段占位字节 0xA5 | 协议文档 | ✅ |
| write_read_17 | 写+读组合（17B 非字对齐） | 边界长度 | 读回 17B == 写入值 | 协议文档 | ✅ |
| write_read_32 | 写+读组合（32B = FIFO 阈值附近） | 边界长度 | 读回 32B == 写入值 | 协议文档 | ✅ |
| ibi | ls_i3c_ibi_enable (2155) + target ls_i3c_target_ibi_raise (2256) + event_isr | 中断/时序 | IBI 事件触发且数据 == pattern | 协议文档 | ✅ |
| hot_join | ls_i3c_do_daa 中 HJ 分支 + event_isr_cf (992) | 中断/时序 | HJ 被识别并处理，无 URUN | 协议文档 | ✅ |
| ccc_basic | ls_i3c_do_ccc (1207)：GETSTATUS/ID 等 CCC | 功能通路 | CCC 完成、响应状态 == 期望值 | 协议文档 | ✅ |
| write_stop_read_1 | ls_i3c_transfer 写+stop+读（stop 条件分支） | 边界长度 | 停后读回 1B == 写入值；target 侧校验写段占位字节 0xA5 | 协议文档 | ✅ |
| write_stop_read_15 | 写+stop+读（15B） | 边界长度 | 停后读回 15B == 写入值；target 侧校验写段占位字节 0xA5 | 协议文档 | ✅ |
| write_stop_read_16 | 写+stop+读（16B） | 边界长度 | 停后读回 16B == 写入值；target 侧校验写段占位字节 0xA5 | 协议文档 | ✅ |
| write_stop_read_17 | 写+stop+读（17B 非对齐） | 边界长度 | 停后读回 17B == 写入值 | 协议文档 | ✅ |
| write_stop_read_32 | 写+stop+读（32B） | 边界长度 | 停后读回 32B == 写入值 | 协议文档 | ✅ |
| read_underrun_8_16 | ls_i3c_transfer 读 + target T-bit 提前终止（ISR FC 落账路径） | 异常/提前终止 | 读 16 实际收 8，不挂死，恢复读 OK | 协议文档（T-bit 提前终止合法） | ✅ |
| read_underrun_16_32 | 同上，跨 target TX FIFO(16) 边界 | 异常/提前终止 | 读 32 实际收 16，不挂死，恢复读 OK | 协议文档 | ✅ |
| nack_bad_addr | ls_i3c_transfer 地址 NACK（ISR ERR/SER 检测 + 写完整性校验） | 异常参数/恢复 | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | 协议文档 | ✅ |
| back_to_back_8x8 | ls_i3c_transfer 零间隙 8 连发 + target 预填/续发 | 中断/时序 | 8 次全部成功、数据正确 | 压力/时序 | ✅ |
| big_256 | ls_i3c_transfer 256B 读写 + target read_processed_cb 续发 | 边界长度 | 256B 写入+读回 == pattern | 协议文档 | ✅ |
| ccc_rapid_8 | ls_i3c_do_ccc 连发（num_xfer 清零回归） | 压力/时序 | getpid x8 全部成功、PID 非零 | 回归（num_xfer 根因） | ✅ |
| read_master_abort | 主机主动截断读 + target TERM 恢复（STOP 后 FLUSHTB 重填对齐） | 异常/提前终止 | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | 协议文档 | ✅ |
| ibi_neg_paths | ls_i3c_ibi_raise fail-fast 三分支：payload>8/-EINVAL、IBIDIS/-EACCES、DAVALID HJ/-EACCES | 异常参数 | 三个返回值精确匹配 | 驱动契约 | ✅ |
| api_neg_paths | ls_i3c_transfer 入口：num_msgs=0/dynamic_addr=0/hdr_mode=1/msgs=NULL → -EINVAL | 异常参数 | 全部 -EINVAL，事后正常读 OK | 驱动契约 | ✅ |
| multi_msg_4 | ls_i3c_transfer 4 段复合帧（W+R+W+R）+ isr_tx 读消息拦截 | 功能通路/时序 | 各段 num_xfer 正确、两段读数据 == pattern | 协议文档 | ⏸ **已屏蔽**（2026-11-05：多次 restart 不在覆盖范围；`I3C_TEST_EN_MULTI_MSG_4=0`，只保留单次 restart 的 write_read_* 覆盖） |
| write_indexed_read_16 | ISR 顺序重构（MATCHED 先于 RXPEND）：write_received_cb 改 tx_offset/tx_len 后 restart 读按新参数预填（target_prefill_tx 偏移路径） | 功能通路/中断时序 | 3 索引（0x00/0x10/0x20）读回 tgt_tx_buf[idx..idx+15] 全部匹配 pattern | 驱动契约 | ✅ |
| write_dummy_read_16 | 写段 dummy 字节期间预填：controller 写 {CMD_INDEXED_READ, idx=0x10, 0xA5×N} 后 restart 读 16B；target 收到索引即按 idx 预填，N 个 dummy 字节走 default 分支幂等续填（不测 ns 阈值）。判定：N∈{0,8,16,32,64,128} 全部读满 16B 且 pattern 从 idx 开始 | 写段时长/预填 | 写段任意 dummy 数（0-128）均能预填；INFO dummy_max_ok=128 | 实测（2026-11-05，SCL 12.5MHz，50M DPLL 源） | ✅ |
| write_dummy_read_len | 写段 dummy 字节期间预填 + FIFO 深度续发：controller 写 {CMD_INDEXED_READ_LEN, idx, len, 0xA5×N} 后读 len 字节；len∈{16,17,32,64}、N∈{0,16,64}，读满且 pattern 正确（>16 由 TXSEND 续发） | 边界长度/中断时序 | 全部组合读满且 pattern 正确；INFO max_len_ok=64 | 实测（2026-11-05） | ✅ |
| read_underrun_stress | ls_i3c_transfer 连续短读压测（8 轮 target 备 8B / master 读 16B）+ 第 9 轮恢复读 | 异常/提前终止 + 并发 | 8 轮 actual==8 且 pattern 对；恢复读 ret==0/actual==16 | 协议文档（T-bit 提前终止合法）+ FC double-give 竞态回归 | ✅ |
| multi_msg_short_read | ls_i3c_transfer 4 段帧（W+R+W+R）末段短读 + 恢复读（整帧重试 ≤3 次吸收时序抖动） | 异常/提前终止 | 前三段 num_xfer 正确、rd1 pattern 对；恢复读 8B pattern 对 | 协议文档 | ✅ |
| short_read_then_nack | 4 轮 [短读 8B → 伪造地址 0x55 NACK → 恢复读] | 异常/恢复 | 每轮短读 actual==8、恢复读 ret==0/actual==8/pattern 对 | 协议文档（跨帧 ERR/RXTGTEND 组合恢复） | ✅ |
| setaasa | i3c_ccc_do_setaasa_all → ls_i3c_do_ccc 广播分支 + target 硬件 SETAASA 自动处理（SDYNADDR DADDR/DCAUSE=2）+ ls_i3c_transfer dynamic_addr==0 → -EINVAL 入口分支 | 功能通路 | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | NXP I3C slave programmers model（DYNADDR/DCAUSE 编码）+ I3C spec SETAASA 语义 | ✅ |
| setdasa | ctrl_ccc_direct_write1(I3C_CCC_SETDASA) → ls_i3c_do_ccc direct 写分支 + target 硬件 SETDASA 自动处理 | 功能通路 | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | 同上（SETDASA 直通到静态地址语义） | ✅ |

## 覆盖说明

- **项数**：本表对应 `I3C_TEST_COUNT = 35` 项（协议头 `i3c_dualcore_test_protocol.h`
  的 `enum i3c_test_id`；两核各注册 35 个 ZTEST 套件）。2026-09-18 审计发现原表
  只有 30 行，缺 `write_read_15` / `write_stop_read_15` / `read_underrun_stress` /
  `multi_msg_short_read` / `short_read_then_nack` 五行，已补齐；代码中"32 项"的
  过期注释同步改为 35。
- **外部**：本工程所有测试项都依赖 I3C5↔I3C6 跨核外部接线（§14），
  创建/执行前需向用户确认接线环境；接线记录见顶层 README.md「外设接线记录」。
- 预期数据（列 4）由固件写/读同一 pattern + 状态字校验（target 回读比对），
  INFO token 上报关键数值（如 read 实际长度），报告"实际"列取用（§13.4）。
- **target 侧判别力（2026-09-18 修复，F5 波）**：
  - 删除 5 处恒真断言 `TGT_EXPECT(addressed, ...)`（`addressed` 只在
    `reset_test_state()` 无条件置 true ⇒ 永远成立），改为可证伪的观测点：
    `daa_discovery` 读本机 `SDYNADDR.DAVALID/DADDR`；`ccc_basic` /
    `ccc_rapid_8` / `nack_bad_addr` / `api_neg_paths` 断言 `write_len == 0`
    （CCC 帧 / 外来地址写 / 负路径调用**不得**污染私有写缓冲）。
  - `read_32_direct` / `multi_msg_4` / `read_master_abort` 三项原先裸依赖
    `reset_test_state()` 的 `last_read_len` 默认值 32（恒真）；现由 controller
    侧显式下发 `CMD_SET_READ_LEN(32)`，target 断言变为真实协议点。
  - 关键长度（1/15/16/17/256）的 TGT 项新增**写方向真实数据校验**
    （写段占位字节 0xA5 是否原样到达）与 **>FIFO 长度的续发字节证据**
    （`rd_cb_bytes`，仅 TEST_START 清零；len=17 需 ≥1，len=256 需 ≥240）。
  - **已知不可观测面（如实标注，非"已覆盖"）**：target 软件**无法**观测
    master 实际读了多少字节——`len ≤ 16` 时数据全部由 TX FIFO 预填供货
    （`i3c_target_tx_write` 直接写 SWDATAB，不经 `read_requested_cb` /
    `read_processed_cb`，见 `i3c_linkedsemi.c:2136/2211`），且 STOP 会复位
    回调计数。故 len=1/15/16 的读方向在 TGT 侧**只有协议点**（命令字节到达），
    读数据正确性仍只由 CTRL 侧 pattern 校验保证。未改的弱项（留待后续）：
    `write_read_32` / `write_stop_read_32` / `read_underrun_8_16` /
    `read_underrun_stress` / `multi_msg_short_read` / `short_read_then_nack`。
- 已知坑（历史）：target 只校验 `last_read_len` 而 controller 实际读 0 → 假 PASS
  （write_read_×4 场景），判定按 §6 双核双 PASS 规则兜底。
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
daa_discovery | !none | !none:协议文档断言（"发现成功、分配到动态地址"）无标量上报
write_8 | !none | !none:协议语义（无 NACK），非标量
write_100 | !none | !none:同上
read_32_direct | !none | !none:读回 32B == pattern，非标量
write_read_1 | !none | !none:读回值 == 写入值 + target 侧占位字节校验的合取
write_read_15 | !none | !none:同上
write_read_16 | !none | !none:同上
write_read_17 | !none | !none:同上
write_read_32 | !none | !none:同上
ibi | !none | !none:IBI 事件 + 数据 == pattern，非标量
hot_join | !none | !none:HJ 被识别且无 URUN，非标量
ccc_basic | !none | !none:响应状态 == 期望值，非标量
write_stop_read_1 | !none | !none:读回值 == 写入值的合取
write_stop_read_15 | !none | !none:同上
write_stop_read_16 | !none | !none:同上
write_stop_read_17 | !none | !none:同上
write_stop_read_32 | !none | !none:同上
read_underrun_8_16 | !none | !none:不挂死 + 恢复读 OK，非标量
read_underrun_16_32 | !none | !none:同上
nack_bad_addr | !none | !none:"读不得伪造数据"是禁止性质，非单值
back_to_back_8x8 | !none | !none:8 次全成功且数据正确，非标量
big_256 | !none | !none:256B 写读 == pattern，非标量
ccc_rapid_8 | !none | !none:8 次全成功且 PID 非零，非标量
read_master_abort | !none | !none:下一笔读从 pattern 起点对齐，非标量
ibi_neg_paths | !none | !none:"三个返回值精确匹配"，但键名未在 INFO 中固定 ⇒ 不硬凑
api_neg_paths | !none | !none:全部 -EINVAL + 事后正常读 OK 的合取
multi_msg_4 | !none | !none:num_xfer + 两段读数据的合取
write_indexed_read_16 | !none | !none:3 个索引的 pattern 匹配合取
write_dummy_read_16 | !none | !none:写段任意 dummy 数均读满 16B 且 pattern 正确，非标量
write_dummy_read_len | !none | !none:同上（len/N 组合的 pattern 校验）
read_underrun_stress | !none | !none:8 轮 actual==8 且 pattern 对的合取
multi_msg_short_read | !none | !none:前三段 num_xfer + 恢复读 pattern 的合取
short_read_then_nack | !none | !none:每轮 short read 与恢复读的合取
setaasa | !none | !none:NXP slave programmers model 断言链（NACK→成功→DAVALID），非单值
setdasa | !none | !none:同上
```
