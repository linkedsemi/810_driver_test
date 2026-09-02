# scons / HAL 示例验证（低频旁支，非主环境）——参考

> **定位**：本文件承接原 `qsh-hw-debug` skill（2026-09-02 降级）。主开发环境是 **Zephyr**；
> 需要验证 `modules/hal/linkedsemi/examples`（`scons ic=qsh` 构建）的 HAL 示例时才翻这里。
> **环境层不在此重复**：DebugServer / 1025（PC 进程 XuanTie DebugServerConsole 提供，非
> CKLink 固件内置）/ 串口 / GDB 通用坑 → 见 `qsh-debug` SKILL.md 正文（本 skill 的调试入口）。
> 通用方法论（断点/分层/探针/坑沉淀）→ `embedded-debug`。

## 构建
```bash
cd modules/hal/linkedsemi/examples/<示例目录> && scons ic=qsh -j8
```
构建警告必须清零再上板（`-Wall` 会抓 unused / const 丢弃等）。

## 串口采集（uart_capture.py，先于程序运行）
```bash
python uart_capture.py <COM口> uart_run1.log 60   # 60 秒窗口，COM 口看设备管理器
```
- 采集窗口必须覆盖程序运行全程：**先启动采集，再启动 GDB**。
- 用 Claude Code `run_in_background: true` 跑（`&` 后台化会吃掉 cd）。
- 工具脚本（uart_capture.py / 各示例 `_run.gdb`）**永不提交**，位于
  `modules/hal/linkedsemi/examples/otbn/`。
- `PermissionError` = 残留采集进程占口：`taskkill //F //PID <pid>`。

## GDB 冷启动 + 断点 + dump（写 `_run.gdb` 后 `-batch` 执行）
```text
set confirm off
set pagination off
target remote 127.0.0.1:1025
monitor reset
load
set $pc=0x10000000
break main.c:<停点行>
continue
x/Nwx <symbol>
quit
```
执行：`/c/user_file/gcc/Xuantie-900-gcc-elf-newlib-mingw-V3.0.1/bin/riscv64-unknown-elf-gdb -batch -x _run.gdb build/qsh/<name>.elf`

要点：
- **冷启动三件套缺一不可**：`monitor reset`（拉硬件复位线）、`load`、`set $pc=0x10000000`。
- **停点 = 程序末尾"跑完就不动"的位置**：HAL 示例通常收尾 `while(1)`；zephyr 原生无此类
  停点需临时加。**停点行号每次校准**：`grep -n "while (1)\|__BKPT" main.c`。
- **IT/回调路径坑**：停在最终 `while(1)` 时异步完成中断可能还没被服务，回调没执行 → dump 旧值；
  先断回调函数入口继续到回调里，再 continue 到 while(1)。
- **`__BKPT()` 坑**：`handle SIGTRAP` 在 CKLink remote 不支持，直接断点打在 `__BKPT()` 行。
- **dump 长度**按「测试项数 × 每项 word 数 + 余量」，宁多勿少（不够静默漏尾部）。

## 解析 dump（程序化，勿肉眼看）
- 判读前先看工程 REC / print_result 宏确认缓冲布局（常见「每项 2 word：ret + 判定标志」）。
- **ret 不是通过标志**：`0`=OK、`1`=HAL_ERROR（**预期失败用例也是 1**）、`5`=HAL_TIMEOUT ——
  判定只看 pass 位。肉眼扫 dump 有看反 ret/pass 的先例，一律脚本解析：

```python
import subprocess, re
r = subprocess.run([GDB, "-batch", "-x", "_run.gdb", elf],
                   capture_output=True, text=True, timeout=120)
words = []
for line in r.stdout.splitlines():
    m = re.match(r"^0x[0-9a-f]+ <[^>]*>:\s*(.*)$", line)
    if m:
        for w in m.group(1).split():
            if re.fullmatch(r"0x[0-9a-fA-F]{1,8}", w):
                words.append(int(w, 16))
# 假设每项 2 word，pass 位为 1 才算通过：
fails = [i for i in range(ntests) if words[2*i+1] != 1]
```

## 双通道验证
GDB dump（内存事实）+ UART print_result（执行事实）用同一判定变量，两边一致才算过；
矛盾时先重跑排除 debug server 瞬态读异常（原理见 embedded-debug §3）。

## 验证原则（提交前自检）
1. 接口/路径改动 → 对应示例全测试项重跑通过
2. GDB dump 与 UART 双通道交叉，无单点依赖
3. 测试工具不提交；git add 只加源码与示例，add 后由用户 review，不主动 commit
