---
name: qsh-test-run
description: QSH 驱动测试执行 skill（三大类③自动化测试）：dbg_run.py 单次烧录+双串口采集+token 解析、test.sh 三模式、run_*_tests.py 一键回归、token 格式与判定标准。构建/烧录/跑用例按本 skill 操作；连不上/烧不进先查 qsh-debug；结果分析见 qsh-test-compare。
---

# QSH 驱动测试执行

用例的构建、烧录、采集、解析全流程。**连接层问题（DebugServer/串口/连接残留）
先查 `qsh-debug`**；工程规范以 `810_driver_test/CLAUDE.md` 为准。

## 1. 前置检查（每次跑测试前）

```bash
# 1) DebugServer 在跑（在线判定=能连，别用 netstat）
tasklist | grep -i DebugServer
# 2) 无 CSKYFlashProgrammer 占用（RAM load 前；烧 flash 时反过来，见 §8）
tasklist | grep -i csky
# 3) 串口没被占用
tasklist | grep -i python
```
任一异常按 `qsh-debug` §2 处理。

**外部环境告知（★ CLAUDE.md §14.2）**：被测外设存在芯片外部连接路径时
（接线/对端/环回/上拉），开始烧录采集前**先向用户报告本次测试环境**
（接线状态、对端设备、串口号、速率配置），确认后再跑；环境变更后重新回归，
不复用旧报告。

## 2. 构建

```bash
cd 810_driver_test/projects/i3c_dualcore_test/tests/case_pure_i3c_12m5
./test.sh            # 只编译（SKIP_LOAD=y），打印 launch.json 片段
./test.sh -g         # 编译 + GDB 加载运行
./test.sh -r         # 仅加载运行（agent 模式）
```
- `west build` 用 `-p auto`（CLAUDE.md §5）
- **改过 overlay（SRAM 地址类）必须 `rm -rf build/controller build/target` 全量重建**
- 编译产物：`tests/<case>/build/{controller,target}/zephyr/zephyr.{elf,bin}`

## 3. 单次烧录 + 采集 + 解析（调试用）

```bash
cd 810_driver_test/projects/i3c_dualcore_test/tests
python scripts/dbg_run.py --case case_pure_i3c_12m5 --timeout 35
# 可选：--raw 全量输出 / --ctrl-uart COMxx --tgt-uart COMxx 显式指定
```

脚本做什么（理解后便于排查）：
1. 枚举非 CKLink 串口（排除 32bf/cklink），全部打开
2. **两遍烧录法**：prime 轮（20s，输出丢弃）→ capture 轮（35s，串口采集）
3. 解析 `I3C_TEST:*` token（ANSI 剥离 + `.search()`）
4. 按 banner 识别角色（CTRL/TGT），打印 SUMMARY 表

**输出判读**：
- `UART ROLE DISCOVERY` 全 `?` → 固件没跑，回 `qsh-debug` §2.3 清理连接残留
- 失败用例行有 `FAIL:<name>:<reason>`，用 `--raw` 看原始日志定位
- gdb 输出存 `tests/dbg_gdb_last.log`，flash 失败时看它尾部

## 4. 一键全量回归

```bash
cd 810_driver_test/projects/i3c_dualcore_test/tests
export I3C_CTRL_UART=COM15 I3C_TGT_UART=COM16   # 可选，默认自动发现
python scripts/run_i3c_tests.py [--case case_pure_i3c_12m5]
```
回归行为（CLAUDE.md §12，所有 run_*_tests.py 一致）：
- **报告归档**：Markdown + JSON 写到 `810_driver_test/reports/<YYYY-MM-DD>/`，
  当天 `summary.md` 索引自动更新（按工程名替换行）；同日重复回归=覆盖写
- **INFO token 解析**：数值型实测（吞吐/占比等）进报告"实际"列（§13.4）
- **内存兜底合并**：case 跑完后调 `dump_results.py --case <case> --json`，
  串口缺项用内存值补，来源标 `mem`；数组全 -1 的项算 NOT_RUN 且 case 判 FAIL；
  GDB 读不到内存（连接残留等）标记 `mem-unavailable` 不阻断回归（§12.3）
- Windows 下超时用 Python `subprocess.run(timeout=...)`，**不用 bash `timeout`**

## 5. Token 格式与判定标准

```text
I3C_TEST:CTRL:START:<name>                 # 开始
I3C_TEST:CTRL:INFO:<name>:<k>=<v>[,<k>=<v>]  # 实测数值（PASS/FAIL 之前，§13.4）
I3C_TEST:CTRL:PASS:<name>                  # 双核各自 PASS
I3C_TEST:CTRL:FAIL:<name>:<reason>         # reason 必填（§13.5 格式）
I3C_TEST:CTRL:SUMMARY:ITEM:<name>:<PASS|FAIL|NOT_RUN>[:reason]
I3C_TEST:CTRL:SUMMARY:END:<passed>/<ran> PASS
I3C_TEST:DONE:PASS|FAIL                    # 全跑完
```
判定标准（CLAUDE.md §6）：
- 每项必须 **CTRL + TGT 双 PASS** 才算过；单核假 PASS（如 target 只校验部分）不算
- 以机器 token 为准，肉眼扫串口不可靠
- token 用 printk 输出（规避 LOG 前缀），parser 只扫 `<PREFIX>:` 前缀

## 6. 常见问题速查

| 现象 | 处理 |
|---|---|
| flash OK 但 0 行/banner 全 ? | 连接残留 → qsh-debug §2.3 |
| token 匹配不上 | 检查 ANSI/`uart:~$ ` 前缀 → 剥离+search（qsh-debug §4.3） |
| 测试顺序错乱假象 | prime/capture 输出混合 → 只采 capture 轮 |
| 某测试 ~6.5s 停摆无 SUMMARY | 目标侧死窗/停摆 → 查目标日志，必要时加 GDB 断点验证 |
| 构建缓存旧地址 | rm -rf build 全量重建 |

## 7. 坑沉淀要求

跑测试遇到的新问题，现象/根因/处理追加到 §6 + CLAUDE.md §10。

## 8. XIP 固件固化与启动测试（flash 烧录）

把固件烧进板载 SPI flash 后从 flash 启动测试（CLAUDE.md §15）。与 RAM load
互补：持久化、断电不丢，适合 XIP 启动类测试/固化镜像。

```bash
# 单次烧录（读/写/校验都在 scripts/flash_program.py）
python 810_driver_test/scripts/flash_program.py --bin zephyr_flash_xxx.bin@0x08000000 --verify
# 双核 ★必须一次会话（全片擦会擦掉另一核，CLAUDE.md §10.14）
python 810_driver_test/scripts/flash_program.py --bin cpu1.bin@0x08000000 --bin cpu2.bin@0x08100000 --verify
# 读回 + 与源比对
python 810_driver_test/scripts/flash_program.py --dump-out rb.bin --dump-addr 0x08000000 --dump-len 0x1000 --compare cpu1.bin

# 烧录→启动→采集一键（token 前缀默认 I3C_TEST，按固件用 --prefix 指定）
python tests/scripts/dbg_run.py --xip --bins cpu1.bin@0x08000000,cpu2.bin@0x08100000 --timeout 15
python tests/scripts/dbg_run.py --xip --bins hello.bin@0x08000000 --timeout 10 --prefix HELLO
```

**要点（实测，CLAUDE.md §15/§10）**：
- **默认测试（单核/双核）= 全 RAM**（现有流程，不烧 flash）
- **XIP 标准组合 = cpu1 XIP + cpu2 RAM**（★ 默认 XIP 场景）：
  - cpu1 用 `lsqsh_evb@2os_xip/lsqsh/cpu1` 构建（XIP 执行 + boot 引导）→ 烧 `0x08000000`
  - cpu2 用**标准 2os** `lsqsh_evb@2os/lsqsh/cpu2` 构建（RAM 固件）→ 产物
    `zephyr_flash_*.bin` 也烧进 flash `0x08100000`，由 cpu1 XIP boot 搬运启动
  - 两个 bin **一次会话**烧录（全片擦会擦掉另一核）；`--reset-and-run` 复位启动
- **CKLink 互斥**：烧 flash 时 DebugServer 必须退出（flash_program.py 检测并警告，
  `--kill-debugserver` 强制杀）；烧完要 GDB 再启 DebugServer。**无两遍烧录法、无 GDB load**
- 地址约定：cpu1@0x08000000、cpu2@0x08100000（**地址在 test_common.sh 环境变量
  `CPU1_XIP_ADDR`/`CPU2_XIP_ADDR` 集中掌握，test.sh 引用，不散落硬编码**）
- 启动方式：`--reset-and-run`（烧完自动复位从 flash 启动）；无效时
  `--no-reset-run --gdb-reset`（烧完后 DebugServer 已空闲，reset 启动）
- 结果判定：`FP_OK: <name>@<addr>` + 退出码 0；失败看 `Bins\flash-programer.log` 尾部
- 环境变量：`FP_EXE`/`FP_ALGO`/`FP_LOG` 可覆盖工具路径；**默认已用工程内
  打包副本**（`tools/flash_program/`，自包含），外部 `C:\user_file\...` 仅回退
