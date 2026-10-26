# TRNG 测试覆盖矩阵

- 驱动: `zephyr/drivers/entropy/entropy_ls_trng.c`（DesignWare TRNG，`trng1: dwtrng@40004000`）
  ＋ `zephyr/drivers/entropy/entropy_ls_apb_trng.c`（APB ring-oscillator TRNG，`trng0: trng@400a2000`；2026-10-26 加入）
- 外部路径: **无**（片内模拟熵源，cpu1 安全域，CLAUDE.md §14 免环境确认）
- 接口: Zephyr entropy API `entropy_get_entropy(dev, buf, len)` / `entropy_get_entropy_isr`（**len 为 uint16_t**，65536 会截断为 0 → 见 ENTROPY_API_UINT16_LENGTH_OVERFLOW.md）
- 关键常量: `GEN_BYTES_PER_CMD=16`（128 bit/命令）、`LS_TRNG_POOL_SIZE=128`、
  `MAX_BITS_PER_REQUEST=2^19 bits=64KB`（触发 ADVANCE_STATE 隐藏分支）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| basic_16 | ls_trng_get_entropy 主循环（pool 充足路径） | 功能通路 | ret==0，且输出**至少一个字节 ≠ 预置哨兵 0xA5**（真写入） | API 契约 + 哨兵 | ✅ |
| not_all_zero | ls_trng_get_entropy 输出校验 | 统计/性质 | 取熵前 memset(0xA5)；输出**≠0xA5 哨兵**且非全 0 | 熵源基本性质 | ✅ |
| not_all_ff | ls_trng_get_entropy 输出校验 | 统计/性质 | 取熵前 memset(0xA5)；输出**≠0xA5 哨兵**且非全 0xFF | 熵源基本性质 | ✅ |
| isr_busywait | ls_trng_get_entropy_isr(ENTROPY_BUSYWAIT) + send_cmd_busywait 轮询（70 行） | 中断/时序 | ret==0，输出 32B | API 契约 | ✅ |
| large_1024 | ls_trng_get_entropy 跨 pool（1024B>128B 多轮 refill） | 边界长度 | ret==0，输出 1024B | API 契约 | ✅ |
| invalid_args | ls_trng_get_entropy len==0/NULL 分支（132-135 行） | 异常参数 | ret==-EINVAL（两分支） | API 契约 | ✅ |
| monobit_8k | ls_trng_get_entropy 全路径输出统计 | 统计/性质 | ones 占比 46%~54%（8192B=65536 bit，σ≈181，±8σ≈[30146,35389]） | 统计公式（二项分布） | ✅ |
| distinct_32 | ls_trng_get_entropy 两次获取 | 统计/性质 | 两 32B 块不相同 | 熵源不重复 | ✅ |
| throughput_64k | 8192B 请求=2^19 bits → trng_issue_next ADVANCE_STATE 分支（85-88 行）+ ISR skip_next（105-107 行）；8×8192 分片（uint16_t 长度截断规避） | 边界长度+中断/时序 | 64KB 总计 ret==0，无跳过块；mbps 上报 INFO | API 契约 + §13.4 | ✅ |
| block_freq_8k | 驱动输出统计（不触代码分支，测熵源性质） | 统计/性质 | 分 128 块×512bit 的 χ² 落在 [32, 224]（χ²(128) 的 ±6σ；均值 128、sd=√256=16） | NIST SP800-22 §2.2 | ⏳ |
| runs_8k | 驱动输出统计 | 统计/性质 | π 偏差 < 2/√n；游程 z = |V−2nπ(1−π)|/(2√(2n)·π(1−π)) < 6 | NIST SP800-22 §2.3 | ⏳ |
| autocorr_8k | 驱动输出统计 | 统计/性质 | 前 8 个 lag 的 \|R(d)\| < 6/√n = 0.02344 | 自相关 i.i.d. σ=1/√n | ⏳ |
| long_seq_1m | trng_issue_next ADVANCE_STATE + skip_next 分支（85-88 / 105-112 行）**反复 16 次**；ISR pool full → IE 关闭（123-125 行） | 边界长度+中断/时序 | 1 MiB 分 128×8KB 全部 ret==0、无退化块、ones 占比 ∈[44%,56%]；us/bytes 上报 INFO | API 契约 + 二项分布 | ⏳ |
| concurrent_get | ls_trng_get_entropy 的 `k_mutex_lock(K_FOREVER)` 路径被调用，但断言**不覆盖互斥语义** | 并发 **smoke** | 双线程各 8×512B 均 ret==0，两块数据不相同 | API 契约；**不覆盖 mutex**（见下） | ⏳ |
| isr_nonblock | ls_trng_get_entropy_isr 非 BUSYWAIT 分支（`169-174` 行，**先取后使能**）；入参校验（`161-163` 行）；ISR 池满自关中断（`123-125` 行） | 中断/时序+异常参数 | NULL/len==0 → -EINVAL；阻塞读 128B 后**池空 + DONE 中断已关** ⇒ 首调返回 ∈[0,64]（同调内重新使能中断）；`k_msleep(20)` 后**二调 == 64** | **驱动真实语义**：按需驱动 + 池满自关中断（有意设计，非缺陷），**池不会自行回填**；旧口径"回填后一次取满 64B"已作废，见 TRIAGE_MISC_2026-09-18.md §4 | ⏳ |

> 状态标记：`✅` = 已验证（2026-08-21 回归）；`⏳` = **P1a 新增，仅编译通过，待 P6 首次实测**。

## P1a 新增项说明（2026-09-17，只生成+编译通过，未烧录）

- 新增 6 项：`block_freq_8k` / `runs_8k` / `autocorr_8k` / `long_seq_1m` / `concurrent_get` / `isr_nonblock`。
- **统计门限一律用常量展开、不引 libm**（picolibc 下 `sqrt`/`fabs` 的链接依赖不确定）：
  n=65536 固定 ⇒ √n=256、√(2n)=362.038671968 直接写字面量。
- `long_seq_1m` 用 8KB 滚动缓冲分 128 片（1 MiB > SRAM 760KB），逐片非退化检查 + 全局占比门限；
  它是**唯一反复走 ADVANCE_STATE/skip_next 隐藏分支**的用例（每片 8192B = 2^19 bit 恰好触发一次）。
- `isr_nonblock`：驱动是「按需驱动 + 池满自关中断」——ISR 内 `k_pipe_put(K_NO_WAIT)`
  失败（池满）即 `IE &= ~DWTRNG_IE_DONE_Msk`（`entropy_ls_trng.c:123-125`）；阻塞路径
  取满 128B 时池被恰好排空、且中断已在期间被 ISR 关闭 ⇒ 紧随其后的非阻塞首调必然取到 0
  （该分支"先取后使能"，`:169-172`，对本次调用太晚）。
  断言据此拆两步：首调只断言 `∈[0,64]`（它同时会重新使能中断）→ `k_msleep(20)` →
  二调断言 `== 64`。**"池会自由回填"的旧口径已作废**（实测 first got=0）。
- 跳过项：**`health_test_path`** —— 驱动（`entropy_ls_trng.c`）**无 health test / 失败注入逻辑**，
  不存在对应分支，无可测对象。
- 新增用例均使用**独立缓冲区**（`buf_stat` / `buf_long` / `conc_buf` / `buf_isr_nb`），
  不与其他用例共享状态，可单独通过。

## 预期数据说明

- TRNG 输出为随机数，**无黄金向量**（每次采样不同），确定性判定用：
  - 错误码契约：`ret==0` / `-EINVAL`（异常参数）
  - 统计门限：monobit 二项分布 B(65536, 0.5)，σ=√(n·p·q)=128，±8σ 门限
    [65536/2−8·128, 65536/2+8·128] = [30146, 35389]（误报概率 ~1.2e-15）
  - 非退化性质：非全 0 / 非全 F / 两次获取不同块
- 数值实测（吞吐、ones 占比）经 INFO token 上报（§13.4），报告"实际"列对比。
- 状态: ✅ = 2026-08-21 回归通过（9/9 PASS）。

## 修订（2026-09-18，测试有效性审计 A 组 P1-3）

- **`basic_16`**：调前 `memset(buf, 0xA5, 16)`，调后断言至少一字节 ≠ 0xA5。
  原版唯一断言是 `ret==0`、`buf[16]` 是未初始化局部数组、一个字节都不校验 ⇒
  驱动"返回 0 但不写数据（短写/静默成功）"照样 PASS。
- **`not_all_zero` / `not_all_ff`**：二者共用**静态复用**的 `buf_256`，其前 128B 被更早
  执行的 `isr_nonblock`（阻塞读 128B）写过 ⇒ 驱动"返回 0 但不写数据"时会在**别人写的
  旧数据**上判 PASS。改为取熵前 `memset(buf_256, 0xA5, 256)` 并断言输出 ≠ 0xA5 哨兵。
  （仅给 `not_all_ff` 加哨兵而不断言"不等于哨兵"是不够的：0xA5 本身就非 0xFF，
  "非全 0xFF"会恒真 —— 故三处都加了"≠ 哨兵"这一条。）
- **`concurrent_get`**：**覆盖声明降级**。原断言只有"两线程 `ret==0` + 两块数据不同"，
  **删掉驱动的 `k_mutex` 后仍然通过**（`k_pipe` 自带 spinlock，两线程各写自己的
  `conc_buf[idx]`）⇒ 不能证明 mutex 互斥。表内路径列已改为"`k_mutex_lock` 路径被调用，
  但断言不覆盖互斥语义"，覆盖维度改为"并发 smoke"。
  **未加反向证据**的原因：可归因的互斥判据需"同一 buf 被两线程交错破坏"的观测，
  与互斥本身是否生效同义，现有接口下不可得；按审计"二选一"取降级声明。
- 三项新增/收紧断言的论证：
  - 驱动正确时必过：真实 TRNG 每个 16B/256B 输出都是随机字节，等于固定哨兵 0xA5
    的概率为 256^-1（`not_all_*` 为 0xA5^256），实际不可能出现。
  - 典型失效时必不过：驱动返回成功但不写 buffer（短写/池空静默）时输出保持哨兵
    ⇒ 三条"≠ 哨兵"断言全部 FAIL。

---

## trng0（APB ring-oscillator TRNG，2026-10-26 新增）

- 驱动：`zephyr/drivers/entropy/entropy_ls_apb_trng.c`（`linkedsemi,ls-apb-trng`）。
- 与 trng1 是**不同硬件模块、不同驱动**：池 64 B（`CONFIG_ENTROPY_LS_APB_TRNG_POOL_SIZE`），
  ISR 每次 MATCH 只写 2 B；ISR busywait 走 `trng_poll16()` 逐 16-bit 轮询。
- 本镜像的 `zephyr,entropy` 仍指向 trng1，trng0 仅由测试直接驱动。
- 用例名统一加 `apb_` 前缀（`trng0::apb_basic_16` 等）：报告层按裸测试名汇总，
  与 trng1 重名会让后跑的 suite 覆盖先跑的（假 PASS 风险）。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| apb_basic_16 | `ls_apb_trng_get_entropy` 主循环 | 功能通路 | ret==0；输出 ≠ 哨兵 0xA5 | API 契约 | ✅ |
| apb_not_all_zero | 同上 | 统计/性质 | 非全 0 | 熵源性质 | ✅ |
| apb_not_all_ff | 同上 | 统计/性质 | 非全 0xFF | 熵源性质 | ✅ |
| apb_invalid_args | `!buf` / `!len` 入口分支 | 异常参数 | -EINVAL | API 契约 | ✅ |
| apb_isr_busywait | `ls_apb_trng_get_entropy_isr` + `trng_poll16`（BUSYWAIT） | 中断/时序 | ret==32 | API 契约 | ✅ |
| apb_large_1024 | 跨 64 B pool 多轮循环 | 边界长度 | ret==0；非全 0；四分块不全同 | API 契约 | ✅ |
| apb_monobit_8k | 输出统计 | 统计/性质 | ones 占比 46%~54% | 二项分布 | ✅ |
| apb_throughput | 4096 B 计时 | 性能 | ret==0；上报 MB/s / KB/s / Mbps | §13.4 INFO | ✅ |
| apb_distinct_32 | 32×64 B | 统计/性质 | 两两不重复 | 熵源性质 | ✅ |

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 本段是上表「预期数据」列之外的**机读断言**：只登记**不由被测代码算出**的预期
> （手算推导 / 外部规范 / 黄金向量 / 契约值）。没有确定预期的项显式写 `!none`。
>
> ★ 本工程的统计门限**刻意比固件里的门限更紧**（固件为免误报取了很松的 46%~54%、
> 44%~56%，见 `trng_test.c` 注释自述"远松于 ±6σ"）。门限常量（`lo`/`hi`/`gate`）
> 也一并进断言 —— 只校验测得值无法发现"门限被悄悄放宽"。

```expect
# 测试项 | 断言 | 来源标签:说明
monobit_8k | ones in [32000,33536]; percent in [48.83,51.17] | handcalc:二项分布 B(65536,0.5) ⇒ σ=√(n/4)=128，取 ±6σ
throughput_64k | bytes == 65536 | handcalc:8 片 × 8192B 之和（uint16_t 长度上限规避）；mbps 无确定预期 ⇒ 不设门限
block_freq_8k | blocks == 128; chi_square in [32,224]; lo == 32; hi == 224 | handcalc:χ²(128) 均值 128、sd=√(2·128)=16 ⇒ ±6σ=[32,224]
runs_8k | pi in [0.4921875,0.5078125]; z < 6 | spec:NIST SP800-22 §2.3；τ=2/√n=0.0078125，z 取 ±6σ
autocorr_8k | max_abs_r < 0.0234375; gate in [0.02343,0.02344]; lag in [1,8] | handcalc:i.i.d. σ=1/√n ⇒ 6/√65536=0.0234375；lag 取 1..8（前 8 个，1 基）
long_seq_1m | bytes == 1048576; ones_pct in [49.9,50.1] | handcalc:B(8388608,0.5) ⇒ σ_pct≈0.0173%，取 ±6σ≈±0.104%
concurrent_get | ret0 == 0; ret1 == 0 | contract:API 契约（"两块数据不相同"无 INFO 通道，未登记）
isr_nonblock | len == 64; second_got == 64; first_got in [0,64] | contract:驱动语义：阻塞读排空池并关 DONE 中断 ⇒ 首调 0..64（同调内重使能），20ms 后二调必 64
basic_16 | ret == 0 | contract:API 契约（★本项无 INFO 通道 ⇒ 恒为 NO_ACTUAL，是缺口不是"无预期"）
not_all_zero | ret == 0 | contract:API 契约 + 哨兵（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
not_all_ff | ret == 0 | contract:API 契约 + 哨兵（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
isr_busywait | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
large_1024 | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
invalid_args | ret == -EINVAL | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
distinct_32 | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
apb_basic_16 | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
apb_not_all_zero | ret == 0 | contract:API 契约 + 哨兵（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
apb_not_all_ff | ret == 0 | contract:API 契约 + 哨兵（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
apb_invalid_args | ret == -EINVAL | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
apb_isr_busywait | ret == 32 | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
apb_large_1024 | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
apb_monobit_8k | ones in [32000,33536]; percent in [48.83,51.17] | handcalc:二项分布 B(65536,0.5) ⇒ σ=128，取 ±6σ（与 trng1 同名口径）
apb_throughput | bytes == 4096 | handcalc:TRNG0_PERF_BYTES；mb_s/kb_s 无确定预期 ⇒ 不设门限
apb_distinct_32 | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
```
