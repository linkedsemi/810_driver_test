# ENTROPY_API_UINT16_LENGTH_OVERFLOW.md

## 现象

调用 `entropy_get_entropy(dev, buf, 65536)` 返回 `-EINVAL`，但驱动源码
`ls_trng_get_entropy` 对 len 的检查是 `len == 0` 才返回 -EINVAL（132-135 行），
65536 显然不是 0。

## 根因

Zephyr entropy API 的 length 参数类型是 **`uint16_t`**：

```c
/* include/zephyr/drivers/entropy.h */
int entropy_get_entropy(const struct device *dev, uint8_t *buffer, uint16_t length);
```

`65536` 传入时被截断为 `0`，走到 `len == 0` 分支 → `-EINVAL`。
不是驱动 bug，是 API 类型上限（单次请求 ≤ 65535B）。

## 证据

- 源码位置: `entropy_ls_trng.c` 132-135 行（`if (len == 0 || buf == NULL) return -EINVAL;`）
- API 声明: `include/zephyr/drivers/entropy.h`（uint16_t length）

## 修复/规避

64KB 吞吐测试分 8 × 8192B 请求（单次 8192B = 2^19 bits 恰好触发
`trng_issue_next` 的 `req_bits + 128 > MAX_BITS_PER_REQUEST(2^19)` →
`CMD_ADVANCE_STATE` + `skip_next` 隐藏分支，85-88 行；ISR 侧 105-107 行
对应 skip 分支——顺带覆盖了该边界），总计仍为 64KB，并上报 mbps INFO token。

## 通用教训（一次性问题，不进 §10）

写长度参数类测试前先查 API 原型的类型上限；uint16_t 的长度参数
最大值测试用 65535（若驱动接受），65536 是"截断陷阱"而非合法输入。
