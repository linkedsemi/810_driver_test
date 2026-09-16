# Skill 库便携说明

> 本目录随 skill 库整目录迁移。`810_driver_test/CLAUDE.md` 是工程级指南，**不随迁移**。
> 🔒 保护：只在 skill 库结构或加载机制变更时修改，业务规则写到对应文档。

## 库成员

### 通用文档

| 文档 | 用途 |
|------|------|
| `CLAUDE.md`（本文件） | 库结构/加载机制 |
| `USER_GUIDE.md` | 新手入口：怎么用/怎么迁移 |
| `TOOLS_AND_SKILLS.md` | skill 索引和调用方式 |
| `DOC_EDIT_RULES.md` | 文档规范（注释规范见 §3.5） |
| `PERSISTENT_RULES.md` | 迁移种子（首次写入全局记忆） |

### Skills（`skills/`，平铺一层）

| skill | 定位 |
|---|---|
| `embedded-debug` | 通用调试方法论（跨硬件） |
| `debug-knowledge` | 调试领域知识库 |
| `skill-registry` | 外部插件/MCP 能力目录 |
| `env-adapter` | 跨环境引导 + 新环境第一个落点 |
| `skill-orchestrator` | 闭环路由 manifest（runner 必读） |
| `skill-auditor` | 完整性自检 + 迁移验证 |

### 810 专用（随库迁移；非 810 环境休眠保留——不删、不加载）

| skill | 定位 |
|---|---|
| `qsh-driver-test` | 810 总入口路由 |
| `qsh-debug` | QSH 调试主入口（含 ref-scons-hal-verify） |
| `qsh-test-gen` | 测试工程/用例生成 |
| `qsh-test-validate` | 测试工程合规验证 |
| `qsh-test-run` | 测试执行（构建/烧录/采集） |
| `qsh-test-compare` | 结果分析/报告/对比 |

### Agents

| agent | 定位 |
|---|---|
| `runner.md` | 自适应监督 agent 通用框架（策略骨架/监控/断点续跑），跨环境复用 |
| `runner-qsh-routing.md` | runner 的 QSH 环境路由插件（810 专用，随库休眠保留）；新环境参照它新建 `runner-<env>-routing.md` |
| `keeper.md` | 文档体系与 token 预算守护 |

### 工具（`.claude/tools/`，随库迁移）

| 目录 | 定位 |
|---|---|
| `.claude/tools/gdb/` | 通用 GDB 调试工具集（gdb_proxy.py + gdb_mcp_server.py + README.md）；能力登记见 skill-registry，用法见其 README.md |

> `.claude/tools/` 随库整目录迁移。工程根 `tools/` 下的其他内容（如 flash_program）为工程专用二进制，不随库迁移。

## 加载机制

- skill 只从三类位置加载：个人级 `~/.claude/skills`、**启动目录** `.claude/skills`、已装插件。
- `.claude/skills` **只认一层** `<名>/SKILL.md`；深层目录不被当 skill。
- 子项目的 skill 只有实际读写该子树文件时才按需载入。
- 插件级 skill 不独立，不能只拷 `skills/` 子目录。

## 持久规则引导

首次工作时读 `PERSISTENT_RULES.md` 写入全局记忆（`~/.claude/projects/<project>/memory/`）。
已有同名记忆则跳过（幂等）。

## 路由速查

| 任务 | 调 |
|---|---|
| 驱动/固件/烧录/串口调试（QSH） | `qsh-debug` |
| 跨硬件方法学 | `embedded-debug` |
| 领域招式查询/沉淀 | `debug-knowledge` |
| 810 驱动测试任务 | `runner` agent 自动路由 |
| 新环境/迁移引导 | `env-adapter` |
| 闭环链路/策略执行前 | `skill-orchestrator` |
| 迁移后验证 | `skill-auditor` |
| 都不命中 | `embedded-debug` + `debug-knowledge` |
