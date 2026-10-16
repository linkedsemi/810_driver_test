# case_scan_burst —— 扫描回归用例（正式）

> 覆盖：空白名单 + filter on 下逐址扫描 0x08..0x77，**三种事务类型**逐一断言
> 「每个被拦地址恰好产生一次上报」。已登记进 `tests/cases.yaml` 与
> `run_test.py` 的 `TEST_NAMES_BY_CASE`。

## 测什么

固件 `i2cf::scan_burst_drain` 对三种事务类型各跑一轮 112 址扫描（0x08..0x77）：

| 类型 | 事务 | 用途 |
|---|---|---|
| `write1` | `i2c_write(up, &v, 1, addr)` | 基线对照 |
| `quick0` | `i2c_write(up, NULL, 0, addr)` | **真正的 i2cdetect / Zephyr `i2c scan` 形态**（`i2c_shell.c` 用 `msgs[0].len=0U`） |
| `read` | `i2c_read(up, &v, 1, addr)` | 复核"读/写方向不影响上报"（enum 矩阵已证） |

每轮断言：

- `isr == probed`（112）—— 每址恰好一条 `ADDRESS_BEYOND_WHITELIST` 上报；
- `elapsed_ms < 3000` —— 扫描本身耗时的粗上界（实测 ≈16 ms）。

## 背景（为什么保留 BEGIN/END marker）

用户曾反馈「开 filter 后外部 `i2cdetect` 扫描，ERR 日志一条条慢慢打印，20+ 秒才出完」，
而扫描本身很快。**「打印慢」是 log 线程驱逐的耗时，只在采集侧按到达时间可见** ——
固件侧只能测入队时刻（日志行自带时间戳全是 `(+0)` 紧发），测不到它。

固件打两条 marker：

| marker | 含义 |
|---|---|
| `SCAN_BURST_BEGIN mode=.. addr=0x8..0x77` | 扫描开始，**计时起点** |
| `SCAN_BURST_END mode=.. probed=.. elapsed_ms=.. isr=..` | 扫描已结束；**排在 112 条 ERR 之后入队** |

⇒ `END` 到达时刻 − `BEGIN` 到达时刻 = **日志驱逐跨度**。

⚠️ **该跨度随 UART / 日志后端 / 平台变化**（810 实测 ~1.0–1.4 s；客户 shell 日志后端
场景约 22 s）⇒ **只作观测上报，绝不写成硬断言**（写死必然假 FAIL）。带到达时间戳的
采集可用 `python scripts/dbg_run.py --case case_scan_burst --timeout 90 --raw`
（`--raw` 每行前缀 `+<相对首行的秒数>`，由本工程私有的 `tests/scripts/dbg_run.py` 提供）。

## 怎么跑

```bash
cd 810_driver_test/projects/i2cf_test/tests
python scripts/dbg_run.py --case case_scan_burst --timeout 60        # 调试
python run_test.py --case case_scan_burst                            # 正式回归（归档）
```

## 实现要点

| 坑 | 说明 |
|---|---|
| **应用目录 `Kconfig` 会改 `KCONFIG_ROOT`** | 必须首行 `source "Kconfig.zephyr"`，否则整棵 Kconfig 树丢失 → `I2C_FILTER_LINKEDSEMI`/`I2C_TARGET` 等全变未定义、Kconfig 报错中止（已实测踩到）|
| `CONF_FILE` 相对路径**按 app 目录解析** | 手工 `west build` 时要用**绝对路径**；走 `test.sh` 则由 `test_common.sh` 处理 |
| `case_default` 必须不受影响 | `case_default` 无 `case.conf` 时 `CONFIG_I2CF_SCAN_BURST` **未定义**、0 个 Kconfig warning、构建正常 |
| 上报数量以 **ISR 计数**为准，不数日志 | 112+ 条 ERR 会撑满 16 KB log ring、部分行被静默丢弃；ISR 计数不会丢 |
