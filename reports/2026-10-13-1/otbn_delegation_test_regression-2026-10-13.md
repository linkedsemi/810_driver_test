# otbn_delegation_test 上板复测报告 — 2026-10-13

> 🔴 **本文件结论已过期，就地更正**：本文件写于板子**仅部分在线**（缺 MCP2200）之时，结论为
> "5 case 一个都没能上板"。**当日板子已完全恢复**，随后完成了「迁移到显式接口 + 解决耦开关矩阵
> + 上板全量」。**权威且最新**的结果见 [`otbn_delegation_test_migration-2026-10-13.md`]
> (otbn_delegation_test_migration-2026-10-13.md)：**6/6 case 上板 PASS（双核 native +
> 双核 PROJECT EXECUTION SUCCESSFUL）**。本文件其余内容仅保留为"部分在线阶段"的过程留档，
> **不要**据此文件下任何运行时结论。

> 会话归档目录：`reports/2026-10-13-1/`（当天单份权威；本文件并入，未新建 `-2`）。
> 本文件记录：改动摘要 / 环境阻塞 / 已完成的非上板验证 / 基线锚点 / 结论 / 未做项。
> **本任务为硬件任务，经 runner（STRATEGY_TEST）执行。**

---

## 0. 结论（一句话）

**板子物理不在线**（无 XuanTie/CKLink 探针，板载双串口 COM16/17/18 全部消失，只剩一个无关的桌面
CP210x COM5）⇒ **5 个 case 一个都没能上板运行**。本次改动**编译级验证通过**（5 case × 双核全量重建，
零错误；实测符号形态与设计一致），但「运行时无行为回归」这一目标**无法判定** —— 需板子恢复后原样重跑。

---

## 1. 任务与改动摘要

**目标**：验证一处驱动源码改动无运行时行为回归，方法是上板跑全 5 个 case 并与基线
`reports/2026-10-12-2/`（同工程同 5 case，刚跑过、全 PASS）逐项对比。

**改动文件**：`d:/zephyr4_0/zephyr/soc/linkedsemi/lsqsh/otbn/mbedtls/mbedtls_otbn_crypto_mbox.c`
（client 分支）+ 新增同目录头文件 `mbedtls_otbn_crypto_mbox.h`。

**实测改动内容**（`git diff`，工作树分支 = `linkedsemi`；`.c` 为 `M`、`.h` 为 `??` 未跟踪）：

- 三个代理函数体**改名**：`mbedtls_ecdsa_genkey/sign/verify` → `mbedtls_ls_otbn_ecdsa_genkey/_sign/_verify`
  （代理体、`client_sem` 串行、mbox 往返逻辑原样搬移）。
- 原 mbedtls 名字**保留**，改为薄转发：`return mbedtls_ls_otbn_ecdsa_*(...);`
- 新增头文件仅声明（`mbedtls_otbn_crypto_mbox.h`），由 `zephyr_include_directories(mbedtls/.)` 覆盖。
- diff 规模：`1 file changed, 37 insertions(+), 3 deletions(-)`（+ 新头文件）。

**被测用例如何触达该改动**（关键）：5 个 case 的测试体（`tests/src/otbn_delegation_test.c` /
`_bench.c` / `_soak.c`）**全部调用 `mbedtls_ecdsa_genkey/sign/verify`**（mbedtls 名字），
cpu2（client）侧这些名字在改动后走**新的转发路径** ⇒ **本次改动确实在用例的必经路径上**，
上板跑用例能覆盖它（这也是为何必须上板才能判定运行时行为）。

---

## 2. 环境预检（STRATEGY_TEST 前置，逐项手动执行）

按 `qsh-debug` §2.4 / `ref-self-heal.md` §1 六项逐项手动验证（未委托 run_all 内置预检）：

| # | 检查项 | 结果 | 证据 |
|---|---|---|---|
| 1 | DebugServer 进程存在 | **FAIL** | 初始 `tasklist` 无 `DebugServerConsole.exe`；手动启动一次即报错（见 §3） |
| 2 | 1025 端口 LISTENING | **FAIL** | `netstat -ano \| grep :1025` 无输出（无监听、无 ESTABLISHED） |
| 3 | GDB 能连 127.0.0.1:1025 | **FAIL** | 不可达（DS 未能起来） |
| 4 | 串口数量够（双核 ≥2，排除 CKLink） | **FAIL** | 仅枚举到 **1** 个口，且是**无关桌面口** `COM5` (CP210x, VID 10C4:EA60)；板载两串口缺席 |
| 5 | 无 FlashProgrammer 冲突 | PASS | `tasklist | grep -i csky` 无输出 |
| 6 | 无 1025 ESTABLISHED 残留 | PASS | 无 1025 任何记录 |

命中第 1/3/4 项 FAIL。按自愈阶梯执行软件步骤（启 DS、查端口、查 PnP）后仍失败，
到达需**物理介入**的步骤（拔插 CKLink USB / 检查板子供电与接线）⇒ 按 `qsh-debug` §2.2a
**中止并上报，不再空转重启 DS**（§2.2a 明确禁止反复重启 DS/杀进程空转）。

---

## 3. 阻塞证据（板子物理不在线，非软件/配置问题）

**3.1 DebugServer 启动原文**（`cd C:/uaer_app/debug_server/bin && ./DebugServerConsole.exe`）：

```
|  XuanTie Debugger Server (Build: Jun  3 2026, Windows)    |
ERROR: No XuanTie CKLINK has been connected to PC or XuanTie CKLINK driver isn't installed correctly!
ERROR: Cklink connection failed.
Input enter to exit...
```

这正是 `qsh-debug` §2.2a 的判据：**根本找不到探针**（区别于 §2.2 的「探针在、被别的程序占用」）。

**3.2 探针不在 PnP 枚举**：`Get-PnpDevice -PresentOnly` 过滤 `CKLink|XuanTie` → **空**。
（USB 设备仅枚举到通用 hub / 复合设备 / 一个 USB-based DSL Instrument，无 XuanTie CKLink。）

**3.3 板载串口缺席**：

| 端口来源 | 器件 | 昨日（基线 2026-10-12-2）| 本次 |
|---|---|---|---|
| cpu2/uart5 = CTRL | MCP2200 (04D8:00DD) | COM18 | **缺席** |
| cpu1/uart1 = TGT | CH342 (1A86:55D2) | COM16（+COM17 备用）| **缺席** |
| 调试器自身口 | CKLink (32BF) | — | **缺席** |
| 无关桌面口 | CP210x (10C4:EA60) | COM5 | COM5（**与板子无关**）|

基线 `reports/2026-10-12-2/port_mapping.json` = `{"ctrl":"COM18","tgt":"COM16"}`，
其 `precheck_report.md` 记串口枚举 = `['COM5','COM17','COM16','COM18']`。
⇒ 板子三个串口（COM16/17/18）本次**全部消失**，只剩那个无关的 COM5。

**3.4 判据结论**：`No XuanTie CKLINK...` + PnP 无 CKLink + 板载串口全缺席
= **板子物理不在线**（未上电 / USB 掉线 / 线松 / 板子被移走）。
**不是** DebugServer 配置问题、**不是** FlashProgrammer 抢占（无 csky 进程）、
**不是** G-ENV-COM5 那类「无关口拖累预检」问题（这里连板载口都不在）。

---

## 4. 已完成的非上板验证（编译级）——**证明改动真被编译进镜像**

因硬件阻塞，本报告额外执行了**不需要板子**的构建验证（走本工程既有入口
`tests/run_test.py --build-only`，即各 case 的 `test.sh` 默认 `SKIP_LOAD=y` 只编译路径），
用于回答「改动能否真正编进被测固件」，并为板子恢复后直接上跑做好准备。

**4.1 构建结果**（`python run_test.py --build-only --ctrl-uart COM18 --tgt-uart COM16`）：

| case | 结果 | 耗时 |
|---|---|---|
| case_dead_channel | OK | 163.5s（双核） |
| case_default | OK | 173.3s |
| case_no_server | OK | 172.9s |
| case_seq_soak | OK | 173.4s |
| case_throughput | OK | 173.9s |

**全部 5 case × 双核全量（pristine）重建，零错误**（日志 `tmp/run_otbn_deleg_build_2026-10-13.log`，
尾部 `Build-only requested; skipping run phase.`）。

**4.2 产物新鲜度（证明改动已编译入镜像）**：源 `mbedtls_otbn_crypto_mbox.c` mtime =
**2026-10-13 13:43:38**；其后每个 case 的 client 与 server 两份
`.../zephyr/CMakeFiles/zephyr.dir/soc/linkedsemi/lsqsh/otbn/mbedtls/mbedtls_otbn_crypto_mbox.c.obj`
与对应 `zephyr.elf` mtime 均落在 **14:03–14:16**（**均晚于源文件**）⇒ 改动确已编译进全部镜像。
（昨日基线 ELF mtime = 10-12 22:32–22:47，早于源改动 ⇒ 若不复建即上跑会是旧镜像；本次已复建，规避。）

**4.3 符号形态（实测，与改动设计一致）**——`case_default` **client** ELF（controller）：

```
mbedtls_ecdsa_genkey             size 0x4  T   ← 薄转发桩（4 字节）
mbedtls_ecdsa_sign               size 0x4  T
mbedtls_ecdsa_verify             size 0x4  T
mbedtls_ls_otbn_ecdsa_genkey     size 0x140 T  ← 大函数体（真代理）
mbedtls_ls_otbn_ecdsa_sign       size 0x18e T
mbedtls_ls_otbn_ecdsa_verify     size 0x19c T
```

**server** ELF（target）：`mbedtls_ecdsa_genkey/sign/verify` 为本地实现体、
**无任何 `mbedtls_ls_otbn_ecdsa_*` 符号** ⇒ 代理符号未泄漏到服务端镜像，与头文件注释所述一致。

> 定性：编译级证据**只证明**「改动能编进被测固件、符号形态符合设计」，
> **不证明**运行时行为正确 —— 后者必须上板（见 §5/§6）。

---

## 5. 基线锚点（2026-10-12-2 逐项，抄录自其 JSON 报告，用于板子恢复后对比）

| case | 基线结论 | 基线计数 | ctrl_done | tgt_done | mem_state |
|---|---|---|---|---|---|
| case_default | PASS | 24/24 | SUCCESSFUL | SUCCESSFUL | unavailable |
| case_dead_channel | PASS | 2/24 | SUCCESSFUL | SUCCESSFUL | unavailable |
| case_no_server | PASS | 2/24 | SUCCESSFUL | SUCCESSFUL | unavailable |
| case_seq_soak | PASS | 1/1 | SUCCESSFUL | SUCCESSFUL | unavailable |
| case_throughput | PASS | 1/1 | SUCCESSFUL | SUCCESSFUL | unavailable |

**逐项形态摘要（全部 `src=token`，无 mem 兜底）**：

- **case_default**：24 项（t01–t16c、t17、t19、t21、t80、t88、t89）**两核全 PASS**。
- **case_dead_channel / case_no_server**：客户端（CTRL）**绝大多数项 SKIP**（门控 `client_proxy_ready()`），
  仅 `t10_reject_unsupported_curve` 与 `t80_proxy_responds_within_timeout` 两核 PASS；
  服务端（TGT）本地项全 PASS。分母 24 - 22 SKIP = 2。
- **case_seq_soak**：单测项 `s01_seq_soak` 两核 PASS。
- **case_throughput**：单测项 `b01_throughput` 两核 PASS（数值在 INFO bench 行）。

---

## 6. 逐项对比表（本次 vs 基线）

| case | 基线结论 | **本次结论** | 差异 |
|---|---|---|---|
| case_default | PASS 24/24 | **未获取**（板子离线，未跑） | 无法计算 |
| case_dead_channel | PASS 2/24 | **未获取** | 无法计算 |
| case_no_server | PASS 2/24 | **未获取** | 无法计算 |
| case_seq_soak | PASS 1/1 | **未获取** | 无法计算 |
| case_throughput | PASS 1/1 | **未获取** | 无法计算 |

**结论：无法判定运行时回归（blocked），非「无回归」也非「有回归」。**
唯一能下的编译级结论 = **编译无回归**（§4）。

> 本表刻意**不填**任何由旁证推出的「本次结果」——原始 ztest native 行、
> 两核 `PROJECT EXECUTION SUCCESSFUL`、归档完整性三判据（H1/H2/H3）**一条都没有**，
> 任何「以编译通过推运行时无回归」的写法都属违规。

---

## 7. 未做与不确定项（诚实标注）

**未做（因硬件阻塞）**：

1. **5 个 case 的运行时全部未做**：无任何串口采集、无 ztest native 逐项行、无 INFO 值。
2. **无两核 `PROJECT EXECUTION SUCCESSFUL`**：判定链（`qsh-test-compare` §2）的原始证据全缺。
3. **归档完整性三判据未核**：无 `serial_CTRL.log` / `serial_TGT.log`，H1/H2/H3 无从判。
4. **未做 `check_archive.py` / `expect_check.py`**：无归档可核。
5. **未在 `reports/2026-10-13-1/summary.md` 加行**：该文件由 run 流程按 case 写入；
   **本次未跑 case ⇒ 不加行**（避免用无证据的行污染权威 summary）。本报告为独立分析文件。

**不确定项 / 需向用户确认**：

- 板子为何离线（掉电？USB 拔了？被移走？）—— 需用户确认并恢复供电/连接。
- 恢复后 `--ctrl-uart/--tgt-uart` 的实际口号可能变（Windows 重新枚举会改号），
  需按 banner 重新定角色（**不要**沿用基线的 COM18/COM16）。

**已核对、不构成风险**：

- 工作树分支 = `linkedsemi`（与基线同族；本改动是**工作树未提交改动**，非分支差异，
  不触发 G-BRANCH-1 那类「改动躺在别的分支」的误读）。
- 本会话未碰过 1026 端口（无 cpu2 通道访问），无需为「1026 残留」预重启 DS。
- 无 `git checkout/restore/stash/reset` 操作；改动文件原样保留（`.c` M、`.h` ??）。

---

## 8. 板子恢复后立即执行清单（幂等，可直接照做）

```bash
# 1) 确认板子回来（应见 CKLink + CH342/MCP2200 串口）
python -c "from serial.tools.list_ports import comports; print([p.device for p in comports()])"
powershell -NoProfile -Command "Get-PnpDevice -PresentOnly | Where-Object { \$_.FriendlyName -match 'CKLink|XuanTie' } | Select FriendlyName"

# 2) 重启 DebugServer（首启到 1025 LISTENING 后等 ~5s 稳定期）
cd C:/uaer_app/debug_server/bin && ./DebugServerConsole.exe &

# 3) 按 banner 定角色（排除 32bf/cklink），CSKYFlashProgrammer 不得占用
# 4) 单工程全 5 case（会再全量重建，含本节要求的"证明重编译"）：
cd 810_driver_test/projects/otbn_delegation_test/tests
python run_test.py --ctrl-uart <CTRL> --tgt-uart <TGT> \
  --session-dir ../../reports/2026-10-13-1        # 并入当天单份权威目录
# 5) 判定 = 逐 case 与 §5 基线一致；核归档完整性（H1/H2/H3）
```

---

*本报告为 2026-10-13 会话（`reports/2026-10-13-1/`）的一部分；与环境预检、编译验证同步产出。*
