# Skill 库统一与调试知识架构（2026-09-02）

> 本次会话把散落的 skill 收拢成一个唯一物理库体，并把调试知识按域结构化。本文件供后续会话
> 恢复上下文用。物理状态以 `810_driver_test/.claude/skills/` 实际目录为准，本文件记录"为什么
> 这样设计 + 接线方式 + 待办"。

## 完成状态跟踪

| Phase | 内容 | 状态 |
|---|---|---|
| P0 | 机制核实：skill 只认 `.claude/skills/<名>/SKILL.md` 一层；子项目按需加载；插件级不可只拷 skills/ | 已完成 |
| P1 | 唯一物理库体 = `810_driver_test/.claude/skills/`（qsh-hw-debug 自 zephyr 根移入） | 已完成 |
| P2 | 从 qsh-debug 拆出通用方法论 skill `embedded-debug`（分层/断点/双通道/探针/注释边界/坑沉淀三分） | 已完成 |
| P3 | 新建 `skill-registry`（外部插件/MCP 能力目录） | 已完成 |
| P4 | 新建 `debug-knowledge`（调试领域知识库：common/mcu/zephyr/linux/riscv/arm，含装载器+自动填充器） | 已完成 |
| P5 | 样例验证装载/沉淀链路（zephyr 域 2 条 + common 域 1 条 + README 索引） | 已完成 |
| P6 | qsh-hw-debug ↔ qsh-debug 环境层去重 + 修正 1025 归属冲突 | 已完成 |
| P7 | qsh-hw-debug 降级：scons/HAL 非主环境 → `qsh-debug/ref-scons-hal-verify.md`，skill 删除 | 已完成 |
| P8 | 原顶层 `DEBUG_ISSUES_QUICKREF.md` 迁入 `debug-knowledge/knowledge/common/`（M1 方法留 embedded-debug §3.3），顶文件删除 | 已完成 |
| P9 | zephyr 根 `.claude/skills/` 接线 5 junction：qsh-debug + 通用层(embedded-debug/debug-knowledge/skill-registry/env-adapter) | 已完成 |
| P10 | 文档与记忆同步（根/810 CLAUDE.md、qsh-driver-test、embedded-debug、norms 记忆、MEMORY.md） | 已完成 |
| P11 | 新建 `env-adapter`（跨环境引导接线 + 通用入口，root junction 第 5 个），探测环境/盘点/接线(junction)/路由 | 已完成 |
| P12 | （待办，非阻塞）通用层抽独立便携库跨环境接线；真实调试招式自动喂 debug-knowledge | 待开始 |

## 为什么这样设计

- **单一物理副本 + junction 双入口**：skill 内容只在 810 库一份，zephyr 根用 `mklink /J`
  junction 指回，避免两处漂移。改动只发生在一处。
- **主环境是 Zephyr**（用户明确）：QSH 调试入口收敛为 `qsh-debug` 一个 skill；scons/HAL
  示例验证是低频旁支 → 降级成 `qsh-debug` 内 ref md，不占 skill 位。
- **通用层与 810 层分离（为将来跨环境库铺路）**：
  - 810 专属：qsh-driver-test / qsh-test-*（做 810 任务时按需自动载入）
  - 通用/领域：embedded-debug（方法学）/ debug-knowledge（按域知识树）/ skill-registry（外部能力）/
    env-adapter（跨环境引导接线 + 通用入口——"只迁这条路径到新工程就能用"的适配器）
  - 通用层已在 zephyr 根预置 junction（5 个），将来整组搬出即成独立便携库。
- **调试知识三分沉淀**（embedded-debug §6）：一次性 bug→.md；通用环境/工具链坑→embedded-debug
  速查+CLAUDE §10；**领域可复用招式→debug-knowledge 对应域**（自动填充，不堆一次性个案）。

## 接线方式（跨环境时照做）

```bash
# zephyr 根（或其他环境根）下为库内通用 skill 建 junction（勿复制、勿在 junction 另存）
cmd //c mklink //J "D:\<env>\.claude\skills\<name>" "D:\zephyr4_0\810_driver_test\.claude\skills\<name>"
# name ∈ qsh-debug / embedded-debug / debug-knowledge / skill-registry / env-adapter
# （批量接通用层也可先只带 env-adapter，它按 §2 引导接出其余）
# 810 库改名/搬迁 → 重建 junction；删除 junction 用 cmd rmdir（勿 -Recurse）
```

## 目录快照（810_driver_test/.claude/skills/）

```text
810_driver_test/.claude/skills/            ← 唯一物理库体（平铺一层）
├── qsh-driver-test                        ← 810 总入口路由
├── qsh-debug/                             ← QSH 调试主入口（root junction）
│   ├── SKILL.md
│   └── ref-scons-hal-verify.md            ← scons/HAL 低频验证参考
├── qsh-test-gen / qsh-test-run / qsh-test-compare
├── embedded-debug/                        ← 通用调试方法论（root junction）
├── debug-knowledge/                       ← 领域知识库（root junction）
│   └── knowledge/{common,mcu,zephyr,linux,riscv,arm}/   ← 每域 README 索引 + 条目 md
├── skill-registry/                        ← 外部插件/MCP 能力目录（root junction）
└── env-adapter/                           ← 跨环境引导接线 + 通用入口（root junction）
```

## 关键坑/事实（别再踩）

- `.claude/skills` **只认一层** `<名>/SKILL.md`；深目录不被发现 → debug-knowledge 的域目录
  放"内容 md 而非 skill"；想造可调用 skill 必须平铺（或用名字前缀）。
- junction bug #41177 只影响**个人级**初扫；**项目级** junction 实测可被发现（本会话验证）。
- 本会话曾把 qsh-hw-debug 误以为与 qsh-debug"基本重复"——实为两个不同工作流（HAL 示例 vs
  驱动测试），重复的是环境层；已按"环境层收敛到 qsh-debug"处理，勿再合并。
- 记忆 `debug-issues-quickref` 已改名 `debug-knowledge-common-eio`；原顶层 quickref 文件已删。
