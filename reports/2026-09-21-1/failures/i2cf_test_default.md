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
| filter_blocks_nonwhitelist | 功能通路+异常参数 | 白名单只授权 `0x51` 时，上游 master 读 `0x50` → `ret_blocked < 0`；filter off 后同址 → `0`（COVERAGE.md:148 / 源码 `i2cf_test.c:876-878`） | `ret_blocked = 0`（访问**未被拦截**，反而成功） | `i2cf_test.c:876: (ret_blocked < 0 is false)`；日志原文 `non-whitelisted 0x50 must be blocked (whitelist=0x51), got 0`。**归属：簇 1 + 簇 2**（双属，见「归因」） | `serial_TGT.log:47`；skip 设计**在位**（`i2cf_test.c:860-862`：`i2cf_first_slave_bypass()==0` ⇒ `ztest_test_skip()`），因 `0x50` 应答而**未生效**（`serial_TGT.log:82`） |
| isr_addr_beyond_whitelist | 中断/时序 | 空白名单下上游 master 访问 `0x50` → ① ISR 触发 `isr_count>=1` ② `SMBF_NONWHITELIST` 锁存非零 ③ 主机侧返回负值（COVERAGE.md:147 / `i2cf_test.c:806-822`） | `i2cf_isr_wait(100) = false`（ISR **未触发**，故 ②③ 未判到）；日志附 `ret=0` 表示主机侧访问也没失败 | `i2cf_test.c:806: (i2cf_isr_wait(100) is false)`；日志原文 `filter ISR not fired: upstream access to 0x50 did not reach the filter? (ret=0, 检查上游 master → i2cf4 SCLI/SDAI(PE03/PB02) 跳线)`。**归属：簇 2** | `serial_TGT.log:69`；上游实例取自 overlay alias（`cpu1_i2cf_test/app.overlay:45` `i2cfupstream = &i2c2;`，自述"若实测发现是别的实例，只改这一行"）、用例经 `i2cf_upstream()`（`i2cf_test.c:654`）取设备 |
| master_eeprom_rw | 功能通路 | 旁路 master(i2c4) 对从设备写 pattern 后 `i2c_burst_read(master, slave, 0x00, ...)` 成功（`ret==0`），再逐字节相等（COVERAGE.md:150 / `i2cf_test.c:626-628`） | `i2c_burst_read` 返回**非零**（读失败）；**具体错误码未采集到** —— 该断言用 `zassert_ok`，失败信息只有 `eeprom read`，未打印 `ret`。失败顺序 =「备份读 OK → 写 OK → **紧接着的读报错**」 | `i2cf_test.c:626: (i2c_burst_read(...) is non-zero)`；日志原文 `eeprom read`。**归属：簇 1 + 独立疑点（写后无延时）** | `serial_TGT.log:75`；失败顺序由 `serial_TGT.log:74-76` 与源码 `i2cf_test.c:624`（写）、`:626`（立刻读，**中间无延时**）共同确定 |
| filter_passthrough_whitelist | 功能通路 | 上游 master 经滤波器读回 == 预先经旁路写入的 pattern（`0x5A..0x61`，`zassert_mem_equal` 逐字节）（COVERAGE.md:149 / `i2cf_test.c:962-965`） | 读**成功**（`i2cf_test.c:958` 的 `zassert_ok` 已过）但数据全 `0xFF`：`readback = ff ff ff ff ff ff ff ff` | `i2cf_test.c:962: (readback not equal to pattern)`；日志原文 `passthrough data mismatch: got ff ff ff ff ff ff ff ff`。**归属：簇 1** | `serial_TGT.log:53`；skip 设计**在位**（`i2cf_test.c:929-931`），因 `0x50` 应答而**未生效**（`serial_TGT.log:82`） |

## 归因

**结论先行：判定为「待用户确认接线」——4 项失败全部落在测试/配置侧（overlay alias / 用例判据 /
延时），不需要驱动工程师。** 4 项均落在 COVERAGE.md §2 的 B 组 + `isr_addr_beyond_whitelist`
（行为层 / 依赖外部器件），纯寄存器/API 契约的 14 项全过。按 `OPEN_ITEMS.md` **O-30** 展开为
**两簇 + 一条独立疑点**：

### 簇 1 —— 3 项「本应 SKIP 却 FAIL」（从设备"被扫描到"）

- **归属**：`master_eeprom_rw` / `filter_blocks_nonwhitelist` / `filter_passthrough_whitelist`。
- **设计（COVERAGE.md §2）**：从设备缺失 ⇒ `ztest_test_skip()`（SKIP 非 FAIL）。**代码在位**：
  guard 块 `i2cf_test.c:609-611` / `:860-862` / `:929-931`（`ztest_test_skip()` 分别在
  `:610` / `:861` / `:930`——与 O-30 所引行号一致），条件 = `i2cf_first_slave_bypass()` 返回 0。
- **实测**（`serial_TGT.log:82`）：`INFO slave_addr_scan: range=08..77,count=1,addrs=50`
  ⇒ 旁路扫描在 **`0x50`** 上有应答，**skip 未生效**，三项遂跑到断言上挂。
- **关键疑问（推断，待用户确认）：`0x50` 是真器件还是假阳性？** 用户此前说过
  「**I2CF 那路有回环了，可能不需要挂 eeprom**」（COVERAGE.md §2 同载）。若确实无器件，
  则 `0x50` 是**回环/浮动造成的假 ACK** —— 判 ACK 的方式是 `i2c_read(down, &v, 1, addr) == 0`
  （`i2cf_test.c:707`），**并不区分"真器件应答"与"回环/假阳性"** ⇒ 一旦有假应答，整个
  「扫不到即 SKIP」的设计就失效。**未采集到**能区分二者的证据（无器件侧波形、无 ACK 时序）。
- **地址不符（实测 vs 假设）**：overlay 写的是「外接从设备 **`0x51` 或 `0x52`，未经实测**」
  （`cpu1_i2cf_test/app.overlay:32-34`），与实测应答的 **`0x50` 不符**。

### 簇 2 —— 上游流量没到滤波器（2 项）

- **归属**：`isr_addr_beyond_whitelist`（`i2cf_test.c:806`，ISR 未触发）+ `filter_blocks_nonwhitelist`
  （`i2cf_test.c:876`，没拦住）。
- 两项都走 `i2cf_upstream()`（`i2cf_test.c:654`），而**上游 master 实例是猜的** ——
  overlay `aliases { i2cfupstream = &i2c2; }`（`cpu1_i2cf_test/app.overlay:45`），自述
  「= 用户口述链路『I2C2 > I2CF4 > I2C3』的左端；⚠️ **若实测发现是别的实例，只改这一行 alias 即可**，
  用例代码无需改动」（`app.overlay:24-28`）。
- `isr_addr_beyond_whitelist` 的判据在**第一条断言就短路**（`i2cf_isr_wait(100) is false`）
  ⇒ ② `SMBF_NONWHITELIST` 锁存、③ 主机侧负值**均未执行** ⇒ 驱动器 ISR 路径本身**未被验到**，
  **不能据此判定驱动 ISR 有缺陷**。
- 该簇即 COVERAGE.md §6.2 **T1** 未决项。相关 §6.1 **D5**（被选作上游 master 的实例若保留
  `pinctrl-noinit`，引脚不挂 I2C 功能、症状伪装成"接线错"，`zephyr/drivers/i2c/i2c_ls.c:901`）
  是候选项 —— **本轮未采集到 overlay 实际取值**（是否已 `delete-property`），无法确认。

### 独立疑点 —— `master_eeprom_rw` 的失败顺序

- **实测**（`serial_TGT.log:74-76`）：失败顺序是「备份读 OK → 写 OK → **紧接着的读报错**」。
- `i2cf_test.c:624` 写完、`:626` 立刻 `i2c_burst_read`，**中间无延时** ⇒ 这是 EEPROM 的
  **经典坑**（写周期内器件不 ACK），**真器件也必挂**。（**推断**：本轮未采集到器件时序/ACK 波形，
  仅由"失败顺序 + 无延时"推出。）
- 该断言用 `zassert_ok`，**具体错误码未采集到**（失败信息只有 `eeprom read` 文案）。

### 结论与待确认的 3 个问题

两簇 + 一疑点的成因**全在测试/配置侧**（overlay alias / 用例判据 / 写后延时），**不需要驱动工程师**。
**待用户确认接线**，明确回答（即 COVERAGE.md §6.2 的 T1/T2）：

1. **上游 master 是哪个实例？** —— 决定 overlay `i2cfupstream` 取值（改一行，用例零改动）。
2. **`i2cf4` 的 SCLO/SDAO(PE02/PB00) 侧是真器件还是回环？** —— 决定 B 组三项是 SKIP 还是实测。
3. **若是器件，地址是多少？** —— overlay 假设 `0x51`/`0x52`，实测应答在 `0x50`，两者不符。

还缺的证据（**未采集到**，非"推断为无"）：
- 错误码级别的 `i2c_burst_read` 返回值（`master_eeprom_rw`，当前只打印 `eeprom read`）；
- 上游 master 的真实实例与跳线落点（T1）；
- O 侧 `0x50` 应答者的身份 / EEPROM 地址与偏移实测（T2）；
- overlay 里 `i2cfupstream` 所指实例的 `pinctrl-noinit` 实际取值（§6.1 D5 候选）。

## 备注

- 与 COVERAGE.md §2 的处置一致：B 组 3 项本设计为「扫不到从设备即 `ztest_test_skip()`，非 FAIL」
  （`i2cf_test.c:609-611` / `:860-862` / `:929-931`）。本轮**扫描到 `0x50` 应答故未 SKIP**，
  才暴露为 FAIL；若旁路扫描返回 0，这 3 项会记为 SKIP、不阻塞 case。
- **陈旧注释（排查陷阱）**：`i2cf_test.c:912` 写 `/* i3c3：上游 master */`，但 alias 实际绑
  `i2c2`（`cpu1_i2cf_test/app.overlay:45`）—— `i2c3` 在本工程里并非上游；排查时极易被带偏。
  同类：`i2cf_test.c:908` 注释称从设备地址 `0x50`（与 overlay 的 `0x51`/`0x52` 又不一致）。
- **台账**：`OPEN_ITEMS.md` **O-30**（2026-09-21）已登记本 case「两簇 + 一独立疑点」的展开与
  「待用户答 3 问」，并给出**不改驱动的候选修法**（确认接线后改 alias 一行 / 给写后加延时或
  ACK 轮询 / 修扫描的假阳性判据）。本文件的归因即按该条原文复核落成。
- `isr_addr_beyond_whitelist` 失败信息内建提示已明写 `检查上游 master → i2cf4 SCLI/SDAI(PE03/PB02) 跳线`
  （`i2cf_test.c:806-809`），与上述归因一致。
- COVERAGE.md §6.2 的 T1/T2 两项未决项**本轮仍未闭环**（即上文「待用户确认的 3 个问题」）；
  §6.1 D5 指出被选作上游 master 的实例若保留 `pinctrl-noinit` 会导致引脚不挂 I2C 功能、
  症状伪装成"接线错"（`i2c_ls.c:901`）—— 属本轮值得复查的一条候选（**未采集到** overlay 实际取值，
  无法确认是否已 `delete-property`）。
- 报告表「实测(INFO)」列对这 4 项均为 `-`（COVERAGE.md 载明本工程仅 4 项有 INFO 通道，
  这 4 项失败均无 INFO 上报），实际值取自 `serial_TGT.log` 的 ztest 原生断言文本。
