# OTBN 代理「总线化」重构 —— 第三轮：服务端独立线程修复复测（2026-10-13）

> **一句话结论（本轮，权威）**：服务端 worker 从 **1 KB 的 `sysworkq`** 迁到**独立 `otbn_deleg_thread`
> （8192 B 栈）**后，上一轮在「被代理的 ECDSA sign」处必现的服务端内核 panic **彻底消失**；
> `otbn_delegation_test` 全部 8 个 case **双核全 PASS**，与重构前基线 `tmp/baseline_2026-10-13-1/`
> 逐项对比 **7/7 共有 case 全部 0-DIFF**。旁证（反例）：把一核的 fifo width 单独退回 32 与另一核 64 混烧，
> 立即复现**原始 MTU 故障**（`msg size: 48, mtu: 32`）⇒ 「两核必须同批、同宽」约束**实测成立**。

> **本文件已就地更新为第三轮结论**。上一版的结论（"MTU 已修、但残留服务端 sign panic、零回归未达成"）
> **已由本轮修复推翻**：残留 panic 的根因是**服务端跑在系统工作队列上、栈不够**，本轮改为独立线程后消失。

---

## 1. 前两轮回顾（结论仍成立的部分）

### 1.1 MTU 缺陷：已修、已验证（第二轮）

- 改动（维护者落盘，本会话仅验证）：4 份板级 dts 的 `mbox_consumer_otbn_crypto` 节点
  `mboxes = <&mbox 1 32 1 32 1>` → `<&mbox 1 64 1 64 1>`
  （`lsqsh_evb_cpu1/2.dts`、`ls1020_evb_lsqsh_cpu1/2.dts`）。
- **编译期验证**：`-p always` 全量重建后，各 `build/*/zephyr/zephyr.dts` 该节点宽度实测 = **`0x40`（64）**。
- **运行期验证（本轮）**：客户端代理调用**不再**出现 `mbox: msg size error, msg size: 48, mtu: 32`；
  genkey / sign / verify 全部经代理往返成功。

### 1.2 残留缺陷（第二轮定位）：服务端「被代理 sign」panic

第二轮实测：MTU 一修好，服务端（cpu1/target）在**第一条经代理的 ECDSA sign**（`t04_sign_p256`）处
内核 panic：

```text
ASSERTION FAIL [z_spin_lock_valid(l)] @ zephyr/include/zephyr/spinlock.h:136
        Invalid spinlock 0x1010c44c
<err> os:  mcause: 7, Store/AMO access fault   mtval: 1012d874
<err> os: >>> ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0
<err> os: Current thread: 0x10108410 (sysworkq)
```

符号解析：`0x1010c44c`=`_sched_spinlock`、`0x1012d874`=`sys_work_q_stack+0x2f4`、`0x10108410`=`k_sys_work_q`
⇒ 服务端 **worker 跑在系统工作队列 `sysworkq`** 上，在**内核内存上发生破坏**（内核锁失效 + 工作队列栈 store fault）。
本地（非代理）sign 全 PASS ⇒ 只在"跨核代理这一跳"触发。

## 2. 第三轮修复（维护者落盘，本会话仅验证）

`zephyr/soc/linkedsemi/lsqsh/otbn/otbn_delegation.c`（服务端分支）：

- **新增独立线程**：`K_THREAD_DEFINE(otbn_deleg_thread, 8192, server_thread, …, K_PRIO_PREEMPT(2), …)`；
  worker 从 `k_msgq_get(..., K_FOREVER)` 阻塞取请求。
- **msgq 改 `K_MSGQ_DEFINE` 静态初始化**：线程可在 `otbn_deleg_server_init()` 之前就阻塞其上，`init` 只注册回调。
- **根因归属**：服务端原先在 `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=1024` 的系统工作队列上执行；
  重构给每 op 加了 handler 帧 + 两个 48 B 结构体 + 回包再复制一份，把本就紧的 1 KB 顶穿 ⇒
  **栈溢出破坏相邻内核内存**（正好解释"内核锁失效 + 工作队列栈 store fault"）。
  （**旁证**：`case_rsa_delegation` 只跑 RSA、不跑 sign，从未走到溢出点，故第二轮"通过"。）

> **机制说明的诚实边界**：本轮**修复前 panic 已消失**，因此**没有再抓 GDB 现场、也没有实测线程栈水位**——
> "栈溢出"是维护者的归因，与本轮"迁到 8 KB 独立线程后 panic 消失"的观测**一致**，但**未被本轮独立测量证实**
> （见 §5 未验证）。

## 3. 主判据：ECDSA 逐项**零回归达成**

### 3.1 方法

- 两核同批 `-p always` **全量 pristine 重建**（8 case × 2 核；源码 `otbn_delegation.c` mtime 22:14 >
  旧构建 21:35 ⇒ 必须重建，否则 `--skip-build` 会复用**不含修复**的旧镜像）。
- 采集：`run_test.py --ctrl-uart COM18 --tgt-uart COM16 --session-dir reports/2026-10-13-1`
  （同 timeout=60s、同构于基线）。
- 逐项对比：`tmp/compare_baseline.py tmp/baseline_2026-10-13-1 reports/2026-10-13-1`。

### 3.2 结果（`(ctrl, tgt, result)` 三元组逐项）

| case | 结果 | 项数 | vs 基线 |
|---|---|---|---|
| `case_default` | **PASS** | 24/24（双核 SUCCESSFUL） | **0-DIFF** |
| `case_alt_via_proxy` | **PASS** | 24/25 | **0-DIFF** |
| `case_explicit_api` | **PASS** | 24/25 | **0-DIFF** |
| `case_dead_channel` | **PASS** | 2/24（客户端门控 SKIP） | **0-DIFF** |
| `case_no_server` | **PASS** | 2/24（同上） | **0-DIFF** |
| `case_seq_soak` | **PASS** | 1/1 | **0-DIFF** |
| `case_throughput` | **PASS** | 1/1 | **0-DIFF** |
| `case_rsa_delegation` | **PASS** | 3/4 | 基线无此 case（后加） |

⇒ **基线 7 个 case 共 124 项，逐项 0 差异**；上一轮那 67 项 DIFF（自 `t04_sign_p256` 起）**全部消失**。
关键项 `case_default` 的 `serial_TGT.log` / `serial_CTRL.log` 现均为**完整套件 + `PROJECT EXECUTION SUCCESSFUL`**，
`t04_sign_p256` … `t89` 全部 `PASS`。

### 3.3 归档完整性旁证

- 判据 `scripts/check_archive.py`：本轮 per-case 归档为「一次、完整、从头到尾」（H1 头部 `ZT_BOOT` 第一 /
  H3 末行 `SUCCESSFUL`）。（H2「`Running TESTSUITE` 恰 1 次」对**多 suite** 工程属**口径误报**，见 KNOWN_ISSUES #117 增补。）

## 4. RSA 经代理（防回归）：**保持 PASS**

`case_rsa_delegation` = **PASS**（CTRL=SUCCESSFUL, TGT=SUCCESSFUL）：

| 项 | CTRL | TGT | 实测（`serial_CTRL.log` INFO） |
|---|---|---|---|
| `r01_rsa_modexp_roundtrip_2048` | PASS | PASS | `rsa_enc: rc=0` / `rsa_dec: rc=0` / `roundtrip ok=1` |
| `r02_rsa_modexp_roundtrip_4096` | PASS | PASS | `roundtrip ok=1` |
| `r03_reject_unsupported_modulus` | PASS | PASS | 本地即拒 `rc=-22` |
| `r90_rsa_mailbox_bound` | PASS | SKIP | CTRL：屏蔽通道后显式调用 `returned=1, rc=-116, elapsed_ms=10001, timeout_ms=15000` |

- `r90` 判据成立：屏蔽通道后显式调用返回**非零** `rc=-116` 且耗时 ≈ 10 s 总线界 ⇒ **确实走了代理**；
  TGT `SKIP` 属设计。
- 与上一轮一致（3/4），**无回归**。

## 5. 旁证（反例）：跨核 fifo 宽度耦合约束**实测成立**

**假设**（dts 注释声明）：`mbox_consumer_otbn_crypto` 的 width 两核必须一致（共享 fifo 区）。

**反例实验**（不改维护者任何文件）：把 **cpu2 CLIENT** 用临时 overlay 单独退回 `32`（生成 dts 实测 `0x20`），
**cpu1 SERVER** 保持 `64`（`0x40`），**混烧**后跑 `case_default`：

| 侧 | fifo | 结果 |
|---|---|---|
| cpu2 CLIENT（32，旧） | 32 | 每次代理发送报 **`<err> mbox_linkedsem: mbox: msg size error, channel: 1, msg size: 48, mtu: 32`**（共 24 次）；**17 FAIL / `PROJECT EXECUTION FAILED`** |
| cpu1 SERVER（64，新） | 64 | 本地矩阵全 PASS / `PROJECT EXECUTION SUCCESSFUL` |

⇒ **约束成立**：一核新一核旧（宽度不一致）**立即复现原始 MTU 故障**（`msg size: 48, mtu: 32`，与第二轮
修复前逐字相同）。故 **"两核必须同批重建、同烧"** 是**硬约束**（不是保守习惯）。
（证据留存：`tmp/d_ce/mismatch_run1.log`、`tmp/d_ce/width32_client.overlay`。）

## 6. 未验证 / 待办

1. **服务端 sign panic 的"栈溢出"机制未被独立测量**：本轮 panic 已消失，**未做 GDB 抓现场、未测线程栈水位**
   （无异常可抓）。若要闭环，可给 `otbn_deleg_thread` 加 `k_thread_stack_space_get` 打印、或临时把栈调小复现。
2. **未做**"服务端 64 / 客户端 32"的**反向**耦合反例（本轮只做了 client32×server64 一个方向）；
   该方向已足以证明约束，但反方向未单测。
3. 未验证 `req->arg[1]`(blen) / 输出指针在**异常输入**下是否越界（本轮用例未覆盖恶意输入）。
4. 本会话**未改任何 `zephyr/`、`modules/`、`810_driver_test/projects` 源文件或 dts**（任务约束：只验证）；
   唯一新增物 = `tmp/d_ce/` 下的反例 overlay / 脚本 / 日志与 `tmp/` 下的运行日志。

## 附 A：本轮复测方法（命令）

```bash
# 1) 全量重建（两核同批 -p always；含修复的源码 mtime 22:14，旧构建 21:35 ⇒ 必须重建）
cd 810_driver_test/projects/otbn_delegation_test/tests
python -u run_test.py --ctrl-uart COM18 --tgt-uart COM16 --session-dir reports/2026-10-13-1
# 2) per-case 重跑一遍（批量只写聚合报告；逐 case 才产出 reports/<proj>_<case>/ 与 summary 行）
for c in case_default ... case_rsa_delegation; do
  python -u run_test.py --case $c --skip-build --ctrl-uart COM18 --tgt-uart COM16 --session-dir reports/2026-10-13-1
done
# 3) 逐项对比
python tmp/compare_baseline.py tmp/baseline_2026-10-13-1 reports/2026-10-13-1
```

## 附 B：报告/归档口径说明

- `run_test.py` **不带 `--case`**（一次跑全部 8 case）只写**聚合报告** `otbn_delegation_test_all_cases/`；
  **per-case 报告**须逐 `--case` 跑（`run_all.py` 即如此逐 case 调用）。本轮两者都做了：
  聚合报告用于确认全 PASS，per-case 报告用于与基线**逐项对比**。
- 聚合行 `otbn_delegation_test_all_cases` **不得进 `summary.md`**（已剔除）；8 个 per-case 行照常保留。
- 中文/编码：本会话中文输出经 `PYTHONIOENCODING=utf-8`（非 tty 通道），避免 `dbg_run.py` 族的
  `UnicodeEncodeError`（KNOWN_ISSUES #91 残缺口）。
