---
name: qsh-driver-test
description: 810_driver_test 自动化测试总入口 skill：工程规范入口（CLAUDE.md）、三大类（驱动 debug 调试 / 测试代码生成 / 自动化测试）skill 路由表、快速标准流程。任何 810_driver_test 下的任务先读本 skill 决定调哪个子 skill；具体操作见对应子 skill。
---

# QSH 驱动自动化测试（总入口）

LSQSH EVB 双核（`lsqsh_evb@2os`）驱动测试工程集 `810_driver_test/`。
**工程规范（命名/固件/协议/Agent/已知坑）以 `810_driver_test/CLAUDE.md` 为准**，
本 skill 只做路由和总览。任务按**三大类**组织：

```text
① 驱动 debug 调试     → qsh-debug      （QSH 环境实操：连接层/烧录/串口/速查）
                       └─ embedded-debug（通用方法论：分层定位/断点/探针纪律/坑沉淀）
② 测试代码生成       → qsh-test-gen    （新建工程/case：模板生成 + 自检调试）
③ 自动化测试         → qsh-test-run    （执行：构建/烧录/采集/回归）
                     └─ qsh-test-compare（结果：解析/判定/报告/对比/内存兜底）
```

**本地 skill 库成员**（`810_driver_test/.claude/skills/`，全部平铺一层）：

| skill | 定位 | 适用 |
|---|---|---|
| `qsh-driver-test`（本 skill） | 总入口 + 路由 | 任何 810 任务先读它 |
| `qsh-debug` | 三大类① QSH 环境实操（主环境 Zephyr） | 烧录失败/固件不跑/串口无数据/驱动异常；scons/HAL 低频验证见其附 `ref-scons-hal-verify.md` |
| `embedded-debug` | 通用调试方法论（跨硬件环境） | 任何嵌入式工程的定位 bug；QSH 调试的方法论层 |
| `debug-knowledge` | 调试领域知识库（按域装载/自动填充） | mcu/zephyr/linux/riscv/arm 的既往招式查询或沉淀 |
| `qsh-test-gen` / `qsh-test-run` / `qsh-test-compare` | 三大类②③ | 新建工程/跑用例/分析结果 |
| `qsh-test-validate` | ②③ 之间的门禁 | 新建/修改工程后合规性验证 + 编译验证（回归前预检） |
| `skill-registry` | 外部插件/MCP 能力目录 | 新环境需要 superpowers/claude-mem 等时查表 |
| `env-adapter` | 跨环境引导接线 + 通用入口 | 新环境/非 810/不确定调哪个时，探测环境+接线+路由 |

## Skill 调用关系

```text
qsh-driver-test (本 skill：总入口，读它决定下一步)
 ├─ ① 驱动有问题（调试）  → qsh-debug（连接/烧录/串口实操）→ embedded-debug（分层定位/断点方法）
 ├─ ② 新建工程/case     → qsh-test-gen（生成后 → qsh-test-validate 验证合规 → qsh-debug 调试验证）
 └─ ③ 跑测试/分析结果    → qsh-test-validate（回归前预检）→ qsh-test-run（执行）→ qsh-test-compare（结果/报告）
```

| 大类 | Skill | 用途 | 典型任务 |
|---|---|---|---|
| ① 调试 | `qsh-debug` | QSH 环境实操：连接层坑（DebugServer/烧录/串口）+ QSH 速查 | 烧录失败、固件不跑、串口 0 行、驱动行为异常 |
| ① 调试 | `embedded-debug` | 通用方法论：分层定位、断点抓现场、探针纪律、坑沉淀 | 跨工程/不依赖环境的根因定位（被 qsh-debug 引用） |
| ② 生成 | `qsh-test-gen` | 新工程/case/协议头/test.sh 模板 + 公共结果模块注入 + 自检调试 | 新建驱动测试工程或 case |
| ③ 测试 | `qsh-test-run` | 执行层：dbg_run.py、test.sh 三模式、run_test.py 回归 | 构建、烧录跑用例、全量回归 |
| ③ 测试 | `qsh-test-compare` | 分析层：token 解析、双 PASS 判定、报告、回归对比、内存兜底 | 分析结果、写报告、对比修复前后 |
| 外部 | `skill-registry` | 插件/MCP 能力目录（superpowers/claude-mem 等启用法） | 新环境取用外部能力、查外部 skill 来源 |

## 任务路由表

| 用户诉求 | 大类 | 动作 |
|---|---|---|
| 构建某个 case / 跑某个测试 | ③ | `qsh-test-run` §2/§3 |
| 连不上 / 烧不进 / 串口没数据 | ① | `qsh-debug` §2/§7 |
| 驱动行为异常 / 定位根因 | ① | `qsh-debug` §0 总流程 → **embedded-debug §1 分层定位 / §2 断点抓现场**（QSH 实操 qsh-debug §5/§6） |
| 跨环境/通用嵌入式调试方法（非 QSH） | ① | `embedded-debug`（全篇） |
| scons/HAL 示例验证（低频非主环境，scons 构建抓串口/GDB） | ① | `qsh-debug` §附（`ref-scons-hal-verify.md`） |
| 查外部插件/MCP 能力怎么启用 | — | `skill-registry` §4 |
| 环境迁移/新工程引导/不确定调哪个 skill | — | `env-adapter`（探测环境 + 接线 + 路由） |
| 全量回归 + 报告 | ③ | `qsh-test-run` §4 → `qsh-test-compare` §4 |
| 新增驱动测试工程 | ② | `qsh-test-gen` §1（先读 CLAUDE.md） |
| 新增 case（不改 C 代码） | ② | `qsh-test-gen` §2 |
| 新建/修改工程后验证合规性 | ②③ | `qsh-test-validate`（静态检查 + 编译验证） |
| 回归前预检工程合法性 | ③ | `qsh-test-validate`（快速静态扫） |
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
