---
name: env-adapter
description: 本地 skill 库的"环境适配/引导接线 + 通用入口"。当把 skill 库内容（debug 方法论/领域知识库/外部能力目录等）复制或 junction 到一个新的外部工程、或当前环境是新迁入/不确定时调用。职责：探测环境类型 → 盘点当前 .claude/skills 可见集 → 若库体在别处则用 junction 接进当前环境（可选写 CLAUDE.md 指引）→ 按任务路由到库内对应 skill（qsh-debug / embedded-debug / debug-knowledge / skill-registry / qsh-driver-test）。810 内部任务仍 qsh-driver-test 优先；本 skill 是"新环境 / 非 810 / 不知用哪个"的总入口与接线器。
---

# env-adapter —— 跨环境引导接线 + 通用入口

让 skill 库内容"迁到哪个环境都能直接用"。**skill 在自己被发现前无法自我安装**，所以本 skill
的用法是：新环境只要先有本 skill（junction/复制一个即可），它就能指挥 Claude 把库接好、之后路由。

## 什么时候调本 skill

- 新工程刚迁入 skill 库内容（`810_driver_test/.claude/skills/` 那套，或其中通用层），想让它们可用；
- 在**非 810 的外部环境**（其他 MCU/zephyr/linux 工程）做调试，需要方法学/领域知识/外部能力目录；
- 当前环境不确定、不知道该调哪个 skill —— 先调本 skill 盘点路由。

## §0 判环境（先定主入口，勿抢 810）

| 环境信号（按当前目录/工程特征） | 类型 | 主入口 |
|---|---|---|
| 目录含 810_driver_test 结构 / `projects/*_test` / 本库即 810 | 810 驱动测试 | **qsh-driver-test**（本 skill 不抢） |
| 有 `west.yml` / `zephyr/` / `linkedsemi_zephyr_project` | zephyr | `qsh-debug` + `embedded-debug` / `debug-knowledge` |
| 其他嵌入式（MCU/裸机，有厂商 SDK/Keil/IAR 等） | 通用嵌入式 | `embedded-debug` + `debug-knowledge` |
| Linux 内核/驱动工程 | linux | `embedded-debug`（方法）+ `debug-knowledge` linux 域 |

> 判定失败一律按"通用嵌入式"处理，别猜死。

## §1 盘点可见集与库体位置

0. **若当前 `.claude/CLAUDE.md` 存在**（本库便携说明随迁了），先读它拿库成员/默认接线子集/路由；
   若本环境并非由本库迁入、也没有该文件，则跳过。
1. `ls .claude/skills/`（当前启动目录）——已物理可见的 skill。
2. 库体位置 = 能找到 `SKILL.md` 集合的目录。三种情况：
   - **本机就是库体**（.claude/skills 下齐套）→ 跳过 §2；
   - **迁来但散在别处**（内容拷到了别的工作目录，当前 .claude/skills 不全）→ §2 用 junction 接；
   - **只有本 skill 一个**（只带了入口）→ §2 用 junction 把其余接进来。

## §2 接线（把库内 skill 接进当前环境）

原则：**单一物理副本 + junction 双入口**，不复制内容、不在 junction 处另存；同名 skill 已物理存在就
跳过（避免重名冲突）。

```bash
# 每个要暴露的 <名> 执行（源 = 库体目录，目标 = 当前启动目录 .claude/skills）
cmd //c mklink //J "D:\<当前环境>\.claude\skills\<名>" "D:\<库体>\.claude\skills\<名>"
# 删除 junction 用 cmd rmdir（勿用 rm -r，会连目标一起删）
cmd //c rmdir "D:\<当前环境>\.claude\skills\<名>"
```

按环境选择暴露子集（默认）：
- zephyr 类：`qsh-debug` `embedded-debug` `debug-knowledge` `skill-registry`
- 810 本机：全已物理存在，跳过
- 通用嵌入式/linux：`embedded-debug` `debug-knowledge`（若需要也可 `skill-registry`）
- 只带入口的迁移：本 skill + `embedded-debug` 起手，够引导其余

可选：给当前环境 `CLAUDE.md` 追加一行入口指引（幂等，已有则不改）：
> skill 入口：调试方法学 `embedded-debug` / 领域知识 `debug-knowledge` / 外部能力 `skill-registry`；环境不明确先 `env-adapter`。

## §3 路由（任务 → skill）

| 任务 | 路由 |
|---|---|
| 驱动/固件/烧录/串口调试（QSH） | `qsh-debug` |
| 跨硬件方法学（怎么调试） | `embedded-debug` |
| 某领域既往招式查询/沉淀 | `debug-knowledge`（§装载/§写入） |
| 外部插件/MCP 能力启用 | `skill-registry` |
| 810 驱动测试工程任务 | `qsh-driver-test` |
| 不命中 | `embedded-debug` 方法学 + `debug-knowledge` 查对应域 |

## 迁移推荐（用户工作流）

"只直接迁这条路径内容到新工程" = 把 `810_driver_test/.claude/skills/` 整套（或其中通用层）拷进
新工程的 `.claude/skills/` → **天然可见，无需 junction** → 调一次本 skill 做环境确认 + 按需写
CLAUDE 指引。若只带一个入口，带本 skill 即可引导出其余（§2）。

## §4 工程适配（从模板工程迁到新工程时）

把 `.claude/` 从 810_driver_test（或其他模板工程）搬到新工程后，本节指导
替换工程特定内容。

### 4.1 需要替换的硬编码清单

| 类型 | 模板中的值 | 扫描方式 | 替换为 |
|------|-----------|---------|--------|
| Board 名 | `lsqsh_evb@2os`, `lsqsh_evb@1os` | grep 全部 .md | `<new_board>` |
| 工程名 | `i3c_dualcore_test`, `810_driver_test` | grep 全部 .md | `<new_project>` |
| 串口号 | `COM14`/`COM15`/`COM16`/`COM17` | grep 全部 .md | `<new_ports>` |
| 地址 | `0x10000000`/`0x100c0000`/`0x4002200c` | grep 全部 .md | `<new_addrs>`（如架构不变可保留） |
| 调试器路径 | `C:/uaer_app/debug_server` | grep qsh-debug | `<debug_server_path>` |
| 工具链 | `riscv64-unknown-elf-gdb` | grep qsh-debug/qsh-test-run | `<toolchain_prefix>` |

### 4.2 适配流程

```text
1. 复制 .claude/ 到目标工程
2. 确定哪些 skill 是"通用保留"、哪些是"工程专属需替换"
   通用保留：skill-orchestrator, skill-auditor, env-adapter, embedded-debug,
             debug-knowledge, skill-registry, keeper
   工程专属：qsh-* 系列（删除或替换为新工程等价 skill）
3. 按 §4.1 替换硬编码
4. 生成新工程 CLAUDE.md（从模板工程 CLAUDE.md 提取结构，填入新工程信息）
5. 调 skill-auditor 做迁移后验证（§1 完整性检查 + §2 闭环链检查）
6. skill-auditor 通过 → 迁移完成
```

### 4.3 新工程 CLAUDE.md 生成模板

从模板工程 CLAUDE.md 提取以下结构，替换工程特定内容：

```markdown
# <新工程名> 规范

## 工程概述
- SoC: <soc_name>
- Board: <board_name>
- 架构: <arch>（单核/双核）
- 外设: <driver_list>

## 构建命令
west build -p always -b <board> <src>

## 核心规范速查
（从模板 CLAUDE.md §4 提取，保留通用部分，删除 810 专属条目）

## Skill 入口
（由 .claude/CLAUDE.md 自动提供，无需重复）
```

## §5 迁移清单（打包带走前的 checklist）

```text
必须带走：
  □ .claude/ 整目录（agents/ + skills/ + CLAUDE.md + TOOLS_AND_SKILLS.md + DOC_EDIT_RULES.md）
  □ 工程 CLAUDE.md（工程级，与 .claude/CLAUDE.md 不同）
  □ SPEC_REFERENCE.md / KNOWN_ISSUES.md（如有）

按需带走：
  □ common/ 公共模块（test_results/ztest_glue/report_dir/ztest_report 等）
  □ regression/ 回归框架（run_all.py/cases.yaml）

带走后必须做：
  □ 运行 skill-auditor §1 完整性检查
  □ 按 §4.1 替换硬编码
  □ 运行 skill-auditor 二次验证
  □ runner.md 策略表按新工程定制
```

## 验证

新开一个该环境会话 → 让 Claude 列可用 skill，确认目标已可见；或 `/env-adapter` 走一遍 §0-§5。

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| 环境判定完成 | 对应策略 skill（qsh-driver-test / qsh-debug / embedded-debug） | 按 §0 路由 |
| 接线完成 | skill-auditor（验证接线有效） | junction 建完后 |
| 工程适配完成 | skill-auditor（迁移后验证） | 硬编码替换后 |

## 链路
> **相关**→ `skill-orchestrator` · `skill-auditor` · **触发**← 新环境/不确定时
