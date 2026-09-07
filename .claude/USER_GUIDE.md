# .claude/ Skill 库用户指南

> 本目录是 Claude Code 的 **skill 库**：一组预定义的 AI 协作规范，让 Claude
> 自动按正确的流程、引用正确的知识来帮你干活。
> **你不需要记任何 skill 名字**——直接说任务，Claude 自动路由。

---

## 一、怎么用（日常）

### 最简用法：直接说任务

```
帮我新建一个 SPI 驱动测试工程        → Claude 自动走 gen → validate → run → compare
i3c case_pure_i3c_12m5 失败了帮我查  → Claude 自动走 debug → 分层定位
跑一次全量回归                       → Claude 自动走 test-run → compare → 报告
```

**不需要手动指定 skill**，Claude 根据任务自动选择。如果想明确指定：

```
用 qsh-test-gen 帮我新建 XXX 测试
走 supervisor，帮我调试 XXX 问题
```

### 任务类型 → Claude 自动走的链路

| 你说的 | Claude 自动做 | skill 链 |
|--------|-------------|----------|
| "新建 XXX 测试" | 生成工程 → 合规检查 → 首次验证 → 结果分析 | gen → validate → run → compare |
| "XXX 失败了" | 环境预检 → 分层定位 → 断点抓现场 | debug → embedded-debug |
| "跑全量回归" | 逐 case 烧录采集 → 结果分析 → 报告 | run → compare |
| "分析这个日志" | 解析 ztest 输出 → 判定 PASS/FAIL → 归因 | compare |
| "检查工程合不合规" | 目录/配置/CMakeLists/编译 全扫 | validate |
| "搬到新工程" | 接线 → 替换硬编码 → 完整性验证 | env-adapter → auditor |

### 闭环机制

每个任务完成后，Claude 会自动做**坑沉淀**：
- 反复踩的坑 → `KNOWN_ISSUES.md`
- 可复用的调试招式 → `debug-knowledge/` 知识库
- 模板缺陷 → 自动修复生成模板

你不需要手动"记录"，Claude 在任务闭环时自动沉淀。

---

## 二、目录结构速查

```text
.claude/
├── CLAUDE.md                    ← 库说明（不要改）
├── TOOLS_AND_SKILLS.md          ← skill 索引表
├── DOC_EDIT_RULES.md            ← 文档编辑规范
├── USER_GUIDE.md                ← 本文件（用户指南）
│
├── agents/                      ← Claude 使用的 agent
│   ├── supervisor.md            ← 硬件任务监督（串行化锁）
│   └── skill-keeper.md          ← 文档维护（纯文档任务）
│
├── skills/                      ← skill 库（每个一个目录）
│   ├── skill-orchestrator/      ← ★ 闭环路由总图（所有策略必读）
│   ├── skill-auditor/           ← ★ 完整性自检 + 迁移验证
│   ├── env-adapter/             ← 环境探测 + 工程适配 + 迁移清单
│   ├── embedded-debug/          ← 通用调试方法论（跨硬件）
│   ├── debug-knowledge/         ← 调试领域知识库（按域沉淀）
│   ├── skill-registry/          ← 外部插件/MCP 能力目录
│   ├── qsh-driver-test/         ← 810 总入口路由
│   ├── qsh-debug/               ← QSH 调试实操（连接/烧录/串口）
│   ├── qsh-test-gen/            ← 测试代码生成
│   ├── qsh-test-run/            ← 测试执行（烧录/采集）
│   ├── qsh-test-compare/        ← 测试结果分析/报告
│   └── qsh-test-validate/       ← 工程合规性验证
│
└── plans/                       ← 计划文档（会话恢复用）
```

**通用 skill**（任何工程都能用）：orchestrator / auditor / env-adapter /
embedded-debug / debug-knowledge / skill-registry / supervisor / skill-keeper

**810 专用 skill**（qsh-* 系列）：搬到新工程时需要替换为等价 skill。

---

## 三、搬到新工程

### 快速复制（3 步）

```bash
# 1. 复制整个 .claude/ 到新工程
xcopy /E /I "D:\zephyr4_0\810_driver_test\.claude" "D:\new_project\.claude"

# 2. 删除 810 专用 skill（如果是非 QSH 工程）
rmdir /S /Q "D:\new_project\.claude\skills\qsh-driver-test"
rmdir /S /Q "D:\new_project\.claude\skills\qsh-debug"
rmdir /S /Q "D:\new_project\.claude\skills\qsh-test-gen"
rmdir /S /Q "D:\new_project\.claude\skills\qsh-test-run"
rmdir /S /Q "D:\new_project\.claude\skills\qsh-test-compare"
rmdir /S /Q "D:\new_project\.claude\skills\qsh-test-validate"

# 3. 让 Claude 做迁移验证（在新工程目录下启动 Claude）
# 对 Claude 说："用 skill-auditor 做迁移后验证"
```

### 迁移后 Claude 自动做的事

1. **完整性检查**：所有 skill 文件存在？引用没断裂？闭环链完好？
2. **硬编码扫描**：列出所有需要替换的 Board 名/COM 口/地址/路径
3. **生成适配报告**：告诉你具体改什么、怎么改
4. **二次验证**：改完后再跑一遍确认闭环恢复

### 只迁移通用层（最小集合）

如果新工程不需要 QSH 专用功能，只带这些就够了：

```
.claude/
├── CLAUDE.md
├── TOOLS_AND_SKILLS.md
├── DOC_EDIT_RULES.md
├── agents/ (supervisor.md + skill-keeper.md)
└── skills/
    ├── skill-orchestrator/    ← 闭环路由
    ├── skill-auditor/         ← 自检
    ├── env-adapter/           ← 适配
    ├── embedded-debug/        ← 调试方法论
    ├── debug-knowledge/       ← 知识库
    └── skill-registry/        ← 外部能力
```

然后按新工程需求写等价的 `qsh-test-gen` 等 skill。

---

## 四、常见操作

### 跑回归测试
```
跑一次全量回归
```
Claude 会：环境预检 → 逐 case 烧录采集 → 结果分析 → 生成报告 → 坑沉淀

### 调试失败的 case
```
case_xxx 失败了，帮我查
```
Claude 会：匹配速查表 → 分层定位（应用→驱动→硬件）→ 断点抓现场 → 修复建议

### 新建测试工程
```
帮我新建一个 XXX 驱动测试工程
```
Claude 会：确认外部环境 → 生成骨架 → 合规检查 → 首次验证 → 调通

### 查看知识库有什么招式
```
查一下 debug-knowledge zephyr 域有什么调试招式
```

### 健康检查
```
用 skill-auditor 做一次完整性检查
```

---

## 五、常见问题

### Q: Claude 不按 skill 走怎么办？
说"走 supervisor"或"用 qsh-test-gen skill"，显式指定。

### Q: 怎么知道有哪些 skill？
说"列出所有可用 skill"或读 `TOOLS_AND_SKILLS.md`。

### Q: 新工程没有 supervisor agent 怎么办？
复制 `.claude/agents/supervisor.md` 过去，按新工程定制策略表即可。

### Q: 知识库条目怎么增加？
不需要手动。调试/测试闭环后，Claude 自动把可复用招式写入
`debug-knowledge/knowledge/<域>/`。也可以手动说"把这个经验沉淀到知识库"。

### Q: 怎么知道闭环链是否正常？
说"用 skill-auditor 检查闭环链完整性"。
