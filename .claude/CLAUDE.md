# Skill 库便携说明（随 .claude/ 整目录迁移）

> 本目录 `810_driver_test/.claude/` 是 **skill 库的迁移单元**：整目录拷到任何嵌入式/zephyr/linux 工程，
> 里面的 skill 即可被发现（放到目标工程根 `.claude/` 下即加载；或参考 `env-adapter` 用 junction 接）。
> `810_driver_test/CLAUDE.md`（上一级）是 810 工程级指南，**不随本目录迁移**——需要驱动测试规范时单独带上。

## 库成员（`.claude/skills/`，平铺一层 = 一个 skill 一个目录）

| skill | 定位 | 用途 |
|---|---|---|
| `qsh-driver-test` | 810 总入口路由 | 810 驱动测试三大类任务先读它（仅 810 用） |
| `qsh-debug`（含附 `ref-scons-hal-verify.md`） | QSH 调试主入口 | 连接/烧录/串口/速查；scons/HAL 低频验证见附 md |
| `qsh-test-gen` / `qsh-test-run` / `qsh-test-compare` | 810 驱动测试执行链 | 生成/跑用例/分析（仅 810 用） |
| `embedded-debug` | 通用调试方法论 | 分层定位/断点/双通道/探针/坑沉淀三分（跨硬件） |
| `debug-knowledge` | 调试领域知识库 | `knowledge/{common,mcu,zephyr,linux,riscv,arm}/` 内容 .md；装载 + 自动填充 |
| `skill-registry` | 外部插件/MCP 能力目录 | superpowers/claude-mem 等启用法 |
| `env-adapter` | 跨环境引导接线 + 通用入口 | 探测环境 → junction 接线 → 路由（**新环境第一个落点**） |

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
   默认子集：qsh-debug / embedded-debug / debug-knowledge / skill-registry / env-adapter。
   **单一物理副本、勿在 junction 处另存**；物理库改名/搬迁后重建 junction。

## 路由速查（任务 → skill）

| 任务 | 调 |
|---|---|
| 驱动/固件/烧录/串口调试（QSH） | `qsh-debug` |
| 跨硬件方法学 | `embedded-debug` |
| 某领域既往招式查询/沉淀 | `debug-knowledge`（§装载 / §写入） |
| 外部插件/MCP 能力 | `skill-registry` |
| 810 驱动测试工程任务 | `qsh-driver-test` |
| 新环境/不确定/迁移引导 | `env-adapter` |
| 都不命中 | `embedded-debug` 方法学 + `debug-knowledge` 查对应域 |

## 留档（随本目录）

- `plans/skill-library-unification.md` —— 结构/接线/待办（供会话恢复）
- `plans/skill-library-structure.excalidraw(.png)` —— 结构图（env-adapter 加入前的快照）
