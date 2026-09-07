---
name: qsh-test-validate
description: 810_driver_test 测试工程合规性验证 skill：检查新建/修改的工程是否符合规范（目录结构、命名、prj.conf、公共模块注入、sys.path、ztest_glue、构建通过）。新建工程后、修改工程后、或回归前预检时调用。
---

# QSH 测试工程合规性验证

目标：**在跑回归之前，先验证工程本身是否合法**——结构对、配置对、编译过。
规范依据：`810_driver_test/CLAUDE.md` + `qsh-test-gen` SKILL.md。
样板对照：`810_driver_test/projects/i3c_dualcore_test/`（双核样板）、`trng_test/`（单核样板）。

## 0. 用法

```bash
# 在 agent 中调用：指定工程名或路径
# 例：验证 otbn_test 工程
# → 自动检查全部维度 + 尝试编译每个 case

# 可单独检查某维度（跳过编译）：
# → 只做静态检查（结构/配置/代码模式）

# 全量验证（静态 + 编译）：
# → 静态检查 + west build 每个 case
```

## 1. 检查维度清单

### 1.1 目录结构（必查）

| 检查项 | 单核工程 | 双核工程 | 判定 |
|--------|---------|---------|------|
| `tests/` 目录存在 | ✅ | ✅ | 必须 |
| `tests/case_*/` 至少一个 | ✅ | ✅ | 必须 |
| `tests/case_*/test.sh` 每 case | ✅ | ✅ | 必须 |
| `tests/scripts/run_test.py` 或 `tests/run_test.py` | ✅ | ✅ | 必须（一键回归入口） |
| `tests/scripts/dump_results.py` | ✅ | ✅ | 必须（GDB 内存兜底） |
| `tests/scripts/dbg_run.py` | ✅ | ✅ | 必须（单次调试） |
| `tests/COVERAGE.md` | ✅ | ✅ | 必须（覆盖矩阵） |
| `cpu1_*/` 固件目录 | ✅ | ✅ | 必须 |
| `cpu0_*_controller/` 或 `cpu2_*/` | ❌ | ✅ | 双核必须 |
| `common/test_results.{h,c}` 引用 | ✅ | ✅ | 必须（公共结果模块） |
| `common/ztest_glue.c` 引用 | ✅ | ✅ | 必须（自动记账） |

**检查方法**：
```python
# 伪代码
project_dir = Path("810_driver_test/projects/<driver>_test")
assert (project_dir / "tests").exists()
for case_dir in (project_dir / "tests").glob("case_*"):
    assert (case_dir / "test.sh").exists(), f"{case_dir} 缺 test.sh"
```

### 1.2 命名规范（必查）

| 检查项 | 规则 | 示例 |
|--------|------|------|
| 工程目录名 | `<driver>_test` | `otbn_test`, `i3c_dualcore_test` |
| case 目录名 | `case_<速记>` | `case_pure_i3c_12m5`, `case_default` |
| I3C 角色术语 | `controller` / `target` | `cpu0_i3c_controller/` |
| I2C/SPI/UART 角色术语 | `master` / `slave` | `cpu0_spi_master/` |
| 纯片内单核 | `cpu1_<driver>` | `cpu1_trng_test/` |
| **禁用混搭** | 不得 `host_master` / `device_slave` | — |

**检查方法**：
```python
I3C_ROLES = {"controller", "target"}
I2C_SPI_ROLES = {"master", "slave"}
# 从工程类型推断期望角色词
# 扫描 cpu*_ 目录前缀验证
```

### 1.3 prj.conf 配置（必查）

所有 case 的 `prj.conf`（含 overlay 注入的）必须包含：

```kconfig
# ztest 逐项输出（§10.26，=n 则 START/PASS/FAIL 行被抑制）
CONFIG_ZTEST_VERBOSE_OUTPUT=y

# log 规范（§6.1.7）
CONFIG_LOG=y
CONFIG_LOG_MODE_DEFERRED=y
CONFIG_LOG_BUFFER_SIZE=16384
CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=10
CONFIG_LOG_BACKEND_UART=y
CONFIG_LOG_PRINTK=n

# ztest 基础
CONFIG_ZTEST=y

# ★ NEWLIB 必须关闭（§10.35：=y 与 Zephyr POSIX types 冲突）
CONFIG_NEWLIB_LIBC=n
```

**检查方法**：解析 prj.conf（简单 `key=value` 格式），逐项比对。
注意：部分配置可能在 board defconfig 中，prj.conf 未显式写不算错——只检查**显式写了但值错**的项。

### 1.4 CMakeLists.txt 公共模块注入（必查）

每个固件的 `CMakeLists.txt` 必须包含：

```cmake
# 公共结果模块
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/test_results.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
# 或用 ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/ 取决于目录深度

# ztest_glue（自动记账）
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/ztest_glue.c)

# 双核 cpu2 侧还需要 overlay 注入：
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)
```

**检查方法**：grep CMakeLists.txt 中的 `test_results.c`、`ztest_glue.c`、`cpu2_console_uart5.overlay`。

### 1.4a 复用收敛检查（必查，SPEC_REFERENCE §6.1.9）

凡公共层已有等价物的内容，工程不得私拷副本：

| 检查项 | 规则 | 检查方法 |
|---|---|---|
| overlay 公共片段 | app.overlay 不得手写 `&gpioa { status` 使能块；cpu1 侧（单核+双核 target）应 `#include` `common/gpio_toggle.dtsi`，cpu2 侧不得包含 | `grep -n "&gpio" <固件>/app.overlay`：cpu1 侧命中=FAIL（应走 dtsi include），cpu2 侧命中=FAIL |
| 公共 C 源 | 不得单独 `target_sources` test_results.c/ztest_glue.c/gpio_toggle.c，必须走 `common_sources.cmake` | grep CMakeLists 直接点名这三个文件=FAIL（common_sources.cmake include 除外） |
| 私有 results 数组 | 禁止固件私有 results 数组（原有检查项，归并到本原则） | grep 源码内 `test_result` 自定义数组 |
| 脚本私拷 | 新增通用脚本必须入公共层/scripts 样板，不得工程内另起等价脚本 | 人工 review |

### 1.5 run_test.py sys.path 设置（必查）

`run_test.py` 必须在顶部（import dump_results 之前）设置：

```python
sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))          # ztest_report, report_dir
sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))  # dump_results
```

**检查方法**：
```python
content = run_test_py.read_text()
assert "common" in content and "sys.path" in content
# 检查 scripts/ 是否在 sys.path 中（或用 subprocess 调 dump_results.py）
has_scripts_path = 'scripts' in content and 'sys.path' in content
# 如果用 subprocess 调 dump_results 则不需要 scripts 路径
uses_subprocess_dump = 'dump_results.py' in content and 'subprocess' in content
assert has_scripts_path or uses_subprocess_dump
```

### 1.6 ztest_glue 集成模式（必查）

固件 `main.c` / `test_main()` 必须遵循：

```c
void test_main(void) {
    zt_init("TGT");           // 第一行：打印 ZT_BOOT + tr_init
    // ... 业务线程（被动驱动型需 k_thread_join）...
    zt_run_all();             // 末尾：ztest_run_all + 自动记账
}
```

**检查方法**：grep 固件 `main.c` / `test_main` 中的 `zt_init` 和 `zt_run_all`。

### 1.7 test.sh 三模式（必查）

每个 case 的 `test.sh` 必须支持三种模式：
- `./test.sh` — 只编译（`SKIP_LOAD=y`），打印 launch.json 片段
- `./test.sh -g` — 编译 + GDB 加载运行
- `./test.sh -r` — 仅加载运行（agent 模式）

**检查方法**：grep test.sh 中的 `-g` / `-r` 参数处理、`SKIP_LOAD` 变量。

### 1.8 dump_results.py TEST_NAMES 对齐（建议查）

`dump_results.py` 的 `TEST_NAMES` 列表必须按 ztest_unit_test **链接序**（SORT_BY_NAME）排列，与 ztest_glue 记账 idx 对齐。

**检查方法**：若已编译，用 `nm --numeric-sort <elf> | grep _ztest_unit_test` 对比 TEST_NAMES 顺序。

### 1.9 模板缺陷防漏检查（必查，2026-09-07 沉淀）

以下 3 项源自 6 个新工程实机跑测暴露的模板缺陷，**必须逐项检查**：

#### 1.9a cpu2 hold 步骤（串口 0 行最高频根因）

dbg_run.py / run_test.py 的 gdb 命令序列必须包含：
```
set {int}0x4002200c = 2
```
此命令在 `reset` 前执行，保持 cpu2 复位防止其从 flash 自启劫持 uart1。

**检查方法**：
```python
for script in [dbg_run_py, run_test_py]:
    content = script.read_text()
    assert "0x4002200c" in content, f"{script.name} 缺 cpu2 hold 步骤"
```

#### 1.9b overlay chosen 节点

单核工程的 overlay 必须显式声明 console chosen 节点：
```dts
chosen {
    zephyr,console = &uart1;
};
```
双核 cpu2 侧走 `cpu2_console_uart5.overlay`（公共层），不需额外声明。

**检查方法**：
```python
for case_dir in tests.glob("case_*"):
    overlay = case_dir / "controller.overlay"  # 单核工程
    if overlay.exists():
        content = overlay.read_text()
        assert "zephyr,console" in content, f"{overlay} 缺 chosen 节点"
```

#### 1.9c 测试代码禁用堆分配

测试源码（`src/*.c`）不得调用 `k_malloc` / `k_calloc` / `k_realloc`，
改用静态数组（`static uint8_t buf[N]`）。堆依赖极具迷惑性：
k_malloc 返回 NULL → memset(NULL) → store fault → 系统挂死 →
串口表现为 0 行（看似 console/log 配置问题，实际是崩溃）。

**检查方法**：
```python
for src in project_dir.rglob("src/*.c"):
    content = src.read_text()
    for bad in ["k_malloc", "k_calloc", "k_realloc"]:
        assert bad not in content, \
            f"{src} 用了 {bad}（堆分配），改用静态数组"
```

## 2. 编译验证（必做）

对每个 `case_*` 目录执行 `west build`：

```bash
# 单核 1os
west build -p auto -b lsqsh_evb@1os/lsqsh/cpu1 -d <case>/build <src> \
    -DDTC_OVERLAY_FILE=<overlay>

# 双核 cpu1
west build -p auto -b lsqsh_evb@2os/lsqsh/cpu1 -d <case>/build_cpu1 <src> \
    -DDTC_OVERLAY_FILE=<overlay>

# 双核 cpu2
west build -p auto -b lsqsh_evb@2os/lsqsh/cpu2 -d <case>/build_cpu2 <src> \
    -DDTC_OVERLAY_FILE=<overlay>
```

**判定**：编译 rc=0 且无 error/warning（或只容忍已知无害 warning）。

## 3. cases.yaml 集成检查（建议查）

若工程应加入全量回归，检查 `regression/cases.yaml` 中是否有对应条目：

```python
cases_yaml = Path("810_driver_test/regression/cases.yaml").read_text()
for case_dir in project_tests.glob("case_*"):
    case_rel = f"projects\\{project_name}\\tests\\{case_dir.name}"
    case_rel_fwd = case_rel.replace("\\", "/")
    assert case_rel in cases_yaml or case_rel_fwd in cases_yaml, \
        f"{case_dir.name} 未在 cases.yaml 中注册"
```

## 4. 验证输出格式

验证结果输出为结构化报告：

```markdown
# <driver>_test 合规性验证报告
- 日期: YYYY-MM-DD HH:MM
- 工程: projects/<driver>_test
- 类型: 单核/双核

## 静态检查
| 维度 | 结果 | 详情 |
|------|------|------|
| 目录结构 | ✅ PASS | 所有必需文件/目录存在 |
| 命名规范 | ✅ PASS | 角色术语正确 |
| prj.conf | ⚠️ WARN | case_xxx 缺 CONFIG_LOG_BUFFER_SIZE |
| CMakeLists | ✅ PASS | 公共模块注入完整 |
| sys.path | ❌ FAIL | run_test.py 缺 scripts/ 路径（P0 崩溃风险） |
| ztest_glue | ✅ PASS | zt_init + zt_run_all |
| test.sh | ✅ PASS | 三模式支持 |
| cases.yaml | ⚠️ WARN | case_xxx 未注册 |

## 编译验证
| Case | Board | 结果 | 耗时 | 备注 |
|------|-------|------|------|------|
| case_default | 1os/cpu1 | ✅ | 12s | |
| case_default | 2os/cpu1 | ✅ | 15s | |
| case_default | 2os/cpu2 | ✅ | 10s | |

## 总结
- 静态检查: 6/8 PASS, 1 WARN, 1 FAIL
- 编译: 3/3 PASS
- 建议: 修复 sys.path 后再跑回归
```

## 5. 修复建议模板

对每个 FAIL 项，自动给出修复建议（参照 qsh-test-gen 规范）：

| FAIL 项 | 修复 |
|---------|------|
| 缺 `tests/scripts/dump_results.py` | 从样板 `i3c_dualcore_test/tests/scripts/` 复制，改 TEST_NAMES |
| 缺 `sys.path.insert(scripts/)` | `run_test.py` 顶部加 `sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))` |
| 缺 `zt_init` / `zt_run_all` | `test_main()` 首行加 `zt_init("TGT")`，末尾加 `zt_run_all()` |
| prj.conf 缺 `CONFIG_ZTEST_VERBOSE_OUTPUT=y` | 显式加上（§10.26：=n 则逐项输出被抑制） |
| CMakeLists 缺 `test_results.c` | 加 `target_sources(app PRIVATE .../common/test_results.c)` |
| case 未在 cases.yaml 注册 | 在 `regression/cases.yaml` 对应位置加一行 |

## 6. 典型验证场景

| 场景 | 调用方式 | 检查范围 |
|------|---------|---------|
| 新工程生成后自检 | 全量（静态+编译） | 所有维度 |
| 修改 prj.conf/overlay 后 | 静态+编译受影响 case | prj.conf + 编译 |
| 回归前预检 | 静态快速扫 | 目录+配置+sys.path |
| 排查回归失败 | 针对 FAIL case | 编译+配置+glue 集成 |

## → 下一步（闭环链）

| 完成状态 | 下一个 skill | 触发条件 |
|---------|------------|---------|
| 全部 PASS | ★ qsh-test-run（首次验证） | 自动转 |
| 存在 FAIL 项 | 回 qsh-test-gen 修 | FAIL 标注具体修复项 |
| 存在 WARN 项 | 记录后继续 | 非阻塞 |
