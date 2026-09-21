# I2C filter (PFR/TMP) — i2cf_test_case_default 失败分析

| 项 | 值 |
|---|---|
| 工程 | i2cf_test |
| case | i2cf_test_case_default (case_default) |
| 被测驱动 | `modules/lib/pfr_tpm/drivers/pfr/i2c/i2c_filter.c`（接口 `i2c_filter.h`） |
| 报告 | [i2cf_test_report.md](../i2cf_test_case_default/i2cf_test_report.md) |
| 结果 | FAIL (14/18) |
| 执行标记 | CTRL=无（单核工程，无 CTRL 侧）, TGT=FAILED |
| 内存兜底 | 有效 |

> 单核工程：replay_meta.json 只归档 TGT(COM16)，无 CTRL 日志/无 CTRL 执行标记。
> 串口跑满 91 行并以 `PROJECT EXECUTION FAILED` 收尾（`serial_TGT.log:89-91`），
> 四个断言失败均带原生 ztest 错误文本 ⇒ **非采集截断，是真实断言失败**。

## 失败项明细

| 测试项 | 维度 | 预期 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|
| filter_blocks_nonwhitelist | 功能通路+异常参数 | 白名单只授权 `0x51` 时，上游 master 读 `0x50` → `ret_blocked < 0`；filter off 后同址 → `0`（COVERAGE.md:148 / 源码 `i2cf_test.c:876-878`） | `ret_blocked = 0`（访问**未被拦截**，反而成功） | `i2cf_test.c:876: (ret_blocked < 0 is false)`；日志原文 `non-whitelisted 0x50 must be blocked (whitelist=0x51), got 0` | `serial_TGT.log:47` |
| isr_addr_beyond_whitelist | 中断/时序 | 空白名单下上游 master 访问 `0x50` → ① ISR 触发 `isr_count>=1` ② `SMBF_NONWHITELIST` 锁存非零 ③ 主机侧返回负值（COVERAGE.md:147 / `i2cf_test.c:806-822`） | `i2cf_isr_wait(100) = false`（ISR **未触发**，故 ②③ 未判到）；日志附 `ret=0` 表示主机侧访问也没失败 | `i2cf_test.c:806: (i2cf_isr_wait(100) is false)`；日志原文 `filter ISR not fired: upstream access to 0x50 did not reach the filter? (ret=0, 检查上游 master → i2cf4 SCLI/SDAI(PE03/PB02) 跳线)` | `serial_TGT.log:69` |
| master_eeprom_rw | 功能通路 | 旁路 master(i2c4) 对从设备写 pattern 后 `i2c_burst_read(master, slave, 0x00, ...)` 成功（`ret==0`），再逐字节相等（COVERAGE.md:150 / `i2cf_test.c:626-628`） | `i2c_burst_read` 返回**非零**（读失败）；**具体错误码未采集到** —— 该断言用 `zassert_ok`，失败信息只有 `eeprom read`，未打印 `ret` | `i2cf_test.c:626: (i2c_burst_read(...) is non-zero)`；日志原文 `eeprom read` | `serial_TGT.log:75` |
| filter_passthrough_whitelist | 功能通路 | 上游 master 经滤波器读回 == 预先经旁路写入的 pattern（`0x5A..0x61`，`zassert_mem_equal` 逐字节）（COVERAGE.md:149 / `i2cf_test.c:962-965`） | 读**成功**（`i2cf_test.c:958` 的 `zassert_ok` 已过）但数据全 `0xFF`：`readback = ff ff ff ff ff ff ff ff` | `i2cf_test.c:962: (readback not equal to pattern)`；日志原文 `passthrough data mismatch: got ff ff ff ff ff ff ff ff` | `serial_TGT.log:53` |

## 归因

**四项失败全部落在「行为层 / 依赖外部器件」这一组**（COVERAGE.md §2 的 B 组 + `isr_addr_beyond_whitelist`），
纯寄存器/API 契约的 14 项全过。失败模式高度一致，指向**上游 master 跳线 / 下游从设备通路**，而非驱动寄存器逻辑：

1. **上游侧（`isr_addr_beyond_whitelist`）**：`i2cfupstream`（overlay alias，真实实例未经实测）访问 `0x50`
   **完全没有到达 i2cf4** —— ISR 不触发、`ret=0`（主机侧根本没被拦）。这正是 COVERAGE.md §6.2 T1
   未决项：上游 master 是哪个实例、跳线是否真的落在 `i2cf4.SCLI/SDAI(PE03/PB02)`，仓库内无法证实。
   **判据失败在 `isr_addr_beyond_whitelist` 的第一条断言就短路**，② `SMBF_NONWHITELIST`、
   ③ 主机侧负值均未执行 ⇒ 驱动器 ISR 路径本身**未被验到**，不能据此判定驱动 ISR 有缺陷。

2. **下游侧（`master_eeprom_rw` / `filter_passthrough_whitelist`）**：二者都先经 `i2cf_first_slave_bypass()`
   扫描 —— `slave_addr_scan` 上报 `count=1, addrs=50`（`serial_TGT.log:82`）⇒ 旁路扫描**看到 `0x50` 应答**，
   故这两项**没有走 SKIP**、进入实测，却在真正的 burst 读/写上一败一读回 `0xFF`。
   - `master_eeprom_rw`：`i2c_burst_read` 返回非零（`serial_TGT.log:75`，错误码未采集到）。
   - `filter_passthrough_whitelist`：旁路写 pattern 后再经滤波器读回得全 `0xFF`（`serial_TGT.log:53`）。
   扫描"应答"与 burst 读写"失败/读到 `0xFF`"**互相矛盾**，最可能是：应答者并非真 EEPROM（或地址 `0x50`
   未经实测、偏移/容量不符），或旁路到下游的物理通路不稳定。**当前证据不足以区分**是外部器件问题
   还是滤波器透传通路问题 —— 需按 COVERAGE.md §6.2 T2 用示波器/换已知器件确认 O 侧到底挂了什么。

3. **`filter_blocks_nonwhitelist`** 的 `ret_blocked = 0`（未拦 `0x50`）与 (1) 同源：上游访问没能真正
   进入/穿过滤波器，或白名单未生效。它**在第一条断言就失败**，紧跟其后的「filter off 自证对照」
   （`i2cf_test.c:883-887`）未执行 ⇒ 无法用自证排除"器件/接线"因素。

**结论**：判定为**环境/接线未就绪（外部上游跳线 + 下游从设备通路）导致的失败，无法判定驱动缺陷**。
还缺的证据：
- 错误码级别的 `i2c_burst_read` 返回值（`master_eeprom_rw`，当前只打印 `eeprom read`）；
- 上游 master 的真实实例与跳线落点（T1）；
- O 侧 `0x50` 应答者的身份 / EEPROM 地址与偏移实测（T2）。

## 备注

- 与 COVERAGE.md §2 的处置一致：B 组 3 项本设计为「扫不到从设备即 `ztest_test_skip()`，非 FAIL」
  （`i2cf_test.c:609-611` / `:860-862` / `:929-931`）。本轮**扫描到 `0x50` 应答故未 SKIP**，
  才暴露为 FAIL；若旁路扫描返回 0，这 3 项会记为 SKIP、不阻塞 case。
- `isr_addr_beyond_whitelist` 失败信息内建提示已明写 `检查上游 master → i2cf4 SCLI/SDAI(PE03/PB02) 跳线`
  （`i2cf_test.c:806-809`），与上述归因一致。
- COVERAGE.md §6.2 的 T1/T2 两项未决项**本轮仍未闭环**；§6.1 D5 指出被选作上游 master 的实例
  若保留 `pinctrl-noinit` 会导致引脚不挂 I2C 功能、症状伪装成"接线错"（`i2c_ls.c:901`）——
  属本轮值得复查的一条候选（未采集到 overlay 实际取值，无法确认）。
- 报告表「实测(INFO)」列对这 4 项均为 `-`（COVERAGE.md 载明本工程仅 4 项有 INFO 通道，
  这 4 项失败均无 INFO 上报），实际值取自 `serial_TGT.log` 的 ztest 原生断言文本。
