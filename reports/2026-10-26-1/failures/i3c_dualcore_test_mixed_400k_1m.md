# i3c_dualcore_test_mixed_400k_1m 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | i3c_dualcore_test |
| case | case_mixed_400k_1m |
| 执行标记 | CTRL=CONFLICTING_MARKERS（head 残片 `PROJECT EXECUTION SUCCESSFUL`@serial_CTRL.log:2 + capture 轮 `FAILED`@:674）, TGT=None（i3c 设计：target test_main 不返回） |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [i3c_dualcore_test_report.md](../i3c_dualcore_test_case_mixed_400k_1m/i3c_dualcore_test_report.md) |
| 结果 | FAIL (34/35；失败 1) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| write_stop_read_1 | ❌ FAIL | token | 停后读回 1B == 写入值；target 侧校验写段占位字节 0xA5 | TGT 本地 SUMMARY `FAIL write_stop_read_1: write_len 0 != 1`（serial_TGT.log:353）；ztest ` FAIL - write_stop_read_1 in 0.021 seconds`（:580） | CTRL:WEST_TOPDIR/810_driver_test/projects/i3c_dualcore_test/cpu0_i3c_controller/src/i3c_controller_test.c: 201: ctrl_test_end: (ctrl_failed is true) | serial_TGT.log:353,:576-582；serial_CTRL.log:674（capture 轮 `PROJECT EXECUTION FAILED`）；该 case `msg size error`=0 |

## 归因

**归因：无法判定（偏数据面单次失败，与本次 mbox 通道修复无直接关系）**。依据：
- 失败在 **I3C 数据面**：target 侧 1 字节写后停，写段 `write_len` 读到 0（serial_TGT.log:353），不是 token 丢失（target 本地 ztest 行完整）。
- **不是 G-MBOX-CHANNEL-INDEX-SHIFT**：本 case 双核 `msg size error` = **0**；`rlog_channel` dts `mboxes` 首格=5 且为 `&mbox` 第 6 个子节点（controller/target 均 OK）；`channel: 4 ... mtu: 2` / `msg size: 127` = **0 行**。
- **同批其余 6 个 i3c_dualcore case 全 35/35 PASS**（含 400k 的 12m5/6m25 与 1m 的 mixed_1m_*），故非全局驱动/环境故障。
- 历史上该项在 2026-09-21-1 ~ 2026-10-12-2 五个会话均 PASS，2026-10-23-4（mbox 错位会话）与本轮 FAIL。
- **复跑定性（2026-10-26 11:30~11:32，同镜像 `--skip-build`，ELF md5 controller `bb7904eb4969c963874805d968092996` / target `34c767a4b6ff50d86ad93b12b8d86921` 三次一致）**：
  - 复跑 1：**PASS 35/35**，`write_stop_read_1` TGT `PASS - write_stop_read_1 in 0.021 seconds`（`serial_TGT.log:587`），`msg size error`=0。
  - 复跑 2：**PASS 35/35**，TGT `PASS - write_stop_read_1`（`serial_TGT.log:566`），`msg size error`=0。
  - **结论：1/3 失败，判定为瞬态，未定性**（不是确定性缺陷；也不能写成"已修复"——首轮失败原因未复现）。首轮证据已备份至 `tmp/evidence_2026-10-26-1_mixed_400k_1m_run1/`。
  - 注意：`reports/2026-10-26-1/i3c_dualcore_test_case_mixed_400k_1m/` 现为复跑 2 的 PASS 结果（按任务要求原地覆盖），`summary.md` 该行已变 PASS；首轮 FAIL 证据以上述备份为准。

## 备注

归档完整性：`check_archive.py` 对该 case 报 `BAD`，但属**口径误报**（capture 轮完整：1 个 boot banner、`ZT_BOOT CTRL`、CTRL 34 START/34 PASS/1 FAIL、TGT 35 START/34 PASS/1 FAIL；H1/H2 的多余 `PROJECT EXECUTION` 来自 **prime 轮尾部泄漏到归档头部**（`serial_CTRL.log:2` 在 boot banner `:40` 之前），H3 的"末行非 SUCCESSFUL"是 app 的 `SUMMARY: 34/35` 打在完成标记之后；TGT "无 ZT_BOOT" 是 D2 前缀 + 时间戳前缀导致的锚点正则不匹配）。同类口径误报见 KNOWN_ISSUES #117。
