# spif 命令表 DUMMY_CYCLE：槽 5 READ 不可写，槽 6 FAST_READ 正常

- **日期**：2026-10-29
- **状态**：已定性；**测试代码选槽问题，不是驱动缺陷**；未改驱动
- **相关**：`projects/spif_test` `cmd_idx_accessors`

## 1. 原现象

原断言在固定表 **索引 5** 上做 DUMMY 往返：

```c
spif_set_dummy_by_idx(dev, 9, 5);
spif_get_dummy_by_idx(dev, &dummy, 5);
zassert_equal(dummy, 9, ...);
```

实测（2026-10-23 ~ 2026-10-26 多轮）：

```
slot5_raw=0x77, cmd=0x77, raw_dummy=0, get_dummy=0
```

同槽 CMD 可写、EN 正常，只有 `[12:8]` DUMMY 恒 0。

## 2. 复核实验（2026-10-29，实机）

在 `cmd_idx_accessors` 中同时打印槽 5 与槽 6 原始寄存器：

```
INFO cmd_idx_accessors.slot5_read:
  raw=0x77, cmd=0x77, raw_dummy=0, get_dummy=0 (READ has no dummy phase)
INFO cmd_idx_accessors.slot6_fast_read:
  raw=0x90b, cmd=0xb, raw_dummy=0x9, get_dummy=0x9
```

证据：`reports/2026-10-29-1/spif_test_case_default/serial_TGT.log`
（`INFO cmd_idx_accessors.slot5_read` / `.slot6_fast_read`）；权威测试清单见
`projects/spif_test/tests/COVERAGE.md`。

- 槽 5 = `SPIF_READ_CMD`，命令 `0x03`（普通 READ）。SPI NOR 的 READ 命令**没有 dummy 相**，
  该槽 DUMMY 字段硬件固定 0。
- 槽 6 = `SPIF_READ_FAST_CMD`，命令 `0x0b`（FAST READ）。DT 默认 `cmd-fast-read-dummy=8`；
  写 9 后原始寄存器 `0x90b`，回读 9 ⇒ 字段可写、可回读。
- 驱动 `spif_set_dummy_by_idx()` / `spif_get_dummy_by_idx()` 是标准读-改-写，
  同槽 CMD/EN 正常 ⇒ 无掩码、无写址缺陷。

## 3. 结论与处置

- **原断言选错槽位**：在无 dummy 相的 READ 槽上要求 DUMMY 往返，构造上不可满足。
- 处置（已落地）：DUMMY 往返改到槽 6 FAST_READ；槽 5 保留 CMD 往返 + DUMMY 诊断；
  `spif_replace_fixed_dummy()` 也用槽 6；5-bit 掩码测试仍用槽 6。
- **驱动不需要修改**。
- 结果：`cmd_idx_accessors` PASS，`spif_test case_default` 24/24 PASS。

## 4. 复用规则

- 测命令表 DUMMY 字段，必须选**有 dummy 相**的槽：FAST_READ(6)、READ_QUAD_DATA(7)、
  READ_QUAD_ADDR_QUAD_DATA(8) 等。
- 普通 READ(5)、PAGE_PROGRAM/ERASE 等无 dummy 相的槽，DUMMY 位可能固定 0；
  不要用它们做写回读断言。
- 槽位映射见 `reg_spi_filter.h` 的 `enum cmd_table`（0..29）与
  `enum cmd_dummy_table`（同样 0..29，按槽对齐）。
