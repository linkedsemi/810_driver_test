# 模板缺陷防漏检查（4 项必查）——参考

> **定位**：承接 qsh-test-validate §1.9 的完整内容。以下 4 项源自 6 个新工程实机跑测
> 暴露的模板缺陷（2026-09-07/08 沉淀），验证工程合规时**必须逐项检查**。

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
