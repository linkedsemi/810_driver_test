---
name: qsh-test-run
description: QSH 驱动测试执行 skill（三大类③自动化测试）：dbg_run.py 单次烧录+双串口采集+ztest 原生输出解析、test.sh 三模式、run_*_tests.py 一键回归（批量入口 regression/run_all.py）、ztest 原生输出格式与判定标准。构建/烧录/跑用例按本 skill 操作；连不上/烧不进先查 qsh-debug；结果分析见 qsh-test-compare。
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
3. 解析 **ztest 原生输出**（`common/ztest_report.py` `parse_ztest`：ZT_BOOT /
   `PASS|FAIL|SKIP - <name> in X.XXX seconds` / `INFO <name>: k=v` /
   `PROJECT EXECUTION SUCCESSFUL|FAILED`，ANSI 剥离 + `re.search()`）
4. 按 banner 识别角色（CTRL/TGT），打印 SUMMARY 表

**单核工程（otbn_test/otbn_selftest）差异**（★ CLAUDE.md §10.23/§10.24）：
- **单遍烧录**：reset → load → set $pc → continue 直接采首轮。会话内
  interrupt 后二次 load 实测两度静默挂死（gdb 无任何输出）；reset 已冲刷
  I-cache，load 后 .data/.bss 天然干净，结果与两遍烧录法逐项一致。
  双核 i3c 工程维持两遍烧录不变（prime 轮确实有用）
- **写包提速默认开**：load 前发 `set memory-write-packet-size 16384` +
  `set remotewritesize 16384`，CKLink load 实测 **1KB/s → 41KB/s（41x）**
  （192KB 镜像 ~190s → ~5s）；`--wpacket-size 0` 关闭。stub 不接受时回
  E22 且立即失败，不会静默卡死
- **交互式流程（★ §10.25）**：reset+load 后**等 gdb 打出 "Transfer rate"**
  再 set $pc+continue。一次性 `-ex` 链在 load 刚完成的瞬间发 continue，
  DebugServer 状态不佳时会被**静默丢弃**（CPU 停 halt 无 BOOT，detach 才
  跑起来，实测回归 3/3 复现；dbg_run.py 交互式 0 失败）。run_*_tests.py
  已固化交互式 + 90s 无 BOOT 判 no_boot 强制 [RETRY] 自愈
- **免烧重跑 `--no-load`**：镜像已在 RAM（此前 load 过、未断电）时
  attach → sanity 查 0x100c0000 → `set $pc=入口` 重跑，0 烧录；
  另有 dump_results.py 内存兜底秒读（qsh-test-compare §3）

**输出判读**：
- `UART ROLE DISCOVERY` 全 `?` → 固件没跑，回 `qsh-debug` §2.3 清理连接残留
- 失败项行有 ` FAIL - <name> in X.XXX seconds`，reason 来自 zassert 的
  `Assertion failed at <file>:<line> (<func>): <msg>`（打印在 FAIL 行之前），
  用 `--raw` 看原始日志定位
- gdb 输出存 `tests/dbg_gdb_last.log`，flash 失败时看它尾部

## 4. 一键全量回归

### 4.1 批量总控（★ 首选入口：regression/run_all.py）

```bash
cd 810_driver_test
python regression/run_all.py                # 环境预检 + cases.yaml 全部启用 case
python regression/run_all.py --no-precheck  # 跳过预检（调试用）
python regression/run_all.py --no-erase     # 跳过全量前 flash 全片擦除
python regression/run_all.py --dry-run      # 只列启用 case 与映射脚本
```
- **cases.yaml**：YAML list（`cases:` 下每行一个 case 目录，相对
  810_driver_test），`#` 注释 = 屏蔽。VS Code YAML 模式下 `Ctrl+/` 可直接
  toggle 注释。
- 流程（按 run_all.py 代码实际顺序）：flash 全片擦除一次 → **分配会话目录**
  （§12.1）→ 环境预检（§12.6，FAIL=exit 2 交 qsh-debug，报告写入该目录）→
  逐 case 调对应工程 run 脚本 → 日志存 session/logs/ →
  **单 case FAIL 不阻断后续**；退出码 0=全 PASS / 1=有 FAIL / 2=预检 FAIL
- **双核 UART 由 run_all.py 显式注入**（COM15/COM16，`--ctrl-uart/--tgt-uart`）
  ——双核 run 脚本**必须显式 UART，不支持自动发现**（2026-08-29 实测；
  单核脚本可自动发现）

### 4.2 单工程/单 case 回归（run_*_tests.py）

```bash
cd 810_driver_test/projects/i3c_dualcore_test/tests
export I3C_CTRL_UART=COM15 I3C_TGT_UART=COM16   # ★ 双核必须显式（无自动发现）
python scripts/run_i3c_tests.py [--case case_pure_i3c_12m5] [--session-dir <dir>]
```
回归行为（CLAUDE.md §12/§6.2，所有 run_*_tests.py 一致）：
- **判据核心（★ §6.2）**：两遍烧录（prime 判"任何 ztest 输出"、pass-2 判
  ZT_BOOT）、spawn 后 20s 无 ZT_BOOT = 环境问题中止回归、BOOT 后 10s 无新
  串口字节 **且已见任一核 `PROJECT EXECUTION ...` 完成标记** = 套件结束提前杀
  （中途静默=慢速 case 长间隔，继续等，case_timeout 兜底）、串口实时采集
  （事后读只剩 shell 提示符）、ztest 输出解析 `re.search()` 不锚点（shell
  前缀）、每 case 前后 DebugServer **按需**自愈（§10.17：残留检测通过才重启，
  实测 6 case 仅重启 1 次）、回归前构建全量 case（勿 --skip-build 混旧镜像，
  §10.18）
- **报告归档（会话目录）**：Markdown + JSON 写到
  `810_driver_test/reports/<YYYY-MM-DD>-<idx>/<project>_<case>/`（`common/
  report_dir.py` 统一分配，禁止自算路径）；`summary.md` 只在会话目录最外层，
  **行粒度 = case**（key `<project>_<case>`）；不带 `--session-dir` 自动分配
  新会话目录
- **INFO 约定行解析**：`INFO <test_name>: k=v[,k=v]` 数值型实测（吞吐/占比等）
  进报告"实际"列（§13.4）
- **内存兜底合并**：case 跑完后调 `dump_results.py --case <case> --json`，
  串口缺项用内存值补，来源标 `mem`；数组全 -1 的项算 NOT_RUN 且 case 判 FAIL；
  GDB 读不到内存（连接残留等）标记 `mem-unavailable` 不阻断回归（§12.3）
- Windows 下超时用 Python `subprocess.run(timeout=...)`，**不用 bash `timeout`**

## 5. ztest 原生输出格式与判定标准

```text
ZT_BOOT <core>                            # glue printk，test_main 第一行（BOOT 判据）
Running TESTSUITE <suite>                 # ztest 原生，套件开始
START - <name>                            # ztest 原生，逐项开始
 PASS - <name> in X.XXX seconds           # ztest 原生，逐项判定
 FAIL - <name> in X.XXX seconds
 SKIP - <name> in X.XXX seconds
INFO <name>: <k>=<v>[,<k>=<v>]            # ZT_INFO 宏，实测数值（§13.4）
Assertion failed at <file>:<line> (<func>): <msg>  # zassert 原生，FAIL reason（§13.5）
PROJECT EXECUTION SUCCESSFUL|FAILED       # ztest 原生，本核套件完成标记
```
判定标准（CLAUDE.md §6）：
- 单核：该核全项 `PASS - <name>` + 本核 `PROJECT EXECUTION SUCCESSFUL`；
  双核：同名测试项**两核各自 `PASS - <name>`** + 两核各自 `PROJECT EXECUTION
  SUCCESSFUL` 才算过；单核假 PASS（如 target 只校验部分）不算
- **双核例外（i3c_dualcore_test）**：target `test_main` 不返回（CLAUDE.md §4.1）
  → TGT 端口**没有** `PROJECT EXECUTION` 行，属设计；该工程完成判定 =
  CTRL 侧 SUCCESSFUL 且 TGT execution != "FAILED"（样板 `run_i3c_tests.py`
  `evaluate_case`）。i2c_pure/i3c_i2c_switch 仍按两核各自 SUCCESSFUL 判
- 以机器输出为准，肉眼扫串口不可靠；逐项输出依赖
  `CONFIG_ZTEST_VERBOSE_OUTPUT=y`（§10.26，=n 则 START/PASS 行被抑制全 MISSING）
- ztest/glue 输出 printk 直出 UART（无 LOG 前缀）；解析用 `common/ztest_report.py`
  `parse_ztest`（ANSI 剥离 + `re.search()`），勿自写解析器

## 6. 常见问题速查

| 现象 | 处理 |
|---|---|---|
| flash OK 但 0 行/banner 全 ? | 连接残留 → qsh-debug §2.3 |
| ztest 输出行匹配不上（全 MISSING） | 检查 ANSI/`uart:~$ ` 前缀 → 剥离+search（qsh-debug §4.3）；另查 prj.conf `CONFIG_ZTEST_VERBOSE_OUTPUT=y`（§10.26） |
| 测试顺序错乱假象 | prime/capture 输出混合 → 只采 capture 轮 |
| 某测试 ~6.5s 停摆无 PROJECT EXECUTION | 目标侧死窗/停摆 → 查目标日志，必要时加 GDB 断点验证 |
| 构建缓存旧地址 | rm -rf build 全量重建 |
| load 极慢（~1KB/s，192KB 镜像要 ~190s） | 没设写包参数 | 单核 dbg_run.py 默认已开 16384（41x，§3）；手动 gdb 照 §3 发两条 `set`；E22 = stub 不接受，立即失败不卡死 |
| 单核工程 interrupt 后二次 load 无输出挂死 | 会话内二次 load 静默挂死（§10.24） | 改单遍烧录（§3）；同镜像迭代用 `--no-load` 免烧重跑 |

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
