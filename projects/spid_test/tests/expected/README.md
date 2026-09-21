# spid_test 预期数据

本工程 **无外部黄金向量文件**：判定所需的预期值全部内联在固件断言里，
来源逐项记录在 `tests/COVERAGE.md` 的「预期数据 / 预期来源」两列。

| 类别 | 预期值 | 来源 |
|---|---|---|
| API 错误码契约 | `0` / `-EINVAL` / `-ENOSR` | 驱动源码分支（行号见 COVERAGE.md） |
| 寄存器字段回读 | 与写入值一致（位宽受限字段除外） | 器件寄存器定义（`reg_*.h` 结构体位宽） |
| 结构体/位宽边界 | 见 COVERAGE.md 边界项 | 驱动头文件常量 + 寄存器字段位宽 |

> 实例地址取 DT 节点（tpm_spis1 @0x4000c000 / tpm_spis2 @0x4000e000），不用 sample 里硬编码的 SPID2_BASE。`master_send_tpm_frame` 的 SPI master 实例选择与 CRB 帧格式为待确认项（见 COVERAGE.md 未决项）。
