---
name: skill-orchestrator
description: Skill 库闭环路由 manifest。定义所有 skill 之间的显式转换关系、任务生命周期、runner 强制读取规则。runner 执行策略前必须先读本文件获取 skill 链，执行完一个 skill 后按本文件决定下一步。不直接执行任务，只管"谁该调谁"。
---

# Skill 闭环路由 Manifest

> **定位**：本文件是 skill 库的"总调度图"——不执行任何具体任务，
> 只定义 skill 之间的转换规则。runner 每个策略执行前**必须 Read 本文件**。
>
> **环境范围**：§2 的链定义是 810/QSH 环境实例（引用 qsh-* skill）。
> 迁移到新环境时本文件随库休眠保留；新环境由 env-adapter §4.2 指导
> 生成等价 manifest（链骨架 STRATEGY_* 通用，替换 skill 名即可）。

## 1. 任务生命周期（闭环）

```text
用户任务
  │
  ├─ "新建/实现/编写/迁移" ──→ STRATEGY_DEV
  ├─ "调试/失败/不跑/异常" ──→ STRATEGY_DEBUG
  ├─ "回归/全量/批量测试" ──→ STRATEGY_TEST
  ├─ "为什么/分析/根因"   ──→ STRATEGY_ANALYZE
  ├─ "验证/检查/validate" ──→ STRATEGY_VALIDATE
  └─ 混合/不确定          ──→ 拆分后分别路由
```

## 2. 显式转换规则表

> ★ = 强制自动转（不可跳过）
> ☆ = 条件转（满足条件才转）
> — = 结束/回到调用方

### STRATEGY_DEV（新建工程 / 修改驱动）

| 步骤 | skill | 完成后转 | 条件 |
|------|-------|---------|------|
| 1 | qsh-test-gen | ★ qsh-test-validate | 生成完成 |
| 2 | qsh-test-validate | ☆ 回 qsh-test-gen | 存在 FAIL 项 |
| 2 | qsh-test-validate | ★ qsh-driver-review | 全部 PASS（涉及驱动代码时） |
| 2 | qsh-test-validate | ★ qsh-test-run | 全部 PASS（仅测试工程，不涉及驱动） |
| 3 | qsh-driver-review | ☆ 回代码修改 | 存在 P0 发现 |
| 3 | qsh-driver-review | ★ qsh-test-run | 全部 PASS / 只有 P1/P2 |
| 4 | qsh-test-run | ★ qsh-test-compare | 首次采集完成 |
| 5 | qsh-test-compare | ★ 坑沉淀决策 | 分析完成 |

### STRATEGY_DEBUG（调试）

| 步骤 | skill | 完成后转 | 条件 |
|------|-------|---------|------|
| 1 | qsh-debug | — 回调用方 | 问题解决 |
| 1 | qsh-debug | ☆ debug-knowledge §写入 | 发现领域可复用招式 |
| 1 | qsh-debug | ☆ KNOWN_ISSUES | 发现通用环境坑 |
| 1 | qsh-debug | — 升级 | 3 轮未闭环 |

### STRATEGY_TEST（回归）

| 步骤 | skill | 完成后转 | 条件 |
|------|-------|---------|------|
| 1 | qsh-test-run | ★ qsh-test-compare | 采集完成 |
| 2 | qsh-test-compare | ★ 坑沉淀决策 | 分析完成 |

### STRATEGY_ANALYZE（分析）

| 步骤 | skill | 完成后转 | 条件 |
|------|-------|---------|------|
| 1 | debug-knowledge §装载 | — | 查到直接用 |
| 1 | debug-knowledge | ☆ embedded-debug | 没查到，用方法学现推 |
| 2 | embedded-debug | ☆ debug-knowledge §写入 | 推出可复用结论 |

### 坑沉淀决策（所有策略共享）

| 坑类型 | 归处 | 执行者 |
|--------|------|--------|
| 领域可复用调试法 | debug-knowledge §写入 | 当前 agent |
| 通用环境/工具链坑 | 写 `.claude/gotchas/INBOX.md` → keeper 沉淀到 KNOWN_ISSUES.md + 对应 skill 速查 | 写：当前 agent / 沉淀：keeper |
| 模板缺陷 | 写 INBOX → keeper 沉淀到 qsh-test-gen 模板规则 + qsh-test-validate 检查项 | 写：当前 agent / 沉淀：keeper |
| 文档漂移 | keeper 文档一致性维护 | keeper |
| 一次性 bug | 问题总结 .md 存档 | 当前 agent（不入知识库） |

## 3. Supervisor 强制读取规则

runner 启动任何策略前，**必须**：

```
Step 1: Read 本文件（skill-orchestrator/SKILL.md）
Step 2: 从 §2 查当前策略的 skill 链
Step 3: Read 链中每个 skill 的 SKILL.md（subagent 无 Skill 工具，用 Read 文件路径）
Step 4: 按链顺序执行，每完成一步查本文件决定下一步
Step 5: 不得跳过链中 ★ 标记的环节
```

## 4. 跳过条件（允许不走完整链的情况）

| 条件 | 可跳过的环节 |
|------|------------|
| 用户明确说"只生成不验证" | qsh-test-validate（但需记录跳过） |
| 调试任务不涉及代码修改 | 坑沉淀（确认无新坑即可结束） |
| 纯查询（"这个函数在哪"） | 所有策略，直接回答 |

## 5. 闭环完整性自检

当怀疑链路断裂时，调 `skill-auditor` 做完整性检查。

## 链路
> **路由**→ 所有 skill（定义完整链路 manifest） · **被读**← runner · keeper
