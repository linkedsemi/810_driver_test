---
name: qsh-hw-debug
description: QSH 芯片（RV32 E906 + CKLink 调试器）硬件调试流程：UART 串口日志采集（uart_capture.py）、CKLink gdbserver 控制（monitor reset / load / 断点 / 内存 dump）、测试结果双通道验证。当需要在本工程抓串口日志、用 GDB 操作目标板、验证 HAL/示例测试结果时使用。
---

# QSH 硬件调试（串口 + CKLink + GDB）

LinkedSemi QSH 系列（E906 内核）硬件调试的标准流程。目标板通过 CKLink 调试器
连接，GDB 经 127.0.0.1:1025 的 gdbserver 控制；程序日志经 UART 串口输出。

```
┌────────────┐   JTAG   ┌──────────┐  TCP:1025  ┌──────────────────────────┐
│ QSH 开发板 │◄────────►│ CKLink   │◄──────────►│ riscv64-unknown-elf-gdb   │
│ (E906 核)  │          │ gdbserver│            │ (Xuantie 工具链自带，无    │
└─────┬──────┘          └──────────┘            │  Python 支持！)           │
      │ COM 口 (115200)                           └──────────────────────────┘
      └─────────► uart_capture.py（后台采集 UART 日志）
```

**关键限制**：工具链 GDB 无 Python 支持 → 结果校验一律用 `x/Nwx` 内存 dump +
**外部** Python 解析，不能在 GDB 内写脚本。

## 调试链路（谁在控制谁）

GDB 不直接操作 CKLink 硬件，全部经标准远程调试协议（RSP）：

```
GDB 客户端 ──TCP 127.0.0.1:1025──► CKLink 固件内的 gdbserver ──JTAG──► 目标
```

- `target remote 127.0.0.1:1025`：CKLink 的 gdbserver 是**本地 TCP 服务**
  （端口 1025 是其默认监听口），GDB 用 RSP 远程协议连接
- `monitor <cmd>`：GDB **原样直通**不解析，由 gdbserver 固件实现
  （如 `monitor reset` = 固件拉硬件复位线）
- `break main.c:行`：GDB 用 ELF 调试信息换算地址 → RSP Z 包 → gdbserver
  写 E906 调试模块的**硬件断点寄存器**
- `x/Nwx` 读内存：RSP m 包 → 经 JTAG 总线访问目标 RAM —— 这也是偶发
  「读 glitch」（2 词瞬态异常）的来源，重跑即消
- `load`：GDB 解析 ELF 逐段经 RSP 写入目标

自动化方式：`riscv64-unknown-elf-gdb -batch -x _run.gdb build/qsh/<name>.elf`
（批处理脚本 + 单次会话跑完，不进入交互）。
整条链上唯一握在手里的工具就是 GDB 客户端；三件套（monitor reset / load /
set $pc）都是标准 RSP 操作，无私有魔法。

## 环境

| 项 | 值 |
|---|---|
| GDB | `/c/user_file/gcc/Xuantie-900-gcc-elf-newlib-mingw-V3.0.1/bin/riscv64-unknown-elf-gdb`（Xuantie 工具链，不是标准 riscv gdb） |
| 调试连接 | `target remote 127.0.0.1:1025`（CKLink gdbserver） |
| UART | 主机 COM 口 @ 115200——**口号不固定**：以设备管理器识别的实际串口为准（工程上常见 COM16；Python: `serial.Serial(port, 115200, timeout=1)`） |
| HAL 示例构建 | `scons ic=qsh -j8`（在示例目录内） |
| zephyr 构建 | `west build -p auto -b <board> linkedsemi_zephyr_project/samples/<sample>` |
| ELF 产物 | `build/qsh/<示例名>.elf` |

调试工具脚本（**永不提交**，仓库约定，位于 `modules/hal/linkedsemi/examples/otbn/`）：
- `uart_capture.py` — 串口采集
- 各示例目录下的 `_run.gdb` — 单次 GDB 会话脚本

## 标准流程（单次调试）

### 1. 构建
```bash
cd modules/hal/linkedsemi/examples/<示例目录> && scons ic=qsh -j8
```
构建警告必须清零再上板（`-Wall` 会抓 unused / const 丢弃等）。

### 2. 启动 UART 采集（先于程序运行）
```bash
python uart_capture.py <COM口> uart_run1.log 60   # 60 秒采集窗口（COM 口看设备管理器）
```
- **窗口必须覆盖程序运行全程**：先启动采集，再启动 GDB 运行。
- 用 Claude Code 的 `run_in_background: true` 跑采集（`&` 后台化会吃掉 cd，
  必须用绝对路径或 run_in_background）。
- `PermissionError: could not open port` = 有残留采集进程占口：
  `taskkill //F //PID <pid>`（或 tasklist 找 python 进程）后重试。

### 3. GDB 冷启动 + 断点 + dump（写 `_run.gdb` 后 `-batch` 执行）
模板：
```
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
执行：`/c/user_file/gcc/.../riscv64-unknown-elf-gdb -batch -x _run.gdb build/qsh/<name>.elf`

要点：
- **冷启动三件套缺一不可**：`monitor reset`（CKLink 复位，不是 gdb 的 reset）、
  `load`、`set $pc=0x10000000`。缺任何一步都会跑旧镜像或状态残留。
- **停点 = 程序末尾「跑完就不动」的位置**：HAL 示例通常在测试收尾加
  `while(1)`；zephyr 原生代码没有这类停点，需要时在调试位置临时加
  （或在目标停住处断点）。**停点行号每次校准**：随代码编辑漂移，
  先 `grep -n "while (1)\|__BKPT" main.c` 确认。
- **IT/回调路径的坑**：程序停在最终 `while(1)` 时，异步完成中断可能还没被
  CPU 服务，回调体没执行 → dump 到的是旧值。**先断回调函数入口，continue 到
  回调里，再继续到 while(1)**（批量脚本的 `cb_name` 参数就是这个作用）。
- **`__BKPT()` 的坑**：`handle SIGTRAP` 在 CKLink remote 上不支持，直接断点打在
  `__BKPT()` 所在行即可（SIGTRAP 会让目标停在那里）。
- **dump 长度计算**：按「测试项数 × 每项占的 word 数 + 余量」取，宁多勿少；
  长度不够会静默漏掉尾部测试项，造成误判。

### 4. 解析 dump（程序化，不要肉眼看）
`x/Nwx` 输出格式：每行地址 + 4 个 u32 word。

**判读前先确认结果缓冲布局**（看工程的 REC / print_result 宏定义）：
- 常见的布局是「每项 2 word：ret + 判定标志」（如 `result[idx*2]=ret`、
  `result[idx*2+1]=pass`）
- **ret 不是通过标志**！`0`=OK、`1`=HAL_ERROR（**预期失败用例也是 1**，如 busy
  冲突、参数校验拒绝、篡改签名）、`5`=HAL_TIMEOUT —— 判定只看 pass 位
- 肉眼扫 dump 极易数错偏移（有把 ret/pass 看反的先例），一律程序化解析：

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

### 5. 双通道验证（结果可信的前提）
- **GDB dump（内存事实）** + **UART print_result（执行事实）** 独立取证，
  程序打印与写内存用同一判定变量 —— 两边一致才算过。
- 两边矛盾时**先怀疑 debug server 瞬态读异常**（有「2 word 读错、重跑即消」
  先例），重跑一次确认，不要立刻怀疑代码。

## 调试陷阱清单

1. `&` 后台化吃掉 cd → 绝对路径或 run_in_background
2. COM 口被残留进程占用 → taskkill
3. dump 长度不够 → 漏掉尾部测试项
4. 停点（while(1)/__BKPT）行号漂移 → 每次 grep 校准；zephyr 原生无停点需临时加
5. 每次运行都要冷启动三件套 → 否则旧镜像/状态残留
6. IT 路径不断回调 → dump 到未执行的值
7. ret 位=1/5 是预期失败/超时 → 判定只看 pass 位
8. GDB 与 UART 矛盾 → 重跑，先排除 debug server 瞬态读异常
9. 测试工具（.gdb/.py/.log）永不提交，git add 后由用户 review
10. **软件配置全对 + 硬件波形异常 → 转电路层排查（方法学 M1，跨工程通用）**
    - 触发：寄存器探针读出来全符合预期，但总线波形异常（高电平起不来/上升沿慢/
      偶发 -EIO）。**寄存器读"全对"≠ 总线没问题**，示波器优先级高于寄存器断点。
    - 软件侧推导电路变化：对端切走模式（I3C→I2C/掉电/GPIO）→ 其内部上拉/端接
      消失 → 并联等效阻抗变（R/2 → 单侧 R）→ RC 超位时间。实例：I3C 双路对接对端
      切走后回 I3C -EIO，寄存器全对，根因是总线只剩单侧 ~1KΩ、40ns 高电平拉不上。
    - 规避方向：上拉降挡（pinctrl 强制 `IO_PULL_UP2` 生效）/ 加大 IO 驱动 / 外部电阻。
    - 速查手册：`d:\zephyr4_0\DEBUG_ISSUES_QUICKREF.md`（方法学 M1 + 实例 #1，
      新坑按"命中方法学挂实例 / 新开速查行"归档）

## 验证原则（提交前自检）

1. 所有接口/路径改动 → 对应示例的全测试项重跑通过
2. GDB dump 与 UART 日志双通道交叉确认，无单点依赖
3. 测试工具不提交；git add 只加源码与示例，add 后由用户 review，不主动 commit
4. 每完成一个里程碑同步更新 `.claude/plans/` 下对应 plan 文件的状态表

## 快速命令备忘

```bash
# 串口采集 60 秒（后台，COM 口看设备管理器）
python modules/hal/linkedsemi/examples/otbn/uart_capture.py <COM口> uart.log 60
# GDB 单次会话
gdb -batch -x _run.gdb build/qsh/<name>.elf
# 找停点行号（HAL 示例的 while(1) / __BKPT()）
grep -n "while (1)\|__BKPT" examples/otbn/<示例>/main.c
# 找残留串口进程
tasklist | grep python
```
