# removed-onceoff-cases —— 一次性诊断残留归档

> 归档日期：2026-10-16
> 工程：`810_driver_test/projects/i2cf_test/`
> 背景：加法做完（3 个正式 case 已转正），现做**减法** —— 删一次性诊断 case。
> **工作区不在 git 下，删除不可逆** ⇒ 本归档是唯一恢复源。

## 1. 删了什么、为什么

### 1.1 case 目录（`projects/i2cf_test/tests/` 下，整目录）

| 归档子目录 | 原路径 | 为什么删 |
|---|---|---|
| `case_scan_burst_shell/` | `tests/case_scan_burst_shell/` | 一次性诊断：把 `case_scan_burst` 的日志配置换成"客户那套"（SHELL=y + SHELL_LOG_BACKEND）复现排空劣化。结论已得，非回归 case。 |
| `case_mwrite_probe/` | `tests/case_mwrite_probe/` | 一次性探针：`MASTER_WRITE_MODE`(SMBUS_FILTER_SET.bit3) 对 read 帧的影响。只观测、无断言，结论已得。 |
| `case_quick_scan/` | `tests/case_quick_scan/` | 一次性诊断：quick command（0 长度写）扫描单笔耗时。结论已得（~0.14 ms/笔，客户 200 ms 假设未复现）。 |
| `case_wave/` | `tests/case_wave/` | 一次性波形用例：循环波形发生器供示波器抓取。非回归 case（无 zassert）。 |
| `case_abl/` | `tests/case_abl/` | 一次性消融实验：日志配置消融（SHELL/LOG 配置逐项排查）。结论已得。 |

> ⚠️ 归档**不含各 case 的 `build/` 子目录**（各 ~240–266 MB，纯构建产物，可由归档里的
> `test.sh` + `.conf` 重建）。归档只含**创作内容**（源码/脚本/conf）。

### 1.2 专属固件源（`cpu1_i2cf_test/src/`）

| 归档路径 | 原路径 |
|---|---|
| `firmware/i2cf_wave.c` | `cpu1_i2cf_test/src/i2cf_wave.c` |
| `firmware/i2cf_wave.h` | `cpu1_i2cf_test/src/i2cf_wave.h` |

仅 `CONFIG_I2CF_WAVE_GEN=y` 时编入（`case_wave` 专用），无其它引用。

### 1.3 共享文件里被删的块（见 `shared-snapshots/` 的**删除前快照**）

| 文件 | 删的块 | 保护开关 |
|---|---|---|
| `src/i2cf_test.c` | 文件尾部三个 `#ifdef` 块（含各自注释头）：`quick_scan` / `mwrite_mode_probe` / shell 命令 `scanburst` | `CONFIG_I2CF_QUICK_SCAN` / `CONFIG_I2CF_MWRITE_PROBE` / `CONFIG_I2CF_SCAN_BURST_SHELL && CONFIG_SHELL` |
| `src/main.c` | `#include "i2cf_wave.h"` + `#if defined(CONFIG_I2CF_WAVE_GEN)` 分支 | `CONFIG_I2CF_WAVE_GEN` |
| `Kconfig` | `config I2CF_QUICK_SCAN` / `I2CF_MWRITE_PROBE` / `I2CF_WAVE_GEN` / `I2CF_SCAN_BURST_SHELL` 四个块 | —— |
| `CMakeLists.txt` | 两个 `if(CONFIG_I2CF_WAVE_GEN)` 块（gpio_toggle HEADER_FILE_ONLY + `target_sources(src/i2cf_wave.c)`） | `CONFIG_I2CF_WAVE_GEN` |

## 2. 坚决**没删**的（保留项）

- 三个正式 case 目录：`tests/case_default/`、`tests/case_enum_matrix/`、`tests/case_scan_burst/`
- `cpu1_i2cf_test/src/i2cf_enum.c`（`case_enum_matrix` 固件源）
- `src/i2cf_test.c` 里 `CONFIG_I2CF_SCAN_BURST` 保护的 `ZTEST(i2cf, scan_burst_drain)` 块（`case_scan_burst` 固件源）
- `tests/cases.yaml` 三个正式 case 登记、`tests/run_test.py` 的 `TEST_NAMES_BY_CASE`、`tests/scripts/dump_results.py` 的对应项表
- 正式 case 的 `case.conf` 命名约定
- **★ 未删（不在本次删除清单内）：`case_filteroff_probe`** —— 它也是"一次性诊断残留"
  （`CONFIG_I2CF_FILTEROFF_PROBE` + `i2cf_test.c` 对应块 + `tests/case_filteroff_probe/`），
  但用户给的删除清单未列它 ⇒ **按纪律未删，如实报告**（详见随附报告）。

## 3. 如何恢复

```bash
cd d:/zephyr4_0/810_driver_test
DST=reports/2026-10-16-1/removed-onceoff-cases

# ① case 目录（不含 build/，build 由 test.sh 重建）
for c in case_scan_burst_shell case_mwrite_probe case_quick_scan case_wave case_abl; do
  mkdir -p projects/i2cf_test/tests/$c
  (cd "$DST/$c" && tar -cf - .) | tar -xf - -C projects/i2cf_test/tests/$c
done

# ② 固件源
cp "$DST/firmware/i2cf_wave.c" "$DST/firmware/i2cf_wave.h" projects/i2cf_test/cpu1_i2cf_test/src/

# ③ 共享文件回滚（整文件，含被删块）
cp "$DST/shared-snapshots/i2cf_test.c" "$DST/shared-snapshots/main.c" projects/i2cf_test/cpu1_i2cf_test/src/
cp "$DST/shared-snapshots/Kconfig" "$DST/shared-snapshots/CMakeLists.txt" projects/i2cf_test/cpu1_i2cf_test/
```

> 共享文件 `shared-snapshots/*` 是**删除前**逐字节快照（md5 已核）。若只想恢复单个块，
> 从快照里摘取对应 `#ifdef ... #endif` 区段即可（块边界见 §1.3 与各块注释头）。
