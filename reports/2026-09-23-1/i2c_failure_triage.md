# i2c 驱动回归失败排查（i2cf_test / i3c_i2c_switch_test）

- 日期：2026-09-23
- 排查对象：`i2cf_test case_default`（下称 **A**）、`i3c_i2c_switch_test case_switch_core`（下称 **B**）
- 排查范围：`reports/2026-09-21-1`、`reports/2026-09-22-1`、`reports/2026-09-23-1` 三份会话
- 取证方式：**未上板**。全部来自 ① 原始串口归档 + 报告 JSON、② 固件/驱动源码、③ `git log/-S` 改动窗口
- 参照基线：`i2c_pure_test case_pure_i2c_400k` 三份会话均 PASS 1/1（与本两案无共同代码路径，仅作"采集链路本身健康"的旁证）

---

## 0. 结论速览

| 项 | 三次会话结果 | 失败项 | 归因分类 | 是驱动缺陷吗 | 下一步 |
|---|---|---|---|---|---|
| **A** | 14/18 → 17/18 → 17/18 | `filter_blocks_nonwhitelist` | **未定**：① 用例对"关滤波=不转发"的假设 ② 拦截后滤波器残留态（需显式恢复） | **待定** | 一次上板判别实验（§3.4），判完才好定"改用例 or 改驱动" |
| **B** | 10/11 → 10/11 → 10/11 | `broadcast_nack_recovery` | **用例自身时序脆弱**：首帧 RSTACT 用固定 `k_sleep(6ms)` 抢 target 的 ~6~8ms NACK 窗口 | **否** | 复跑取众数；把 6ms 调大或改成等 target 状态信号（§4.4） |

> 两者都**不是**"批量判定把 PASS 判成 FAIL"——见 §1 的更正 3/4、§5.2。

---

## 1. 复核：与任务书所给事实的出入（逐条更正）

任务书声明这批观测"均为假设，须自行复核"。复核结果如下（其余核对无误）：

1. **A 的失败行号不对**。任务书称三次都是 `i2cf_test.c:916: (ret_free is non-zero)`。
   实际：删 `:876` 那次 —— **`2026-09-21-1` 失败于 `:876`（`ret_blocked < 0 is false`，第 1 步"被拦"）**；
   `:916`（`ret_free is non-zero`，第 2 步"自证"）只出现在 **09-22-1 / 09-23-1**。
   两处的物理含义完全不同：前者是"没拦住"，后者是"关掉后没恢复"。**不能混为一谈**。
2. **`2026-09-21-1` 不是"唯一失败项"**。该轮 4 项 FAIL（14/18）：
   `filter_blocks_nonwhitelist`(:876)、`filter_passthrough_whitelist`(:962)、
   `isr_addr_beyond_whitelist`(:806)、`master_eeprom_rw`(:626)。
   任务书"14/18 唯一失败项"只有 17/18 的两轮成立。
3. **B 不是"missing 导致的失败"**。三次会话的 CTRL 串口里**都有显式 FAIL token**：
   `I3C_I2C_TEST:CTRL:FAIL:broadcast_nack_recovery:rstact_whole_should_nack`
   （+ 同内容的 `SUMMARY:ITEM` 行）。报告里变的只是**证据来源列**：09-21 是 `mem`、09-22/09-23 是 `missing`；
   `ctrl` 字段三次都是 `FAIL`，`result` 三次都是 `FAIL`。⇒ **判定同源、结论未变**，
   `missing` 不是"采集链路把结果弄丢了"。
4. **`内存兜底: 不可用` 是按设计，不是采集故障**。`common/acquire.py:507-517` 明载 **2026-09-22 用户裁定**：
   "全量测试 gdb 只负责烧录……不应当自动去通过 gdb 获取结果"，批量三个取数点**一律写死 `mem = None`**。
   实测：09-21-1 有 0 个 case 标 `不可用`，09-22-1 / 09-23-1 各有 **29/50** 个 —— 是全体行为，不是 A/B 特有的异常。
   （另：该裁定同时说明"mem 取值在批量里本来就不算证据"，即取与不取判定中性。）
5. **会话目录名与报告时间戳不一致**（归档时注意）：`2026-09-22-1` 的报告时间戳是 `2026-09-22 17:48`，
   而 `2026-09-23-1` 的是 **`2026-09-22 22:56`** —— 目录名按会话分配日期，报告时间戳是实际执行时刻。

核对无误的部分：三份 summary 的计数（14/18→17/18→17/18、10/11 恒定）；A 的源码路径；
A 的 09-22/09-23 失败文本 `(ret_free is non-zero)`；B 的 `CTRL:rstact_whole_should_nack`；
`i2c_pure_test` 三次全 PASS。

---

## 2. 改动窗口（任务书要求的第 2 问）

`810_driver_test` 是独立 git 仓库，窗口内相关提交：

| 提交 | 时间 | 对本两案做了什么 |
|---|---|---|
| `43b232e` | 2026-09-21 11:12:53 | **给 `i3c_i2c_switch_test` 引入单出口（RLOG）**：新增 `common/rlog.{h,c}`、从核 token 改经 mbox 送主核、`ctrl_test_begin/end` 插 `rlog_relay_drain()`、`ctrl_test_end` 末尾插 `k_sleep(K_MSEC(2))`、收尾改 `rlog_wait_secondary_done(2000)` —— **直接压在 B 的时序路径上** |
| `d073ddf` | 2026-09-21 15:39 | 只改 `i3c_i2c_switch_test/tests/run_test.py`（判定脚本），不碰固件时序 |
| `a405816` | 2026-09-22 17:22:37 | **改 `i2cf_test` 配置**：片内 target 从 `0x50` 迁到 **`0x33`**（避开板上真实存在的 EEPROM 惯例地址）；overlay/src/prj.conf/test.sh/run_test.py |

对照会话：09-21-1 执行于 09-21 13:17（**在 `43b232e` 之后 2 小时**）、09-22-1 于 09-22 17:48、09-23-1 于 09-22 22:56。

**判定**：
- **A 的 14/18 → 17/18 不是驱动行为变化，是测试配置变化**（换址消除撞址）。见 §3.3。
- **B 是"PASS→FAIL"**：上次全量为 PASS 11/11（引自 `tmp/final/triage.md:119` 与 `KNOWN_ISSUES` #98 增补段；
  `reports/` 下只保留 09-21 起的会话，**本轮未能独立复核该旧报告**），且翻转点与 `43b232e` 精确重合。见 §4.3。

---

## 3. 失败项 A —— `i2cf_test` / `filter_blocks_nonwhitelist`

### 3.1 现象

该用例是两步判据：

| 步 | 动作 | 判据 | 09-21 | 09-22 / 09-23 |
|---|---|---|---|---|
| 1 | filter **ON**，白名单只授权另一地址（`0x2A`），上游读从设备 `0x33` | 必须被拦（返回值 < 0） | **FAIL**（`ret_blocked = 0`，没拦住，`:876`） | PASS（被拦，`-5`） |
| 2 | filter **OFF**（`_en(false,false,false)`），同一地址再读一次（"自证"步） | 必须可达（返回值 == 0） | 未执行到 | **FAIL**（`ret_free = -5`，`:916`） |

原始文本（09-23-1，`serial_TGT.log:45-51`；**该文件已被 §9 的复测覆盖，原文见 §9 顶部的转存路径**）：

```
START - filter_blocks_nonwhitelist
    Assertion failed at .../src/i2cf_test.c:916: i2cf_filter_blocks_nonwhitelist: (ret_free is non-zero)
with filter OFF 0x33 must be reachable, got -5 (EEPROM 未在位 / 跳线缺失; 地址未经实测)
[00:00:00.453,000] <err> i2c_pfr_filter: i2cf@40091800: address beyond whitelist: i2c@0x33
 FAIL - filter_blocks_nonwhitelist in 0.042 seconds
```

### 3.2 复现条件

- 稳定复现（09-22、09-23 两次独立会话同行同因；O-30 记载为两轮一致），非 flake。
- 接线：零外部器件；`i2c2`（上游主机）→ `i2cf4` 输入，`i2cf4` 输出 → `i2c3`（挂片内 target `eeprom@33`）。
- 单命令复跑：`cd 810_driver_test/projects/i2cf_test/tests && python run_test.py --case case_default --timeout 30`
  （调试用 `python scripts/dbg_run.py --case case_default --timeout 35`）。
- 硬件必做动作：无特殊动作（1os 单核路径）。

### 3.3 证据

**(a) 前 3 项 FAIL 的消失是"换址"而非"修好"** —— 说明 14/18→17/18 的增量来源：

| 会话 | 片内 target 地址 | `slave_addr_scan`（旁路扫描，观测项） | `filter_passthrough_whitelist` |
|---|---|---|---|
| 09-21-1 | `0x50` | `count=1, addrs=50` | FAIL（读回全 `0xFF`） |
| 09-22-1 / 09-23-1 | `0x33` | `count=2, addrs=33 50` | PASS（`addr=0x33,len=8,rd=5a5b5c5d`，逐字节正确） |

`0x50` 在换址后**仍然应答**（扫描同时报 `33` 与 `50`）⇒ 板上确实另有 `0x50` 器件（用户已告知是 EEPROM 惯例地址），
旧轮"`0x50` 既有片内 target 又有板载器件"是同址双应答者 ⇒ 那时的 `ret=0` / `0xFF` 等观测**不可归因**。
换址后：被拦地址返回 `-5`（NACK）、透传读回逐字节正确 ⇒ 与 `OPEN_ITEMS` O-30 的三条独立证据一致。
**结论：A 本轮暴露的 `:916` 是"潜伏项首次被执行到"，不是换址引入的新回归。**

**(b) 两种归因假设，且当前证据不足以区分**：

- **假设 H1（用例侧）**：`_en(false, false, ...)`（`BLOCK_DISABLE=1, FILTER_DISABLE=1`）在本 HW 上是"只监视、不转发"，
  因此"关掉滤波器后同址应可达"这一**期望本身不成立**。
- **假设 H2（驱动/硬件侧）**：拦截动作会在滤波器内留下阻塞/残留态，仅靠"关掉开关"不足以恢复，
  需显式清中断（`INTR_CLR`）或硬复位。

判据来源核查（关键）：
- 该期望的**唯一依据是驱动源码的负逻辑三元式**（`i2c_filter.c:231-236`：`BLOCK_DISABLE = filter_en?0:1`、
  `FILTER_DISABLE = wlist_en?0:1`），**不是任何硬件文档或实测**：
  `reg_i2c_filter.h:39-49` 只给了字段名/位域，无行为语义；
  厂商样例 `modules/lib/pfr_tpm/samples/i2c_filter_loopback/src/main.c` 只在 `fill_bitmap` 前后临时关开，
  **从不"关着做一次读"** ⇒ 样例也**没有**为 H1/H2 提供任何证据。
- 全仓其它 `filter_en(...)` 调用点（含 `i2cf_test.c` 自身）**没有第二处**在关断态下发起上游读 ⇒
  **该期望在本次之前从未被独立验证过**。

两条假设**都能解释全部现有观测**，故**判"未定"**（守 `OPEN_ITEMS` O-30 与 `KNOWN_ISSUES` #112 的纪律：不得放宽判据）。

**(c) 可排除的类别**：
- **不是环境/接线**：同轮紧随其后的 `filter_passthrough_whitelist` 读**同一地址 `0x33`** 成功（filter ON + 白名单授权），
  `master_eeprom_rw` 16 字节读写 PASS ⇒ 跳线与从设备在位均正常。
- **不是采集/判定链路**：该项失败是串口原生 ztest 断言行（非 mem、非超时截断）。
- **不是撞址**：换址后仍失败（见 (a)）。

### 3.4 归因结论与"下一步怎么定"

**归因未定**（H1 用例假设 / H2 驱动残留态，处置相反）。**一个上板实验即可判死**，建议一次做完：

```
（需要改测试源码 + 重新烧录 ⇒ 属碰硬件动作，须串行、独占板子）
试验 1（核心判别）：reset_all → switch_to_filter → filter_off → 读 0x33（**前置无任何违规**）
   - 读成功  ⇒ H2 成立（残留态）；"关滤波=可转发"本身没问题
   - 读失败  ⇒ H1 成立（该模式不转发）；用例的自证步构造错误
试验 2（H2 的确认与修法线索）：按用例原序列制造一次违规 → 写 INTR_CLR(0x3) → 再 filter_off → 读 0x33
   - 若恢复可达 ⇒ 明确"拦截后需清中断"的恢复序列
预期看到什么：串口出现该新增步骤的 PASS/FAIL；过程量经 ZT_INFO 上报。
为什么必须上板：这是被测 SoC 的 I2C 滤波器**硬件行为**，仓库内无仿真模型、无历史数据点，
离线重放只能复现判定、不能产生新的硬件观测。
```
（若判定落到 H1 ⇒ 改用例：自证步应改用**旁路控制器** `switch_to_master()` 作对照，
而**不是**在关断态下读 —— 但**在实验出结论前，`ret_free == 0` 断言不得放宽**。）

### 3.5 交接单（驱动侧）见 §6。

---

## 4. 失败项 B —— `i3c_i2c_switch_test` / `broadcast_nack_recovery`

### 4.1 现象

用例：让 target 临时切到 I2C（不响应 I3C 广播）→ **第一帧**广播 `RSTACT whole` 应被 NACK →
target 自动切回 → 后两帧广播 `RSTACT peripheral` / `RSTDAA` 应正常 → 帧末 `EVR.BUSY==0`。

三次会话结果完全一致：`FAIL (10/11)`，失败原因串恒为 `rstact_whole_should_nack`。
即**第一帧被 ACK 了**（返回值 0，按源码 `i3c_i2c_controller_test.c:569-572` 判失败）。

原始文本（`serial_CTRL.log:46/59`）：

```
I3C_I2C_TEST:CTRL:FAIL:broadcast_nack_recovery:rstact_whole_should_nack
...
I3C_I2C_TEST:CTRL:SUMMARY:ITEM:broadcast_nack_recovery:FAIL:rstact_whole_should_nack

PROJECT EXECUTION FAILED
```

### 4.2 复现条件

- 稳定复现（三份会话 + O-23/#98 记载的多次复现），**不是环境 flake**。
- 复跑：`cd 810_driver_test/projects/i3c_i2c_switch_test/tests && python run_test.py --case case_switch_core`。
- 与 `KNOWN_ISSUES #87`（i3c 的 prime 零输出 flaky）**不是一回事**，勿并案。

### 4.3 证据与归因

1. **失败只落在首帧**：reason 串唯一且是第 1 条断言（`ctrl_set_fail` 为 last-wins 覆写）⇒
   其后 `rstact_peripheral` / `rstdaa` / `busy_bit_not_cleared` 三条**都没触发**
   ⇒ **#51 修复的"广播 CCC NACK 后控制器恢复路径"本身是好的**，唯一的失配是"第一帧到达时 target 已不在 I2C 窗口"。
2. **窗口预算是固定值、且余量极小**：target 侧窗口 = dispatcher(~1ms) + `TGT_SWITCH_DELAY_MS(5ms)` →
   `NACK_WINDOW_MS(2ms)` 自动切回，即约 **6~8ms**；controller 侧固定 `k_sleep(K_MSEC(6))` 后发第一帧
   （源码注释自陈"若仍 FAIL，可适当增大到 7ms"）。跨核 dispatcher 时延可变 ⇒ 抢窗本质。
3. **PASS→FAIL 的改动窗口 = 框架单出口改造**：上次全量为 PASS 11/11（来源同上，未独立复核）；
   而 `43b232e`（09-21 11:12）恰好给本工程插入：target token 改走 **mbox→主核** 的中继
   （多一次跨核搬运与 FIFO 重试）、`ctrl_test_begin` 加 `rlog_relay_drain()`、
   **`ctrl_test_end` 末尾加 `k_sleep(K_MSEC(2))`**、收尾改 2s 等待 —— 全部落在本用例的计时区间内，
   与 6~8ms 的窗口同量级。
4. **判定口径无异议**：CTRL 有显式 FAIL token ⇒ 按 §6.2.4"token 显式 FAIL 仍是测坏了"，
   即便 TGT 侧没有逐项结果也判 FAIL（`SPEC_REFERENCE.md:305-306`）。
   TGT 侧本项记的是 `NOT_RUN`（09-21 有 mem 时读得 `tgt=NOT_RUN`；TGT 自身 SUMMARY 也把它算作 1 个未过），
   与"窗口错过 ⇒ target 侧该项未完成记账"自洽。

### 4.4 归因结论与建议动作

**归因：用例自身时序竞态（B 类）—— 非被测驱动缺陷。**
**且按 `SPEC_REFERENCE` §6.2.4 归因纪律 / `KNOWN_ISSUES` #98 增补段：
框架级改动（单出口）之后，该时序敏感用例的 PASS→FAIL 不得记为驱动回归。**

建议（均属用例侧，**本轮未执行**）：
1. 复跑 3 次取众数，把 prime/首帧失败与 DS 重启记入报告（区分环境层与断言层）。
2. 按作者注释把 6ms 调到 7~8ms 试；根治做法是**去掉抢窗**：改为等 target 侧"已切到 I2C"的状态信号
   （现在 target 侧本项连一条时间戳 INFO 都没有 —— 这也是目前无法 100% 断言"窗口错过"的缺口）。
3. 不要在驱动侧改 `i3c_linkedsemi.c`（除非复跑证明 `EVR.BUSY` 或后续 CCC 也坏）。

---

## 5. 附带发现（框架侧；**不改变** §3/§4 的判定）

### 5.1 `i3c_i2c_switch_test` 未抬高 `RLOG_TEXT_MAX`，TGT 长行被静默截断（潜在，本轮未造成误判）

- 机制：单出口把从核每行打成 RLOG 帧，文本上限 `common/rlog.h` 默认 **`RLOG_TEXT_MAX = 54`**；
  `common/rlog.c:166-183` 的超长分支**截到 51 字节并补 `...`**。
- 实测（三份会话一致）：`serial_TGT.log:60` 的本项 SUMMARY 行被截成
  `I3C_I2C_TEST:TGT:SUMMARY:ITEM:broadcast_nack_recove...`（原长 60B）。同族旁证：
  `i2c_pure_test` 的 ztest 分隔线（67 个 `=`）同样被截成 51 个 `=` + `...`。
- 影响评估：
  - **本轮无害**：SUMMARY 块是给人读的收尾账（`parse_ztest` 只认 PASS/FAIL/SKIP 与 INFO），
    且 TGT 本项的真实记账就是 `NOT_RUN`（见 §4.3 第 4 条）⇒ **截断没有改变任何判定**。
  - **潜在风险**：同项目 TGT 的 `I3C_I2C_TEST:TGT:FAIL:<name>:<reason>` 对长名+长 reason 会超 54B ⇒
    **判定行被截 ⇒ 解析不出项名 ⇒ 变成假 `MISSING`**。属"最需要证据时报告最像好消息"同族。
- 已有现成修法（`SPEC_REFERENCE` §6.9.8「`RLOG_TEXT_MAX` 必须按工程抬高」）：工程 `CMakeLists.txt`
  `target_compile_definitions(app PRIVATE RLOG_TEXT_MAX=118)` + overlay 的 mbox `width` 抬到 128。
  `i3c_dualcore_test` / `app_iwdg_cpu2_test` 已这么做，**本工程与 `i2c_pure_test` 未做**。
- 处置建议：**不要顺手改**（属框架/模板改造，且改 `width` 要两核同步、影响 overlay ABI）。
  先在 `OPEN_ITEMS` 立一条，由框架负责人按 §6.9.8 的配方统一处理。

### 5.2 诊断陷阱：批量里"内存兜底: 不可用 / 来源=missing"是按设计，不是故障

`common/acquire.py:507-517`（2026-09-22 用户裁定）明确：批量路径**不调 gdb 读内存**，
三个取数点写死 `mem = None`。故 09-22 之后报告里的 `内存兜底: 不可用（内存兜底未执行）` +
`来源=missing` 是**预期形貌**。09-21-1 为 0/50、09-22-1 与 09-23-1 各 29/50 —— 群体现象。
判定中性（同一处注释已论证：批量 mem 取值本就落在"非证据"，§6.2.4 R8/G10）。
**排查时勿把 `missing` 当采集故障**（本轮已按此更正任务书的一条假设）。

---

## 6. 交接单（可直接转交驱动侧）—— i2cf4 滤波器"关掉后不放行"

> 正文只写场景/预期/现象；用例名、行号、断言文本集中在附录 A。

### 6.1 被测对象与拓扑

```
上游主机 i2c2 ──跳线──► i2cf4.SCLI/SDAI ──〔i2cf4 被测滤波器〕── i2cf4.SCLO/SDAO ──跳线──► i2c3(片内从设备 @0x33)
```

- 被测器件：`i2cf4`（I2C 滤波器，驱动 `modules/lib/pfr_tpm/drivers/pfr/i2c/i2c_filter.c`）
- 接线：零外部器件，两段板上跳线
- **已确认正常**（不在本单范围）：滤波器能正确识别并锁存违规地址（`INTR_STT/INTR_RAW`=0x2 `ADDRESS_BEYOND_WHITELIST`、
  `SMBF_NONWHITELIST` 锁存正确地址），且此时主机侧确实收到 NACK；从设备读写、地址扫描、
  **经滤波器的授权透传（8/16 字节逐字节相等）均正常**。

### 6.2 场景（异常）

1. 滤波器**开拦截**，白名单**只授权 0x2A**（另一个地址）；
2. 上游主机访问 **0x33**（不在白名单）⇒ 被拦截（符合预期）；
3. **然后关掉滤波器**（软件写 `SMBUS_FILTER_SET`：拦截关闭 + 白名单关闭）；
4. 对 **0x33** 再访问一次。

### 6.3 预期（测试的期望）

第 4 步**应当成功** —— 滤波器已关闭，这笔访问应直达从设备。
依据：把"关闭滤波器"理解为"该器件不再干预总线"。

### 6.4 现象（实测，两轮以上一致）

**第 4 步仍然失败（`-EIO`）—— 关掉滤波器后，同一地址依然不可达。**
且这次失败**没有**再产生一条"address beyond whitelist"的违规日志
（符合"拦截已关"，但转发也没有发生）。

### 6.5 需要驱动侧回答的问题

1. **拦截一旦发生过，滤波器是否进入某种"阻塞/残留"态，必须靠清中断（`INTR_CLR`）或硬复位才能恢复？**
   仅"关掉滤波器开关"是否不足以恢复？
2. 若是设计如此 ⇒ 请给出**正确的恢复序列**（要清哪些寄存器/位、顺序、是否需要等待）。
3. 若第 1 问为否 ⇒ 那么 `_en(false, false, ...)`（即 `BLOCK_DISABLE=1, FILTER_DISABLE=1`）
   在本 HW 上**是否为"不转发"模式**？若是，测试用它做"无干预"对照的假设不成立，要改用例。
4. 另：`linkedsemi_i2c_filter_cold_reset()` 收尾把滤波器置于 `_en(false,false,true)` 状态 ——
   这是否意味着**上电/复位后的默认态也不转发**？请确认默认态语义。

**旁证（倾向第 1 问，但未证实）**：同工程另一用例同样是"违规访问"场景，
但它在**每笔访问前都清中断**，且该用例通过；本项的自证步**没有清中断**。差异正好落在"是否清中断"上。

---

## 附录 A：代码证据（供驱动侧按需查阅，勿写入交接正文）

| 内容 | 位置 |
|---|---|
| 失败断言行（自证步） | `projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c:913-919`（`i2cf_filter_off` → `k_msleep(5)` → `zassert_ok(ret_free, ...)`） |
| 被拦步 | 同文件 `:905-910` |
| 关闭滤波器 = 写 `SMBUS_FILTER_SET` | `modules/lib/pfr_tpm/drivers/pfr/i2c/i2c_filter.c:224-254`（`BLOCK_DISABLE=1, FILTER_DISABLE=1`） |
| 寄存器字段定义（无行为语义） | `modules/lib/pfr_tpm/drivers/pfr/i2c/reg_i2c_filter.h:39-49` |
| 复位后默认态 | `i2c_filter.c:292-349`（收尾 `linkedsemi_i2c_filter_en(dev, false, false, true)`） |
| 厂商样例（只临时关开，从不关着读） | `modules/lib/pfr_tpm/samples/i2c_filter_loopback/src/main.c:160-205` |
| 串口原始证据 | `reports/2026-09-23-1/i2cf_test_case_default/serial_TGT.log:45-51`（同 `reports/2026-09-22-1/...`）。⚠ 该文件现已为 §9 的 **PASS 复测**结果；原 FAIL 原文见 `tmp/i2cf_pre_h1_fix/` |
| 期望的来源（软件位映射推断） | `projects/i2cf_test/tests/COVERAGE.md:148` |
| 换址提交 | `git show a405816 -- projects/i2cf_test/cpu1_i2cf_test/app.overlay` |

## 附录 B：B 的代码证据

| 内容 | 位置 |
|---|---|
| 首帧断言（`r1 == 0` 即失败） | `projects/i3c_i2c_switch_test/cpu0_i3c_controller/src/i3c_i2c_controller_test.c:566-572`（`k_sleep(K_MSEC(6))` 在 `:566`） |
| 窗口常量 | `projects/i3c_i2c_switch_test/tests/include/i3c_i2c_switch_test_protocol.h`（`TGT_SWITCH_DELAY_MS=5` / `NACK_WINDOW_MS=2`） |
| 引入单出口的提交 | `43b232e`（2026-09-21 11:12:53）：`rlog_relay_drain()` in `ctrl_test_begin`、`k_sleep(K_MSEC(2))` in `ctrl_test_end`、`rlog_wait_secondary_done` in `test_main` |
| 原始证据 | `reports/2026-09-23-1/i3c_i2c_switch_test_case_switch_core/serial_CTRL.log:46,59`；`serial_TGT.log:48,60` |

---

## 7. 未决项（`OPEN_ITEMS.md` 相关条目）

| ID | 状态 | 与本任务关系 |
|---|---|---|
| **O-30** | 待驱动侧（⚠️ 不得擅自放宽判据） | **就是失败项 A**。本报告复核后维持其结论；追加 §3.4 的判别实验（原交接单 `tmp/i2cf_final/CONCLUSION.md` 仍有效，可与 §6 合并使用） |
| **O-31** | 待驱动工程师裁定 | `spif_test` 同类"判据归属"问题，与 A 的"改用例 or 改驱动"是同一决策类型，**可一并裁定** |
| **O-26** | 待裁定 | `eth_test` 挂死（PHY 自旋），与本两案无关；但同为"驱动侧定性"队列 |
| O-23 / O-28 / O-29 | 已结 | B 的记账复核手段与结论来源（本报告 §4.3 第 4 条据此） |

**本轮新增（建议立条）**：
- `i3c_i2c_switch_test` / `i2c_pure_test` **未抬高 `RLOG_TEXT_MAX`** ⇒ TGT 长 token 行被静默截断（§5.1，潜在）。
- （说明性）批量 `内存兜底: 不可用` 是 2026-09-22 裁定的预期形貌，建议在报告模板/速查里点明，避免后续再被误判为采集故障（§5.2）。

> ⚠ **证据路径变更（2026-09-23 复测后）**：`reports/2026-09-23-1/i2cf_test_case_default/`
> 下的同名 case 目录已被复测结果**覆盖**（§9，按"当天单份权威报告"惯例）。
> 本报告 §3 引用的**原 FAIL 证据已转存**：
> - 原 FAIL 17/18（`:916 ret_free`）：`tmp/i2cf_pre_h1_fix/i2cf_test_case_default/serial_TGT.log:47`
> - 中间失败 1（`:942`，裸读版）：`tmp/i2cf_h1fix_attempt1/i2cf_test_case_default/serial_TGT.log:47`
> - 诊断块原始串口：`tmp/i2cf_probe2_session/i2cf_test_case_default/serial_TGT.log`

---

## 9. 追加（2026-09-23）：O-30 处置 = 改用例 —— 自证步改单变量对照 + 上板验证

> 用户裁定：`O-30` 处置方向 = **改用例**。本节记录改动、**全部**上板结果（含中间失败）、
> 由实测反推出的行为规律，以及对"H1 判别实验"证据的更正。

### 9.1 改动

| 文件 | 改动 |
|---|---|
| `projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c` | `filter_blocks_nonwhitelist` 的**自证步**：由「关滤波器后同址应可达」改为「**只往白名单新增一个 `slave` 授权**（通道 1），通路 / 发起方 / 目标地址 / 滤波开关全不动 → 必须可达」。**主判据 `ret_blocked < 0` 原样保留**，未动。 |
| 同上 | `i2cf_filter_off()` 定义处注释：**该行记录已于同日撤回** —— 当时改为「**本平台 filter off ≠ 直通**，不构成自证（原文那套推断已证伪）」，其依据 H1 已被 §9.5/§9.4 撤回；现注释口径 = 「**该断言位置不可测**（紧接被拦事务之后，#116 规律 1）；filter off 是否直通**未定**」。口径更正落点见 `reports/2026-09-23-1/cleanup-2026-09-23.md` 事项 A。 |
| 同上 | 自证步内新增一笔**"热身"读**（结果丢弃，不进判据）——理由见 §9.3 规律 1。 |
| `projects/i2cf_test/tests/COVERAGE.md` | 期望列同步改判（原"filter off 后同址 → 0"标注为已证伪）。 |

`git diff` 仅落在上述两个文件（另有既存的一次性诊断块 `CONFIG_I2CF_SCAN_BURST` /
`CONFIG_I2CF_FILTEROFF_PROBE`，**本次未动**，按指令保留）。

### 9.2 上板结果（逐次如实记录，含失败）

| # | 自证步实现 | 结果 | 失败点 / 关键证据 |
|---|---|---|---|
| 基线 | 「关滤波器后同址应可达」（原始） | FAIL 17/18 | `:916 (ret_free is non-zero)` |
| ① | 新增 ch1 授权 + **裸 `i2c_read`** | FAIL 17/18 | `:942 (ret_authorized is non-zero)` |
| ② | 诊断块 1（非判据） | — | `blocked=-5, post_plain=-5, post_burst=0, clr_plain=0, fresh_plain=0, fresh_burst=0` |
| ③ | 诊断块 2（非判据） | — | `a_plain=-5,a_burst=0,b_plain=0,b_burst=0,d_plain=-5,e_plain=-5,e_burst=0` |
| ④ | 诊断块 3（非判据，各 3 轮） | — | `blk=-5,-5,-5` ｜ `p_plain(候选A)=0,-5,-5` ｜ `q_burst(候选B)=0,0,0` |
| ⑤ | **就地改 ch0 地址**（0x2A→0x33）+ burst | FAIL 17/18 | `:952 (ret_authorized is non-zero)` |
| ⑥ | 新增 ch1 授权 + burst（**无热身**） | FAIL 17/18 | `:957 (ret_authorized is non-zero)` |
| ⑦ | **最终版**：新增 ch1 授权 + **热身一笔** + burst（判据） | **PASS 18/18** | `authorized_ret=0`；`PROJECT EXECUTION SUCCESSFUL`（串口 `serial_TGT.log:318`） |
| ⑧ | 最终版**复跑**（稳定性确认） | **PASS 18/18** | `authorized_ret=0`；`PROJECT EXECUTION SUCCESSFUL` |
| ⑨ | 最终版（**注释订正后**源码）再复跑，使"已验证产物 == 当前源码" | **PASS 18/18** | 同上。**最终版累计 3/3 全绿** |

**最终版实测 INFO**：`whitelist=0x2a,target=0x33,blocked_ret=-5,authorized_ret=0`
（`reports/2026-09-23-1/i2cf_test_case_default/serial_TGT.log`）。
`summary.md` 该行已由 `FAIL 17/18` 更新为 **`PASS 18/18`**。

**新断言的证伪条件**（非恒真；满足"仍可失败"的硬要求）：
① 白名单未真正生效（通道索引 / 使能位 / 命令位图写错）⇒ 该地址仍被拦 ⇒ `-EIO`；
② 滤波器放行（转发）通路坏 ⇒ `-EIO`；
③ 下游从设备不在位 / 跳线缺失 ⇒ 无 ACK ⇒ `-EIO`；
④ **热身笔与正式笔都 `-EIO`** ⇒ §9.3 规律 1 不成立 ⇒ FAIL。

### 9.3 由本次实测反推出的行为规律（本节是本轮最有价值的部分）

1. **一笔被滤波器拦下（NACK）的事务之后，紧接的下一笔事务在本通路上多半会丢失。**
   证据：**10 次处于该位置的事务里 9 次 `-EIO`**（`post_plain` / `d_plain` / `e_plain` / `a_plain`
   / ③的 `p_plain`×2 / 实现 ①②⑤⑥ 的判据读），**包括中间隔了一次冷复位的情形**
   （③`a_plain`、④`p_plain`）；唯一成功的是 ④ 的 k=0。而**再下一笔**（热身之后那一笔）
   实测 **5/5 正常**（`post_burst` / `e_burst` / `q_burst`×3 全为 0）。
   ⇒ 这解释了为什么"紧跟在刻意拦截之后的任何自证步"都做不成：**它不是判据写错，是位置本身多半不可测**。
2. **裸 `i2c_read` 比 `i2c_burst_read` 更不可靠**：同一配置（冷复位 + 只授权 `slave` + filter on）
   重复 3 轮得 `0, -5, -5`；而 burst 在"非紧接位置" **8/8 为 0**。
   ⇒ 旁证：既有的 `filter_passthrough_whitelist`（PASS）用的正是 burst 形态。
3. **就地改写"已在用通道"的授权地址疑似不生效**：把 ch0 的地址由 `0x2A` 改成 `0x33`
   （plain / burst 各一次）**都读到 `-EIO`**；而"新增 ch1 = 0x33"多次读到 `0`。
   **未取寄存器证据 ⇒ 未定**，此处仅作"不用它"的理由，**勿当结论引用**。
   （若属实，属驱动/硬件可查点，建议另立条跟踪。）

### 9.4 对"H1 判别实验"证据的更正（重要；不改变处置方向）

§3.4 的判别实验（`CONFIG_I2CF_FILTEROFF_PROBE`，2026-09-23 执行）被用作了"H1 成立"的判据。
本轮复核发现**该实验的两条读数都不能作为 H1 的证据**：

1. **判读样本 `fresh_off_ret=-5` 是裸 `i2c_read`** —— 而裸读在本通路上本身就不可靠（§9.3 规律 2），
   且该步的**前一笔**（`bypass`）刚失败 ⇒ 按规律 1，这一笔本来就该失败。**单个 `-5` 样本不构成 H1 的证据。**
2. **该实验的"旁路对照"步构造成立不了**：`bypass_ret=-5` 是**用上游 master `i2c2` 发波，
   却把 SCLO/SDAO 引脚交给了 `i2c4`**（`switch_to_master`）⇒ 输出侧不在 i2cf4 手上，
   该步**恒 `-5`**，不构成"链路可达"的对照。

⇒ **H1（"filter off = 不转发"）目前既未被证实、也未被否定。**
但**处置方向不受影响**：原自证步「关滤波器后同址应可达」的期望**确实不可满足**
（§9.3 规律 1 给出了更简单的解释：那是**紧接拦截之后的不可测位置**），
故**"改用例"是对的**——只是依据应换成 §9.3，而不是 H1。

### 9.5 遗留（未做，建议立条）

- §9.3 规律 3（就地改址疑似不生效）：需寄存器级证据才能定性（读回 `WHITELIST_ADDRESS_*`）。
- §9.3 规律 1（被拦后紧接一笔丢失）的**机制**未定（滤波器 FSM？下游从机 resync？上游控制器？）：
  若要在驱动侧定性，最小实验 = 在拦截后以**同一笔**读的**重试**观察寄存器/总线状态。
- 一次性诊断残留（`CONFIG_I2CF_SCAN_BURST` / `CONFIG_I2CF_FILTEROFF_PROBE` + `tests/case_scan_burst/`
  `tests/case_filteroff_probe/`）**按指令本次保留未动**，用毕可整体删除。
- **⚠ 源码注释仍带已撤回的 H1 结论（超出 keeper 写域，须 runner 处理）**：
  `projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c` 的 `i2cf_filter_off()` 注释（`:766-772`，
  写"本平台 filter off ≠ 直通…已被证伪"）及 `:879-880` / `:936-938` 的同口径措辞、判别实验块
  `:1207`（`fresh_off_ret != 0 ⇒ H1 成立`）—— 其依据都是**§9.4 已撤回的 H1**。
  应改为"该位置不可测（§9.3 规律 1）/ H1 **未定**"，并同步 §9.1 改动记录里那句"更正为…"。
