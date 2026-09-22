# i3c_i2c_switch_test — case_switch_core 失败分析

| 项 | 值 |
|---|---|
| 工程 | i3c_i2c_switch_test |
| case | case_switch_core |
| 被测驱动 | `zephyr/drivers/i3c/i3c_linkedsemi.c`（控制器广播 CCC 恢复路径，KNOWN_ISSUES #51 修复点） |
| 报告 | [i3c_i2c_switch_test_report.md](../i3c_i2c_switch_test_case_switch_core/i3c_i2c_switch_test_report.md) |
| 结果 | FAIL (10/11) |
| 执行标记 | CTRL=FAILED, TGT=SUCCESSFUL |
| 内存兜底 | 有效（`mem_state=ok`、`mem_unavailable=false`、`evidence_gap=[]`） |

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| broadcast_nack_recovery | 异常/恢复 | 'N' 窗口内首帧广播 RSTACT whole 应被 NACK（`i3c_ccc_do_rstact_all` 返回 `r1 != 0`）；target 自动切回后 RSTACT peripheral（`r2==0`）与 RSTDAA（`r3==0`）应正常；帧末 `EVR BIT12(BUSY)==0`（合取，COVERAGE 第 26 行） | 首帧未被 NACK：`r1 == 0`（返回成功，即被 ACK 接受），触发 `rstact_whole_should_nack` 失败分支；其余两个子断言与 EVR BUSY 未见报错（原因为 last-wins，无后续覆盖） | 报告原文 `CTRL:rstact_whole_should_nack`；串口 token 原文 `I3C_I2C_TEST:CTRL:FAIL:broadcast_nack_recovery:rstact_whole_should_nack` | 报告：[report.md:27](../i3c_i2c_switch_test_case_switch_core/i3c_i2c_switch_test_report.md)、[report.json:112-119](../i3c_i2c_switch_test_case_switch_core/i3c_i2c_switch_test_report.json)；日志：`serial_CTRL.log:46`（token FAIL 行）、`serial_CTRL.log:59`（SUMMARY 带 reason）、`serial_TGT.log:48`（TGT 仅 START 无 END）；源码：`projects/i3c_i2c_switch_test/cpu0_i3c_controller/src/i3c_i2c_controller_test.c:569-571`（`r1 == 0` 即判失败）、`:577-591`（其余三条断言的位置）；**记账复测（2026-09-21，cpu2 通道）**：`ctrl_fail_reason` 与 `test_reasons[10]` **逐字节 = `rstact_whole_should_nack`** ⇒ 上表 reason 串完整、未被抹（O-29，详见「补充取证」第 3 条与「归因」第 4 条） |

补充取证（用于把"实际值"钉死）：
- 报告表格该行「实测(INFO)」列为 `-`，`来源` 列为 `mem`，故无串口 INFO 数值可引；实际值由上条 FAIL token 的 reason 串 + 断言源码共同确定（`ctrl_set_fail` 为 **last-wins** 覆写，见 `i3c_i2c_controller_test.c:100-108` ⇒ reason 停留在第 1 条断言，反证后端 `rstact_peripheral` / `rstdaa` / `busy_bit_not_cleared` 三条**没有**再触发）。
- 「10/11 PASS」由两核 SUMMARY 行给出：`serial_CTRL.log:60` `SUMMARY:END:10/11 PASS`、`serial_TGT.log:61` 同值。
- **`ctrl_fail_reason` 「被抹」复测：不可复现（O-23 后半，2026-09-21 结）** —— 拿到 cpu2 独立调试通道后（`target remote 127.0.0.1:1026`，**O-28**）实测：① 硬件写监视（`HWWP=3`）只抓到两个写者 —— `ctrl_set_fail→vsnprintk` 与 `tr_record→vsnprintf`，**都写入完整正确串**；② 真实 harness（`run_test.py --case case_switch_core`）跑完后读 cpu2，`ctrl_fail_reason` 与 `test_reasons[10]` **逐字节 = `rstact_whole_should_nack`**（`0x72 0x73 0x74 0x61 0x63 0x74 0x5f 0x77 …`），`test_results[10]=0x01=TR_FAIL`；③ 4 种不同配置（带/不带 1026 客户端、带断点/带写监视、干净 DS）**均无清零**（**O-29**）。⇒ 原"有确定性痕迹 / 写过又被部分清零"**改判为「观测口径可疑」** —— 最可能是按 O-28 的访问纪律读到了**未运行 / 陈旧**的 cpu2 RAM（cpu2 没跑过的会话里其 `.bss` 读作全 0 初值或上一轮残留），**不是真被谁清零**；"缺手段"的卡点就此消解。

## 归因

**B 类 —— 用例自身时序竞态（窗口错过），非被测驱动缺陷。**

依据（均为本轮从报告/日志/源码复核得到，非转述既有结论）：
1. 用例用**固定延时抢窗口**：`k_sleep(K_MSEC(6))`（`i3c_i2c_controller_test.c:566`）等 target 进入 I2C NACK 窗口；窗口由 `TGT_SWITCH_DELAY_MS=5` + `NACK_WINDOW_MS=2` 拼成（协议头 `tests/include/i3c_i2c_switch_test_protocol.h:72,76`），目标窗口约 6~8ms，且 target 侧还要经 dispatcher 线程调度（`i3c_i2c_target_test.c:446-455`），时延可变。作者注释自陈 `:561-565`「目标窗口：~6ms~8ms（dispatcher 开销可变）…若仍 FAIL（dispatcher 偏慢），可适当增大到 7ms」。
2. **失败只发生在首帧**：reason 串唯一且为第 1 条断言 ⇒ 若 target 卡在 I2C 态，第 2/3 条广播 CCC 同样会失败并覆写 reason，实测没有 ⇒ **控制器侧广播 CCC 恢复路径（#51 修复点）本身完好**，唯一失配的是"第一帧到达时 target 已回到 I3C 态"。
3. 断言被判定为 DUT 失败（`ctrl_failed` ⇒ CTRL 打 `PROJECT EXECUTION FAILED`），但失败量是**跨核调度时延**，不由驱动状态决定。
4. **（补，2026-09-21）原"记账矛盾"已收口，且与本 case 的 FAIL 判定正交**：O-23 后半"`ctrl_fail_reason` 被抹"在用上 cpu2 通道后**不可复现**（复测细节见上「补充取证」第 3 条）⇒ 由"有确定性痕迹"**改判为「观测口径可疑」**（最可能是读到了未运行/陈旧的 cpu2 RAM，成因见 O-28 的副作用与 O-29）。**此改判不改变本 case 的 FAIL 判定本身**：`broadcast_nack_recovery` 仍是固定 `k_sleep(6ms)` 抢窗的时序脆弱项，其 FAIL **真实且稳定复现**（依据 1~3；`KNOWN_ISSUES #98` 前半亦**不被推翻**）。

无法判定/仍缺的证据：target 侧在第一帧 RSTACT 到达时刻的**实际 pinmux 状态**（即"target 到底进没进 I2C 窗口"）本轮**未采集到** —— 无 target 侧时序 INFO 行，`serial_TGT.log` 中该项只有 `START`（`:48`）没有对应 END/PASS。要定死"窗口错过"而非"target 未切"，需补一条 target 切换时刻的时间戳日志。

## 备注

- 执行标记 CTRL=FAILED / TGT=SUCCESSFUL 与报告一致（两核各有一条 app 自打标记 + 一条 ztest `PROJECT EXECUTION` 标记）：CTRL `serial_CTRL.log:62` `PROJECT EXECUTION FAILED`；TGT `serial_TGT.log:63` `PROJECT EXECUTION SUCCESSFUL`。但 `serial_TGT.log:62` 另有 `I3C_I2C_TEST:TGT:DONE:FAIL`（TGT 自己算出 10/11），两标记同核并存的现象即 KNOWN_ISSUES **#98 前半**所述的"固件自打标记 + ztest `end_report()` 空跑恒 SUCCESSFUL"；报告该字段取 `PROJECT EXECUTION` 行，与本 case 实际 TGT 记账（FAIL）口径不同。
- 既有登记（引用，勿据此判驱动）：
  - `KNOWN_ISSUES.md` **#98 末段** —— 本项已被 `COVERAGE.md` 记为脆弱项（"固定 `k_sleep(6ms)` 抢窗口"），明确**"勿据此判驱动"**；并注明同期框架改造改变了日志/采集路径，时序敏感用例的 PASS→FAIL 不得直接记成驱动回归。**#98 后半（2026-09-21 改写）**：原"`ctrl_fail_reason` 被抹 / 有确定性痕迹"已改为**"不可复现 ⇒ 观测口径可疑"**（对应用上 O-28 的 cpu2 通道后 O-29 的复测）；前半（双 `PROJECT EXECUTION` 标记）与 `broadcast_nack_recovery` 的 FAIL **均不推翻**。
  - `KNOWN_ISSUES.md` **#51** —— `broadcast_nack_recovery` 是"广播 CCC NACK 后 controller 错误残留"修复的常驻回归项，改动该恢复路径必跑。
  - `OPEN_ITEMS.md` **O-23** —— 前半（双 `PROJECT EXECUTION` 标记）已定论；后半（`ctrl_fail_reason` 被抹）**2026-09-21 复测不可复现**（O-29，**建议结**，结项动作归用户）。其记账矛盾为独立调试题，**不卡全量回归**；全量口径只看 token（本 case token 三行齐全）。本轮日志侧同样只见完整 reason 串（`serial_CTRL.log:46,59`）。
  - `OPEN_ITEMS.md` **O-28** —— cpu2 调试通道 `target remote 127.0.0.1:1026`（本轮取证的**手段来源**；DS 启动日志会给 `CPUs(CPU0):…:1025` 与 `CPUs(CPU1):…:1026` 两条）。⚠ **凡碰过 1026 必须重启 DebugServer**，否则后续回归的 cpu2 侧全部不运行（正是"读到陈旧 RAM"的成因）；恢复手段 = `set {int}0x4002200c = 1` 释放 cpu2 后重启 DS。
  - `OPEN_ITEMS.md` **O-29** —— O-23 后半的复测结论（写监视只见两个写者且串完整；`ctrl_fail_reason` / `test_reasons[10]` 逐字节 = `rstact_whole_should_nack`；4 种配置无清零）。
  - `tmp/final/triage.md` **§4.1** —— 同一结论（B 类），本文件已按报告/日志/源码逐条复核，与之一致。
- 回归差异：上一次全量（`2026-09-17-3`）本项目为 PASS 11/11，本轮为该 case 的 PASS→FAIL（引自 triage §4.1；未在本次只读范围内另行核对旧报告）。
- 建议（本次未执行，仅记录）：复跑 3 次判 flaky；若稳定 FAIL，按作者注释把 6ms 调大或改为等 target 侧状态信号（去掉抢窗口）—— 属用例改动，非本轮范围。
