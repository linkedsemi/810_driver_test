# Claude Code 功能 & Skill 速查清单

> 2026-09-04 整理。★ = 高频项。
> **调用方式**：🟢 自动（agent 根据任务自动选择）/ 🔵 手动（你需要明确触发）

---

## 一、内置命令

| 命令 | 调用方式 | 用法 |
|------|---------|------|
| ★ **/loop** | 🔵 手动 | 对话框输入 `/loop <任务描述>` |

---

## 二、自研 Skill

| Skill | 调用方式 | 用法 | 说明 |
|-------|---------|------|------|
| ★ **qsh-driver-test** | 🟢 自动 | agent 读 CLAUDE.md 自动路由 | 总入口，决定调哪个子 skill |
| ★ **qsh-debug** | 🟢 自动 | supervisor 策略 STRATEGY_DEBUG 自动调 | DebugServer/GDB/串口/四步自愈 |
| ★ **embedded-debug** | 🟢 自动 | qsh-debug 内部自动调 | 通用分层定位方法论 |
| **debug-knowledge** | 🟢 自动 | 定位时自动查知识库 | 调试领域知识库 |
| **qsh-test-gen** | 🟢 自动 | supervisor 策略 STRATEGY_DEV 自动调 | 新建工程/case 模板生成 |
| **qsh-test-validate** | 🟢 自动 | 生成后/回归前自动调 | 合规性验证 + 编译检查 |
| ★ **qsh-test-run** | 🟢 自动 | supervisor 策略 STRATEGY_TEST 自动调 | 构建/烧录/跑用例 |
| ★ **qsh-test-compare** | 🟢 自动 | 测试完成后自动调 | 结果解析/报告/对比 |
| **skill-registry** | 🟢 自动 | 需要外部能力时自动查 | 外部插件/MCP 目录 |
| **env-adapter** | 🟢 自动 | 新环境/不确定时自动调 | 环境探测 + 路由 |

> 所有 skill 都是 🟢 自动——你只需要说任务，agent/supervisor 自动选择并调用。

---

## 三、自研 Agent

| Agent | 调用方式 | 用法 | 说明 |
|-------|---------|------|------|
| ★ **supervisor** | 🟢 自动 | 碰硬件/碰回归构建目录的任务走 supervisor（分级路由，CLAUDE.md） | 策略路由/失败恢复/断点续跑 |

> 分级路由（2026-09-04 用户裁定）：碰硬件/碰回归构建目录的任务（烧录、串口、调试、回归、
> 固件开发）必须走 supervisor（硬件串行化）；纯文档、纯代码分析任务主会话可直接派独立
> agent 并行执行；纯查询不需要 supervisor。

---

## 四、外部插件 Skill（需安装）

| 插件 | 调用方式 | 用法 | 说明 |
|------|---------|------|------|
| ★ **superpowers:systematic-debugging** | 🔵 手动 | `/superpowers:systematic-debugging` | 系统性根因定位 |
| **superpowers:brainstorming** | 🔵 手动 | `/superpowers:brainstorming` | 设计前澄清 + 出方案 |
| **superpowers:writing-plans** | 🔵 手动 | `/superpowers:writing-plans` | 写执行计划 |
| **superpowers:executing-plans** | 🔵 手动 | `/superpowers:executing-plans` | 分步执行计划 |
| **superpowers:verification-before-completion** | 🔵 手动 | `/superpowers:verification-before-completion` | 完成前自证 |
| **claude-mem** | 🟢 自动 | MCP 工具自动可调 | 跨会话记忆检索 |
| **skill-creator** | 🔵 手动 | `/skill-creator:skill-creator` | 新建/改进 skill |
| **code-review** | 🔵 手动 | `/code-review:code-review` | PR 审查 |
| **excalidraw-diagram** | 🔵 手动 | `/excalidraw-diagram` | 画架构/流程图 |

---

## 五、内置 Tool（agent 自动调用，你不需要直接操作）

| Tool | 说明 |
|------|------|
| **Bash** | 执行 shell 命令 |
| **Read / Edit / Write** | 文件读写编辑 |
| **Grep / Glob** | 搜索代码/找文件 |
| **Agent** | 启动子 agent |
| **Workflow** | 多 agent 编排 |
| **WebFetch / WebSearch** | 查网络资料 |
| **CronCreate** | 定时任务 |
| **ScheduleWakeup** | /loop 调度唤醒 |

> Tool 全部 🟢 自动——agent 根据需要自行调用，你无需干预。

---

## 六、你的操作清单（只需记住的）

### 你手动做的事

| 操作 | 怎么做 | 场景 |
|------|--------|------|
| 发任务 | 对话框直接说 | "帮我跑全量回归" / "case_xxx FAIL 了帮我查" |
| 持续执行 | `/loop <任务描述>` | 无人值守长时间跑 |
| 中断 | `Esc` | 停止当前 agent |

### agent 自动帮你做的事（你不需要管）

| 事情 | 谁在做 |
|------|--------|
| 选择策略（测试/调试/开发/分析） | supervisor agent |
| 调用对应 skill | supervisor agent |
| 环境自愈（DebugServer/串口/CKLink） | supervisor → qsh-debug |
| 失败重试（最多 2 次） | supervisor agent |
| 跳过不可恢复的失败，继续下一个 | supervisor agent |
| 进度持久化 + 断点续跑 | supervisor agent |
| 汇总报告 | supervisor agent |
