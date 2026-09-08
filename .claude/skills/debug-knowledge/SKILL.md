---
name: debug-knowledge
description: 调试领域知识库（按域沉淀/装载）：common / mcu / zephyr(RTOS) / linux / riscv / arm。调试会话开始或出现某域症状时先查该域索引取既往招式；定位闭环且可复用（非一次性、非纯环境工具链坑）的调试法 → 自调本 skill §写入 自动归入对应域。跨域方法学见 embedded-debug；外部插件/MCP 能力见 skill-registry；与 qsh-* 同处 810 统一 skill 库体。
---

# 调试领域知识库（debug-knowledge）

## 定位

本 skill 承载"**各调试领域有什么既往做法**"的知识树；`embedded-debug` 承载"**怎么调试**"的
方法学。两者配合：

```text
出现领域症状/开始调试
   → 查本库对应域索引（§装载），有直接招式就用
   → 没有 → 用 embedded-debug 方法学现推
   → 推出可复用的结论 → 写回本库对应域（§写入，自动填充）
```

## 知识树结构

本 skill 目录内的 `knowledge/` 放的是**内容文件（普通 markdown，不是 skill）**，因此可任意
嵌套，不受 `.claude/skills` 单层扫描限制：

```raw
debug-knowledge/
├── SKILL.md                            ← 本文件（装载 + 自动填充规程）
└── knowledge/
    ├── common/     跨域通用（硬件电气/内核通用 等）
    ├── mcu/        MCU/裸机/外设（无 RTOS）
    ├── zephyr/     Zephyr RTOS（驱动/boot/调度/shell，主战场在 810_driver_test）
    ├── linux/      Linux 内核/驱动/用户态
    ├── riscv/      RISC-V 体系（异常/CLIC/MMU/调试器）
    └── arm/        ARM 体系（异常/TrustZone/内核）
每域一份 README.md 索引；正文条目按主题一个 .md。
```

## §装载（读：调试前先查）

1. 判定当前调试所属域：裸机/外设 → `mcu`；Zephyr → `zephyr`；Linux → `linux`；
   ISA 层 → `riscv`/`arm`；跨域或电气类 → `common`（不确定归哪也先看 common）
2. 读 `<本skill>/knowledge/<域>/README.md` 索引 → 命中主题读正文；没命中不硬找
3. 索引没有 → 用 `embedded-debug` 方法学现推，推完按 §写入 决定是否入库

## §写入（自动填充：沉淀可复用调试法）

**触发时机**：定位闭环，且同时满足——
① **非一次性**（换板/换项目/换驱动还会踩）；② **非纯环境/工具链坑**（那些进
`embedded-debug` §6 或工程 CLAUDE.md §10）；③ 属某域且该域还没有等价条目。

**流程**：
1. 选域目录（不确定归哪 → `common/`，宁宽勿散）
2. 主题 slug：`<现象或主题>-<简述>.md`，如 `i3c-pullup-eio.md`
3. 正文模板（保持与现有条目同构）：`现象 / 根因 / 判定方法 / 处理 / 关联`
   （关联指向 embedded-debug 方法学节、本库对应域既有笔记、qsh-* 实操节）
4. 更新该域 `README.md` 索引（一行：主题 | 场景 | 一句话）
5. 不写死本机路径/口号；通用表述 + 具体实例编号

**纪律**：宁可空框架也不堆一次性个案——过不了 §写入① 的只写问题总结 .md，不入本库。

## 晋升规则（毕业为 skill）

某域条目被反复 /调用、或流程复杂到值得独立成可调 skill 时 → 提升为顶层平铺 skill
`debug-<域>-<主题>`（放 `.claude/skills/` 一层），并在原域 README 留指针。框架先松，
随实际用量收紧。

## 与相邻物的分界

| 物 | 装什么 |
|---|---|
| `embedded-debug` | 方法学（怎么想）+ 通用环境/工具链坑速查 |
| `debug-knowledge`（本 skill） | 各领域可复用调试招式（按域自动填充） |
| `skill-registry` | 外部插件/MCP 能力目录 |
| qsh-* / 工程 CLAUDE.md §10 | QSH/810 工程级坑 |
| ~~顶层 `DEBUG_ISSUES_QUICKREF.md`~~（2026-09-02 已删） | 内容并入 `knowledge/common/` 笔记（电气/总线等），方法学 M1 在 embedded-debug §3.3；跨工程速查由本库按域装载承接 |

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| §装载查到招式 | 回调用方（直接用） | 命中 |
| §装载未命中 | embedded-debug（方法学现推） | 没有现成招式 |
| §写入完成 | keeper（索引同步） | 域 README 更新 |

## 链路
> **相关**→ `embedded-debug` · **被查**← 所有调试/分析链
