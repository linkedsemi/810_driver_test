# otbn_delegation_test 测试覆盖矩阵

**被测对象**：OTBN ECDSA **delegation**（代理）通路 —— cpu2（controller，
uart5）的 `mbedtls_ecdsa_{genkey,sign,verify}` 是**代理桩**，把每次运算经
`otbn_crypto` mbox 通道发给 cpu1（target，uart1）；cpu1 执行**真实** OTBN
ECDSA 并把结果回填。被测/依赖源码：

- `zephyr/soc/linkedsemi/lsqsh/otbn/mbedtls/mbedtls_otbn_crypto_mbox.c`
  （CLIENT 桩；SERVER 消费体；协议 `otbn_delegate_params`）
- `modules/crypto/mbedtls/library/ecdsa_alt.c`（SERVER 侧真实 OTBN ECDSA ALT）
- 曲线白名单 = `SECP256R1 / SECP384R1 / SM2`

**双核拓扑（片内，无外部接线）**：

| 核 | 角色 | 控制台 | mbedtls_ecdsa_* 后端 |
|----|------|--------|----------------------|
| cpu2 | controller = delegation **CLIENT** | uart5 | 代理桩（mbox round-trip 到 cpu1） |
| cpu1 | target = delegation **SERVER** | uart1 | 本地 OTBN ECDSA（同时响应 cpu2 请求） |

**用例模型**：两核编译**同一份**测试体（`tests/src/otbn_delegation_test.c`），
差异只在 Kconfig（`case_explicit_api` 另经 `-DOTBN_DELEG_EXPLICIT_API` 在客户端
切换被测接口名为显式 `mbedtls_ls_otbn_ecdsa_*`，见 §1）。同名项双核 PASS 才判 PASS。

## 1. case 清单

| case | 目的 | 期望 |
|------|------|------|
| `case_default` | 功能通路 + 接口用法变体 + 契约负例（含 2 个缺陷项） | **FAIL**（t17 等缺陷项 FAIL —— 见 §3） |
| `case_dead_channel` | 客户端通道被 `mbox_set_enabled(false)` 禁用 ⇒ 代理调用无回应 | FAIL（t80 FAIL = 永久阻塞） |
| `case_no_server` | 对端 `..._DELEGATION_SERVER=n`（无 responder）⇒ 代理调用无回应 | FAIL（t80 FAIL = 永久阻塞） |
| `case_throughput` | 吞吐率基准：`-DOTBN_DELEG_BENCH=1` 换成 `tests/src/otbn_delegation_bench.c`（**只跑 b01 一项**） | PASS（测量项：数据在 INFO bench 行） |
| `case_throughput_iso_proxy` | **隔离**吞吐基准（**代理侧**）：同样换 bench 源码，但**只有 cpu2 跑基准（代理路径）**，cpu1 加 `-DOTBN_DELEG_BENCH_HOLD=1` **空转**（只由 boot 期的 delegation 线程应答 cpu2 的 mbox，自己不跑基准）⇒ 消除 `case_throughput` 的跨核争用。配合 `_iso_local` 求代理往返开销 | PASS（cpu2 出 9 组 `INFO bench:`；cpu1 出 `INFO b01_throughput: role=hold`） |
| `case_throughput_iso_local` | **隔离**吞吐基准（**本地侧 = 直接 ALT 基线**）：**只有 cpu1 跑基准**，其 `DELEG_*` = `mbedtls_ecdsa_*` = 本地 OTBN（`ecdsa_alt.c`）；cpu2 加 `-DOTBN_DELEG_BENCH_HOLD=1` **空转**（通道在、但**不发任何请求**）⇒ cpu1 的 OTBN 只被本地基准独占 | PASS（cpu1 出 9 组 `INFO bench:`；cpu2 出 `INFO b01_throughput: role=hold`） |
| `case_throughput_iso_local_hwrng` | 同 `_iso_local`，但基准的 `genkey/sign` 用**硬件熵源**（`-DOTBN_DELEG_BENCH_HWRNG=1`）而非内置 LCG —— 即**代理服务端 handler 实际所用的那个 RNG**（`ecdsa_alt.c` 每次 genkey/sign 取 `curve_size` 字节 `f_rng`，verify 不取）。**代理 − 本 case = 纯总线往返**；代理 − `_iso_local` 才暴露其中多少来自熵源 | PASS（cpu1 出 9 组 `INFO bench:`；cpu2 出 `role=hold`） |
| `case_entropy_iso` | **熵源取数计时**：cpu1 用 `-DOTBN_DELEG_BENCH=1 -DOTBN_DELEG_BENCH_ENTROPY=1` 计时 `DT_CHOSEN(zephyr_entropy)`（= `trng0`，即代理服务端 handler 实际取数用的那个设备）的**两条取数路径** —— 阻塞版 `entropy_get_entropy()`（mutex + pipe，睡等 ISR 喂 2 字节）与 busywait 版 `entropy_get_entropy_isr(..., ENTROPY_BUSYWAIT)`（关中断、寄存器轮询）；cpu2 加 `-DOTBN_DELEG_BENCH_HOLD=1` 空转。矩阵 `{entropy_block, entropy_busy} × {1,16,32,48} B`，每格 N=20、预热 3，**正反两遍**（pass1 block→busy、pass2 busy→block）以暴露顺序偏置。见下方法学注 | PASS（cpu1 出 16 组 `INFO bench:` 计时行；cpu2 出 `INFO b02_entropy: role=hold`） |
| `case_explicit_api` | 显式入口：客户端按名调用 `mbedtls_ls_otbn_ecdsa_{genkey,sign,verify}`（共享体，`-DOTBN_DELEG_EXPLICIT_API=1`）+ 同镜像等价项 t16d；应用侧 `#include "mbedtls_otbn_ecdsa_mbox.h"` | 与 `case_default` 同形（t16d 客户端 PASS / 服务端 SKIP） |
| `case_alt_via_proxy` | 普通入口：客户端（cpu2）配成「入口=走代理」（`CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION=y`，见 controller.conf），测试体按名调用 **普通 `mbedtls_ecdsa_*`** —— 该配置下它们就是 `mbox.c` 的转发桩（共享体 `-DOTBN_DELEG_VIA_PROXY=1`） | 与 `case_default` 同形（同一 mbox 桩，只是经普通名抵达）+ t90 客户端 PASS / 服务端 SKIP |
| `case_rsa_delegation` | **RSA 族**：客户端（cpu2）显式调 `mbedtls_ls_otbn_rsa_modexp_encrypt()/_decrypt()`，服务端（cpu1）须开 `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT=y`（见 target.conf）用**本地** OTBN RSA 应答并自测。**独立源文件** `-DOTBN_DELEG_RSA=1`（`tests/src/otbn_delegation_rsa.c`，与 `case_throughput` 换 bench 源码同式；因 `r*` 链接序在 `t*` 之前、r90 屏蔽通道会破坏 ECDSA 矩阵，**不得**与 ECDSA 体同源） | r01 2048 位往返 m'==m / r02 4096 位往返（双核 PASS）；r03 非支持模数被拒（双核 PASS）；r90 屏蔽通道后显式 RSA 调用**必须超时非零**（客户端 PASS / 服务端 SKIP） |

> `case_dead_channel`/`case_no_server` 的**功能/负例项在客户端 SKIP**（门控
> `client_proxy_ready()`），只跑 t80；服务端不受影响（其项为本地运算）。
>
> **`case_alt_via_proxy` 的独立价值 = t90**：其余项与 `case_default` 到达的是**同一个**
> mbox 桩（默认 case 经显式名、本 case 经普通名），仅"到达路径"不同；正确的转发结果
> 本身**无法**区分"真走代理"与"客户端悄悄退回本地软件"（软件 ECDSA 也会给出合法密钥对）。
> t90 用**屏蔽通道后普通名 genkey 必须超时非零**这一软件不可能有的可观测行为来封口。

> **`case_throughput` 方法学边界**：两核**同时**跑基准且**共用同一 OTBN** ⇒ 各数值
> 含**互相争用**，`med` 主要反映排队的等待；**只有服务端的 `min_us` 接近"无争用本地
> 成本"**。要分离"代理往返开销"，需**一次只让一核跑**（另一核空转）——当前是
> `med` 之差 ≈ 往返量级（verify 便宜，p256 处 ≈ 4 ms），但**不足以做精细拆分**，
> 属**待改进的测量设计**。
>
> **→ 已按上条落地隔离 case（2026-10-16）**：`case_throughput_iso_proxy`
> （cpu2 跑代理、cpu1 空转）、`case_throughput_iso_local`（cpu1 跑本地、cpu2 空转）、
> `case_throughput_iso_local_hwrng`（同前者但用硬件熵源）。每个 case **只有一核**在碰
> OTBN，另一核通过 `-DOTBN_DELEG_BENCH_HOLD=1`（bench 源码里 `#if OTBN_DELEG_BENCH_HOLD`
> 的 hold 分支）只注册一个**同名** `b01_throughput` 占位项（输出 `role=hold`、平凡 PASS），
> 使双核矩阵同名项齐全。`case_throughput` 保留不动，供"含争用"口径对照。
>
> **★ 代理 vs 本地之差不是单一量（2026-10-16 实测）**：`ecdsa_alt.c` 的 `genkey`/`sign`
> 每次从 `f_rng` 取 `curve_size` 字节，**`verify` 不取**（`ecdsa_alt.c` 无 f_rng 调用）。
> 代理路径的服务端 handler 传的是**硬件熵源** `ecdsa_server_get_random()`
> （→`entropy_get_entropy`），而 bench 的本地路径传的是**内置 LCG**（免费）。
> ⇒ 直接相减会把"熵源成本"当成"代理开销"：实测 `genkey`/`sign` 的差值随 `curve_size`
> 线性增长（P256/SM2 32B ≈ 61 ms、P384 48B ≈ 90 ms ≈ 1.9 ms/字节），
> **`verify`（无熵、无回传数据体）差值仅 ≈ 60~85 µs —— 那才是纯往返**。
> 故三列解读：`iso_proxy` = 代理端到端；`iso_local_hwrng` = 同熵源的直接 ALT；
> **两者之差 = 纯总线往返**；`iso_local`（LCG）= 纯 OTBN 计算（无熵、无往返）。
>
> **计时口径**：`k_cycle_get_32()` 差值（600 MHz 硬件周期计数）换算 µs；
> `BENCH_N=20` 实测点、`BENCH_WARMUP=3` 次预热（吸收 OTBN 固件装载/会话初始化）。
> **回绕**：`k_cycle_get_32()` 在 `2^32 / 600 MHz ≈ 7.158 s` 回绕；单次 ECDSA 运算
> 实测为 ms 量级（≪7.158 s），故单点差值不受回绕影响；数组元素为 `uint32` 周期差，
> 若单点逼近 7.158 s 会呈大数并被 max 暴露（本批未见）。
>
> **★ `case_entropy_iso` 计时口径（2026-10-16）**：只测 `trng0` 的**两条取数 API**，
> 不改任何驱动代码。`trng0`（`trng@400a2000`）的 compatible 是 `linkedsemi,ls-apb-trng`
> ⇒ 实际服务它的是 `zephyr/drivers/entropy/entropy_ls_apb_trng.c`（**不是**
> `entropy_ls_trng.c` —— 那个绑 `trng1: dwtrng` 且 `status="disabled"`）；两条路径
> 结构：阻塞 = mutex + `k_pipe_get` 睡等 ISR 喂 **2 字节/次**；busywait = `irq_lock`
> 关中断 + `trng_poll16` 逐样本轮询。矩阵 `{block, busy} × {1,16,32,48} B`、每格
> N=20、预热 3，**正反两遍**（pass1 block→busy、pass2 busy→block）以暴露顺序偏置
> （busywait 先排空 pipe，轮到第二跑的 API 可能白捡几个残留字节）。回绕同 §计时口径。

## 2. 覆盖矩阵

| 测试项 | 驱动路径 | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|----------|------|----------|----------|------|
| t01_genkey_p256 | 代理桩→mbox op 0x11→server 消费体→本地 OTBN keygen | 功能通路 | rc=0；`0<d<n`；`check_privkey`=0；`check_pubkey`=0；`write_binary` 编码长 `1+2n` 且 `read_binary` 回读 X/Y/Z 一致 | 标准契约 | ✅ PASS 双核 |
| t02_genkey_p384 | 同上 | 功能通路(48B) | 同上 | 标准契约 | ✅ PASS |
| t03_genkey_sm2 | 同上 | 功能通路(SM2) | 同上 | 标准契约 | ✅ PASS |
| t04_sign_p256 | 代理桩 op 0x22 | 功能通路 | rc=0；r,s∈(0,n) | 标准契约 | ✅ PASS |
| t05_sign_p384 | 同上 | 功能通路 | 同上 | 标准契约 | ✅ PASS |
| t06_sign_sm2 | 同上 | 功能通路 | 同上 | 标准契约 | ✅ PASS |
| t07_verify_p256 | 代理桩 op 0x33 | 功能+异常 | 合法=0；篡改 r(`r+1 mod n`)≠0 | 标准契约 | ✅ PASS |
| t08_verify_p384 / t09_verify_sm2 | 同上 | 同上 | 同上 | 标准契约 | ✅ PASS |
| t10_reject_unsupported_curve | 桩白名单分支 / `ecp_group_load` default | 异常参数 | 白名单外曲线 rc≠0 | 标准契约 | ✅ PASS |
| t11_sign_blen_lt_n | `mbedtls_ecdsa_sign` blen=20(<n) | 边界长度 | rc=0；r,s∈(0,n) | 标准契约 | ✅ PASS |
| t12_sign_blen_gt_n | blen=58(>n=48, P384) | 边界长度 | 同上 | 标准契约 | ✅ PASS |
| t13_verify_r_out_of_range | verify 传 r=n | 异常参数 | rc≠0 | 标准契约 | ✅ PASS |
| t14_verify_s_zero | verify 传 s=0 | 异常参数 | rc≠0 | 标准契约 | ✅ PASS |
| t15_concurrent_genkey | 2 线程并发 genkey | 并发 | 两线程均在超时内完成且 rc=0（`client_sem` 串行） | API 契约 | ✅ PASS |
| t16a_sign_blen_zero | sign blen=0 | 边界长度 | rc=0；r,s∈(0,n) | 标准契约 | ✅ PASS |
| t16b_sign_blen_eq_n | sign blen=48(=n, P384) | 边界长度 | rc=0 | 标准契约 | ✅ PASS |
| t16c_repeat_channel_init | 重复 `*_chanels_init()` 后再 genkey | 状态机 | rc=0 | API 契约 | ✅ PASS |
| t16d_mbedtls_and_explicit_agree | 同镜像：`mbedtls_ecdsa_genkey`（转发桩）与 `mbedtls_ls_otbn_ecdsa_genkey`（显式）各跑一次 | 状态机 / 接口 | 两者 rc=0；各自 `check_pubkey`=0 | API 契约 | ⏳ 仅 case_explicit_api：客户端待上板（本轮仅编译）；服务端 SKIP |
| **t17_sign_bad_d_zero** | sign 传 `d=0` | 异常参数 | **rc≠0（应拒绝）** | 标准契约 | ❌ **FAIL 双核：rc=0（被接受）= 产品缺陷** |
| t19_verify_bad_q_off_curve | verify 传 Q=`Q.X+1`（非曲线点） | 异常参数 | rc≠0 | 标准契约 | ⚠️ 服务端 ✅ rc=-19968；**客户端 panic** |
| t21_verify_wrong_keypair | 用另一密钥对的 Q 验签 | 语义通路 | rc≠0 | 标准契约 | ⚠️ **服务端 panic**；客户端未达 |
| t80_proxy_responds_within_timeout | 独立线程发起代理调用 + 超时等待 | 无响应防护 | 3s 内返回（case_default）；否则 FAIL | 契约 | case_default ✅；dead/no_server ❌（阻塞坐实） |
| t88_sign_bad_d_ge_n | sign 传 `d=n` | 异常参数 | rc≠0 | 标准契约 | ❌ **产品缺陷**（run1 服务端 rc=0；另有 panic） |
| t89_verify_bad_q_infinity | verify 传 Q=无穷远点（Z=0） | 异常参数 | rc≠0 | 标准契约 | 见 §3 |
| t90_plain_api_mailbox_bound | 屏蔽客户端 otbn_crypto 通道后，调**普通** `mbedtls_ecdsa_genkey`（case_alt_via_proxy 下它=转发桩） | 无响应防护 / 绑定证明 | 3s 内返回且 rc≠0（转发桩在 `DELEGATE_REPLY_TIMEOUT_MS` 后超时；软件会 rc=0 立即返回） ⇒ 证明普通名确绑到 mbox | 契约 | 仅 `case_alt_via_proxy`：客户端 ✅ / 服务端 SKIP |
| r01_rsa_modexp_roundtrip_2048 | 代理桩 op 0x41/0x42 → server `mbedtls_ls_otbn_rsa_modexp`（本地 OTBN RSA） | 功能通路（数值往返） | `c=m^e mod n` rc=0；`m'=c^d mod n` rc=0；**`m'==m` 逐字节** | 标准契约（固定 2048 位向量） | 双核（client 代理 / server 本地） |
| r02_rsa_modexp_roundtrip_4096 | 同上（4096 位） | 功能通路（数值往返） | 同上 | 标准契约（固定 4096 位向量） | 双核 |
| r03_reject_unsupported_modulus | 桩白名单分支 / `mbedtls_ls_otbn_rsa_modexp` 尺寸门 | 异常参数 | 1024/1536 位 rc≠0（客户端 `-EINVAL` / 服务端 `-ENOTSUP`） | 标准契约 | 双核 |
| r90_rsa_mailbox_bound | 屏蔽客户端 otbn_crypto 通道后调**显式** `mbedtls_ls_otbn_rsa_modexp_encrypt` | 无响应防护 / 绑定证明 | 返回且 rc≠0，且耗时 ≈ 10s 总线超时（`OTBN_DELEG_TIMEOUT_RSA_MS`）⇒ 证明 RSA 调用确走 mbox（软件会 rc=0 立即返回） | 契约 | 仅 `case_rsa_delegation`：客户端 ✅ / 服务端 SKIP |

> **契约负例暴露的产品缺陷族（只报不改）**：vendor OTBN ECDSA ALT **不做输入校验** ——
> 无效私钥 `d`（=0 / ≥n）**被接受并返回成功**；无效公钥（非曲线点 / 无穷远点）、
> 跨密钥对验签会**触发内核 panic**（`ZEPHYR FATAL ERROR 4` / `mcause 11 ECALL`）。
> panic 会**中断该核的 ztest 套件**，故其上其后的项（t21/t80/t88/t89）在多数轮次
> **未执行**——这是产品缺陷的次生后果，非用例缺陷。
>
> 维度对照 §6.5.1：功能通路（3 接口 × 3 曲线）、边界长度（blen ⟨n / ⟩n / =0 / =n）、
> 异常参数（越界 r/s、无效 d、无效 Q、白名单外曲线）、并发、状态机（重复 init）、
> 无响应防护（t80）。外部路径：**无**（片内 mbox）。

---

## 独立预期（机器可读）

> 格式 / 判定口径见 `common/expect_check.py` 文件头；文法真源 `SPEC_REFERENCE.md` §6.5.6。

```expect
# 测试项 | 断言 | 来源标签:说明
t01_genkey_p256 | !none | contract:合取（d 区间 + check_privkey + check_pubkey + 编解码往返），无单标量
t02_genkey_p384 | !none | contract:同 t01
t03_genkey_sm2 | !none | contract:同 t01
t04_sign_p256 | !none | contract:r,s∈(0,n) 合取
t05_sign_p384 | !none | contract:同 t04
t06_sign_sm2 | !none | contract:同 t04
t07_verify_p256 | !none | contract:合取（合法=0 且 篡改≠0）
t08_verify_p384 | !none | contract:同 t07
t09_verify_sm2 | !none | contract:同 t07
t10_reject_unsupported_curve | !none | contract:断言 rc≠0，无固定标量
t11_sign_blen_lt_n | !none | contract:合取（rc=0 且 r,s∈(0,n)）
t12_sign_blen_gt_n | !none | contract:同 t11
t13_verify_r_out_of_range | !none | contract:断言 rc≠0
t14_verify_s_zero | !none | contract:断言 rc≠0
t15_concurrent_genkey | !none | contract:合取（两线程完成 + rc=0）
t16a_sign_blen_zero | !none | contract:合取（rc=0 且 r,s∈(0,n)）
t16b_sign_blen_eq_n | !none | contract:断言 rc=0
t16c_repeat_channel_init | !none | contract:断言 rc=0
t16d_mbedtls_and_explicit_agree | !none | contract:合取（转发桩 rc=0 + 显式 rc=0 + 各自 check_pubkey=0）
t17_sign_bad_d_zero | !none | contract:断言 rc≠0（实测 rc=0 = 缺陷）
t19_verify_bad_q_off_curve | !none | contract:断言 rc≠0
t21_verify_wrong_keypair | !none | contract:断言 rc≠0
t80_proxy_responds_within_timeout | !none | contract:断言"超时内返回"
t88_sign_bad_d_ge_n | !none | contract:断言 rc≠0（实测 rc=0 = 缺陷）
t89_verify_bad_q_infinity | !none | contract:断言 rc≠0
t90_plain_api_mailbox_bound | !none | contract:合取（3s 内返回 + rc≠0；软件会 rc=0 立即返回）
r01_rsa_modexp_roundtrip_2048 | !none | contract:合取（enc rc=0 + dec rc=0 + m'==m 逐字节）
r02_rsa_modexp_roundtrip_4096 | !none | contract:同 r01
r03_reject_unsupported_modulus | !none | contract:断言 rc≠0（1024 与 1536 位各一）
r90_rsa_mailbox_bound | !none | contract:合取（超时内返回 + rc≠0 + 耗时 ≥ 8s；软件会 rc=0 立即返回）
```
