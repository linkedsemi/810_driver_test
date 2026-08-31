---
name: qsh-driver-test
description: 810_driver_test 自动化测试总入口 skill：工程规范入口（CLAUDE.md）、三大类（驱动 debug 调试 / 测试代码生成 / 自动化测试）skill 路由表、快速标准流程。任何 810_driver_test 下的任务先读本 skill 决定调哪个子 skill；具体操作见对应子 skill。
---

# QSH 驱动自动化测试（总入口）

LSQSH EVB 双核（`lsqsh_evb@2os`）驱动测试工程集 `810_driver_test/`。
**工程规范（命名/固件/协议/Agent/已知坑）以 `810_driver_test/CLAUDE.md` 为准**，
本 skill 只做路由和总览。任务按**三大类**组织：

```text
① 驱动 debug 调试     → qsh-debug      （驱动有问题时：环境链路/断点抓现场/分层定位/探针纪律）
② 测试代码生成       → qsh-test-gen    （新建工程/case：模板生成 + 自检调试）
③ 自动化测试         → qsh-test-run    （执行：构建/烧录/采集/回归）
                     └─ qsh-test-compare（结果：解析/判定/报告/对比/内存兜底）
```

## Skill 调用关系

```text
qsh-driver-test (本 skill：总入口，读它决定下一步)
 ├─ ① 驱动有问题（调试）  → qsh-debug（§0 调试总流程 → 连接 → 断点抓现场 → 分层定位）
 ├─ ② 新建工程/case     → qsh-test-gen（生成后回 qsh-debug 调试验证）
 └─ ③ 跑测试/分析结果    → qsh-test-run（执行）→ qsh-test-compare（结果/报告）
```

| 大类 | Skill | 用途 | 典型任务 |
|---|---|---|---|
| ① 调试 | `qsh-debug` | 连接层坑（DebugServer/烧录/串口）+ 断点抓现场 + 分层根因定位 + 探针纪律 | 烧录失败、固件不跑、串口 0 行、驱动行为异常、定位根因 |
| ② 生成 | `qsh-test-gen` | 新工程/case/协议头/test.sh 模板 + 公共结果模块注入 + 自检调试 | 新建驱动测试工程或 case |
| ③ 测试 | `qsh-test-run` | 执行层：dbg_run.py、test.sh 三模式、run_test.py 回归 | 构建、烧录跑用例、全量回归 |
| ③ 测试 | `qsh-test-compare` | 分析层：token 解析、双 PASS 判定、报告、回归对比、内存兜底 | 分析结果、写报告、对比修复前后 |

## 任务路由表

| 用户诉求 | 大类 | 动作 |
|---|---|---|
| 构建某个 case / 跑某个测试 | ③ | `qsh-test-run` §2/§3 |
| 连不上 / 烧不进 / 串口没数据 | ① | `qsh-debug` §2/§7 |
| 驱动行为异常 / 定位根因 | ① | `qsh-debug` §0 总流程 → §5 分层定位 → §6 断点抓现场 |
| 全量回归 + 报告 | ③ | `qsh-test-run` §4 → `qsh-test-compare` §4 |
| 新增驱动测试工程 | ② | `qsh-test-gen` §1（先读 CLAUDE.md） |
| 新增 case（不改 C 代码） | ② | `qsh-test-gen` §2 |
| 结果 PASS/FAIL 判定 | ③ | `qsh-test-compare` §2（双核双 PASS 才算过） |
| 修复前后对比 | ③ | `qsh-test-compare` §5 |
| 串口 token 被刷掉 / 无串口取结果 | ③ | `qsh-test-compare` §3（dump_results.py GDB 读内存兜底） |
| 烧 XIP 固件 / flash 固化 / XIP 启动测试 | ③ | `qsh-test-run` §8（flash_program.py + dbg_run.py --xip；CLAUDE.md §15） |
| 新坑沉淀 | — | 写进 CLAUDE.md §10 + 对应 skill 速查表 |

## 标准流程（一键走通）

```bash
# 0. 环境检查（DebugServer 在跑、无冲突、串口空闲）
tasklist | grep -i "DebugServer\|CSKYFlash\|python"
# 1. 构建（或 -g 直接跑）
cd 810_driver_test/projects/i3c_dualcore_test/tests/case_pure_i3c_12m5 && ./test.sh
# 2. 单 case 烧录 + 采集 + 解析
cd ../.. && python scripts/dbg_run.py --case case_pure_i3c_12m5 --timeout 35
# 3. 全量回归
python run_test.py
```

## 环境速查（细节见 CLAUDE.md §0 与 qsh-debug）

- **1025 端口由 PC 进程 XuanTie DebugServer 提供**（`C:\uaer_app\debug_server\bin\DebugServerConsole.exe`，
  端口在 `bin\default.ini`），**不是** CKLink 固件内置；不启动就没有 1025。
  在线判定 = GDB 连一次，**勿用 netstat**。
- GDB：`riscv64-unknown-elf-gdb`，`target remote 127.0.0.1:1025`，无 Python。
- 串口：COM15=controller（MCP2200）、COM16/17=target（CH342）、COM14=CKLink 自身
  （排除）；口号以设备管理器为准，按 banner 动态识别。
- 烧录：双核 i3c 用两遍烧录法；cpu1 `load` ELF@0x100c0000，cpu2 `restore bin@0x10000000`。
  **单核工程（otbn_test/otbn_selftest）用单遍烧录 + 写包提速 41x + --no-load 免烧重跑**
  （见 qsh-debug §3.1）。
- Flash 烧录（XIP，CLAUDE.md §15）：`scripts/flash_program.py` 封装
  `CSKYFlashProgramerConsole.exe` + `qsh_flash_algo.elf`；与 DebugServer **互斥**
  占用 CKLink（烧 flash 前 DebugServer 必须退出）；cpu1@0x08000000、cpu2@0x08100000。

## 验证原则

1. 每项测试 **CTRL + TGT 双 PASS** 才算过；单核假 PASS 不算（qsh-test-compare §2）。
2. 结果以机器 token 为准；GDB 证据与串口日志矛盾时先重跑。
3. 串口 token 被刷掉/无串口时，用 `qsh-test-compare` §3 的 dump_results.py
   读内存兜底取结果（公共结果数组 `test_results[]`，CLAUDE.md §4.5）。
4. 定位闭环后按 CLAUDE.md §7 写问题总结存档。
