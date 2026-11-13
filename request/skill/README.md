# 文档补充 / 修改提案与结果记录

本目录放两类内容：

- **修改提案**：对现有文档的补充/修改建议，写清目标文件、原文位置、建议文本、证据、状态。
- **结果/定性记录**：一次实测的结果、根因定性、可复用教训；测试结论以
  `projects/<工程>/tests/COVERAGE.md` 为唯一源，结果快照链接到报告目录。

实际可复用的 skill 文件放在 workspace 根的 [`skill/`](../../../skill/README.md)。

## 规则

1. 提案必须写清：**目标文件 + 原文位置 + 建议文本 + 证据 + 状态**；结果记录写清
   **日期 + 工程/用例 + 判据 + 证据路径 + 状态**。
2. 状态词表：`未应用` / `已应用` / `已记录` / `已定性` / `已归档` / `已修复，未提交`。
   默认 `未应用`，由用户决定是否合并；结果记录用后四种。
3. 不复制原文档全文，只引用需要改动的行，避免漂移；结果快照只保留判据与证据。
4. 分类按主题建目录（如 `spi_test/`），文件名 `<主题>_<内容>.md`；目标列填工程/文档对象。

## 索引

| 记录 | 对象 / 目标文档 | 状态 |
|---|---|---|
| [`ENVIRONMENTS/ENVIRONMENTS_host_qsh_wsl2.md`](ENVIRONMENTS/ENVIRONMENTS_host_qsh_wsl2.md) | `810_driver_test/.claude/ENVIRONMENTS.md` | 未应用 |
| [`qsh-debug/ref-host-wsl2_proposal.md`](qsh-debug/ref-host-wsl2_proposal.md) | `810_driver_test/.claude/lib/qsh/skills/qsh-debug/` | 未应用 |
| [`spi_test/spif_loopback_decision.md`](spi_test/spif_loopback_decision.md) | `projects/spi_test` 回环用例归属 | 已归档 |
| [`spi_test/spif_loopback_result.md`](spi_test/spif_loopback_result.md) | `projects/spif_test` 回环/过滤矩阵结果 | 已记录（24/24，见报告） |
| [`spi_test/spif_cmd_dummy_slot.md`](spi_test/spif_cmd_dummy_slot.md) | `projects/spif_test` `cmd_idx_accessors` 槽位定性 | 已定性 |
| [`spi_test/spi_flash_test_result.md`](spi_test/spi_flash_test_result.md) | `projects/spi_test` 本轮结果 | 已记录（29/29） |
| [`spi_test/spi_dw_rdsr_wren_word_size.md`](spi_test/spi_dw_rdsr_wren_word_size.md) | `zephyr/drivers/spi/spi_dw.c` | 已修复，未提交 |

## 相关实际文件

- 可复用流程 skill：[`skill/wsl-zephyr-dev/SKILL.md`](../../../skill/wsl-zephyr-dev/SKILL.md)
- 可复用 SPIF 测试 skill：[`skill/qsh-spif-test/SKILL.md`](../../../skill/qsh-spif-test/SKILL.md)
- 宿主差异参考：[`skill/qsh-debug/ref-host-wsl2.md`](../../../skill/qsh-debug/ref-host-wsl2.md)
- 本次适配证据：`810_driver_test/request/wsl_environment/wsl_environment_ask/05_verification/`
