---
name: skill-registry
description: 外部插件级 skill 与 MCP 能力目录（嵌入式开发相关，无法并进本地 .claude/skills 的那部分）：superpowers（流程方法论）、claude-mem（跨会话记忆/MCP）、skill-creator（skill 制作）、code-review（PR 审查）、excalidraw-diagram（可视化）等。每条含名称/用途/触发词/前置依赖/新环境启用法。本机可用的外部能力查本表；到了新环境需要某项时，Claude 先读本表获取其来源与启用方式，再自行调取。本地自研 skill 清单见 qsh-driver-test。
---

# 外部能力目录（插件级 skill + MCP，无法并进本地 skill）

## 用途与用法

**本地库**（`810_driver_test/.claude/skills/`，qsh-* + embedded-debug）是自研、随工程携带、
纯文本、跨机器复制即用的 skill。**外部能力**（插件/市场安装、MCP 服务）依赖安装机制与
服务端，无法"归档"进本地库——但嵌入式开发中高频用到。本 skill 就是它们的**目录 + 取用
指南**：

- **在本机**：直接按触发词调用（它们已随插件/MCP 装好，以 `插件名:skill` 或 `/skill` 暴露）。
- **在新环境**：某任务需要某项外部能力而它不在 → 读本表拿到**来源与启用方式**，按 §4
  装好再调；装不上/断网时，表中"用途/触发"足以让 agent 按语义自行完成等效工作。

## 目录（按嵌入式开发价值排序）

| 能力 | 来源（插件/市场） | 用途（嵌入式开发视角） | 触发/调用 | 新环境启用 |
|---|---|---|---|---|
| superpowers 流程方法论 | 插件 `superpowers`（市场 superpowers-marketplace） | 调试/设计/执行的纪律化流程 | `/superpowers:systematic-debugging`（定位 bug 前）；`/superpowers:brainstorming`（设计前）；`/superpowers:writing-plans`、`/superpowers:executing-plans`；`/superpowers:verification-before-completion`（声称完成前自证） | 插件市场安装，见 §4 |
| claude-mem 记忆 + MCP | 插件 `claude-mem`（市场 thedotmack）+ MCP 服务 | 跨会话记住/检索历史解决过的坑（mem-search）；知识库问答 | `/claude-mem:mem-search`；MCP `claude-mem search/timeline/...`；启动上下文注入 | 插件 + 其 MCP 服务端需连 cmem 数据，见 §4 |
| skill-creator | 插件 `skill-creator`（市场 claude-plugins-official） | 新建/改进本地 skill、评估触发效果 | `/skill-creator:skill-creator` | 插件安装，见 §4 |
| code-review | 插件 `code-review`（市场 claude-plugins-official） | PR/改动审查 | `/code-review:code-review` | 插件安装，见 §4 |
| excalidraw-diagram | **个人级 skill**（`~/.claude/skills/excalidraw-diagram`，非插件） | 架构/流程/时序可视化图 | `/excalidraw-diagram` | **不是插件**——从旧环境把整个目录复制到新环境 `~/.claude/skills/` 即用 |
| 内置 bundled | Claude Code 自带 | dataviz（图表）、security-review、update-config 等 | `/dataviz` 等 | 随版本自带，无需安装 |

## 各能力要点（嵌入式用法速记）

### superpowers（推荐先学 systematic-debugging）
- **systematic-debugging**：遇到 bug/测试失败/行为异常、**在提修复方案之前**走系统性根因
  定位（复现→假设→验证→修因不修表）。与本地 `embedded-debug` 方法论互补：它管"流程
  纪律"，embedded-debug 管"嵌入式分层知识"。
- **brainstorming**：做新特性/新工程设计前澄清意图 + 出方案 + 批准门禁。
- **writing-plans / executing-plans / verification-before-completion**：多步任务写执行计划、
  分步执行、声称完成前跑验证命令自证（evidence before assertions）。与
  根仓库 `SPEC_REFERENCE.md` §7「Plan 管理规范」配合使用。

### claude-mem（本机的跨会话记忆）
- 记住"这个坑上次怎么解决的"，配 `debug-issues-quickref`、各 skill 沉淀联动。
- MCP 工具：`search` / `timeline` / `get_observations`（分层取，勿一次拉全量）。

### skill-creator
- 生成/改进 `.claude/skills/<name>/SKILL.md`；本地库新增 skill 时可用它校验格式与 description。

## §4 新环境启用方式（插件类通用）

```text
1. 入口：Claude Code `/plugin` 命令，或 IDE 的插件/市场面板
2. 已知市场与包（按需 add/install，联网受限机器可能需离线兜底）：
   - superpowers     → market: superpowers-marketplace   repo: obra/superpowers-marketplace
   - claude-mem      → market: thedotmack                repo: thedotmack/claude-mem（含 MCP 服务）
   - code-review     → market: claude-plugins-official（官方）
   - skill-creator   → market: claude-plugins-official（官方）
3. 装完：本会话需触发一次工具调用或重启会话刷新 skill 列表，再 `/名字` 调用
4. 断网/装不上的兜底：按本表"用途/触发"描述，用本地已读到的语义手动完成等效步骤，
   或从旧环境拷贝同版本插件目录（插件不独立，拷贝 skills/ 子目录无效，需整个插件随市场装）
```

## 810 项目内置工具能力（tools/）

非插件、随 810_driver_test 工程携带的可执行能力，登记在此统一查取。其中
**`.claude/tools/gdb/` 为通用工具**（GDB 前缀、DebugServer 地址均为可配项，见 env-adapter §4.1），
随 `.claude/` 整目录一起迁移；其余工程根 `tools/` 内容（如 flash_program）为工程专用，不随库走。

| 能力 | 来源 | 用途 | 触发/调用 | 启用 |
|---|---|---|---|---|
| gdb_proxy.py | `.claude/tools/gdb/` | 统一 GDB CLI 中间件，JSON 输出，三后端自动降级（MCP→pygdbmi→batch） | `python .claude/tools/gdb/gdb_proxy.py <cmd>`（connect/load/break/continue/read-mem/read-reg/backtrace/exec/status/disconnect） | 零依赖（batch 兜底）；`pip install pygdbmi` 启用交互模式 |
| gdb_mcp_server.py | `.claude/tools/gdb/` | GDB MCP Server，暴露 `gdb_*` 原生工具 | 注册后 agent 直接调 `gdb_connect`/`gdb_read_memory` 等 | `pip install pygdbmi mcp`；迁移时 env-adapter §4.2 自动注册 `.mcp.json`，手动注册见 `.claude/tools/gdb/README.md` |

**gdb 工具新环境获取**：随 `.claude/` 整目录迁移后自然可用（`.claude/tools/gdb/` 随迁）；
若目标环境缺失 → 从模板工程 `.claude/tools/gdb/` 整目录复制，或按 `.claude/tools/gdb/README.md`
的接口说明重新生成。复制后按 env-adapter §4.1 调整 `gdb_proxy.py` 顶部默认常量。

## 维护

- 新增/移除某项外部能力时同步更新本表（来源、触发、启用法三栏必填）。
- 本表是纯文本 skill，随本地库跨环境携带——**这正是外部能力"目录化"而非"归档化"的意义**。

## 链路
> **独立**（外部插件目录） · **被查**← 需要外部能力时
