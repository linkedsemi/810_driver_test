# 驱动审核报告模板——参考

> **定位**：承接 qsh-driver-review §2 Step3 的报告输出模板。每次审核完成按此格式产出。

```markdown
# 驱动代码审核报告
- 日期: YYYY-MM-DD HH:MM
- 文件: zephyr/drivers/i3c/i3c_linkedsemi.c
- 变更: ls_i3c_configure, ls_i3c_do_daa

## 发现

### P0 — Mutex 泄漏（共 3 处）
| # | 函数 | 行号 | 问题 | 修复 |
|---|------|------|------|------|
| 1 | ls_i3c_configure | L430 | `return -EINVAL` 在持锁状态 | → `goto out` |
| 2 | ls_i3c_configure | L435 | `return -EINVAL` 在持锁状态 | → `goto out` |
| 3 | ls_i3c_do_daa | L1295 | `return -ETIMEDOUT` 在持锁状态 | → `goto out_daa_done` |

### P1 — 硬件 Cleanup 遗漏（共 0 处）
（无）

### P2 — 其他（共 0 处）
（无）

## 结论
- P0: 3 处，必须修复后才能合入
- P1: 0 处
- P2: 0 处
```
