# i2c_pure_test 测试覆盖矩阵

**被测对象**：纯 I2C 链路（无 I3C 参与）400K 波形质量与基本写读通路。
i2c5（cpu2 master，PJ10/PJ11）↔ i2c6（cpu1 slave，PJ08/PJ09，地址 0x58），
引脚复用由 i2c5/i2c6 节点 pinctrl-0（board 宏默认生成）在 init 时自动应用。
驱动路径参照 `zephyr/drivers/i2c/i2c_ls.c`（i2c_ls_transfer 主机写/读主循环、
i2c 从机逐字节回调路径）。

**预期数据约定**（协议头 `tests/include/i2c_pure_test_protocol.h` 为单一事实源）：
- 写 pattern：`W <len> <0x00,0x01,...,0x0F>`（递增序列，slave 逐字节校验）
- 读 pattern：`TGT_READ_PATTERN_BASE(0xAA) + i`（slave 预填 `tgt_tx_buf[256]`）
- 收尾：master 发 `'P'`，slave 打印 SUMMARY + DONE

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| i2c_wave | i2c_ls_transfer 主机写（i2c_write 18B：'W'+len+16B）+ 主机读（i2c_read 16B）主循环；slave 侧 write_requested/write_received/read_requested/read_processed/stop 逐字节回调 | 功能通路 + 波形观察 | 100 轮 ×（写 16B 0x00..0x0F + 读 16B 0xAA..0xB9）全部匹配；轮间 100ms 静默（示波器单次触发抓拍）；master INFO 上报 rounds=100,delay_ms=100,rate_khz=400；slave INFO 上报 writes/bytes/reads 计数 | 协议约定（pattern 定义，协议头） | 待验证 |

> **slave 侧判据（2026-09-18 修复，F5 波）**：原实现只断言
> `tgt_write_seen`（见过至少一笔写）+ `!tgt_data_mismatch`（无错字节），
> 丢 99/100 轮仍 PASS。现新增（依据源码常量 `I2C_WAVE_ROUNDS=100` /
> `I2C_WAVE_LEN=16`）：
> - `zassert_equal(tgt_write_cnt, 100)` —— 100 轮写帧一帧不少；
> - `zassert_equal(tgt_write_bytes, 1600)` —— 数据字节总数一致；
> - 等 `'P'` 的循环加 30s 超时（原为无限等，master 崩溃时永久挂起，
>   只能靠 harness case 超时强杀、无法归因）。
> 未改：slave 侧读方向仍不校验（读内容由 master 侧逐字节 pattern 校验，
> `tgt_read_cnt` 仅 INFO 上报）—— slave 的读回调逐字节供数由 i2c 从机
> 驱动保证，重复断言判别力有限，且 master 侧已覆盖。

> 维度对照 §13.1：功能通路（主机写读主循环 + 从机逐字节回调全覆盖）。
> 外部路径（§14）：I2C5↔I2C6 跨核对连，接线见顶层 README「外设接线记录」。
> 波形质量（占空比/上升沿/振铃）由示波器人工判定，固件只保证总线持续有
> 400K 突发波形可供抓拍。
>
> **实测耗时（2026-09-19）**：`i2c_wave` 单轮 **102.76 ms**（100ms 延迟 + ~2.8ms 传输）⇒
> 整轮 **10.276 s**，与源码注释 `~10s` / slave 超时 `≈10.1s` **一致**（原"远慢于 10.1s"结论已撤回，
> 那是采集窗口含 gdb `load` 的测量假象；`KNOWN_ISSUES.md` #99）。
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
i2c_wave | !none | !none:断言是 100 轮写读 pattern 的合取（示波器单次触发抓拍用），无单一标量
```
