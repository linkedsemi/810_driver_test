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

## 验证

新开一个该环境会话 → 让 Claude 列可用 skill，确认目标已可见；或 `/env-adapter` 走一遍 §0-§3。
