# 驱动缺陷模式代码示例——参考

> **定位**：承接 qsh-driver-review §1.1/§1.2/§1.5 的典型缺陷 C 代码示例。
> 审核规则与检查方法在 SKILL.md；对不上模式时翻这里看具体长什么样。

## 1. Mutex / Semaphore 泄漏（对应 §1.1）

```c
// ❌ 致死锁：错误路径 return 跳过了 unlock
k_mutex_lock(&data->lock, K_FOREVER);
if (bad_condition) {
    return -EINVAL;   // ← 永久死锁！后续所有传输阻塞在 K_FOREVER
}
// ... 正常逻辑 ...
k_mutex_unlock(&data->lock);

// ✅ 正确：goto 统一出口
k_mutex_lock(&data->lock, K_FOREVER);
if (bad_condition) {
    ret = -EINVAL;
    goto out;
}
// ...
out:
k_mutex_unlock(&data->lock);
return ret;
```

## 2. 错误路径跳过硬件 Cleanup（对应 §1.2）

```c
// ❌ 错误路径跳过了 xfer_reset，残留 FIFO 数据污染下一笔传输
k_mutex_lock(&data->lock, K_FOREVER);
hw_fifo_reset(base);
ret = do_transfer(base);
if (ret != 0) {
    return ret;   // ← FIFO 未清理 + 锁未释放！
}
hw_fifo_reset(base);
k_mutex_unlock(&data->lock);

// ✅ 正确：错误路径也走 reset + unlock
if (ret != 0) {
    hw_fifo_reset(base);  // 清理硬件
    goto out;
}
```

## 3. 返回值未检查 / 丢弃（对应 §1.5）

```c
// ❌ 返回值完全丢弃 — 如果函数失败，后续操作在错误状态上继续
hw_mem_read(remote_addr, buf, size);           // 可能返回错误
data = parse(buf, size);                       // 读到的是垃圾数据

// ❌ 返回值捕获但不检查 — err 被覆盖，前一步错误被吞掉
hw_mem_write(addr1, data1, len);               // 错误码丢弃
err = hw_mem_write(addr2, data2, len);         // 只检查了这一步

// ❌ 回调函数指针返回值未检查 — 随机数/IO 等外部源失败时静默继续
f_rng(p_rng, cc_buf, curve_size);              // f_rng 返回 int 错误码

// ✅ 正确：检查每次调用的返回值
err = hw_mem_read(remote_addr, buf, size);
if (err != 0) {
    goto exit;
}
err = parse(buf, result);
if (err != 0) {
    goto exit;
}
```

### `err |=` 累积模式

```c
// ❌ 隐患：第一个调用失败被静默吞掉，err 只反映最后一个调用
err |= hw_write(addr1, data1, len);
err |= hw_write(addr2, data2, len);  // 两个都可能失败

// ✅ 安全：立即检查，任一失败即退出
err = hw_write(addr1, data1, len);
if (err != 0) goto exit;
err = hw_write(addr2, data2, len);
if (err != 0) goto exit;
```
