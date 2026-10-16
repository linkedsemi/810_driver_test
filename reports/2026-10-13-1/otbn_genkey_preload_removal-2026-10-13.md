# OTBN genkey 契约修复复验 + 移除测试侧预载（证伪性验证）（2026-10-13）

> 归档：`reports/2026-10-13-1/`（当天单份权威）。本文件为本轮（移除本核侧预载 → 上板全量
> 复跑 → 证明 `ecdsa_alt.c` 本核 ALT 契约修复生效）的权威报告。
> 同日早前的 `otbn_kconfig_choice_and_framework_fix-2026-10-13.md` 其中 **§C「genkey_or_fail 的
> 预载不可移除」与 §5① 「本核 ALT 一侧仍缺」两处结论已由本轮推翻**（该缺口本轮已修）。

---

## 0. 结论（一句话）

**`ecdsa_alt.c` 本核 OTBN `mbedtls_ecdsa_genkey()` 的自载曲线修复生效**：移除全部本核侧
测试预载后，**服务端（本核 OTBN 路径）不再出现 `private_grp.N==0` 的整片 FAIL**；上板全量
`otbn_delegation_test` **7/7 case PASS（双核 `PROJECT EXECUTION SUCCESSFUL`）**，逐项与基线
**0 差异**。⇒ 上一轮"预载不可移除"的判定**已被证伪**，三个 genkey 入口（软件 / 本核 OTBN / 代理）
现行为一致，调用方**不再需要预载**。

---

## A. 证伪性验证：移除本核侧预载 + 服务端干净通过

### A.1 移除的预载（`projects/otbn_delegation_test/tests/src/`）

| # | 文件:位置 | 移除内容 | 依据 |
|---|---|---|---|
| 1 | `otbn_delegation_test.c::genkey_or_fail()`（原 :179） | `mbedtls_ecp_group_load(&ctx->private_grp, gid)`（**主目标**；其注释原载"pre-load stays on purpose"） | 缺口已修 |
| 2 | `otbn_delegation_bench.c`（原 :138） | `zassert_equal(mbedtls_ecp_group_load(&ctx.private_grp, gids[c]), 0, …)` | 其后 warmup 即 `DELEG_GENKEY`，冗余 |
| 3 | `otbn_delegation_soak.c`（原 :111） | `zassert_equal(mbedtls_ecp_group_load(&ctx.private_grp, SECP256R1), 0, …)` | 其后紧接 `DELEG_GENKEY`，冗余 |
| 4 | `otbn_delegation_test.c::run_neg_op()`（原 :597） | `mbedtls_ecp_group_load(&ctx2.private_grp, gid) != 0 \|\|`（与 genkey 同条件） | 其后紧接 `DELEG_GENKEY(&ctx2,…)`，冗余 |

判据统一为：**该处 group 由其后（或同条件）的 genkey 调用载入，故显式载入冗余**。

### A.2 **保留**的一处显式载入（★ 不是 genkey 缺口）

`otbn_delegation_test.c:572` 的 `mbedtls_ecp_group_load(&ctx.private_grp, gid)` **保留**：
`run_neg_op()` 的 `NEG_SIGN_D_ZERO` / `NEG_SIGN_D_N` 分支**直接以手工构造的 `d` 调
`DELEG_SIGN`、不经 `genkey`**（`d = grp.N` 还要求 `grp` 已载入才算"d==n"），故必须显式载入。
这是"不经 genkey 就 sign"的需要，与 genkey 契约无关。

### A.3 上板证据（关键判据）

- **服务端（TGT = cpu1，本核 OTBN）`case_default` 24/24 原生 PASS**，`PROJECT EXECUTION
  SUCCESSFUL` ×1，**零 `Assertion failed` / 零 `N==0`**。
- **`INFO genkey: gid=3,nbits=256`**（TGT）—— `nbits` 非零 = `ctx->private_grp` 已被
  genkey 载入（旧缺口下此处为 `N==0`）。gid=4→nbits=384、gid=14→nbits=256 同。
- 全 7 case 的 TGT 侧：各 24 条 `PASS -`、各恰 1 条 `PROJECT EXECUTION SUCCESSFUL`、
  无 FAIL 行（见 §B 表）。
- 源/产物新鲜度：`otbn_delegation_test.c` 源 mtime 19:22、`bench.c` 19:21、`soak.c` 19:21；
  `case_default` 的 `.obj` mtime 19:30–19:32、`zephyr.elf` 19:31–19:32 ⇒ **产物晚于源**（改动已编进镜像）。
- 源 md5（本轮被测版本）：`otbn_delegation_test.c=fceac2539693c93d101c6dd2ce802469`、
  `bench=96308e3d30e23e3f34a8e3f8dc480a81`、`soak=a2986dca03b80cc9f1bd752ea10c2a06`；
  `modules/crypto/mbedtls/library/ecdsa_alt.c=83f814cf94910c7217dc2c290316292e`（修复在 `:373`）。

---

## B. 上板全量复跑（7/7 PASS，逐项与基线 0 差异）

环境（预检 6 项手动全过；角色按 banner 重判）：CKLink-Lite 在位；`DebugServerConsole` PID 15920
（本轮为清残留**已重启**）；1025 LISTENING；**COM18 = CTRL（cpu2 客户端）**、
**COM16 = TGT（cpu1 服务端）**（二者由 `dbg_run.py` banner 自动发现确认，非沿用旧号；另有
无关口 COM5/COM17）。命令：`run_test.py --skip-build --session-dir reports/2026-10-13-1`
（镜像为本轮 `--build-only` 全量重建产物、7 case 全 OK）。

| case | CTRL | TGT | 结果 | 计数 | 与基线 |
|---|---|---|---|---|---|
| case_alt_via_proxy | SUCCESSFUL | SUCCESSFUL | **PASS** | 24/25（t90 服务端 SKIP） | 逐项一致 |
| case_dead_channel | SUCCESSFUL | SUCCESSFUL | **PASS** | 2/24（22 SKIP） | 逐项一致 |
| case_default | SUCCESSFUL | SUCCESSFUL | **PASS** | 24/24 | 逐项一致 |
| case_explicit_api | SUCCESSFUL | SUCCESSFUL | **PASS** | 24/25（t16d 服务端 SKIP） | 逐项一致 |
| case_no_server | SUCCESSFUL | SUCCESSFUL | **PASS** | 2/24（22 SKIP） | 逐项一致 |
| case_seq_soak | SUCCESSFUL | SUCCESSFUL | **PASS** | 1/1 | 逐项一致 |
| case_throughput | SUCCESSFUL | SUCCESSFUL | **PASS** | 1/1 | 逐项一致 |

**逐项 diff 结果 = 0 处差异**（对基线 = 本轮改动前的 `reports/2026-10-13-1` 同名 case，
已备份 `tmp/otbn_preload_2/baseline_reports/`）。关键项：`t16d` client
`INFO equiv: mbedtls_rc=0,explicit_rc=0` + PASS（server SKIP）；`t90` client
`INFO mailbox_bound: returned=1,rc=-116,timeout_ms=3000` + `PASS in 1.005s`（server SKIP）。

**归档完整性（`scripts/check_archive.py`）**：6/7 case `COMPLETE`；`case_throughput` 报
`ARCHIVE: BAD`，唯一一条 `H1: ZT_BOOT 之前已有 3 行测试项` —— 头 3 行为 **prime 轮 `INFO bench:`**
（`clear()` 与采集线程竞态的**已知口径误报** G-ARCH-H1-1）；capture 轮**完整**
（CTRL/TGT 各恰 1 条 `ZT_BOOT`/`START`/`PASS`/`PROJECT EXECUTION SUCCESSFUL`）⇒ **内容未退化，判定无误**。

---

## C. 知识更正（keeper 通知）

两处已过期文档**已派 keeper 更正**（本轮不自行改 skill 文档），要求写清
**"两侧均已修（2026-10-13）、预载不再需要"**：

1. `qsh-test-gen/SKILL.md` §1.6 第 12 条（现文"代理侧已修、**本地 ALT 缺口仍存**…
   **仍必须预载**"）→ 改为两侧均已修、预载不再需要。
2. `debug-knowledge/knowledge/mcu/vendor-alt-contract-mismatch.md`（§处理"一侧修了另一侧
   仍可能缺"）→ 注明本 QSH 实例缺口已闭合；方法学保留。
3. 附带报告（不改）：`ecdsa_alt.c` 新注释引用的 `KNOWN_ISSUES.md #120` **实为
   `MBEDTLS_ECP_RESTARTABLE` 与 ECC ALT 硬冲突**，与 genkey 契约无关 —— 疑似指错号。

**状态：已完成并经直接读文件核实落盘**（keeper 于本轮派发并落地）：

- `qsh-test-gen/SKILL.md` §1.6 第 12 条 → 标题已改 **[历史陷阱，两侧均已修 ★ 2026-10-13]**，
  正文"**三个入口行为一致、调用方不再需要任何预载**" + 证伪实测行 + "**唯一仍需显式载 group
  的路径** = 不经 genkey 直接 sign/verify（`run_neg_op` 的 D_ZERO/D_N 分支）"。**已核实**。
- `debug-knowledge/…/vendor-alt-contract-mismatch.md` §处理 → 新增
  "⚠ **本文件记录的 QSH 实例：缺口已闭合（2026-10-13）** … **凡经 `genkey` 的路径都不再需要预载曲线**"。
  **已核实**。
- 新增 `KNOWN_ISSUES.md` **#121**（该 genkey 契约条目：两侧已修 + 证伪实测）；#120 处并已加
  "非本契约缺口 ⇒ 应改指 #121" 的校正说明。

---

## D. 委托样例客户端（编译级）

`mbedtls_client` 的 `boards/lsqsh_evb_cpu2.conf` 已加
`CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION=y`（用户改动）。本轮以
`west build -p always -b lsqsh_evb@2os/lsqsh/cpu2 … -DCONF_FILE="prj.conf;boards/lsqsh_evb_cpu2.conf"`
重建（`tmp/otbn_preload_2/smp2_client/`，exit=0）：

- `.config`：`…_ALT_VIA_DELEGATION=y`、`CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT=y`、
  `…_ALT_NONE is not set`、本核入口 `…SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT is not set`。
- 符号（`nm`）：普通名 `mbedtls_ecdsa_{genkey,sign,verify}` @ `0x10002396/39a/39e`
  （**相邻 4 字节 = 转发桩**），代理体 `mbedtls_ls_otbn_ecdsa_*` @ `0x10001f20/206c/21fa`（大函数体）。
  ⇒ 普通名**确为** mbox 转发桩，"静默退软件"在该配置下**结构上不可能**。
- **运行时未上板**（见 §E）。

---

## E. 现仍需显式预载的路径（回答"我预期一条都不需要"）

**凡经 genkey 的路径 → 一条都不需要**（三个入口均自载曲线）。

**唯一仍需显式载 group 的地方**（因**不经 genkey**，与本契约无关）：
`otbn_delegation_test.c:572`（`run_neg_op` 的 `NEG_SIGN_D_ZERO`/`NEG_SIGN_D_N`：直接以手工 `d`
调 `DELEG_SIGN`）。除此以外，测试源码中已无其它 `mbedtls_ecp_group_load`（`:505` 是注释）。

---

## F. 未验证 / 不确定项（显式）

1. **样例客户端运行时未上板**：`case_alt_via_proxy`（810）已**运行期**证明"普通名 → 代理"
   同一代码路径（`t90` 判别器，PASS）；样例二进制本身**未烧录**（样例非 810 工程，需另做
   双核 RAM 手动烧录，非低成本）。⇒ 样例仅**编译级**证据（§D）。
2. **keeper 更正结果**：已落盘并经直接读文件核实（§C）；keeper 的形式化回报未在报告冻结前返回，但三处落点内容已确认。
3. **`case_throughput` 归档 `H1` 误报**：按已知口径判**无内容退化**，但**未**去修 `clear()`
   竞态本身（属框架侧既有问题，不改）。
4. 本轮用 `--skip-build` 复用 `--build-only` 的镜像（同批、源未漂移），**未**逐 case 二次全量重建。

---

## G. 留档与产物

- 本轮报告：本文件；汇总：`reports/2026-10-13-1/summary.md`（已移除非约定聚合行
  `otbn_delegation_test_all_cases`，备份 `tmp/otbn_preload_2/removed_all_cases/`）
- 上板逐 case 归档：`reports/2026-10-13-1/otbn_delegation_test_case_*/`（serial_CTRL/TGT.log + report.{md,json}）
- 基线快照（改动前同名 case）：`tmp/otbn_preload_2/baseline_reports/` + 逐项台账
  `tmp/otbn_preload_2/baseline_items.txt`
- 构建：`tmp/otbn_preload_2/build_only.log`（7 case 全 OK zero error）
- 全量运行：`tmp/otbn_preload_2/full_run.log`
- 样例编译：`tmp/otbn_preload_2/smp2_client/`、`tmp/otbn_preload_2/smp2_client_build.log`
- **未用** `git checkout/restore/stash/reset`；**未改** zephyr/mbedtls 侧已改文件（`ecdsa_alt.c` 等）。

---

*本报告为 2026-10-13 会话（`reports/2026-10-13-1/`）的一部分。*
