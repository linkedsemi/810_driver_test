# 跨域通用调试知识

硬件电气（上拉/时序/RC）与跨 ISA/OS 的通用现象与手法；方法学本体在 embedded-debug。

## 收录条目

> 本域为**空框架**——内容由 `debug-knowledge` §写入 在遇到可复用的领域调试法时自动填充。
> 框架结构：正文条目 = 本目录下一个主题一个 .md（slug：`<现象>-<简述>.md`）；下表为索引。

| 主题 | 场景 | 一句话 |
|---|---|---|
| [I3C 对端上拉并联消失 → -EIO](bus-pullup-parallel-eio.md) | 动态切换/重使能后总线 -EIO（寄存器全对） | M1 完整实例 #1：对端上拉消失→RC 超位时间；示波器优先 |
| [MSYS Bash `&;` 语法错误](msys-bash-bg-semicolon.md) | 脚本里 `cmd &; next` 报 syntax error | `&` 已是终止符，后面不能直接跟 `;`，用换行或空格替代 |
