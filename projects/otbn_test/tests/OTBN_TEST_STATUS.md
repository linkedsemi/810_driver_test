# OTBN 测试状态记录（调试中）

> §7 状态记录文档。定位闭环后拆分为独立 BUG 总结。

## 测试项状态表（final，2026-08-28 21:22，case_default @ lsqsh_evb@2os/cpu1，串口全量采集）

> 修复进展四连：基线 18/25（run18 内存兜底 / rerun1 免烧 / singlepass1
> 单遍逐项一致）→ BUG-3 修复 23/25 → BUG-5 测试修复 24/25 →
> **BUG-2 测试修复 25/25 全 PASS**（api_aes_kat 转 PASS，全部闭环）。

| 测试项 | 结果 | 备注 |
|---|---|---|
| api_sha224/256/sm3/384/512_kat | PASS | OTBN hash 基本路径正常 |
| api_sha256_varlen | PASS（修复后） | 原样本 1024 行向量写错（见 BUG-1） |
| api_aes_kat | **PASS（BUG-2 修复后转 PASS）** | aes_xts vec2 明文向量移植缺 8 字节（见 BUG-2） |
| api_sm4_kat | PASS | |
| api_rsa2048_kat | **PASS（BUG-3 修复后转 PASS）** | 修复前 kat_sign_cmp mismatch（BUG-3） |
| api_rsa_keygen_2048 | PASS | ~1.0-2.8s（高方差） |
| api_rsa_keygen_3072 | PASS | ~2.2-3.1s |
| api_rsa_keygen_4096 | PASS | 19.7s/11200 次 RNG（见 BUG-4） |
| api_ecdsa_p256_kat | **PASS（BUG-5 测试修复后转 PASS）** | 定 k KAT 与 PC 逐位一致（见 BUG-5） |
| api_ecdsa_p384 | PASS | |
| api_ecdsa_sm2 | PASS | |
| perf_sha256_64k / perf_rsa2048_sign / perf_ecdsa_p256 | PASS | |
| mix_sha256_keygen_seq / stream_keygen / keygen_sha256_keygen / sign_sha256_stream | **PASS ×4（BUG-3 修复后转 PASS）** | 修复前全部 `byte[0]:0x6a!=0x24` |
| mix_sha512_stream_keygen / mix_sm3_stream_keygen / mix_ecdsa_sha256_stream | PASS | 一直 PASS |

## BUG-1：mbedtls_test 样本 sha256 varlen 1024 行向量错误（已闭环）

- 现象：api_sha256_varlen FAIL，`single-update mismatch len=1024 off=0`。
- 根因：`samples/mbedtls_test/src/main.c` `test_sha256_various_lengths()` 的
  len=1024 行预期值有 33 个字节（多写一个 0xee，"…80,ee,ea,1e,e0…" 应为
  "…80,ee,a1,e0…"），编译告警 excess elements，截断后前 32B 也是错的。
  该行从未真正通过过。
- 证据：PC 端 `hashlib.sha256(bytes([2])*1024)` =
  `14d6fc84…8980eea1e0db…9d1984`，与表中值不同。
- 修复：本工程 otbn_api_test.c 已改为正确值（PC 复核），修复后上板 PASS。
- 后续：建议同步修 `samples/mbedtls_test/src/main.c` 同源行。

## BUG-2：AES XTS vec=2 mode=0 不匹配（★ 已闭环：测试移植缺字节，硬件正常）

- 现象：aes_xts_test 第 3 组向量（key=0xff,0xfe,0xfd…，encrypt）KAT 不符，
  ret=1（首个 mismatch 即 exit，vec2 dec 未跑到）。vec 0/1 及
  ECB/CBC/CFB/OFB/CTR 全过。
- **根因 = 测试移植缺字节**：`otbn_api_test.c` 的 `aes_test_xts_pt32[2]`
  只有 24 字节（3 行 ×8，缺第 4 行 8 个 0x44），静态数组零填充 → 实际输入
  = `0x44×24||0x00×8`，而黄金密文是 `0x44×32` 的 XTS 结果 → 必 mismatch。
  样本 `samples/mbedtls_test/src/main.c` 同名数组是完整的 32 字节——缺行
  是移植时漏抄（编译无告警：部分初始化零填充合法）。**硬件 AES 完全正常，
  黄金向量完全正确**（用户"只支持部分计算"猜想不成立，问题确在测试侧）。
- **PC 独立验证**（cryptography 库 + 手写 mbedtls 同款 ble GF 乘法）：
  XTS-256(key=fffefdfc…, du=3333333333+0×11, pt=0x44×32) =
  `af85336b597afc…1a227a89` 与测试黄金密文逐位一致；bugged 输入
  `0x44×24||0×8` 得 `af85336b597afc…2fd25393…12cac4d`（块 2 起分叉，
  与"vec2 enc 首个 32B 比对即败"吻合）。
- **修复**：pt32[2] 补上第 4 行 8 个 0x44（与样本 main.c 对齐）。
- **验证（2026-08-28 21:22 回归）**：api_aes_kat 转 PASS，otbn_test
  **25/25 全 PASS**（全部 token 采集，无内存兜底）。
- 教训：向量从样本手工移植后要逐数组核对长度（部分初始化零填充不报错，
  与 BUG-1 同为"样本向量行写错/漏抄"类问题）。

## BUG-3：RSA 运算后 SHA256 结果出错（★ 已闭环：驱动层修复 + 上板验证 2026-08-28）

- 现象：api_rsa2048_kat 的 `kat_sign_cmp mismatch`——RSA encrypt+decrypt
  （OTBN MODEXP 固件）之后紧接着算的 SHA256("hello rsa otbn test") 结果错误：
  自发 sign+verify 自洽通过（用的是同一个错 hash），但与 OpenSSL 黄金签名
  比对失败。
- **硬件实锤**：同一次运行串口抓到 `E: otbn cmd error , error bit : 0x8`
  （ERR_BITS 0x8 = ILLEGAL_INSN，OpenTitan OTBN 错误位定义）——hash 操作
  在 RSA modexp 的 IMEM 固件上执行，跑到非法指令。
- 根因链条（与实锤吻合）：
  1. api_sha256_varlen 最后跑过 SHA256 → `otbn_hash.c` 静态缓存
     `s_last_firmware_id = OTBN_FIRMWARE_SHA256`；
  2. test_rsa 的 encrypt/decrypt 走 `rsa_alt.c`，无条件把 IMEM 换成
     RSA MODEXP 固件（不通知 hash 模块）；
  3. test_rsa 中紧随其后的 sha256 调用：`load_firmware()` 发现
     `s_last_firmware_id == ctx->firmware_id`（SHA256）→ **跳过 IMEM 重载**
     → 在 RSA 固件上执行 hash → ILLEGAL_INSN → digest 垃圾。
- **修复（已实施）**：把"当前 IMEM 里的固件"状态上收到 session 层共享——
  `otbn_common.c` 新增 `ls_otbn_imem_firmware_confirm(id)` /
  `ls_otbn_imem_firmware_get()`（IMEM 跨 session 持久，init/deinit 时重置
  UNUSED）；`otbn_hash.c`/`otbn_ecc.c` 删除各自静态缓存（`s_last_firmware_id`
  /`cur_curve`），改为查共享状态；`rsa_alt.c` 在 modexp/keygen 固件写 IMEM
  成功后 confirm。
- **验证（2026-08-28 20:37 回归）**：mix 4 项（sha256_keygen_seq /
  stream_keygen / keygen_sha256_keygen / sign_sha256_stream）+ api_rsa2048_kat
  **全部转 PASS**，`0x6a` ILLEGAL_INSN 特征消失，otbn_test 18/25 → 23/25。
  详细总结见 `BUG3_IMEM_FIRMWARE_STATE.md`。

## BUG-4：RSA-4096 keygen —— 定性为概率性慢，非确定性挂死（rerun1 计时实锤）

- 现象：旧编排下连续四轮（窗口 360s/600s/900s/600s）停在
  `START:api_rsa_keygen_4096`；新编排（4096 末尾）run18 + rerun1 两轮
  均 PASS。
- **rerun1 计时证据（skip-load 免烧重跑，串口心跳 INFO）**：
  `nbits=4096,time_ms=21845,rng_calls=12215` —— **21.8s 完成**，12215 次
  TRNG RNG 调用。不是挂死，是素数搜索慢 + 概率性长尾（候选素数随机游走，
  个别轮次可能数分钟）。
- 参考：otbn_ram 记录只有 RSA-2048 keygen 基线（0.1~3.7s，高方差），
  无 4096 基线；otbn-rsa-keygen-record.md §8 注明 3072/4096 未采基线。
- 旧轮次挂死原因未完全闭环：4096-先行实验也挂过（与前置 keygen 无关），
  但旧编排每轮 4096 之前有 rsa2048_kat 的 0x8 ILLEGAL_INSN（BUG-3），
  OTBN 错误态遗留 + 概率性长尾共同嫌疑。新编排下连续两轮通过。
- 结论：测试保留（末尾跑），超时窗口按 ≥5min 概率长尾设置；单轮 <30s
  属正常。驱动层无需针对 4096 修改（性能优化另行评估）。

## BUG-5：ECDSA P-256 KAT r 不匹配（★ 已闭环：测试移植 bug，固件无问题）

- 现象（run8）：api_ecdsa_p256_kat FAIL（`ecdsa p256 kat r mismatch`），
  随后 api_ecdsa_p384 的 START 再未出现，直到 600s 窗口耗尽。
- **run18 反转**：p256_kat 仍 FAIL（r mismatch 稳定复现），但其后
  p384/sm2/perf **全部 PASS**——p384 挂死在 run18 新编排下不复现。
- **2026-08-28 排查链**：① BUG-3 修复后仍 FAIL → 排除缓存混用；
  ② PC 端 Python ecdsa 复核黄金向量（d/Q/k/hash→r/s）**全部正确**；
  ③ 根因实锤 = **测试移植丢失定 k 语义**：样本 `ls_mbedtls_get_fixed`
  = `memcpy(buf, fix_trng_output, size)`（即准备好的 k_buf），移植时
  被简化成 0xccbbaa 循环 pattern（`otbn_get_fixed`），k 不是 KAT 的 k
  → r 必 mismatch。**固件无问题**。
- **修复**：新增 `otbn_rnd_buf(ctx, buf, size)`（从 ctx buffer 读 k），
  KAT 改传 k_buf 作 p_rng；其余自洽 sign+verify 场景（p384/sm2/perf）
  维持 otbn_get_fixed 不变。
- **验证（2026-08-28 回归）**：api_ecdsa_p256_kat **转 PASS**，
  otbn_test 24/25。**顺带证明 OTBN P-256 固件定 k 签名与 PC（Python
  ecdsa）逐位一致**。p384 挂死自 run18 起未再复现（旧编排 BUG-3 污染
  链连带，已随 BUG-3 修复消除），临时编排可还原。

## 快速验证路径（2026-08-28 实测固化，otbn_test/otbn_selftest 两工程通用）

| 路径 | 耗时 | 用途 |
|---|---|---|
| 内存兜底 dump_results.py | 秒级（0 烧录） | 镜像已在 RAM 时拿全量结果 |
| dbg_run.py --no-load | ~60s（0 烧录） | 免烧重跑，同镜像迭代（set $pc=入口 软复位） |
| 单遍烧录（默认流程） | ~65s（load ~5s） | 冷启动全流程；写包参数 41x 提速后 load 1KB/s→41KB/s |
| 全量回归 run_*_tests.py | ~2-3min | 自动构建+烧录+采集+报告归档（§12） |

三个提速/流程改造的坑已沉淀 CLAUDE.md §10.23（写包 41x）/§10.24（会话内
二次 load 挂死 → 单遍烧录）。selftest 套件全程 8s（30/30 PASS）。

## otbn_selftest 上板结果（selftest_run1，2026-08-28，首次上板）

- 30/30 PASS，`OTBN_SELFTEST:DONE:PASS`，全程 13s（load 5s + 套件 8s）。
- st_entropy NOT_RUN：熵源自检未编入本配置（与 samples 配置一致），非失败。
- 单遍烧录流程 + wpacket 提速首次完整走通，与 otbn_test 同链路。
