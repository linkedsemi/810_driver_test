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
差异只在 Kconfig。同名项双核 PASS 才判 PASS。

## 1. case 清单

| case | 目的 | 期望 |
|------|------|------|
| `case_default` | 功能通路 + 接口用法变体 + 契约负例（含 2 个缺陷项） | **FAIL**（t17 等缺陷项 FAIL —— 见 §3） |
| `case_dead_channel` | 客户端通道被 `mbox_set_enabled(false)` 禁用 ⇒ 代理调用无回应 | FAIL（t80 FAIL = 永久阻塞） |
| `case_no_server` | 对端 `..._DELEGATION_SERVER=n`（无 responder）⇒ 代理调用无回应 | FAIL（t80 FAIL = 永久阻塞） |
| `case_throughput` | 吞吐率基准：`-DOTBN_DELEG_BENCH=1` 换成 `tests/src/otbn_delegation_bench.c`（**只跑 b01 一项**） | PASS（测量项：数据在 INFO bench 行） |

> `case_dead_channel`/`case_no_server` 的**功能/负例项在客户端 SKIP**（门控
> `client_proxy_ready()`），只跑 t80；服务端不受影响（其项为本地运算）。

> **`case_throughput` 方法学边界**：两核**同时**跑基准且**共用同一 OTBN** ⇒ 各数值
> 含**互相争用**，`med` 主要反映排队的等待；**只有服务端的 `min_us` 接近"无争用本地
> 成本"**。要分离"代理往返开销"，需**一次只让一核跑**（另一核空转）——当前是
> `med` 之差 ≈ 往返量级（verify 便宜，p256 处 ≈ 4 ms），但**不足以做精细拆分**，
> 属**待改进的测量设计**。

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
| **t17_sign_bad_d_zero** | sign 传 `d=0` | 异常参数 | **rc≠0（应拒绝）** | 标准契约 | ❌ **FAIL 双核：rc=0（被接受）= 产品缺陷** |
| t19_verify_bad_q_off_curve | verify 传 Q=`Q.X+1`（非曲线点） | 异常参数 | rc≠0 | 标准契约 | ⚠️ 服务端 ✅ rc=-19968；**客户端 panic** |
| t21_verify_wrong_keypair | 用另一密钥对的 Q 验签 | 语义通路 | rc≠0 | 标准契约 | ⚠️ **服务端 panic**；客户端未达 |
| t80_proxy_responds_within_timeout | 独立线程发起代理调用 + 超时等待 | 无响应防护 | 3s 内返回（case_default）；否则 FAIL | 契约 | case_default ✅；dead/no_server ❌（阻塞坐实） |
| t88_sign_bad_d_ge_n | sign 传 `d=n` | 异常参数 | rc≠0 | 标准契约 | ❌ **产品缺陷**（run1 服务端 rc=0；另有 panic） |
| t89_verify_bad_q_infinity | verify 传 Q=无穷远点（Z=0） | 异常参数 | rc≠0 | 标准契约 | ❌ **服务端 panic** |

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
t17_sign_bad_d_zero | !none | contract:断言 rc≠0（实测 rc=0 = 缺陷）
t19_verify_bad_q_off_curve | !none | contract:断言 rc≠0
t21_verify_wrong_keypair | !none | contract:断言 rc≠0
t80_proxy_responds_within_timeout | !none | contract:断言"超时内返回"
t88_sign_bad_d_ge_n | !none | contract:断言 rc≠0（实测 rc=0 = 缺陷）
t89_verify_bad_q_infinity | !none | contract:断言 rc≠0
```
