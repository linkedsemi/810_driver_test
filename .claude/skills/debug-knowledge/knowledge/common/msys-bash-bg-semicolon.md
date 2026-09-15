# MSYS Bash `&;` 语法错误：后台操作符后直接跟分号

## 现象
在 MSYS/Git Bash 中执行形如 `command &; next_command` 的脚本时报语法错误：
```
bash: syntax error near unexpected token `;'
```
或更隐蔽的情况：`&` 被解释为字面参数而非后台操作符，命令行为异常。

## 根因
Bash（POSIX shell）语法中，`&` 是命令终止符（command terminator），其后可以跟另一个命令但**不能直接跟 `;`**。`;` 同样是命令终止符，两个终止符连在一起是语法错误。

这在 MSYS/Git Bash 中尤其容易踩，因为：
- Windows 用户习惯用 `;` 连接命令（类比 cmd.exe 的 `&`）
- 脚本中 `cmd1 &; cmd2` 看起来像是"后台运行 cmd1，然后执行 cmd2"，实际是语法错误
- 嵌在 Python `subprocess.run(["bash", "-c", "..."])` 中时，报错信息不直观

## 正确写法
```bash
# 正确：换行分隔
cmd1 &
cmd2

# 正确：空格分隔（& 本身是终止符，后面跟新命令即可）
cmd1 & cmd2

# 正确：括号分组
(cmd1 &); cmd2

# 错误：& 后直接跟 ;
cmd1 &; cmd2   # ← syntax error
```

## 判定
1. 脚本里搜索 `&;` 模式（grep `'&;'` 或 `'&\s*;'`）
2. 报错信息含 `near unexpected token ';'` 且行中有 `&`

## 处理
- 删掉 `&` 后面的 `;`（`&` 已经是终止符，不需要再加分号）
- 或用换行替代 `;`（更清晰）

## 关联
- 810_driver_test KNOWN_ISSUES（脚本与工具链域）
- MSYS 环境通用坑（单斜杠 taskkill、路径转换等见 KNOWN_ISSUES #16/#28）
