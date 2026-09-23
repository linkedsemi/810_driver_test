# spi_test `link_probe` 根因定位报告（2026-09-23，源码级，未上板）

| 项 | 值 |
|---|---|
| 工程 | `810_driver_test/projects/spi_test` |
| case | `case_default`（单核 `lsqsh_evb@1os/lsqsh/cpu1`） |
| 失败项 | `link_probe` —— **唯一 FAIL**。报告口径 `FAIL (18/19)`（18 PASS + 1 FAIL；另 7 项 SKIP 不计入分母）。套件共 26 项：`spi` 18 项 + `spi_loopback` 8 项（1 FAIL + 7 SKIP）；跳线接好后应为 26/26 |
| 报告 | `reports/2026-09-23-1/spi_test_case_default/spi_test_report.{md,json}`、`serial_TGT.log` |
| 断言位置 | `projects/spi_test/cpu1_spi_test/src/spi_loopback.c:454` |
| 涉及驱动 | `zephyr/drivers/spi/spi_ls.c`（spi1，slave）+ `zephyr/drivers/spi/spi_dw.c`（spi3，master） |
| 本次动作 | **未改驱动、未改固件、未改判据**；仅修正 1 处报告头接线声明（§8） |
| 上板动作 | 无（板子被另一路任务占用，严格只读） |

---

## 0. 结论（TL;DR）

1. **分层定论 = C（环境：`SPI1↔SPI3` 外部回环跳线未接）**，**不是驱动缺陷**，也不是用例逻辑写错。
   与 09-22 `tmp/final/triage.md §4.3` 的既有归因一致（该处记「C（跳线未接）/ B（未同步降级）」）。
2. `slave_ret=-119` **不是驱动返回的错误码**，是用例自己设的**哨兵初值** `-EINPROGRESS`
   （`spi_loopback.c:319`）。含义是「从设备线程被 `k_thread_abort()` 前，`spi_transceive()` 从未返回」。
   驱动侧真要说返回值，只可能是 `spi_ls.c:362-366` 的 `recv_frames` 或 `-ETIMEDOUT(-116)`，**都不可能是 -119**。
3. `state=0` = `LB_LINK_BAD`（`spi_loopback.c:364`），由 `lb_probe_link()` 的 5 条判据失败推出（§4）。
4. 这是**陈年既有项**，不是新回归：`spi_ls.c`/`spi_dw.c`/`spi_context.h` 在本回归窗口内**零改动**；
   09-21 / 09-22 / 09-23 三份报告的 `link_probe` INFO **逐字相同**；`OPEN_ITEMS.md` O-18 已把它写作基线。
5. `spif_test` 的 2 项 FAIL **与本项不同源**，不要合并处理（§7）。

---

## 1. 现象（回原始报告复核，纠正任务书转述）

`reports/2026-09-23-1/spi_test_case_default/serial_TGT.log`：

```
:88  Running TESTSUITE spi_loopback
:90  START - link_probe
:91  INFO link_probe: master_ret=0,slave_ret=-119,state=0
:93      Assertion failed at .../spi_loopback.c:454: spi_loopback_link_probe: (lb_link_state not equal to LB_LINK_OK)
:95  [inf] spi_loopback: link probe: master_ret=0 slave_ret=-119 timed_out=1 mrx0=ff stx0=c0 srx0=00 mtx0=30 -> BAD
:96   FAIL - link_probe in 0.531 seconds
:98-117  7 项 SKIP（repeat_roundtrips_state_isolation / roundtrip_fifo_boundary /
         roundtrip_length_sweep / roundtrip_master_tx_slave_rx / roundtrip_max_length_256 /
         roundtrip_pattern_matrix / slave_ret_recv_frames）
:121 PROJECT EXECUTION FAILED
```

**任务书里被我推翻/修正的四点**：

| 任务书原文 | 复核结论 |
|---|---|
| 「`slave_ret=-119`，说明 master 发得出去、slave 收不上来」 | **半对**。从机确实什么都没收到；但 `master_ret=0` **不能证明主机"发得出去"** —— DW SPI 主机的完成性只取决于自身 FIFO/状态机推进，与总线上有没有对端无关（`COVERAGE.md` §4「可判定性来源」已写明）。主机 8 个时钟打进了空气。 |
| 「-119 是 slave 侧驱动返回值」 | **错**。是用例哨兵初值，见 §3。 |
| 「可能只是单核工程里 loopback 需要一个真实从设备」 | **错**。对端是**本芯片自己的 spi1**（作从设备），不是外部器件；缺的是一条**物理跳线**。 |
| 报告「接线: 无（spi1/spi2 片内引脚组…总线上无从设备）」 | **该行是 `run_test.py` 写死的模板文字，对本 case 不成立** —— P4.6 起套件 `spi_loopback`（8 项）有外部前置条件。本轮任务书的误判正是引自这一行。已修正，见 §8。 |

「slave 侧驱动未实现/未使能」也被排除：`CONFIG_SPI_SLAVE=y`（`cpu1_spi_test/prj.conf:42`），
`spi_ls_data_exchange_slave()`（`spi_ls.c:207-236`）存在且会被走到 —— 见 §5 旁证 C。

---

## 2. 关键变量语义

| 变量 | 语义 | 来源 |
|---|---|---|
| `master_ret` | 主机 `spi_transceive(lb_master, …)` 的返回值 | `spi_loopback.c:339` |
| `slave_ret` | 从机线程写入的 `spi_transceive` 返回值；**未返回则是哨兵 `-EINPROGRESS`** | `:319`（初值）、`:204`（唯一真写点）、`:355`（回读） |
| `state` | `lb_link_state`：`-1`=UNKNOWN / `0`=BAD / `1`=OK | `:363-367` |
| `timed_out` | `k_thread_join(…, 500ms)` 是否超时（`LB_JOIN_MS=500`，`:109`） | `:343-345` |
| `mrx0/stx0/srx0/mtx0` | 主收 / 从发 / 从收 / 主发 的**首字节** | `:416-420` |

---

## 3. `slave_ret = -119` 的完整追链（任务书问题 2）

从 `spi_loopback.c:454` 的断言点往上，**-119 根本没穿过 Zephyr SPI API，也没到过驱动**：

```
spi_loopback.c:319   lb_slave_ret = -EINPROGRESS;        ← 哨兵初值，每轮 lb_roundtrip() 都设一次
spi_loopback.c:330   k_thread_create(&lb_slave_thread, …)  ← 从机线程挂上去
spi_loopback.c:339   r.master_ret = spi_transceive(lb_master, …)   ← 主机：返回 0
spi_loopback.c:343   r.join_rc = k_thread_join(&lb_slave_thread, K_MSEC(500));
spi_loopback.c:344   if (r.join_rc != 0) { r.timed_out = true;   ← 实测 timed_out=1 走这里
spi_loopback.c:351       k_thread_abort(&lb_slave_thread);        ← 线程被强杀，Δ 没机会写 lb_slave_ret
spi_loopback.c:352       (void)spi_release(lb_slave, &lb_cfg_s); }
spi_loopback.c:355   r.slave_ret = lb_slave_ret;          ← 仍 === -EINPROGRESS === -119
```

`lb_slave_ret` 的**唯一真正写入点**是 `lb_slave_worker()` 的 `:204`
（`lb_slave_ret = spi_transceive(lb_slave, &lb_cfg_s, …)`）。
`timed_out=1` ⇒ 该赋值从未发生 ⇒ `-119` 只能来自 `:319` 的哨兵。

**errno 换算（不凭印象，两个 libc 都查了固件实际用的那套）**：
- `zephyr/lib/libc/minimal/include/errno.h:103` → `#define EINPROGRESS 119`
- `modules/lib/picolibc/newlib/libc/include/sys/errno.h:163` → `#define EINPROGRESS 119`
  （本工程 `CONFIG_PICOLIBC=y`，以这条为准；同文件 `:160 ETIMEDOUT 116`、`:178 ENOTSUP 134`）

⇒ `-119 = -EINPROGRESS`。**任务书里"先确认 -119 是哪个 errno"的答案：它是用例的"尚未开始"哨兵，不是驱动错误码。**

**从机为什么永远不返回**（这条是驱动路径，也是为什么不能靠超时兜底）：
`spi_context.h:150-154` 在 `CONFIG_SPI_SLAVE` 下把从设备的完成等待设为 `K_FOREVER`：

```c
if (IS_ENABLED(CONFIG_SPI_SLAVE) && spi_context_is_slave(ctx)) {
        timeout = K_FOREVER;        /* 从模式：不知道主机什么时候来时 */
}
…
if (k_sem_take(&ctx->sync, timeout)) { return -ETIMEDOUT; }
```
`ctx->sync` 只在从机把 `len` 帧收满、`spi_ls_complete()` → `spi_context_complete()` 时才被 give。
**主机不给时钟 ⇒ 从机永远收不满 ⇒ 永远 K_FOREVER。** 这正是用例必须用「独立线程 + 有界 join + abort」的原因
（`spi_loopback.c` 文件头 §安全性设计 1）。

---

## 4. `state=0` 的语义与 5 条判据拆解

`spi_loopback.c:363-367`：
```c
#define LB_LINK_UNKNOWN (-1)
#define LB_LINK_BAD     0
#define LB_LINK_OK      1
```
`state=0` = **`LB_LINK_BAD`**，由 `lb_probe_link()` `:408-411` 的合取式失败推出：

| 判据（`:408-411`） | 实测 | 结果 |
|---|---|---|
| `master_ret == 0` | `0` | ✅ |
| `!timed_out` | `timed_out=1` | ❌ 从机 500 ms 内没结束 |
| `slave_ret == 8` | `-119` | ❌ |
| `lb_mrx[] == lb_stx[]` | `mrx0=ff` vs `stx0=c0` | ❌ 主收 ≠ 从发 |
| `lb_srx[] == lb_mtx[]` | `srx0=00` vs `mtx0=30` | ❌ 从收 ≠ 主发 |

**电气读法（分层定位）**：

- `mrx0=0xff`：主机在 MISO(PG03) 上读到**空闲高**。若 MISO 一路（PG03↔PA02）接通、且从机被 NSS 选中并在移位，
  主机应原样读回 `lb_stx[0]=0xc0`。读到 `0xff` ⇒ **线上没有任何东西在驱动 MISO**。
- `srx0=0x00`：从机缓冲仍是 `:404` 的 `memset(…,0x00,…)` ⇒ `spi_ls_data_exchange_slave()`（`spi_ls.c:207-236`）
  **一帧都没搬过**。
- 从机线程卡在 `K_FOREVER`（§3）⇒ 从机从未收到 8 帧 ⇒ **SCK 上没有到达从机的时钟**，
  或**从机从未被 NSS 选中**（ST 系 SPI 从模式下 NSS=高即不参与总线，这两种情形现象**完全一致**，仅凭本用例无法区分 ⇒ 见 §11 的分支判读）。
- 主机侧 `master_ret=0` ⇒ `spi_dw` 主机自身的配置/中断/FIFO 状态机工作正常。

⇒ 「主机自己跑完 + 双向零流量」是**待测回路根本没接上**的教科书形态。

---

## 5. 旁证（为什么排除了"用例写错"与"驱动缺陷"）

**旁证 A（最强，且是本轮新增）—— 同一批次里 spi1 的引脚被另一个工程占用，两套接线互斥。**
`spid_test` 在同一批次、晚 4 分钟（`spi_test` 22:50:54，`spid_test` 22:54:52）跑出 **PASS 10/10**，
其声明的前置接线是：

> `外部: tpm_spis1 的 MISO PI04 / MOSI PI05 / SCK PI06 / CSN PI07 与 SoC SPI master（本 overlay 选 spi1，管脚 PA00/PA01/PA02 + cs）四线对连`
> —— `projects/spid_test/tests/COVERAGE.md:10-11`，另见其 `tests/cases.yaml` `meta.env` 与
> `reports/2026-09-23-1/spid_test_case_default/spid_test_report.md:11`

而 `spi_loopback` 要求 spi1 的 **同一组** PA00/PA01/PA02（+PA03）连到 spi3 的 PG14/PG15/PG03（+PG13）。
**两套接线在 spi1 侧互斥，同一批回归不可能同时满足** ⇒ 至少有一个工程必然处于"未接线"状态，
且本轮"未接线"的正是 `spi_loopback`。
（严谨起见：`spid_test` 的 PASS 本身**不能反证其跳线一定在** —— 它自己的 COVERAGE:27 写明
"它只证明主机事务发出去了，spid 从端收发全坏也不影响它 PASS"。但这不影响结论：
`spi_loopback` 侧的"零流量"由 §4 独立成立。）

**旁证 B** —— 18 项 `spi` 套件本轮**全 PASS**，其中 `transceive_duplex_pair` 的
`ret=0, rx0=ff, rx1=ff, rx31=ff` 已被登记为「MISO 悬空、不断言内容」的既有事实
（`spi_test_report.md:31`）⇒ **spi1/spi2 总线上确实没有从设备**，与 §4 的读法自洽。

**旁证 C** —— `spi_ls.c` 的**主模式**通路在本轮被 `spid_test` 用同驱动、同引脚独立证过：
`master_send_tpm_frame` 的 `spi_transceive` 返回 0（`spid_test_report.md:25`）⇒
"spi1 的驱动/引脚/pinctrl 根本不可用"这一假设被排除；剩下的只能是**回路没接**。

**旁证 D** —— 用例侧的配置是**有效的**：C1 修复（09-18）已把上一轮"`lb_cfg_m`/`lb_cfg_s` 是全零结构体"
的真因修掉，`master_ret` 由 `-116` 变为 `0`（`COVERAGE.md:270` 记录了修复前形态；
`app.overlay:139-164` 记录主机 CS 现在确实驱动 PG13；`spi_loopback.c:139-169` 主/从两侧
均 8 bit + MSB + 模式位一致）。本轮**没有**回归到"配置对象没生效"的老形态。

---

## 6. 陈年 vs 新回归（任务书问题 3）

`git log -S` / `git log -- <path>` 结果：

| 仓库 / 文件 | 最后一次改动 | 日期 | 是否在本回归窗口内 |
|---|---|---|---|
| `zephyr` / `drivers/spi/spi_ls.c` | `9e3311cbf74` "fix interrupt bug"（仅把 `irq_config(dev)` 从 `spi_ls_init()` 头部挪到尾部，±2/-4 行） | **2026-01-03** | ❌ 窗口外 |
| `zephyr` / `drivers/spi/spi_dw.c` | `77799c4d2c1` "cali to id" | 2026-09-08 | ❌ 窗口外 |
| `zephyr` / `drivers/spi/spi_context.h` | `ca829e1b776` | 2024-09-20 | ❌ 窗口外 |
| `810_driver_test` / `projects/spi_test/cpu1_spi_test/src/spi_loopback.c` | `43b232e`（整工程一次性提交） | 2026-09-21 | 仅在窗口起点被"引入"，非改动 |
| `810_driver_test` / `projects/spi_test/tests/run_test.py` | `a405816` / `d073ddf`（只动判定框架） | 2026-09-21/22 | 与 `link_probe` 无关 |

**三份报告的 `link_probe` INFO 逐字相同**（`reports/2026-09-21-1`、`2026-09-22-1`、`2026-09-23-1`）：
```
INFO link_probe: master_ret=0,slave_ret=-119,state=0
[inf] spi_loopback: link probe: master_ret=0 slave_ret=-119 timed_out=1 mrx0=ff stx0=c0 srx0=00 mtx0=30 -> BAD
```
`COVERAGE.md` §「C1 修复说明」:270 还记录了 C1 之前的**另一种**失败形态
（`master_ret=-116 (-ETIMEDOUT)` / `slave_ret=0`）—— 也是 FAIL。

⇒ **`link_probe` 自 P4.6 引入以来从未 PASS 过**。`OPEN_ITEMS.md` O-18 已把它明确写成基线：
> 「`spi_test` 18 PASS + 1 FAIL(`link_probe`，既有) + 7 SKIP = 与基线一致」

**判定：陈年既有项，非本轮新回归。**

---

## 7. 与同期 spi / spif / spid 工程的关系（任务书问题 3 后半）

| 工程 | 本轮结果 | 代码路径 | 与 `link_probe` 是否同源 |
|---|---|---|---|
| `spid_test` | **PASS 10/10** | `spi_ls.c`（spi1 作 **master**）→ `tpm_spis1` | 否；但它是**旁证 A/C 的来源** |
| `spi_test` | FAIL 18+1/26 | `spi_ls.c`（spi1 作 **slave**）↔ `spi_dw.c`（spi3 作 master） | 本体 |
| `spif_test` | FAIL 16/18 | `pfr_tpm`/`spi_filter` + `spi_dw.c`（spi3/spi4，经 SPIF master 引脚） | **否** |

`spif_test` 的 2 项失败是 `cmd_idx_accessors`（`spif_test.c:327`，DUMMY_CYCLE 回读 ≠ 写入）与
`passthrough_mode_switch`（`:776`，`filter_mode_check()` 与 `passthrough_mode_check()` 两态同时为真）——
属 `pfr_tpm` 寄存器语义 / pinctrl 状态复用，**与 `spi_loopback` 的通路没有共用失败路径**。
另有结构性差异可以佐证二者不同源：`spif_test` 显式 `&spi3 { pinctrl-0 = <>; }`（把 spi3 的 net 让给 SPIF），
而 `spi_test` **保留** spi3 的板级 pinctrl（`app.overlay:123-127` 记录了这条差异）。

⇒ **不要把 spi 与 spif 的失败合并归因。**

---

## 8. 本次改动清单

### 已改（1 处，**不改任何判据**）

**`projects/spi_test/tests/run_test.py`** — `write_markdown_report()` 内那条写死的报告头接线行：

```python
# 改前
'- 接线: 无（spi1/spi2 片内引脚组 + 板级 CS GPIO；总线上无从设备，判据来自驱动源码/寄存器）',
# 改后（附注释说明为何分列）
'- 接线: 套件 `spi`（18 项）无 —— spi1/spi2 片内引脚组 + 板级 CS GPIO，总线上无从设备；'
'套件 `spi_loopback`（8 项）需 `外部: SPI1↔SPI3 跳线`'
'（SCK PA00↔PG14 / MOSI PA01↔PG15 / MISO PA02↔PG03 / CS PA03↔PG13），'
'跳线未接时 7 项 SKIP、`link_probe` FAIL',
```

- **为什么改**：原值是单核模板遗留，只对套件 `spi` 成立；对 8 项 `spi_loopback` 是**事实错误**，
  且直接导致本轮任务书把「环境未满足」误判为驱动问题。修正值逐字对齐
  `projects/spi_test/tests/cases.yaml` 的 `meta.env` 与 `COVERAGE.md`「回环可行性结论」的逐 pin 表。
- **为什么能修 / 有无副作用**：只改报告 `- 接线:` 一行的**文本**，不参与 `evaluate_case()` /
  `judge_case()` 的任何计算，判定结果与通过率**逐字不变**。已 `python -m py_compile run_test.py` 通过。
- **范围**：`git diff --stat` = 1 file changed, 8 insertions(+), 1 deletion(-)。

### 未改（有意为之）

- `zephyr/drivers/spi/spi_ls.c`、`spi_dw.c`：**无证据支持任何改动**，且两者在回归窗口内零改动（§6）。
- `projects/spi_test/cpu1_spi_test/src/spi_loopback.c`、`app.overlay`：判据与配置均自洽（旁证 D），
  不动。**特别是没有把 `link_probe` 改成 `ztest_test_skip()`** —— 理由见 §9。
- 硬件：全程零上板。

---

## 9. 为什么**没有**把 `link_probe` 改成 SKIP（重要，防后续误改）

`link_probe` 与其余 7 项对**同一前置条件**采用了不同口径（7 项 SKIP、1 项硬 FAIL），
09-22 的 `tmp/final/triage.md §4.3` 把这一半归为 **B（用例口径不统一）**，并建议
「或让 `link_probe` 与其余 7 项走同一 `ztest_test_skip` 口径」。**本次不采纳**，理由：

1. **会掩盖驱动缺陷**（本项目明文纪律）。改成 SKIP 后，若**跳线确实接着而驱动真的坏了**，
   `lb_link_ok()` 返回假 ⇒ 8 项全部 SKIP ⇒ `SKIP` 不计入 `ran`（`run_test.py:253-254`）
   ⇒ case 报 `PASS 18/18`。**整套回环覆盖会在一次"看起来是环境问题"的伪装下整体消失**，
   且没有任何一处会报警。这与 `OPEN_ITEMS.md` O-31 的纪律
   （「**不得擅自放宽判据** …… 放宽会掩盖可能的驱动缺陷」）直接冲突。
2. **与工程自声明矛盾**：`cases.yaml` `meta.env` 明写「需接线：spi3(PG13) ↔ spi1(PA03) 跳线」。
   一个**声明了外部前置条件**的 case，其前置条件未被满足时应报 FAIL（"环境没准备好"），
   而不是报 PASS（"全部通过"）。
3. `link_probe` 是**唯一**让整套回环覆盖在摘要里保持可见的东西。它是刻意设计的 canary，
   见 `spi_loopback.c:750` 原注释：「链路不通时除 link_probe 外全部 skip」。

⇒ **维持 FAIL**。若用户裁定跳线属"可选条件"，应改的是 `cases.yaml`（拆 case / 屏蔽回环），
而不是把断言降级为 skip —— 那属用户裁定，不在本任务写域内（见 §10）。

---

## 10. 需要**写域外**的改动（本任务不执行，待裁定）

| # | 位置 | 需要的改动 | 归谁 |
|---|---|---|---|
| X-1 | `projects/spi_test/tests/cases.yaml`（拆 case）**或** `regression/cases.yaml`（接线编排） | `spi_test` 与 `spid_test` 在同一批次启用，但二者声明的外部接线**在 spi1 的 PA00/PA01/PA02(+CS) 上互斥**（§5 旁证 A）。同一批回归里必有一个处于未接线态。**需要裁定**：① 拆成"需跳线的 case"与"不需跳线的 case"分批跑；② 或接受 `link_probe` 永久 FAIL 作为环境信号并写进基线。 | 用户 |
| X-2 | `810_driver_test/README.md §4`、`projects/spi_test/tests/COVERAGE.md`「外部路径」清单 | 现只把 7 项 `roundtrip_*` 登记为"外部路径"，**未把 `link_probe` 算进去**（09-22 triage §4.3 的"缺"栏即指此）。需一并登记。属文档。 | keeper / 用户 |
| X-3 | `zephyr/drivers/spi/spi_ls.c`（可选，**非本失败根因**） | 若用户要推动上游修复，`COVERAGE.md`「上游缺陷观察」已登记 12 条，本轮复核确认其中 #12（`:359-360` 同步路径重复 `spi_context_complete()`，非 ASYNC 下 `k_sem_give(&ctx->sync)` 每传输留 1 个残留令牌）仍然存在 —— 本用例靠 `lb_drain_stray_sync()`（`spi_loopback.c:261-288`，带运行时自检）规避。**驱动修好后该函数与镜像结构体应整体删除。** | 驱动工程师 |
| X-4 | `projects/spi_test/cpu1_spi_test/app.overlay:54-64`（R-4） | `SPI1_NSS_FUNC1_IDX19_PINMUX_PA(XX)` 的 `XX` 是模板参数，「**PA03 就是 idx19 焊盘**」属强推断非图证。若 PA03 不是，则从机永远选不中 ⇒ 即使接好跳线也会 FAIL。需用户按原理图/复用表确认。 | 用户 |

---

## 11. 未定项 / 仍未定论

| # | 未定 | 影响 | 怎么解 |
|---|---|---|---|
| U-1 | **跳线是否真的在**（COVERAGE §6 R-1，用户原话为"应该是""好像是"的推测语气） | 决定本项是 C（环境）还是 A/B | 见 §12 上板验证 |
| U-2 | **CS 一路 `PA03↔PG13` 是否存在**（R-5） | 缺这一路 ⇒ 从机永不被选中 ⇒ `slave_ret` 仍是超时值，**现象与"完全没接线"一致**，本轮无法区分 | §12 的分支判读 / 量 PA03 电平 |
| U-3 | **`spi3` 的 net 上是否挂板载 NOR**（R-2） | 若有，主机拉低 PG13 时它会跟选中并争 MISO ⇒ `mrx0` 会是一个**固定非 0xff 值**（本轮是 `0xff`，**指向"没有"**，但未定论） | §12 分支判读 |
| U-4 | 本轮 `spi_test_report.json` 的 `mem_state="unavailable"`（09-21 那份是 `"ok"`） | 不影响本项判定（token 已给出确定结果）；不影响归因 | 与本任务无关，登记备查 |

---

## 12. 上板怎么验（确切命令 / 预期 / 为什么必须上板）

### 12.1 主验证实验（**一次即可定论**）

```bash
# 1) 按 COVERAGE §6 R-5 逐 pin 接好跳线（这是实验的自变量）
#    SCK  PA00(spi1) ↔ PG14(spi3)
#    MOSI PA01(spi1) ↔ PG15(spi3)
#    MISO PA02(spi1) ↔ PG03(spi3)
#    CS   PA03(spi1) ↔ PG13(spi3)     <== 这一路最容易漏，且是本轮 C1 新增的
#    ⚠️ 接之前必须先拆掉 spid_test 的 spi1↔tpm_spis1 跳线（两者同引脚，见 §5 旁证 A）

# 2) 跑单 case（烧录 + 双串口采集，无需重建）
cd D:/zephyr4_0/810_driver_test/projects/spi_test/tests
python scripts/dbg_run.py --case case_default --timeout 35
#    等价替代：python run_test.py          （全流程含归档）
#              cd case_default && ./test.sh -r   （只烧不重建）
```

**预期（跳线正确时）**：
```
INFO link_probe: master_ret=0,slave_ret=8,state=1
[inf] spi_loopback: link probe: master_ret=0 slave_ret=8 timed_out=0 mrx0=c0 stx0=c0 srx0=30 mtx0=30 -> OK
```
且 `spi_loopback` 套件 8 项**全部 PASS**、无 SKIP ⇒ case 变 `26/26 PASS`、`execution=SUCCESSFUL`。

**分支判读表**（这一张表就是本报告的可执行结论）：

| 实测形态 | 结论 | 下一步 |
|---|---|---|
| `state=1` / `slave_ret=8` / 8 项全 PASS | **确认 C（环境）**，U-1 关闭 | 结案；把 `link_probe` 写进基线或按 X-1 拆 case |
| 仍 `state=0`，`timed_out=1`，`mrx0=ff` | 跳线没接好/接错脚；或 U-2（PA03 不是 NSS 焊盘） | 逐 pin 通断量线；量传输期间 PA03 是否被 PG13 拉低 |
| 仍 `state=0`，但仍 `slave_ret=-119` 且 `mrx0` **不是** `0xff`（某个固定值） | 有器件在驱动 MISO ⇒ 疑 U-3 板载 NOR 争总线 | 核对 spi3 net 上的 NOR；必要时 `/delete-property/ cs-gpios` 改硬件 CSN |
| `slave_ret = -116 (-ETIMEDOUT)` 或 `0`（"立刻返回"） | 回到 `COVERAGE.md`「上游缺陷观察 #12」的 `ctx->sync` 残留路径（本用例的 `lb_drain_stray_sync()` 没兜住） | 查 `:261-288` 的运行时自检有没有打 `stray-sync drain self-check failed` |
| `master_ret < 0` | 主机侧（`spi_dw.c` / spi3 pinctrl / dmac2） | 与 `spif_test` 的 spi3 用法对比 |

### 12.2 寄存器级取证（可选，用于区分 U-1 与 U-2）

跑完用例后在 GDB 里读 spi1（`reg_spi_t` 布局见 `modules/hal/linkedsemi/hal_driver/inc/reg_spi_type.h:9-30`，
基址 `0x4008a000`，偏移 `CR1=0x00 / SR=0x08 / IER=0x24 / IFM=0x34`）：

```gdb
x/1xw 0x4008a000    # spi1 CR1 —— 期望 MSTR(bit2)=0（从模式）、SPE(bit6)=1（已使能、正在等时钟）
x/1xw 0x4008a008    # spi1 SR  —— 看 BSY/RXNE
x/1xw 0x4008a034    # spi1 IFM —— 错误中断标志（MODF/OVR/FR）
```
- `CR1.MSTR=1` ⇒ 从模式配置根本没生效（用例/驱动配置问题，回到 B）。
- `CR1.SPE=0` ⇒ `spi_ls_complete()` 已把外设关掉（疑 #12 残留令牌路径）。
- `CR1.MSTR=0 且 SPE=1` 而数据仍不动 ⇒ **从机配置正确地在等时钟** ⇒ 只能是物理回路（U-1/U-2）。
  这条读数是**把"从机没配好"与"线没接"彻底分开**的最廉价取证。

### 12.3 为什么**必须**上板

- 本项判据的全部内容就是**一条物理回路的连通性**（`外部: SPI1↔SPI3 跳线`）。
  `COVERAGE.md` §6 R-1/R-4/R-5 已明确：DT、原理图、源码三处**都无法证明这条跳线存在**，
  用户口述还是推测语气（"应该是""好像是"）。源码级分析能排除配置与驱动，**无法证伪"线没接"**。
- 寄存器级读数（§12.2）只能在主从**真的同时跑起来**时才有意义 —— 静态分析拿不到 `SPE/MSTR` 的运行时值。
- 且 §5 旁证 A 表明：本板同一批次里 spi1 的引脚已被 `spid_test` 的跳线占用，
  "回环跳线此刻是否接上"本身就是**台架的物理状态**，不是代码属性。

---

## 附：本次核对的命令与文件（可复现）

```bash
# 现象与原始输出
810_driver_test/reports/2026-09-23-1/spi_test_case_default/serial_TGT.log:88-121
# 登记核对（是否已登记问题）
810_driver_test/OPEN_ITEMS.md   → O-18（本项已写作基线）
810_driver_test/KNOWN_ISSUES.md → #80（spi_ls.c 缺陷簇，与本项无因果）
810_driver_test/tmp/final/triage.md:156-166  → 09-22 已归因为 C（+B）
810_driver_test/projects/spi_test/tests/COVERAGE.md:109-196（回环可行性结论，§6 未决项在 :158-168）/ :170-196（回环用例表）/ :212-266（上游缺陷观察）/ :267-300（C1 修复说明）
# 源码链
projects/spi_test/cpu1_spi_test/src/spi_loopback.c:199-206, 308-357, 363-421, 445-456, 750
projects/spi_test/cpu1_spi_test/prj.conf:42        （CONFIG_SPI_SLAVE=y）
projects/spi_test/cpu1_spi_test/app.overlay:30-87, 129-178
zephyr/drivers/spi/spi_context.h:84-87, 140-180, 202-215
zephyr/drivers/spi/spi_ls.c:59-143, 207-236, 259-299, 302-371
zephyr/lib/libc/minimal/include/errno.h:103
modules/lib/picolibc/newlib/libc/include/sys/errno.h:160,163,178
modules/hal/linkedsemi/hal_driver/inc/reg_spi_type.h:9-30,34,36,38,81,83,95,238
# 历史
git -C zephyr log --date=short --pretty='%h %ad %s' -8 -- drivers/spi/spi_ls.c
git -C 810_driver_test log --date=short --pretty='%h %ad %s' -6 -- projects/spi_test
git -C 810_driver_test log -S"link_probe" --oneline
```
