# runner 环境路由表 — QSH / 810_driver_test

> **定位**：`runner.md` 通用框架的**环境专属插件**。本文件承载 QSH/810 环境的
> skill 关联表、策略→skill 链、环境预检来源、心跳监控参数、规范外链。
> runner.md 主体不引用本文件以外的任何 qsh-* 路径。
>
> **迁移语义**：随 `.claude/` 整库迁移；在非 QSH 环境中**休眠保留**（不删、不加载）。
> 新环境参照本文件结构新建 `runner-<env>-routing.md`，并在 runner.md 启动流程
> Step 3.5 指向它。回到 QSH 场景时本文件直接复用。

## 1. 关联 Skill（QSH 专属；通用部分见 runner.md）

| Skill | 文件路径（相对于 810_driver_test/） | 何时读 |
|-------|--------------------------------------|--------|
| qsh-debug | `.claude/skills/qsh-debug/SKILL.md` | 环境预检 / 调试 / 自愈时 |
| gdb 工具集 | `.claude/tools/gdb/README.md` | 断点抓现场/需 GDB 操作时（统一接口，优先于手写 .gdb 脚本；详见 qsh-debug §6） |
| qsh-test-run | `.claude/skills/qsh-test-run/SKILL.md` | 跑测试时 |
| qsh-test-gen | `.claude/skills/qsh-test-gen/SKILL.md` | 新建工程时 |
| qsh-test-compare | `.claude/skills/qsh-test-compare/SKILL.md` | 结果分析时 |
| qsh-test-validate | `.claude/skills/qsh-test-validate/SKILL.md` | 合规检查时 |
| qsh-driver-review | `.claude/skills/qsh-driver-review/SKILL.md` | 驱动代码审核时（mutex 泄漏/资源清理/中断安全） |

## 2. 策略 → skill 链（QSH）

| 策略 | 主用 skill 链（Read 路径） |
|------|---------------------------|
| `STRATEGY_TEST` | `.claude/skills/qsh-test-run/SKILL.md` → `.claude/skills/qsh-test-compare/SKILL.md`（**§4.1 `failures/` 失败归因为固定收尾动作**，有 FAIL 就必做） |
| `STRATEGY_DEBUG` | `.claude/skills/qsh-debug/SKILL.md` → `.claude/skills/embedded-debug/SKILL.md` |
| `STRATEGY_DEV` | 按领域 Read 对应 skill → qsh-test-gen → qsh-test-validate → qsh-driver-review → qsh-test-run |
| `STRATEGY_DEV`（审核） | `.claude/skills/qsh-driver-review/SKILL.md` |
| `STRATEGY_ANALYZE` | `.claude/skills/debug-knowledge/SKILL.md` → `.claude/skills/embedded-debug/SKILL.md` |
| `STRATEGY_VALIDATE` | `.claude/skills/qsh-test-validate/SKILL.md` |

### 策略执行概要（★ 含闭环链，按 skill-orchestrator §2 执行）

**STRATEGY_TEST**：读 cases.yaml → 过滤已完成 → 环境预检 → 逐 case 执行（退出码 0=PASS）→ 失败分类恢复或跳过 → 更新 state → **→ qsh-test-compare（★ 自动转）→ ★ qsh-test-compare §4.1 失败归因 `failures/`（有 FAIL 则必做：先 `python common/failures_report.py --session <会话>` 出骨架，再让 agent 只补 `⚠️` 处）→ 坑沉淀决策** → 汇总

**STRATEGY_DEBUG**：匹配速查表 → qsh-debug §0 总流程（环境→复现→分层定位→断点）→ 每步查已知坑 → 3 轮未闭环则升级 → **→ 坑沉淀决策（领域招式→debug-knowledge §写入；通用坑/模板缺陷→写 INBOX，沉淀归 keeper）**

**STRATEGY_DEV**：拆子任务 → **qsh-test-gen（生成）→ ★ qsh-test-validate（合规自检）→ FAIL 回 gen / PASS → ★ qsh-driver-review（驱动代码审核，P0 发现回代码修改）→ ★ qsh-test-run（首次验证）→ ★ qsh-test-compare → 坑沉淀决策** → 构建失败读错误修复重试（最多 2 次）

**STRATEGY_ANALYZE**：**debug-knowledge §装载（先查）** → 收集证据 → 形成假设（至少 2 个）→ 查知识库+代码分析 → 排除收敛 → **☆ debug-knowledge §写入（可复用招式入库）** → 报告结论

**STRATEGY_VALIDATE**：读规范 → 逐项检查工程合规 → 标记违规+修复建议 → 合规报告

**STRATEGY_MULTI**：拆子任务 → 按类型路由 → 串行执行+独立 state → 子任务失败不阻断兄弟

## 3. 手动环境预检来源（QSH）

逐项清单与命令见 **qsh-debug SKILL.md §2.4**（唯一来源）。任何一项不过 → 自愈
（qsh-debug §2.5 阶梯）→ 重验 → 仍不过则中止并升级。

> 预检原则（为什么必须手动、内置预检的盲区）见 runner.md「手动环境预检」节。

## 4. 心跳监控参数（QSH）

| 参数 | 值 / 来源 |
|------|-----------|
| 批量回归脚本 | `regression/run_all.py`（background 模式启动） |
| 烧录/load 静默豁免 | 未设写包可 ~190s（qsh-debug §3） |
| prime 轮豁免 | 首轮 20s 输出丢弃，设计如此 |
| 静默阈值 | 该 case 的 `case_timeout`，不用固定值 |
| 脚本级超时（优先于看门狗） | `case_timeout` / 90s no_boot（qsh-test-run §4.2） |
| 卡死进程特征 | GDB 进程在 = 疑似 GDB 挂死；无 GDB 且日志停 = 疑似脚本卡死 |

## 5. 详细规范外链（QSH）

| 主题 | 位置（Read 路径） |
|------|-------------------|
| 策略详细实现 / 失败恢复 / 进度持久化 / 断点续跑 / 汇总报告模板 | `SPEC_REFERENCE.md` §6.8 |
| 环境预检 | `.claude/skills/qsh-debug/SKILL.md` §2.4 |
| 自愈升级策略 | `.claude/skills/qsh-debug/SKILL.md` §2.5 |
