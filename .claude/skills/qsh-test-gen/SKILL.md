---
name: qsh-test-gen
description: QSH 驱动测试代码生成 skill（三大类②自动化测试代码生成）：按 810_driver_test/CLAUDE.md 规范自动生成新的驱动测试工程或 case（目录骨架、协议头、双核固件骨架、公共结果模块注入、test.sh 三模式、dbg_run.py），并指导自行调试验证到可用。新建 <driver>_test 工程或新 case 时调用本 skill。
---

# QSH 测试工程 / 用例生成

目标：**新 agent 直接按规范生成新测试例程，并自己调试到可用**。
规范依据：`810_driver_test/CLAUDE.md`（命名/固件/协议/Agent 规范全部以它为准）。
样板工程：`810_driver_test/projects/i3c_dualcore_test/`（生成时逐文件对照抄）。

## 1. 生成新驱动测试工程（<driver>_test）

### 1.0 外部环境确认（★ 先做，CLAUDE.md §14.1）

生成前先判断被测外设是否有**芯片外部连接路径**：
- **有**（I3C 对连、UART 环回、GPIO 互连、外接传感器等）→ **必须先问用户
  具体外部环境**（接线、对端设备、电平/上拉/速率），用户告知后才能开始生成；
  环境记录进顶层 README.md「外设接线记录」+ 工程 COVERAGE.md 标注 `外部: ...`
- **无**（纯片内外设：TRNG/加密/存储/看门狗）→ 直接生成，COVERAGE.md 标注"片内"

### 1.1 目录骨架（CLAUDE.md §1，★ 工程统一收在 `projects/` 下）
```text
810_driver_test/projects/<driver>_test/
├── cpu0_<role>_controller/     # cpu2 侧固件（发起方）
│   ├── src/main.c + src/<role>_controller_test.c
│   ├── prj.conf
│   └── CMakeLists.txt          # 注入 common/cpu2_console_uart5.overlay + cpu2_mem_layout.overlay + test_results.c
├── cpu1_<role>_target/         # cpu1 侧固件（响应方）
│   ├── src/main.c + src/<role>_target_test.c(.h)
│   ├── prj.conf
│   └── CMakeLists.txt          # 注入 test_results.c
└── tests/
    ├── include/<driver>_test_protocol.h
    ├── scripts/dbg_run.py      # 从样板复制改名，改 TESTS_DIR/prefix
    ├── scripts/dump_results.py # 从样板复制（GDB 读内存兜底，CLAUDE.md §6.1，必须带 --json）
    ├── scripts/run_<driver>_tests.py  # 一键回归（必须实现 §12 归档，见 §1.6）
    ├── COVERAGE.md             # 覆盖矩阵（§13.3 模板，见 §1.6）
    ├── expected/               # 确定性测试的预期数据（NIST 向量等，§13.2）
    ├── case_<速记>/test.sh + overlay/conf 差异
    └── *.md                    # 问题总结
```

> 固件目录 `cpu<N>_<role>/` 的 role 按 **CLAUDE.md §1.1 命名结构规范**选：
> I3C → controller/target；SPI/I2C/UART → master/slave；host/device 模型 →
> host/device；**纯片内单核外设（无总线角色）→ 直接 `cpu1_<driver>`**（如
> `cpu1_trng_test`）。角色名必须来自被测驱动标准术语，不臆造混搭名。

### 1.2 关键注入（cpu2 侧 CMakeLists.txt，样板照抄；工程在 projects/ 下，common 引用比原来多一级 `../../../`）
```cmake
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)
```

### 1.3 协议头（tests/include/<driver>_test_protocol.h）
- opcode 用 ASCII 助记符（`'S'/'E'/'W'/'L'/'I'/'H'/'R'`），避免与 direct CCC 命令码混淆
- 测试枚举 `<DRIVER>_TEST_COUNT` + 名字数组（名字即 token 里的 test_name）
- token 宏：`I3C_TEST_START/PASS/FAIL/SUMMARY_BEGIN/ITEM/ITEM_REASON/END/DONE`
- 全部 `printk` 输出（规避 LOG 前缀）

### 1.4 固件骨架要点（CLAUDE.md §4 血泪教训）
1. **test_main() 必须 `k_thread_join(&dispatcher_thread, K_FOREVER)`**，
   不得建线程即返回（否则 LOG_PANIC → 8ms 死窗）
2. ISR 回调内**禁止打日志**，只记账；dispatcher 线程在 TEST_END 时汇总打印
3. target 侧 IBI/HJ 触发命令必须经事件队列交给 dispatcher 线程（ISR 内禁止
   调会拿锁的驱动 API）
4. 双核统一用 **§4.5 公共结果模块**记账（`common/test_results.{h,c}`），
   **禁止**私有 results 数组：
   - CMakeLists 注入 `../../../common/test_results.c` + include dir（工程在 projects/ 下）
   - setup 里 `tr_init(I3C_TEST_COUNT)`；测试结束时
     `tr_record(id, failed ? TR_FAIL : TR_PASS, "%s", reason)`
   - SUMMARY 打印读 `test_results[]`/`test_reasons[]`（勿再引用旧私有数组名）
5. 结尾打 SUMMARY（`SUMMARY:BEGIN/ITEM/ITEM_REASON/END` token 流程）

### 1.5 构建/加载（scripts/test_common.sh 复用）
- `test_common.sh` 已通用化：设 `TEST_ROOT` 后 source，`run_test` 一把跑
- case 的 test.sh 三模式：`./test.sh`（只编译）/ `-g`（+GDB 加载运行）/ `-r`（仅加载）
- GDB 地址/串口全走环境变量（`GDB_SERVER_HOST/PORT`、`*_UART`），不硬编码

### 1.6 覆盖矩阵 + 预期数据 + 回归脚本（CLAUDE.md §12/§13，★ 必做）
1. **COVERAGE.md**：对着被测驱动源码逐路径列测试项（每个条件分支至少一个
   测试触及），按 §13.3 模板填：`测试项 / 驱动路径 / 维度 / 预期数据 / 预期来源 /
   状态`。维度从 §13.1 清单选（功能通路/边界长度/异常参数/统计性质/并发/
   中断时序/复位掉电）。**不得拍脑袋**：先读驱动源码（`zephyr/drivers/` 对应
   `.c`），把分支点抄进矩阵再设计用例。
2. **expected/**：确定性测试（通信数据/状态字/加密向量）生成预期数据文件，
   固件 include 后逐字节比对：
   - 加密类 → 抽 NIST/官方测试向量生成 `expected/nist_vectors.h`
   - 通信类 → 固定 pattern + 状态字（预期字节序列/寄存器值）放 expected/
   - 生成脚本随工程保留（如 `tests/scripts/gen_vectors.py`），保证可复现
3. **run_<driver>_tests.py** 必须实现（样板 run_i3c_tests.py 已带）：
   - 写报告到 `810_driver_test/reports/<YYYY-MM-DD>/<driver>_test_report.{md,json}`
   - 更新当天 `reports/<date>/summary.md` 索引（§12.4，按工程名替换行）
   - 解析 INFO token 填报告"实际"列（§13.4）
   - 串口缺项用 `dump_results.py --json` 内存兜底合并（§12.3）

## 2. 生成新 case（不改 C 代码）

新 case = 新建目录 + 三件套：
```text
tests/case_<速记>/
├── test.sh          # 从样板 case 复制，改 TEST_NAME 和头注释
├── controller.overlay / controller.conf   # 差异（如 I3C 速率）
└── target.overlay / target.conf
```
共享同一份固件源码，case 差异只体现在 overlay/conf/test.sh。

## 3. 生成后自检 + 调试验证（必须做，验收标准 CLAUDE.md §11）

1. `./test.sh` 双核编译通过（零失败）
2. 确认 DebugServer 在跑（qsh-debug §2），跑 `dbg_run.py --case <新case>`
   - banner 角色识别正确（CTRL/TGT 无 `?`）
   - 每项测试 CTRL+TGT 双 PASS
3. 单 PASS 不算：target 侧也必须出 `PASS` token
4. 失败时按 token reason 定位：`--raw` 看原始日志 → 必要时 GDB 断点抓现场
   （qsh-debug §6）
5. 串口无 token/被刷掉时：`python scripts/dump_results.py --case <case> --json`
   读内存兜底取结果（§6.1 / §12.3）
6. 调通后：更新顶层 README.md 接线登记表、写问题总结存档（CLAUDE.md §7）、
   把新坑沉淀进 CLAUDE.md §10
7. 跑一次 `run_<driver>_tests.py` 一键回归，确认：
   - 报告落在 `reports/<日期>/<driver>_test_report.{md,json}`，内容含预期/实际列
   - summary.md 有本工程行（§12.4）
   - COVERAGE.md 所有测试项最终状态为 ✅（回归后对表）

## 4. 常见生成错误

| 错误 | 原因 | 修法 |
|---|---|---|
| token 不出现 | test_main 返回导致 LOG_PANIC 死窗/停摆 | `k_thread_join(K_FOREVER)` |
| ISR 里 LOG 导致时序崩 | 违反 §4.2 | 改记账，dispatcher 汇总打印 |
| IBI/HJ 卡死 | ISR 里调 i3c_ibi_raise（拿 mutex） | 经事件队列 |
| 编译过但烧录不跑 | overlay 缓存旧 SRAM 地址 | `rm -rf build/*` 全量重建 |
| 串口角色全 ? | 连接残留 / 口号错 | qsh-debug §2.3 / §4.1 |
| INFO 宏编译错（"macro passed N arguments" / format 警告） | variadic 宏把格式串与数值参数一起透传，格式串既拼进 fmt 又重复入参 → 参数错位 | 宏签名 `(name, kv, ...)`，kv 只拼接不入参：`printk(PREFIX ":TGT:INFO:%s:" kv "\n", (name), ##__VA_ARGS__)` |
| run 脚本 build 失败 "source directory -D does not exist" | west build 的 `-D` 必须连写 `-DVAR=VAL`，拆开会被当 source 目录 | `["west","build","-p","auto","-b",BOARD,"-d",build, src, f"-DDTC_OVERLAY_FILE={overlay}"]` |
| cmake 报 Invalid character escape '\z' | Windows 反斜杠路径传进 cmake 被转义 | 路径统一 `.as_posix()`（正斜杠） |
| run 脚本 ValueError: too many values to unpack | token 正则 `(TGT)` 用了捕获组，groups() 多出角色组 | 固定文本用非捕获组 `(?:TGT)`，只有变量用 `(...)` |
| run 脚本 TypeError: write_text() got unexpected keyword 'mode' | Python 3.5+ 的 Path.write_text 无 mode 参数 | 用 `with open(path, "a"/"w", encoding=...)` |

## 5. 坑沉淀要求

生成/调试过程遇到的新问题，处理方式写入对应 skill 速查表 + CLAUDE.md §10。
