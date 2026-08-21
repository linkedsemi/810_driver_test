# TRNG 测试覆盖矩阵

- 驱动: `zephyr/drivers/entropy/entropy_ls_trng.c`（DesignWare TRNG，`trng1: dwtrng@40004000`）
- 外部路径: **无**（片内模拟熵源，cpu1 安全域，CLAUDE.md §14 免环境确认）
- 接口: Zephyr entropy API `entropy_get_entropy(dev, buf, len)` / `entropy_get_entropy_isr`（**len 为 uint16_t**，65536 会截断为 0 → 见 ENTROPY_API_UINT16_LENGTH_OVERFLOW.md）
- 关键常量: `GEN_BYTES_PER_CMD=16`（128 bit/命令）、`LS_TRNG_POOL_SIZE=128`、
  `MAX_BITS_PER_REQUEST=2^19 bits=64KB`（触发 ADVANCE_STATE 隐藏分支）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| basic_16 | ls_trng_get_entropy 主循环（pool 充足路径） | 功能通路 | ret==0，输出 16B | API 契约 | ✅ |
| not_all_zero | ls_trng_get_entropy 输出校验 | 统计/性质 | 16B 非全 0 | 熵源基本性质 | ✅ |
| not_all_ff | ls_trng_get_entropy 输出校验 | 统计/性质 | 16B 非全 0xFF | 熵源基本性质 | ✅ |
| isr_busywait | ls_trng_get_entropy_isr(ENTROPY_BUSYWAIT) + send_cmd_busywait 轮询（70 行） | 中断/时序 | ret==0，输出 32B | API 契约 | ✅ |
| large_1024 | ls_trng_get_entropy 跨 pool（1024B>128B 多轮 refill） | 边界长度 | ret==0，输出 1024B | API 契约 | ✅ |
| invalid_args | ls_trng_get_entropy len==0/NULL 分支（132-135 行） | 异常参数 | ret==-EINVAL（两分支） | API 契约 | ✅ |
| monobit_8k | ls_trng_get_entropy 全路径输出统计 | 统计/性质 | ones 占比 46%~54%（8192B=65536 bit，σ≈181，±8σ≈[30146,35389]） | 统计公式（二项分布） | ✅ |
| distinct_32 | ls_trng_get_entropy 两次获取 | 统计/性质 | 两 32B 块不相同 | 熵源不重复 | ✅ |
| throughput_64k | 8192B 请求=2^19 bits → trng_issue_next ADVANCE_STATE 分支（85-88 行）+ ISR skip_next（105-107 行）；8×8192 分片（uint16_t 长度截断规避） | 边界长度+中断/时序 | 64KB 总计 ret==0，无跳过块；mbps 上报 INFO | API 契约 + §13.4 | ✅ |

## 预期数据说明

- TRNG 输出为随机数，**无黄金向量**（每次采样不同），确定性判定用：
  - 错误码契约：`ret==0` / `-EINVAL`（异常参数）
  - 统计门限：monobit 二项分布 B(65536, 0.5)，σ=√(n·p·q)=128，±8σ 门限
    [65536/2−8·128, 65536/2+8·128] = [30146, 35389]（误报概率 ~1.2e-15）
  - 非退化性质：非全 0 / 非全 F / 两次获取不同块
- 数值实测（吞吐、ones 占比）经 INFO token 上报（§13.4），报告"实际"列对比。
- 状态: ✅ = 2026-08-21 回归通过（9/9 PASS）。
