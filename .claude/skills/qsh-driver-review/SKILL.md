---
name: qsh-driver-review
description: 底层驱动代码审核 skill：检测 mutex/semaphore 泄漏、中断安全违规、资源清理遗漏、错误路径跳过 cleanup 等嵌入式驱动常见致命缺陷。修改驱动代码后、合入回归前调用。
---

# QSH 底层驱动代码审核

目标：**在编译/烧录之前，用静态分析手段捕获嵌入式驱动代码中的致命缺陷**——
mutex 死锁、信号量泄漏、中断安全违规、错误路径资源遗漏。
这些问题在嵌入式系统中表现极为隐蔽：不是编译报错，而是运行时偶发卡死/死锁/数据损坏。

## 0. 触发时机

| 触发条件 | 调用方式 |
|---------|---------|
| 修改 `zephyr/drivers/` 下任何 `.c` 文件 | 🟢 自动 |
| 修改 `modules/hal/linkedsemi/` 下驱动代码 | 🟢 自动 |
| runner 完成 STRATEGY_DEV 后 | 🟢 自动（skill-orchestrator 闭环链） |
| 用户说"审核"/"review"/"检查驱动" | 🔵 手动 |

## 1. 审核维度清单

### 1.1 ★ Mutex / Semaphore 泄漏（P0 必查）

**规则**：任何 `k_mutex_lock()` / `k_sem_take()` 之后，函数的**所有**出口路径
（包括错误提前返回）都必须有对应的 `k_mutex_unlock()` / `k_sem_give()`。

**典型缺陷**：错误路径 `return -EINVAL` 在持锁状态直接返回 → 永久死锁；
正确模式 = `goto out` 统一出口（代码示例见 [ref-defect-patterns.md](ref-defect-patterns.md) §1）。

**检查方法**：
```
对每个含 k_mutex_lock / k_sem_take 的函数：
  1. 标记 lock 点（行号 L_lock）
  2. 找到对应的 unlock 点（行号 L_unlock）
  3. 搜索 [L_lock, L_unlock] 区间内的所有 return / exit() 调用
  4. 任何一个 return 之前没有 unlock → P0 BUG
```

**已知真实案例**：`zephyr/drivers/i3c/i3c_linkedsemi.c` `ls_i3c_configure()` 函数，
两处 `return -EINVAL` 在持锁状态直接返回，导致 I3C 总线永久死锁。
同一文件 `ls_i3c_do_daa()` 中 `out_daa` 标签后 `return -ETIMEDOUT` 同样泄漏锁。

**扩展检查**：
- `k_sem_take` 之后 `return` 前是否有 `k_sem_give`（配对使用时）
- `k_mutex_lock` 嵌套时的释放顺序（反序释放防 ABBA 死锁）
- `K_FOREVER` vs 有限超时：关键路径（传输、configure）用 `K_FOREVER` 的函数，
  一旦泄漏锁就是永久死锁，应考虑对非关键路径改用有限超时

### 1.2 错误路径跳过硬件 Cleanup（P0 必查）

**规则**：涉及硬件操作（DMA、外设寄存器、FIFO）的函数，错误路径必须执行
硬件清理（reset FIFO、disable DMA、clear flags），否则硬件状态残留污染下一笔操作。

**典型缺陷**：错误路径跳过 `xfer_reset`/`fifo_flush`，残留 FIFO 数据污染下一笔
传输（代码示例见 [ref-defect-patterns.md](ref-defect-patterns.md) §2）。

**已知真实案例**：`zephyr/drivers/spi/spi_ls.c` `spi_ls_configure()` 的三个错误返回
（半双工 `:72-75`、TI 模式 `:77-80`、超频 `:118-121`）都在 `ctx->config = config`（`:140`）
**之前**返回；`spi_ls_transceive()` 走 `goto done` → `spi_context_release(ctx, err)`，
在 `CONFIG_SPI_ASYNC=n` 下解引用 `ctx->config->operation`，而**首次调用**时 `ctx->config == NULL`
⇒ load access fault（固件进异常入口、整轮套件中断）。形态 = **"错误路径上的状态比正常路径少"**
（错误早退 ↔ cleanup 却假设了正常路径已建立的状态）。登记：KNOWN_ISSUES #80。

**检查方法**：
```
对每个含硬件操作（寄存器写、DMA 配置、FIFO 操作）的函数：
  1. 找到 xfer_reset / dma_stop / fifo_flush 等清理调用
  2. 确认所有错误路径都有对应的清理
  3. 特别关注 goto 标签后的代码是否跳过了必要的清理
```

### 1.3 中断安全违规（P1 必查）

**规则**：
- ISR 中**禁止**调用可能阻塞的函数（`k_mutex_lock`、`k_sem_take(K_FOREVER)`、
  `k_malloc`、`LOG_INF` 在非 DEFERRED 模式下）
- ISR 中**禁止**访问可能被主线程并发修改的非 volatile / 非 atomic 数据
- 共享数据访问必须有互斥保护（`irq_lock` / `k_mutex` / `atomic_t`）

**检查方法**：
```
1. 找到所有 ISR 函数（IRQ_CONNECT 注册的、DT_INST_IRQ 绑定的）
2. 检查 ISR 内是否调用了阻塞 API
3. 检查 ISR 访问的全局变量是否有保护（或声明为 volatile/atomic）
```

### 1.4 DMA / 缓冲区安全（P1 必查）

**规则**：
- DMA 缓冲区必须对齐到 cache line（`__aligned(CONFIG_DCACHE_LINE_SIZE)`）
- DMA 传输期间缓冲区不能被释放或栈回收（栈上 DMA buf 必须等 transfer 完成）
- DMA 完成回调中不能做阻塞操作

**检查方法**：
```
1. 找到 DMA 缓冲区定义，检查对齐属性
2. 检查 DMA buffer 生命周期是否覆盖整个传输周期
3. 检查 DMA 回调函数是否违反 ISR 规则（回调常在中断上下文执行）
```

### 1.5 返回值未检查 / 丢弃（P0 必查）

**规则**：函数返回错误码（`int`、负 `errno`、特定框架错误码）时，
调用方**必须**检查并处理错误，不得静默丢弃。

**典型缺陷形态**（完整代码示例见 [ref-defect-patterns.md](ref-defect-patterns.md) §3）：
返回值完全丢弃 / 捕获但不检查（err 被覆盖）/ 回调函数指针返回值未检查 /
`err |=` 累积导致前面失败被吞掉。

**高风险函数类别**（必须检查返回值，按特征识别而非硬编码函数名）：

| 类别 | 特征 | 不检查的后果 |
|------|------|-------------|
| **硬件读写** | 向外设寄存器/共享内存/总线读写数据的函数 | 读到垃圾数据 / 写入静默失败 |
| **硬件控制** | 向外设发送命令/触发操作的函数 | 外设状态机错乱 |
| **安全/加密** | 密钥操作、随机数生成、签名校验函数 | 安全漏洞 / 数据不可信 |
| **数据编解码** | 序列化/反序列化、编码/解码函数 | 数据不完整/损坏 |
| **回调函数指针** | 通过函数指针调用的外部回调 | 外部源失败时静默继续 |
| **总线/通信** | SPI/I2C/I3C/UART/CAN 等总线传输函数 | 后续操作基于错误总线状态 |

**检查方法**：
```
对每个文件：
  1. 找到所有返回 int/错误码的函数调用（含回调函数指针调用）
  2. 判断函数是否属于上述高风险类别
     - 命名特征：*_read, *_write, *_xfer, *_transfer, *_send, *_recv,
       *_init, *_deinit, *_open, *_close, *_cmd, *_exec, *_encode, *_decode,
       *_sign, *_verify, *_encrypt, *_decrypt, *_generate, *_parse
     - 参数特征：输出缓冲区指针 + 长度参数（写入目标）
     - 声明特征：头文件中返回 int / 返回负 errno / 返回特定错误码
  3. 检查返回值是否被赋值到变量并检查
  4. 如果返回值被丢弃 → P0 BUG
  5. 如果返回值用 |= 累积但前面有丢弃 → P0 BUG（前面的失败被吞掉）
```

**特别注意 `err |=` 模式**：
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

**已知真实案例**：`zephyr/drivers/mdio/mdio_dwmac.c` 的 `mdio_dwmac_init()`（L274-276）里
`mdio_dwmac_ethphy_reset(dev);` 的返回值被**直接丢弃** —— 属本表「硬件控制」类，判 **P0**。
后果不止"漏一次检查"：PHY 复位失败被静默忽略后，后续 MDIO 读回垃圾值，测试把根因报成
**"PHY ID 不符"** 这种**误导性归因**，而非"复位失败"——缺陷因此更难定位。
教训：硬件控制类函数丢弃返回值，代价是**归因错误**。
（登记：KNOWN_ISSUES #73 / OPEN_ITEMS O-16。）

### 1.6 错误码一致性（P2 建议查）

**规则**：
- Zephyr 驱动 API 约定返回负 errno（`-EINVAL`, `-ETIMEDOUT`, `-EIO` 等）
- 不得返回正数作为错误码
- 不得返回 `0` 表示错误

**检查方法**：
```
grep 函数中所有 return 语句，检查错误码是否为负 errno
```

### 1.7 并发竞态（P1 必查）

**规则**：
- 多线程共享的驱动数据结构必须有保护（`dev->data` 中的字段）
- `configure()` 和 `transfer()` 并发调用不得导致数据不一致
- 避免 TOCTOU（check-then-act）：如先检查 FIFO 非满再写入，中间可能被 ISR 改变状态

**检查方法**：
```
1. 找到 dev->data 结构体中被多个函数访问的字段
2. 检查这些访问是否都在同一 mutex 保护区内
3. 特别关注 configure 和 transfer 可能并发的场景
```

### 1.8 过期/失效索引作写地址（P0 必查）

**规则**：用索引变量计算写地址（`base + idx * stride`）或数组下标前，必须确认该索引在
**写入点**仍是有效值。典型缺陷：搜索/分配函数在"未命中/失败"分支把索引置为**负 errno**
（如 `-ENOSR`），随后**未提前 return** 仍用该索引算写地址 → 写到**表外/越界寄存器**
（负偏移落在基址之前）。此类分支常被"传一个默认值"的自然调用命中。

**检查方法**：
```
对每个 base + idx * stride（或数组下标）形式的写地址/写操作：
  1. 回溯 idx 的来源与所有赋值点
  2. 是否存在"未命中/失败"分支把 idx 置为负值或错误码
  3. 该分支之后是否仍有使用 idx 的写操作（缺提前 return）→ P0
```

**已知真实案例**：`modules/lib/pfr_tpm` 的 `spif_add_cmd()`——"命令已在固定表"分支把 `idx`
置为 `-ENOSR` 后仍用其算 `table_base + idx * 4`，写到**表外寄存器**（`spi_filter.c:884`；
`spif_add_cmd_with_dummy` 同型 `:938`）。
**数值（2026-09-18 复核更正）**：`-ENOSR = **-63**`（本项目 picolibc；**`-28` 是 `ENOSPC`**）
⇒ 偏移 **-252 字节（-0xFC）**，spif1 落点 **`0x4006FF30`**（RTC 块 `0x4006FC00..0x4006FFFF`）。
**该缺陷已于 2026-09-18 修复**（两处写址改用固定表循环变量 `off`，即 `table_base + off * 4`，
`off ∈ [0,30)`）。登记：KNOWN_ISSUES #78。

### 1.9 同步路径重复完成 → 信号量/令牌残留（P0 必查）

**规则**：同一笔传输的完成动作（`*_complete()` / `k_sem_give` / 回调触发）在同一路径上
**只能发生一次**。"先等完成、再无条件 complete" 会让每笔传输**残留一个令牌**，
下一笔在错误时刻被唤醒（症状与"传输没开始/没结束"极像）。

**检查方法**：
```
1. 找 wait_for_completion / k_sem_take 之后紧跟的 complete / k_sem_give 调用
2. 确认二者不会同时出现在同一路径（同步模式下 complete 应由 ISR 独占）
3. 与同类驱动对照（如 STM32-like SPI 的 spi_gd32.c）
```

**已知真实案例**：`spi_ls.c:359-360` —— `spi_ls_transceive()` 先
`spi_context_wait_for_completion()`，紧接着又调 `spi_ls_complete()`（`:274` 无条件
`k_sem_give(&ctx->sync)`，`spi_context.h:209-212`）⇒ 每完成一次传输残留 1 个令牌。
主设备侧因 ISR 把线程饿死到传输结束而**表象正常**，只有**从设备**侧暴露
（返回后 `SPE=0`，等不到主机时钟）。对照 `spi_gd32.c:400-406`：同步模式**不再** complete。
登记：KNOWN_ISSUES #80。

### 1.10 状态撤销写错位（"撤销位" ≠ "使能位"）→ 撤销静默失败（P1 必查）

**规则**：凡"使能 → 撤销"成对存在的寄存器字段（输出使能、通道启动、时钟门、中断屏蔽、
FIFO 刷新、DMA 复位），**撤销路径必须清与使能路径同一个位/半区**。写错位（尤其写到**只读
回读位**）时撤销**静默无效**：调用返回成功、无日志、寄存器回读仍显示"已使能"，此后所有观测
都被这个残留状态污染 —— 表现为"配置看起来全对，但状态不还原"。

**检查方法**：
```
对每一对 enable/disable（或同一函数里的置位/清位分支）：
  1. 取使能写入的表达式与撤销写入的表达式（grep 该寄存器名 / EN / OE / CTRL）
  2. 逐位比较：掩码相同吗？半区（低位数据区 vs 高位控制区）相同吗？
  3. 撤销位若落在"回读/状态"半区 ⇒ 写它无效，判 P1
  4. 有"改状态 + 验证"接口时，检查调用方是否**读回验证**（无验证的还原 = 假设成功）
```

**已知真实案例**：`modules/hal/linkedsemi/soc/rv32/qsh/ls_soc_gpio.c` —— `io_cfg_output()` 置
`1<<pin<<16`（高半区）、`io_cfg_disable_output()` 清 `1<<pin<<16`（高半区，正确），而
`io_cfg_input()` 清的是 `1<<pin`（**低半区 = DIN 回读位**）⇒ `GPIO_OUTPUT → GPIO_INPUT` 从不
撤销输出使能，pad 保持上次驱动的值；驱动回读 `gpio_lsqsh.c::gpio_ls_port_get_raw()` =
`OE_DIN & 0xffff` 读的正是该 pad 值 ⇒ 一根脚一旦被驱动过就永远读到旧值，且无任何报错。
只有走 `io_cfg_disable()`（→ `io_cfg_disable_output()`）才真撤销。登记：KNOWN_ISSUES #93。

## 2. 审核执行流程

### Step 1: 确定审核范围
```
输入：修改的文件列表（git diff --name-only）
过滤：只保留 .c / .h 文件
分组：按驱动模块分组（同一目录下的文件为一组）
```

### Step 2: 逐维度扫描
```
对每个文件：
  1. 用 Grep 找到所有 k_mutex_lock / k_sem_take 调用
  2. 对每个调用，读取所在函数全文
  3. 标记 lock 点和所有 unlock 点
  4. 搜索 [lock, unlock] 区间内的 return/goto 语句
  5. 验证每个 return 路径都有 unlock
  6. 同样检查硬件 cleanup 覆盖度
  7. 用 Grep 找到所有高风险函数调用（按 §1.5 类别特征匹配，非硬编码函数名）
  8. 检查每个调用的返回值是否被赋值并检查
  9. 特别关注 err |= 模式中前面调用返回值被静默丢弃的情况
 10. 对每对 enable/disable（或置位/清位分支），按 §1.10 逐位比对"撤销位 vs 使能位"
     （掩码/半区是否一致；撤销是否落到只读回读位）
```

### Step 3: 生成审核报告

```markdown
# 驱动代码审核报告
- 日期: YYYY-MM-DD HH:MM
- 文件: zephyr/drivers/i3c/i3c_linkedsemi.c
- 变更: ls_i3c_configure, ls_i3c_do_daa

## 发现

### P0 — Mutex 泄漏（共 3 处）
| # | 函数 | 行号 | 问题 | 修复 |
|---|------|------|------|------|
| 1 | ls_i3c_configure | L430 | `return -EINVAL` 在持锁状态 | → `goto out` |
| 2 | ls_i3c_configure | L435 | `return -EINVAL` 在持锁状态 | → `goto out` |
| 3 | ls_i3c_do_daa | L1295 | `return -ETIMEDOUT` 在持锁状态 | → `goto out_daa_done` |

### P1 — 硬件 Cleanup 遗漏（共 0 处）
（无）

### P2 — 其他（共 0 处）
（无）

## 结论
- P0: 3 处，必须修复后才能合入
- P1: 0 处
- P2: 0 处
```

## 3. 自动修复模式

对 P0 的 mutex 泄漏，审核 skill 可自动生成修复 patch：

```
对每个泄漏点：
  1. 确认函数末尾的 unlock 位置（L_unlock）
  2. 在 unlock 前插入 goto 标签（如 out:）
  3. 将泄漏的 return 改为：
     ret = <error_code>;
     goto out;
  4. 如果函数原本没有 ret 变量，添加 int ret = 0;
```

## 4. 与其他 Skill 的关系

| Skill | 关系 |
|-------|------|
| **qsh-test-validate** | validate 检查测试工程合规；review 检查驱动代码本身 |
| **embedded-debug** | review 发现问题 → embedded-debug 定位根因 |
| **skill-orchestrator** | 闭环链：STRATEGY_DEV 完成后自动触发 review |

## → 下一步（闭环链）

| 审核结果 | 下一个动作 |
|---------|-----------|
| P0 发现 + 已修复 | → qsh-test-validate（重新验证）→ qsh-test-run（回归） |
| P0 发现 + 需人工判断 | 报告 + 修复建议，等用户确认 |
| 全部 PASS | → qsh-test-run（回归验证） |
| P1/P2 只有建议 | 记录后继续，非阻塞 |

## 链路
> **输入**← git diff / runner(STRATEGY_DEV) · **输出**→ qsh-test-validate · **相关**→ skill-orchestrator, embedded-debug
