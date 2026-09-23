# case_scan_burst —— 一次性诊断（非回归用例）

> ⚠️ **本 case 只用于验证一个用户反馈的问题，不登记进 `regression/cases.yaml`，
> 用完可整体删除**（删除方法见文末）。

## 反馈的现象

> 开启 i2c filter 后，外部进行 `i2cdetect -y 5` 扫描，日志中的
> `[1970-01-01 01:24:13.861800(+0)] <err> i2c_pfr_filter: 2cf@40091000: address beyond whitelist: i2c@0x73`
> **打印很慢，要 20 多秒才打印完**。

补充（用户澄清）：**扫描本身很快就完成**了，是日志**一条条**慢慢出，不是一股脑打出来。

## 为什么不能只看固件时间戳

- 固件侧能测的只有**入队时刻** —— 就是日志行自带的时间戳，所以全是 `(+0)`（紧发）。
- 「打印慢」是 **log 线程驱逐（drain）**的耗时，**只在到达时间上可见**。

## 测量原理

固件 `i2cf::scan_burst_drain` 打两条 marker：

| marker | 含义 |
|---|---|
| `SCAN_BURST_BEGIN addr=0x8..0x77` | 扫描开始，**计时起点** |
| `SCAN_BURST_END probed=.. elapsed_ms=.. isr=..` | 扫描已结束；**它排在 112 条 ERR 之后入队** |

⇒ **`END` 的到达时刻 − `BEGIN` 的到达时刻 = 日志驱逐跨度**（≈用户说的 20 秒）。

固件侧另有两个量（`ZT_INFO` 与 END 行里）：

- `probed` = 探测的地址数（112）
- `elapsed_ms` = **扫描本身**耗时（用户说"很快" ⇒ 这个数应该小）
- `isr` = 被拦事件数（断言 == `probed`，即"每址一行 ERR"的来源）

## 怎么跑

```bash
cd 810_driver_test/projects/i2cf_test/tests
python scripts/dbg_run.py --case case_scan_burst --timeout 90 --raw
```

`--raw` 每行前缀 `+<相对首行的秒数>` —— 该时间戳由**本工程私有的** `tests/scripts/dbg_run.py`
提供（加了平行的 `times` 列表 + 该前缀，**未改 `common/`**；`dbg_run.py` 是每工程一份，改动不外溢）。

**看什么**：`SCAN_BURST_BEGIN` 与 `SCAN_BURST_END` 两行的 `+秒数` 之差。

## A/B 对照（可选）

`scan_burst.conf` 里 `CONFIG_I2CF_SCAN_BURST=y` 不变的前提下，把固件里的
`i2cf_filter_on(dev)` 换成 `i2cf_filter_off(dev)` 再跑一次 —— 此时无被拦事件、无 ERR 行，
`BEGIN→END` 的跨度应≈扫描本身耗时。两者之比即"日志/拦截的代价"。

## 实现要点（生成时踩到的）

| 坑 | 说明 |
|---|---|
| **应用目录 `Kconfig` 会改 `KCONFIG_ROOT`** | 必须首行 `source "Kconfig.zephyr"`，否则整棵 Kconfig 树丢失 → `I2C_FILTER_LINKEDSEMI`/`I2C_TARGET` 等全变未定义、Kconfig 报错中止（已实测踩到）|
| `CONF_FILE` 相对路径**按 app 目录解析** | 手工 `west build` 时要用**绝对路径**；走 `test.sh` 则由 `test_common.sh` 处理 |
| `case_default` 必须不受影响 | 已验：不加 `scan_burst.conf` 时 `CONFIG_I2CF_SCAN_BURST` **未定义**、0 个 Kconfig warning、构建正常 |

## 怎么删（用完清理）

```bash
cd 810_driver_test/projects/i2cf_test
rm -rf tests/case_scan_burst                    # ① 本目录
rm -f  cpu1_i2cf_test/Kconfig                   # ② 应用 Kconfig（删前确认没别的私有符号）
# ③ 删 cpu1_i2cf_test/src/i2cf_test.c 末尾的 `#ifdef CONFIG_I2CF_SCAN_BURST … #endif` 整块
# ④ 可选：还原 tests/scripts/dbg_run.py 的到达时间戳改动（git checkout 即可）
```

**不用改** `regression/cases.yaml` / `CLAUDE.md` / `README.md` —— 本 case 从未登记。
