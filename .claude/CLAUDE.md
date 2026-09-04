# Skill 库便携说明（随 .claude/ 整目录迁移）

> ⚠️ **保护级别：高** - 本文件是 **skill 库迁移的核心文档**，非必要不修改！
>
> **修改规则**：
> - ✅ 允许：skill 库结构变化（新增/删除 skill、目录调整）
> - ✅ 允许：接线机制、加载机制等重大架构变更
> - ❌ 禁止：单个 skill 的功能更新（应更新对应 skill 自己的 SKILL.md）
> - ❌ 禁止：测试规范、固件编写等业务规则（应更新 810_driver_test/CLAUDE.md）
>
> **修改前必须**：确认变更影响整个 skill 库的便携性和迁移能力
> **建议**：先与团队讨论，避免破坏跨环境兼容性

> 本目录 `810_driver_test/.claude/` 是 **skill 库的迁移单元**：整目录拷到任何嵌入式/zephyr/linux 工程，
> 里面的 skill 即可被发现（放到目标工程根 `.claude/` 下即加载；或参考 `env-adapter` 用 junction 接）。
> `810_driver_test/CLAUDE.md`（上一级）是 810 工程级指南，**不随本目录迁移**——需要驱动测试规范时单独带上。

## 库成员

### 通用规范文档（`.claude/` 根目录）

| 文档 | 定位 | 用途 |
|------|------|------|
| `CLAUDE.md`（本文件） | skill 库便携说明 | 库结构/接线/迁移指南 |
| `TOOLS_AND_SKILLS.md` | skill 清单 | 所有 skill 的索引和调用方式 |
| `DOC_EDIT_RULES.md` | 文档编辑规范 | CLAUDE.md/SKILL.md 编辑规则（通用） |

### Skills（`.claude/skills/`，平铺一层 = 一个 skill 一个目录）

| skill | 定位 | 通用性 | 用途 |
|---|---|---|---|
| `embedded-debug` | 通用调试方法论 | ✅ 通用 | 分层定位/断点/双通道/探针/坑沉淀三分（跨硬件） |
| `debug-knowledge` | 调试领域知识库 | ✅ 通用 | `knowledge/{common,mcu,zephyr,linux,riscv,arm}/` 内容 .md；装载 + 自动填充 |
| `skill-registry` | 外部插件/MCP 能力目录 | ✅ 通用 | superpowers/claude-mem 等启用法 |
| `env-adapter` | 跨环境引导接线 + 通用入口 | ✅ 通用 | 探测环境 → junction 接线 → 路由（**新环境第一个落点**） |
| `agents/supervisor.md` | 自适应任务监督 agent | ✅ 通用 | 策略路由/失败恢复/断点续跑（需按工程域定制） |
| `qsh-driver-test` | 810 总入口路由 | ⚠️ 810 专用 | 810 驱动测试三大类任务先读它 |
| `qsh-debug`（含附 `ref-scons-hal-verify.md`） | QSH 调试主入口 | ⚠️ QSH 专用 | 连接/烧录/串口/速查；scons/HAL 低频验证见附 md |
| `qsh-test-gen` / `qsh-test-run` / `qsh-test-compare` | 810 驱动测试执行链 | ⚠️ 810 专用 | 生成/跑用例/分析 |
| `qsh-test-validate` | 810 测试工程合规验证 | ⚠️ 810 专用 | 检查工程是否符合规范 |

> **Supervisor 优先**：810_driver_test 下的持续性任务由 supervisor 自动路由（见工程 CLAUDE.md），
> supervisor 内部直接读本库 skill，不经过 junction。Junction 机制保留，供非 supervisor 场景使用。

## 加载机制（别违反）

- skill 只从三类位置加载：个人级 `~/.claude/skills`、**启动目录** `.claude/skills`、已装插件。
- `.claude/skills` **只认一层** `<名>/SKILL.md`；深层目录不被当 skill。
- 子项目里 `.claude/skills` 的 skill，只有会话实际读写该子树文件时才按需载入。
- 插件级 skill 不独立，不能只拷 `skills/` 子目录。

## 接线到新环境（三选一）

1. **直接迁**：把本目录 `.claude/skills/` 的内容拷到目标工程根的 `.claude/skills/` → 天然可见。
2. **只带 env-adapter**：目标环境先放 `env-adapter` 一个，调它 §2 用 junction 接出其余。
3. **junction 指回单一物理库**（推荐，避免多处漂移）：
   ```bash
   cmd //c mklink //J "D:\<env>\.claude\skills\<名>" "D:\<物理库>\.claude\skills\<名>"
   # 删除 junction 用 cmd rmdir（勿用 rm -r 连目标一起删）
   ```

### 迁移内容分类

**✅ 通用内容（默认迁移）**：
- 通用规范文档：`DOC_EDIT_RULES.md`
- 通用 skills：`embedded-debug` / `debug-knowledge` / `skill-registry` / `env-adapter`
- 通用 agents：`supervisor.md`（需按目标工程定制）

**⚠️ 工程相关（按需迁移）**：
- 工程专用 skills：`qsh-*` 系列（仅 810/QSH 工程需要）
- 工程规范文档：`810_driver_test/CLAUDE.md` / `SPEC_REFERENCE.md` / `KNOWN_ISSUES.md`（不随 .claude 迁移）

**单一物理副本、勿在 junction 处另存**；物理库改名/搬迁后重建 junction。

## 路由速查（任务 → skill）

| 任务 | 调 |
|---|---|
| 驱动/固件/烧录/串口调试（QSH） | `qsh-debug` |
| 跨硬件方法学 | `embedded-debug` |
| 某领域既往招式查询/沉淀 | `debug-knowledge`（§装载 / §写入） |
| 外部插件/MCP 能力 | `skill-registry` |
| 810 驱动测试工程任务 | `supervisor` agent 自动路由（手动备选：`qsh-driver-test`） |
| 新环境/不确定/迁移引导 | `env-adapter` |
| 都不命中 | `embedded-debug` 方法学 + `debug-knowledge` 查对应域 |

## 留档（随本目录）

- `plans/skill-library-unification.md` —— 结构/接线/待办（供会话恢复）
- `plans/skill-library-structure.excalidraw(.png)` —— 结构图（env-adapter 加入前的快照）
