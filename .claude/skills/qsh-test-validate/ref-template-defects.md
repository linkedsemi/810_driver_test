# 模板缺陷防漏检查（10 项必查）——参考

> **定位**：承接 qsh-test-validate §1.9 的完整内容。以下 10 项源自新工程实机跑测
> 暴露的模板缺陷（2026-09-07/08 沉淀 a~d，2026-09-16 沉淀 e~h，2026-09-17 沉淀 i、j），
> 验证工程合规时**必须逐项检查**。

## 1. cpu2 hold 步骤（串口 0 行最高频根因）

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

## 2. overlay chosen 节点

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

## 3. 测试代码禁用堆分配

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

## 4. run_test.py 归档必须同时产出 .md + .json

2026-09-08 沉淀：这 6 个新工程实机暴露，run_test.py 归档段若只写
`<driver>_test_report.json`，summary.md 对应行就会链 .json，偏离
SPEC §6.4.1 的 `.md`+`.json` 成对要求（根因 = 模板归档段漏写 md 步骤，
非注解缺失）。

**检查方法**：
```python
# run_test.py 归档段必须写 .md（write_markdown_report 或等价 _report.md 写入），
# 且 update_summary 报告路径应指向 .md（与 json 同源）
content = run_test_py.read_text()
assert ("write_markdown_report" in content
        or "_report.md" in content), \
    f"{run_test_py.name} 归档缺 .md 报告（模板缺陷 §1.9d）"
assert "REPORT_BASE" in content, \
    f"{run_test_py.name} 缺 REPORT_BASE 定义"
```

## 5. ZTEST_SUITE 回调槽位（套件级 vs 用例级）

`ZTEST_SUITE(name, predicate, setup, before, after, teardown)` 中
**`setup`/`teardown` 是套件级（整个 suite 各跑一次），`before`/`after` 是用例级
（每例前后都跑）**。落错槽的典型形态：`setup` 正确落套件级，`teardown` 却落进
`after` → 第一个用例后就释放共享句柄 → 第二个用例拿到 NULL → 空指针崩溃 →
SoC 异常入口 `while(1)` → 串口从此静默（症状像配置/烧录问题）。

规则：**在 `setup` 里 open/init 的资源，必须在 `teardown` 里 close/deinit
（同层配对）**；用例间共享的句柄一律走套件级 setup/teardown，不要混用 after。
`before`/`after` 形参是 `void(*)(void*)`、`setup` 是 `void*(*)(void)`，类型不通，
所以"把 teardown 挪去 before/after"修不通。

**检查方法**（启发式，命中需人工确认资源配对）：
```python
import re
for src in project_dir.rglob("src/*.c"):
    for m in re.finditer(r"ZTEST_SUITE\s*\(([^;]*?)\)\s*;", src.read_text(), re.S):
        args = [a.strip() for a in m.group(1).split(",")]
        if len(args) != 6:            # name, predicate, setup, before, after, teardown
            continue
        setup, after, teardown = args[2], args[4], args[5]
        assert not re.search(r"_teardown|_cleanup|_release|_deinit", after), \
            f"{src}: 释放型函数落在 after 槽（用例级）——须挪到 teardown 槽"
        if setup != "NULL":
            assert teardown != "NULL", \
                f"{src}: setup 开了共享资源但 teardown 槽为空——须同层配对"
```

## 6. 工程分型与入口地址（单核 1os / 双核 2os）

**分型按"是否存在 cpu1 之外的固件目录（`cpu0_*` 或 `cpu2_*`）"判定**。第二核目录
命名**跨工程不统一**（`cpu0_i2c_master` / `cpu0_i3c_controller` / `cpu2_controller_mbox_test`
三种并存），**禁止用 `controller`/`cpu2` 关键词猜**——`i2c_pure_test` 第二核叫
`cpu0_i2c_master`（无 `cpu2`、无 `controller`），关键词法会把它误判成单核并给出
错误结论。权威矩阵见 SPEC_REFERENCE §6.1.10：

| 分型 | board | cpu1 入口 | 加载函数 |
|------|-------|----------|---------|
| 单核 | `lsqsh_evb@1os/lsqsh/cpu1` | `0x10000000` | `build_single_test`/`load_and_run_single` |
| 双核 | `lsqsh_evb@2os/lsqsh/{cpu1,cpu2}` | `0x100c0000` | `build_test`/`load_and_run` |

单核工程 1os 构建的 cpu1 入口 = `0x10000000`；2os 入口 = `0x100c0000`。
模板从 2os 样板抄脚本时会把 2os 入口抄进来 → `dbg_run.py` / `dump_results.py` /
`run_test.py` 的 `$pc` 设错 → 固件根本没跑 → **串口 0 行**（与"固件/配置问题"
同症状，极易误判成 0/0 报告）。反向混用（双核工程误用 `build_single_test`）会把
board 强制成 1os，两侧固件对不上（KNOWN_ISSUES #43）。

**检查方法**：
```python
# 先按目录判型：存在 cpu1 之外的固件目录（cpu0_* 或 cpu2_*）= 双核
# 取广义前缀，勿用 "controller"/"cpu2" 关键词（i2c_pure_test 第二核是 cpu0_i2c_master）
fw_dirs = [d.name for d in project_dir.iterdir() if d.is_dir()]
dual = any(d.startswith(("cpu0_", "cpu2_")) for d in fw_dirs)
expect_board = "@2os/lsqsh/cpu1" if dual else "@1os/lsqsh/cpu1"
expect_rs    = "0x100c0000" if dual else "0x10000000"

if not dual:
    for script in [dbg_run_py, dump_results_py, run_test_py]:
        content = script.read_text()
        # 入口必须走环境变量，默认 0x10000000（单核 1os）
        assert 'os.environ.get("TARGET_ROM_START"' in content, \
            f"{script.name}: TARGET_ROM_START 硬编码（单核须 os.environ.get 默认 0x10000000）"
        # 只允许注释里出现 0x100c0000 作 2os 说明；代码赋值不得硬编码
        for line in content.splitlines():
            code = line.split("#", 1)[0]
            assert "0x100c0000" not in code.lower(), \
                f"{script.name}: 硬编码 2os 入口 0x100c0000（应走 TARGET_ROM_START 环境变量）"
    assert "@1os/lsqsh/cpu1" in run_test_py.read_text(), \
        "单核 run_test.py 的 BOARD 必须 lsqsh_evb@1os/lsqsh/cpu1"

# 板级/分型一致（双向）：test.sh 调用的加载函数与分型相符
sh = "\n".join(p.read_text() for p in tests.glob("case_*/test.sh"))
assert ("build_single_test" in sh) != dual, \
    "test.sh 加载函数与分型不符（单核 build_single_test / 双核 build_test）"
```
范本 = `otbn_test` / `otbn_selftest` / `mbedtls_test`（1os board + env 入口）。

## 7. 样板脚本残留识别常量

从样板工程整体拷贝 `tests/scripts/` 后漏改识别常量，会产生错误结果或误判：

| 脚本 | 残留常量 | 后果 |
|------|---------|------|
| `dbg_run.py` | `TGT_BANNERS`/`CTRL_BANNERS`（或 `TESTS_DIR`） | 残留上一驱动 banner（如 "CPU1 TRNG"）→ 角色识别全 `?`，误报 "no firmware banner seen"（像固件没跑，实为常量没改） |
| `dump_results.py` | `TEST_NAMES` | 残留他工程测试名（如 trng 的 `basic_16/distinct_32`）→ mem 兜底读**本工程**内存槽却按**他工程名**映射 → **假 PASS**（比无输出更危险） |
| `run_test.py` | 打印/连接文案 | 报告头显示他工程名（如 flash 报告写 "ADC temper test results"） |

任一脚本出现**其他驱动/工程名**即 FAIL。此类残留只在串口不可用走 mem 兜底时才暴露，
须在生成阶段核掉（KNOWN_ISSUES #59）。

**检查方法**（工程名去掉 `_test` 作为自身关键词，如 `adc_temper_test` → `ADC_TEMPER`）：
```python
KW = driver_kw.upper()                      # 自身关键词
OTHERS = ("TRNG", "OTBN", "MBEDTLS", "FLASH", "IOPMP", "WDT", "DMA", "ADC")
for script in [dbg_run_py, dump_results_py, run_test_py]:
    for line in script.read_text().splitlines():
        code = line.split("#", 1)[0].upper()   # 注释/文案属代码外，仍应核对
        for other in OTHERS:
            if other != KW and other in code:
                print(f"WARN {script.name}: 疑似残留他工程名 '{other}'，人工确认")
```

## 8. 边界常量来源 + 缩容/兜底改动须附根因（review 类）

**边界用例的常量必须可溯源**——来自驱动源码掩码/数据手册，或（两者与实机不符时）
寄存器**写-回读实测上限**。凭"看起来安全"拍的数值会在硬件量程处**静默截断**：某寄存器
头文件声明 12 位（`MASK=0xfff`）、实机写 `0xfff` 只回读 `0x7ff`（有效 11 位）→ 单块上限
差一半，超限块长被截断/无完成中断 → 用例失败（判定法见 debug-knowledge mcu 域
"寄存器位域以实测为准"）。

**缩容 / 缩短超时 / 加兜底钩子 ≠ 修复**（判 FAIL 的形态）：
- 缩小边界参数：根因（如量程截断）未解时，大小两个值**同因同果**，缩容后仍失败；
- 缩短超时：只把"等不到完成中断"从超时表现改为更早超时，未触及根因；
- 加"每例前 stop/deinit"兜底钩子：前置条件不满足时钩子**空转**（如 `dma_stop` 在通道
  未使能时提前 `return 0`，不复位 state），既不修复也不兜底，只增噪声与误导。

**检查方法**（静态只出线索，最终由 `git diff` + 人工判定）：
```python
import re
# 线索：边界类常量应有出处（驱动源码/手册/写-回读实测），无出处的魔法数人工确认
pat = re.compile(r"\w*(SIZE|LEN|BLOCK|COUNT|MAX)\w*\s*=\s*"
                 r"(0x[0-9a-fA-F]+|\d{3,}|\d+\s*\*\s*1024)", re.I)
for src in project_dir.rglob("src/*.c"):
    for line in src.read_text().splitlines():
        if pat.search(line):
            print(f"线索 {src}: {line.strip()} —— 确认该边界值有实测/源码出处")
```
**改动面审查**：比对 `git diff`，出现"参数缩容 / 超时调小 / 新增 before 钩子调 stop"而
提交说明与注释**无根因**的，判 FAIL——先给出根因（量程截断？时序？残留状态？），
再决定改值还是改驱动。

## 9. C/H 文件块注释内不得出现 `*/` 序列

生成的头文件/固件源码在**块注释**里写"通配路径示例"（`case_*/controller.conf`、`a*/b`）
是高频写法，但 `*/` 会**提前终止注释**，其后的中文说明被当代码 → 编译报
`error: expected '=', ',', ';', 'asm' or '__attribute__' before '.' token`（列号指向注释行）。
这是"注释里写通配路径"与 C 词法的固有冲突，与工程无关，**换任何工程都会再踩**。
生成时注释内用 `case_xxx/` 代替 `case_*/`。

**检查方法**（行级启发式，命中需人工确认该 `*/` 确在块注释内）：
```python
import re
# 缺陷形态：`*/` 紧贴标识符/路径字符（`_*/c`、`a*/b`）。
# 排除合法行尾注释终止符 ` */`（前一字符为空白）与 `/*foo*/`（后一字符为行尾）。
pat = re.compile(r"[A-Za-z0-9_.\-]\*/(?=[A-Za-z0-9_.\-])")
for src in project_dir.rglob("src/*"):
    if src.suffix not in (".c", ".h"):
        continue
    for i, line in enumerate(src.read_text(errors="replace").splitlines(), 1):
        if pat.search(line):
            print(f"FAIL {src}:{i}: 注释内 `*/` 序列会提前终止注释 —— {line.strip()}")
```

## 10. build_single_test 的 `CONF_FILE` 语义（复用上游 test/sample 时）

`test_common.sh` 的 `build_single_test()`（`:344-372`）在 `<src>/prj.conf` 存在时把 conf 参数拼成
`-DCONF_FILE=<src>/prj.conf;<conf>`（**追加**）——这对"810 自建固件 + case 差异化 conf"成立，
但**复用上游 test/sample** 时不成立：上游 `zephyr/tests/subsys/fs/fat_fs_api` 需要
`CONF_FILE="prj_mmc.conf"`（**整体替换**，其 `testcase.yaml` 的 `filesystem.fat.api.mmc` 语义），
用 `build_single_test` 会带回上游 `prj.conf`（如 `CONFIG_DISK_DRIVER_FLASH=y`）→ 固件按错误分型
编译 → **编译失败**。`-DCONF_FILE` 没有"只替换不追加"的写法，`-DEXTRA_CONF_FILE` 只能追加。

规则：**复用需要整体替换 conf 的上游 test/sample** 时，本工程 `case_*/test.sh` 直接调
`west build -b "${SINGLE_BOARD}" <src> -DCONF_FILE="<上游 conf>;<810 增量 conf>"`，仍走
`${SINGLE_BOARD}` 与 `print_launch_json_single`/`load_and_run_single`（不硬编码板级、不碰公共脚本）。

**检查方法**（启发式，命中需人工确认 conf 目标）：
```python
# case 的 test.sh 若用 build_single_test 且 src 指向本工程之外的"上游 test/sample"，
# 而该上游需要整体替换 conf → FAIL（应自持 west build -DCONF_FILE="..."）
for sh in tests.glob("case_*/test.sh"):
    txt = sh.read_text()
    if "build_single_test" not in txt:
        continue
    for line in txt.splitlines():
        if "build_single_test" in line and ("zephyr/tests" in line or "zephyr/samples" in line):
            print(f"FAIL {sh}: 用 build_single_test 构建上游 test/sample —— 须确认其 conf 是否需整体替换"
                  f"（需替换则改自持 west build -DCONF_FILE=\"<上游 conf>;<增量>\"，KNOWN_ISSUES #76）")
```

