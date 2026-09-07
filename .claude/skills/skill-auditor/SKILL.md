---
name: skill-auditor
description: Skill 库完整性自检 + 迁移后验证。检查所有 skill/agent 文件是否存在、交叉引用是否断裂、闭环链是否完好、硬编码是否可替换。迁移 .claude/ 到新工程后跑一次确认所有链接有效。也可手动调用做定期健康检查。
---

# Skill 库完整性自检（skill-auditor）

> **定位**：不执行具体任务，只做"体检"——检查 skill 库的文件完整性、
> 引用一致性、闭环链完好性。迁移后 / 定期健康检查时调用。

## 1. 完整性检查清单

### A. 文件存在性（必查）

逐一确认以下文件可读：

```text
.claude/CLAUDE.md
.claude/TOOLS_AND_SKILLS.md
.claude/DOC_EDIT_RULES.md
.claude/agents/supervisor.md
.claude/agents/skill-keeper.md
.claude/skills/skill-orchestrator/SKILL.md
.claude/skills/skill-auditor/SKILL.md
.claude/skills/env-adapter/SKILL.md
.claude/skills/embedded-debug/SKILL.md
.claude/skills/debug-knowledge/SKILL.md
.claude/skills/skill-registry/SKILL.md
```

工程专用（810/QSH，迁移时按需替换）：

```text
.claude/skills/qsh-driver-test/SKILL.md
.claude/skills/qsh-debug/SKILL.md
.claude/skills/qsh-test-gen/SKILL.md
.claude/skills/qsh-test-run/SKILL.md
.claude/skills/qsh-test-compare/SKILL.md
.claude/skills/qsh-test-validate/SKILL.md
.claude/skills/debug-knowledge/knowledge/{common,mcu,zephyr,linux,riscv,arm}/README.md
```

### B. 引用完整性（必查）

从所有 `.md` 文件中提取文件路径引用，逐一验证目标存在：

1. **supervisor.md 的 skill 表**：每个"文件路径"列的路径可读
2. **skill-keeper.md 的 skill 表**：同上
3. **各 SKILL.md 的交叉引用**：`qsh-debug` 引 `embedded-debug`、
   `qsh-test-gen` 引 `qsh-test-validate` 等
4. **skill-orchestrator 的转换表**：每个目标 skill 存在

```bash
# 自动化方法：grep 所有 md 中的 .claude/skills/ 路径引用
grep -roh '\.claude/skills/[^ ]*' *.md agents/*.md skills/*/SKILL.md | sort -u
# 逐一检查目标文件是否存在
```

### C. 闭环链完整性（必查）

按 `skill-orchestrator` §2 转换规则表检查：

| 策略 | 链路 | 检查 |
|------|------|------|
| STRATEGY_DEV | gen→validate→run→compare→沉淀 | 4 个 skill 全存在 |
| STRATEGY_DEBUG | debug→(knowledge/KNOWN_ISSUES) | debug + knowledge 存在 |
| STRATEGY_TEST | run→compare→沉淀 | 2 个 skill 存在 |
| 坑沉淀 | knowledge §写入 / KNOWN_ISSUES / skill-keeper | 3 条路径可达 |

### D. 工程绑定检查（迁移时必查）

扫描所有文件中的**硬编码**，标记需要替换的项：

| 硬编码类型 | 扫描关键词 | 替换目标 |
|-----------|-----------|---------|
| Board 名 | `lsqsh_evb` | `<new_board>` |
| 工程名 | `i3c_dualcore_test` / `810_driver_test` | `<new_project>` |
| COM 口 | `COM14` / `COM15` / `COM16` / `COM17` | `<new_ports>` |
| 地址 | `0x10000000` / `0x100c0000` / `0x4002200c` | `<new_addrs>`（如适用） |
| 调试器路径 | `C:/uaer_app/debug_server` | `<debug_server_path>` |
| SoC 架构 | `riscv64-unknown-elf-gdb` | `<toolchain_prefix>` |

```bash
# 自动化扫描
grep -rn "lsqsh_evb\|COM1[4567]\|0x100[0-9a-f]\{6\}\|C:/uaer_app\|riscv64-unknown-elf" \
  .claude/skills/*/SKILL.md .claude/agents/*.md
```

## 2. 迁移后验证流程

```text
Step 1: 执行 §1 完整性检查（A-D）
Step 2: 输出适配报告（哪些要改、改什么）
Step 3: 用户/agent 按报告逐一替换
Step 4: 二次运行 §1 A-C 确认闭环恢复
Step 5: 更新工程 CLAUDE.md（如有）
```

### 适配报告模板

```markdown
# Skill 库迁移适配报告
- 源工程: 810_driver_test
- 目标工程: <new_project>
- 日期: YYYY-MM-DD

## 文件完整性
| 检查项 | 结果 | 说明 |
|--------|------|------|
| 通用 skill（6个） | ✅ 全部存在 | — |
| 工程专用 skill（6个） | ⚠️ 需替换 | qsh-* 系列为 810 专用 |
| agent 文件 | ✅ 存在 | supervisor 需定制策略表 |

## 引用完整性
| 断裂引用 | 位置 | 修复 |
|----------|------|------|
| （无 / 列出具体断裂处） | | |

## 闭环链
| 链路 | 状态 | 说明 |
|------|------|------|
| DEV: gen→validate→run→compare | ⚠️ | qsh-* 需替换为新工程等价 skill |
| DEBUG: debug→knowledge | ✅ | 通用链完好 |
| TEST: run→compare | ⚠️ | qsh-* 需替换 |

## 硬编码替换清单
| 文件 | 行 | 原值 | 替换为 |
|------|---|------|--------|
| qsh-debug/SKILL.md | 34 | lsqsh_evb | <new_board> |
| ... | | | |
```

## 3. 定期健康检查

非迁移场景下，定期调用 §1 A-C 检查：
- 新增 skill 后：确认注册到 TOOLS_AND_SKILLS.md 和 .claude/CLAUDE.md
- 文档大改后：确认引用未断裂
- 坑沉淀后：确认 debug-knowledge 索引更新

## 4. 自动修复建议

对常见问题提供一键修复：

| 问题 | 修复建议 |
|------|---------|
| skill 未注册到 TOOLS_AND_SKILLS.md | 自动生成注册行 |
| SKILL.md 缺"下一步"章节 | 从 skill-orchestrator 转换表生成 |
| debug-knowledge README 未更新 | 自动生成索引行 |
| 硬编码残留 | 列出逐项替换指令 |
