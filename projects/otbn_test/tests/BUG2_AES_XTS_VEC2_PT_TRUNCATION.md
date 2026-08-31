# BUG-2：AES XTS vec2 明文向量移植缺 8 字节（已闭环）

## 现象

otbn_test 回归中 `api_aes_kat` 稳定 FAIL，reason：
`aes_xts mismatch vec=2 mode=0`——aes_xts_test 第 3 组向量（key =
`0xff,0xfe,0xfd,…,0xf0 || 0x22×16`）encrypt 方向 KAT 比对失败，ret=1
首个 mismatch 即 goto exit（vec2 的 decrypt 从未跑到）。vec 0/1 及
ECB/CBC/CFB/OFB/CTR 全部通过。

## 排查链

1. **读测试代码**（`otbn_api_test.c` aes_xts_test）：3 组 XTS-256 向量
   （32 字节数据单元，每组 enc+dec），失败点仅在 vec2 enc。
2. **读驱动**（`modules/crypto/mbedtls/library/aes_alt.c`）：XTS 是
   mbedtls 软件逻辑（GF 乘法 ble 版）+ 硬件 ECB；每 block 重新 setkey。
   逻辑与上游 mbedtls 一致，无 key 相关分支。
3. **PC 独立验证黄金向量**（Python cryptography + 手写 mbedtls 同款
   `gf128mul_x_ble`）：XTS-256(key2, du=`3333333333`+0×11, pt=`0x44×32`)
   = `af85336b597afc…1a227a89`——与测试黄金密文**逐位一致**，排除
   "黄金向量写错"和"硬件 AES 只支持部分计算"。
4. **发现根因**：`aes_test_xts_pt32[2]` 只有 **24 字节**（3 行 ×8），
   缺第 4 行 8 个 `0x44`。静态数组 `[][32]` 部分初始化 → 缺的 8 字节
   被零填充，实际送入的明文 = `0x44×24 || 0x00×8` ≠ 黄金向量对应的
   `0x44×32` → 必 mismatch。对照样本 `samples/mbedtls_test/src/main.c`
   同名数组是完整 32 字节——缺行是移植到 otbn_api_test.c 时漏抄。

## 证据

- PC 复算（修复后输入）：`af85336b597afc1a900b2eb21ec949d292df4c047e0b21532186a5971a227a89`
  == 测试黄金密文 ✓
- PC 复算（bugged 输入 `0x44×24||0×8`）：`af85336b597afc1a900b2eb21ec949d2f`
  `d25393495ca8fab746e37c9012cac4d`——块 1（前 16B）相同、块 2 起分叉，
  与"首个 32B memcmp 即败"的现象吻合。
- 编译无告警：C 语言部分初始化零填充是合法行为，不缺 `-W` 覆盖。

## 修复

`otbn_api_test.c` `aes_test_xts_pt32[2]` 补上第 4 行 8 个 `0x44`
（与样本 main.c 对齐），其余不动。

## 验证

2026-08-28 21:22 回归：`api_aes_kat` 转 PASS，otbn_test **25/25 全 PASS**
（全部 token 采集，无内存兜底），报告归档
`reports/2026-08-28/otbn_test_report.{md,json}`。

## 结论与通用教训

- **硬件 AES 完全正常**，黄金向量完全正确——问题在测试向量移植。
- 用户猜想"硬件只支持部分计算"不成立；但"测试方向有问题"成立：
  问题在测试侧输入数据。
- 教训：向量从样本手工移植后逐数组核对**长度**（部分初始化零填充
  不报错、不告警），与 BUG-1（样本向量行写错）同属"样本向量漏抄/抄错"
  一类问题，移植时照抄 + 对表长度可防。
