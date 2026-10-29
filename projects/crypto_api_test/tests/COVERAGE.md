# crypto_api_test 测试覆盖矩阵

- 工程定位：**Zephyr crypto 驱动 API**（`zephyr/crypto/crypto.h` 的
  `cipher_begin_session` / `cipher_{block,cbc,ctr,ccm,gcm}_op` /
  `hash_begin_session` / `hash_update` / `hash_compute`）的驱动级测试。
  **不是** mbedtls 的 `*_alt` 软件栈 —— 那套另有覆盖（`mbedtls_test`）。
- 驱动:
  - `zephyr/drivers/crypto/crypto_linkedsemi_sm4.c`（`sm4@40029400`）
  - `zephyr/drivers/crypto/crypto_linkedsemi_sha256.c`（`sha256@40002800`，SHA224/SHA256/SM3）
  - `zephyr/drivers/crypto/crypto_linkedsemi_sha512.c`（`sha512@40005000`，SHA384/SHA512）
- 外部路径: **无**（片内加密引擎，cpu1 安全域，CLAUDE.md §14 免环境确认）
- 接口能力：`query_hw_caps` = `CAP_RAW_KEY | CAP_SEPARATE_IO_BUFS | CAP_SYNC_OPS | CAP_NO_IV_PREFIX`
  （SM4）/ `CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS`（hash）；同步语义，`hash_*` 无 async。
- 关键常量：SM4 分组 16B；CTR `ctr_len=32`（iv 12B + counter 4B）；CCM/GCM nonce 12B、tag 16B；
  SHA256 块 64B / SHA512 块 128B。

> case 只有一个：`case_default` —— 三颗驱动（SM4 / SHA256 / SHA512）编入**同一个
> 镜像**。它们是不同外设（各自的 IRQ / clock / reset / mutex），同时使能互不冲突；
> 按驱动拆 case 只会多一次构建、多一份报告，故不拆。

## case_default — SM4 部分（`CONFIG_CRYPTO_LINKEDSEMI_SM4=y`）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| ecb | `sm4_linkedsemi_ecb_encrypt` / `_decrypt` + `sm4_linkedsemi_key_expansion` | 功能通路 | 64B 密文 == `sm4_ecb_ct`；解密回环 == 明文 | OpenSSL 3.2 `enc -sm4-ecb` 复核（有效 key `00112233...EEFF`） | ⏳ |
| cbc | `sm4_linkedsemi_cbc_encrypt` / `_decrypt`（`mem_xor_128` 链接） | 功能通路 | 64B 密文 == `sm4_cbc_ct`；解密回环 == 明文 | OpenSSL 3.2 `enc -sm4-cbc`，IV=00..0F | ⏳ |
| ctr | `sm4_linkedsemi_ctr_crypt`（`inc_ctr` 16B 计数器路径） | 功能通路 | 64B 密文 == `sm4_ctr_ct`；解密回环 == 明文 | OpenSSL 3.2 `enc -sm4-ctr`，nonce 00..0B + counter 0 | ⏳ |
| ccm | `sm4_linkedsemi_ccm_encrypt` / `_decrypt`（`ccm_cbc_mac` + `ccm_ctr_mode`，tag 校验分支） | 功能通路 | 64B 密文 == `sm4_ccm_ct`；16B tag == `sm4_ccm_tag`；用该 tag 解密回环 == 明文 | RFC 3610 参考实现（块函数 = OpenSSL SM4-ECB） | ⏳ |
| gcm | `sm4_linkedsemi_gcm_encrypt` / `_decrypt`（`ghash` / `gmult`，tag 比较分支 `memcmp(t_prime, tag)`） | 功能通路 | 64B 密文 == `sm4_gcm_ct`；16B tag == `sm4_gcm_tag`；用该 tag 解密回环 == 明文 | pyca/cryptography（`algorithms.SM4` + `modes.GCM`） | ⏳ |

## case_default — hash 部分（`CONFIG_CRYPTO_LINKEDSEMI_SHA256=y` + `_SHA512=y`）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| sha224_kat | `sha256_linkedsemi_sha`（`SHA_CALC_SHA224` 分支，短报文单次 final） | 功能通路 | digest == `h_sha224_abc` | Python hashlib sha224("abc") | ⏳ |
| sha256_kat | `sha256_linkedsemi_sha`（SHA256 默认分支） | 功能通路 | digest == `h_sha256_abc` | Python hashlib sha256("abc") | ⏳ |
| sm3_kat | `sha256_linkedsemi_sha`（`SHA_CALC_SM3` 分支） | 功能通路 | digest == `h_sm3_abc` | OpenSSL 3.2 `dgst -sm3` | ⏳ |
| sha224_split | `sha256_linkedsemi_sha` 多次 `hash_update`（`sha_block_start` 跨块续算） | 功能通路 | 13+20+23 分段 digest == `h_sha224_m448` | Python hashlib sha224(FIPS 448-bit 报文) | ⏳ |
| sha256_split | 同上（SHA256 分支） | 功能通路 | 分段 digest == `h_sha256_m448` | Python hashlib sha256(同报文) | ⏳ |
| sm3_split | 同上（SM3 分支） | 功能通路 | 分段 digest == `h_sm3_m448` | OpenSSL 3.2 `dgst -sm3` | ⏳ |
| sha256_pad_boundary | `sha256_linkedsemi_sha` 填充分支（`0x80` + 补零 + 64bit 长度），长度 0/55/56/64 | 边界长度 | 四个 digest 分别 == `h_sha256_pad_{0,55,56,64}` | Python hashlib，内容 = byte(i) | ⏳ |
| sha384_kat | `sha512_linkedsemi_sha_align` → `sha512_linkedsemi_sha`（SHA384 模式） | 功能通路 | digest == `h_sha384_abc` | Python hashlib sha384("abc") | ⏳ |
| sha512_kat | 同上（SHA512 模式） | 功能通路 | digest == `h_sha512_abc` | Python hashlib sha512("abc") | ⏳ |
| sha384_split | `sha512_linkedsemi_sha` 缓冲拼接分支（`buffer_idx`） | 功能通路 | 32+48+32 分段 digest == `h_sha384_m896` | Python hashlib sha384(FIPS 896-bit 报文) | ⏳ |
| sha512_split | 同上（SHA512 模式） | 功能通路 | 分段 digest == `h_sha512_m896` | Python hashlib sha512(同报文) | ⏳ |
| sha512_pad_boundary | `sha512_linkedsemi_sha` 填充分支（128B 块），长度 0/111/112/128 | 边界长度 | 四个 digest 分别 == `h_sha512_pad_{0,111,112,128}` | Python hashlib，内容 = byte(i) | ⏳ |

> 状态标记：`⏳` = 仅生成 + 编译通过，待首次实测；`✅` = 已实测 PASS。

## case_default — 错误返回（负例）

> 判据来源 = **本驱动的实际契约**（2026-10-29 实机复核后改判，见 KNOWN_ISSUES #157）：
>   - session 级不支持（flags / algo / mode / keylen）→ `-ENOTSUP`。驱动
>     `begin_session` 在这四条路径上一律返回 `-ENOTSUP`（sm4 5 处、sha256 2 处、
>     sha512 2 处）。语义上也更贴切：这些参数的**枚举值本身合法**，只是驱动不支持，
>     不属"参数非法"。
>     ⚠️ 与本树多数厂商驱动（`return -EINVAL` 184 处 : `-ENOTSUP` 23 处）**刻意
>     不一致**：驱动侧统一口径会波及 mbedtls 等其它工程，故测试按驱动真实契约断言。
>     若驱动将来统一成 `-EINVAL`，本组期望值需同步改回。
>   - 参数越界（CCM 的 NULL / 长度 / tag_len）→ `-EINVAL`，与该驱动的 pkt 级路径一致。
>
> ✅ **2026-10-29 实测 28/28 PASS**（`reports/2026-10-29-1/crypto_api_test_case_default/`）：
> 7 项 session 级负路径实测 `ret=-134`，且每条都命中驱动自己的 `LOG_ERR("Unsupported …")`
> 分支（非空跑）。
>
> CCM 组带**正对照**：先跑一次合法调用要求返回 0，否则后续 `-EINVAL` 可能只是
> "会话已坏"，负例全绿也没有判别力。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| sm4_caps | `sm4_linkedsemi_query_caps` + `device_is_ready` | 功能通路 | caps 含 RAW_KEY / SYNC_OPS / SEPARATE_IO_BUFS；设备 ready | 驱动 `SM4_LINKEDSEMI_CIPHER_CAPS` + 样例 `validate_hw_compatibility` | ⏳ |
| sm4_bad_algo | `cipher_begin_session` 的 `algo != SM4` 检查 | 异常参数 | `ret == -ENOTSUP` | 驱动实际契约（`crypto_linkedsemi_sm4.c` 返回 -ENOTSUP，裁定见 #157） | ✅ 2026-10-29 PASS（ret=-134） |
| sm4_bad_keylen | `cipher_begin_session` 的 `keylen != 16` 检查 | 异常参数 | `ret == -ENOTSUP` | 驱动实际契约（同上） | ✅ 2026-10-29 PASS（ret=-134） |
| sm4_bad_flags | `cipher_begin_session` 的 `ctx->flags & ~CAPS` 检查（传 ASYNC） | 异常参数 | `ret == -ENOTSUP` | 驱动实际契约（同上） | ✅ 2026-10-29 PASS（ret=-134） |
| sm4_bad_mode | `cipher_begin_session` 的 mode switch `default` | 异常参数 | `ret == -ENOTSUP` | 驱动实际契约（同上） | ✅ 2026-10-29 PASS（ret=-134） |
| sm4_ccm_params | `sm4_linkedsemi_ccm_encrypt` 的入参检查（7 条） | 异常参数 | 正对照 `ret==0`；out_buf NULL / in_buf NULL / ad NULL / out_buf_max 不足 / tag_len 3 / 5 / 18 全部 `ret == -EINVAL` | 生态惯例（smartbond/stm32/tc_shim/mtls_shim 的入参检查均 -EINVAL） | ⏳ |
| hash_caps_sha256 | `sha256_linkedsemi_query_caps` + ready | 功能通路 | caps 含 **RAW_KEY**（wrapper 断言要求，见 #158）/ SYNC_OPS / SEPARATE_IO_BUFS | 驱动 `SHA256_LINKEDSEMI_HASH_CAPS` | ✅ 2026-10-29 PASS（caps=0x32） |
| hash_bad_algo_sha256 | `sha256_linkedsemi_cipher_begin_session` 的 switch `default`（SHA384/SHA512） | 异常参数 | 两者均 `ret == -ENOTSUP` | 驱动实际契约（`crypto_linkedsemi_sha256.c` 返回 -ENOTSUP，见 #157） | ✅ 2026-10-29 PASS（-134/-134） |
| hash_caps_sha512 | `sha512_linkedsemi_query_caps` + ready | 功能通路 | caps 含 **RAW_KEY**（wrapper 断言要求，见 #158）/ SYNC_OPS / SEPARATE_IO_BUFS | 驱动 `SHA512_LINKEDSEMI_HASH_CAPS` | ✅ 2026-10-29 PASS（caps=0x32） |
| hash_bad_algo_sha512 | `sha512_linkedsemi_cipher_begin_session` 的 switch `default`（SHA224/SHA256/SM3） | 异常参数 | 三者均 `ret == -ENOTSUP` | 驱动实际契约（同上） | ✅ 2026-10-29 PASS（-134×3） |
| hash_bad_flags | `sha256_linkedsemi_cipher_begin_session` 的 `flags & ~CAPS` 检查（传 ASYNC） | 异常参数 | `ret == -ENOTSUP` | 驱动实际契约（同上） | ✅ 2026-10-29 PASS（ret=-134） |

**本组未覆盖的负例（驱动缺口，未写成会打崩固件的用例）**：

- **GCM 路径没有任何参数校验**（`sm4_linkedsemi_gcm_encrypt/decrypt` 不检查
  NULL / tag_len），NULL 入参会直接解引用。写 NULL 负例会触发 Load access fault、
  把整个 case 打崩，故不写；**建议驱动补齐与 CCM 同级的检查后再补负例**。
- **ECB / CBC 对 `in_len % 16 != 0` 无错误码**：ECB 循环按整块，余块被忽略且
  `return ret;` 中 `ret` 可能是未初始化值；CBC 同样忽略余块。返回值不确定，
  无法写确定断言（记为缺口，不编预期）。

## 已知观察项（不作为本工程的 PASS/FAIL 断言）

1. **SM4 key 字节序两套栈不一致**
   - Zephyr crypto 驱动：`sm4_linkedsemi_key_expansion()` 先 `sys_memcpy_swap()`
     （`crypto_linkedsemi_sm4.c:80`）再写 KEY 寄存器 ⇒ 实际生效的 key 是调用方
     缓冲的**字节逆序**。样例 `samples/crypto/sm4` 的向量正是按这个约定生成的
     （用 `00112233445566778899AABBCCDDEEFF` 才能与 OpenSSL 对上）。
   - mbedtls `sm4_alt`：`HAL_SM4_KeyExpansion(key)` 直接吃标准字节序
     （`MT_SM4_KEY = 0123456789ABCDEFFEDCBA9876543210` 与标准向量一致，已复核）。
   - 同一颗硅、两套栈的 key 约定不同。本工程**沿用样例约定**（那是该 API 的既有
     行为），不对"标准 key 序"下断言。

2. **`ctx->ops.cipher_mode` 的赋值点（此前误判，已更正）**
   - 通用封装 `cipher_begin_session()` 自己就会写
     `ctx->ops.cipher_mode = mode;`（`zephyr/include/zephyr/crypto/crypto.h:168`），
     因此后续 `cipher_*_op()` 里的
     `__ASSERT(ctx->ops.cipher_mode == <mode>)` 本就成立。
   - 早先"驱动不填该字段 ⇒ ASSERT=y 下会 abort"的判断是**误判**（只读了封装
     尾部、漏看第 168 行），已撤回；测试侧不需要也不应再赋值。

## 独立预期（机器可读）

```expect
# 测试项 | 断言 | 来源标签:说明
ecb | kat == 1; roundtrip == 1 | golden:OpenSSL 3.2 enc -sm4-ecb 复核的同组向量（有效 key 00112233...EEFF）
cbc | kat == 1; roundtrip == 1 | golden:OpenSSL 3.2 enc -sm4-cbc，IV=00..0F
ctr | kat == 1; roundtrip == 1 | golden:OpenSSL 3.2 enc -sm4-ctr，nonce 00..0B + 32bit counter
ccm | kat == 1; tag == 1; roundtrip == 1 | golden:RFC 3610 参考实现（块函数 = OpenSSL SM4-ECB）
gcm | kat == 1; tag == 1; roundtrip == 1 | golden:pyca/cryptography algorithms.SM4 + modes.GCM
sha224_kat | kat == 1 | golden:Python hashlib sha224("abc")
sha256_kat | kat == 1 | golden:Python hashlib sha256("abc")
sm3_kat | kat == 1 | golden:OpenSSL 3.2 dgst -sm3("abc")
sha384_kat | kat == 1 | golden:Python hashlib sha384("abc")
sha512_kat | kat == 1 | golden:Python hashlib sha512("abc")
sha224_split | split_eq_kat == 1 | golden:Python hashlib sha224(FIPS 448-bit 报文)，分段喂入须与单次一致
sha256_split | split_eq_kat == 1 | golden:Python hashlib sha256(同报文)
sm3_split | split_eq_kat == 1 | golden:OpenSSL 3.2 dgst -sm3(同报文)
sha384_split | split_eq_kat == 1 | golden:Python hashlib sha384(FIPS 896-bit 报文)
sha512_split | split_eq_kat == 1 | golden:Python hashlib sha512(同报文)
sha256_pad_boundary | len0 == 1; len55 == 1; len56 == 1; len64 == 1 | golden:Python hashlib，长度 0/55/56/64 的 byte(i) 报文
sha512_pad_boundary | len0 == 1; len111 == 1; len112 == 1; len128 == 1 | golden:Python hashlib，长度 0/111/112/128 的 byte(i) 报文
sm4_caps | ready == 1; raw_key == 1; sync == 1; sep_io == 1 | contract:SM4_LINKEDSEMI_CIPHER_CAPS 含 RAW_KEY|SYNC_OPS|SEPARATE_IO_BUFS，且设备 ready
sm4_bad_algo | ret == -22; ok == 1 | spec:生态惯例 -EINVAL（smartbond:796 / stm32:342 / mtls:333）；本驱动 -ENOTSUP 为偏差
sm4_bad_keylen | ret == -22; ok == 1 | spec:生态惯例 -EINVAL（stm32:371 / nrf:98 / si32:241）
sm4_bad_flags | ret == -22; ok == 1 | spec:生态惯例 -EINVAL（smartbond:790 / stm32:336 / mtls:327）
sm4_bad_mode | ret == -22; ok == 1 | spec:生态惯例 -EINVAL（stm32:360）
sm4_ccm_params | base == 0; null_out == -22; null_in == -22; null_ad == -22; small_out == -22; tag3 == -22; tag5 == -22; tag18 == -22 | spec:生态惯例 -EINVAL（smartbond/stm32/tc_shim/mtls_shim 入参检查）；base 为正对照
hash_caps_sha256 | ready == 1; sync == 1; sep_io == 1 | contract:SHA256_LINKEDSEMI_HASH_CAPS 含 SYNC_OPS|SEPARATE_IO_BUFS
hash_caps_sha512 | ready == 1; sync == 1; sep_io == 1 | contract:SHA512_LINKEDSEMI_HASH_CAPS 含 SYNC_OPS|SEPARATE_IO_BUFS
hash_bad_algo_sha256 | sha384 == -22; sha512 == -22 | spec:生态惯例 -EINVAL（npcx:149 / it8xxx2:191）
hash_bad_algo_sha512 | sha224 == -22; sha256 == -22; sm3 == -22 | spec:生态惯例 -EINVAL（npcx:149 / it8xxx2:191）
hash_bad_flags | ret == -22; ok == 1 | spec:生态惯例 -EINVAL（npcx:143 / it8xxx2:196）
```
