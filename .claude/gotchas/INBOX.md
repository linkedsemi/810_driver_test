# Gotcha 收件箱

> 撞坑时随手记。keeper 定期扫描，对比 agent 输出 vs 人期望，提出 skill 改进。
> 处理后从收件箱删除。

## 待处理

（空）

（以下为已处理索引）
> keeper 处理后**移出待处理**，此处只留一行索引（**不可再作动作项**）。完整沉淀见对应文件。
> **日期**列 = 本次沉淀日期；条目自带日期见正文（正文已删，事实全在归处文档内）。

| 日期 | 条目 | 沉淀归处 |
|---|---|---|
| 2026-09-17 | G-P2-1 单核加载写 `APP_CPU_SRST=2`（保持 cpu2 复位）⇒ `is_app_cpu_running()` 返回 true ⇒ 带该守卫的驱动 API 全 `-EBUSY` | KNOWN_ISSUES #75 · qsh-debug §7 · qsh-test-gen §1.0（#38 加副作用交叉引用） |
| 2026-09-17 | G-P2-2 `build_single_test` 的 `CONF_FILE` 是追加语义，无法整体替换上游 test 的 conf | KNOWN_ISSUES #76 · qsh-test-gen §1.5 · qsh-test-validate §1.9j + ref-template-defects §10 |
| 2026-09-17 | G-P2-3 `regression/run_all.py` 的 `PROJECT_SCRIPT` 硬编码 → 新工程只改 `cases.yaml` 会静默 SKIP | SPEC_REFERENCE §6.4.5 同步清单第 5 步 · qsh-test-gen §3.8 |
| 2026-09-17 | G-P4-1 `ZTEST(suite,name)` 实体函数名 = `<suite>_<name>`，会与同名驱动导出符号撞型 | qsh-test-gen §1.4 第 9 条 · qsh-test-validate §1.2 · ref-gen-errors |
| 2026-09-17 | G-P4-2 未注册模块可用 `set(EXTRA_ZEPHYR_MODULES ...)` 写在应用 CMakeLists.txt（免 CLI 参数） | KNOWN_ISSUES #77 · qsh-test-gen §1.5 · ref-gen-errors |
| 2026-09-17 | G-P4-3 `pfr_tpm` 的 `spif_add_cmd()` 过期索引 → 表外野写（P0，只记录不修改） | KNOWN_ISSUES #78 · qsh-driver-review §1.8 · qsh-test-gen §1.6 |
| 2026-09-18 | G-P3-2 `CONFIG_NET_CONFIG_INIT_TIMEOUT` 默认 30 s ⇒ 网线未插时 ztest 开机阻塞 | KNOWN_ISSUES #89 · qsh-test-gen §1.7 · qsh-test-validate §1.3（WARN）· ref-gen-errors |
| 2026-09-18 | G-P3-3 Zephyr ≥4.0 无 `net/arp.h`，ARP 判据只能间接做 | KNOWN_ISSUES #90 · qsh-test-gen §1.7 |
| 2026-09-18 | G-P3-4 `build_single_test` 固定 `-p always`，与"优先 `-p auto`"冲突 | 已由 G-FW-1 的 3 个构建注入开关消解 · qsh-test-gen §1.5 |
| 2026-09-18 | G-P3-5 弱/强判据混放同一 case ⇒ 屏蔽口径被强判据"绑架" | qsh-test-gen §1.6 第 6 条（按"是否需要外部条件"切 case） |
| 2026-09-18 | G-P4.5-1 `spi1/2`=`spi_ls.c`、`spi3/4`=`spi_dw.c`（实例归属看 compatible，不看号段） | KNOWN_ISSUES #82 · debug-knowledge `mcu/peripheral-capability-evidence.md` |
| 2026-09-18 | G-P4.5-2 lsqsh 所有 GPIO 端口默认 `disabled` ⇒ CS 在非 GPIOA 端口时设备静默不就绪 | KNOWN_ISSUES #81 · qsh-test-gen §1.0 · qsh-test-validate §1.10q（WARN） |
| 2026-09-18 | G-P4.5-3 `qsh-test-validate` §1.4a 的 `grep "&gpio"` 检查方法过宽（必然误报） | qsh-test-validate §1.4a 判据收窄为 `grep -nE "&gpioa\b"`（含 io_probe_test 复现实例） |
| 2026-09-18 | G-P4.5-4 `spi_ls.c` 缺陷簇（P0 NULL 解引用 / 同指针重配静默改频 / 分频循环越界） | KNOWN_ISSUES #80 · qsh-driver-review §1.2 |
| 2026-09-18 | G-P4-4 pfr_tpm 的 `pinctrl-noinit`：配对 I2C 用作上游 master 时必须删掉 | KNOWN_ISSUES #83 · qsh-test-gen §1.8 · debug-knowledge `mcu/paired-controller-pin-ownership.md` |
| 2026-09-18 | G-P4-5 I2C filter 的"配对控制器"与"上游 master"是两个角色，不可互相推断 | qsh-test-gen §1.8 · debug-knowledge `mcu/paired-controller-pin-ownership.md` |
| 2026-09-18 | G-P4.7-1 SPIF 切 master 时必须清空配对 DW SPI 控制器的 `pinctrl-0` | KNOWN_ISSUES #83 · qsh-test-gen §1.8 · debug-knowledge `mcu/paired-controller-pin-ownership.md` |
| 2026-09-18 | G-P4.6-1 使能 DW SPI（`spi3`/`spi4`）必须同时带 `&dmac2` + `timing-calibration-disabled` | KNOWN_ISSUES #82 · qsh-test-gen §1.8 · qsh-test-validate §1.10p（WARN）· ref-gen-errors |
| 2026-09-18 | G-P4.6-2 "`spi3` 没有 NSS/片选复用"**结论错误**（见 G-C1-1 行） | 事实已按 G-C1-1 更正后并入 KNOWN_ISSUES #82 |
| 2026-09-18 | G-FW-1 `test_common.sh` 新增 3 个构建注入开关（`EXTRA_CMAKE_ARGS` / `BOARD_OVERRIDE` / `CONF_MODE`） | qsh-test-gen §1.5 · SPEC_REFERENCE §6.4.5 第 5/6 步 · qsh-test-validate §1.10o |
| 2026-09-18 | G-P6-1 内存兜底把"固件没跑"读成"全项 PASS"（`TR_PASS == 0`） | 已由 G-D1 哨兵门修复 · qsh-test-compare §2 · qsh-test-run §4.2 |
| 2026-09-18 | G-P6-2 双核工程漏登记 `run_all.py` 的 `DUAL_CORE` ⇒ 三个 case 恒 0/0 | qsh-test-gen §3.8 · SPEC_REFERENCE §6.4.5 第 6 步 |
| 2026-09-18 | G-P6-3 工程 `run_test.py` 两个登记期缺陷（`SerialCapture.baud` 未赋值 / `test_names` 硬编码） | ref-gen-errors（两条）· qsh-test-validate §1.10n |
| 2026-09-18 | G-P6-4 `i3c_dualcore_test case_mixed_400k_6m25` 实测 flaky（≈1/4） | KNOWN_ISSUES #87 |
| 2026-09-18 | G-P6-5 `ZTEST_SUITE` 第 5/6 形参（after / teardown）用法不一致 ⇒ 项间清理易误判 | qsh-test-gen §1.4 第 8 条 · qsh-test-validate §1.10l · KNOWN_ISSUES #92（含 `wdt_iwdg_ls` 守卫真因） |
| 2026-09-18 | G-B3-1 `spi-ctrl-caps-mask` 是 **disable 掩码** —— 更正 G-C1C2-2 的"上电即进 QPI 4-4-4" | KNOWN_ISSUES #84 · debug-knowledge `mcu/peripheral-capability-evidence.md` |
| 2026-09-18 | G-B3-2 验证重建 ELF 时 `strings` 只出 ASCII，中文断言串会漏判 | debug-knowledge `common/binary-grep-vs-strings.md` |
| 2026-09-18 | G-B3-3 同一 workspace 内出现并发 `west build`（另一会话在构建同一批目录） | qsh-test-validate §2（开工/收尾进程检查 + 收尾重验 ELF） |
| 2026-09-18 | G-C1C2-1 硬件不在线时误判为"连接问题"（判据 = CKLink 是否被 USB 枚举） | qsh-debug §2.2a + §7 |
| 2026-09-18 | G-C1C2-2 裸 `spi_nor_op` 用例必须适配器件运行态，且必须断言内容 | KNOWN_ISSUES #84（QPI 前提经 G-B3-1 更正，保留"4 字节地址"与"raw 必须断言内容"两条）· qsh-test-gen §1.6 |
| 2026-09-18 | G-C1-1 纠正 G-P4.6-2：`spi3`(DW SPI) **有**片选复用宏（`CSN0/CSN1`），当时 grep 漏了 `CSN` | KNOWN_ISSUES #82 · debug-knowledge `mcu/peripheral-capability-evidence.md`（关键字全集纪律） |
| 2026-09-18 | G-C1-2 `spi_loopback.c` 的 `spi_config` 对象全零，而为它准备的宏定义了却从未使用 | debug-knowledge `mcu/dead-config-macro-smoke-gun.md` · qsh-test-gen §1.6 |
| 2026-09-18 | G-C1-3 `spi_ls.c:359-360` 同步路径重复 complete（P0，只记录不修改） | KNOWN_ISSUES #80 · qsh-driver-review §1.9（新检查维度） |
| 2026-09-18 | G-D1 内存兜底假 PASS 已按哨兵门修复（关闭 G-P6-1 的建议项） | qsh-test-compare §2（`mem_state` 三态）· qsh-test-run §4.2 · qsh-test-gen §1.0 |
| 2026-09-18 | G-D2 `TEST_NAMES` / `test_names` 与固件 `ZTEST` 数漂移 ⇒ 报告静默漏项 | qsh-test-validate §1.10n · ref-gen-errors |
| 2026-09-18 | G-D3 脚本类缺陷簇（双核 dump 错归属 / `run::timeout` 恒真 / SKIP 计入分母） | qsh-test-gen §1.6 第 8 条 · qsh-test-run §6 · qsh-test-compare §2 |
| 2026-09-18 | G-F1-1 "恒真断言"空壳用例族（驱动恒返回 0 时断言不可能 FAIL） | qsh-test-validate §1.10m（WARN）· qsh-test-gen §1.6 第 4/5 条（判据三问 + 用例自包含） |
| 2026-09-18 | G-F1-2 复刻驱动内部 `dev->data` 结构体做只读观测的纪律 | qsh-test-gen §1.6 第 7 条（只断显式赋值字段） |
| 2026-09-18 | G-FRAME-1 `run_all.py` 的 i3c `--skip-build` 注释与事实相反（per-case 构建粒度） | KNOWN_ISSUES #88 · qsh-test-run §6 · SPEC_REFERENCE §6.4.5（§6.4.5 正文早于本条已有勘误） |
| 2026-09-18 | G-FRAME-2 "执行窗口到点"本身不是失败证据（判据只看证据，不看窗口） | qsh-test-compare §2 · qsh-test-run §5/§6 |
| 2026-09-18 | G-IOP-1 `gpio_lsqsh.c` 不依赖 pinctrl，运行期可自由改方向 + 上下拉 | debug-knowledge `mcu/gpio-runtime-reconfigure-no-pinctrl.md` · qsh-test-gen §1.2（反向提示） |
| 2026-09-18 | G-IOP-2 / G-IO-3 `LOG_MODE_DEFERRED` 下批量直吐大块输出丢消息/合并/乱码（同一根因，并案） | KNOWN_ISSUES #85 · qsh-test-gen §1.4 第 6 条 · ref-gen-errors |
| 2026-09-18 | G-IOP-3（**撤回** —— 与 G-P4.5-3 完全同题，复现实例已并入 G-P4.5-3 行） | 见 G-P4.5-3 行 |
| 2026-09-18 | G-IO-1 `io_probe_test` 首跑矩阵是下三角 artifact（驱动过的脚没还原） | KNOWN_ISSUES #86 · qsh-test-gen §1.4 第 10 条 · qsh-test-validate §1.10r · ref-gen-errors |
| 2026-09-18 | G-IO-2 `dbg_run.py` 裸 `print()` 遇非 GBK 字符抛 `UnicodeEncodeError` ⇒ `--raw` 采集半途中断 | KNOWN_ISSUES #91 · qsh-test-gen §1.5 · ref-gen-errors |
| 2026-09-18 | G-P5-1 `ZT_INFO` 第一参必须是**字符串字面量**（文档写法易读成标识符） | SPEC_REFERENCE §6.5.4 · qsh-test-gen §1.4 第 4 条 · qsh-test-validate §1.10k · ref-gen-errors |
| 2026-09-18 | G-P5-2 `pwm_ls.c` 的 `resets` 配置成员声明与实例初始化项守卫不对称 | KNOWN_ISSUES #79 · qsh-test-gen §1.0 |
| 2026-09-18 | G-P6F-1 `qsh-driver-review` §1.8 仍带 #78 的旧错数值（-28 / `SPIF_CMD_BASE - 112`） | qsh-driver-review §1.8 已更正：`-ENOSR`=**-63** · 偏移 **-252** · spif1 落点 **`0x4006FF30`**（并注明该缺陷 2026-09-18 已修） |
| 2026-09-18 | G-P6F-2 `DEFECT_ANALYSIS_2026-09-17.md` §1.6 饱和阈值单位漏三零 | KNOWN_ISSUES #74 已含正确值（**131072000 ms** ≈ 36.4 h）；档案已就地更正 |
| 2026-09-18 | G-P6F-3 `runner.md` 引用的 `driver-comment-convention` 记忆不存在 | `runner.md` 铁律 8 改指 `.claude/DOC_EDIT_RULES.md` §3.5（单一信息源，不另建 memory） |
| 2026-09-18 | G-IOFIX-1 HAL `io_cfg_input()` 清错 `OE_DIN` 半区（低半区 = DIN 回读位）⇒ OUTPUT→INPUT 撤不掉输出使能，撤销静默无效 | KNOWN_ISSUES **#93** · qsh-driver-review **§1.10**（新检查维度）+ §2 扫描步骤 10 · qsh-test-gen §1.4 第 10 条 |
| 2026-09-18 | G-IOFIX-2 探测/测量工具：任何状态改写必须**读回验证**，否则单点故障级联污染成"全网连通"；阳性对照不能替代不变量自检 | debug-knowledge common 域 **`probe-scan-selfcheck-invariants.md`**（+ 域 README 索引）· qsh-test-gen §1.6 第 4 条（测量类追加一问）/ §1.4 第 10 条 · qsh-test-validate §1.10 行 r |
| 2026-09-18 | G-IOFIX-3 基线门：基线为低的脚不得产生连线证据；实测 PF12/PH12/PG07 三个 spif4 输入脚异常（待查） | KNOWN_ISSUES **#94**（#86 已加指针）· debug-knowledge `common/probe-scan-selfcheck-invariants.md`（基线门）· qsh-test-gen §1.4 第 10 条 · qsh-test-validate §1.10 行 r |
| 2026-09-18 | G-IOFIX-4 自己上一轮的 `nohup west build` 未退干净 + 前台同 build dir 重建 ⇒ `ninja: failed recompaction: Permission denied` | qsh-test-validate §2（并发第二种来源：自己的残留）· qsh-test-run §6 · qsh-debug §7（指针行） |
| 2026-09-18 | G-HW2-1 `APP_CPU_SRST` 写 2 后**实测回读 = `0x0`** ⇒ #75 的"守卫误判 ⇒ API 全 `-EBUSY`"推论**未获实测支持**（写 1 回读 `0x3` 且触发 bootrom 级重启；emmc SKIP=0） | KNOWN_ISSUES **#75 更正批注**（+#38 副作用更正）· qsh-debug §7 · qsh-test-gen §1.0（**待父会话裁定**：写 2 语义；证据来源 `tmp/hw2/`） |
| 2026-09-18 | G-HW2-2 `common/judge.py::mem_state_line()` 在 `mem_state==ok` 时丢 `note` ⇒ "mem 被门禁拒绝"诊断串在 `.md`/控制台不可见 | KNOWN_ISSUES **#95**（2026-09-19 已修 `judge.py:127`）· qsh-test-compare §2 · SPEC_REFERENCE §6.2.4 E6 注 |
| 2026-09-18 | G-HW2-3 `ZTEST(<suite>,…)` 缺同名 `ZTEST_SUITE(<suite>,…)` ⇒ 整套件从未注册执行（emmc `emmc_app_cpu_guard` 2 项；keeper 扫描另现 qspi `qspi_flash_id` 8 项） | KNOWN_ISSUES **#96** · qsh-test-validate **§1.10s**（新检查项，含正反双向自测） |
| 2026-09-18 | G-HW2-4 `run_all.py --dry-run` 在 `:425` 提前 `return 0` ⇒ 看不到 `audit_stale_elves()` 哨兵清单（`:495`，擦除之后） | qsh-test-run §4.1 · SPEC_REFERENCE §6.8.1（STRATEGY_TEST 注） |
| 2026-09-19 | G-HW2-5 `mem_state_line()` 已修，但 `eth_test` 控制台 `print_report()` 绕过该函数 ⇒ 该工程控制台仍看不到 E6 诊断串（残留，未修） | KNOWN_ISSUES **#95 残留缺口段** · qsh-test-compare §2 · SPEC_REFERENCE §6.2.4 E6 注 |
| 2026-09-19 | G-CLEAN-1 报告里"看似有结果"的 PASS 可能属于**从未注册的套件**（`size_is_32mib` 归因失效） | KNOWN_ISSUES **#96 归因陷阱段** · qsh-test-compare §5（第 6 条）· qsh-test-validate §1.10s |
| 2026-09-19 | G-CLEAN-2 拿历史报告归因前必须先比对源码/ELF mtime 与报告生成时间 | qsh-test-compare §5（第 7 条）· KNOWN_ISSUES #88（产物新鲜度同族） |
| 2026-09-19 | G-HW3-1 `i2c_pure_test` 串口采集窗口内拿不到测试启动后的输出（用例其实已跑完；判定必须走 `dump_results.py`，根因未定位） | KNOWN_ISSUES **#97** · qsh-test-run §6 · qsh-debug §7 · qsh-test-compare §3 |
| 2026-09-19 | G-HW3-2 `i3c_i2c_switch_test case_switch_core` 同一核流同时出现 `PROJECT EXECUTION FAILED` 与 `SUCCESSFUL`，且 RAM `ctrl_failed=1` + 空 reason（根因未定位） | KNOWN_ISSUES **#98**（i3c 切换域）· qsh-test-compare §2（相反标记不得只取最后一条） |
| 2026-09-19 | G-HW3-3 `app_iwdg_cpu2_test` 的 `dbg_run.py` 委托 `<case>/test.sh -r` ⇒ 构建目录不可隔离（镜像产物 + 通用 dbg_run 绕行） | qsh-test-gen §1.5（禁委托，须内联 gdb）· ref-gen-errors |
| 2026-09-19 | G-HW3-4 `i2c_pure_test` 的 `i2c_wave` 实测整轮 ≥40s，远超注释 `~10.1s` 估算（40s 窗只到 85/100 轮） | KNOWN_ISSUES **#99** · qsh-test-run §6（该工程采集窗 ≥90s） |
| 2026-09-19 | G-HW4-1 `dbg_run.py` capture 轮 `Popen(gdb, stdout=PIPE)` 后不排空 ⇒ gdb 阻塞 ⇒ 长用例尾部证据丢失（**更正 #97 / #99** 两条结论） | KNOWN_ISSUES **#97（重写，根因已定位）** + **#99（撤回"≥40s"）** · qsh-test-run §3 + §6 · qsh-debug §7 · qsh-test-compare §3 · qsh-test-gen §1.5（drain 模板硬要求） |
| 2026-09-19 | G-HW4-2 双 `PROJECT EXECUTION` = app `printk` + ztest 强 `main()` 的 `end_report()`（app 不调 `ztest_run_all` ⇒ ztest 那条恒 SUCCESSFUL 空跑）（**更正 #98 前半**） | KNOWN_ISSUES **#98 前半（已定论）** · qsh-test-compare §2 · qsh-test-gen §1.4 第 5 条 · qsh-test-run §5 |
| 2026-09-19 | G-HW4-3 `test_reasons[10]` 被抹（原始字节 `00×8` + `hole_should_nack`）⇒ #98 后半改写为"有确定性痕迹、缺 cpu2 断点手段" | KNOWN_ISSUES **#98 后半** · qsh-test-compare §3（`x/s` 读空 ≠ 没写）· qsh-debug §6（单 gdb 会话只控一核） |
| 2026-09-19 | G-HW4-4 `i2c_wave` 实测 **102.76 ms/轮 = 10.276 s**，源码注释正确（**撤回 #99**） | KNOWN_ISSUES **#99（更正/撤回）** · qsh-test-run §6 · `projects/i2c_pure_test/tests/COVERAGE.md`（补实测耗时） · OPEN_ITEMS O-22 |
| 2026-09-20 | G-R7-1 批量判定必须**按核**取证据来源 —— `src` 是跨核聚合，不能当 case 级判据（G9/D6 口径） | SPEC_REFERENCE §6.2.4 **G9/D6 注**（唯一真源）· qsh-test-compare **§2** + §4（`src` 语义） |
| 2026-09-20 | G-R7-2 token 宏 no-op ⇒ 批量里只能靠 mem ⇒ 弱证据 PASS（O-13 本体；#96 姊妹形态） | KNOWN_ISSUES **#96 姊妹形态段**（指针）· SPEC_REFERENCE §6.2.4 G9 注（O-13 根治）· OPEN_ITEMS O-13（已结） |
| 2026-09-20 | G-FMT-1（并入同题 G-DRAIN-1）`Path.write_text()` 在 Windows 默认把 LF 文件整份翻成 CRLF ⇒ diff 从几行炸到几百行 | debug-knowledge common 域 **`write-text-newline-flip.md`**（+ 域 README 索引）· 工程 `KNOWN_ISSUES.md`（脚本/工具链域）**待补——本会话未执行**（KNOWN_ISSUES 在写域外） |
| 2026-09-20 | G-FMT-2 `tests/COVERAGE.md` 期望列的**裸竖线**撕裂 markdown 表格（下游一行撕成两半；新解析器已尾两列锚定容错，源文件仍坏） | qsh-test-gen **§1.6 第 1 条**（生成期转义规则）· qsh-test-validate **§1.10t**（新检查项）· 另发现 5 份同族坏表（iopmp/spi/emmc/eth/io_probe）· 修源**本会话未执行**（`projects/` 在写域外） |
| 2026-09-20 | G-HW5-1 破坏性 case 的采集窗口按标称超时设 ⇒ 误判"没复位"（判据取跨复位 `__noinit` 终态，或窗口 ≥60s） | KNOWN_ISSUES **#100** · qsh-test-run §6 · qsh-test-compare §2 · qsh-debug `ref-flash-variants.md` §2 |
| 2026-09-20 | G-HW5-2 `dbg_run.py::flash_once` 用 `communicate(timeout=)` 收尾 ⇒ prime 轮 gdb 被 kill、连接残留（**已修**：23 份 live 模板改 drain + `proc.wait()` + 定时 `\x03`/detach） | KNOWN_ISSUES **#101** · qsh-debug §2.3 · qsh-test-gen §1.5 · qsh-test-validate **§1.10u**（新检查项，正反自测）· ref-gen-errors · qsh-test-run §3 |
| 2026-09-20 | G-HW5-3 命令通道固件（注册 0 个 ztest 测试）经 `zt_run_all` **无条件**再打一条 `PROJECT EXECUTION SUCCESSFUL`（与自裁决行冲突；R6 归 `CONFLICTING_MARKERS`） | KNOWN_ISSUES **#98 前半（同族增补）** · qsh-test-compare §2 · qsh-test-gen §1.4 第 4 条 |
| 2026-09-20 | G-HW5-4 `app_iwdg_cpu2_test case_reset` 的 flash 固化流不启动 cpu2（COM18 全静默）—— 2os 板 bootrom 只引导 cpu1，cpu2 镜像需 RAM staging | KNOWN_ISSUES **#102** · SPEC_REFERENCE §6.7.3 · qsh-debug `ref-flash-variants.md` §2 · qsh-test-run §8 |
| 2026-09-20 | G-FAMFILL-1 `run_all.py` 的 `--skip-build` 哨兵门只验"符号存在"、不验"源码新鲜度" ⇒ 烧陈旧 ELF 产生假 FAIL / 假 PASS（`-fipa-icf` 折叠双向） | KNOWN_ISSUES **#88 姊妹形态段** · SPEC_REFERENCE §6.2.2 + §6.8.1 · qsh-test-run §4.2/§6 · qsh-test-compare §5 · ref-gen-errors |
| 2026-09-21 | G-B2I-1 RLOG 帧长上限是**单出口的隐式证据边界**（默认 54B 静默截断 ztest D1 行 ⇒ TGT 逐项零证据 FAIL、无报错） | SPEC_REFERENCE §6.11.6「偏离 #2」（唯一真源）· qsh-test-gen §1.2 |
| 2026-09-21 | G-B2I-2 核信封必须加在**中继侧**、不塞进从核帧（14B 信封挤帧长必截断长行） | SPEC_REFERENCE §6.11.6「偏离 #1」· qsh-test-gen §1.2 |
| 2026-09-21 | G-B2I-3 `_LoadWatch` 进度信号被行缓冲漏掉：gdb `load` 进度是 `\r` 原地刷新 ⇒ drain 须双向切分 + `os.read(fd)` | SPEC_REFERENCE §6.11.4（**正文缺 `\r`/`os.read` 细节，待 runner 补**）· qsh-test-run §4.2 |
| 2026-09-21 | G-B2I-4 归档「两份 token 块」是既有现象（prime 轮固件在被杀 gdb 后继续跑） | SPEC_REFERENCE §6.11.6 末段已覆盖，未另写 |
| 2026-09-21 | G-B2I-5 临时 `k_oops()` 探针放在引导核早期 ⇒ 整板静默（从核也起不来），现场被误读成「崩溃」 | qsh-debug §7 速查表 + §8 探针纪律 |
| 2026-09-21 | G-B2F-1 `Popen(stdout=PIPE)` 同族第 4 处（precheck `_gdb_session_b_proc`）+ 判据与 `scan_pipes.py` 防线 | SPEC_REFERENCE §6.11.1（唯一真源）· qsh-test-gen §1.5 · qsh-debug §7 |
| 2026-09-21 | G-B2F-2 行内截断转义吞 token 首字母 ⇒ 项降级 mem（两写入方 + 单核无 `PRINTK_SYNC`） | SPEC_REFERENCE §6.11.2 · qsh-test-compare §1 规则 1 |
| 2026-09-21 | G-B2F-3 纠偏：`load_fail` 不是管道死锁，真因是**绝对** 20s 窗口（烧录慢 17~50s 被误判） | SPEC_REFERENCE §6.11.4 · qsh-test-run §4.2 |
| 2026-09-21 | G-B2F-4 `date_str` 第三处捕获 bug（闭包在赋值前捕获 ⇒ 不带 `--session-dir` 时 `NameError`） | ref-gen-errors（新行 + AST 静态判据）· qsh-test-gen §1.5 |
| 2026-09-21 | G-B2F-5 单出口在 ztest 型工程是"换后端"（上游 `PRINT_DATA` 覆盖点，代价在 L0 分拣契约） | SPEC_REFERENCE §6.11.5 / §6.11.6 已覆盖（可行性 + 落地），未另写 |
| 2026-09-21 | G-FINAL-1 token 文法两类（D1 原生 / D2 自定前缀）⇒ 用 ztest 正则核"分拣是否到位"会误得 0 | qsh-test-compare §2 · qsh-test-run §5 · qsh-test-gen §1.2（真源 SPEC_REFERENCE §6.9.1 已有） |
| 2026-09-21 | G-FINAL-2 框架改造改变跨核 dispatcher 时延 ⇒ 时序敏感用例不得直接比旧基线 | qsh-test-compare §5（第 3 条扩展）· KNOWN_ISSUES #98（增补段）· SPEC_REFERENCE §6.2.4（归因纪律注，非判定语义） |
| 2026-09-21 | G-FINAL-3 `summary.md` 分母 ≠ `report.json` 逐项计数（核项数以 summary 为准） | qsh-test-compare §4 |
| 2026-09-21 | G-FINAL-4 "前置外部条件未满足"降级口径不统一（skip vs 硬 FAIL） | qsh-test-gen §1.6 第 9 条 · qsh-test-validate §1.10v |
| 2026-09-21 | G-E1 CKLink **瞬时故障** → DebugServer 自杀（`DebuggerServer quit`）⇒ 批次后续 case 级联假 FAIL | qsh-debug **§2.3a** + §7 · qsh-test-run §4.1 + §6 · FRAMEWORK_MAP §3（判据类，**未**新开 KNOWN_ISSUES 号） |
| 2026-09-21 | G-E2 16/24 工程 `run_test.py` 无 DebugServer 自愈（只有 6 份定义 `DEBUGSERVER_EXE`）；`run_all.py` case 循环内不补刀 | **已修（E-2）**：`qsh-test-run` **§4.1（确定性结论）+ §6** · 实现 `common/acquire.py::ensure_debugserver_alive()`（一处实现、两处调用）· 见下行 G-E2-fix 行 |
| 2026-09-21 | G-E3 `i3c_dualcore_test` flaky 第二种 sub-mode「prime 首轮零输出」（与首形态同族、不同 sub-mode） | KNOWN_ISSUES **#87 补注**（未新开号）· qsh-test-run §4.1 |
| 2026-09-21 | G-E2-fix **已修**：DebugServer 存活检查提升为**每 case 前置**——`common/acquire.py::ensure_debugserver_alive()`（唯一实现，复用既有 netstat/`kill_residual_gdb`/`restart_debugserver_stable`）被 ① `run_all.py` case 循环 ② 五个 L1 策略入口（`single_cap`/`_ports_dict_one`/`dual_run`/`mbox`/`iwdg`）双双调用 ⇒ 24 工程全覆盖（含不经 run_all 的单跑）；起不来时 run_all 记 `ENV`（summary `ENV-ABORT`）fail-closed，run_test.py 明确报错中止该 case。上板 A/B：改动前 DS 掉线 ⇒ 4/4 假 FAIL；改动后同场景 ⇒ 1 FAIL（被打中那 case）+ 3 PASS 自愈。 | SPEC_REFERENCE **§6.2.3 第 6 条**（+ §6.4.5 流程 + §6.8 第 3 条分工注，本会话已写入）· qsh-test-run **§4.1 + §6（已更新为确定性结论，2026-09-21 keeper）** · 对应上行 G-E2 行 |
| 2026-09-21 | G-E2-1（= 提示语 G-E2-2）Bash 工具里 `cd <dir> && <cmd> > log 2>&1 &` 后台化的是**整条链**（含 `cd`），父 shell 的 cwd 不变 ⇒ 随后的相对路径 `head log` 报 "No such file"，极易被误读成"启动失败"而**重复启动** ⇒ 两个 `run_all.py` 并发抢板子（实测：两实例、两 session、共写同一日志文件、板子结果互相污染）。**判据/纪律**：① 后台命令一律用**绝对路径**重定向日志；② "日志没出现"先查**进程表**（`Get-CimInstance Win32_Process` 按 `run_all.py` 过滤）再判断死活，**不要凭文件是否存在下结论**。 | debug-knowledge common 域 **`bash-bg-cd-subshell.md`**（+ 域 README 索引）· qsh-test-run **§4.1**（G-E2-2 条 + nohup 条指针）· FRAMEWORK_MAP **§3**（污染会话目录识别/处置） |
| 2026-09-21 | G-E2-1b **健康路径不得用 `probe_gdb_alive()`**：任何 `gdb 连接→detach→退出` 必留连接残留（KNOWN_ISSUES #17）⇒ 用 gdb 探活会自己制造残留、把"健康"变成"需要重启"。健康判据只用**无副作用** netstat（1025 LISTENING 且无 ESTABLISHED）；抗抖动 = **15s 轮询**等 listen + netstat **重试 3 次**才认定死（防错杀活实例、白重启 ~35s）。健康路径实测 24ms/case。 | qsh-test-run **§4.1**（G-E2-1 条）· 实现 `common/acquire.py::ensure_debugserver_alive()` / `_ds_healthy()` · 关联 `KNOWN_ISSUES.md` #17 |
