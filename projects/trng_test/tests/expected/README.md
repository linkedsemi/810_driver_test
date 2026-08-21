# expected/ 说明（TRNG 工程）

TRNG 输出为随机数，**无黄金向量可放**（每次采样不同，NIST 向量不适用）。
确定性判定改用以下预期（已固化在固件断言 + COVERAGE.md 预期列）：

| 判定类型 | 预期 | 实现位置 |
|---|---|---|
| 错误码契约 | ret==0 / -EINVAL | trng_test.c 各测试 |
| 统计门限 | monobit ones ∈ [30146, 35389]（B(65536,0.5) ±8σ） | monobit_8k |
| 非退化性质 | 非全 0、非全 F、两块不同 | not_all_zero/ff、distinct_32 |
| 数值实测 | 吞吐 mbps、ones 占比 → INFO token 上报 | throughput_64k / monobit_8k |

未来若加确定性向量类测试（如 TRNG 作为确定性 PRNG 播种、DRBG 向量），
生成脚本 `scripts/gen_vectors.py` 产出的头文件放本目录。
