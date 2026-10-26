# perf_test 测试覆盖矩阵

工程定位：**性能吞吐单工程**，把 TRNG / 加密 / DMA / flash(qspi) / mbox 的速率
测量集中到一处。**单个 case，两核分别跑一种引擎**（2026-10-26 探针实测两核均可
访问两引擎；选这个划分是为了避免 `dmac2` 争用）：

| 核 | 引擎 | 内容 |
|---|---|---|
| cpu1 | **硬件** | SHA256/SHA512/SM3（`*_HARDWARE_ALT`）+ AES/SM4 + DMA/flash/mbox/trng |
| cpu2 | **OTBN** | mbedtls OTBN 接口（SHA256/SHA512/SM3）+ 本地 RSA 模幂；另做 mbox echo + flash 代理 |

OTBN 侧**不使用 delegation**；`mbedtls_ls_otbn_rsa_modexp` 是本地后端。

**所有测试数据统一从 cpu1 串口输出**：cpu2 的 result/PERF 行经 `perf_ch9` 送到
cpu1，由 cpu1 打印为 `PERF_TEST:CTRL:<行>`（cpu2 本地只留一行 banner）。

加密尺寸：hashes/ciphers 每个算法测 **1/4/16/32 KB**，每个尺寸一行 `PERF`。

- 板级：`lsqsh_evb@2os`（cpu1 / cpu2）
- 外部接线：**无**（TRNG/crypto/DMA 片内；mbox 走共享 SRAM）
- 输出：每条测量一行 `PERF <module> | <op> | bytes=.. | us=.. | MB/s=.. | KB/s=..`
  （纯操作数用 `ops=.. | ops/s=..`）
- 判定：两核各自 `ZT_BOOT` + 至少 1 项 + 全部项 PASS + `PROJECT EXECUTION SUCCESSFUL`。
  **速率不设硬门限**；各套件的 `zassert` 守住功能通路（返回值 / 数据比对）。

## cpu1（21 项）

| 测试项 | 路径 | 维度 | 功能断言 | 速率输出 |
|---|---|---|---|---|
| `trng1_throughput` | `entropy_get_entropy(trng1)`，32 KB 分 4 片 | 性能 | ret==0 | MB/s、KB/s |
| `trng0_throughput` | `entropy_get_entropy(trng0)`，4 KB | 性能 | ret==0 | MB/s、KB/s |
| `sha256` | mbedtls SHA256（hardware ALT）1/4/16/32 KB | 性能 | starts/update/finish==0 | MB/s ×4 |
| `sha512` | mbedtls SHA512（hardware ALT）1/4/16/32 KB | 性能 | 同上 | MB/s ×4 |
| `sm3` | mbedtls SM3（hardware ALT）1/4/16/32 KB | 性能 | 同上 | MB/s ×4 |
| `sha256_unaligned` | SHA256 输入 `perf_buf + 1`（非 32 B 对齐） | 性能+正确性 | 对齐/非对齐 digest 相同 | MB/s ×4 |
| `sha512_unaligned` | SHA512 输入 `perf_buf + 1`（非 4 B 对齐） | 性能+正确性 | 对齐/非对齐 digest 相同 | MB/s ×4 |
| `sm3_unaligned` | SM3 输入 `perf_buf + 1` | 性能+正确性 | 对齐/非对齐 digest 相同 | MB/s ×4 |
| `aes128_ecb` | AES-128 ECB 1/4/16/32 KB | 性能 | setkey/ecb==0 | MB/s ×4 |
| `aes128_cbc` | AES-128 CBC 1/4/16/32 KB | 性能 | setkey/cbc==0 | MB/s ×4 |
| `aes128_ctr` | AES-128 CTR 1/4/16/32 KB | 性能 | setkey/ctr==0 | MB/s ×4 |
| `sm4_ecb` | SM4 ECB 1/4/16/32 KB | 性能 | setkey/ecb==0 | MB/s ×4 |
| `sm4_ctr` | SM4 CTR 1/4/16/32 KB | 性能 | setkey/setiv/ctr==0 | MB/s ×4 |
| `mem2mem` | dmac1 mem→mem，**1 KB 块**；总量 1/4/16 KB | 性能 | dma_config/start==0、完成、memcmp | MB/s（3 行） |
| `flash1_read` | qspi1 `flash1` rwfs 分区，32 KB | 性能 | flash_area_read==0 | MB/s |
| `flash1_write` | qspi1 rwfs，先擦后写 32 KB | 性能（破坏性） | flash_area_write==0 | MB/s |
| `flash1_erase` | qspi1 rwfs，32 KB | 性能（破坏性） | flash_area_erase==0 | MB/s |
| `flash2_read` | qspi2 `flash2` NOR（SFDP 实测 64 MiB），32 KB | 性能 | flash_read==0 | MB/s |
| `flash2_write` | qspi2 NOR，先擦后写 32 KB | 性能（破坏性） | flash_write==0 | MB/s |
| `flash2_erase` | qspi2 NOR，32 KB | 性能（破坏性） | flash_erase==0 | MB/s |
| `roundtrip` | mbox ch5→cpu2→ch6，500×64 B | 性能 | 发送==0、每帧回环<=1 s、计数一致 | MB/s、frames/s |

## cpu2（11 项）

OTBN 加密（mbedtls OTBN ALT，1/4/16/32 KB，含对齐/非对齐 digest 比对）：

| 测试项 | 说明 |
|---|---|
| `sha256_otbn` / `sha256_otbn_unaligned` | mbedtls SHA256（OTBN），对齐 / `+1` 非对齐 |
| `sha512_otbn` / `sha512_otbn_unaligned` | mbedtls SHA512（OTBN），对齐 / `+1` 非对齐 |
| `sm3_otbn` / `sm3_otbn_unaligned` | mbedtls SM3（OTBN），对齐 / `+1` 非对齐 |
| `rsa2048_modexp` | `mbedtls_ls_otbn_rsa_modexp`（本地 OTBN 后端，**不走 delegation**），ops/s |

伴测/代理：

| 测试项 | 说明 |
|---|---|
| `proxy_erase` | 通过 mbox 代理让 cpu1 擦除 qspi2 NOR 的 4 KB 区；测端到端耗时 |
| `proxy_read` | 代理读取 4 KB；数据分 40 B 帧回传，测端到端耗时 |
| `proxy_write` | 代理写入 4 KB（40 B 帧），再回读校验；测写入端到端耗时 |
| `peer_alive` | echo 线程已启动 + mbox rx 设备就绪，并等满 500 帧回环；伴测在后台回环。 |

以上 11 项的 `PASS/FAIL`、`PERF` 行、`ZT_BOOT CTRL` 与 `PROJECT EXECUTION`
全部经 `perf_ch9` 发给 cpu1；`run_test.py` 把 cpu1 流里带 `PERF_TEST:CTRL:`
的行剥前缀后归到 CTRL 流再判定。

### flash 代理协议（`include/perf_proxy.h`）

- 通道：`perf_ch7`（cpu2 请求 → cpu1）/ `perf_ch8`（cpu1 应答 → cpu2），64 B 帧。
- 操作：PING / READ / WRITE / ERASE；读数据按 40 B/帧回传，最后一帧 `LAST`。
- 目标：qspi2 板载 NOR，偏移 `0x10000`（64 KB），每操作 4 KB。
- **端到端口径**：cpu2 记录的时间包含请求、mbox 数据传输与 cpu1 的 flash 操作；
  cpu1 侧用 `perf_flash_mutex` 与本地 `perf_flash` 套件串行化，避免并发访问 NOR。

## 判定与限制

- 两核测试项**结构上不同名**（被测对象不同），判定按“各核自身全部 PASS”而非
  “同名项双核 PASS”——与 `app_iwdg_cpu2_test` / `i3c_dualcore_test` 同源，
  见 SPEC_REFERENCE §6.2.4。
- 吞吐数值与执行位置有关：本工程用标准 2os（cpu1 代码在 RAM）。若要 XIP 基线，
  需另建 `@2os_cpu1_xip` 变体，数值会不同。
- **dmac1 单块上限**：DT `ctlh-block-ts-mask = 0x7ff` ⇒ 单个 block ≤ 2047 B；
  `mem2mem` 用 1 KB 块累计到 1/4/16 KB，报告的是总字节速率。
- **不编入 GPIO toggle**：`gpio_toggle.c` 的启动 `LOG_INF` 会行内切开 `PERF` 行，
  本工程有意不 include `common_sources.cmake`，只引入 `test_results.c` +
  `ztest_glue.c`。
- **dmac2 争用**：cpu1 的硬件 SHA ALT 用 `dmac2`；cpu2 的 OTBN 路径不用，
  所以这个“cpu1 硬件 / cpu2 OTBN”的划分避免了探针里出现的两核抢 dmac2
  （cpu1 SHA 被拖到 10 s 级）。
- **单出口**：cpu1 是唯一测试数据出口；cpu2 的本地 UART 只有 banner。
  `perf_ch9` 宽 128 B（PERF 行放得下），与 ch5..ch8（64 B）一起占 MBOX 区
  约 2960 B / 4096 B。
- **对齐/非对齐**：SHA256 的快速路径要求 32 B 对齐，SHA512 要求 4 B；
  `*_unaligned` 用 `perf_buf + 1`，并与对齐输入（同一常量内容）的 digest 比对，
  既测速率也防“非对齐返回垃圾但很快”的假性能。参考
  `samples/mbedtls_throughput/test_info/qsh_crypto_throughput_0915.md`。
- 加密引擎选择：SHA/SM3 走硬件 ALT（`*_HARDWARE_ALT`），AES/SM4 走硬件 ALT；
  OTBN 引擎的吞吐见 `projects/otbn_test` 的 `perf_*` 项。
