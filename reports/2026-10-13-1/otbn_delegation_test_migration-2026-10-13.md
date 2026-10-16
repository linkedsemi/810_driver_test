# otbn_delegation_test — 迁移到显式代理接口 + 解耦开关矩阵验证（2026-10-13）

> 归档：`reports/2026-10-13-1/`（当天单份权威）。本文件为本轮（迁移 + 矩阵 + 上板）的
> 权威报告；**取代**同日早前 `otbn_delegation_test_regression-2026-10-13.md` 的「板子离线 ⇒
> 未上板」结论（该文件已就地加更正指针，避免两套矛盾结论）。
> 上板全量已执行：**6/6 case PASS，双核 native + 双核 `PROJECT EXECUTION SUCCESSFUL`**。
>
> ⚠ **本文两处已被后续轮次取代**（见 `otbn_kconfig_choice_and_framework_fix-2026-10-13.md`）：
> §1.3 的「第 5 格不做常驻 case（会拖入本地 OTBN）」—— 新 Kconfig choice 下第 5 格**不再**
> 拖入本地 OTBN，已新增 `case_alt_via_proxy` 补齐；§5.3 的「`run_test.py` 多 case 批量缺陷
> **只报告未修**」—— 已修（批量=逐 case，含反例）。

---

## 0. 结论（一句话）

客户端测试体已迁到**显式接口** `mbedtls_ls_otbn_ecdsa_*`（3 个源文件、按角色切换），
6 个 case 全部上板 PASS（含新 `case_explicit_api`），项数不变；四格解耦矩阵 4/4 编译通过、
符号归属与设计逐一吻合、**无重复符号**；附加四组 `RESTARTABLE` 实测证明 **`KNOWN_ISSUES
#120` 的结论已过期**（`RESTARTABLE`+`DELEGATION_CLIENT` 现可编过）。

---

## 1. 任务 1：客户端测试体迁移到显式接口

### 1.1 迁移前的退化（逐项审计）

拆开解耦后，客户端（cpu2）只有 `CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT=y`、
无 `..._OTBN_ALT`（落在矩阵第 3 格），故 `mbedtls_ecdsa_*` = **纯软件**（`ecdsa.c`）。
而 5 个 case 的测试体**原先全部调 `mbedtls_ecdsa_*`** ⇒ 客户端侧所有项**改测软件、代理一行未跑**。
逐项影响（迁移前）：

| 项 | 迁移前客户端实际被测 | 类型 |
|---|---|---|
| t01–t09（genkey/sign/verify × {P256,P384,SM2}） | 软件 ECDSA | **改测软件**（代理未跑） |
| t10_reject_unsupported_curve | 软件拒绝 `DP_NONE` | 改测软件 |
| t11–t16c（blen/越界/并发/重复 init） | 软件 | 改测软件 |
| t17/t88/t19/t89/t21（契约负例） | 软件对无效输入的处置 | 改测软件 |
| **t80_proxy_responds_within_timeout** | **软件立即返回** ⇒ 恒 PASS | **空断言**（"证代理 3s 内返回"证不到任何代理行为） |
| b01_throughput（客户端） | 软件计时 | 改测软件（"代理往返开销"测的是软件耗时） |
| s01_seq_soak（客户端） | 软件计时 | 改测软件（"代理 seq 环绕"根本没走 mbox） |
| t16d（case_explicit_api） | 见 1.3 | 见 1.3 |

服务端（cpu1，本地 OTBN `ecdsa_alt.c`）不受影响，仍是有效对拍基准。

### 1.2 迁移实现（复用既有宏机制，**未另起炉灶**）

沿用上一轮 `case_explicit_api` 引入的 `DELEG_GENKEY/SIGN/VERIFY` 重定向宏，把 guard
从 `defined(OTBN_DELEG_EXPLICIT_API) && defined(...CLIENT)` **改为按角色**
`defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)`：客户端恒调显式名，服务端
恒调 `mbedtls_ecdsa_*`。对 3 个源文件做了同一处理：

| 文件 | 改动 |
|---|---|
| `tests/src/otbn_delegation_test.c` | 重定向 guard 改为按角色（客户端→显式） |
| `tests/src/otbn_delegation_bench.c` | 新增同样的按角色重定向块；6 处调用点为宏 |
| `tests/src/otbn_delegation_soak.c` | 新增同样的按角色重定向块；4 处调用点为宏 |

`OTBN_DELEG_EXPLICIT_API` 标志**保留**，但语义收窄为"**只登记 `t16d` 这一项**"（仅
`case_explicit_api` 传），因此其余 5 个 case 的项集**一项不增**。

### 1.3 迁移后仍失义的一项：`t16d`（已修，见 §4.1）

`t16d` 的注释原称"证明转发桩存活"。但 `case_explicit_api` 的客户端仍在第 3 格
（`..._OTBN_ALT=n`），此配置下 `mbedtls_ecdsa_genkey()` 是**软件**、不是 `mbox.c` 的转发桩
（转发桩只在 `..._ALT_VIA_DELEGATION=y`，即第 5 格存在）。故 `t16d` 在该 case 里**退化为
"软件 vs 代理"对照**，非"转发桩存活证"。已就地改写注释如实说明（`§4.1` 另修了它的运行期缺陷）。

> 未把 `case_explicit_api` 客户端切到第 5 格：第 5 格会 `select CONFIG_LINKEDSEMI_OTBN`，
> 令 `otbn_common.c` 的 `POST_KERNEL` `SYS_INIT` 在 **cpu2 启动期**跑 OTBN 中断初始化
> （`zephyr/soc/linkedsemi/lsqsh/otbn/otbn_common.c:194`）——这是本任务书边界外的**运行时**
> 改动，与"离线只编译"的约束下无法验证安全性，故**只报告不建议启用**。第 5 格本体的
> 编译正确性已在 §2 验证。

### 1.4 项数变化

**无**。三份清单（固件注册项 / `run_test.py::TEST_NAMES(_BY_CASE)` / `dump_results.py`）
逐项一致，实测 ELF 注册数：`case_default/dead_channel/no_server = 24`、`case_throughput = 1`、
`case_seq_soak = 1`、`case_explicit_api = 25`，与清单相符。

---

## 2. 任务 2：开关矩阵编译验证（A=`..._OTBN_ALT`，D=`..._DELEGATION_CLIENT`，X=`..._ALT_VIA_DELEGATION`）

镜像：810 客户端/服务端工程（cpu2/cpu1），`west build -p always`。以 `nm` 对
**构建目录内相关全部 `.obj`** 做"谁定义 `mbedtls_ecdsa_*`"的扫描（比只看链接后 ELF 更强：
GC-sections 会把未被引用的定义丢弃，只看 ELF 会漏掉潜在重复）。

| 格 | 配置 | `mbedtls_ecdsa_*` 由谁提供（实测） | 符号归属证据 | 重复符号 | 编译 |
|---|---|---|---|---|---|
| **2** 服务端 | A=1, D_server=1 | `ecdsa_alt.c`（本地 OTBN） | 仅 `ecdsa_alt.c.obj` 定义 | 无 | ✅ 通过 |
| **3** 客户端现状 | A=0, D=1 | **软件 `ecdsa.c`** | 仅 `ecdsa.c.obj` 定义（`ecdsa_alt.c` 未入构建） | 无 | ✅ 通过 |
| **4** 客户端补 A=1 | A=1, D=1, X=0 | `ecdsa_alt.c`（本地 OTBN） | 仅 `ecdsa_alt.c.obj` 定义；`mbox.c.obj` 无 `mbedtls_ecdsa_*`（仅 `mbedtls_ls_otbn_ecdsa_*`） | 无 | ✅ 通过 |
| **5** 客户端补 A=1,X=1 | A=1, D=1, X=1 | **我们的转发桩（`mbox.c`）** | 仅 `mbedtls_otbn_crypto_mbox.c.obj` 定义；`ecdsa_alt.c.obj` **为空**（文件级 `#if !(D&&X)` 排除），`ecdsa.c.obj` 无定义（ALT 宏排除） | 无 | ✅ 通过 |

**关键机制实证**：`ecdsa_alt.c` 由 `zephyr/modules/mbedtls/CMakeLists.txt:137` **仅在 A=1 时**
加入构建；文件级 `#if !(D_CLIENT && X)` 是第二道保险，正好在第 5 格把它清空以避免与
`mbox.c` 的桩重复。四格均无重复定义、均链接成功。

---

## 3. 附加：四组 `RESTARTABLE` 组合实测（核实 `KNOWN_ISSUES #120`）

`MBEDTLS_ECP_RESTARTABLE` 经 `-DEXTRA_CFLAGS:STRING=-DMBEDTLS_ECP_RESTARTABLE` 注入（已用
`compile_commands.json` 核实该 `-D` 确实进入编译命令，非"没生效导致假通过"）。客户端 conf 为基。

| # | 组合 | 结果 | 证据 |
|---|---|---|---|
| R1 | `RESTARTABLE` + `DELEGATION_CLIENT`（A=0） | **✅ 编译通过** | 全量链接成功（exit 0） |
| R2 | `RESTARTABLE` + `..._OTBN_ALT`（D=0） | **❌ 失败（应失败）** | `config-linkedsemi-tls.h:76: #error "Linkedsemi ECC ALT cannot coexist with MBEDTLS_ECP_RESTARTABLE. …（delegation client alone no longer defines this ALT）…"`（= **新文案**，先于 `check_config.h:195` 触发） |
| R3 | `RESTARTABLE` 单独 | **✅ 编译通过** | 全量链接成功 |
| R4 | `DELEGATION_CLIENT` 单独（无 RESTARTABLE） | **✅ 编译通过** | = 第 3 格，`case_default` 客户端构建 |
| **R1b** 对照 | `RESTARTABLE`+`DELEGATION_CLIENT`+**强制** `-DMBEDTLS_ECDSA_{SIGN,VERIFY,GENKEY}_ALT`（模拟旧语义的"client 强制 ALT"） | **❌ 失败** | 同一 `config-linkedsemi-tls.h:76` 新文案 |

**结论**：`#120` 原文"`RESTARTABLE`+`DELEGATION_CLIENT` → 报错"**在新解耦语义下已不成立**
（R1 通过）；R2 报的是**更新后的文案**；R1b 作为对照证明**旧语义（client 强制 ALT）下会失败**，
即 R1 的"通过"确是新语义带来的、非注入失效。⇒ **`KNOWN_ISSUES #120` 需按此更新**（`§7` 已记 INBOX）。

---

## 4. 上板全量（任务 3）：6/6 PASS

环境（本轮实测，动态定角色）：CKLink-Lite 在位（COM14）；`DebugServerConsole` 1025 **LISTENING**
（PID 46840）；串口 **COM18=MCP2200=cpu2/uart5=CTRL**、**COM16=CH342=cpu1/uart1=TGT**（按
banner 复核：COM18 打印 `[CPU2 OTBN_DELEGATION CTRL]`、COM16 打印 `[CPU1 OTBN_DELEGATION TGT]`）。

> ⚠ 过程记录：本会话初段板子**仅部分在线**（缺 MCP2200），随扫描逐步恢复；最终 6 口齐全。
> 早期"离线"结论已由本节取代。

判定口径：每项 = **同名项双核 PASS**；case = **两核 `PROJECT EXECUTION SUCCESSFUL` + 全部项双核 PASS**。

| case | CTRL | TGT | 结果 | 计数 |
|---|---|---|---|---|
| case_default | SUCCESSFUL | SUCCESSFUL | **PASS** | 24/24 |
| case_dead_channel | SUCCESSFUL | SUCCESSFUL | **PASS** | 2/2（SKIP 22） |
| case_no_server | SUCCESSFUL | SUCCESSFUL | **PASS** | 2/2（SKIP 22） |
| case_throughput | SUCCESSFUL | SUCCESSFUL | **PASS** | 1/1 |
| case_seq_soak | SUCCESSFUL | SUCCESSFUL | **PASS** | 1/1 |
| case_explicit_api | SUCCESSFUL | SUCCESSFUL | **PASS** | 24/24（SKIP 1） |

**与基线（`reports/2026-10-12-2/`）对比**：

| case | 基线 | 本轮 | 差异 |
|---|---|---|---|
| case_default | PASS 24/24 | PASS 24/24 | 一致 |
| case_dead_channel | PASS 2/24 | PASS 2/24 | 一致 |
| case_no_server | PASS 2/24 | PASS 2/24 | 一致 |
| case_seq_soak | PASS 1/1 | PASS 1/1 | 一致 |
| case_throughput | PASS 1/1 | PASS 1/1 | 一致 |
| case_explicit_api | —（基线无） | PASS 24/24 | 新增 case |

**关键项实测**：`t16d` client **PASS**（native `PASS - t16d…`，0.051s）/ server **SKIP**
（native `SKIP - t16d…`）；`t80_proxy_responds_within_timeout` PASS（`INFO proxy_probe:
responded=1,rc=0`——**真代理往返**，迁移前此项是空断言）；`s01_seq_soak` client
`INFO seq_soak: iterations=160,proxy_calls=640`（**640 次真实代理调用穿越 seq 环绕**）。

---

## 5. 本轮发现（含两处真实缺陷）

1. **`t16d` 运行期失败（测试 bug，已修，§5.1）**——代理 `genkey` 不像 `mbedtls_ecdsa_genkey()`
   那样自动载入曲线到 `ctx->grp`，而 `t16d` 未先 `mbedtls_ecp_group_load()` ⇒
   `check_pubkey()` 在**空 group** 上失败（`explicit entry produced an invalid public key`）。
2. **`mbedtls_ls_otbn_ecdsa_genkey` 的调用契约与 `mbedtls_ecdsa_genkey` 不同**（**报告，未改**）：
   前者要求调用方已载入曲线（它只用 `gid` 算尺寸、只填 `d`/`Q`），后者自行载入。
   属 zephyr 侧 `mbox.c`，任务书要求"除非真实缺陷显式报告"——**此处报告，未改动**。
3. **框架：多 case 批量运行会误判单测项 case**（**报告，未改**）：`run_test.py` 在
   `len(cases)!=1` 时对所有 case 用默认 `TEST_NAMES`（24 项），**忽略** `TEST_NAMES_BY_CASE`。
   后果：一次性跑 6 case 时 `case_seq_soak`/`case_throughput`/`case_explicit_api` 被按 24 项判，
   出现**假 FAIL**（实测其串口双核均 PASS+SUCCESSFUL）。本轮改**逐 case 单跑**取正确判定；
   该错误聚合行（`otbn_delegation_test_all_cases`，52/144）已从 summary 移除并备份到
   `tmp/otbn_mig/removed_all_cases_artifact/`。**框架修复超出本任务范围，仅报告。**

### 5.1 t16d 修复

`t16d` 在调显式 genkey 前对 `ctx_exp` 补 `mbedtls_ecp_group_load(&ctx_exp.private_grp, P256)`
（与 `genkey_or_fail()` 对全矩阵的做法一致），并附英文注释说明代理 genkey 的契约差异。
修复后 `case_explicit_api` 由 FAIL 转 **PASS**。改动只在 `#ifdef OTBN_DELEG_EXPLICIT_API`
内，**其余 5 case 二进制不受影响**（其 PASS 结果对 t16d 编辑前的镜像仍然有效）。

---

## 6. 未验证 / 不确定项（显式）

1. **第 5 格作为实际 case 配置**：编译已验证（§2），但**未**把任何 case 客户端切到第 5 格，
   故 **`..._ALT_VIA_DELEGATION=y` 转发桩的"运行期"行为无常驻 case 覆盖**（仅 `t16d` 的软件对照
   与编译期存在性）。原因见 §1.3（cpu2 启动期 `SYS_INIT` 的运行时改动无法离线证安全）。
2. **`RESTARTABLE` 组合只到"编译级"**：R1/R3 编过，但未上板运行。
3. **`case_dead_channel`/`case_no_server` 的注释已过期**：两 case 的 test.sh 注释仍写
   "预期 FAIL（永久阻塞）"，但 `mbox.c` 已有 `DELEGATE_REPLY_TIMEOUT_MS=1000` 超时修复，
   `t80` 现按"1s 内返回"PASS（与基线一致）。属**文档漂移**，本轮未改（避免扩散）。
4. **`tests/COVERAGE.md` 的缺陷族描述已过期**：其"t17/t19/t21/t88/t89 = 产品缺陷（FAIL/panic）"
   与**基线及本轮实测全 PASS** 矛盾。属文档漂移，未改。
5. **框架多 case 判定的修复**（§5.3）未做，只报告。
6. **`case_explicit_api/target` 传 `-DOTBN_DELEG_EXPLICIT_API` 的副作用**仅验到"未引入显式符号 +
   t16d SKIP + 25 项"，未穷举其它编译期分支。

---

## 7. 留档与产物

- 迁移源码：`810_driver_test/projects/otbn_delegation_test/tests/src/{otbn_delegation_test,otbn_delegation_bench,otbn_delegation_soak}.c`
- 矩阵/RESTARTABLE 构建：`810_driver_test/tmp/otbn_mig/mbuild/{cell4,cell5,r1_restart_client,r2_restart_alt,r3_restart_only,r1b_restart_client_forcedalt}`（含 `compile_commands.json`、各 `.obj`）
- 日志：`810_driver_test/tmp/otbn_mig/{matrix_*,build_case_*,run_case_*,full_run}.log`
- 上板归档：`810_driver_test/reports/2026-10-13-1/otbn_delegation_test_case_*/`（serial_CTRL/TGT.log + report.md/json）
- 被移除的批量聚合产物备份：`810_driver_test/tmp/otbn_mig/removed_all_cases_artifact/`
- summary 备份：`810_driver_test/tmp/otbn_mig/summary.md.bak_before_cleanup`
- **未用** `git checkout/restore/stash/reset`；未改 zephyr 侧四个文件。

---

*本报告为 2026-10-13 会话（`reports/2026-10-13-1/`）的一部分；取代同日早前"板子离线"结论。*
