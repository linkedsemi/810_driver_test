# flash（soc_flash_ls）测试覆盖矩阵

> P1a 建档（2026-09-17）：本工程此前**无 COVERAGE.md**，按 `SPEC_REFERENCE.md` §6.5.3 模板新建。

- 驱动栈: `zephyr/drivers/flash/soc_flash_ls.c`（`linkedsemi_ls_flash_controller`）
- 器件: `flash1: flash@8000000`（DT `reg = <0x8000000 DT_SIZE_M(4)>`，`erase-block-size=4096`，
  `write-block-size=4`），控制器 `qspi1@40000000`；内存映射基址 `0x08000000`
- 工程: `810_driver_test/projects/flash_test`
- 类型: **单核**（只有 `cpu1_flash_test/`）→ board `lsqsh_evb@1os/lsqsh/cpu1`，cpu1 入口 `0x10000000`
- 外部路径: **无**（板载 QSPI boot flash，零接线）
- 套件: `flash`（`ZTEST_SUITE(flash, NULL, flash_test_setup, NULL, NULL, flash_test_teardown)`；
  setup/teardown 为**套件级**，`fa` 在 setup 打开、teardown 关闭）
- 接口: `flash_area_*`（分区层）+ `flash_*`（设备层：`flash_read_jedec_id` / `flash_sfdp_read`
  / `flash_get_page_info_by_offs` / `flash_get_parameters`）

## 关键地址事实（用户裁定 S-5）

**boot flash 物理容量 = 4 MiB。** 板级 1os overlay 的分区表排到 32 MiB，超出的部分
是**另一颗 flash 的陈旧信息**，用户裁定不追。⇒ **新增用例的地址一律以 4 MiB 为界。**

| 分区 | offset | size | 末地址 | 在 4 MiB 内? |
|---|---|---|---|---|
| `sbl` | 0x0 | 56 KB | 0xE000 | ✅ |
| `sbl_env` | 0xE000 | 8 KB | 0x10000 | ✅ |
| `a_sec_image` | 0x10000 | 448 KB（2026-10-29 缩小） | 0x80000 | ✅（含 cpu1 镜像 0x10000..0x51000） |
| **`test_scratch`** | **0x80000** | **512 KB** | **0x100000** | ✅ **写/擦目标（2026-10-29 新增）** |
| `a_app_image` | 0x100000 | 12 MiB | 0xD00000 | ❌ 超出（实际只放 cpu2 镜像 0x100000..0x13F100） |
| `a_rofs` | 0xD00000 | 2 MiB | 0xF00000 | ❌ |
| `rwfs` | 0xF00000 | 2 MiB+64 KB | 0x1210000 | ❌ |
| `b_*` | 0x1110000+ | … | 0x2000000 | ❌ |

**写/擦目标 = `test_scratch`（0x80000..0x100000，512 KB）**，位于 cpu1 镜像末尾
（0x51000）与 cpu2 镜像起点（0x100000）之间；两端都不碰镜像。分区由
`cpu1_flash_test/app.overlay` 定义（`a_sec_image` 同步缩到 0x80000，消除重叠）。
512 KB 同时满足 `test_flash_area_info` 的 `fa_size >= 512 KB` 断言。

## 覆盖矩阵（套件 `flash`，10 项）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| test_flash_read_write_basic | `flash_ls_write`（分页循环）→ `flash_ls_read` | 功能通路 | 256B 写回读逐字节相等 | flash_area API 契约 | ☐ |
| test_flash_erase_all_ff | `flash_ls_erase`（4K 对齐校验 + 扇区擦除）→ read | 功能通路 | **先写 0x00 512B（回读确认生效）→ 擦 4096 → 跨扇区 16×256B 逐字节全 0xFF** | NOR 擦除语义（可证伪） | ☐ |
| test_flash_write_read_multiple_patterns | 同上，5 种 pattern 循环 | 功能通路 | 5 种 pattern 各自逐字节相等 | 同上 | ☐ |
| test_flash_page_boundary | `flash_ls_write` 的 `MIN(FLASH_PAGE_SIZE - off%PAGE, size)` 跨页分支 | 边界长度 | 页边界（4096−128 起 256B）跨页写回读相等 | `soc_flash_ls.c` 分页逻辑 | ☐ |
| test_flash_erase_size_boundary | `flash_ls_erase` 的 `offset/size % KB(4)` 校验分支 | 异常参数 | 擦 100B → 非 0 | `soc_flash_ls.c:527-530` | ☐ |
| test_flash_area_info | `fa->fa_size`（分区几何） | 功能通路 | size ≥ 512 KB | flash_area 契约 | ☐ |
| test_flash_write_out_of_bounds | `flash_area_write(fa, size, …)` 越界 | 异常参数 | 返回非 0 | flash_area 契约 | ☐ |
| **test_flash_jedec_id** | `flash_ls_read_jedec_id`（`hal_flashx_read_id`）+ `flash_ls_sfdp_read`（`hal_flashx_read_sfdp`）—— **两函数此前零覆盖** | 功能通路 | `flash_read_jedec_id` == 0 且 ID 非全 0x00/全 0xFF；`flash_sfdp_read(0,4B)` == 0 且头 4 字节 == ASCII `"SFDP"`（JESD216） | JESD216 标准 + 驱动源码 `:673-710` | ⏳ |
| **test_flash_device_bounds** | `flash_ls_get_parameters`（`write_block_size`）+ `flash_ls_layout`（`DT_REG_SIZE/FLASH_SECTOR_SIZE` 页表） | 边界长度 | `write_block_size == 4`；末页 `start_offset+size == 4 MiB`（等效容量断言）；`flash_get_page_info_by_offs(4 MiB)` → `-EINVAL` | DT `flash1` reg/属性 + `soc_flash_ls.c:762-774` | ⏳ |
| **test_flash_partition_bounds** | `flash_ls_read`（分区首/末 16B 边界读）+ 分区几何 | 边界长度 | `sbl`/`sbl_env`/`a_sec_image` 三个**落在 4 MiB 内**的分区：`fa_size == DT`、`fa_off+fa_size ≤ 4 MiB`、首 16B 与末 16B（`size-16`）读均 == 0；**且 6 段采样不全为 0x00**（读通路恒 0 的签名） | 板级 1os overlay 分区表 + `S-5` + `soc_flash_ls.c:597-617` | ⏳ |

> 状态：`☐` = 既有项（历史未回填勾选）；`⏳` = **P1a 新增，仅编译通过，待 P6 首次实测**。

## P1a 新增项说明（2026-09-17，只生成+编译通过，未烧录）

新增 3 项：`test_flash_jedec_id` / `test_flash_device_bounds` / `test_flash_partition_bounds`。
既有 7 项一行未改；唯一对既有文件的追加式改动是 `flash_test.c` 增加头部 include
（`<zephyr/drivers/flash.h>`、`ztest_glue.h`）与 **1 处新增配置项**
`CONFIG_FLASH_JESD216_API=y`（`prj.conf` 追加；否则 `flash_read_jedec_id` /
`flash_sfdp_read` API 与驱动对应分支根本不编入）。

### 跳过项（P1a）

| 候选 | 跳过类型 | 原因 |
|---|---|---|
| ~~**partition_boundary（分区边界读写）**~~ | ~~地址约束下无安全可写区~~ → **2026-10-29 已解除** | 原缺口：4 MiB 内四个分区全是固件镜像，唯一可写 `rwfs` 在 15 MiB（超芯片）。现由 `test_scratch`（0x80000..0x100000）覆盖：它在 4 MiB 内、不碰任何镜像。 |
| **data_retention（写入 → 软复位 → 回读）** | 流程不支持 | ①需在 RAM-load（GDB 载入 SRAM）流程内做复位：复位后固件不会从测试镜像重启，无法在同一个 ztest 会话内"回读"；②即使复位，也需一处**可写且掉电保持**的区域，而 4 MiB 内无可写区（同上）。⇒ 不可在现有 harness 内做出确定性判定。 |
| **erase_chip_scope（擦除范围边界：64K/32K/4K 分支选择）** | 地址约束下无安全可写区 + 判定非确定 | `flash_ls_erase` 的 64K/32K/4K 分支选择（`:539-550`）需要**真实擦写**才能从回读判定走了哪条分支；4 MiB 内无可写区（同上）。**非对齐擦除的拒绝路径已由既有 `test_flash_erase_size_boundary` 覆盖。** |
| **XIP 一致性**（同一偏移 `flash_area_read` vs `0x08000000+off` 直接读） | 需确认运行模式 | 本工程以 **RAM-load** 方式运行（`zephyr,flash = &sram_ro`），`0x08000000` 处的内存映射读是否与控制器读一致，取决于 XIP 使能与 QSPI 控制器状态，**在 RAM-load 模式下无法确定**；且该验证与 P2 的 `qspi_flash_test`（`qspi2` 独立栈）范围重叠 ⇒ 留给 P2/P6 裁定。 |

## 修订（2026-09-18，测试有效性审计 A 组 P0-5）

### `test_flash_erase_all_ff` —— 消除 no-op 擦除假覆盖
- **原状**：setup（`:35`）已擦同一 4K 区，本项跑在第 3 位、前两项不写 flash ⇒ 擦的是
  **本来就是全 0xFF** 的区域，**no-op 擦除照样 PASS**；且只断言前 256B，"擦 256B"与
  "擦 4096B"不可区分。
- **改**：先 `flash_area_write` 写 0x00 填 512B → **回读确认写入生效** → `flash_area_erase`
  4096B → 跨扇区（16 × 256B）逐字节断言全 0xFF。
- **驱动正确时必过 / 典型失效时必不过**：擦除生效 ⇒ 全部 0xFF 必过；`flash_ls_erase`
  no-op / 只擦 256B / 擦错地址（且失败被 `flash_ls_read` 恒 return 0 掩盖）⇒ 回读出现
  非 0xFF 字节必不过。前置"回读 == 0x00"另拦"写静默失败使区域仍为 FF、擦除断言退化"。
- **地址（2026-10-29 更新）**：本项已改落在 `test_scratch`（0x80000，4 MiB 内），
  不再依赖未决的 15 MiB `rwfs`；写入必然落在真实器件地址域内。

### `test_flash_partition_bounds` —— 断言 head/tail 判别力
- **原状**：唯一数据证据 `head0/tail0` **只经 `ZT_INFO` 打印、从不断言**；断言只有
  `ret==0`，而 `flash_ls_read` **丢弃 HAL 返回值并无条件 `return 0`**
  （`soc_flash_ls.c:597-617`）⇒ QSPI 读通路全坏（读出全 0 / 全错）本项仍 PASS。
- **改**：收集 3 分区 × (head+tail) 共 6 段 16B，断言**不全为 0x00**。
- **驱动正确时必过 / 典型失效时必不过**：真实 flash 读回的固件镜像不可能 96 字节全 0
  ⇒ 必过；读通路恒 0（DMA/控制器未回数据）⇒ 必不过。
- **故意未做**（如实标注，非疏漏）：**没有**断言 head/tail "≠全 0xFF"。因为
  `sbl`/`sbl_env`/`a_sec_image` 的**烧录状态未知**（板卡走 GDB RAM-load，flash 内容是否
  已编程未经硬件侧确认）——若三分区恰好空白/已擦除，"非 FF"断言会造出**假 FAIL**，
  比空壳更糟（审计纪律：不编造无依据断言）。读通路"非全 FF"的判别力已由
  `test_flash_jedec_id`（SFDP 头 == `"SFDP"` + JEDEC ID 非全 0/全 F）独立覆盖。

## 记录（2026-09-18，只记录不改）

> **2026-10-29 更新**：S-5 已落地。写/擦目标从 15 MiB `rwfs` 改为 4 MiB 内的
> `test_scratch`（0x80000..0x100000），由项目 overlay 定义；`a_sec_image` 同步缩到
> 0x80000 消除重叠。原记录保留如下作为历史。

1. **地址域整体越界（S-5，未决）**：本工程所有写/擦落在 `rwfs` = **15 MiB**
   （`0xf00000`），而 DTS 声明 `flash1: flash@8000000` 只有 **4 MiB**
   （`DT_SIZE_M(4)`）。文件头"绝不碰固件区"的承诺**无代码支撑**（约束靠分区表而非运行时
   校验）。审计标注"判不了，需要硬件侧确认 die 实际容量与地址解码位宽"。
   **本次只记录，未改任何地址**（用户已裁定 S-5 属敏感区）。
   - 间接证据：`2026-09-17-3` 回归中 `test_flash_read_write_basic`（rwfs 0xF00000 处
     256B 写→回读→memcmp 相等）**PASS** ⇒ 该地址的读写至少"看起来"生效；但这既可能是
     真实 15 MiB 器件，也可能是地址解码回绕，**均未证实**。
2. **`test_flash_erase_size_boundary` 断言口径宽**：`zassert_not_equal(ret, 0)` 只要求
   "非 0"，未绑定 `-EINVAL`；驱动 `soc_flash_ls.c:527-530` 实际返回 `-EINVAL`。
   属 P2（口径不严），**本次未改**（不在 A 组范围）。
3. **注释陈旧**：文件头（`:4-9`）写 "只擦写 rwfs2 分区"，实际代码用
   `FIXED_PARTITION_ID(rwfs_partition)`（分区 label = `rwfs`）——`rwfs2` 是旧名。
   P3 文档失真，**未改**（避免与 `run_test.py` 同批改动冲突）。
4. **COVERAGE 状态列过期**：既有 7 项仍标 `☐ 历史未回填勾选`，但
   `2026-09-17-1/-3` 报告显示 10/10 PASS。属 P3，**未回填**（状态列口径需全局统一）。
---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
> 来源标签：`handcalc`(手算) `spec`(外部规范/寄存器或 DT 定义) `golden`(黄金向量)
> `crosscheck`(跨来源交叉) `contract`(API 契约值) `driversrc`(转录自被测驱动实现，
> **只证"没变"不证"仍对"**) `samecode`(同源复算，**非独立**) `!none`(无确定预期)。

```expect
# 测试项 | 断言 | 来源标签:说明
test_flash_area_info | size in [524288,17179869184] | spec:flash_area 契约 size ≥ 512KB（★无 INFO 通道 ⇒ NO_ACTUAL）
test_flash_erase_size_boundary | ret != 0 | contract:非擦除块大小对齐必须拒绝（★无 INFO 通道 ⇒ NO_ACTUAL）
test_flash_write_out_of_bounds | ret != 0 | contract:越界写必须拒绝（★无 INFO 通道 ⇒ NO_ACTUAL）
test_flash_read_write_basic | !none | !none:256B 写回读 memcmp
test_flash_erase_all_ff | !none | !none:先写后擦、跨扇区 16×256B 全 0xFF 的合取
test_flash_write_read_multiple_patterns | !none | !none:5 种 pattern 的 memcmp 合取
test_flash_page_boundary | !none | !none:跨页 256B 写回读 memcmp
```
