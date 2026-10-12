# expected/

本工程的预期数据全部是**结构性不变量**，由 CPU 侧 mbedtls bignum 在固件内独立复算
（`otbn_keygen_test.c::check_key_consistency`），不另存向量文件：

- `n == p*q`
- `p`、`q` 为素数（CPU 侧 Miller-Rabin，16 轮）
- `p % 4 == 3`、`q % 4 == 3`（`otbn_rsa_gen_candidate` 的 `|= 0x03` 约束）
- `e*d ≡ 1 (mod lcm(p-1, q-1))`，`e = 65537`

确定性项（`rsa_2048_deterministic`）的预期 = **自身两次运行的逐位自洽**（同 seed 的固定
RNG 流 ⇒ 相同密钥对），无需外部黄金向量。

即：本工程**没有** NIST/外部黄金向量；OTBN 适配接口的独立 KAT 见兄弟工程
`projects/otbn_test/` 与 `projects/mbedtls_test/`（RSA modexp 向量）。

## ref_vector.h —— 客户 TPM 参考向量（`ref_vector_kat` 用例）

`ref_vector.h` 是**客户 TPM（TCG 参考实现，deterministic 路径）产出的 RSA-2048 密钥对**，
小端，供 `otbn_rsa_key_from_pq()` 的逐字节 KAT（唯一外部独立来源的期望）。
来源：`rsa_keygen/rsa2048密钥相关.txt`（本仓库，主机侧已验证 N=P*Q、qInv=Q⁻¹ mod P、
dP/dQ 与 `d=e⁻¹ mod lcm(P-1,Q-1)` 一致、4 次重复逐字节相同）。

- 数组：`ref_p_le[128]` `ref_q_le[128]` `ref_n_le[256]` `ref_d_le[256]`，
  另附 `ref_dp_le`/`ref_dq_le`/`ref_qinv_le`（供参考，本用例未使用）。
- 文件为**原样拷贝**（sha1 与来源一致），未改任何期望字节。
- 关键事实：`P ≡ 3 (mod 4)`、`Q ≡ 1 (mod 4)`。

