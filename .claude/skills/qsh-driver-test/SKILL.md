---
name: qsh-driver-test
description: 810_driver_test 手动总入口（备选）。正常任务由 supervisor agent 自动路由，本 skill 仅在 supervisor 不可用或需要手动指定 skill 时使用。
---

# QSH 驱动自动化测试（手动总入口）

> **正常流程**：supervisor agent 自动路由，你不需要手动调本 skill。
> **手动场景**：supervisor 不可用、需要绕过 supervisor 直接指定 skill、或调试 supervisor 本身。

## 路由表（手动调用参考）

| 诉求 | 手动调用 |
|------|---------|
| 驱动有问题 / 烧录失败 / 串口没数据 | `qsh-debug` |
| 分层定位 / 断点方法（通用） | `embedded-debug` |
| 构建 / 烧录 / 跑用例 / 全量回归 | `qsh-test-run` |
| 分析结果 / 写报告 / 回归对比 | `qsh-test-compare` |
| 新建测试工程 / 新 case | `qsh-test-gen` |
| 验证合规性 / 回归前预检 | `qsh-test-validate` |
| 调试知识库查询 | `debug-knowledge` |
| 外部插件能力 | `skill-registry` |

## 自动路由（正常路径）

```
用户任务 → CLAUDE.md Supervisor 强制路由规则
         → Agent(subagent_type="supervisor")
         → supervisor 读 task → 选策略 → 调对应 skill
```

详见 `.claude/agents/supervisor.md`。
