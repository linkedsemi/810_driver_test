# O-39 —— 那条 `Address NACK` 日志由哪个调用点产生、生成于何时

- **日期**：2026-09-24
- **板卡**：`lsqsh_evb@2os`（cpu1 = target / cpu2 = controller），本会话独占
- **被测**：`i3c_i2c_switch_test` → `case_switch_core`（含 `broadcast_nack_recovery`）
- **方法**：**GDB 断点**（`ls_i3c_log_err_type` @ controller ELF `0x1000a0c4`）+ 双串口原样采集；
  **零源码改动、零探针**（断点不是探针，无需回退）
- **结论**：**落在判读表第一支 —— 那条 `Address NACK` 与窗口内那笔读无关**；
  且**当前（B2 修复后）构建根本不产生这条日志**：两次断点跑共 40 次命中，**`SER.ANACK` 从未置位**，
  通用路径 `:1161` **从未命中**。附带更正一条既有推断：**deferred 日志的时间戳 = 生成时刻**
  （打印顺序乱，时间戳不乱）。

---

## 1. 五个调用点核对（任务书要求先核）

`zephyr/drivers/i3c/i3c_linkedsemi.c`，`grep -n "ls_i3c_log_err_type"` 实数：

| 任务书给的 | 真实源码 | 结论 |
|---|---|---|
| `:1161` 通用传输路径 | `:1161`（`ls_i3c_request_transfer_flag` 末尾兜底；查 `EVR.ERRF` **或** `SER.ANACK\|DNACK\|COVR\|DOVR`，检出即 `msg_state=ERR` → `return -EIO`） | ✅ 对上 |
| `:1304` DAA | `:1304`（`ls_i3c_do_daa` 的 `out_daa` 后；查 `EVR.ERRF`） | ✅ 对上 |
| `:1761` `out_ccc_stop` 兜底 | `:1761`（`ls_i3c_do_ccc`；**只查 `EVR.ERRF`**） | ✅ 对上 |
| `:2364` | `:2364`（`ls_i3c_isr` 的 **ERR 中断分支**；调用前已 `ClearFlag_ERR`，**未清 SER**） | ✅ 对上 |
| `:2454` | `:2454`（`ls_i3c_isr` 的 **FC 帧完成分支**里复查 ERR；驱动注释原文即"…会把错误帧（**如地址 NACK 的读**）当成功帧 give 信号量 → 上层以 **ret=0/actual=0** 返回"） | ✅ 对上 |

**5/5 全部核准，与任务书一致，无需纠正。** 日志本体 `:2302`（`LOG_ERR("%s: Address NACK")` 在 `:2308`）。

> 附：`LL_I3C_ClearFlag_ERR` 只写 `CEVR`（`ls_ll_i3c_controller.h:2344`），**不清 `SER`**；
> 故在 `:2364` 断点处读到的 `SER` 是**真实值**，不是被清后的空值。（这是本次读数有效性的前提。）

---

## 2. 实验环境：两个必须先讲清的障碍

### 2.1 **单 GDB 会话控不了对侧核 —— 会有"静默致命"副作用（实测）**

从 **1025**（CPU#0）对控制器固件下**软件断点**（`break *0x1000a0c4`），断点**不会**被 gdb 上报，
而是被控制器固件的异常处理接管 —— 串口打出：

```
<err> os:  mcause: 3, Breakpoint
<err> os:    mepc: 1000a0c4
<err> os: call trace:  0: sp: ... ra: 1000a0c4
                       1: sp: ... ra: 1000b5e6   ← ls_i3c_do_ccc:1762
<err> os: >>> ZEPHYR FATAL ERROR 0: CPU exception on CPU 0
```

⇒ 复现并坐实了 `qsh-debug §6` 的说法：**必须走 1026**。
（副作用：会把被测固件打成 `ZEPHYR FATAL ERROR`，该轮数据作废 —— 这是"只看命中数"发现不了的那类静默失败。）

### 2.2 1026 的两个前提，以及为什么最终**不用 reset**

- 1026 只在 **DS 启动时 cpu2 已脱离复位**才监听；且**任何 `reset` 之后 1026 会连不上**
  （实测 `error 138` / `Remote communication error. Target disconnected`）。
- 而软件断点写在 RAM 里，`restore controller.bin 0x10000000` 会**把断点擦掉** —— 所以断点必须**装 restore 之后**，
  于是"先 reset+restore 再连 1026 装断点"这条顺序**走不通**。

**最终采用（Plan E，全程不 reset）**：

```
0. bootstrap：正常跑一次固件，让 cpu2 进入运行态 → 重启 DebugServer → 1026 出现
1. A  (1025): file target.elf + load + set $pc=0x100c0000            → detach   [大块写 1]
2. BP (1026): target remote :1026（cpu2 运行中 ⇒ 可连，连接即 halt）+ file controller.elf   [保持连接]
3. B  (1025): restore controller.bin binary 0x10000000              → detach   [大块写 2]
             （A/B 两次大块写必须分属两个 gdb 会话，否则 DebugServer 卡死）
4. BP (1026): break *0x1000a0c4 + commands(bt / EVR / SER / continue) + set $pc=0x10000000 + continue
5. C  (1025): set $pc=0x100c0000 + continue
```

> 踩坑记录：`-x` 命令文件模式**遇错即中止整个脚本**（KNOWN_ISSUES #61）—— 第一版 B 段里写了无效的
> `set memory-write-packet-size`，导致 `reset`/`restore` **一条都没跑**，而日志只留一行
> `No symbol table is loaded`，极易误判成环境问题。**判据：每段 gdb 收尾都断言关键动作真的发生了**
> （本实验断言 B 段出现 `Restoring binary file`，否则中止）。

---

## 3. 命中数据

**两次独立跑，共 40 次命中**（均为 `break`（软件断点），1026 通道）：

| 轮次 | 命中总数 | `ls_i3c_isr:2364`（ISR ERR 分支） | `ls_i3c_do_ccc:1761`（out_ccc_stop） | `:1161` | `:1304` | `:2454` |
|---|---|---|---|---|---|---|
| 第 1 轮 | 19 | 17 | 2 | 0 | 0 | 0 |
| 第 2 轮（带时间戳） | 21 | 20 | 1 | 0 | 0 | 0 |

**寄存器读数（逐次一致，无例外）**：

| 调用点 | `EVR`(0x40094158) | `SER`(0x40094160) | 会打印什么 |
|---|---|---|---|
| `ls_i3c_isr:2364`（×37） | `0x00001103`（BUSY+bit8+TXFEF+CFEF；**ERRF 已被本分支先清**） | **`0x00000000`** | **什么都不打**（`ls_i3c_log_err_type` 逐位查 SER，全 0） |
| `ls_i3c_do_ccc:1761`（×3） | `0x00001903` | **`0x00000010` = `PERR`** | `Illegally formatted CCC detected` |

**关键否定证据**：
1. **`SER.ANACK`(0x100) 在整个实验里从未置位** —— 40 次命中里一次都没有。
2. **通用路径 `:1161` 一次都没命中** —— 即：没有任何一笔 I3C 传输走进"检出错误位并 `return -EIO`"这条分支。
3. 阶段归属（第 2 轮时间轴）：`:1761` 那一次在**启动期 `ls_i3c_init → i3c_bus_init →
   `i3c_device_basic_info_get → i3c_ccc_do_getmrl`**（boot，与用例无关）；`:2364` 的 20 次
   均匀散在**每个用例项开始时**（每个 `CTRL:START:<item>` 之后 ~70ms 各 1~2 次），
   **`broadcast_nack_recovery` 阶段内一次都没有**。
4. 三次朴素单跑（无断点）+ 任务书原文的 `run_test.py --case case_switch_core` + **归档的
   B2 修复后日志**：`grep -c "Address NACK"` 全部 = **0**。

---

## 4. 判读（机械对应）

| 判读表条件 | 实测 | 结论 |
|---|---|---|
| 命中点能对上"本就该失败"的操作 ⇒ 与窗口内那笔读无关 | 命中的两个点分别是**启动期 GETMRL 收 PERR** 与**每项起始处的 ISR ERR**；**没有一次 `SER.ANACK`**；通用路径 `:1161` 零命中 | **成立** |
| 命中点只可能是读那笔、且 `SER.ANACK` 在返回后才置位 ⇒ 真问题 | 若要成立，读那笔必须走通用路径并置 `SER.ANACK`；实测 `:1161` **零命中**、`ANACK` **零置位** | **不成立** |
| 断点一次不命中 ⇒ 那轮没有 ANACK | 断点命中很多，但**没有一次是 ANACK** | **等价结论成立** |

**落点：第一支（与窗口内那笔读无关）**。理由链：

1. 窗口内那笔读若真的拿到地址 NACK，走的是通用路径 `:1161`，该分支检出 `SER.ANACK` 即
   `msg_state=ERR` 并 **`return -EIO`** —— 与观测到的 **`ret=0`** 直接矛盾；实测该分支**从未进入**。
2. 当前树里唯一"负路径"是 `i2c_nack_bad_addr`（`i2c_transfer(..., 0x70)`，**走 i2c 驱动**，
   不会产生 i3c 驱动的 `Address NACK`）；其余 I3C 定向传输一律打向 0x32（target 在位）；
   窗口内的广播按设计**不置 ANACK**。
3. ⇒ **B2 修复后的树里根本不存在能置 `SER.ANACK` 的操作** ⇒ 那条日志**不可复现**，
   它属于**当时存在、现已不在树中**的一次操作（`broadcast_nack_recovery` 现在的负路径已被
   B2 修复消掉；探针轮的重复扫描读也已整体删除）。

> ⚠️ **诚实边界**：本实验**直接否证**了"窗口内那笔读产生该日志"，但**没有直接观测到**那次 ANACK
> （当前树无法造出它）。"它来自哪个已消失的操作"是**结构性推断**，不是观测。

---

## 5. 关于「deferred 打印顺序 ≠ 生成顺序」——**部分认，且要更正一半**

### 5.1 **打印顺序 ≠ 生成顺序：认**

直接证据（`reports/2026-09-24-1/i3c-b1b2-probe-run6.log` L118–119）：

```
[00:00:00.435,000] <err> i3c:B1B2PROBE:S t=28ms ret=0 d0=AA d1=AB d15=B9
 i3c@40094000: Address NACK
```

一条 `<err>` 被**劈成两半**、中间插进**一整行** `printk`。这只有"日志线程写了一半、另一线程
插进来写完整行"才可能出现；本工程 `cpu0_i3c_controller/prj.conf:2` = `CONFIG_LOG_MODE_DEFERRED=y`
正好提供两个写者。同一形貌在同文件 L36 / L41 / L50 / L55 / L59 / L176 反复出现（非孤例）。
⇒ **串口里一行的物理位置不可信。**

### 5.2 **"所以生成时刻不可定 / 可来自更早的任意一次操作"：不认**

`LOG_ERR()` 宏在**调用线程内**就完成消息打包与提交，时间戳在这一刻写入：

```
zephyr/include/zephyr/logging/log_msg.h:375   z_log_msg_finalize(_msg, ...)   // 宏展开在调用点
      → zephyr/subsys/logging/log_msg.c:51     z_log_msg_commit(msg)
      → zephyr/subsys/logging/log_core.c:690   msg->hdr.timestamp = timestamp_func();
```

deferred 只延迟**格式化与输出**，不延迟时间戳的采集。⇒ **那条日志自带的
`[00:00:00.435,000]` / `[00:00:00.612,000]` 就是它的生成时刻**（且两次是不同的 `LOG_ERR`
调用、两笔独立事件，不是同一条被打印两遍）。

**所以正确的表述是**：deferred 下 **"串口位置"不可信、"时间戳"可信** —— 而不是"生成时刻无从得知"。
这正好解释了 probe 日志的观测：日志的**串口位置**落在探测行中间（看起来"紧跟那次读"），
但它的**时间戳**把它钉在 uptime 435ms / 612ms，与"打印位置"之间没有必然关系。

---

## 6. 给留档的可复用手法

1. **deferred 日志：`位置不可信、自带时间戳=生成时刻`**（§5）。此前 O-39 的两版推断
   （"ANACK 晚 15ms" / "驱动窗口过窄"）都源于把"打印晚"读成"置位晚"；本条把两者**分开**。
2. **单 gdb 会话对另一核下断点 = 静默致命**：断点不上报，固件吃 `mcause=3 Breakpoint` →
   `ZEPHYR FATAL ERROR`。判据是**串口出现 `os:  mcause: 3, Breakpoint`**，不是"断点没命中"。
3. **`reset` 之后 1026 连不上**（`error 138`）⇒ 要在 cpu2 侧下断点，必须
   **先占 1026、再写 RAM、全程不 reset**（§2.2 Plan E）。
4. **`-x` 命令文件遇错即中止**（KNOWN_ISSUES #61 的另一面）：一段 gdb 脚本"静默什么都没做"
   比报错更危险 ⇒ 每段脚本收尾都要**断言关键动作真的发生**（如 `Restoring binary file`）。

---

## 7. 证据文件

| 文件 | 内容 |
|---|---|
| `tmp/o39_exp/gdb_BP.log` | 第 2 轮 1026 断点会话完整输出（21 次命中，含 `bt`/`EVR`/`SER`） |
| `tmp/o39_exp/ctrl.log` / `tgt.log` | 同名轮次的双串口原样采集（`Address NACK` = 0） |
| `tmp/o39_exp/run.log` | 带统一时间戳的命中 × 用例阶段对照 |
| `tmp/o39_exp/run_o39.py` / `run_o39b.py` / `run_o39c.py` | 烧录+采集 / 断点实验（Plan E）复现脚本 |
| `reports/2026-09-24-1/i3c-b1b2-probe-run6.log` | 既往：那条被劈开的 `<err>`（L118–119）与两处 `Address NACK`（L118/L176） |

- **零源码改动自证**：全程只做 `target remote / file / load / restore / break / bt / continue / detach`，
  未写任何源文件；`tmp/o39_exp/` 在 `.gitignore` 的 `tmp/` 内。
- **git 只读**：全程未执行 `git checkout/restore/stash/reset`。
- **收尾**：碰过 1026 ⇒ 实验结束后已重启 DebugServer（见任务收尾）。
