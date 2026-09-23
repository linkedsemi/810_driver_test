# emmc_test 两项失败 — 根因定位与修复

- 日期：2026-09-23
- 范围：**仅源码级定位与修改**。本轮**未构建、未烧录、未碰板/串口/DebugServer**（板子被另一路任务占用）。
- 涉及 case：`case_block_driver`、`case_block_pio`（两 case 固件用例集完全相同，仅 `CONFIG_SDHCI_SDMA_ENABLE` 不同）
- 被测驱动：`zephyr/drivers/sdhc/sdhci-of-linkedsemi.c`（`linkedsemi,sdhci`）
- 结论一句话：**两项是两条互不相干的驱动缺陷（不是同一代码路径），且都长期潜伏在 HEAD 里、从未真正修过**。
  「2026-09-18 已修复」是**文档漂移**（修复从未落到提交里），不是本轮新引入的回归。

---

## 0. 对任务书假设的更正

| 任务书假设 | 复核结果 |
|---|---|
| 第 1 项 = request 路径 mutex 未释放 | ✅ **成立**（但因果链需细化，见 §1.2） |
| 第 2 项 = set_io 越界时钟没做入参校验、直接接受 | ✅ **成立** |
| 「两项都是同代码路径，两个 case 只差传输模式，所以同源」 | ❌ **不成立**。两项分属**两个不同函数**（`linkedsemi_sdhci_request` 与 `linkedsemi_sdhci_set_io`），彼此无关，只是恰好写在同一个测试文件里。两 case 失败项相同，是因为「用例集相同 + 两项缺陷都在与 DMA/PIO 无关的分支上」。 |
| 报告行号 `:460` / `:496` | ✅ 逐字复核一致（`emmc_block_test.c`） |
| （隐含）用例注释描述的「修复后的驱动」 | ❌ **驱动里没有这个修复**。见 §3。 |

---

## 1. 现象复核（原始报告 + 串口原文）

原始报告：本目录 `emmc_test_report.md` / `serial_TGT.log`（两 case 逐字节相同）。
`FAIL (8/10)`，两项 FAIL：

### 1.1 串口原文（两 case 相同）

```
START - request_mutex_not_leaked
INFO request_mutex_not_leaked: join=-11,probe_ret=2147483647,probe_ret2=2147483647,done=0
    Assertion failed at .../emmc_block_test.c:460: zz_emmc_block_edge_request_mutex_not_leaked: (jret is non-zero)
FAIL - request_mutex_not_leaked in 2.043 seconds

START - set_io_clock_out_of_range_not_silent
INFO set_io_clock_out_of_range_not_silent: f_min=50000,clock=1000,ret=0
    Assertion failed at .../emmc_block_test.c:496: zz_emmc_block_edge_set_io_clock_out_of_range_not_silent: (ret equal to 0)
FAIL - set_io_clock_out_of_range_not_silent in 0.039 seconds
```

### 1.2 ★ 关键细节：`probe_ret` 从未被赋值 ⇒ 泄漏发生在**前一个用例**里

`request_mutex_not_leaked` 的探针线程（`emmc_block_test.c:402-431`）会**连发两次**同样的非法请求，
两次都会写 `probe_ret` / `probe_ret2`。实测 `probe_ret == 2147483647 (0x7fffffff)` = **初始哨兵值，从未被赋值**
⇒ 探针线程**连第一次 `sdhc_request()` 都没返回**，`jret = -EAGAIN`（2000 ms join 超时）。

⇒ `access_mutex` 在探针线程启动**之前**就已经被持有。套件内唯一能在其前面把锁吃掉的，
是**紧邻前一项** `request_data_invalid_opcode_notsup`：它用**同样的非法 opcode**（`SD_SEND_STATUS=13`）
调了一次 `sdhc_request()`，**拿到了 `-ENOTSUP` 因此 PASS**，但（修复前）是在**持锁状态下**
`return -ENOTSUP` —— 返回码对、锁泄漏，所以它 PASS、下一项永久阻塞。

这条自洽链同时解释了「为什么前一项 PASS、后一项 FAIL」——**用例本身没写错**，
它恰好是「只查返回码」与「查锁的可用性」两种判据的分工。

---

## 2. 用例实际断言逻辑（源自源码，非转述）

文件：`810_driver_test/projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c`

- `zz_emmc_block_edge.request_mutex_not_leaked`（`:433-469`）
  - `:451-453` 起一个 `K_PRIO_PREEMPT(5)` 探针线程，线程内 **连发两次** `sdhc_request(dev, cmd, data)`：
    `cmd.opcode = EMMC_INVALID_DATA_OPCODE(=13, SD SEND_STATUS)`、`data != NULL`（`emmc_block_test.h`）。
  - `:455` `jret = k_thread_join(&mutex_probe_thread, K_MSEC(2000))`；`:460` 断言 `jret == 0`；
    `:465/:467` 断言两次返回都是 `-ENOTSUP`。
  - **判据 = 「第二次请求能在 2000 ms 内返回」**，即**锁没有被永久占用**。非挂死、自包含。

- `zz_emmc_block_edge.set_io_clock_out_of_range_not_silent`（`:471-501`）
  - `:485` `io.clock = 1000`（< `f_min`）；`:491` `sdhc_set_io()`；`:496` `zassert_not_equal(ret, 0)`。
  - **判据 = 越界时钟不得被静默接受**（契约断言）。

> 该判据与**上游 Zephyr 自带的契约测试完全同形**：
> `zephyr/tests/drivers/sdhc/src/main.c:98-100`
> ```c
> io.clock = props.f_max + SDHC_FREQUENCY_SLIP;   /* +10 MHz */
> ret = sdhc_set_io(sdhc_dev, &io);
> zassert_not_equal(ret, 0, "Invalid io configuration should not succeed");
> ```
> ⇒ 810 这两项**不是自造判据**，是上游 SDHC 契约。linkedsemi 驱动是全仓**唯一**违反该契约的实现。

---

## 3. 根因 A —— `linkedsemi_sdhci_request()` 持锁早退泄漏 `access_mutex`（P0）

**文件**：`zephyr/drivers/sdhc/sdhci-of-linkedsemi.c`
**HEAD 版本**：加锁 `:422`，解锁 `:505`，而 `default: return -ENOTSUP;`（`:475-478`）**落在 [422,505] 区间内**。

```c
422  ret = k_mutex_lock(&dev_data->access_mutex, K_FOREVER);   /* ← 锁点 */
...
449  if (data) {
453      switch (cmd->opcode) {
475      default:
476          DEV_ERR(dev, "invalid opcode: %#x", cmd->opcode);
477          return -ENOTSUP;                                    /* ← 泄漏点：持锁直接 return */
478      }
...
505  k_mutex_unlock(&dev_data->access_mutex);                  /* ← 解点（被跳过） */
```

`K_FOREVER` ⇒ 泄漏后**跨线程永久阻塞**（`zephyr/kernel/mutex.c` 无 dead-owner 回收）。
次生效应：`:502` 的 `atomic_set(&g_sdhci_busy, 0)` 同样被跳过 ⇒ `linkedsemi_sdhci_card_ready()`
恒 false ⇒ **eMMC coredump 被静默丢弃**（「故障现场恰好丢失崩溃现场」）。

### 3.1 异常路径穷举（`qsh-driver-review` §1.1 清单）

| # | 位置（HEAD） | 在锁内？ | 结论 |
|---|---|---|---|
| 1 | `:418` `return -EBUSY`（`is_app_cpu_running()` 守卫） | ❌ 锁**之前** | ✅ 正确，**不得**加 unlock（会二次解锁崩溃） |
| 2 | `:427` `return -EBUSY`（`k_mutex_lock` 失败分支） | ❌ **加锁失败** | ✅ 正确，**不得**加 unlock |
| 3 | **`:477` `return -ENOTSUP`（`default:` 分支）** | ✅ **锁内** | ❌ **唯一的泄漏点** |
| 4 | `:507` `return ret`（函数尾，正常出口） | ✅ 锁内 | ✅ 正确（`:505` 已 unlock） |

**全函数有且仅有 1 个泄漏点**，与 `DEFECT_ANALYSIS_2026-09-17.md §4.2` 的复核结论一致。
其余加锁点：`linkedsemi_sdhci_reset()` `:176/:180` 配对完整，无 return 夹在中间 ✅。

### 3.2 修复

按 `DEFECT_ANALYSIS_2026-09-17.md §4.6` 的建议 **A+B 同时做**：

- **B：白名单前置到加锁之前** —— 新增 `linkedsemi_sdhci_data_opcode_supported()`（`:421-443`），
  在 `k_mutex_lock` 之前拒掉非法 data-opcode ⇒ **锁未持有，天然无泄漏**；
- **A：单出口** —— `default:` 分支改为 `ret = -ENOTSUP; goto unlock;`，新增 `unlock:` 标签，
  **置于 `g_sdhci_busy` 清除「之前」**（正常路径顺延进标签 → 清 busy → 解锁；`goto` 也落到同一处）。
  ⇒ 两条出口**都**会清 `g_sdhci_busy`，不会引入 §3 的次生效应。
  A 保留为**防御性兜底**（B 生效后 `default` 不可达）。

> ⚠️ **原地更正一处历史表述**：`DEFECT_ANALYSIS_2026-09-17.md §4.6` 写的是
> 「标签必须置于 `g_sdhci_busy` 清除（`:502`）**之后**，否则引入上述次生缺陷」——
> 按 C 语义这句是**反的**：标签若在清除**之后**，`goto` 会**跳过**清除 ⇒ 反而制造次生缺陷。
> 本次按语义正确的方向落（标签在清除**之前**）。**建议父会话就地更正该文档**。

**行为等价性**：修复前后，非法 opcode 的**返回码完全相同**（`-ENOTSUP`），
只是**拒绝时机**从「锁内」提前到「锁外」；合法路径逐字节未动。**无副作用**。

---

## 4. 根因 B —— `linkedsemi_sdhci_set_io()` 越界时钟静默忽略

**文件**：同上。**HEAD 版本** `:228-232`：

```c
228  if (ios->clock != 0
229      && (ios->clock <= dev_config->max_bus_freq)
230      && (ios->clock >= dev_config->min_bus_freq)) {
231      mmc_clock_freq_change(host, ios->clock);
232  }
              /* ← 条件不满足：不改频、不报错、继续执行，:296 return 0 */
```

越界输入被**静默吞掉**，调用方（`mmc.c`）以为换频成功，实际总线仍跑旧频率 ——
`mmc_set_max_freq()` / `mmc_set_hs_timing()` 都是**依据 `sdhc_set_io` 的返回码**决定
「是否继续按新时序发命令」的，静默接受会让上层以错误的时序假设工作。

**本板 DT**：`max-bus-freq = 0xbebc200 (200 MHz)`、`min-bus-freq = 0xc350 (50 kHz)`；
用例注入 `clock = 1000` ⇒ 越界。

### 4.1 修复

按**上游 Zephyr 的既有约定**（`imx_usdhc.c:309`、`intel_emmc_host.c:1018`、
`rcar_mmc.c:1108`、`sdhc_esp32.c:973` 四处**完全同形**）：

```c
if (ios->clock && (ios->clock > f_max || ios->clock < f_min)) {
        return -EINVAL;
}
```

新增 `:237-243`：`clock != 0` 且越界 ⇒ `-EINVAL`（并 `DEV_ERR` 打出范围）。
`clock == 0` 语义是「保持当前时钟」，**始终合法**（`sd.c:129` 的初始值、`:371` 的 gate 都是 0）。

### 4.2 回归安全性检查（穷举全部调用方，确认不会误伤正常路径）

树内 `sdhc_set_io()` 调用点（sd 子系统 + 本用例）全部核对：

| 调用点 | 传入 clock | 是否越界 |
|---|---|---|
| `sd.c:154/:161`（上电/掉电复位） | `0`（`sd_init_io` 刚置 0） | 否（0 放行） |
| `sd.c:174` | `SDMMC_CLOCK_400KHZ` | 否 |
| `sd_ops.c:373/:380`（换 1.8V 时 gate，`:371` 先存原值） | `0` → 原值 | 否 |
| `sd_ops.c:388` | 恢复原 clock | 否 |
| `sd_ops.c:808`（deinit 掉电） | 保持原 clock | 否 |
| `mmc.c:270` | `0` | 否 |
| `mmc.c:392`（`mmc_set_max_freq`） | 26 MHz / 20 MHz | 否 |
| `mmc.c:429`（`mmc_set_bus_width`） | 保持 26/20 MHz | 否 |
| `mmc.c:459`（`mmc_set_hs_timing`） | `MMC_CLOCK_52MHZ` | 否 |
| `mmc.c:552`（HS200） | `MMC_CLOCK_HS200` = 200 MHz | 否（== `f_max`，边界含） |
| `mmc.c:604`（**HS400**） | `MMC_CLOCK_HS400` = **400 MHz** | **越界**，但被 `mmc.c:565` 的 `host_props.host_caps.hs400_support` 门卫挡住 —— 本驱动 `:206` 恒置 `false` ⇒ **不可达** |
| `sdmmc.c:271 / :522 / :735` | `MIN(f_max, hs_max_dtr)` / `SD_CLOCK_25MHZ` / `f_max` | 否（都取过 MIN） |

**结论：树内无任何可达调用会传越界非零 clock ⇒ 该改动不会破坏 eMMC/SD 初始化。**

---

## 5. 陈年还是新回归？—— 判定：**陈年潜伏项，从未真正修过**

`zephyr/` 是 git 仓库（`D:/zephyr4_0` 根目录**不是**）。四项证据：

1. **`git status --porcelain drivers/sdhc/sdhci-of-linkedsemi.c`（本轮改动前）= 空** ⇒ 工作区 == HEAD。
2. **`git show HEAD:drivers/sdhc/sdhci-of-linkedsemi.c`** 逐行确认：`:477 return -ENOTSUP;` 在锁内、
   **没有任何 `unlock:` 标签**（全文 `k_mutex_unlock` 只有 `:180` 与 `:505` 两处，即 HEAD 形态与本轮改动前完全一致）。
3. **`git log -S "invalid opcode" -- drivers/sdhc/sdhci-of-linkedsemi.c`** 只命中 `96191eaa1be`
   （远古提交）⇒ 该 `default: return -ENOTSUP` 早于 810 emmc_test 全部工作。
4. **最近三次改该文件的提交**：`2c1c4a62da2`(2026-05-14)、`3d52a5149a0`(2026-05-23)、
   `a72826a748b`(2026-06-08) —— **全部早于** 810 emmc_test 首轮（2026-09-17）**，回归窗口内零改动**。

⇒ **不是新回归。** 历史归档 `2026-09-17-4|5`、`2026-09-18-1`、`2026-09-21-1`、`2026-09-22-1`、`2026-09-23-1`
**全部为 `FAIL (8/10)` 且失败项完全相同** —— 说明它**从来没有通过过**。

### 5.1 更正「2026-09-18 已修复」这处文档漂移

`projects/emmc_test/tests/COVERAGE.md` 声称缺陷 #1「**已于 2026-09-18 修复**」、
状态列写「✅ PASS（2026-09-18 驱动修复后改判）」。**该修复从未落到代码里**（HEAD 无此改动、工作区亦无）。
用例注释里描述的 `` `if (data && !data_opcode_supported(opcode)) return -ENOTSUP;` 位于 `:449` 的
`k_mutex_lock` 之前 ``那一版代码，**全仓 `.c` 文件中不存在**（`grep data_opcode_supported` 零命中）。

**机理与本项目已登记的 `OPEN_ITEMS O-16` 完全同类**（原文）：
> 「文档记「已修」而代码未提交 ⇒ 后续任何 git 分支操作都可能静默丢弃，且看文档的人无从察觉」

O-16 记录 09-21 有过一次 `checkout` + `reset --hard`，冲掉了 09-18 的**未提交工作区改动**。
本次 mutex 修复很可能在同一批里被一起冲掉。⇒ **教训复用：修复必须进提交，文档不得先于代码宣称「已修」。**

---

## 6. 改动清单

| 文件 | 位置 | 改动 | 为什么能修 |
|---|---|---|---|
| `zephyr/drivers/sdhc/sdhci-of-linkedsemi.c` | 新增 `:421-443` | `linkedsemi_sdhci_data_opcode_supported()` 白名单判定函数 | 供加锁前使用 |
| 同上 | 新增 `:461-470` | `k_mutex_lock` **之前**拒绝非法 data-opcode → `-ENOTSUP` | 修复 B：锁未持有，天然无泄漏 |
| 同上 | 改 `:525-532` | `default:` 分支 `return -ENOTSUP` → `ret = -ENOTSUP; goto unlock;` | 修复 A：防御性单出口 |
| 同上 | 改 `:555-570` | 把 `g_sdhci_busy` 清除块移到新增的 `unlock:` 标签**之后** | 单出口汇合点；保证 `goto` 也会清 busy，coredump 可用性不被次生缺陷破坏 |
| 同上 | 改 `:228-247` | 越界时钟 → `-EINVAL` + `DEV_ERR`；`clock != 0` 才改频 | 修复 B 项：对齐上游契约 |

`git diff --stat`：`1 file changed, 64 insertions(+), 4 deletions(-)`（**未提交**，提交动作归用户）。

**副作用评估**：
- 修复 A：非法 opcode 返回码不变（`-ENOTSUP`），仅拒绝时机提前；合法路径零改动。
- 修复 B：见 §4.2 —— 树内无可达调用受影响；`clock == 0` 语义保持。
- ⚠️ 唯一可见的行为变化：**若**将来有人把 `host_caps.hs400_support` 改成 `true`，
  则 HS400 的 `clock = 400 MHz` 现在会被 `-EINVAL` 拒掉（原来会被静默忽略、总线仍跑 200 MHz）。
  这**是期望行为**（DT 声明 `max-bus-freq` 只有 200 MHz），且会**响亮失败**而非静默跑错频率。

---

## 7. 上板怎么验

> ⚠️ 必须**重新构建**：本次改的是驱动 `.c`，旧 ELF 里没有修复。板子当前被另一路任务占用，
> 本轮**未执行**下列任何一步。

### 7.1 命令（在 `D:/zephyr4_0/810_driver_test/projects/emmc_test/tests` 下）

```bash
# 推荐：单 case 全流程（构建 + 加载 + 采集 + 判定 + 归档）
python run_test.py --case case_block_driver
python run_test.py --case case_block_pio

# 或：单 case 快速调试（只加载/采集，不构建）—— 需先自行构建一次
python scripts/dbg_run.py --case case_block_driver --timeout 35

# 或：只构建（看编译是否通过，先排除语法问题）
bash case_block_driver/test.sh
```

### 7.2 预期结果

| 观察点 | 修复前（当前归档） | 修复后预期 |
|---|---|---|
| 报告结果 | `FAIL (8/10)` | **`PASS (10/10)`** |
| `INFO request_mutex_not_leaked` | `join=-11,probe_ret=2147483647,probe_ret2=2147483647,done=0` | **`join=0,probe_ret=-134,probe_ret2=-134,done=1`** |
| `INFO set_io_clock_out_of_range_not_silent` | `f_min=50000,clock=1000,ret=0` | **`f_min=50000,clock=1000,ret=-22`**（`-EINVAL`） |
| 执行标记 | `FAILED` | `PROJECT EXECUTION SUCCESSFUL` |
| 串口 | 2 条 Assertion failed | 无 Assertion failed |

两个 case 都应变成 `PASS (10/10)`（用例集相同，两项缺陷都与 DMA/PIO 无关）。

**最有区分度的单点**：`probe_ret` 从 `2147483647`（哨兵未变）变成 `-134`。
`probe_ret` 一变，就证明「第一次请求在锁外就被拒了」—— 这是修复 B 的直接指纹。

### 7.3 为什么必须上板（不能只靠静态分析结案）

1. **`-EINVAL` 这条新路径从未在本板执行过**：它是新增分支，静态分析只能证明「树内调用方不触发它」，
   不能证明「编译后该分支的返回值/日志形态在真机上如预期」。
2. **验证的是「锁未泄漏」这一运行时性质**：判据是 `k_thread_join(2000ms)` 的**超时与否**，
   是**时序/并发**性质，只有真跑固件才能得到。
3. **要排除「修复 A 引入次生效应」**：`unlock:` 标签若落错位置会让 `g_sdhci_busy` 永不清零、
   eMMC coredump 静默失效 —— 该效应**只在运行时显现**，编译期无症状。
4. **要确认没打断 eMMC 初始化**：`set_io` 的 `-EINVAL` 若被某个此前未识别出的初始化调用命中，
   会表现为 `disk_access_init` 失败 / 用例大面积 FAIL —— 只有上板能证伪 §4.2 的穷举结论。
5. 因此：**上板看到 10/10 + 上述两个 INFO 形态**，才算本项闭环。**本轮不得凭静态分析宣称已修好。**

### 7.4 不打算做（明确排除）

- 不改 `CONFIG_SDHCI_SDMA_ENABLE`、不改 overlay、不改用例断言、不改 `common/` 或 `regression/`。
- 不把 `K_FOREVER` 换成有限超时（会改变并发契约，须与 SD 栈一起评估；见 DEFECT_ANALYSIS §4.6）。

---

## 8. 仍未定项 / 风险

| # | 项 | 说明 |
|---|---|---|
| R-1 | **修复未提交** | 工作区改动（+64/-4）。按 O-16 教训，**提交动作归用户**；未提交期间任何 `checkout`/`reset` 都会再次静默丢失。 |
| R-2 | `set_io` 的 **bus_width `default: return -ENOTSUP`**（HEAD `:268-270`） | 属「错误路径跳过 cleanup」类（`qsh-driver-review` §1.2）：此处**已改过电源**（`:249-269`）再返回错误，硬件状态与返回码不一致。**本轮未改**（不在两项失败靶点内、改动面无测试覆盖、且返回码本身非 0）。**登记备查**。 |
| R-3 | `hs400_support = true` 的未来影响 | 见 §6 副作用条 —— 届时 `set_io(400 MHz)` 会被拒。属期望行为但需驱动侧知晓。 |
| R-4 | `linkedsemi_sdhci_transfer_blocking` 的两处**无超时自旋**（`:411-412`，`SDHCI_COMMAND_INHIBIT_FLAG` / `SDHCI_DATA_INHIBIT_FLAG`） | 驱动已知缺陷 #3（COVERAGE.md 记为「未覆盖/注入不可达」）。本轮**未动**。 |
| R-5 | 文档漂移需一并更正 | `COVERAGE.md` 的两处「2026-09-18 已修复 / ✅ PASS」与「执行顺序与破坏性」段的「该缺陷已于 2026-09-18 修复」是**不实记录**。已在 `COVERAGE.md` 末尾追加 `2026-09-23` 更正批注指向本报告，**未改写原文**（沿用本项目「就地更正、留痕不删」的惯例）。 |
| R-6 | `report_data_invalid_opcode_notsup` 用例不查锁 | 它只断言返回码，所以泄漏时它照样 PASS。**不建议改**（改了会削弱「返回码契约」这一独立判据），但需知晓：**它单独的 PASS 不能证明无泄漏**，靠的是同套件的 `request_mutex_not_leaked`。 |

---

## 附：本次驱动代码审核结论（`qsh-driver-review` 清单）

| 维度 | 结果 |
|---|---|
| §1.1 Mutex/信号量泄漏 | **P0 × 1**（`:477`）→ **已修**；`reset()` 配对完整；`transfer_blocking` 的 `k_sem_take` 由 ISR give、无需 taker give |
| §1.2 错误路径跳过 cleanup | P1 × 1（`set_io` bus_width default，见 R-2）→ **登记不改**；`transfer_blocking` 的 `goto err` 清理完整（清 SIGNAL_ENABLE / INT_STATUS / RESET_CMD\|RESET_DATA）✅ |
| §1.3 中断安全 | ISR（`:91-169`）内无阻塞调用（`k_sem_give` 为 ISR 安全；`DEV_INF` 走 `CONFIG_LOG_MODE_DEFERRED=y`）✅ |
| §1.5 返回值丢弃 | `mmc_clock_freq_change` / `sdhci_reset` / `sdhci_send_command` 均为 `void`（`sdhci.h:598/605/612`）⇒ 无丢弃问题；`sdhci_set_transfer_config` 的返回值**已检查**（`:383`）✅ |
| §1.9 同步路径重复完成 | 无（`transfer_blocking` 不 give）✅ |
| §1.6 错误码一致性 | `-ENOTSUP` / `-EBUSY` / `-EINVAL` 均为负 errno ✅ |
