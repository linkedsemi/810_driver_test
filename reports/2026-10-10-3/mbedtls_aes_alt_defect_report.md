# mbedtls AES 硬件 ALT 缺陷报告（`aes_alt.c`）

| 项 | 值 |
|---|---|
| 报告日期 | 2026-10-10 |
| 被审文件 | `modules/crypto/mbedtls/library/aes_alt.c` |
| 文件 md5 | `be211a8bf589bff23b5f0a6e4366872d` |
| 文件大小 / mtime | 20896 B / 2026-10-10 19:09:25 |
| 仓库 HEAD | `a367ec250`（"otbn ecdsa update"） |
| 工作区状态 | 该文件为已修改未提交（`M`） |
| 适用配置 | `CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI=y`（片内 CRYPT 硬件引擎，LSCRYPT） |
| 板级实测归档 | `810_driver_test/reports/2026-10-10-3/` |

> **行号已逐条对照上表所载 md5 的文件核对过。**
> 若文件已变动，请以 md5 为准重新核对标号。

---

## 0. 结论速览

**AES 正向通路（标准向量层面）已全部正确**，但驱动层仍有 **9 处缺陷**。
其中 3 处是**现有回归测试永远抓不到的**（标 ★）—— 它们只在**并发、中断、硬件异常、边角计数器**下发作。

| 级别 | 缺陷 | 位置 | 回归能测到吗 |
|---|---|---|---|
| **P0** | 共享寄存器**无任何互斥** | 全文 | ✅ **已补用例并实测复现**（见 P0-1 末） |
| **P0** | 混合 CTR 依赖的硬件计数器语义——**前提已被实测证伪** | `:400-449` | ✅ **已补用例并实测证伪**（见 P0-2 末） |
| **P1** | CBC 用**断言**代替错误码 | `:186` | ✅ **已补用例并实测**（见 P1-1 末） |
| **P1** | CBC `length==0` → **缓冲区外读 / 栈垃圾写进 IV** | `:230-232` | ✅ **已补用例并实测**（见 P1-2 末） |
| **P1** | `crypt_ctr` 的 **`nc_off` 空指针解引用** | `:379` | ✅ 已测出 |
| **P2** | 硬件忙等**无超时** | `:162` `:217` `:421` | ★ 测不到（需故障注入） |
| **P2** | 强制 `uint32_t*`，**假定调用方对齐** | `:60` `:143` `:189` `:191` `:402` | 🟡 **经复核：非缺陷**，仅契约卫生（见 P2-2） |
| **P2** | **`ctx` / `iv` 空指针检查缺失**（ECB `ctx`；CBC `ctx`/`iv`） | `:139` `:178-186` | ✅ **已补用例并实测**（见 P2-3 末） |
| **P3** | `free` **不清硬件 KEY 寄存器** | `:34-47` | ★ 测不到（需 GDB 读回） |

---

## P0-1　共享寄存器无任何互斥

### 机理

LSCRYPT 是**一套寄存器**，所有 AES context 共用：

| 寄存器 | 用途 |
|---|---|
| `KEY0-7` | 密钥 |
| `DATA0-3` | 输入块 |
| `RES0-3` | 输出块 |
| `IVR0-3` | IV / CTR 计数器 |
| `CR` | 模式、算法、使能位（`aes_config()` 整体重写） |

mbedtls 的 context **只在软件里存密钥**（`ctx->key` / `ctx->keybits`），每次运算前重新写进 `KEY` 寄存器。
这**只解决了"顺序使用"（A 用完 B 用），没解决"交错 / 抢占"**。

### 失效序列（多线程）

```
T1: mbedtls_aes_crypt_cbc(ctxA, …4 块…)
      循环 :209-224，每块写 DATA → GO → 轮询 → 读 RES
      ↕  T1 在第 1 块与第 2 块之间被抢占
T2: mbedtls_aes_crypt_ecb(ctxB, …)
      aes_hw_setkey()  → KEY 被换成 B 的密钥                    (:57)
      aes_config()     → CR 的模式从 CBC 改为 ECB，并动 IVREN 位
                       （:219 CBC 已把 IVREN 清零，靠硬件内部链接）
      写 DATA → GO → 读 RES
T1 恢复
      → KEY 是 B 的、CR 不是 CBC、硬件内部 CBC 链接状态也已丢失
      ⇒ 第 2 块起密文全错，且 CBC 链接语义被破坏
```

**中断里同理**：ISR 中做任何 AES 运算，都会打断线程正在进行的运算。

### 关键约束（决定了该选哪种同步原语）

1. **CBC / CTR 不能只锁单块**：块间链接由**硬件内部**完成（第一次 `GO` 后清掉 `IVREN`，后续块硬件自己接）。锁必须**跨越整个缓冲区**。
2. **因此不能用"关中断"类原语跨整个缓冲区**：256 KB @ 15 MB/s ≈ **17 ms 不响应中断**，实时性不可接受。
3. **`k_mutex` 是睡眠锁，ISR 中无法获取**。若产品要求 AES 可被 ISR 调用，必须换自旋类原语 —— 但那与第 2 条冲突。

### 两条可行路线

| | 做法 | 代价 |
|---|---|---|
| **(a)** | 睡眠锁**跨越整个 API 调用**（CBC/CTR）；ECB 每块一次（它是逐块 API）。**明确声明 AES 不可在 ISR 中调用** | API 使用受限 |
| **(b)** | 把 **CBC 改成软件链接**（像现在的 CTR 那样每块独立），锁粒度降到单块 | 每块多一次异或；失去硬件链接加速。收益：可用轻量原语，且支持 ISR |

### 为什么回归此前测不到（现已补齐用例并实测复现）

810 原并发用例只覆盖 SHA + ECDSA，**没有 AES 并发**；各 case 里 AES 全是单线程顺序调用。
这类缺陷**不会报错，只产出错误密文** —— 属最危险的一类。

**新增用例 `aes_concurrent_ctx`（`cipher_test.c`）后实测（2026-10-10，`reports/2026-10-10-4/`）**：

| case | 结果 | INFO |
|---|---|---|
| `case_hw_all`（硬件 ALT） | **FAIL** | `bytes=8192,interleaved_eq_single_threaded=0` |
| `case_sw_baseline`（纯软件） | PASS | `interleaved_eq_single_threaded=1` |

**竞态被实测坐实**：线程 A 对 8 KB 做 CBC 期间被线程 B 的一次 ECB 抢占，
A 的密文与单线程参考值不符。软件基线同用例通过 ⇒ 缺陷在共享硬件寄存器，不在测试逻辑。

用例设计要点（供复核）：ztest 测试线程是**协作式**（优先级 -1），不可被其它线程抢占，
因此"两线程互相打断"在测试线程内造不出来；改为把长操作放进**可抢占的独立线程**，
由测试线程在其跑到半途时放行第二个线程 —— **确定性触发**，不靠碰运气。
用例内亦注明：FAIL 能证明竞态存在，PASS 不能证伪。

---

## P0-2　混合 CTR：硬件计数器递增规则与 mbedtls 不一致（**已实测证伪**）

### 现状

`mbedtls_aes_crypt_ctr`（`:370`）是**混合实现**：

| 区段 | 实现 | 位置 |
|---|---|---|
| 续用上次剩余的密钥流 | 软件 `mbedtls_xor` | `:386-395` |
| **整块** | **硬件 CTR 引擎批量** | `:400-427` |
| 尾块 | 软件（`mbedtls_aes_crypt_ecb` 生成密钥流块） | `:439-452` |

硬件跑完整块后，软件**补做**同样次数的 `mbedtls_ctr_increment_counter()`（`:432-436`）来让 `nonce_counter` 与硬件同步。

### ★ 前提已被实测证伪（2026-10-10，`reports/2026-10-10-4/`）

**前提**：硬件自动递增的计数器必须与 mbedtls 的**大端 128 位整体递增**逐位一致。

补了确定性用例 `aes128_ctr_carry`（计数器 `00000000_00000000_FFFFFFFF`，加 1 必须**跨 32 位字边界**进位），实测：

| 调用形态 | `case_hw_all`（硬件 ALT） | `case_sw_baseline`（纯软件） |
|---|---|---|
| 单次 4 块 | **FAIL**（`single_ok=0`） | PASS（`single_ok=1`） |
| 分段 1 块 + 3 块 | PASS（`segmented_ok=1`） | PASS |
| 解密回环 | **FAIL**（`roundtrip_ok=0`） | PASS |

**结论：硬件计数器递增规则与 mbedtls 不一致 —— 它不跨 32 位字进位。**

这个"单次失败／分段通过／软件基线全过"的组合本身就是决定性证据
（软件基线三项全过 ⇒ 期望向量自洽，失败在硬件路径）：

- **单次 4 块**：计数器 `…FFFFFFFF` 的递增由**硬件**在芯片内部完成，需要进位到高字
  ⇒ 硬件进位规则不符 ⇒ **第 2 块起密钥流即错**。
- **分段 1 + 3**：第 1 段只算 1 块，由**软件**把计数器递增成 mbedtls 语义的正确值；
  第 2 段从该值开始，**其后的递增不再跨字边界** ⇒ 全对。

### 影响（这是真实缺陷，不是理论风险）

**任何 CTR 数据流，只要计数器中途跨过一次 32 位字边界，从那一刻起的密文就是错的** ——
而长缓冲区跨字边界是常态。该错误**不会报错**，只产出错误密文。

这正是客户在修复要求里预先点名的那件事：

> 若硬件 counter 递增规则或字节序无法证明与 mbedtls 一致，
> **应优先使用已有 AES-ECB 原语实现状态机以保证正确性**。

**建议**：按客户给的路径处理 —— 整块路径也改用 `mbedtls_aes_crypt_ecb` 逐块生成密钥流
（即纯软件状态机），放弃"硬件批量 CTR"这条快路径。若坚持保留硬件批量，必须先解决
"硬件递增与 mbedtls 不一致"这一硬伤（例如每块显式重载计数器）。

> 注：既有的 `aes128_ctr_seg` 用的是 NIST F.5.1 计数器 `…FCFDFEFF`，只能压到**字节**进位
> （`…FEFF` → `…FF00`），**压不到 32 位字回绕**，所以该用例一直是 PASS 的 ——
> 这正是这个缺陷此前未被发现的原因。

---

## P1-1　CBC 用断言代替错误码

```c
:186    __ASSERT_NO_MSG(length % 16 == 0);
```

| 配置 | 后果 |
|---|---|
| `CONFIG_ASSERT=y`（当前 810 用例即是） | 非整块长度直接 **kernel fatal**，调用方拿不到错误码 |
| `CONFIG_ASSERT=n`（生产常见） | 断言被编译掉 → `:209` 循环按 16 字节整块跑 `ceil(length/16)` 次 → **多读多写最多 15 字节** |

上游 `modules/crypto/mbedtls/library/aes.c` 的做法：
```c
if (length % 16) { return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH; }
```

> 注：`MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH` 在本文件 `:559` `:564` 有用到，但那是 **XTS** 的长度检查，**CBC 没有用**。

**实测（2026-10-10，`reports/2026-10-10-5/`，用例 `aes_cbc_len_contract`）**：

| case | 结果 | 现象 |
|---|---|---|
| `case_hw_all`（硬件 ALT） | **FAIL** | `ASSERTION FAIL [length % 16 == 0] @ aes_alt.c:186` → **`ZEPHYR FATAL ERROR 4: Kernel panic`**（mcause 11） |
| `case_sw_baseline`（纯软件） | **PASS** | `bad_len_ok=1`（软件实现按上游返回错误码） |

契约差异**精确隔离在硬件 ALT 驱动**。

---

## P1-2　CBC `length == 0` → 缓冲区外读 / 栈垃圾写进 IV

缺少上游的早退（`library/aes.c`：`if (length == 0) { return 0; }`）。

`length = 0` 时 `:209` 的循环不执行，但 **IV 回写照跑**：

```c
:213        unsigned char last_input_block[16];    /* 未初始化 */
:230        memcpy(iv, last_input_block, 16);          /* 解密分支：
                                                          栈垃圾写进调用方的 iv */
:232        memcpy(iv, (unsigned char *)(out - 4), 16); /* 加密分支：
                                                          读 output 之前 16 字节
                                                          → 越界读（信息泄露） */
```

**实测（2026-10-10，`reports/2026-10-10-5/`，同一用例 `aes_cbc_len_contract`）**：
`case_hw_all` 的 `zero_len_ok=0` —— `length==0` 调用后 `iv` 确实被改写（拷进了未初始化的栈）；
`case_sw_baseline` 的 `zero_len_ok=1`（软件实现有早退，`iv` 原封不动）。

---

## P1-3　`crypt_ctr` 的 `nc_off` 空指针解引用

```c
:378    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
:379    size_t offset = *nc_off;        /* ← 无 NULL 守卫，直接解引用 */
```

同文件其它入参都有守卫（`:36` free、`:107` setkey ctx/key、`:139` ECB in/out、`:182` CBC in/out），**唯独漏了 `nc_off`**。

### 实测证据（`reports/2026-10-10-3/`）

`aes_alt_null_params` 用例喂入 NULL `nc_off`：

```
mcause: 5, Load access fault
mtval:  0
mepc:   0x10018458  → mbedtls_aes_crypt_ctr
Current thread: 0x100874f8 (aes_alt_null_params)
>>> ZEPHYR FATAL ERROR 0: CPU exception on CPU 0
 PASS - aes_alt_null_params in 0.159 seconds   ← 假 PASS（结果停在初值）
```

> 该 PASS 是**假 PASS**：异常把测试线程杀掉，`ztest_test_fail()` 从未执行。
> 判定链已修（执行窗口内有致命异常一律判 FAIL），报告中该项现为 **FAIL**。

**对照**：2026-09-29 的版本同一项返回 `ctr_null_ncoff=-33`（有守卫）
⇒ 本版**丢掉了这个守卫**，属回归。

**2026-10-10 二次复测（`reports/2026-10-10-4/`）**：该项**仍为 FAIL**，
故障 PC 仍在 `mbedtls_aes_crypt_ctr`、`mtval=0` ⇒ 确认本文件（md5 `be211a8b…`）**尚未修复**。

**建议修法**：
```c
if (ctx == NULL || nc_off == NULL || nonce_counter == NULL ||
    stream_block == NULL || input == NULL || output == NULL) {
    return MBEDTLS_ERR_AES_BAD_INPUT_DATA;
}
```

---

## P2-1　硬件忙等无超时

```c
:162    while (REG_FIELD_RD(LSCRYPT->SR, CRYPT_AESRIF) == 0);
:217    while (REG_FIELD_RD(LSCRYPT->SR, CRYPT_AESRIF) == 0);
:421    while (REG_FIELD_RD(LSCRYPT->SR, CRYPT_AESRIF) == 0);
```

若引擎因故永不置位（时钟被门控、复位未释放、硬件异常），CPU **死循环且无出口**，调用方永远拿不回控制权。

**建议**：加循环上限，超时后返回错误码。

---

## P2-2　强制 `uint32_t*`（**经复核：不是缺陷，是契约卫生项**）

> **2026-10-14 复核结论：本项不构成本芯片上的功能缺陷，不建议改代码。**
> 保留原分析供参考，但**请勿按"待修缺陷"处理**。

| 位置 | 对象 |
|---|---|
| `:60` | 密钥（`u32_key`） |
| `:143` `:144` | ECB 的 input / output |
| `:189` `:190` `:191` | CBC 的 input / output / iv |
| `:402` `:403` `:404` | CTR 内部的 input / output / nonce_counter |

### 为什么它**不是**问题（三条实测/配置证据）

1. **本核硬件支持非对齐访问**：`CONFIG_RISCV_ISA_EXT_THEAD=y`（T-Head 核，RV32）。
   T-Head 的实现对普通 load/store 提供硬件级非对齐支持；构建配置里**没有**任何
   misaligned 陷入/模拟开关。
2. **实测不发作**：`aes128_ctr_unaligned`、`aes_ecb_cbc_unaligned`（in/out/iv 各偏移 1 字节）**均 PASS**。
3. **无性能悬崖**：`aes128_ctr_unaligned` 耗时 **0.007 s**，与对齐的 `aes128_ctr_inplace` **完全相同**。
   若走"陷入 → 软件模拟"，每条访问都要过一次异常，不可能只差 0。

⇒ **调用方传非对齐缓冲能拿到正确结果，不崩、不慢。**

### 那客户要求 #4 算满足了吗

**意图已满足** —— 客户要求"不假定输入和输出具有 32 位对齐"，而实际行为正是"不要求调用方对齐"。
差别只在于：实现**依赖了核的硬件能力**，而不是显式处理，且**代码里没有注释说明这一依赖**。

### 建议（不建议改代码）

**加一行注释写明依赖即可**，例如：

> `本核（T-Head RV32）硬件支持非对齐字访问，故此处直接按 uint32_t 读取；`
> `若移植到不支持该特性的核，需改为字节访问。`

不建议改成"显式字节访问器"：实测显示那会让 ECB/CTR 吞吐**下降约 40%**（2026-09-29 实测），
而收益仅是"不依赖核特性"—— 在 QSH/LS1010（后者为 ARM Cortex-M，同样允许非对齐）上没有落点。

---

## P2-3　`ctx` / `iv` 的空指针检查缺失

| 函数 | 已检查 | **未检查** |
|---|---|---|
| `mbedtls_aes_setkey_enc`（`:107`） | `ctx`、`key` | — |
| `mbedtls_aes_crypt_ecb`（`:139`） | `input`、`output` | **`ctx`** |
| `mbedtls_aes_crypt_cbc`（`:182`） | `input`、`output` | **`ctx`**、**`iv`** |
| `mbedtls_aes_crypt_ctr`（`:379`） | 无 | **`ctx`**、**`nc_off`**（另见 P1-3） |

**实测（2026-10-10，`reports/2026-10-10-5/`，用例 `aes_alt_null_params`）**：
执行到第 3 次调用 `ecb_null_ctx` 时 **`mcause: 5, Load access fault`，`mtval: 0x20`**，
测试线程被杀。故障前的两条 INFO（`setkey_null_ctx=-33`、`setkey_null_key=-33`）证明
**前面两个入参的守卫是在位的** —— 缺的正是 `ctx` / `iv` 这几个。

---

## P3　`free` 不清硬件 KEY 寄存器

```c
:34     void mbedtls_aes_free(mbedtls_aes_context *ctx)
        {
            if (ctx == NULL) { return; }
            mbedtls_platform_zeroize(ctx, sizeof(mbedtls_aes_context));   /* 只清软件副本 */
            ...
        }
```

硬件 `KEY0-7` 里的密钥**一直留在寄存器中**，直到下一次运算覆盖。

---

## 附录 A　已修复、无需重复处理的部分

以下在**本版本已正确**，请勿重复修改：

| 项 | 证据 |
|---|---|
| **CTR 流式状态机**（`nc_off` / `stream_block` / `nonce_counter` 正确维护） | `aes128_ctr_seg` 实测 `seg_eq_nist=1, state_after7=1, state_after23=1, nc_off_final=0` |
| **XTS 密钥已存进 `ctx->crypt`**（`:523`），文件级全局量已移除 | `aes128_xts_multi_ctx` 实测 `ctx_a_eq_kat=1, ctx_b_eq_kat=1` |
| **非法密钥长度返回 `MBEDTLS_ERR_AES_INVALID_KEY_LENGTH`**（`:96`），不再静默成功 | 源码核对 + 既有用例 |
| **非整块 CTR 长度不再越界** | `aes128_ctr_len` 实测 `len1/len15/len17/len31=1, guard_ok=1` |
| **ECB / CBC 的数据字节序正确** | `aes_ecb_order` 实测三路均等于 FIPS-197 C.1 |
| **AES-192 / AES-256 的密钥装载分支正确** | 新用例 `aes192_256_kat` 实测 PASS（ECB FIPS-197 C.2/C.3 + CBC NIST F.2.2/F.2.3 + 解密回环，两 case 均过） |

---

## 附录 B　回归测试覆盖缺口

现有回归**结构上无法**发现下列缺陷：

| 缺陷 | 为什么测不到 | 需要的新用例 | 本轮是否已补 |
|---|---|---|---|
| P0-1 无互斥 | 三个 case 中 AES 全是单线程顺序调用 | **AES 并发用例**：两个线程各持一个 context，交错做 CBC/CTR，比对密文 | ✅ 已补 `aes_concurrent_ctx`，实测 **FAIL**（坐实竞态） |
| P0-2 计数器语义 | 现有向量只压到字节进位 | **跨字进位向量**：计数器 `00000000_00000000_FFFFFFFF`，≥2 块 | ✅ 已补 `aes128_ctr_carry`，实测 **FAIL**（证伪前提） |
| P2-1 忙等超时 | 正常硬件不会触发 | 需故障注入（运行中门控时钟） | ❌ 未补（需故障注入，另议） |
| P3 硬件 KEY 残留 | 无观测通道 | 需 GDB 读回 `KEY0-7` | ❌ 未补（需 GDB 通道，另议） |
| P1-1 / P1-2 CBC 长度契约 | 无用例 | `length==0` 与 `length=17` 两种调用 | ✅ 已补 `aes_cbc_len_contract`，实测 **FAIL** |
| P2-3 `ctx` / `iv` 空指针 | 原用例只喂 in/out | 补喂 NULL ctx / iv | ✅ 已扩展 `aes_alt_null_params`，实测 **FAIL**（定位到 ECB `ctx`） |
| **AES-192 / AES-256 密钥装载分支零覆盖** | 既有用例全用 128 位密钥 | ECB/CBC KAT | ✅ 已补 `aes192_256_kat`，实测 **PASS**（该分支无缺陷） |

> 另注：`aes128_ctr_inplace` 与 `aes128_ctr_unaligned` 两项在**上一代有缺陷的实现上也是 PASS**
> （它们只覆盖"单次调用 + 整块"，探不到分段/越界类回归）。
> 保留它们有价值（可捉别的缺陷），但**不能当作"CTR 正确"的证据**。

### B-1　已补：AES 并发用例 `aes_concurrent_ctx`

线程 A（可抢占，低优先级）对 8 KB 做 CBC（约 0.5 ms ≈ 5 tick）；
测试线程在 A 跑到半途（2 tick = 200 µs）时放行线程 B（更高优先级）；
B 抢占 A 并对**另一个 context** 做一次 ECB ⇒ 改写共享 KEY/CR；A 的密文必须仍等于单线程参考值。

**实测（`reports/2026-10-10-4/`）**：`case_hw_all` **FAIL**、`case_sw_baseline` PASS。

> 说明：ztest 测试线程优先级 -1 是**协作式**、不可被抢占，故不能让它自己充当"被打断的一方"；
> 用"可抢占线程 + 定时放行"把交错做成**确定性**的。
> 已知局限（用例内亦注明）：若 A 在放行前跑完则退化为"未交错"，故 **FAIL 证明竞态存在，PASS 不能证伪**。

### B-2　已补：CTR 跨字进位用例 `aes128_ctr_carry`

计数器取 `00000000_00000000_FFFFFFFF`，查单次 4 块 / 分段 1+3 / 解密回环三种形态。

**实测（`reports/2026-10-10-4/`）**：`case_hw_all` **FAIL**（`single_ok=0, segmented_ok=1, roundtrip_ok=0`）、
`case_sw_baseline` PASS（三项全 1）。**确定性用例，已证伪硬件计数器的进位规则。**

---

## 附录 C　板级实测汇总

### C-1　2026-10-10 首轮（`reports/2026-10-10-3/`，现有用例）

| case | 结果 | `ZEPHYR FATAL ERROR` |
|---|---|---|
| `case_sw_baseline` | **PASS 31/31** | 0 |
| `case_otbn_all` | **PASS 44/44** | 0 |
| `case_hw_all` | **FAIL 36/38** | 1 |

`case_hw_all` 两个 FAIL：

1. `aes_alt_null_params` —— P1-3（`nc_off` 空指针），**真回归**
2. `rsa4096_modexp` —— **假 FAIL**：180 s 采集窗被前一项 `rsa_keygen_non_f4_fallback`
   （软件 RSA 非 F4 素数搜索）的 151 s 重尾吃满而截断，**与 AES 无关**。

### C-2　2026-10-10 第二轮（`reports/2026-10-10-4/`，补上两个新用例后）

| case | 结果 | `ZEPHYR FATAL ERROR` |
|---|---|---|
| `case_sw_baseline` | **PASS 33/33** | 0 |
| `case_hw_all` | **FAIL 37/40** | 1 |

`case_hw_all` 三个 FAIL，**全部落在 `aes_alt.c`**：

| 项 | 判据 | 结论 |
|---|---|---|
| `aes128_ctr_carry` | `single_ok=0, segmented_ok=1, roundtrip_ok=0` | **P0-2：硬件计数器进位规则被证伪** |
| `aes_concurrent_ctx` | `interleaved_eq_single_threaded=0` | **P0-1：共享寄存器竞态被坐实** |
| `aes_alt_null_params` | `mcause 5 / mtval 0`，PC 落在 `mbedtls_aes_crypt_ctr` | **P1-3：`nc_off` 空指针，仍未修** |

对照：这两个新用例在 `case_sw_baseline`（纯软件）**全部 PASS** ⇒ 失败定位在硬件 ALT 路径，
不是用例、数据或环境问题。

**附带修正**：把采集窗口从 180 s 提到 240 s 后，`rsa4096_modexp` 正常跑完并 PASS
（`us=4775236`）⇒ C-1 里那项确系窗口截断，与驱动无关。

**构建**：两 case 首次编译零错误；新增代码零 warning
（仅 3 条 warning 来自既有无关文件：`rsa_test.c:323 -Wcomment`、`dma_dw_common.h SET_BIT redefined ×2`）。

### C-3　2026-10-10 第三轮（`reports/2026-10-10-5/`，补上密钥尺寸 / 长度契约 / 扩展 NULL 入参后）

| case | 结果 | `ZEPHYR FATAL ERROR` |
|---|---|---|
| `case_sw_baseline` | **PASS 35/35**（SUCCESSFUL） | 0 |
| `case_hw_all` | **FAIL 38/42** | 2 |

`case_hw_all` 四个 FAIL，**全部落在 `aes_alt.c`**：

| 项 | 现象 | 对应缺陷 |
|---|---|---|
| `aes128_ctr_carry` | `single_ok=0` | P0-2 |
| `aes_concurrent_ctx` | `interleaved_eq_single_threaded=0` | P0-1 |
| `aes_cbc_len_contract` | `zero_len_ok=0`，随后 `length=17` 触发 `ASSERTION FAIL @ aes_alt.c:186` → Kernel panic | P1-1 + P1-2 |
| `aes_alt_null_params` | 执行到 `ecb_null_ctx` 时 `mcause 5 / mtval 0x20` | P2-3（+ P1-3） |

**新增覆盖的验证结果**：

| 项 | 结果 | 说明 |
|---|---|---|
| `aes192_256_kat` | **PASS**（两 case） | 192/256 位密钥装载分支**无缺陷** —— 该覆盖缺口以"绿"收口 |
| `aes_cbc_len_contract` | hw **FAIL** / sw PASS | CBC 长度契约差异隔离在硬件 ALT |
| `aes_alt_null_params`（扩展后） | hw **FAIL** | 又定位到 **ECB 缺 `ctx` 检查**（原用例只喂 in/out，测不到） |

**零回归**：`hw_all` 相对 C-2 只多出这两个新项（一 PASS 一 FAIL），其余 **40 项逐项 verdict 不变**；
`sw_baseline` 相对 C-2 只多两项且均 PASS。

**可诊断性改进已验证**：`aes_alt_null_params` 原先把 INFO 汇总在末尾，一旦某次调用 fault
就整条 INFO 丢失（C-1/C-2 里该项都是"FAIL 无 reason"）。改为**每次调用后即时打印**后，
C-3 归档里能看到故障前的 `setkey_null_ctx=-33`、`setkey_null_key=-33` —— 直接指认出
漏守卫的是后面几个入参，而不是前面两个。

### C-4　2026-10-14 收口（`reports/2026-10-14-1/`、`-2/`）—— **本报告所列缺陷全部消除**

被测驱动 md5 **`e3031a2715bd82ddb1c963c02bdc4f08`**（mtime 2026-10-14 16:14:10）。

| case | 结果 | `ZEPHYR FATAL ERROR` |
|---|---|---|
| `case_hw_all` | **PASS 42/42**（SUCCESSFUL） | **0** |
| `case_sw_baseline` | **PASS 35/35**（SUCCESSFUL） | **0** |
| `case_otbn_all` | **PASS 48/48**（SUCCESSFUL） | **0** |

**⚠️ 口径提醒（重要）**：`CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI` **只在 `case_hw_all` 置 y**
（另两个 case 的 `aes_alt.c.obj` 根本不存在）。⇒ **只有 `case_hw_all` 验证硬件驱动**；
`case_sw_baseline` / `case_otbn_all` 全绿只说明**软件 AES 未退化**，不能用作硬件驱动的证据。

**逐条对账**：

| 缺陷 | 状态 |
|---|---|
| P0-1 无互斥 | ✅ `aes_hw_mutex`，`aes_concurrent_ctx` PASS |
| P0-2 硬件计数器跨字进位 | ✅ 改为**按低 32 位字回绕边界分片**，`aes128_ctr_carry` FAIL→PASS |
| P1-1 CBC 断言代错误码 | ✅ 返回 `MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH`，`aes_cbc_len_contract` PASS |
| P1-2 CBC `length==0` | ✅ 补早退，同上用例 `zero_len_ok=1` |
| P1-3 `nc_off` 空指针 | ✅ 补齐守卫，`aes_alt_null_params` 全项 `-33` |
| P2-1 忙等无超时 | ✅ `aes_hw_wait_done()`，超时 100 ms |
| P2-3 `ctx`/`iv` 空指针 | ✅ 补齐 |
| P3 `free` 不清硬件 KEY | ✅ 清零 `KEY0-7` |
| P2-2 强制 `uint32_t*` 对齐 | 🟡 **经复核：非缺陷**（本核 T-Head RV32 硬件支持非对齐访问；实测不崩不慢）—— 建议仅加注释说明依赖 |

**最后一项残留的收口**：`aes128_ctr_carry` 在 C-3 时只覆盖 **1 种分片形状**（`until_wrap == 1`）。
本轮把它加强为 **4 种形状 × 3 种调用形态**后复测：

```
INFO aes128_ctr_carry: n=4,single_ok=1,segmented_ok=1,roundtrip_ok=1,
                       single_bits=1111,seg_bits=1111
```

`until_wrap` = 1 / 2 / 4 / 16（分别对应批次 1+3、2+3、4+2、单批）在单次与分段两种调用下
**全部正确** —— 这段新写的分片算术至此才算真正被门禁住。
