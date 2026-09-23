# DMA 回归复测（ERR 分支改动后）— 2026-09-23

## 0. 结论一句话

**回归通过，无新增失败**：`case_default` PASS 7/7、`case_destructive` PASS 9/9，
与今天早些时候的修复后基线**逐项一致**；串口判据
（`channel 0 must be inactive to reconfigure` 一行不出现、`test_dma_self_copy_rejected`
走 `source and destination address are identical`、`suspend_idle_ret=-22`）**全部满足**。

> ⚠️ **本次没有、也不能证明 ERR 分支那两行正确。** ERR 分支在本平台不可注入
> （`OPEN_ITEMS O-8` 已结项：用户裁定该场景不会发生）⇒ 这两行在测试里**从未被执行**。
> 本次能证明的唯一命题是：**该改动没有破坏原有功能（TFR/BLOCK/配置/停止等正常路径）**。

---

## 1. 磁盘检查（开工门槛）

```
$ df -h /d
Filesystem      Size  Used Avail Use% Mounted on
D:               76G   73G  3.4G  96% /d
```

可用 **3.4G ≥ 2G** ⇒ 未触发「停下来报我」的阈值，继续构建。
全程未删除任何 build 目录。

---

## 2. 环境预检（qsh-debug §2.4 / ref-self-heal §1，逐项手动）

| # | 项 | 结果 |
|---|---|---|
| 1 | DebugServer 进程 | 初始**未运行** ⇒ 已启动（PID 16860），后按残留纪律重启为 **PID 2804** |
| 2 | 1025 LISTENING | ✅ `TCP 0.0.0.0:1025 LISTENING` |
| 3 | GDB 连通（在线唯一判据） | ✅ `target remote 127.0.0.1:1025` → `[Inferior 1 (Remote target) detached]`，EXIT=0 |
| 4 | 串口数量（单核需 ≥1） | ✅ 4 个非 CKLink 口：COM17/COM16（CH342）、COM18（MCP2200）、COM5（CP210x，无关口） |
| 5 | 无 FlashProgrammer 冲突 | ✅ 无 csky/flashprog 进程 |
| 6 | 无 1025 ESTABLISHED 残留 | ✅ 重启后 `no ESTABLISHED` |

> 项 3 的探测本身会留连接残留，故探测通过后**重启了一次 DebugServer** 再开跑
> （qsh-debug §2.3：唯一可靠的清残留手段）。

---

## 3. ⚠️ 构建阻塞：一个与本任务无关的语法损坏（已恢复，须知会）

首次构建 **BUILD FAILED**，但**不是** DMA 改动引起：

```
zephyr/include/zephyr/drivers/i3c/target_device.h:259:17: error: expected expression before 'return'
  259 |                 return -ENOSYS;
```

真实原因在同文件 **:258** —— 多了一个游离的 `++`：

```c
	if (api->target_tx_write == NULL) {++      /* ← 损坏处，紧随 '{' */
```

- `git diff` 相对 HEAD 的**唯一**差异就是这一处（未提交的意外编辑，文件 mtime
  **2026-09-23 16:10:08**）。该头文件被 i3c 驱动链引入 ⇒ 它坏掉会**阻塞所有含 i3c 的固件构建**。
- **处置**：`git checkout -- include/zephyr/drivers/i3c/target_device.h`（还原为 HEAD 内容，
  等价于探针清理纪律 qsh-debug §8），还原后 `git status` 该目录干净。
- **判据**：这是**还原**（1 个字符的语法错误），不是功能改动；不是本任务写域内的代码。
  若该文件此刻正被别的会话编辑，请注意本次已将其重置到 HEAD。
- **与本任务的关系**：无关。DMA 相关的 TU（`dma_dw_common.c`）在同一轮构建里**编译通过**，
  仅有的 `SET_BIT` 宏重定义 warning 是既有现象、与本次改动无关。

---

## 4. 是否真的重编译了（本次验证的核心前提）

**是。** 源码 mtime 晚于旧 ELF，且新 ELF/obj 均为本轮生成：

| 产物 | 旧 mtime | 新 mtime |
|---|---|---|
| `zephyr/drivers/dma/dma_dw_common.c`（源） | — | **2026-09-23 16:47:03** |
| `modules/hal/linkedsemi/soc/rv32/qsh/ls_soc_gpio.c`（源） | — | **2026-09-23 16:46:55** |
| `case_default/.../zephyr.elf` | 04:48:51 | **17:05:40** |
| `case_default/.../dma_dw_common.c.obj` | — | **17:05:39** |
| `case_default/.../dma_linkedsemi.c.obj` | — | **17:05:34** |
| `case_default/.../D_/.../ls_soc_gpio.c.obj` | — | **17:05:31** |
| `case_destructive/.../zephyr.elf` | 04:52:39 | **17:09:20** |
| `case_destructive/.../dma_dw_common.c.obj` | — | **17:09:19** |

- 未使用 `--skip-build`（本次就是要改动真正编进去）。
- 启动固件 banner 版本号从 `openbmc-zephyr-v0.1.0-827-g5f7a7de96f39`
  变为 `-831-ge862da23a408` ⇒ **烧的是新构建的镜像**（不是复用的旧 ELF）。

---

## 5. 复测结果（原始）

命令（未加 `--skip-build`，写入同一会话目录 `reports/2026-09-23-1`）：

```bash
cd D:/zephyr4_0/810_driver_test
python projects/dma_test/tests/run_test.py --case case_default     --session-dir reports/2026-09-23-1
python projects/dma_test/tests/run_test.py --case case_destructive --session-dir reports/2026-09-23-1
```

### 5.1 `case_default` — EXIT=0

```
## case_default: PASS (7/7)
  test_dma_channel_config_stop: PASS
  test_dma_dmac2_transfer: PASS
  test_dma_invalid_channel: PASS
  test_dma_lli_chain: PASS
  test_dma_loop_transfer: PASS
  test_dma_multi_channel_concurrent: PASS
  test_dma_transfer_ones_zeros: PASS
```

串口：`PROJECT EXECUTION SUCCESSFUL`；`Assertion failed` **0** 条。

### 5.2 `case_destructive` — EXIT=0

```
## case_destructive: PASS (9/9)
  test_dma_abort_mid_transfer: PASS
  test_dma_block_too_large: PASS
  test_dma_data_size_zero: PASS
  test_dma_invalid_data_size: PASS
  test_dma_large_block: PASS
  test_dma_lli_pool_exhaust: PASS
  test_dma_one_byte: PASS
  test_dma_self_copy_rejected: PASS
  test_dma_suspend_resume_guards: PASS
```

串口：`PROJECT EXECUTION SUCCESSFUL`；`Assertion failed` **0** 条。

### 5.3 判据核验（任务书指定）

| 判据 | case_default | case_destructive |
|---|---|---|
| `channel 0 must be inactive to reconfigure` 出现次数 | **0** | **0** |
| `... address are identical` 出现次数 | 0（本 case 无此项） | **1**（`test_dma_self_copy_rejected` 走此路径 ✅） |
| `suspend_idle_ret` | 无此项 | **`-22`**（`-EINVAL`，非 `0` ✅） |
| `Assertion failed` 条数 | 0 | 0 |

### 5.4 基线与复测对比

**基线取证先于复测**（G-P2V-2）：复测前已把两 case 的旧 `serial_TGT.log` 抄录并
另存为 `serial_TGT.baseline.log`（同目录，供复核）。

| case | 修复后基线（今日早些时候） | 本次复测 | 结论 |
|---|---|---|---|
| `case_default` | PASS 7/7 | **PASS 7/7** | 一致 |
| `case_destructive` | PASS 9/9 | **PASS 9/9** | 一致 |

原始串口 diff（剥离时间戳后）：

- `case_destructive`：**仅** banner 版本行不同（`-827-...` → `-831-...`），
  其余逐字节一致。
- `case_default`：除 banner 版本行外，差异全部来自「源码行号/地址后移」——
  `dma_dw_common.c:200` → `:206`（既有负例陷阱 `ASSERTION FAIL [cfg->source_burst_length > 0]`
  的行号，仍按设计触发并恢复）、若干寄存器 dump 地址值偏移。**无任何用例判定变化。**

---

## 6. 是否有新增失败

**无。** 两 case 均 PASS，与基线逐项一致；无任何新增 FAIL / SKIP / MISSING / NOT_RUN。

---

## 7. 空间是否告急

- 开工 `df -h /d`：73G used / **3.4G avail（96%）**。
- 收工复查：见 §8 备注（构建只增量重编若干 TU + 重链接，未产生新的大目录）。
- 目前已通过门槛，**但 3.4G 处于紧张区间**，后续大范围重建（如 HAL 改动引发全量重编）
  仍有触底风险，建议适时清理（**清理需用户批准，本次未删任何东西**）。

---

## 8. 诚实边界（必读）

1. **本次不能证明 ERR 分支那两行正确。** 该分支在本平台不可注入（`OPEN_ITEMS O-8` 结项），
   测试里**永远不会执行**。本次结论的措辞只能是「**回归通过，无新增失败**」，
   **不是**「已验证修复」。
2. 本次证明的命题：该改动**没有破坏原有功能**（TFR / BLOCK / 配置 / 停止 / 负例路径）。
3. 同构建内还有 HAL 侧 `ls_soc_gpio.c::io_cfg_input()` 的改动，**无测试覆盖**、本次未单独验证
   （任务书已声明无需）。
4. 构建阻塞（§3）是**独立事件**，与 DMA/HAL 改动无关；已还原，但上游 HEAD 之外的这处
   未提交损坏**来源未知**，建议知会相关会话。
5. 未改用例判定口径、未放宽任何断言。

---

## 9. 证据文件

| 文件 | 说明 |
|---|---|
| `reports/2026-09-23-1/dma_test_case_default/serial_TGT.log` | 本次复测原始串口 |
| `reports/2026-09-23-1/dma_test_case_default/serial_TGT.baseline.log` | 复测前基线（抄录留存） |
| `reports/2026-09-23-1/dma_test_case_destructive/serial_TGT.log` | 本次复测原始串口 |
| `reports/2026-09-23-1/dma_test_case_destructive/serial_TGT.baseline.log` | 复测前基线（抄录留存） |
| `reports/2026-09-23-1/dma_test_case_{default,destructive}/dma_test_report.{md,json}` | 本轮重写（复测覆盖） |
| `reports/2026-09-23-1/summary.md` | 会话汇总已更新（两 case PASS） |
