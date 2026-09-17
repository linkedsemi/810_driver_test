# mbedtls SM4 硬件加速包装层吞掉 HAL 错误码 根因与修复

> 定位闭环：2026-09-16。现象：4 个 mbedtls case 同时 FAIL，且**唯一失败项都是**
> `sm4_bad_params`。修复后 4 个 case 全 PASS，并在后续**全量回归的正常流程中独立确认**
> （非定向复跑），全量 29/29 PASS。

## 现象

`projects/mbedtls_test` 的 `case_hw_otbn_mix` / `case_hw_all` / `case_sw_hw_mix` /
`case_all_on_max` 四个 case FAIL，报错点完全一致：

```c
ret = mbedtls_sm4_ctr_crypto(&ctx, ct, NULL, 16);
zassert_not_equal(ret, 0, "sm4 ctr NULL input should fail");   // robustness_test.c:143
// 实际 ret == 0 —— 非法入参被上报为"成功"
```

## 根因

**HAL 已经把错误报上来了，包装层把它扔了。**

```
调用链： robustness_test.c ──► mbedtls_sm4_ctr_crypto() ──► HAL_SM4_CTR_Crypt() ──► SM4 硬件

HAL 侧（modules/hal/linkedsemi/hal_driver/src/ls_hal_sm4.c）:
    if ((cnt == NULL) || ((in_len != 0) && ((in == NULL) || (out == NULL))))
        return HAL_INVALIAD_PARAM;            ← 校验存在，返回值也对

包装层（modules/crypto/mbedtls/library/sm4_alt.c，修改前）:
    HAL_SM4_CTR_Crypt(ctx->iv, input, ilen, output);   ← 返回值被丢弃
    return 0;                                          ← 恒返回成功
```

上层因此**无法区分"已加密"与"压根没处理"** —— 又是一例"错误路径被静默吞掉"。

## 关键证据（判定"驱动缺陷"而非"测试预期过度"）

三条独立证据，均指向驱动侧：

1. **对称性**：HAL 自己实现了 NULL 校验并返回 `HAL_INVALIAD_PARAM` —— 它是有意设计，
   不是多余分支。若测试预期"过度"，HAL 的校验就该一并算错。
2. **同类对照**：同一套用例里 `aes_bad_params`（走 mbedtls 上游软件实现）PASS、
   `sha256_bad_params` / `sha384_bad_params` PASS。**唯独硬件加速这一条路径不报错**
   → 差异在包装层，不在测试预期。
3. **同目录约定**：`modules/crypto/mbedtls/library/rsa_alt.c` 等同类包装层一律用负
   errno 上抛（`-EINVAL` / `-EBUSY` / `-EIO`）。本文件属**偏离既有约定**。

## 修复

| 文件 | 改动 |
|---|---|
| `modules/crypto/mbedtls/library/sm4_alt.c` | 新增 `#include <errno.h>`；`mbedtls_sm4_ctr_crypto()` 由"丢弃返回值、恒 `return 0`"改为 `return HAL_SM4_CTR_Crypt(...) == HAL_OK ? 0 : -EINVAL;` |

改动仅作用于**非法入参分支**：合法调用返回 `HAL_OK` → 仍返回 0，对正常加解密路径**零影响**。

## 验证

- 修复后 4 个 case 复跑 PASS：`hw_otbn_mix` 36/36、`hw_all` 26/26、`sw_hw_mix` 25/25、
  `all_on_max` 38/38。
- 随后一轮**零改动的全量回归**中，这 4 个 case 在**正常全量流程内**再次 PASS —— 即修复
  获得了独立确认，而非"只验证了改过的那一项"。
- 全量回归 **29/29 PASS**（670 项）。

## 为什么此前没暴露（不是回归）

**是被超时掩盖的潜伏缺陷。** 上一轮 mbedtls 各 case 有 11~17 项 `NOT_RUN`（超时截断），
`sm4_bad_params` 恰好落在没跑到的那批里；本轮 `timeout=600s` 跑完整套件后才首次执行。
→ **跨轮 `--timeout` 不一致的结果不可直接对比**（见 KNOWN_ISSUES #63）：短 timeout 截断
导致"潜伏缺陷首次执行"看起来像"新 FAIL"。

## 后续：ecb 原语同族修复（O-3，已结）

`mbedtls_sm4_ecb_encrypt` / `mbedtls_sm4_ecb_decrypt` 原先同样**无入参校验、恒 `return 0`**。
但它们的硬件操作是**内联的**（直接读写 SM4 寄存器，不经过 HAL），因此**不能照抄 ctr 的
"转发 HAL 返回值"改法**——必须在本层自行校验：

| 入参 | 处理 | 理由 |
|---|---|---|
| `ctx` / `input` / `output` 为 NULL | `-EINVAL` | 与同目录 `rsa_alt.c` 的负 errno 约定一致；上层须能区分"已加密"与"未处理" |
| `ilen == 0` | `-EINVAL` | `SM4_CALC_LEN` 写的是 `ilen/16-1`，0 会下溢成 255 组，硬件按错误长度启动 |
| `ilen % 16 != 0` | `-EINVAL` | 只有 16 字节整块语义；原先仅有 `__ASSERT_NO_MSG`（release 构建被编译掉） |

覆盖加在既有 `robustness/sm4_bad_params`（同一 ZTEST 内扩项，不新增测试项）：
NULL ctx / NULL in / NULL out（ENC+DEC）、len=0、len=20，并补一条**合法入参仍返回 0**
的正向断言，确认校验未误伤正常路径。

> 更正一处此前的表述：ecb 原语并非"零用例覆盖"——`cipher/sm4_ecb` 与 `cipher/sm4_cbc`
> 已覆盖其**功能通路**（KAT）；缺的是**异常参数**覆盖，即本次补的部分。

## 通用教训

1. **包装层必须上抛底层错误码。** "HAL 有校验" ≠ "上层能看见错误" —— 中间任何一层丢弃
   返回值，校验就形同虚设。
2. **同类路径的横向对照是最快的定性手段**：同一套用例里 `aes_*` / `sha*_bad_params` PASS
   而 `sm4_*` FAIL，一眼就能把嫌疑锁定在"这条路径的包装层"，不必去争论测试预期是否合理。
3. **回归前必须固定 `--timeout`**：否则"超时截断下的 NOT_RUN"会在拉长超时后变成"新 FAIL"，
   被误判成回归。
