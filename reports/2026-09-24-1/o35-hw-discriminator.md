# O-35 残余（HW 层版本）零成本判别实验 — 结论：**假设被整体否定**

- 日期：2026-09-24
- 工程 / case：`i2cf_test` / `case_default`（单核 `lsqsh_evb@1os/lsqsh/cpu1`）
- 板卡独占：本会话独占板子（无并发硬件任务）
- 相关台账：[`OPEN_ITEMS.md`](../../OPEN_ITEMS.md) O-35；前置定性报告 `reports/2026-09-23-1/o35-fill-bitmap-source-triage.md`

## 1. 被检验的假设

`O-35` 残余的 HW 层版本：

> **i2c filter 驱动「就地改写"已在用通道"的授权地址」在硬件层不生效** ——
> 即"使能态下写地址寄存器被硬件忽略"。

软件层已被离线源码定性排除（`i2c_filter.c:157` 重址扫描判据豁免自身、idx 0/1 落同一寄存器），
只剩"寄存器头无行为语义、源码判不了"的 HW 层版本。

## 2. 实验设计（单变量对照）

**改动**：`projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c:963`，通道号 `1` → `0`，**仅此一个字符**。

```c
962  i2cf_filter_off(dev);   /* 先退拦截态再改表 */
963  zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 1, slave, bm),   /* ← 改前 */
964             "authorize %#x in whitelist (channel 1)", slave);
```

改后 `:963` 为 `..._fill_bitmap(dev, 0, slave, bm)`。

**为什么是干净的单变量对照**：

| | 改前（idx=1） | 改后（idx=0） |
|---|---|---|
| 被写的通道 | 通道 **1**（此前未用，`SMBF_ADDRESS_ENABLE[1]` 由 0→1） | 通道 **0**（**此前已授权** `authorized=0x2A`，其 `SMBF_ADDRESS_ENABLE[0]` **已为 1**） |
| 语义 | **新增通道** | **就地改写已在用通道**（= O-35 假设场景） |

- 两者**只差"通道号"**：发起方（`up`）、通路（`switch_to_filter()` 后经滤波器）、
  目标地址（`slave=0x33`）、长度、命令位图（`bitmap_all_on`，全放行）、滤波器开关状态全部不变。
- 现行代码**已含热身笔**（`:968-970`），"判读笔之前有没有热身"这一 confound 已被排除
  （`i2c_failure_triage.md` ⑥ 的污染对照不再成立）。
- `idx=0` 与 `idx=1` 在驱动里落**同一寄存器** `WHITELIST_ADDRESS_3_0`（`offset=(idx>>2)<<2` 均为 0），
  差别只在 `field[0]` vs `field[1]` 与 `SMBF_ADDRESS_ENABLE[0]` vs `[1]`。

## 3. 机械判读（任务书给定）

| 结果 | 结论 |
|---|---|
| PASS 18/18 | **假设被整体否定** —— 就地改写与新增通道行为一致 ⇒ O-35 彻底结案 |
| FAIL | 假设成立（HW 层） |

## 4. 实测结果：**PASS 18/18**

```
cd D:/zephyr4_0/810_driver_test/projects/i2cf_test/tests
python run_test.py --case case_default --session-dir d:/zephyr4_0/810_driver_test/reports/2026-09-24-1
```

```
  filter_blocks_nonwhitelist PASS
-> 18/18 PASS, skipped=0, execution=SUCCESSFUL
```

### 4.1 关键证据（就地改写确实在 HW 生效）

`serial_TGT.log`：

```
INFO filter_blocks_nonwhitelist: whitelist=0x2a,target=0x33,blocked_ret=-5,authorized_ret=0
```

- 改后 ch0 的授权地址被**就地改写** `0x2A → 0x33`（ch1 未填、保持禁用）；
- 随后对 `0x33` 的正式读得 **`authorized_ret=0`**（成功）；
- 而同一次运行里对 `slave` 的读在改表前得 `blocked_ret=-5`（被拦）⇒ **阻断机制本来在工作**。
- 若硬件忽略了这次就地改写，ch0 会仍是 `0x2A`、`0x33` 仍属未授权 ⇒ 该笔应得 `-EIO`（FAIL）。
  实测得 `0` ⇒ **就地改写落到了硬件**，与"新增 ch1"路径**行为一致**。

### 4.2 证据可信性（排除 #117 类静默假 PASS）

- 归档完整性：`scripts/check_archive.py`（2026-09-24 迁出 `tmp/`；核集合改由工程决定）对 `serial_TGT.log` 报 **`[TGT] OK lines=86`**
  （H1 首条 ztest 信号 = `ZT_BOOT TGT`；H2 `ZT_BOOT`/`Running TESTSUITE`/`PROJECT EXECUTION` 各恰 1 次；
  H3 末条非空行 = `PROJECT EXECUTION SUCCESSFUL`）。`[CTRL] MISSING` 属单核工程设计
  （无 CTRL 口），**基线快照同样报 BAD/同一原因** ⇒ 本轮与已知良好基线结构一致，非退化。
  （该 `[CTRL] MISSING` 系**旧版写死两口**所致，**已修**：新版按工程核数判定，单核只核 TGT、不再报
  BAD —— 见 `KNOWN_ISSUES` #117 增补段。原误报记载留痕不删。）
- `execution=SUCCESSFUL`、18/18 全 token 来源、无 MISSING。

## 5. 结论

**假设被整体否定。** "使能态下写地址寄存器被硬件忽略"不成立：**就地改写已在用通道**与
**新增通道**在本平台上**行为一致**。`O-35` 的残余（HW 层版本）就此关闭，O-35 **彻底结案**。

## 6. 收尾自证

- `:963` 已手工改回 `1`（**未使用任何 git 回退命令**）。
- grep 自证：`963: zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 1, slave, bm),` —— 已复原。
- `git diff -- projects/i2cf_test/cpu1_i2cf_test/src/i2cf_test.c` **输出为空**（该文件相对 HEAD 无净变化，
  与实验前一致）。

## 7. 意外 / 需知会项（FLIP 显式告知）

1. **同名 case 目录被复测覆盖**：本轮 `--session-dir` 指向本日会话 `2026-09-24-1`，
   按"复测覆盖同名 case 目录"惯例替换了原 `i2cf_test_case_default/`（原为全量回归的 PASS 18/18）。
   - 覆盖前后**判定均为 PASS 18/18，无 FLIP**；
   - 原报告的**报告头**环境行为 `tgt_uart=COM16`，本轮为 `tgt_uart=auto`（自动发现同样落到 CH342 Serial-B = COM16，
     非实质变化）；
   - 原基线四件套已快照于 `tmp/o35_base/i2cf_test_case_default/`，可回溯。
2. **改动的写入时机**：`:962` 先 `i2cf_filter_off()`（全局退拦截态）再 `:963` 改表。**全局 filter 关闭**，
   但 **ch0 的 per-channel 使能位 `SMBF_ADDRESS_ENABLE[0]` 仍为 1**（`filter_en(...,clr_tbl=false)`
   不清该寄存器，已核 `i2c_filter.c:226-251`）⇒ 本次写入**正是"通道已使能态下的地址寄存器写入"**，
   与 O-35 假设的场景一致，判别有效。
3. 未改动任何其它行；`OPEN_ITEMS.md` 的 O-35 行**本会话未修改**（不在任务产出范围内）——
   建议由台账维护方将其标记为**彻底结案**。

## 附：原始证据文件

- `reports/2026-09-24-1/i2cf_test_case_default/serial_TGT.log`
- `reports/2026-09-24-1/i2cf_test_case_default/i2cf_test_report.md` / `.json`
- 基线快照：`tmp/o35_base/i2cf_test_case_default/`
