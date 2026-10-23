# OTBN RSA keygen 测试覆盖矩阵

被测对象：OTBN 加速 RSA 密钥生成 —— `zephyr/soc/linkedsemi/lsqsh/otbn/otbn_rsa_keygen.c`
（`otbn_rsa_keygen()` / `otbn_rsa_check_prime()` / `otbn_rsa_key_from_pq()` /
`otbn_rsa_gen_candidate()` / `otbn_rsa_modulus_bytes()` / `otbn_rsa_prime_bytes()`），
以及重构后的 mbedtls keygen hook（`modules/crypto/mbedtls/library/rsa_alt.c` 的
`mbedtls_rsa_gen_key_otbn()`，经 `mbedtls_rsa_gen_key()` 调用，
`CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT=y`）。

片内测试，无外部连接路径（§14 标注"片内"）。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| candidate_residue_and_topbits | `otbn_rsa_modulus_bytes`/`otbn_rsa_prime_bytes` 三尺寸+无效 size 分支；`otbn_rsa_gen_candidate` 的候选位约束（低两位置位、最高两位置位） | 功能通路 / 边界 | 256/384/512、128/192/256、无效=0；低字节 `&3==3`、MSB `&0xC0==0xC0`；同 seed 复现 | API 契约（`otbn_rsa_keygen.h` 宏定义）+ 源码约束 | ⬜ |
| direct_apis_ok | `otbn_rsa_check_prime`（素数→true / 合数→false）、`otbn_rsa_key_from_pq`（复算一致）；`ls_otbn_imem_firmware_get()`==KEYGEN | 功能通路 | check_prime(p)=true、check_prime(2^1024-1)=false；key_from_pq 的 n/d 与 keygen 逐字节相同 | RSA 数学定义 + 合成数 2^1024-1=(2^512-1)(2^512+1) 手算 | ⬜ |
| mbedtls_gen_key_ok | `mbedtls_rsa_gen_key` → `rsa_alt.c::mbedtls_rsa_gen_key_otbn` → `otbn_rsa_keygen`；`mbedtls_rsa_check_privkey` | 功能通路 | 两者 ret==0 | mbedtls API 契约 | ⬜ |
| rng_error_propagates | `otbn_rsa_gen_candidate` 的 `ret = rng(...); if (ret) return ret;` 上抛链（经 `otbn_rsa_find_prime`→`otbn_rsa_keygen`） | 异常参数 | rng 返回 -EIO/-EPERM ⇒ keygen 原样返回对应码 | `otbn_rsa_keygen.h` 错误码契约 | ⬜ |
| rsa_2048_correctness | `otbn_rsa_keygen` 全路径（2048） | 功能通路 | `n==p*q`；p、q 素数；p、q ≡3 mod 4；`e*d≡1 mod lcm(p-1,q-1)` | RSA 数学定义（CPU 侧 mbedtls bignum 独立复算） | ⬜ |
| rsa_2048_deterministic | `otbn_rsa_keygen`（2048）同一固定 RNG 流跑两次 | 统计性质 / 功能通路 | p/q/n/d 两次逐位相同 | 模块设计目标（固定 RNG 流 ⇒ 固定密钥对） | ⬜ |
| rsa_3072_ok | `otbn_rsa_keygen`（3072） | 功能通路 | ret==0；`n==p*q`；p、q ≡3 mod 4 | RSA 数学定义（CPU 复算） | ⬜ |
| rsa_4096_ok | `otbn_rsa_keygen`（4096） | 功能通路 | ret==0；`n==p*q`；p、q ≡3 mod 4 | RSA 数学定义（CPU 复算） | ⬜ |
| session_no_leak | `otbn_rsa_keygen` 连续 10 次 2048（session_acquire/release 配对） | 并发 / 资源清理 | 10 次全部 ret==0（泄漏则第 2 次 -EBUSY） | `otbn_rsa_keygen.c:528/558` session 生命周期契约 | ⬜ |
| unaligned_rejected | `otbn_rsa_is_aligned` 守卫：keygen（p_le/n_le）、check_prime（候选）、key_from_pq（n_le） | 异常参数 | 非 4 字节对齐缓冲一律 `-EINVAL` | `otbn_rsa_keygen.h` 对齐硬约束 | ⬜ |
| ref_vector_kat | `otbn_rsa_key_from_pq`（纯算术）+ `otbn_rsa_check_prime`（P/Q 两个方向）；向量 = 客户 TPM 参考实现 RSA-2048 密钥对 | 功能通路 / 跨来源交叉 | ① 返回 0；② N 与参考逐字节相同；③ d 与参考逐字节相同；④ `check_prime(P)`==true（P≡3 mod 4）；⑤ `check_prime(Q)`==false（**既定约束**，见下） | `tests/expected/ref_vector.h`（客户 TCG 参考实现 dump，主机侧验证过 N=P*Q、qInv、dP/dQ/`d=e⁻¹ mod lcm` 自洽） | ⬜ |
| zzz_modexp_kat_n128b_random | `mbedtls_ls_otbn_rsa_modexp` → 1024 位通用模幂（MODE 0x2a7）；`rsa_otbn_load_app` 首次载入 rsa_imem | 功能通路 / 新档 bring-up | ret==0（非 -ENOTSUP）；输出与 KAT 逐字节相同；fw==RSA_MODEXP | `tests/expected/modexp_vectors.h`（N128B_random） | ⬜ |
| zzz_modexp_kat_n192b_random | 同上 → 1536 位通用模幂（MODE 0x7c3） | 功能通路 / 新档 bring-up | 同上 | `tests/expected/modexp_vectors.h`（N192B_random） | ⬜ |
| zzz_modexp_kat_n256b_random | 同上 → 2048 位通用模幂（既有档回归） | 功能通路 / 回归 | 同上 | `tests/expected/modexp_vectors.h`（N256B_random） | ⬜ |
| zzz_modexp_kat_n384b_hostkat | 同上 → 3072 位通用模幂（既有档回归） | 功能通路 / 回归 | 同上 | `tests/expected/modexp_vectors.h`（主机 `pow()` 独立 KAT） | ⬜ |
| zzz_modexp_kat_n512b_hostkat | 同上 → 4096 位通用模幂（既有档回归） | 功能通路 / 回归 | 同上 | `tests/expected/modexp_vectors.h`（主机 `pow()` 独立 KAT） | ⬜ |
| zzz_modexp_kat_n512b_f4_hostkat | 同上 → 4096 位 F4 模幂（e=65537 分支） | 功能通路 / 回归 | 同上 | `tests/expected/modexp_vectors.h`（主机 `pow()` 独立 KAT） | ⬜ |
| zzz_modexp_kat_n4b_base0 | 128B 模数边界：base=0 | 边界 | 输出 == 0 | 数论定义 | ⬜ |
| zzz_modexp_kat_n4b_base1 | 128B 模数边界：base=1 | 边界 | 输出 == 1 | 数论定义 | ⬜ |
| zzz_modexp_kat_n4b_basenm1 | 128B 模数边界：base=n-1、指数为奇数 | 边界 | 输出 == n-1 | 数论定义 | ⬜ |
| zzz_modexp_kat_n4b_exp0 | 128B 模数边界：exp=0 | 边界 | 输出 == 1（x^0 mod n） | 数论定义 | ⬜ |
| zzz_modexp_kat_n4b_exp1 | 128B 模数边界：exp=1 | 边界 | 输出 == base | 数论定义 | ⬜ |

> 判据独立性：密钥正确性一律由 **CPU 侧 mbedtls bignum** 独立复算（不经被测模块），
> 且"OTBN 真的被用到"额外由 `ls_otbn_imem_firmware_get()==OTBN_FIRMWARE_RSA_KEYGEN`
> 断言（防"静默软件回落"）。
>
> `ref_vector_kat` 的期望来自**外部独立来源**（客户 TPM，非本仓库代码算出）——
> 这是本工程覆盖强度最高的一项：它把 `key_from_pq` 钉死在客户密钥对上。
> ⚠️ 关键事实：参考 P ≡ 3 (mod 4)、**Q ≡ 1 (mod 4)**。⇒ `otbn_rsa_key_from_pq`（纯算术）
> 应逐位产出参考 N/d；但**整条 keygen 全流程结构上不可能产出这对密钥**（候选值流不同 +
> CHECK_PRIME 对 ≡1 mod 4 恒判合数）。用例 ⑤ 即把该接口约束钉成回归 —— **是既定约束，不是 bug**。
>
> 未覆盖（后续补）：
> - `-ETIMEDOUT` / `-EIO` / `-ENODATA`（OTBN 执行错/超时/尝试预算耗尽）—— 需注入
>   OTBN 故障或定向 RNG 使 20000 次内找不到素数，当前无可靠注入点，不造假断言。
> - `otbn_rsa_gen_candidate` 的 rng==NULL / candidate==NULL / 无效 size 参数分支。

---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6。
> 来源标签：`handcalc`(手算) `spec`(外部规范/寄存器或 DT 定义) `golden`(黄金向量)
> `crosscheck`(跨来源交叉) `contract`(API 契约值) `driversrc`(转录自被测驱动实现)
> `samecode`(同源复算，**非独立**) `!none`(无确定预期)。

```expect
# 测试项 | 断言 | 来源标签:说明
candidate_residue_and_topbits | !none | !none:多判据合取（尺寸换算表 + 候选位约束 + 自洽复现），无可落单标量
direct_apis_ok | !none | !none:多判据合取（check_prime true/false + key_from_pq 逐字节复算 + IMEM 固件状态）
mbedtls_gen_key_ok | !none | !none:两项 API 返回码合取（gen_key==0 且 check_privkey==0）
rng_error_propagates | !none | !none:两条错误码上抛合取（-EIO 且 -EPERM）
rsa_2048_correctness | !none | !none:多判据合取（n=p*q + 素性 + 3 mod 4 + e*d≡1 mod lcm）
rsa_2048_deterministic | !none | !none:两次运行逐位自洽（非外部标量预期）
rsa_3072_ok | !none | !none:多判据合取（ret + n=p*q + 3 mod 4）
rsa_4096_ok | !none | !none:多判据合取（ret + n=p*q + 3 mod 4）
session_no_leak | !none | !none:10 次成功次数合取（计数==10）
unaligned_rejected | !none | !none:4 个入口的 -EINVAL 合取
ref_vector_kat | !none | !none:多判据合取（N/d 逐字节比对 + P 判素 + Q 判合数）；向量本体为外部黄金数据，非单标量预期
zzz_modexp_kat_n128b_random | byte_equal == 1 | golden:128B KAT（外部向量文件）
zzz_modexp_kat_n192b_random | byte_equal == 1 | golden:192B KAT（外部向量文件）
zzz_modexp_kat_n256b_random | byte_equal == 1 | golden:256B KAT（外部向量文件）
zzz_modexp_kat_n384b_hostkat | byte_equal == 1 | golden:384B KAT（主机 pow 独立复算）
zzz_modexp_kat_n512b_hostkat | byte_equal == 1 | golden:512B KAT（主机 pow 独立复算）
zzz_modexp_kat_n512b_f4_hostkat | byte_equal == 1 | golden:512B F4 KAT（主机 pow 独立复算）
zzz_modexp_kat_n4b_base0 | byte_equal == 1 | handcalc:0^e mod n == 0
zzz_modexp_kat_n4b_base1 | byte_equal == 1 | handcalc:1^e mod n == 1
zzz_modexp_kat_n4b_basenm1 | byte_equal == 1 | handcalc:(n-1)^奇数 mod n == n-1
zzz_modexp_kat_n4b_exp0 | byte_equal == 1 | handcalc:x^0 mod n == 1
zzz_modexp_kat_n4b_exp1 | byte_equal == 1 | handcalc:x^1 mod n == x
```
