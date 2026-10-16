# OTBN ECDSA Kconfig 二选一改造验证 + 代理 genkey 契约修复 + run_test.py 框架缺陷修复（2026-10-13）

> 归档：`reports/2026-10-13-1/`（当天单份权威）。本文件为本轮（矩阵编译验证 + RESTARTABLE
> 复测 + genkey 契约验证 + 新增第 5 格 case + 框架修复 + 上板全量）的权威报告。
> 同日早前的 `otbn_delegation_test_migration-2026-10-13.md` / `..._regression-2026-10-13.md`
> 保留，但其中"第 5 格无运行期覆盖""run_test.py 批量修复只报告未做"两处**已由本轮取代**。
>
> ⚠ **更正（2026-10-13 同日后续轮）**：本文件 **§C「`genkey_or_fail()` 的预载：评估结论 = 不能移除」
> 与 §5①「本地 ALT 一侧缺口仍存 ⇒ 本核路径仍须预载」两处结论已被推翻**。本核 ALT
> `modules/crypto/mbedtls/library/ecdsa_alt.c` 的自载曲线修复已落地（`:373`），移除测试侧**全部**预载后
> 上板全量 **7/7 case PASS**、服务端不再出现 `private_grp.N==0`。三入口（软件/本核 OTBN/代理）现均自载曲线、
> 调用方**不再需要预载**。详见 `otbn_genkey_preload_removal-2026-10-13.md` 与 `KNOWN_ISSUES #121`。

---

## 0. 结论（一句话）

Kconfig **二选一**改造语义正确：本核 OTBN 与走代理**互斥**、走代理**不再拖入本地
OTBN 驱动**；五格矩阵对象级符号归属逐格吻合、**无重复符号**；`RESTARTABLE` 四组与新文案
全部符合预期；代理 `genkey` 契约修复生效（`t16d` 去掉测试侧预载仍 PASS）；新增
`case_alt_via_proxy` 补齐第 5 格运行期覆盖；`run_test.py` 多 case 批量缺陷已修（批量=逐 case，
反例仍 FAIL）；**上板 7/7 case PASS（双核 native + 双核 `PROJECT EXECUTION SUCCESSFUL`）**。

---

## A. 五格矩阵编译验证（810 客户端镜像 cpu2，`west build -p always`）

镜像 = `projects/otbn_delegation_test/cpu2_controller_otbn_delegation_test`（客户端，
`lsqsh_evb@2os/lsqsh/cpu2`）。符号归属用 `riscv64-unknown-elf-nm -g --defined-only`
对**构建目录内全部 `.obj`** 扫描（比只看链接后 ELF 强：GC-sections 会丢未引用定义）。
构建产物：`tmp/otbn_verify/mbuild/cell*/`。

| 格 | 加速入口 | 代理开关 | `mbedtls_ecdsa_{genkey,sign,verify}` 由谁提供（实测） | 显式 `mbedtls_ls_otbn_ecdsa_*` | 重复符号 | 编译 |
|---|---|---|---|---|---|---|
| **1** | 关闭 | 关 | `ecdsa.c.obj`（纯软件） | —（CLIENT=n，mbox.c 未编） | 无 | ✅ |
| **2** | 关闭 | 开 | `ecdsa.c.obj`（纯软件） | `mbedtls_otbn_crypto_mbox.c.obj` | 无 | ✅ |
| **3** | 本核 OTBN | 关 | `ecdsa_alt.c.obj`（本地 OTBN） | —（mbox.c 未编） | 无 | ✅ |
| **4** | 本核 OTBN | 开 | `ecdsa_alt.c.obj`（本地 OTBN） | `mbedtls_otbn_crypto_mbox.c.obj` | 无 | ✅ |
| **5** | **走代理** | (自动开) | **`mbedtls_otbn_crypto_mbox.c.obj`（转发桩）** | 同文件（两套名都在 mbox.c） | 无 | ✅ |

**第 5 格的关键证据（未拉本地 OTBN）** —— `cell5_viadeleg/zephyr/.config` 与
`cell2_none_proxyon/zephyr/.config` 的 **OTBN 相关行逐行 diff 只有 2 行不同**（choice 成员）：

```
< CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_NONE=y                 # cell2
> # CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_NONE is not set      # cell5
< # CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION is not set   # cell2
> CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION=y              # cell5
```
两格**都没有** `CONFIG_(MBEDTLS_)LINKEDSEMI_OTBN=y`（`grep -c` 均 0）；两格都有
`CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT=y`。⇒ 第 5 格**只**经由
`..._DELEGATION_CLIENT`，**不含**本地 OTBN 驱动/SYS_INIT —— 正是本轮改造的核心目的，
也解除了上一轮"不敢把第 5 格做成常驻 case"的阻塞。

`ecdsa_alt.c` 由 `zephyr/modules/mbedtls/CMakeLists.txt:137` **仅在**本地入口被选时入构建；
文件级 `#if !defined(..._ALT_VIA_DELEGATION)` 是第二道保险，正好在第 5 格把它排除。

### 互斥验证（实测）

`conf` 同时写 `MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT=y` **与**
`MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION=y` → 构建通过，`.config` 里**只有**
`..._ALT_VIA_DELEGATION=y`，本核入口为 `is not set`。⇒ Kconfig choice 只允许一个（实测）。

### 零迁移验证

- **810 服务端**（`cpu1_target/prj.conf` 的 `..._OTBN_ALT=y`，**未改一字**）：编译通过，
  `nm` 显示 `ecdsa_alt.c.obj` 提供 `mbedtls_ecdsa_*`（旧符号名仍选中本核 OTBN）。✅
- **样例**（`samples/crypto/otbn/delegation/mbedtls_{client,server}`）：以**记录的 board**
  `lsqsh_evb@1os/lsqsh/cpu{1,2}` **无法编过**——但失败是**板级/DT**问题而非 Kconfig：
  cpu2@1os 无 `zephyr,flash` chosen（`soc.c` 报 `DT_CHOSEN_zephyr_flash_..._ADDRESSU`
  undeclared），cpu1@1os 链接缺 `__device_dts_ord_247`（mbox 设备）。其 `build/` 目录
  **从来就没有** `zephyr.elf`（先于本轮即失败）。改用 `@2os` 后**两样例均编过**
  （`smp2_client/smp2_server` exit=0），且 OTBN 符号归属正确
  （client=`ecdsa.c` 软件 + `mbox.c` 显式；server=`ecdsa_alt.c` 本地 OTBN）。
  ⇒ 与 Kconfig 改造**无关**（choice 改动不可能产生 DT/链接错误）。**见 §5 发现 ②**。
- **各 case**：见 §G 上板全量（7 case 全部以现有 conf 构建、运行通过）。

---

## B. `RESTARTABLE` 组合复测（判据已变）

`-DMBEDTLS_ECP_RESTARTABLE` 经 `-DEXTRA_CFLAGS:STRING=` 注入；已用 `compile_commands.json`
核实该 `-D` 真的进入编译命令（`b4` 构建出现 **1241** 处，非"注入失效致假通过"）。

| # | 组合 | 结果 | 证据 |
|---|---|---|---|
| B1 | 入口=**关闭** + 代理开 + RESTARTABLE | **✅ 通过** | 全量链接成功（= 第 2 格） |
| B2 | 入口=**本核 OTBN** + RESTARTABLE | **❌ 失败（应失败）** | `config-linkedsemi-tls.h:77` **新文案**（`…set CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_NONE=y (no OTBN ECDSA alternative; the delegation client alone no longer defines this ALT)…`），先于 `check_config.h:195` |
| B3 | 入口=**走代理** + RESTARTABLE | **❌ 失败（应失败）** | 同一 `config-linkedsemi-tls.h:77` 新文案（走代理也定义 ALT 宏） |
| B4 | RESTARTABLE 单独 | **✅ 通过** | 全量链接成功 |

⇒ B1 通过坐实"入口=关闭 + 代理开"下 `RESTARTABLE` 合法（= `KNOWN_ISSUES #120` 的旧结论已过期）；
B3 失败符合预期（走代理定义 ALT 宏 ⇒ 与 RESTARTABLE 冲突）。

---

## C. 代理 genkey 契约修复验证

`mbedtls_ls_otbn_ecdsa_genkey()` 现已自行 `mbedtls_ecp_group_load(&ctx->private_grp, gid)`。

- **`t16d`（去掉测试侧预载）→ PASS**：`case_explicit_api` 客户端 native
  `INFO equiv: mbedtls_rc=0,explicit_rc=0` + `PASS`（服务端 SKIP）。⇒ 代理 genkey 自载曲线生效。✅
- **`genkey_or_fail()` 的预载：评估结论 = 不能移除**（**已按实测回填**）。本轮实测：移除后
  上板，**服务端（本核 OTBN 路径）t01–t09/t11–t14/t16a/t16b 全 FAIL**（`cmp(d,N)<0 is false`
  ⇒ `ctx->private_grp.N==0`），且**级联**使客户端 t15/t16a–c 也 FAIL（`rc=-116`）。根因见 §5 发现 ①：
  `mbedtls_ecdsa_genkey`（`ecdsa_alt.c` **本地** ALT）**不**自行载入 group。回填后 7/7 case PASS。
  故测试侧预载仅对**代理**路径多余（`t16d` 已证明），对**本核 OTBN**路径仍**必需**。

---

## D. 新增 `case_alt_via_proxy`（补齐第 5 格运行期覆盖）

**设计**：cpu2 客户端配成「入口=走代理」（`controller.conf`：
`CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION=y`）⇒ 其**普通**
`mbedtls_ecdsa_*` **就是** `mbox.c` 的转发桩；共享测试体的 `DELEG_*` 重定向宏据此改为
"客户端在 `ALT_VIA_DELEGATION` 下用**普通名**"（`otbn_delegation_test.c` 顶部 + bench/soak
同规则）。服务端不变（本地 OTBN 对拍）。

**项清单（25 项）**：default 矩阵 24 项（t01–t16c、t17/t19/t21、t80、t88/t89 全经**普通名→代理**）
\+ 新项 **`t90_plain_api_mailbox_bound`**（客户端断言 / 服务端 SKIP）。
三份项名清单已同步（固件 `ZTEST` 注册 / `run_test.py::TEST_NAMES_BY_CASE` /
`scripts/dump_results.py::TEST_NAMES_BY_CASE`），`cases.yaml` 与 `COVERAGE.md` 已登记。
实测 ELF 注册：客户端 25 项、服务端 25 项（t90 排 t89 之后）。

**判据（能区分"真走代理"与"其实在软件算"）** —— 不靠 `rc==0`：

1. **编译期**：`t90` 内置 `#error` 断言客户端**不得**出现 `CONFIG_LINKEDSEMI_OTBN`
   （本地 OTBN ⇒ 与"走代理不拖本地驱动"矛盾）；实测客户端 `.config` 确无。
2. **运行期（可观测的对核副作用，t80 同款有界探针）**：屏蔽客户端 `otbn_crypto` 通道
   （`mbox_set_enabled(false)`）后，调**普通** `mbedtls_ecdsa_genkey` ⇒ 必须**超时返回非零**；
   软件 ECDSA 会**立即 rc=0** 返回合法密钥对，从而 FAIL 此项。
   实测：`INFO mailbox_bound: returned=1,rc=-116,timeout_ms=3000` + `PASS in 1.005s`
   （≈ `DELEGATE_REPLY_TIMEOUT_MS`）；而同 case 的 `t80`（通道正常）
   `INFO proxy_probe: responded=1,rc=0`。⇒ 普通名**确绑到 mbox**。
3. **负例（防假 PASS）**：t07/t08/t09 的**篡改 r** 必须被拒；`t10` 白名单外曲线必须被拒；
   `t17/t88/t19/t21/t89` 未见异常的无效输入必须被拒。全部 PASS（经代理）。

---

## E. `run_test.py` 多 case 批量缺陷修复

**缺陷**：`main()` 在 `len(cases)!=1` 时对**所有** case 用默认 `TEST_NAMES`、忽略
`TEST_NAMES_BY_CASE` ⇒ 单测项 case（throughput/seq_soak 各 1 项）被按 24 项判 ⇒ **假 FAIL**。
**修复**：把"按 case 取表"下沉到 `l2_spec()._judge`（`TEST_NAMES_BY_CASE.get(ci.case, TEST_NAMES)`），
**单跑与批量同一路径**，离线重放（`regression/replay.py`）同样受益。

**修复前后对照**（离线，复用本轮归档串口日志，走**同一** L2 路径；`tmp/otbn_verify/verify_runtest_fix.py`）：

| case | 修复前（旧 main 逻辑） | 修复后 |
|---|---|---|
| case_throughput | **FAIL**（24 项全 MISSING） | **PASS**（1/1） |
| case_seq_soak | **FAIL** | **PASS**（1/1） |

**批量 == 单跑**：一个 spec 判全部 7 case vs 逐 case 独立 spec ⇒ **0 处不一致**。

**反例（证明修复未放宽校验）**：篡改 `case_default` 串口日志里 `t01` 的 `PASS→FAIL` ⇒
批量判定**仍 FAIL**（clean=PASS / tampered=FAIL）。

**真机上板批量（最强证据）**：`python run_test.py`（无 `--case`，全部 7 case 一次调用，`--skip-build`）
⇒ 控制台 `Tests per case: case_alt_via_proxy=25, case_dead_channel=24, case_default=24,
case_explicit_api=25, case_no_server=24, case_seq_soak=1, case_throughput=1`（**逐 case 各取其表**），
**7/7 PASS**（`s01`、`b01` 单测项 case 均 PASS，旧缺陷下它们会假 FAIL）。
该批量的逐 case 结论与逐 case 单跑**逐项一致**。

**文档**：`qsh-test-run §4.2` 那条"取正确判定 = 逐 case 单跑"的绕法已改为"**已修复**"。
批量写出的非约定聚合行 `otbn_delegation_test_all_cases`（现已正确，PASS 78/124）按既有约定
从 `summary.md` 移除，备份于 `tmp/otbn_verify/removed_all_cases_batch/`。

---

## F. 过时知识更正

- `qsh-test-gen §1.6 第 12 条`（原"调代理 genkey 前必须先 `ecp_group_load`"）已就地改为
  **"[历史陷阱，已修]"**：代理 genkey 现自载曲线，**调用方不再需要预载**；并补充
  "只有面对未含此修复的旧库时才需按老契约预载"。
- `qsh-test-run §4.2`：同上（E 节）。

---

## G. 上板全量（7/7 PASS）

环境（本轮实测，按 banner 复核角色）：CKLink-Lite 在位（COM14）；`DebugServerConsole`
1025 LISTENING；**COM18=MCP2200=cpu2/uart5=CTRL**、**COM16=CH342=cpu1/uart1=TGT**。
判定：每项 = 同名项**双核 PASS**；case = 两核 `PROJECT EXECUTION SUCCESSFUL` + 全部项双核 PASS。

| case | CTRL | TGT | 结果 | 计数 | 备注 |
|---|---|---|---|---|---|
| case_default | SUCCESSFUL | SUCCESSFUL | **PASS** | 24/24 | |
| case_dead_channel | SUCCESSFUL | SUCCESSFUL | **PASS** | 2/24（22 SKIP） | |
| case_no_server | SUCCESSFUL | SUCCESSFUL | **PASS** | 2/24（22 SKIP） | 首跑遇 DS 残留**环境失败**，重启 DS 后复跑 PASS |
| case_throughput | SUCCESSFUL | SUCCESSFUL | **PASS** | 1/1 | `INFO bench` 有数值 |
| case_seq_soak | SUCCESSFUL | SUCCESSFUL | **PASS** | 1/1 | `INFO seq_soak: iterations=160,proxy_calls=640` |
| case_explicit_api | SUCCESSFUL | SUCCESSFUL | **PASS** | 24/25 | `t16d` 客户端 PASS / 服务端 SKIP |
| **case_alt_via_proxy** | SUCCESSFUL | SUCCESSFUL | **PASS** | 24/25 | **新增**；`t90` 客户端 PASS / 服务端 SKIP |

**与基线（`reports/2026-10-12-2/`）对比**：6 个既有 case 逐项一致；`case_alt_via_proxy` 新增。
关键项：`t16d` client `mbedtls_rc=0,explicit_rc=0`；`t90` client `returned=1,rc=-116`（1.005s）
/ server SKIP；`t80` `responded=1,rc=0`。

---

## 5. 本轮发现（两处真实缺陷 / 漂移，均**只报未改**）

**① `ecdsa_alt.c` 的 `mbedtls_ecdsa_genkey` 不自行载入曲线（与标准 mbedtls 契约不符）**
`modules/crypto/mbedtls/library/ecdsa_alt.c:350` 的 genkey 只按 `gid` 取 `otbn_curve_info`、
只填 `d`/`Q`，**不调 `mbedtls_ecp_group_load(&ctx->grp, gid)`**（标准 `mbedtls_ecdsa_genkey`
会）。⇒ 任何紧接着读 `ctx->private_grp`（`N`/`nbits`/`check_privkey`/`check_pubkey`）的调用方，
在本核 OTBN 路径上必须自行预载。本轮实测：去掉测试侧预载后**服务端**整片 FAIL（N==0）。
这与本轮刚在**代理**（`mbox.c`）修掉的契约缺口**同源**——修复只覆盖了代理一侧，**本地 ALT 一侧仍缺**。
（该文件在"勿改"清单内，故仅报告。）

**② 委托样例 `mbedtls_client` 的普通名 API 静默从"代理"退化为"软件"**
样例 `boards/lsqsh_evb_cpu2.conf` 只有 `CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT=y`、
无 ALT 项。旧语义下 CLIENT **单独**即定义 ALT 宏 ⇒ `mbedtls_ecdsa_*` = 代理；新语义下
CLIENT **不再**定义 ALT ⇒ `mbedtls_ecdsa_*` = **软件**（实测 `.config`：
`MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_NONE=y`；`nm`：`ecdsa.c.obj` 提供普通名，`mbox.c.obj`
只提供显式名）。样例源码 `src/ecdsa_test.c` 正是按**普通名**调用 ⇒ 其"代理"语义已失效。
**修复建议**：样例加 `CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION=y`
（或改调显式名）。（`otbn_delegation_test` 的客户端因走**显式名**不受影响；见 §D。）
**受影响的既有 conf**：全仓扫描 `..._DELEGATION_CLIENT=y` 且无 ALT 项的仅此**样例**
（810 工程已按角色迁移）。

---

## 6. 未验证 / 不确定项（显式）

1. **矩阵仅编译级**：第 1/3/4 格**未上板**（仅对象级符号归属 + 链接成功）；上板只覆盖第 5 格
   （`case_alt_via_proxy`）与第 2 格（`case_alt_via_proxy` 的服务端…实为第 3 格本地 OTBN）。
   第 1 格（纯软件）无 810 case 覆盖。
2. **样例 @1os 失败未闭环**：判定为**先于本轮**的板级/DT 缺口（build 目录从无 elf、
   `zephyr,flash` chosen 在 cpu2-1os 缺定义）；未去修该缺口，也未确认样例"正规"构建入口
   （是否本就该用 @2os）。@2os 下两样例均编过。
3. **`ecdsa_alt.c` 契约缺口只报未改**（§5 ①，属"勿改"清单文件）。
4. **`case_no_server` 首跑环境失败**（DS 残留，`[ENV] restarting DebugServer` 后仍 0 输出）——
   复跑 PASS；未深究残留来源（疑早前预检 detach 的滞后效应）。
5. **`t90` 的通道屏蔽是"运行期副作用"**：它跑在最后（名序 t90 最大）；未验证屏蔽后不恢复对
   套件收尾/后续是否有影响（本 case 后续无项，且 `PROJECT EXECUTION SUCCESSFUL` 正常）。
6. **`RESTARTABLE` 只到编译级**：B1/B4 编过但**未上板运行**。

---

## 7. 留档与产物

- 矩阵/RESTARTABLE/样例构建：`tmp/otbn_verify/mbuild/{cell1..cell5,mutual_excl,b1..b4,zm_*,smp2_*}`；
  脚本 `tmp/otbn_verify/build_matrix.sh`、`build_samples_2os.sh`
- 构建日志：`tmp/otbn_verify/log/*.log` + `tmp/otbn_verify/build_matrix_driver.log` + `log/summary.txt`
- `run_test.py` 离线验证：`tmp/otbn_verify/verify_runtest_fix.py`
- 上板逐 case 归档：`reports/2026-10-13-1/otbn_delegation_test_case_*/`（serial_CTRL/TGT.log + report.md/json）
- 批量运行日志：`tmp/otbn_verify/batch_run.log`；被移除的聚合产物：`tmp/otbn_verify/removed_all_cases_batch/`
- 逐 case 驱动：`tmp/otbn_verify/run_percase.sh`（+ `run_case_*.log`, `percase_summary.txt`）
- **未用** `git checkout/restore/stash/reset`；未改本轮 zephyr 侧五个已改文件。

---

*本报告为 2026-10-13 会话（`reports/2026-10-13-1/`）的一部分。*
