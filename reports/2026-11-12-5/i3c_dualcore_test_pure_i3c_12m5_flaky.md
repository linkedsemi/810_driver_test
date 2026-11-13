# i3c_dualcore_test / case_pure_i3c_12m5 稳定性（flaky）分析

- 会话: `wsl_environment/reports/2026-11-12-5`
- 板级/环境: lsqsh_evb@2os，TGT=COM31，CTRL=COM14，timeout=180s（全程一致）
- 被测配置: pure I3C，SCL 12.5MHz（最高档），controller/target 双核
- 采样窗口: 2026-11-13 18:23 ~ 19:21

## 1. 结论（先行）

**判定：flaky（瞬态），不是驱动回归。** 单次 FAIL 后重跑即 PASS；随后 6 轮无条件复跑
**6/6 PASS 34/34**。有效采样 8 轮中失败 1 轮（**失效率 ~12.5%**），失败项在不同轮次间
**轮换**（早期轮为 `ibi_neg_paths`，本次为 `back_to_back_8x8` + `read_underrun_stress`），
无固定代码路径特征。会话归档按最后一轮 **PASS 34/34**（19:21，round6）记录，
flaky 属性由本报告留档。

## 2. 证据表

| # | 时间 | 结果 | 失败项 | 证据 |
|---|------|------|--------|------|
| 1 | 18:23（主队列） | **FAIL 32/34** | `back_to_back_8x8`、`read_underrun_stress` | `flaky_evidence/i3c_dualcore_test_case_pure_i3c_12m5/pre_round_fail_1823.md` |
| 2 | 18:58（重试第 1 次） | PASS 34/34 | — | `.../pre_round_retry_pass.log` |
| 3-8 | 19:01 ~ 19:21（round1~6） | PASS 34/34 ×6 | — | `.../round1/` ~ `.../round6/`（逐轮报告 + 双核 uart 日志） |
| 附 | 01:15（历史轮，环境类） | FAIL（TGT 全 34 项 MISSING） | 全部项（证据缺失） | `.../pre_round_fail_0115_log.txt` —— **不计入失效率** |

- 有效轮次 = 8（1 FAIL + 7 PASS），失效率 = 1/8 = **12.5%**；
- 每轮均同构建、同 timeout、同接线；轮间无 DS 重启/采集失败（那些情况按环境轮单列）。

## 3. 失效形态（两轮不同源，不得并案）

### 形态 A：单项瞬态数据错（18:23，计入 flaky）

- 套件 9.3s 内正常跑完、SUMMARY 正常收尾，无静默/停摆；
- 34 项中仅 2 项 FAIL，其余全 PASS：
  - `back_to_back_8x8`：TGT 摘要 `write_len 7 != 8` —— 8B 连发中丢 1 字节；
  - `read_underrun_stress`：TGT 摘要 `last_read_len 8 != 16 (recovery)` —— 欠载恢复读只收到 8B；
  - CTRL 侧两项均为收尾断言 `ctrl_test_end: (ctrl_failed is true)`（`i3c_controller_test.c:201`）；
- 该轮 TGT 摘要取自当时串口日志（`uart_TGT.log` 行 133/153），日志已随后续轮次覆盖，
  原文引用见本报告；报告表另存于 pre_round_fail_1823.md。

### 形态 B：证据缺失（01:15，环境类，不计入）

- TGT 核全部 34 项无确定结果（串口未捕到 + 内存兜底无结果），报告显式标注
  `证据缺失（非驱动失败）`；CTRL 侧随之整体 FAIL；
- 属 **采集/证据层**（与 KNOWN_ISSUES #87 的环境子模式同族），不是 I3C 传输错误，
  故单列、不混入 flaky 统计。

与 KNOWN_ISSUES **#87**（套件中后段停摆 / prime 零输出）不同：本轮套件完整收尾；
与 **#113**（mixed_1m_12m5 的 `multi_msg_4` 瞬态数据错）同族。

## 4. 为什么重跑会过（根因分析）

1. **速率处于最高档、时序裕量最小**：12.5MHz SCL（COVERAGE 记录：12.5MHz，50M DPLL 源）。
   `back_to_back_8x8`（无间隔连发）和 `read_underrun_stress`（16B 恢复读）都压在
   FIFO 补填/总线周转的边际上（16B 边界在 COVERAGE「已知限制」中已登记）。
2. **失败项轮换 ⇒ 非固定路径缺陷**：早期轮 `ibi_neg_paths`，本轮
   `back_to_back_8x8` + `read_underrun_stress`。若是驱动/协议错，失败项与 reason 应稳定复现。
3. **无持久状态损坏**：每轮 reset 后重新初始化，FIFO/控制器状态不跨轮累积；
   抖动（中断延迟、跨核调度、日志写入抢占）落在边际窗口之外时，传输全对 ⇒ 连续 6 轮 PASS。
4. **排除确定性因素**：同构建、同 timeout、同接线；无陈旧镜像（每轮重新构建/校验）；
   无 DS/采集异常（环境轮已单列）。

结论：这是 **12.5MHz 边际时序下的瞬态数据错**，重跑通过不是"自愈"，而是采样落在了
裕量窗口内；失效率 ~12.5% 与"边际 + 抖动"模型一致。

## 5. 判定与处置

- **本次会话**：`case_pure_i3c_12m5` 归档 PASS 34/34（round6），`summary.md` 已更新；
  flaky 属性以本报告为准，**不得**用单次 FAIL 记回归，也不得用单次 PASS 记"已修复"。
- **复发识别判据**：
  - 失败项**轮换**、各轮 reason 不同、套件正常收尾 ⇒ 瞬态（本形态），按多轮多数处理；
  - **同一项连续多轮同 reason FAIL** ⇒ 停止"重跑排除"，按真缺陷追（优先查 12.5M 时序裕量、
    16B FIFO 边界与信号完整性）。
- **门禁建议**：12.5MHz 档用于发布门禁时采用多轮多数/固定重试；要求 100% 确定性时
  降档 6.25MHz（`case_pure_i3c_6m25`）或检查接线信号质量。
- **复现方法（他人可照做）**：执行口径 `qsh-test-run` §4.4；报告规范 `qsh-test-compare` §4.2。
  证据目录结构：`<session>/flaky_evidence/<driver>_<case>/round<N>/`，
  本报告固定落在会话目录（与 `summary.md` 同级）。
