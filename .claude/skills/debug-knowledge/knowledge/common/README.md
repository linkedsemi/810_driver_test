# 跨域通用调试知识

硬件电气（上拉/时序/RC）与跨 ISA/OS 的通用现象与手法；方法学本体在 embedded-debug。

## 收录条目

> 本域为**空框架**——内容由 `debug-knowledge` §写入 在遇到可复用的领域调试法时自动填充。
> 框架结构：正文条目 = 本目录下一个主题一个 .md（slug：`<现象>-<简述>.md`）；下表为索引。

| 主题 | 场景 | 一句话 |
|---|---|---|
| [I3C 对端上拉并联消失 → -EIO](bus-pullup-parallel-eio.md) | 动态切换/重使能后总线 -EIO（寄存器全对） | M1 完整实例 #1：对端上拉消失→RC 超位时间；示波器优先 |
| [MSYS Bash `&;` 语法错误](msys-bash-bg-semicolon.md) | 脚本里 `cmd &; next` 报 syntax error | `&` 已是终止符，后面不能直接跟 `;`，用换行或空格替代 |
| [`cd X && cmd > log &` 后台化整条链](bash-bg-cd-subshell.md) | 后台启动后相对路径读日志失败，误判"没启动"而重复启动 | `&` 连 `cd` 一起后台化，父 shell cwd 不变；用绝对路径重定向 + 先查进程表再判断死活 |
| [`strings` 只出 ASCII → UTF-8 内容漏判](binary-grep-vs-strings.md) | 改中文断言/日志后 `strings <elf> \| grep` 恒 0 命中，误判成"没重编" | 改二进制 grep（`grep -ac`）或挑 ASCII 片段；mtime/符号序交叉印证。"grep 不到"先怀疑工具，再怀疑构建 |
| [探测/扫描式测量的自检不变量](probe-scan-selfcheck-invariants.md) | 扫描矩阵"完美下三角"/超上限/全空，或"一根脚没还原"被放大成"全网连通" | 状态改写必须**读回验证**（失败即剔出可驱动集、视为无证据）；四条不变量硬闸 = 上限 `N*(N-1)/2` + 对称 + 形状 + **基线门**（基线为低的脚不产生证据）；阳性对照**不能替代**不变量自检 |
| [`write_text()` 把 LF 整份翻成 CRLF](write-text-newline-flip.md) | 批量改仓内文件后 diff 从几行炸到几百行（内容"看起来没变"） | `read_text` 归一 + `write_text` 按平台写回 = 单向翻转；须 `open(..., newline="")` 读写 + **逐文件探测原行尾** + 改后 diff 行数自检（应为常量） |
