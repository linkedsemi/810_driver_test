# 验证输出格式与修复建议模板——参考

> **定位**：承接 qsh-test-validate §4/§5 的完整内容。验证报告的结构化输出模板 +
> 每个 FAIL 项的自动修复建议。

## 1. 验证输出格式

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

## 2. 修复建议模板

对每个 FAIL 项，自动给出修复建议（参照 qsh-test-gen 规范）：

| FAIL 项 | 修复 |
|---------|------|
| 缺 `tests/scripts/dump_results.py` | 从样板 `i3c_dualcore_test/tests/scripts/` 复制，改 TEST_NAMES |
| 缺 `sys.path.insert(scripts/)` | `run_test.py` 顶部加 `sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))` |
| 缺 `zt_init` / `zt_run_all` | `test_main()` 首行加 `zt_init("TGT")`，末尾加 `zt_run_all()` |
| prj.conf 缺 `CONFIG_ZTEST_VERBOSE_OUTPUT=y` | 显式加上（`KNOWN_ISSUES.md` #26：=n 则逐项输出被抑制） |
| CMakeLists 缺 `test_results.c` | 加 `target_sources(app PRIVATE .../common/test_results.c)` |
| case 未在 cases.yaml 注册 | 在 `regression/cases.yaml` 对应位置加一行 |
| run_test.py 归档只有 .json 没 .md | 抄已迁移 run_test.py 的 write_markdown_report() + 归档段写 _report.md，update_summary 报告路径改指 .md |
