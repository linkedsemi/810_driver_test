# qspi_flash_test 回归失败根因报告（2026-09-23）

- 会话：`reports/2026-09-23-1`
- case：`qspi_flash_test_case_default`（14/26）、`qspi_flash_test_case_no_calib`（24/26）
- 方法：**纯源码级 + 串口日志复核**（未构建、未烧录、未碰硬件/串口/DebugServer）
- 行号约定：文中 **报告里的行号** 指改动前的 `qspi_flash_test.c`；
  本轮改动后 `qspi_flash_test.c` 从 :392 起整体后移（`:682 → :729`、`:698 → :746`）。

---

## 0. 结论摘要

**两个独立根因**（证实任务书的"两个独立根因"判断，但**推翻**其中一条的具体归因）：

| # | 根因 | 归属 | 失败项 | 可修性 |
|---|---|---|---|---|
| **A** | `qspi_ls.c` 的"低于校准时钟"分支用 `calib_combo = 1U` 作哨兵；2026-09-08 的表重排把该哨兵从 `{0,0}`（参考值）改成了 `{1,0}`（**校准自己判定为无效**的组合）⇒ 一旦请求频率 < 校准时钟 10 MHz，驱动就永久装上坏捕获，器件读全错 | **驱动层**（`zephyr/drivers/spi/qspi_ls.c`，**写域外**） | default 12 项中的 **10 项** | ❌ 本次**未改**，见 §4 交接 |
| **B** | 用例把器件容量写死 32 MiB；**实测器件是 64 MiB** ⇒ "越界"地址其实在合法区内，驱动按契约正确返回 0 | **用例层**（写域内） | 两 case 各 2 项（4 处） | ✅ **本次已修** |

**任务书的初步观察**：
- ✅ "default 里 `ret_init` + 一大片 `rbuf != wbuf` 是**单根因**"——**成立**（= 根因 A）。但机理不是"flash 根本没起来"，
  而是"**校准状态被污染 → 捕获时序错 → 读到的全是垃圾**"；器件本身是在的、命令也发出去了。
- ❌ "`read/write_past_end_rejected` 是驱动**缺少越界地址校验**"——**推翻**。
  驱动**有**该校验（`spi_nor_multi_dev.c:1385` / `:1476`，返回 `-EINVAL`），
  `TRIAGE_QSPI_SPIF_2026-09-18.md` 的反汇编也已证明它编进了固件。
  **本轮新增证据直接定案**：日志里 `flash_size = 64 MiB`，32 MiB 的地址**没越界**。
  ⇒ 这是**用例常量陈旧**，不是驱动缺校验。

---

## 1. 根因 A —— 校准哨兵指向了被校准判定为无效的捕获组合

### 1.1 代码事实

`zephyr/drivers/spi/qspi_ls.c`（均为当前 HEAD 内容，`git status` 干净）：

```c
/* :46-51 组合表：索引 0 = {1,0}（不是参考值！） */
static const struct {
	uint8_t neg; uint8_t dly;
} calib_combos[] = {
	{1, 0}, {0, 0}, {1, 1}, {0, 1}, {1, 2}, {0, 2}, {1, 3}, {0, 3},
};
#define REF_CAP_DLY   0U          /* :36 */
#define REF_CAP_NEG   0U          /* :37 */
```

```c
/* :957-961 频率低于校准时钟时的分支 */
if (calib_hz > config->frequency) {
	data->calib_combo = 1U;                       /* ← 问题点 */
	data->calib_status = QSPI_TIMING_CALIB_UNUSED;
	goto no_calibration;                          /* ret == 0 → 返回"成功" */
}
```

```c
/* :863-876 组合值 → 实际捕获寄存器 */
static void stg_capture_apply_calibrated(const struct device *dev)
{
	uint8_t stored = data->calib_combo;
	if (stored == 0U || stored == CALIB_COMBO_INVALID ||
	    stored > (CALIB_COMBO_MAX + 1U)) {
		return;
	}
	stg_capture_set(info->reg, calib_combos[stored - 1U].neg,
			calib_combos[stored - 1U].dly);   /* stored=1 → calib_combos[0] = {1,0} */
}
```

即：`calib_combo == 1` **就是**"使用 `calib_combos[0]`"。
在**旧表**里 `calib_combos[0] == {0,0} == REF`，所以这个哨兵等价于"不校准、用参考捕获"；
在**新表**里它变成了 `{1,0}`。

而本驱动的所有**其它**"不校准"路径都显式写参考值，与它不一致：
`apply_hw_config()` `:1245` 与 `no_calibration` 错误路径 `:1057` 都是
`stg_capture_set(reg, REF_CAP_NEG, REF_CAP_DLY)` = `{0,0}`。

### 1.2 这是**新回归**（2026-09-08 引入），不是陈年问题

```
$ git log --format="%h %ad %s" --date=short -S "calib_combo = 1U" -- drivers/spi/qspi_ls.c
75ff6a2a531 2026-09-08 qspi_ls_cali_to_id c
aef6d47dca6 2026-06-22 qspiv2 driver
```

`75ff6a2a531`（2026-09-08）的 diff 里**同时**做了两件事，但只改了表、没改哨兵：

```diff
-#define CALIB_COMBO_MAX      6U
+#define CALIB_COMBO_MAX      7U
-	{0, 0}, {0, 1}, {1, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3},
+	{1, 0}, {0, 0}, {1, 1}, {0, 1}, {1, 2}, {0, 2}, {1, 3}, {0, 3},
```

**交叉证据（本次新增，很硬）**：`linkedsemi_zephyr_project/samples/spi_ls_flash_nor/README.rst`
（该文件最后修改 2026-06-26，早于重排）记录的期望输出里写着：

```
[00:00:00.096,000] <inf> qspi_ls: qspi2@40040000: useless calibration, all capture delay ok
[00:00:00.105,000] <inf> qspi_ls: qspi2@40040000: hw config: spi=50000000 Hz cap_neg=0 cap_dly=0 (calib_combo=0x01)
```

- `calib_combo=0x01` **搭配 `cap_neg=0 cap_dly=0`** —— 正是**旧表**语义（索引 0 = {0,0}）。
- 而本轮实测日志是 `calib_combo=0x01` 搭配 `cap_neg=1 cap_dly=0`（`serial_TGT.log:86`）—— 新表语义。
- 同一份 README 里的 `useless calibration, all capture delay ok` 这句话在当前源码里**已经不存在**
  （`grep -rn "useless calibration" zephyr/drivers/spi/` 无命中）⇒ 该日志确系旧驱动版本产物。

⇒ **同一个 `calib_combo` 值（0x01）在两个版本里代表两种不同的硬件捕获配置**，这就是回归。

### 1.3 现场链（逐行对得上 `serial_TGT.log`，default 案）

器件：`qspi2` 50 MHz + 校准开（校准时钟 10 MHz）。ztest 用例按链接序执行：

| 步骤 | 驱动行为 | 日志行 |
|---|---|---|
| 上电 init | 校准在 50 MHz 跑通，有效窗 `[1,8)`（**索引 0 被判无效**），选 `combo 5 = {1,2}` | `:15-16` |
| `set_freq_above_max_rejected` | 未污染（`calib_hz(10M) > 200M` 不成立） | `:56/:65` |
| **`set_freq_below_min_rejected`** | `6 MHz < 10 MHz` ⇒ **命中 `:957` 分支 → `calib_combo := 1`**；随后 `apply_hw_config` 的 `apply_clk(6 MHz)` 因 `clk_cyc=32` 返回 `-EINVAL`（`re_init=-19`，用例按预期 PASS）。**此刻 HW 仍是 50 MHz/{1,2}，坏值只躺在 `data->calib_combo` 里** | `:77` |
| 该用例的 `after` 钩子 | `set_freq(50M)` + `re_init`：**先探测 JEDEC**（用旧捕获 {1,2}）→ 成功；再 `read_init`，此时 `calib_combo!=0` ⇒ 打 `Already executed calibration.` ⇒ **不再重新校准**；最后 `apply_hw_config` 把 `{1,0}` 装上 | `:80`、`:85`、`:86` |
| **`set_freq_boundary_6m25_accepted`** | `re_init` 的**第一步 JEDEC 探测**用的正是上一步装上的 `{1,0}` ⇒ **`invalid jedec id 00 00 00`×19 ⇒ `re_init = -19`** ⇒ 断言 `:682` 失败 | `:90-92`、`:97` |
| 该用例的 `after` 钩子 | 探测再次失败 ⇒ `re_init` 提前返回，**永远走不到 `apply_hw_config`** ⇒ `{1,0}` 再也换不掉 | `:98-101` |
| `set_freq_zero_silent` + `qspi_flash_rw` / `qspi_flash_stg` **全部** | 全部在 `{1,0}` 捕获下跑 ⇒ RDID/擦/读/写/STG 全错（`erase_sector_all_ff` 读到 `0xcf`、`write_read_lenN` 差异 239/242/3842/7685 B、`raw_read_1_1_1_ok` 差 249 B、`stg_read_chunked_over_4096` 差 7960 B） | `:104-175`、`:185-212` |

**关键点（解释"为什么 after 钩子救不回来"）**：`re_init` 一旦失败就**在 JEDEC 探测处早退**，
`apply_hw_config()` 不会被调用 ⇒ 坏捕获**永久驻留**，直到复位。这也解释了
任务书观察到的"after 钩子恢复了频率却仍全挂"。

**为什么 `case_no_calib` 不沾根因 A**：`timing-calibration-disabled` 令 `:952-955` 首查命中，
`calib_combo` 始终保持初值 0 ⇒ `stg_capture_apply_calibrated()` `:869` 直接 return ⇒
捕获恒为 hw 默认 `{0,0}` ⇒ 6.25 MHz 照样能读（`no_calib/serial_TGT.log:83-100`，
`set_freq_boundary_6m25_accepted ... reinit=0` PASS）。**这同时正面证明：`{0,0}` 在 6.25 MHz 可用**，
即 `{1,0}` 才是坏值 —— 根因 A 的独立反证。

### 1.4 为什么这不是"用例期望写错"

- 用例断言的是 `apply_clk()` 的**公开边界契约**：`clk_cyc = bus/spi - 1 ≤ 31`
  ⇒ `spi_hz ≥ 6.25 MHz` 应被接受（用例头 §"QSPI_BUS_HZ" 注释与 `apply_clk():834-838` 一字不差）。
- `case_no_calib`（25 MHz、校准关）证明该频率下器件**确实能工作**；
  校准开的 50 MHz 也证明器件本身没问题。
- 把两 case 的差异全部收敛到"`calib_combo` 哨兵值"这一个变量后，失败边界与
  `git log -S` 找到的改动窗口（2026-09-08）**完全吻合**。

⇒ 判 **驱动缺陷**，且是**回归**（旧表下 `calib_combo=1 ⇒ {0,0}` 无害）。

### 1.5 失败项归属（default 12 项）

| 失败项 | 归属 |
|---|---|
| `set_freq_boundary_6m25_accepted`、`set_freq_zero_silent` | 根因 A（直接受害者） |
| `erase_sector_all_ff`、`write_read_len1/255/256/4096/8192`、`raw_read_1_1_1_ok`、`stg_read_chunked_over_4096` | 根因 A（坏捕获的下游） |
| `read_past_end_rejected`、`write_past_end_rejected` | **根因 B**（与 A 无关；两 case 都挂） |

---

## 2. 根因 B —— "越界"地址其实没越界（用例常量陈旧）

### 2.1 驱动侧是有校验的（推翻"缺少越界校验"）

`zephyr/drivers/flash/spi_nor_multi_dev.c`：

```c
/* :1385  spi_nor_read()  */  if ((addr < 0) || ((addr + size) > flash_size)) return -EINVAL;
/* :1476  spi_nor_write() */  if ((addr < 0) || ((size + addr) > flash_size)) return -EINVAL;
```

### 2.2 真实容量 = 64 MiB（本轮日志直读，无需推断）

`serial_TGT.log`（两 case 一致）：

```
:12  <inf> spi_nor: flash2@0: 64 MiBy flash          ← flash_size = 0x4000000（SFDP BFP DW2 = 512 Mbit）
:9   <inf> spi_nor: flash2@0: flash present, jedec id 20 40 20   ← ID 字节 2 = 0x20 ⇒ 512 Mbit
```

用例读的地址是 `QSPI_FLASH_EXPECT_SIZE = 0x2000000`（32 MiB）：
`0x2000000 + 16 = 0x2000010 < 0x4000000` ⇒ **在范围内** ⇒ 驱动正确执行并返回 `0`
（日志 `read past end should be -EINVAL, got 0`）⇒ **用例期望不成立**。

`jemd216_bfp_density()`（`include/zephyr/drivers/jesd216.h:206`）是 Zephyr 标准实现
（`dw2 + 1` bits，bit31 特殊编码），非本项目自研 ⇒ SFDP 侧解析可信；
且 JEDEC ID 与 SFDP 密度**两路独立来源同时指向 512 Mbit**。

### 2.3 顺带解释文档里的"32 MiB Puya"

`QSPI_FLASH_EXPECT_SIZE / ID0..2` 的出处是
`samples/spi_ls_flash_nor/README.rst`（2026-06-26）的运行日志，其器件指纹与本轮**全不同**：

| | 文档（2026-06-26） | 本轮实测（2026-09-22） |
|---|---|---|
| JEDEC ID | `85 23 19` | `20 40 20` |
| 容量 | `32 MiBy` | `64 MiBy` |
| SFDP | `v 1.10`，`PH0 ff00 rev 1.8: 23 DW`，`PH1 ff84 rev 1.1: 2 DW` | `v 1.6`，`PH0 ff00 rev 1.6: 16 DW`，`PH1 ff20 rev 1.0: 4 DW` |

⇒ **qspi2 上挂的器件已换型**（或换了板子/板级版本）。这属**硬件/文档一致性**问题，
需要你确认（见 §6 未定项 1）。

### 2.4 为什么改用例而不是改驱动

- 驱动行为**正确**（按 `flash_size` 拒越界），改驱动反而会破坏契约。
- 用例的**目的**是"越界必须被拒"，**不该依赖某个写死的容量**。已改为
  **运行期推导器件容量**（本工程 `CONFIG_FLASH_PAGE_LAYOUT=y` + `CONFIG_SPI_NOR_SFDP_RUNTIME=y`，
  两个前提都在已构建的 `zephyr/.config` 里核实过），器件再换型时用例自动跟随。

---

## 3. 本次改动（写域内，已落地）

| 文件 | 位置 | 改动 | 为什么能修 | 副作用 |
|---|---|---|---|---|
| `810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c` | 新增 `flash_device_size()`（:392-419）+ 重写 `read_past_end_rejected`(:421)/`write_past_end_rejected`(:437) | 越界地址由**运行期推导的器件容量**给出，不再用写死的 32 MiB；顺带上报 `ZT_INFO(... flash_size=%zu ...)` | 容量取自 flash 层 layout（`flash_get_page_count()` × `flash_get_page_info_by_idx(dev,0,&info).size`），与驱动判越界用的 `flash_size` 同源 ⇒ 必然落在边界外 ⇒ 驱动该返回 `-EINVAL` 时用例必 FAIL，不会假 PASS | 无。`flash_get_page_count()`/`flash_get_page_info_by_idx()` 是 Zephyr 公共 API（`flash.h:397,410,421`），非私有符号；查询失败时 `zassert_not_equal(flash_size,0)` **fail-loud**，不做静默兜底 |
| 同上，文件头 :6-8 | 器件描述加"文档值 vs 实测值"标注 | 防文档漂移（KNOWN_ISSUES #108 同族） | 纯注释 | 无 |
| `810_driver_test/projects/qspi_flash_test/tests/include/qspi_flash_test.h` | :20-31 常量区 | 记录"文档 32 MiB/85 23 19" vs "实测 64 MiB/20 40 20" 的矛盾与判据；标注 `QSPI_FLASH_EXPECT_SIZE`/`QSPI_FLASH_ID*` **仅供未注册的 `qspi_flash_id` 套件使用，启用前必须上板复核** | 防止下一个看文档的人按 32 MiB 写用例（#96 那类陷阱） | 无（常量值未改，未注册套件的断言保持原样） |

**未改动**：`zephyr/drivers/spi/qspi_ls.c`（写域外）、`zephyr/drivers/flash/spi_nor_multi_dev.c`（无缺陷）。

**预期效果（离线推断，需上板确认）**：
- `case_no_calib`：24/26 → **26/26**
- `case_default`：14/26 → **16/26**（只翻 past_end 两项；其余 10 项要等根因 A 的驱动修复）

---

## 4. 需要**写域外**改动的项（交接给驱动侧，本次未改）

### 【必须】`zephyr/drivers/spi/qspi_ls.c` —— 1 处（含 2 个编辑点）

**目的**：让"请求频率低于校准时钟"时使用**参考捕获 `{0,0}`**，而不是被校准判定为无效的 `{1,0}`。

推荐改法（与文件内其它 no-calibration 路径语义对齐）：

1. `:42-44` 附近新增一个与 `CALIB_COMBO_INVALID(0xFF)` 区分的哨兵：
   ```c
   #define CALIB_COMBO_REF      (UINT8_MAX - 1U)   /* 0xFE = 不校准，用 REF 捕获 */
   ```
2. `:957-961` 的分支改用它：
   ```c
   if (calib_hz > config->frequency) {
       data->calib_combo = CALIB_COMBO_REF;
       data->calib_status = QSPI_TIMING_CALIB_UNUSED;
       goto no_calibration;
   }
   ```
3. `stg_capture_apply_calibrated()`（`:863-876`）在范围守卫**之前**补：
   ```c
   if (stored == CALIB_COMBO_REF) {
       stg_capture_set(info->reg, REF_CAP_NEG, REF_CAP_DLY);
       return;
   }
   ```

**改后行为**：`calib_combo` 非 0/非 INVALID ⇒ `apply_hw_config()` `:1231-1233` 不会把频率强拉回校准时钟；
`:1247` 走 `stg_capture_apply_calibrated()` ⇒ 装上 `{0,0}` ⇒ 6.25 MHz 可读（`case_no_calib` 已证明）。

**次选（1 token，但脆弱）**：把 `:958` 改成 `data->calib_combo = 2U;`
（新表索引 1 = `{0,0}`）。**不推荐**——它仍在用"表里的位置"表达"参考值"，
下次再动 `calib_combos[]` 会以同样方式静默复发（正是本 bug 的成因）。

**回归护栏建议**：在 `qspi_flash_test` 的 `qspi_flash_calib` 套件里补一项
"freq < 校准时钟后，器件仍可读（RDID 有效）"，把该类哨兵错误钉死。
（本次未加——新增用例属 `qsh-test-gen` 流程，且未上板验证过的新用例不宜进回归。）

### 【建议】`810_driver_test/projects/qspi_flash_test/tests/COVERAGE.md`

§"预期来源"列与 §覆盖矩阵里的 `QSPI_FLASH_EXPECT_SIZE = 0x2000000`、`jedec_id == 0x852319`
仍是文档值。属文档域（keeper），未改。

---

## 5. 上板怎么验（确切命令 / 预期 / 为什么必须上板）

> ⚠️ 本轮**没有**做任何构建/烧录。以下命令供**持有板子与硬件锁的 runner** 执行。
> 分两段：**只验根因 B（现在就能跑）** 与 **验根因 A（须先落 §4 的驱动改动）**。

### 5.1 只验根因 B（不需要改驱动，现在即可跑）

```bash
cd D:/zephyr4_0/810_driver_test/projects/qspi_flash_test/tests
# 单 case（含构建 + 加载 + 采集 + 解析；test.sh 走 ${SINGLE_BOARD}，不硬编码板级）
bash case_default/test.sh -g
bash case_no_calib/test.sh -g
# 或工程级（两个 case 都跑，自动归档到 reports/<date>-1/）
python run_test.py
```

**预期结果**
1. `case_no_calib` → **26/26, OVERALL: PASS**（从 24/26 起）。
2. `case_default` → **16/26**：`read_past_end_rejected` / `write_past_end_rejected` 转 PASS；
   其余 10 项**仍 FAIL**（根因 A 未修，属预期，不是新问题）。
3. 串口新增两行可核对的 `ZT_INFO`（`echo "$(date +%F)"` 对应会话目录里的 `serial_TGT.log`）：

```
INFO read_past_end_rejected: flash_size=67108864,off=67108864
INFO write_past_end_rejected: flash_size=67108864,off=67108864
```
   `flash_size` 若**不是** 67108864（0x4000000），说明器件/板级又变了 —— 用例仍会正确工作，
   但请把该值记进 §6 未定项 1。

**判定要点**：这两项转 PASS **必须是"驱动返回 -EINVAL"**，即日志里
`ret not equal to -EINVAL` 的 assertion 不再出现；只要 `flash_size` 与实际器件一致，
用例就**不可能**假 PASS（地址必然越过末尾）。

### 5.2 验根因 A（须先落 §4 的 `qspi_ls.c` 改动）

```bash
cd D:/zephyr4_0/810_driver_test/projects/qspi_flash_test/tests
# 驱动 .c 改动必须删 build 全量重建（KNOWN_ISSUES #9 / #37/#55：-p auto 不吃源码外的缓存）
rm -rf case_default/build case_no_calib/build
bash case_default/test.sh -g
bash case_no_calib/test.sh -g
```

**预期结果：两 case 均 26/26、`PROJECT EXECUTION SUCCESSFUL`。**

关键日志锚点（`case_default`，逐条对上当前失败行）：

| 锚点 | 当前（失败） | 修复后（预期） |
|---|---|---|
| `set_freq_below_min_rejected` 之后那次 `after` 钩子的 `hw config` | `cap_neg=1 cap_dly=0 (calib_combo=0x01)`（`serial_TGT.log:86`） | `cap_neg=0 cap_dly=0 (calib_combo=0xFE)` |
| `set_freq_boundary_6m25_accepted` | `INFO ... set=0,reinit=-19` + `invalid jedec id 00 00 00` | `INFO ... set=0,reinit=0`，无 `invalid jedec id` |
| `set_freq_zero_silent` | `reinit=-19` | `reinit=0` |
| `erase_sector_all_ff` / `write_read_len*` / `raw_read_1_1_1_ok` / `stg_read_chunked_over_4096` | FAIL（差异 239~7960 B） | PASS（差异 0） |

**为什么必须上板（不能离线判）**
1. 本缺陷是**有状态的硬件时序交互**：坏值 `{1,0}` 会永久驻留 QSPI 捕获寄存器，
   只有复位才清。`regression/replay.py --l2-direct` 能重放"判定链"（把同一份串口文本
   重新判 PASS/FAIL），但它**不产生新的串口文本** ⇒ 无法证明修复生效。
2. `flash_size` 是**器件上电后读 SFDP 得到**的（`CONFIG_SPI_NOR_SFDP_RUNTIME=y`）——
   离线环境里没有这个值，根因 B 的判据本身就只有上板才有。
3. 根因 A 的"`{0,0}` 在 6.25 MHz 可读"目前只有**间接**证据（`case_no_calib` 在 6.25/25 MHz
   全 PASS）；修复后的直接证据只能来自真机跑校准开启分支。
4. 不许把"离线重放判过了"当成"修复验证过"（KNOWN_ISSUES #108 / O-25 的教训）。

---

## 6. 仍未定项

1. **器件换型待你确认**：qspi2 上是 `64 MiB / ID 20 40 20`，而工程/样例文档一律记
   `32 MiB Puya / 85 23 19`，两者的 SFDP rev 与 PH 数也不同。请确认是
   ① 板子换了 flash / 换了板级版本（则应同步更新 `README.rst` 与 COVERAGE 的"实测来源"），
   还是 ② 另有隐情（例如读到的不是目标器件）。**本轮不改 ID 常量**，理由：改了就变成
   "按一行日志写死断言"，而依赖它的 `qspi_flash_id` 套件**当前根本没注册**（KNOWN_ISSUES #96），
   改了也无法上板证伪。
2. **~~`qspi_ls.c` 的修复未落地~~（写域外，§4）** —— **已于第二轮落地，见 §8**；
   落地前 `case_default` 会稳定停在 16/26。该修复本身**尚未构建/未上板验证**（§8.7 / §9）。
3. **`qspi_flash_id` 套件仍未注册**（KNOWN_ISSUES #96，8 项从未执行）。启用时需一并
   复核 `QSPI_FLASH_ID*` / `size_is_32mib` 的期望值 —— 按 §2.3，**当前这组常量几乎肯定是错的**。
4. `set_freq_zero_silent` 用例钉住的"`frequency == 0` 静默接受"仍是驱动已登记的
   设计缺陷（roadmap §5.2 qspi#4），本轮未动；若驱动改为返回 `-EINVAL`，该用例需同步改判
   （用例注释里已写明）。
5. 根因 A 期间 `erase_sector_all_ff` 读到过 `0xcf`、rw/stg 读到过非零垃圾 —— 这些是
   **坏捕获下的读出值**，不代表 flash 内容被破坏。修好后若仍有残留差异，再按新缺陷查。
   本轮**未**验证"坏捕获期间是否可能写坏 flash"（只擦写了 `QSPI_TEST_BASE=1 MiB` 起的测试窗口）。

---

## 7. 建议归档（供 keeper 参考）

- 领域招式候选：**"哨兵值表达'参考/缺省'时，被引用表的语义一变它就静默蜕变"**
  （对照组：`calib_combos[]` 重排 ⇒ `calib_combo=1` 从 `{0,0}` 变 `{1,0}`）。
  与 qsh-driver-review §1.8「过期/失效索引作写地址」同族（都是"索引/哨兵与表的耦合断了"），
  建议并入其 P0 清单或 `debug-knowledge/zephyr`。
- `KNOWN_ISSUES` 候选：**"裁剪/重排常量表时，所有以'下标/哨兵'引用该表的代码必须同步复核"**；
  判别捷径 = 找"同一常量在不同版本代表不同硬件配置"的痕迹（本例 = 样例 README 的旧日志）。
- `TRIAGE_QSPI_SPIF_2026-09-18.md` 的 **Q3/Q4（past_end 未拒）可从"待定"改判为"已结：
  用例常量陈旧"**（该文档 §Q3 列的"待实机一行诊断"正是本轮拿到的 `64 MiBy flash` 日志）。

---

## 8. 驱动侧修复落地记录（第二轮，本 agent）

- 写域：**`zephyr/drivers/spi/qspi_ls.c`（唯一改动文件）**。动手前后均复核 `git status --porcelain drivers/spi/`，
  确认无他人并发改动；同目录其它文件的未提交变更（`i3c`/`dma`/`sdhc`/`wdt` 等）非本轮产物，未触碰。
- 基线：`zephyr` @ `5f7a7de96f39dd1bf651ad089c331466fc094b1a`
- **未构建 / 未烧录 / 未开 DebugServer / 未碰串口** —— 无任何硬件动作

### 8.1 独立复核结论：**根因成立**，且比 §1 的证据链更强

逐条回源码 + git 复核，§1 的每项证据**全部成立**：

| 复核项 | 方法 | 结果 |
|---|---|---|
| 表确被重排 | `git show 75ff6a2a531 -- drivers/spi/qspi_ls.c` | `{0,0},{0,1},{1,1},…` → `{1,0},{0,0},{1,1},…`；`CALIB_COMBO_MAX 6U→7U`；**该 commit 未触碰 `:958`** |
| `calib_combo = 1U` 早于重排 | `git log -S "calib_combo = 1U"` | `75ff6a2a531`(2026-09-08) / `aef6d47dca6`(2026-06-22) —— 引入者是 **6-22**，不是 9-08 ✔ |
| 旧表下 `1U` 确等于 REF | `git show 75ff6a2a531^:… \| grep` | 旧 `stg_capture_apply_calibrated` 与本版**逐字相同**（`calib_combos[stored-1U]`）；旧索引 0 = `{0,0}` ✔ |
| README 交叉证据 | `samples/spi_ls_flash_nor/README.rst:35-36`（`3df2f10`, 2026-06-26） | `calib_combo=0x01` 搭配 `cap_neg=0 cap_dly=0` —— 旧表语义 ✔ |
| `useless calibration` 已消失 | grep `drivers/spi/` | 无命中 ✔ |

**新增加强证据（报告未记录，很硬）——重排删掉了同一坑位的孪生兄弟**：
旧 `:1077`（`best_len == ARRAY_SIZE` 的 "useless calibration, all capture delay ok" 分支）**同样**写
`data->calib_combo = 1U`，语义**也是**"取 `calib_combos[0]` = 参考值"。`75ff6a2a531` 用
`calib_pick_best()` 替换了该分支（连带删掉那条日志）—— 即**同一个 commit 移除了两处位置耦合中的一处，
留下另一处对着新表继续跑**。

⇒ 这不再是"位置耦合理论上会出问题"，而是**本文件里已经实际静默失效过一次、且埋雷与踩雷是同一次提交**。
§4 拒绝次选写法的理由因此**成立，且证据比 §4 自述的更硬**（见 8.3）。

### 8.2 实际落地改动（与 §4 建议的差异）

§4 的 **3 个编辑点全部按建议落地**；差异只有 **1 处纯新增**：

| # | 位置 | §4 建议 | 实际落地 | 差异 |
|---|---|---|---|---|
| 1 | `:42-51` | 新增 `#define CALIB_COMBO_REF (UINT8_MAX - 1U)` | 同；**另补 3 行 `BUILD_ASSERT`** | ➕ **新增**（见下） |
| 2 | `:876-885` | 范围守卫**之前**对哨兵写 `REF_CAP_NEG/REF_CAP_DLY` | 逐字相同 | 无 |
| 3 | `:975-979` | `data->calib_combo = CALIB_COMBO_REF;` | 逐字相同 | 无 |

**偏离项（新增 3 行，超出 §4 范围，待裁定）**：
```c
BUILD_ASSERT(CALIB_COMBO_REF != CALIB_COMBO_INVALID &&
	     CALIB_COMBO_REF > (CALIB_COMBO_MAX + 1U),
	     "CALIB_COMBO_REF must not alias a calib_combos[] entry or CALIB_COMBO_INVALID");
```
**理由**：§4 只要求"用 0xFE"，但 0xFE 的**安全性**建立在"`CALIB_COMBO_MAX` 永远 < 254"这一
**无人守护**的隐含前提上。若日后 `calib_combos[]` 扩到 254 项，或有人把 `CALIB_COMBO_INVALID` 改成
`0xFE`，哨兵会**静默**与真实表项撞值 —— 同一类缺陷换个入口复发（本 bug 的镜像）。该断言编译期即拦下，成本 0。
**若认为越界，删掉这 3 行不影响其余修复。**

注释按 `.claude/DOC_EDIT_RULES.md §3.5` 写：只述**本层**语义与约束（"该值语义 = 不校准，表位置无法表达"），
**不含日期、不含修复叙事**（回归史只留在本报告）。

### 8.3 对 §4"次选写法"理由的评估 —— **报告正确**

§4 拒绝 `calib_combo = 2U` 的理由是"仍靠表里的位置表达参考值，下次重排会以同样方式静默复发"。**评估：成立。**
- **逻辑上**：`2U` 依然把"参考"编码为"表里第 1 个位置"，与 `1U` 编码"第 0 个位置"**是同一种错误**；
  差异只是**这一次**索引 1 恰好是 `{0,0}`。
- **实证上**：位置耦合在本文件**已静默失效过一次**，且就发生在"重排表"的同一次提交里（8.1）—— 证明其失效
  **不需要任何敌意操作，正常维护即可触发**。这一点把 §4 的"推断"升级为"实证"。
- **对照正例**：`spi_dw.c:139` 同族分支写的是 `timing_calibration_delay_arr[cs] = 0;` —— 写**参考值本身**，
  不经过任何表。具名哨兵 == 把 `spi_dw` 这个正确形态搬进 `qspi_ls`（后者因 `calib_combo` 带 +1 偏置，
  无法直接用 0 表达"参考"，故需独立哨兵）。

### 8.4 副作用面评估

`calib_combo` **全仓仅存在于 `qspi_ls.c`**（grep 确认，无 DT 属性、无头文件导出、无 API 消费者）。读写点全清单：

| 行 | 读写用途 | `0xFE` 下的行为 | 相对改前（`1U`） |
|---|---|---|---|
| `:874` → `stg_capture_apply_calibrated` | 装捕获 | 走**新分支** → REF `{0,0}` | **唯一行为变化**（原装 `{1,0}` 坏捕获） |
| `:981` "Already executed calibration" 守卫 | 跳过重校准 | 命中（≠0 且 ≠INVALID） | **不变**（`1U` 同样命中） |
| `:1250` `apply_hw_config` 频率钳位 | 条件 `== INVALID` | **不钳位**，按请求频率走 | **不变** |
| `:1262` `apply_hw_config` 捕获选择 | 条件 `== INVALID` | 走 `apply_calibrated` | **不变** |
| `:1277` 日志 | 打印 | `calib_combo=0xfe` | **文本变化**（§5.2 已列为验收锚点） |
| `:991` 写 `pick+1`（1..8）正常校准 | — | 不受影响 | 新分支只在 `==0xFE` 时命中，且**先于**守卫 return |
| `:1070` 写 `CALIB_COMBO_INVALID` 失败路径 | — | 不受影响 | 同上 |

⇒ **对正常校准流程零影响**：新分支进入条件是 `stored == 0xFE`，而 `0xFE` **只由 `:975` 一个点写入**，
该校验路径（freq < 校准时钟）与正常校准路径（freq ≥ 校准时钟）**互斥**。正常路径取值域仍是
`[1,8] ∪ {0,0xFF}`，故 `calib_combos[stored - 1U]` 恒落在 `[0,7]`，**无越界下标**
（按 qsh-driver-review §1.8「过期/失效索引作写地址」复查：**PASS**）。

**残余风险（一般性，本器件上不成立 ⇒ 不阻塞；列出备案）**：哨兵是**粘性**的。一旦以低于校准时钟的
频率跑过一次，`calib_combo` 即固定为 REF；此后即使把频率**调回**校准时钟以上，`:981` 的
"Already executed" 守卫也会拦住重新校准，于是 REF 被用在 50 MHz。

**但本器件上无害**，且有一条本轮日志的直接反证：`serial_TGT.log:15` 给出
`timing calibration success: combo 5 (cap_neg=1 cap_dly=2) window=[1,8) @ 50000000 Hz`
—— **50 MHz 的有效窗含索引 1，而新表索引 1 正是 `{0,0}` = REF**。即 REF 在 50 MHz 与 6.25 MHz
（`case_no_calib` 已证）**两端都在窗内**，粘性路径因此安全；这也再次说明 `{0,0}` 是正确的落点，
而 `{1,0}`（新表索引 0，被 calibration 判出窗口）才是坏值。

一般情形下 REF 仍**不保证**落在高于校准时钟的有效窗内（换器件/换板可能变），届时粘性路径会暴露。
若要根治，需让"低于校准时钟"这条分支**不声称已校准**（例如置回 0，配合 `stg_capture_hw_applied()` 复检），
属独立设计改动。**未改，等裁定。**

### 8.5 同类落点清单（任务项 3；**均未改**，等裁定）

| # | 位置 | 形态 | 判级 | 备注 |
|---|---|---|---|---|
| 1 | `qspi_ls.c:975` | `calib_combo = 1U` 位置耦合 | **P0 —— 本次已修** | 即本 bug |
| 2 | `qspi_ls.c:1049` `= (uint8_t)pick + 1U;` | +1 偏置：combo **值** ↔ 表**位置** | P2（设计耦合） | 值取自实测扫描、自洽，**当前无害**；但它是"1..8 = 索引+1"约定的**定义处**，表一改语义就跟着漂 |
| 3 | `qspi_ls.c:48` 注释 | 上述约定的文档 | P2 | 本轮已同步更新（含 `0xFE`） |
| 4 | ~~旧 `:1077`~~ | 孪生点位耦合 | 已由 `75ff6a2a531` 移除 | 见 8.1 |
| 5 | `spi_dw.c:139` | 同族分支写**参考值** `0` 而非索引 | — **正例** | 不受本类缺陷影响；`spi_dw` 无 `calib_combo`，`delay_arr[cs]` 直存真值 |
| 6 | `spi_dw.c:143/:206` | `delay 0` 既作合法数据值、又参与"未校准"判定 | P2 观察 | 与 `0xFE` 哨兵相比属另一类**值重载**；`spi_dw.c` 在**写域外**，仅登记 |

### 8.6 回归护栏建议（任务项 5；**未加**，等裁定）

建议在 `qspi_flash_test` 的 `qspi_flash_calib` 套件补一项：**"freq < 校准时钟 → 之后器件仍可读"**，
且**必须同时断言两条**：
1. 该频率下 `qspi_timing_calibration_status(dev) == QSPI_TIMING_CALIB_UNUSED`（证明走了预期分支）；
2. 该频率下 RDID 返回**有效** ID（证明捕获是 REF，而非某个表项）。

**只断言 (2) 不够** —— 若哪天哨兵又被改成某个"恰好可用"的表项，(2) 仍会 PASS。
**理由**：本缺陷的本质是**语义漂移**而非"值不对"，故护栏要钉**语义**（状态 + 可读），
**不能**钉 `calib_combo` 的**数值** —— 钉数值等于再造一个位置耦合。
特别**不建议**把 `calib_combo == 0xFE` 写进用例：那会把驱动内部编码固化进测试，
且 `calib_combo` 不在任何公开头文件中（属私有实现细节）。

### 8.7 上板怎么验（确切命令 + 预期锚点）

```bash
cd D:/zephyr4_0/810_driver_test/projects/qspi_flash_test/tests
# 驱动 .c 改动 ⇒ 必须删 build 全量重建（KNOWN_ISSUES #9 / #37 / #55：-p auto 不吃源码外缓存）
rm -rf case_default/build case_no_calib/build
bash case_default/test.sh -g
bash case_no_calib/test.sh -g
```

| 锚点（`case_default/serial_TGT.log`） | 修复前 | 修复后**预期** |
|---|---|---|
| `set_freq_below_min_rejected` 后那次 after 钩子的 `hw config` | `cap_neg=1 cap_dly=0 (calib_combo=0x01)`（前一轮 `:86`） | `cap_neg=0 cap_dly=0 (calib_combo=0xfe)` |
| `set_freq_boundary_6m25_accepted` | `…set=0,reinit=-19` + 19× `invalid jedec id 00 00 00` | `…set=0,reinit=0`；**无** `invalid jedec id` |
| `set_freq_zero_silent` | `reinit=-19` | `reinit=0` |
| `erase_sector_all_ff` / `write_read_len1/255/256/4096/8192` / `raw_read_1_1_1_ok` / `stg_read_chunked_over_4096` | FAIL（差异 239~7960 B） | PASS（差异 0） |
| 总计 | 14/26（past_end 修后 16/26） | **26/26**，两 case 均 `PROJECT EXECUTION SUCCESSFUL` |

**为什么必须上板**：除 §5 已列理由外，本轮**补一条**——本改动**未经任何编译**验证。
`BUILD_ASSERT` 与新增分支的编译正确性只有真机构建才能确认（见 §9 未定项）。

### 8.8 本次未做（边界声明）

- 未构建 / 未烧录 / 未开 DebugServer / 未碰串口 —— **无任何硬件动作**。
- 未改 `qspi_flash_test.c` / `spi_nor_multi_dev.c` / `spi_dw.c`（写域外）。
- 未新增回归用例（8.6，等裁定）。
- **未放宽任何用例判定口径**（项目纪律：判清前不得放宽断言）。

### 8.9 新增未定项（本轮产生）

1. **`BUILD_ASSERT` 是否保留**（8.2 偏离项，3 行）。
2. **哨兵粘性**（8.4 末段）—— 一般性隐患，本器件上经日志反证**无害**（REF 在 50 MHz 窗内）；
   是否顺带根治（让低于校准时钟的分支不声称已校准）属独立设计改动。
3. **本轮修复未经编译**：无构建即无法排除拼写/类型错误，也未经上板证伪。
