# Agent 闭环协作设计（闭环协议）

> **核心思想**：agent 之间通过**共享文件**通信，主会话作为**事件循环**调度。

---

## 1. 闭环协议

```text
┌─────────────────────────────────────────────────────────┐
│                    主会话（事件循环）                      │
│                                                         │
│  ① 派 supervisor → 跑测试                               │
│  ② 读 result（PASS/FAIL）                               │
│  ③ FAIL? → 派 keeper 沉淀                         │
│  ④ keeper 更新 SKILL.md/KNOWN_ISSUES              │
│  ⑤ 再派 supervisor → 读更新后的文档 → 跑下一轮           │
│  ⑥ 循环直到全部 PASS 或升级                              │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

## 2. 触发条件表

| 事件 | 触发谁 | 传递什么 | 期望产出 |
|------|--------|---------|---------|
| supervisor 跑测 PASS | 主会话记录 | 结果 | 统计更新 |
| supervisor 跑测 FAIL | 主会话 → keeper | FAIL 原因 + 根因 | KNOWN_ISSUES + SKILL.md 更新 |
| keeper 沉淀完成 | 主会话 → supervisor | 更新的文档路径 | supervisor 读新文档继续跑测 |
| supervisor 发现文档过时 | SendMessage → keeper | 具体哪条过时 + 修正建议 | 文档修正 |
| supervisor 自愈失败升级 | 主会话 → 用户 | 结构化升级消息 | 用户决策 |
| 连续 3 轮 FAIL 同一项 | 主会话 → 停止循环 | 不可自愈 | 用户介入 |

## 3. 文件通信协议

agent 之间通过共享文件传递信息（不依赖主会话转发内容）：

| 文件 | 写入者 | 读取者 | 信号 |
|------|--------|--------|------|
| `reports/STATUS.md` | supervisor | 主会话 | 心跳 + 进度 |
| `KNOWN_ISSUES.md` | keeper | supervisor | 已知坑列表 |
| `qsh-test-gen/SKILL.md` | keeper | supervisor | 模板规则 |
| `qsh-test-validate/SKILL.md` | keeper | supervisor | 合规检查项 |
| `.claude/agents/*.md` | keeper | 主会话（派工时读） | agent 配置 |
| `supervisor_state.json` | supervisor | supervisor | 断点续跑 |

**关键**：keeper 更新完文档后，在 STATUS.md 里加一行 `文档更新: qsh-test-gen §4 新增 3 条模板约束`，主会话读到后知道该派 supervisor 重新跑测。

## 4. 迭代协议（完整闭环示例）

```text
第1轮：supervisor 跑 adc_temper → FAIL（cpu2 没 hold）
  → 主会话派 keeper → 沉淀 KNOWN_ISSUES #38 + 更新 qsh-test-gen
  → keeper 更新 STATUS.md："模板已更新: cpu2 hold 注入规则"
  → 主会话读到 → 再派 supervisor 修复脚本 → 重跑 → PASS

第2轮：supervisor 跑 dma_test → FAIL（k_malloc 返回 NULL）
  → 主会话派 keeper → 沉淀 KNOWN_ISSUES #40 + 更新 qsh-test-gen
  → keeper 更新 STATUS.md："模板已更新: 禁堆依赖规则"
  → 主会话读到 → 再派 supervisor 修复代码 → 重跑 → PASS

...循环直到全部 PASS
```

## 5. 防死循环

| 保护机制 | 规则 |
|---------|------|
| 同一 item 重试上限 | 最多 3 轮（+keeper 更新算 1 轮） |
| 全局超时 | 整个迭代任务不超过 4 小时 |
| 连续失败升级 | 同一 item 3 轮都 FAIL → 停止，升级给用户 |
| 文档无变化检测 | keeper 报告"无需更新" → 跳过，直接升级 |

## 6. 主会话实现（伪代码）

```python
while tasks_remaining:
    # 派 supervisor
    result = dispatch_supervisor(task)
    
    if result.passed:
        tasks_remaining -= 1
        continue
    
    # 派 keeper 沉淀
    dispatch_skill_keeper(f"沉淀: {result.fail_reason}")
    
    # 读 STATUS.md 看 keeper 更新了什么
    status = read("reports/STATUS.md")
    if "文档更新" in status:
        # 再派 supervisor 读新文档重跑
        result = dispatch_supervisor(f"修复并重跑: {task}（已更新文档）")
    
    if not result.passed and retry_count >= 3:
        escalate_to_user(result)
        break
```

## 7. 与 Workflow 的关系

当前手动循环（主会话逐轮派工）已够用。当迭代复杂度上升时：
- 可用 Workflow 编写脚本化闭环（pipeline: supervisor → 检测 → keeper → 检测 → supervisor）
- Workflow 内的 agent 继承主会话的工具权限
- 但 Workflow agent 同样没有 Skill 工具，知识仍靠 Read 文件
