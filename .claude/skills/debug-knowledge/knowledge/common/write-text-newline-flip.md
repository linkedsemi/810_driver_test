# Windows `Path.write_text()` 默认把 LF 文件整份翻成 CRLF ⇒ diff 从几行炸到几百行

## 现象

用脚本/工具批量改仓库内文件（同构替换、重命名、批量注入）后，`git diff`（或 diff 行数自检）
显示**本该只有十几行**的改动变成了**几百甚至整文件**——因为每一行都被标为"改动"，而人眼
看不出内容差异。

## 根因

- `Path.read_text()` 走 **universal newline**：把 `\r\n` 归一成 `\n`，读到的文本**丢掉了行尾信息**。
- `Path.write_text()` 默认 `newline=None`：写回时按**平台默认行尾**转换——Windows 下 `\n` → `\r\n`。
- 二者合起来是**单向不可逆**的：一个原本 LF 的文件，读→写一轮后整份变成 CRLF。
- 本仓文件**行尾混用**（多数 CRLF、少数 LF），同一批里可能两种都有 ⇒ 批量改 N 个文件时，
  其中 LF 的那些会**整文件行尾翻转**，diff 从 N 行炸到 N×文件行数。

## 判定

- 改完立刻看 diff：**diff 行数 ≈ 你预期的改动行数**？同构替换应是个常量（如"每文件 ~18 行"）。
  远大于预期 ⇒ 先怀疑行尾翻转，别往下走。
- 定位：`python -c "d=open(p,'rb').read(); print(d.count(b'\r\n'), d.count(b'\n')-d.count(b'\r\n'))"`
  —— 改前记下 `(CRLF, bare-LF)`，改后应**完全一致**。

## 处理

- **逐文件探测原行尾，保持原样**：读用 `open(p, encoding=..., newline="")`（`newline=""` 关掉
  转换，原样保留 `\r\n`/`\n`），写用 `open(p, "w", encoding=..., newline="")`。
  `newline=""` 在写侧的含义是"不转换，写什么是什么"。
- 拼新文本时用**该文件自身探测到的行尾**，不要硬编码 `\n`。
- **改前备份 + 改后 diff 行数自检**（应为常量）——这一条能当场抓出翻转，成本极低。
- 用 `Edit` 类精确替换工具时同理：**CRLF 文件插入的新行必须也是 CRLF**，否则留下混合行尾
  （纯 LF 工具会静默把新行写成 LF）。

## 关联

- 工程 `KNOWN_ISSUES.md`（脚本与工具链域）
- 同域工具链坑：[MSYS Bash `&;` 语法错误](msys-bash-bg-semicolon.md)、[`strings` 只出 ASCII](binary-grep-vs-strings.md)
- 实例：批量改 `projects/*/tests/scripts/dbg_run.py`（14 个 LF + 4 个 CRLF 混用，LF 那批整份翻转）；
  改 `mbox_test/tests/run_test.py` —— 两次都是靠"diff 行数异常"当场发现并回滚重做。
