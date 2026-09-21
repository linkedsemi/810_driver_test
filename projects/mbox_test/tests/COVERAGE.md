# mbox 测试覆盖矩阵

> 工程：`810_driver_test/projects/mbox_test`（双核，cpu1 = target/uart1，cpu2 = controller/uart5）
> 驱动：`zephyr/drivers/mbox/mbox_linkedsemi.c`
> 规范：`SPEC_REFERENCE.md` §6.5（覆盖维度 / 预期数据 / COVERAGE 模板 / INFO 约定行）
> 外部环境：**无外部接线** —— mbox 走片内共享 SRAM 的 MBOX 区（`0x100bf000`，4 KiB，
>   `lsqsh_evb_cpu1.dts:65-69`）；RTC 为片内外设。工程级接线登记见
>   `810_driver_test/README.md`「外设接线记录」。

## 0. 判定口径（2026-09-18 F3 修复后）

判据 = **两核各自 `PROJECT EXECUTION SUCCESSFUL`** 且 **无 FAILED**，且
**全部行为项 PASS**。逐项细节见 §7。

| 口径 | 说明 |
|------|------|
| 逐项上报 | 固件每项打一行 `MBOX_TEST:<core>:(PASS\|FAIL):<name>[:<明细>]`；`<name>` 与本文件 §2/§3 的测试项名一一对应。脚本按 (name, 核) 收编成独立报告项（修复前报告恒只有 2 项，看不出哪条失败） |
| 核判定标记 | 固件在 case 函数返回后按实际状态**择一**打印。失败路径不再只打 `LOG_INF("…FAIL")`（修复前 `main.c` 之后仍无条件打 SUCCESS ⇒ 驱动全坏也判 PASS） |
| boot 门 | 只认"本端口已见本固件 `ZT_BOOT <core>` 之后"的标记（§7.1） |
| 时序门 | `PROJECT EXECUTION` 必须出现在 `ZT_BOOT` 行之后（§7.2） |
| `timed_out` | **不参与判定**（`RUN_TIMEOUT=0` 下 gdb 永不退出，超时是常态）。仅作报告展示 |
| `observe=1` | 观察项：把驱动**当前缺陷**固化成预期值的探针。**不判负**，独立呈现；现状与记录不符时报告提示改判（§3.4） |
| `guard=1` | 结构性守卫：该核对本 case 无行为判据，只证明"已启动并走完入口"，**不构成行为验证**，不计入"全项 PASS"（§3.4） |
| `<err>` 日志计数 | **仅展示，不参与判定**（见 §7.4） |

## 1. 通道与 MTU 一览（板级 dts，两核一致）

| 通道 | 归属节点 | width(B) | depth | P1c 覆盖 |
|---|---|---|---|---|
| ch0 | `mbox_consumer_qspi1` | 24 | 1 | **跳过**（flash delegation server 占用，见 §4） |
| ch1 | `mbox_consumer_otbn_crypto` | 32 | 1 | `case_mbox_consumers` |
| ch2 | `mbox_consumer_espi1` | 8 | 1 | `case_mbox_consumers`（最短通道） |
| ch3 | `mbox_consumer_vuart1` | 8 | 1 | `case_mbox_consumers`（最短通道） |
| ch4 | `mbox_test_ch4`（工程 overlay） | 64 | 4 | 新 case 的数据/探针通道 |
| ch5 | `mbox_test_ch5`（工程 overlay） | 64 | 4 | 应答/短帧契约通道 |

驱动侧约定（`mbox_linkedsemi.c:135-138`）：**rx 回调上报的 `msg.size` 恒等于该通道
width**，与实际发送长度无关；`width` 同时是 `general_fifo_put_size` 的单帧上限（MTU）。

## 2. case_mbox_basic（`MBOX_TEST_CASE_ID = 0`）

帧数契约由 `mbox_test_protocol.h` 的 `MBOX_BASIC_CH4_COUNT/CH5_COUNT` 单点定义，
两侧共用，避免各写一个数字而漂移。

| 测试项（INFO name） | 核 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|---|
| `basic_ch4_frames` | CTRL | `mbox_linkedsemi_send` → `general_fifo_put_size` | 功能通路 | 5 帧全发、`failed=0` | V1 协议契约 | ✅ |
| `basic_ch4_frames` | TGT | `isr` → `rx_callback_handle` → 回调 | 功能通路 | 收到 5 帧、每帧过 `mbox_test_verify()`（magic/channel/payload pattern）、序号连续 | V1 协议契约 | ✅ |
| `basic_ch5_frames` | CTRL | 同上（多通道隔离） | 功能通路/并发 | 3 帧全发、`failed=0` | V1 协议契约 | ✅ |
| `basic_ch5_frames` | TGT | 同上 | 功能通路/并发 | 收到 3 帧、内容与序号全对 | V1 协议契约 | ✅ |
| `basic_mtu_full` | CTRL | `send` 的 `msg->size > width` 分支边界（64 == width） | 边界长度 | `ret == 0` | 驱动 MTU 契约 + `_Static_assert(sizeof == DT width)` | ✅ |

> 判据要点（本轮新增）：**TGT 侧不再"收到就算"** —— 逐帧 `mbox_test_verify()`
> （payload pattern / magic / channel）+ 序号连续性 + **条数等于契约值**。
> cpu2 首帧前 `k_msleep(600)`（`MBOX_BASIC_STARTUP_MS`）等对端注册完回调，
> 消除开机竞态导致的丢首帧（各扩展 case 早已采用同款等待）。
> **失效可检出性**：send 全部失败 ⇒ CTRL 三项 FAIL + `FAILED`；
> 对端一条收不到 ⇒ TGT 计数 0 ≠ 契约值 + `FAILED`（修复前两种情况都打 SUCCESS）。

## 3. P1c 新增 case

### 3.1 `case_mbox_big_crc` —— 4 KB 分片 + CRC32（清单 TC-CPU1-MBOX-02）

| 测试项（INFO name） | 核 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|---|
| `big_payload_crc` | CTRL | `send`→`general_fifo_put_size`（79 片 × 64 B 帧） | 功能通路/边界长度 | target 重组 4096 B、`tgt_st=0`、两核 CRC32 相等 | V2 协议头 + CRC-32(IEEE 802.3, poly 0xEDB88320) | ✅ |
| `big_payload_crc` | TGT | `isr`→`rx_callback_handle` 重组 + 逐字节 pattern | 功能通路/边界长度 | `recv==4096`、`pattern_err==0`、`crc == peer_crc` | 同上 | ✅ |
| `big_crc_chunk_ack` | CTRL | 逐片 ACK 经 ch5 回传 | 中断时序 | 79 片 ACK 全到、`seq` 对齐、`status=0`（`chunk_err/ack_timeout/send_fail` 全 0） | V2 协议契约 | ✅ |
| `big_crc_chunk_ack` | TGT | ch5 发送侧 | 中断时序 | `ack_err == 0` 且收齐 4096 B | V2 协议契约 | ✅ |

分片数 = ceil(4096 / 52) = 79（V2 载荷 52 B/片，末片部分有效）。

### 3.2 `case_mbox_stress` —— 1000 条往返统计

| 测试项（INFO name） | 核 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|---|
| `stress_n_messages` | CTRL | `send`/`isr` 往返 1000 次（逐条 ping-pong） | 统计性质/并发 | `sent==1000`、`send_fail=0`、`ack_timeout=0`、`seq_err=0`、`tgt_recv=1000`、`tgt_seq_err=0`、`tgt_payload_err=0`、`tgt_drop=0`、双核链式 CRC32 相等 | V2 协议契约 + 计数 | ✅ |
| `stress_n_messages` | TGT | 同上（接收/校验/应答侧） | 统计性质/并发 | `recv==1000`、`seq_err=0`、`payload_err=0`、`isr_drop=0`、链式 CRC32 一致 | 同上 | ✅ |
| `stress_rtt_stats` | CTRL | 同上（`k_cycle_get_32` 计时，不打 log） | 统计性质 | 上报 min/avg/max RTT，**不设硬门限**（防 flaky）；仅要求 `samples>0` | §6.5.1 统计维度 | ✅ |

### 3.3 `case_mbox_consumers` —— consumer 通道通路 + 短帧契约

| 测试项（INFO name） | 核 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|---|
| `consumer_ch1_roundtrip` | CTRL+TGT | `fifo[TX/RX][1]` 主路径（32 B 全帧） | 功能通路 | 到达通道 == 1 且 rx size == 32，ACK 回收 | 板级 dts MTU | ✅ |
| `consumer_ch2_roundtrip` | CTRL+TGT | `fifo[TX/RX][2]`（8 B 最短帧） | 功能通路/边界长度 | 到达通道 == 2 且 rx size == 8 | 板级 dts MTU | ✅ |
| `consumer_ch3_roundtrip` | CTRL+TGT | `fifo[TX/RX][3]`（8 B 最短帧） | 功能通路/边界长度 | 到达通道 == 3 且 rx size == 8 | 板级 dts MTU | ✅ |
| `consumer_ch4_roundtrip` | CTRL+TGT | `fifo[TX/RX][4]`（64 B 满帧） | 功能通路 | 到达通道 == 4 且 rx size == 64 | 板级 dts MTU | ✅ |
| `consumer_same_channel_bidir` | CTRL+TGT | 同一通道双向（本核 TX 与 RX 落在对偶 region） | 功能通路 | 4 条探针的 ACK 全部由**原通道**收回/发回 | 驱动 fifo 对偶布局（`tx_fifo_first` 互补） | ✅ |
| `consumer_ch5_short_frame` | CTRL+TGT | `rx_callback_handle` 的 `msg.size = fifo.width` | 边界长度 | 对端发 8 B 时本端仍报 64 | 驱动源码 `:135-138` | ✅ |

> TGT 侧还校验 ch1..ch4 通道的**载荷 pattern**（`0xA0+i`，长度按通道 width 截断）。

### 3.4 `case_mbox_invalid` —— 异常参数（判据全部在 controller 侧）

| 测试项（INFO name） | 核 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|---|
| `inv_send_null_msg` | CTRL | `send` 的 `!CONFIG_SIGNALLING_MODE_SUPPORT` 分支 | 异常参数 | `-ENOTSUP` | 驱动源码 `:191-196` | ✅ |
| `inv_send_bad_channel` | CTRL | `send` 的 `channel >= MBOX_NCHANNELS` 分支 | 异常参数 | `-EINVAL` | 驱动源码 `:172-175` | ✅ |
| `inv_send_oversize_ch4` | CTRL | `send` 的 `msg->size > width` 分支 | 边界长度 | `-EMSGSIZE`（65 > 64） | 驱动 MTU 契约 | ✅ |
| `inv_send_oversize_ch2` | CTRL | 同上（最短通道，9 > 8） | 边界长度 | `-EMSGSIZE` | 驱动 MTU 契约 | ✅ |
| `inv_send_null_data` | CTRL | `send` 的 `msg->size > 0 && msg->data == NULL` 分支 | 异常参数 | `-EMSGSIZE` | 驱动源码 `:180-183` | ✅ |
| `inv_register_bad_channel` | CTRL | `register_callback` 的通道校验 | 异常参数 | `-EINVAL` | 驱动源码 `:208-211` | ✅ |
| `inv_set_enabled_bad_channel` | CTRL | `set_enabled` 的通道校验 | 异常参数 | `-EINVAL` | 驱动源码 `:255-258` | ✅ |
| `inv_zero_size_accepted` | CTRL | `general_fifo_put_size` 的 `size == 0` 路径 | 异常参数 | `INFO pass=1` = 现状"ret == 0（0 长帧被接受，写指针仍推进）" | **观察项**（驱动现状固化） | 👁 **observe=1** |
| `inv_mtu_get_unimplemented` | CTRL | `api.mtu_get` 未接线 → `mbox_mtu_get()` 返回 `-ENOSYS` | 功能通路 | `INFO pass=1` = 现状"`ret == -ENOSYS`" | **观察项**（驱动缺口探针） | 👁 **observe=1** |
| `tgt_invalid_idle` | TGT | 无（判据全在 controller 侧） | — | 只证明 target 已启动并走完入口 | **结构性守卫** | 🛡 **guard=1** |

> **观察项（observe=1）说明**：`inv_zero_size_accepted` 与 `inv_mtu_get_unimplemented`
> 把驱动的**当前缺陷**固化成了预期值 —— 一旦驱动补齐（size=0 改判拒绝 / `api.mtu_get`
> 接线返回通道 width），这两项的 `pass` 会翻成 0。它们**不判负**，脚本把它们渲染成
> `OBS`（现状与记录一致）/ `OBS_DIFF`（现状已变），并在报告里提示
> **"须人工复核并同步改判本行"**。这样回归不会"因为驱动被修好而变红"，
> 也不会让缺陷悄悄被当成规格通过。
>
> **结构性守卫**：本 case 的行为判据 7 项全在 CTRL 侧，TGT 只是待机 4 s。
> 用 `guard=1` 显式声明"本项不是行为证据"，脚本不把它计入"全项 PASS"
> （修复前它作为 `tgt_ok` 的一半参与双核判定，冒充行为验证）。

### 3.5 `case_mbox_fifo_full` —— FIFO 满 / ENOSPC / 使能补排

| 测试项（INFO name） | 核 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|---|
| `fifo_full_enospc` | CTRL | `general_fifo_put_size` 的 `sw_fifo_full` 分支（对端 IRQ 被屏蔽，fifo 保持满） | 边界长度/异常参数 | 前 depth(4) 帧 `ret==0`，后 2 帧 `ret==-ENOSPC`，`other==0` | 板级 dts fifo 深度（固件内 `_Static_assert` 校验） | ✅ |
| `fifo_full_drain_on_enable` | TGT | `set_enabled(true)` → `rx_callback_handle` 补排已入队消息 | 功能通路 | 补排条数 == fifo 深度(4)、`drop == 0` | 驱动源码 `:260-267` | ✅ |

### 3.6 `case_mbox_rtc_sync` —— RTC 双核时间同步（清单 TC-CPU1-RTC-02）

| 测试项（INFO name） | 核 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|---|
| `rtc_read_advances` | TGT | `rtc_ls_get_time` 两次读数（间隔 300 ms） | 统计性质/中断时序 | 前进量 ∈ [0, 3] s，`bad == 0` | RTC 1 Hz 计数 | ✅ |
| `rtc_read_advances` | CTRL | 线上带回来的 `advance_s` 独立复核（`source=payload`） | 统计性质 | 同上 | RTC 1 Hz 计数 | ✅ |
| `rtc_sync_delta` | CTRL | mbox 传递 cpu1 读得的 epoch → cpu2 读同一 RTC 比对 | 功能通路 | 两轮 \|Δt\| ≤ 2 s，且 `frame_bad=0`、`tgt_bad=0` | 容忍窗口 = 1 s 分辨率 + 传输/调度余量 | ✅ |
| `rtc_sync_monotonic` | CTRL | 两轮同步时间戳 | 统计性质 | `e_tgt(第 2 轮) ≥ e_tgt(第 1 轮)` | RTC 单调递增 | ✅ |

> 设计要点：RTC 为片内单一外设，两核各自 init；`rtc_ls_init` 以
> `clock_control_get_status` 判定是否已使能（`rtc_ls.c:412-419`），第二个 init 的核
> 见到时钟已 ON 即跳过 `rtc_cycle_config`/复位，**不会重置计数器**，故双核读数同源可比。
> `rtc_read_advances` 在两核各上报一次：TGT 是本地自测，CTRL 是对线上值的独立复核。

## 4. 跳过项清单

| 候选 | 原因分类 | 说明 |
|------|---------|------|
| `case_mbox_priority`（`priority_preempt`，清单 TC-SYS-MBOX-02） | **驱动不支持** | `mbox_linkedsemi.c` 无优先级概念：V2/V1 消息结构无优先级字段；单 ISR 顺序遍历所有通道、无仲裁/排队；`set_enabled` 仅掩码单一 IRQ。平台侧无法注入优先级差异 → 跳过 |
| `ch0` = `mbox_consumer_qspi1` 通路（`all_consumers` 的一部分） | **无法确定性判定/语义污染** | 该通道在双核构建中已由 flash delegation 占用：cpu1 `soc_flash_ls.c:506`、cpu2 `soc_flash_ls_client.c:84` 均注册了回调，测试流量会与 delegation 协议互相解析 → 跳过并在此登记（其余 3 个未覆盖 consumer 已由 `case_mbox_consumers` 补全） |
| 单帧 4 KB（而非分片） | **平台无法承载** | 共享 MBOX 区仅 4 KiB（`lsqsh_evb_cpu1.dts:65-69`，上界由 SRAM 布局决定：`0x100bffff` 以上即 cpu1 入口区），驱动为每个 consumer 同时分配 TX+RX 两个 region，单帧 MTU 上限远小于 4 KB → 4 KB 数据改以 79 片流式传输 + 端到端 CRC32 覆盖（已实现） |
| `mbox_mtu_get` 契约项（Zephyr 要求 data 模式返回非 0） | **驱动缺口（已登记为观察项）** | 驱动定义了 `mbox_linkedsemi_tx_mtu_get`/`rx_mtu_get` 但未挂入 `mbox_driver_api.mtu_get` → 统一返回 `-ENOSYS`。已登记为 `inv_mtu_get_unimplemented`（`observe=1`）⇒ **不判负**；驱动补齐后该项翻 `OBS_DIFF`，须同步改判为 `== 通道 width` |
| `size == 0` 帧的语义 | **驱动现状（已登记为观察项）** | 现状：0 长帧被接受、写指针仍推进、对端 rx size 恒报通达 width。已登记为 `inv_zero_size_accepted`（`observe=1`）⇒ **不判负**；驱动若改为拒绝，该项翻 `OBS_DIFF`，须同步改判为 `-EMSGSIZE`/`-EINVAL` 之一 |

## 5. 预期数据来源

| 类别 | 来源 |
|------|------|
| 帧格式 / 分片规则 / 状态码 | `tests/include/mbox_test_protocol.h` V2 段（P1c 纯追加） |
| 帧数契约（ch4=5 / ch5=3） | `mbox_test_protocol.h` 的 `MBOX_BASIC_CH4_COUNT` / `MBOX_BASIC_CH5_COUNT`（两核共用） |
| CRC32 | IEEE 802.3 反射算法，双核同源内联实现，初值 0 |
| 通道 MTU / fifo 深度 | 板级 dts `mboxes = <&mbox ch width depth width depth>`（固件用 `DT_PHA_BY_IDX` 取用，深度有 `_Static_assert` 校验） |
| RTC 容忍窗口 ±2 s | RTC 1 s 分辨率 + mbox 传输与线程调度余量 |
| `MBOX_STRESS_COUNT = 1000` | 压力规模（覆盖统计性质维度），延迟门限不写死（防 flaky） |
| 逐项结果槽位序 | `tests/include/mbox_test_report.h` 的 `enum mbox_item`（固定序，供 GDB 兜底按符号读 `test_results[]`，§12.3） |

## 6. 已知偏差

| 项 | 现状 | 说明 |
|----|------|------|
| `tests/scripts/{dump_results.py,dbg_run.py}` 缺失 | 工程只有 `tests/run_test.py` | 本工程不走 ztest 套件（`zt_run_all()` 未调用，`test_results[]` 无 ztest 记账）。**但**逐项结果已按 §4.1 公共模块 `common/test_results.c`（`tr_init`/`tr_record`）记账到固定槽位（`enum mbox_item`）——`test_results_valid` 哨兵由 `mbox_report_init()` 置位，内存兜底路径**已具备数据源**，只是还缺一个本工程的 `dump_results.py` 读取脚本（未实现，不在本轮范围） |
| `prj.conf` 未显式 `CONFIG_LOG_BACKEND_UART` / `CONFIG_LOG_PRINTK` | 沿用工程原值 | 按 qsh-test-validate §1.3「未显式写不算错」。**注**：实测构建产物 `CONFIG_LOG_PRINTK=y`（LOG_MODE_DEFERRED），故 `printk` 与 `LOG_*` 同走延迟日志缓冲；`ZT_BOOT` 与 verdict 标记是同一线程按序入队，端口上的先后序仍成立（§7.2），但**日志缓冲溢出丢消息**是这套机制的固有残余风险（`CONFIG_LOG_BUFFER_SIZE=16384`，本工程日志量远低于该值） |
| cpu1 `app.overlay` 未 `#include common/gpio_toggle.dtsi` | 工程原状 | 本轮不改既有 overlay |
| 双核无"两遍烧录 prime 轮" | 工程原状（本轮**有意保留**） | mbox 走 gdb load（target elf + controller bin），非 flash 固化；且本工程刻意保留"只看标记、不看窗口"的判定哲学。prime 轮属 I-cache 冲刷措施，本轮不引入（见 §7.3） |
| `mbox_test_protocol.h` 的 `MBOX_BIG_CRC_CHUNKS` 注释曾写"86 片" | 本轮已更正为 79 | 实际 = ceil(4096 / 52) = 79；注释与 COVERAGE §3.1 一致 |

## 7. 判定门槛细则（2026-09-18 F3 修复）

### 7.1 boot 门（§10.30）

每个端口**必须先出现本固件的 BOOT 标记**，之后的 verdict 标记才算数：

| 端口 | BOOT 标记 |
|------|-----------|
| controller / cpu2 / uart5 | `ZT_BOOT CTRL` |
| target / cpu1 / uart1 | `ZT_BOOT TGT` |

他方残留镜像/上一轮输出没有本固件 BOOT 标记 ⇒ 其 `PROJECT EXECUTION` 一律忽略。
端口未见过 BOOT ⇒ 该核 execution 项 = `NO_BOOT`，case 判 FAIL 并附
"测试环境可疑（非驱动失败）"注记。

### 7.2 时序门

`PROJECT EXECUTION SUCCESSFUL|FAILED` 必须出现在**本端口 `ZT_BOOT` 行之后**。
BOOT 之前出现的同名标记（上一轮残留 / 崩溃固件的回显）不当证据 ⇒ execution 项
= `NO_MARKER`，判 FAIL。

另：同一端口上 `SUCCESSFUL` 与 `FAILED` **并存**时判 FAIL（报告注明"标记自相矛盾"）；
`SUCCESSFUL` 出现次数 ≠ 1 也判 FAIL。修复后固件每核恰打一个标记。

### 7.3 本工程保留的哲学：不看 `timed_out` 判 FAIL

`test.sh -r` 把 `RUN_TIMEOUT` 置 0 ⇒ gdb `continue` 后永不退出 ⇒ **每次 run 都
必然"超时"**。`timed_out` 与测试成败无关，**只作报告展示**（`mbox_test` 是
marker 式判定的参考实现，另一个 agent 的 F1 修复刻意采纳了这套哲学）。

因此本轮**补的是门（boot + 时序），不是加窗口判据**；`timed_out` 维持不参与判定。
脚本另加了"两核都已见 BOOT + verdict 即提前收敛"的优化 —— 只影响等待时长，
不改变任何 PASS/FAIL 结果。

### 7.4 `<err>` 日志计数仅展示

`ctrl_errs` / `tgt_errs`（`<err>` 行计数）写进报告但**不参与判定**。原因：
`case_mbox_invalid` 的预期拒绝路径本身就由驱动打 `DEV_ERR`（`mbox_linkedsemi.c`
各错误分支），把它当门限会让这个 case 永远红。若将来要当门限，必须按 case 给
"预期 err 条数"，属独立议题。
---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
> 来源标签：`handcalc`(手算) `spec`(外部规范/寄存器或 DT 定义) `golden`(黄金向量)
> `crosscheck`(跨来源交叉) `contract`(API 契约值) `driversrc`(转录自被测驱动实现，
> **只证"没变"不证"仍对"**) `samecode`(同源复算，**非独立**) `!none`(无确定预期)。

```expect
# 测试项 | 断言 | 来源标签:说明
basic_ch4_frames | !none | !none:双核各 5 帧、failed=0 的合取（detail 里是复合串）
basic_ch5_frames | !none | !none:同上（3 帧）
basic_mtu_full | !none | !none:ret == 0，单值但无 INFO 键固定 ⇒ 不硬凑
big_payload_crc | !none | !none:4096B 重组 + 两核 CRC32 相等的合取
big_crc_chunk_ack | !none | !none:79 片 ACK + 收齐 4096B 的合取
stress_n_messages | !none | !none:1000 帧收发计数的合取
stress_rtt_stats | !none | !none:原表明确"**不设硬门限**（防 flaky）" ⇒ 无确定预期
consumer_ch1_roundtrip | !none | !none:到达通道 + rx size 的合取
consumer_ch2_roundtrip | !none | !none:同上
consumer_ch3_roundtrip | !none | !none:同上
consumer_ch4_roundtrip | !none | !none:同上
consumer_same_channel_bidir | !none | !none:4 条探针 ACK 原通道收回，非标量
consumer_ch5_short_frame | !none | !none:对端发 8B 本端仍报 64，非标量
inv_send_null_msg | !none | !none:-ENOTSUP，单值但无 INFO 键固定 ⇒ 不硬凑
inv_send_bad_channel | !none | !none:-EINVAL，同上
inv_send_oversize_ch4 | !none | !none:-EMSGSIZE，同上
inv_send_oversize_ch2 | !none | !none:-EMSGSIZE，同上
inv_send_null_data | !none | !none:-EMSGSIZE，同上
inv_register_bad_channel | !none | !none:-EINVAL，同上
inv_set_enabled_bad_channel | !none | !none:-EINVAL，同上
inv_zero_size_accepted | !none | !none:原文断言是"记录现状（0 长帧被接受）"，非正确性预期
inv_mtu_get_unimplemented | !none | !none:原文断言是"记录现状（-ENOSYS）"，非正确性预期
tgt_invalid_idle | !none | !none:原表自述"只证明 target 已启动"，无判别力
fifo_full_enospc | !none | !none:前 4 帧 0 / 后 2 帧 -ENOSPC 的合取
fifo_full_drain_on_enable | !none | !none:补排条数 + drop 计数的合取
rtc_read_advances | !none | !none:前进量 ∈ [0,3]s 且 bad==0 的合取
rtc_sync_delta | !none | !none:两轮 |Δt| ≤ 2s 的合取
rtc_sync_monotonic | !none | !none:跨轮次的不等式关系，非单值
```
