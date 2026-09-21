# `cd X && cmd > log &` 后台化整条链（含 `cd`）：父 shell cwd 不变

## 现象
执行 `cd <dir> && <cmd> > log 2>&1 &` 后，父 shell 的工作目录**没有**切到 `<dir>`；随后按
**相对路径**访问（`head log` / `cat log`）报 "No such file or directory"。在 agent/自动化
场景里这个报错极易被读成"后台命令**启动失败**" ⇒ **重复启动** ⇒ 两个实例并发抢同一资源。

## 根因
`&` 作用于**整条 AND-list**（`cd X && cmd > log`），整条在**子 shell**里后台执行；`cd` 的生效
范围限于该子 shell，**不影响父 shell**。日志重定向 `> log` 也在子 shell 内解析 ⇒ 相对路径基于
`<dir>`，父 shell 按自己的 cwd 找 `log` 自然找不到。

## 判定
1. 启动后看父 shell 的 cwd —— **未变**（如 `pwd`）。
2. 相对路径读日志报 No such file，但**进程表里命令在跑**（`Get-CimInstance Win32_Process`
   按脚本名/命令行过滤即可见）。

## 处理
- 后台命令**一律用绝对路径**重定向日志：`cmd > /abs/path.log 2>&1 &`；
- 需限定 cwd 时显式分组：`(cd X && cmd) > /abs/path.log 2>&1 &`；
- **纪律**：判断后台任务"死没死"**先查进程表**（按脚本名/命令行匹配），**不要凭日志文件是否
  存在下结论** —— "日志没出现"更可能是路径错，不是没启动。

## 关联
- 810 实例（G-E2-2）：误判导致两个 `run_all.py` 并发抢板子、结果污染；识别/处置见
  `qsh-test-run` §4.1 与 `FRAMEWORK_MAP.md` §3。
- 长时任务脱离工具进程组用 `nohup`（`KNOWN_ISSUES.md` #64）。
- 同族 MSYS 坑：`&;` 语法错误见 `msys-bash-bg-semicolon.md`。
