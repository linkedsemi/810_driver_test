# `no-bus-init` 开关上板验证（phase 2）

> 会话：`reports/2026-09-28-1/` · 工程 `i3c_i2c_switch_test` · 板 `lsqsh_evb@2os`
> 框架报告（自动生成，含逐项表）：`i3c_i2c_switch_test_all_cases/i3c_i2c_switch_test_report.md`
> 原始串口：`i3c_i2c_switch_test_case_no_bus_init/{serial_CTRL,serial_TGT}.log`、
> `i3c_i2c_switch_test_case_switch_core/{serial_CTRL,serial_TGT}.log`
> 本次 n=1（每个 case 单次采集）。A/B 是**同一次启动内**的对照，不跨轮次。

---

## 0. ★ 探针混淆：standing 判读规则（先读这条）

**若 ② 失败（`nbi_init_ret != 0` 或 `recover_*`），在把结论写成"驱动缺陷"之前，必须先排除 ① 的探针残留。**

- 做一次对照：**把 ① 的总线级探针段去掉重跑**（`#if 0` 包住 `probe` 那一段即可，**不改任何判据**），
  看 ② 是否转为通过。
- **两次结果都必须出现在报告里**，并明确写"② 失败是 / 不是探针引起的"。

理由：① 的探针会向无人地址发一笔读（本次实测返回 `-EIO`），存在"测量本身制造 ② 的失败"的可能。
把测量造成的现象当成被测对象的缺陷，是本项目栽过的同类跟头。

**本次是否触发：未触发 —— ② 通过（`nbi_init_ret=0`、`nbi_after_dyn=50`），故未做对照轮。**
本条作为 standing 规则保留：将来若 ② 转红，先按本条排除，再谈驱动。

---

## 1. 三个问题（结论先行）

### Q1 开关生效了吗？—— **生效**（证据等级：本 HW 实测）

`case_no_bus_init`（`&i3c5 { no-bus-init; };`）控制器侧 `i3c_baseline` 的 INFO 原文：

```
I3C_I2C_TEST:CTRL:INFO:i3c_baseline:num_i3c=1,nbi_neg_dyn=0,nbi_neg_xfer=-22,nbi_probe32_ret=-5
I3C_I2C_TEST:CTRL:INFO:i3c_baseline:nbi_probe32_act=0,nbi_init_ret=0,nbi_after_dyn=50,nbi_verdict=PASS
```

| 读数 | 值 | 含义 |
|---|---|---|
| `num_i3c` | `1` | 前提成立（守卫的 `num_i3c > 0` 为真）⇒ 本用例有判别力 |
| `nbi_neg_dyn` | `0` | ① 驱动 init 后 desc **无动态地址** ⇒ 总线**未被枚举** |
| `nbi_neg_xfer` | `-22`（`-EINVAL`） | ① 对无地址 desc 的 `i3c_transfer()` 被驱动**确定性拒绝**（`ls_i3c_transfer:1805`），不碰总线 |
| `nbi_probe32_ret` | `-5`（`-EIO`） | ① **总线级**旁证：寻址 `0x32` 无人应答（NACK） |
| `nbi_probe32_act` | `0` | ① 未伪造出任何数据 |

三条主判据（无地址 / 附着表空 / `-EINVAL`）与总线旁证（`-EIO` + 0 字节）**同向**，全部指向
"驱动 init 跳过了 `i3c_bus_init()`"。反向读法：**若开关未生效**，`nbi_neg_dyn` 会是 `50`（0x32），
断言立刻 FAIL（`nbi:neg_dynaddr_50`）—— 用例对该开关**有判别力**。

### Q2 显式调 `i3c_bus_init()` 能恢复吗？—— **能**（证据等级：本 HW 实测）

- `nbi_init_ret = 0`：应用侧 `i3c_bus_init(dev, dev_list)` 成功。
- `nbi_after_dyn = 50`：其后 desc 获得动态地址 `0x32`。
- 随后的正常 `'B'` + 16B pattern 写读 + `'E'` 走通：该 case 双核 `SUMMARY 11/11 PASS`、
  `TGT DONE:PASS`。

⇒ **opt-out 语义没有把人堵死**：跳过的是驱动代做的枚举，应用自己补做时**同一函数**完全可用。

### Q3 现有 i3c 用例有回归吗？—— **判定层面无回归**（证据等级：本 HW 实测）

见下一节（与新用例**分开**陈述）。

---

## 2. 两 case 分开陈述

### 2.1 `case_no_bus_init`（新用例）

- 结果：**PASS**，双核 **11/11**（`CTRL:SUMMARY:END:11/11 PASS`、`TGT:SUMMARY:END:11/11 PASS`、
  `TGT:DONE:PASS`），`EXECUTION: CTRL=SUCCESSFUL, TGT=SUCCESSFUL`。
- 新断言全部按预期成立（见 Q1/Q2 读数）。**未出现需要放宽判据的情形**。

### 2.2 `case_switch_core`（既有用例，本届未改其判据）

- 结果：**PASS**，双核 **11/11**，`EXECUTION` 双 `SUCCESSFUL`。
- **运行时惰性实测**：其 CTRL 串口日志里 `nbi_` 出现次数 = **0** —— 新代码在该 case **没有被执行**，
  新增的 A/B INFO 一行也没有（即报告内容与改动前同形）。这与编译期证据（该 case 的 ELF 里
  没有任何 A/B 串）互相印证。
- **该工程历史上判据调整过**（COVERAGE §「已知基线」B-1 / B-2 两条裁定），故**不把本次 PASS
  单独当成"该工程一切正常"**：本次只证明"**在 2026-09-28 这次采集上，11 项双核全 PASS、
  且新改动未进入其执行路径**"。B-1 那条"窗口内广播 NACK 不可达"的裁定与本次无关、未被触碰。

### 2.3 本轮未覆盖

- **只跑了 `i3c_i2c_switch_test` 这一个工程**；`i3c_dualcore_test`（7 case）**未跑** ——
  本次改动不涉及该工程（其 DT 无 `no-bus-init`，也不共用源文件），故未纳入。
- 未跑全量回归（`run_all.py`），未跑 `i3c_i2c_switch_test` 以外的任何工程。

---

## 3. 实测 / 推演分栏

| 项 | 等级 | 依据 |
|---|---|---|
| ① 三条主判据 + 总线旁证的实际读数 | **本 HW 实测** | 上面 INFO 原文（`serial_CTRL.log` 第 5–6 行） |
| ② 恢复的实际读数 | **本 HW 实测** | 同上 |
| 两 case 双核 11/11 / PASS | **本 HW 实测** | 归档串口 SUMMARY 行 + 框架判定 |
| `case_switch_core` 未执行新代码 | **本 HW 实测** | 其 CTRL 日志 `nbi_` 计数 = 0 |
| `-EINVAL` 来自 `dynamic_addr==0` 守卫、探针 NACK 为 `-EIO` | **源码推演 → 已被本次读数印证** | `i3c_linkedsemi.c:1805`；读数 `-22`/`-5` 与推演一致 |
| `case_switch_core` 目标码除调试行表外与改动前逐字节相同 | **推演（未逐字节比对）** | 源码 diff 为纯插入且全在 `#if DT_PROP(...)`（该 case 下 = 0）内 + ELF 无 A/B 串；**未**做改动前 ELF 的逐字节 diff |
| 回归的**长期**结论 | **不成立为结论** | n=1；本节只就这一次采集陈述 |

---

## 4. 复现命令

```bash
cd /d/zephyr4_0/810_driver_test/projects/i3c_i2c_switch_test/tests
python run_test.py --ctrl-uart COM18 --tgt-uart COM16     # 两 case 一次会话
```

串口角色按 VID:PID 动态识别（MCP2200 `04D8:00DD` = CTRL；CH342 `1A86:55D2` = TGT，
`find_tgt_uart()` 优先 Serial-B），本次实测与近 4 次会话的 `port_mapping.json` 一致（COM18/COM16）。

---

## 5. 本次遗留 / 注意

- **DebugServer 已按 PID 停掉**（我启动的 pid 28024，跑完后清理，1025 已释放）。再跑需重新启动。
- `dump_results.py` 内存兜底本次**未生效**（报告里 `mem-unavailable` / `来源=token`）——
  这是 2026-09-22 裁定的**预期形貌**（批量路径不自动 attach，见 KNOWN_ISSUES #114），**非故障**；
  且本 case 全部证据来自 token（`来源=token`），不依赖 mem。
- `i3c_baseline` 项**同时**承载既有 16B 写读与本次 A/B，两者的预期值记在
  `tests/COVERAGE.md` §「no-bus-init 开关验证」（未登记进 `expect` 块，理由见该节）。
