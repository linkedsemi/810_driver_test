# BUG-3：OTBN IMEM 固件状态跨模块不可见导致固件混用（已闭环）

> 用户报告的原始问题："RSA 密钥生成与 SHA256 来回混用导致结果出错"。
> 分类：驱动层 bug（OTBN HAL 架构缺陷），修复于 2026-08-28，上板验证通过。

## 现象

- `api_rsa2048_kat` FAIL：RSA encrypt+decrypt 后紧接的
  SHA256("hello rsa otbn test") digest 错误。自发 sign+verify 自洽通过
  （用的是同一个错 hash），但与 OpenSSL 黄金签名比对失败。
- mix_* 4 项（sha256_keygen_seq / sha256_stream_keygen /
  keygen_sha256_keygen / sign_sha256_stream）全 FAIL，同一特征：
  `byte[0]:0x6a != 0x24`（digest 第一字节就是错的，整段垃圾）。
- 反方向（RSA 前跑 hash）的 mix_sha512/sm3/ecdsa 项不受影响。

## 根因

OTBN 的 IMEM/DMEM 是**硬件内存，跨 session 持久**（acquire/release 只
仲裁所有权，不擦内容）。各模块各自维护"IMEM 里现在是谁的固件"的
**私有静态缓存**，互不通知：

1. `otbn_hash.c` 的 `s_last_firmware_id`：hash 上下文里发现
   `s_last_firmware_id == ctx->firmware_id` 就**跳过 IMEM 重载**（省时间）；
2. `rsa_alt.c` / ecdsa_alt 加载固件时**无条件重写 IMEM**，不通知 hash 模块；
3. 于是 RSA keygen/modexp 跑完后，下一次 SHA256 update 误以为 IMEM 里还是
   SHA256 固件 → 在 RSA 固件上执行 hash 指令流。

**硬件实锤**：串口抓到 `E: otbn cmd error , error bit : 0x8` —— ERR_BITS
0x8 = ILLEGAL_INSN（OpenTitan OTBN 错误位定义），hash 指令在 RSA 固件
上命中非法指令；digest 于是为垃圾（0x6a 特征）。

`otbn_ecc.c` 的 `cur_curve` 是同款缓存（**反向混用**：hash 重写 IMEM 后
ECC 跳过重载），已一并修复。修复后 BUG-5（ecdsa p256 kat r mismatch）
不变 → 排除缓存混用为 BUG-5 根因。

## 修复（驱动层，5 文件）

"当前 IMEM 里的固件"状态上收到 session 层共享：

| 文件 | 改动 |
|---|---|
| `zephyr/soc/linkedsemi/lsqsh/otbn/ls_otbn_config.h` | 声明 `ls_otbn_imem_firmware_confirm(id)` / `ls_otbn_imem_firmware_get()` |
| `zephyr/soc/linkedsemi/lsqsh/otbn/otbn_common.c` | 新增 `imem_firmware` 静态态（跨 session 持久）；module init/deinit 重置 UNUSED；实现 confirm/get |
| `zephyr/soc/linkedsemi/lsqsh/otbn/otbn_hash.c` | 删 `s_last_firmware_id`，load 前查 `ls_otbn_imem_firmware_get()`，加载成功后 confirm |
| `zephyr/soc/linkedsemi/lsqsh/otbn/otbn_ecc.c` | 删 `cur_curve`，同上；IMEM/DMEM 写入 err==0 后 confirm |
| `modules/crypto/mbedtls/library/rsa_alt.c` | modexp/keygen 固件写 IMEM 成功后 confirm(RSA_MODEXP / RSA_KEYGEN) |

设计要点：状态挂在 session 层而非各模块，任何固件装载方（hash/ecc/
rsa_alt）都更新它；IMEM 硬件跨 session 持久，故状态不随 acquire/release
清空，只在 module init/deinit（真正复位核心）时重置。

## 验证（2026-08-28 20:37 回归，run_test.py）

- otbn_test：**18/25 → 23/25 PASS**。mix 4 项 + api_rsa2048_kat 全部
  转 PASS，`0x6a` 特征消失，无任何 ILLEGAL_INSN 报错。
- otbn_selftest：30/30 PASS 保持（无回归）。
- 剩余 2 FAIL 均为独立问题：api_aes_kat（BUG-2 硬件 AES XTS）、
  api_ecdsa_p256_kat（BUG-5，待定位）。
- 报告归档：`reports/2026-08-28/otbn_test_report.{md,json}`。

## 后续加固（2026-08-28）：ls_otbn_module_reset 同步状态

- 隐患：`ls_otbn_module_reset()`（cmd 超时/等 idle 超时路径）软复位 OTBN 后
  **不更新 `imem_firmware`**——若复位清了 IMEM（或内容不可信），共享状态
  仍声称旧固件驻留，同会话重试会跳过重载，在状态不明的核心上执行。
- 修复：reset() 末尾补 `imem_firmware = OTBN_FIRMWARE_UNUSED;`。
  若硬件软复位不清 SRAM（大概率），代价只是下次多一次加载；若清了，
  这行就是正确性必要条件。两个方向都安全。
- 验证：otbn_test 回归 25/25 PASS 保持。

## 通用教训

- 硬件状态跨软件模块共享时，**缓存必须单一权威来源**。多个模块各自缓存
  同一硬件状态、且一方更新不通知另一方 = 必然混用。此类场景优先把状态
  上收到资源管理层（session 层），提供 confirm/get 而非各自记小本本。
- OTBN 的 ILLEGAL_INSN（ERR_BITS 0x8）报错是"在错误的固件上执行"的
  特征信号，遇到先查 IMEM 内容而不是操作数。
- 驱动注释边界（§4.6）：修复注释只写状态语义（"IMEM 跨 session 持久，
  状态必须同样持久"），不写调试历史。
