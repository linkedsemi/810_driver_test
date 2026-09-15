---
name: mutex-leak-deadlock
description: Zephyr 驱动中 k_mutex_lock 后 return 跳过 unlock 导致永久死锁的排查和修复模式
tags: [mutex, deadlock, driver, i3c, resource-leak]
date: 2026-09-14
source: i3c_linkedsemi.c ls_i3c_configure / ls_i3c_do_daa 实际案例
---

# Mutex 泄漏致永久死锁

## 现象

驱动首次传输正常，但某个条件触发后**所有后续传输永久阻塞**在 `k_mutex_lock(&data->lock, K_FOREVER)`。
表现：
- 系统不死（无 crash），但 I3C/I2C 总线完全不响应
- 串口无新输出（测试框架判为 timeout）
- 复位后恢复（锁随 RAM 清零）

## 根因

函数内 `k_mutex_lock()` 持锁后，错误路径用 `return -Exxx` 直接返回，
跳过了函数末尾的 `k_mutex_unlock()`。锁永久持有 → 后续任何需要该锁的操作死锁。

## 典型缺陷代码

```c
static int driver_configure(const struct device *dev, ...) {
    struct driver_data *data = dev->data;
    int ret = 0;
    k_mutex_lock(&data->lock, K_FOREVER);      // ← 持锁
    if (bad_condition) {
        return -EINVAL;                          // ← BUG: 死锁！
    }
    // ... 正常逻辑 ...
    k_mutex_unlock(&data->lock);                 // ← 只有正常路径到这里
    return ret;
}
```

## 修复模式

```c
static int driver_configure(const struct device *dev, ...) {
    struct driver_data *data = dev->data;
    int ret = 0;
    k_mutex_lock(&data->lock, K_FOREVER);
    if (bad_condition) {
        ret = -EINVAL;
        goto out;                                // ← 跳到统一出口
    }
    // ... 正常逻辑 ...
out:
    k_mutex_unlock(&data->lock);                 // ← 所有路径都到这里
    return ret;
}
```

## 排查方法

1. Grep 所有 `k_mutex_lock` 调用
2. 对每个调用，读取所在函数全文
3. 标记 lock 点（L_lock）和所有 unlock 点（L_unlock）
4. 搜索 [L_lock, L_unlock] 区间内的 `return` 语句
5. 任何一个 return 前没有 unlock → 确认 BUG

## 实际案例

**文件**：`zephyr/drivers/i3c/i3c_linkedsemi.c`

**案例 1**：`ls_i3c_configure()` (L418)
- L425: `k_mutex_lock(&dev_data->lock, K_FOREVER)`
- L430: `return -EINVAL;` ← scl 配置为 0 时死锁
- L435: `return -EINVAL;` ← HDR 不支持时死锁
- L457: `k_mutex_unlock` ← 只有正常路径到达
- **影响**：I3C configure 参数错误后锁永不释放，所有后续 I3C 传输永久阻塞

**案例 2**：`ls_i3c_do_daa()` (L1183)
- L1189: `k_mutex_lock(&data->lock, K_FOREVER)`
- L1295: `return -ETIMEDOUT;` ← DAA 帧超时时死锁（在 `out_daa` 标签之后、unlock 之前）
- L1307: `k_mutex_unlock` ← 只有正常路径到达
- **影响**：DAA 超时后锁永不释放，总线完全卡死

## 扩展检查

- `k_sem_take` / `k_sem_give` 同样适用配对检查
- `irq_lock` / `irq_unlock` 也要配对
- `k_mutex_lock` 嵌套时注意释放顺序（反序释放防 ABBA 死锁）
- 关键路径（传输函数）用 `K_FOREVER` 的函数，泄漏 = 永久死锁；考虑非关键路径改用有限超时

## 链路
> **相关**→ [[isr-no-log]] · **skill**→ qsh-driver-review §1.1
