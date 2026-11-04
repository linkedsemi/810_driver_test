# tmp/ —— 临时工作区

## 定位

一次性探针、A/B 对比线束、离线重放输出、临时脚本与日志。
**内容可随时删除，不作证据，不入库**（`.gitignore`：`tmp/*`，仅本文件例外）。

## 目录约定

- `tmp/<主题>-<YYYYMMDD>/`：一个任务一个子目录。
- `tmp/replay/<会话>/`：`regression/replay.py` 的默认输出位置。
- 子目录内建议写明：负责人 / 日期 / 计划删除日期。

## 允许

- 一次性 Python/bash 探针、A/B 线束、重放输出、临时串口/构建日志。
- 需要保留的构建产物放 `integrated/`（独立 gitignored 工作区），不要长期堆在 `tmp/`。

## 禁止

- 不把 `tmp/` 当报告归档：单模块报告进 `reports/module/<date>-<idx>/`，全量进 `reports/<date>-<idx>/`。
- 不把 `tmp/` 里的路径写进 `KNOWN_ISSUES.md` / `OPEN_ITEMS.md` / `COVERAGE.md` / `.claude/gotchas/INBOX.md` 作为证据。
- 不提交大二进制、完整 `build/` 目录、原始串口大文件。

## 合并回正式代码

- 可复用脚本 → `common/`、`regression/`、`scripts/`。
- 可复用测试 → `projects/<p>/tests/`。
- 可复用知识 → `KNOWN_ISSUES.md` / skill 的 `ref-*.md` / `debug-knowledge`。
- 证据 → 把结论内联到问题条目，附 `810_driver_test@<12位sha>`；不要留报告路径。

## 清理

- 每个任务收尾清理一次，至少每月一次。
- **先盘点再定向删除**，不要整目录 `rm -rf tmp/`（可能混有 replay/build）：
  ```bash
  find tmp -maxdepth 2 -type d | sort
  du -sh tmp/* 2>/dev/null | sort -h
  find tmp -maxdepth 2 -type f -mtime +30 | head
  ```
