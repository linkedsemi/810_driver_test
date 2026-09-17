# 异常/中断入口 while(1) 挂死 → 读 M 态 CSR 重建现场

## 现象
固件**烧录成功、继续运行后串口完全静默**（无 banner、无 `\x00` 复位噪声）；
GDB attach 发现 PC 停在 trap 入口的 `while(1)`
（如 SoC 的 `isr_stacking_mcause` / `isr_unstacking_mcause` 分支——可能是永久兜底，
也可能是调试期**临时加的陷阱**，见"处理"）。表面像"console/串口配置问题"，
实际是**固件已死在异常入口**。

## 根因
RISC-V 用同一个 `mcause` 承载中断与异常（bit31 区分）。trap 入口的派生代码通常只
放行**预期内的 trap**（中断，或本 SoC 约定的 ECALL-M 之类，如 flash 驱动的
suspend/resume 机制），其余一律 `while(1)` 兜底。命中兜底 ⇒ 发生了**未预期异常**
（或嵌套层级溢出）。CPU 停在兜底循环里，自然一点输出都没有。

## 判定（GDB attach 后逐条读）
1. `info registers pc` / `bt` → 确认卡在 trap 入口 `while(1)`（认符号名）。
2. 读 M 态 CSR（本机 XuanTie E906；通用 RISC-V 同理）：

   | CSR | 看什么 |
   |---|---|
   | `mcause` | bit31=0 → **异常**（≠中断）；低 4 位=异常码：2=非法指令 · 5=load 访问错 · 7=store 访问错 · 11=ECALL-M（本 SoC 约定放行） |
   | `mepc` | **触发异常的指令地址** → `info line *$mepc` / `list *$mepc` 定位源码行。⚠️ 实测有**不吻合**的情形（见"实例"），**以 `mtval` 为准** |
   | `mtval` | 访存异常的**出错地址**；非法指令时的指令编码 |
   | `mstatus` | MPP（出事前特权级）/ MPIE（出事前中断使能） |
   | `mintstatus` / `mexstatus` | （XuanTie 自定义）当前活动中断级别 / 异常处理状态 |

3. 读**固件自维护的现场变量**——异常路径上设计者留的记账变量往往比 CSR 给出更完整的上下文：
   `irq_nested_level[_cpu_id]` 判命中的是哪个兜底（未预期异常 / 嵌套溢出 / 退栈层级为 0，
   三者修法完全不同）；`irq_nested_mcause[][]` 是**留存的异常类型历史**，能看出是否嵌套、
   前几级各是什么。**排查前先 grep 一圈有没有这类"现场记录仪"。**
4. 拿 `mepc` 配同一份 ELF 反查源码即得"谁触发了异常"；到此现场重建完成。

## 处理
- `mepc` 指向的源码行 = 现场 → 按 embedded-debug §1 分层定位该处为何异常
  （空指针越界、非法指令、PMP/权限、栈溢出……）。
- 入口的 `while(1)` 是**停住现场的桩，不是根因**：不要改它，要顺着 `mepc` 去现场。
- **区分永久兜底 vs 调试探针**：`git diff` 该文件——工作区未提交（`M`）的 `while(1)`
  多半是"把异常卡在入口便于 attach 抓现场"而临时加的探针（committed 版本里没有）。
  两者命中时的动作相同（都是读 CSR），但收尾不同：探针调试完按探针纪律还原
  （embedded-debug §4），永久兜底保留。
- 若现场确实需要"卡住再抓"，主动在异常入口加这种 `while(1)` 陷阱是**允许且有效**的
  手法——它把瞬态异常变成可 attach 的静止现场；用完即还原。

## 实例（flash_test）
卡在 `isr_stacking_mcause` 的 `while(1)`（用户临时加的陷阱）时实测：

| 寄存器 | 值 | 解读 |
|---|---|---|
| `mcause` | `0x38000005` | bit31=0 → 异常；异常码 `5` = **load access fault**；bit27=MPIE=1；bit[29:28]=MPP=3 → 出事于 M 态 |
| `mtval` | `0x8` | 出错地址 = **0x8** ⇒ **NULL 指针 + 结构体偏移 8 的读** |
| `mepc` | `0x10004c08` | 落在 `flash_area_erase()` 内（gdb `list *$mepc` 给出函数窗口） |
| `mexstatus` | `0x134` | T-Head 自定义，记录异常状态（位域查 E906 手册） |

`flash_area_erase` 里能在地址 0x8 出错的 `lw ...,8(reg)` 有**三处**，共同点是
**`fa → fa->fa_dev → dev->api` 链条上有 NULL**：

| 指令 | 语义 | NULL 项 |
|---|---|---|
| `10004bf0 lw a3,8(a0)` | `fa->fa_size`（首条访存） | `fa == NULL` |
| `10004c00 lw a5,8(a0)` | `dev->api`，其中 a0 = `fa->fa_dev` | `fa->fa_dev == NULL` |
| `10004c02 lw a5,8(a5)` | `api->erase`，其中 a5 = `dev->api` | `dev->api == NULL` |

（`fa == NULL` 时更早的 `lw a5,4(a5)` 会落在 0x4、`lw a0,12(a0)` 落在 0xC，
但都晚于 0x10004bf0，故仍先在 0x8 出错。）**单凭 `mtval` 分不出是哪一处**——
用**全局变量**逐个排除（`p/x fa` → `p/x fa->fa_dev` → `p/x fa->fa_dev->api`）。

**注意**：本例 `mepc`(`0x10004c08`) 指向 `li a0,-22`（非访存指令），与 `mtval`(`0x8`) 不吻合。
**这里的 CSRs 是可信的**——卡在入口时整机静止：trap 入口硬件关闭中断（`mstatus.MIE`），
本 SoC 只在分工路径 `__soc_handle_all_irqs` 里才 `__enable_irq()`；会改写 `mcause` 的
`csr_write(mcause,…)` 全在 while(1) **之后**，不曾执行。既然 CSRs 为本次 trap 实时值，
不吻合只有两种解释：
① 运行的 ELF 与手上的 `.lst` **不是同一次构建**（改过分区/配置的工程极易发生）；
② `mepc` 对该异常不保证等于精确出错指令。

判定：在目标上 `x/i $mepc` / `x/4i <候选指令地址>` 与 `.lst` 逐字节比对。
**无论哪种，都以 `mtval` 为准**，并用不受入口寄存器污染的**全局变量**（如 `p/x fa`）交叉确认。

### 本例闭环结论（2026-09-16 板上实测）

**现场 = `flash_area_erase(fa = NULL, 0, 4096)`** —— 是 **`fa` 本身为空**，
不是 `fa->fa_dev` / `dev->api` 为空（设备树/驱动侧无问题，`fa_dev` = qspi 控制器设备、`api` 非空）。

**决定性证据不是 CSR，而是"断在 callee 入口打印入参"**（比按 CSR 反推空指针是谁可靠得多）：

| 断点 | 观察 | 结论 |
|---|---|---|
| `flash_area_erase` 入口 + `printf "fa=%x", $a0` | 三次调用：`10010364`、`10010364`、**`0`** | 第三次入参就是 NULL，故障点确定 |
| `flash_area_open` 入口 + 命中点 | **只命中一次** | open 是**套件级**，只跑了一次 |
| setup / teardown 函数入口打印 `$ra`+时间序 | teardown 在**两个用例之间**执行 | teardown 是**用例级**（每例后都跑） |

**根因**：ztest 的 `ZTEST_SUITE(name, predicate, setup, before, after, teardown)`
——**`setup`/`teardown` 是套件级、`before`/`after` 是用例级**。该工程把 `flash_test_setup`
放在套件级、`flash_test_teardown` 放进了 `after` → 第一个用例跑完 teardown 就把 `fa` 置 NULL，
第二个用例拿到 `fa==NULL` 直接崩。修法是把 teardown 挪进 `teardown` 槽
（**不能挪进 `before`/`after`**：两者形参是 `void(*)(void*)`，而 setup 是 `void*(*)(void)`，类型不通）。

**`mepc` 不吻合的结论**：目标上 `x/2xh $mepc` 与 `.lst` **逐字节一致**（`0x5529 0x8082`）
⇒ 排除解释①（镜像不同步）；属解释②——**该核对本次异常给出的 `mepc`
不等于精确出错指令**：真实 faulting 指令是 `c.lw a3,8(a0)`@`0x10004bf0`，
`mepc` 却报 `0x10004c08`（同函数内的分支汇合点，`li a0,-22`）。

**固化为纪律**：
- CSR（`mcause`/`mtval`）只用来定"**出了什么事**"（异常码 + 出错地址）；
  "**谁把坏指针传进来**"必须靠 **断点打印入参 + 全局变量**，不要试图从 `mepc` 反推源码行。
- 排"镜像不同步"最快的一步：目标 `x/2xh $mepc` vs `.lst` 逐字节比对（本次一步排除）。

## 关联
- 方法学：embedded-debug §0（烧录成功 + 串口静默 ⇒ 先抓 CPU，勿循环查串口）·
  §2.1（目标已挂死时的现场抓取）· §4（探针纪律：while(1) 陷阱用完还原）
- 实操：qsh-debug §6（GDB 命令模板）· §7 速查表
- 同族现象（先查崩溃再查配置）：qsh-debug §7「串口 0 行」行 · KNOWN_ISSUES #40
