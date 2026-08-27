# write_delay_read_16：从机"写段 data → 读段欠载"可用准备窗口实测

- 日期：2026-08-27
- 场景：I3C 双核对连（I3C5↔I3C6），SCL 12.5MHz（case_pure_i3c_12m5）
- 需求：write+data+restart+read，2 种时钟源（25M / 50M）下，从机收到写段
  data 后有多少时间可以用来"根据 data 决定回读数据并预填"——即 P1
  （写段最后字节 ACK）→ T-bit 采样前 FIFO 必须就位的实测窗口。

## 测量方法（协议 CMD_INDEXED_READ_DELAY 'Y'）

- controller 写段 3 字节 `{ 'Y', idx, delay_step }`，restart 后读 16 字节。
- target 收到 delay_step 字节后 `spin_ns(delay_step × 50ns)` 忙等，再
  FLUSHTB + 按 idx 预填（模拟"根据 data 计算回读数据"的耗时）。
- controller 扫描 delay_step 0..255（0-12.75µs），记录每档读回字节数：
  - 读满 16B 且 pattern 匹配（idx=0x10 → tgt_tx_buf[0x10..0x1f]）→ 该档来得及
  - 读回 0B（T-bit 提前终止）→ 该档欠载
  - 临界档 = 最后一个读满 16B 的档位 → 窗口 ≈ critical × 50ns
- idx 用 0x10 而非 0x00：读回数据必须来自新偏移，与 FIFO 常备数据
  （0x00..0x0f）区分——防止"FLUSHTB 无效、FIFO 里是旧常备数据"的假成功
  （对照实验确认过 prefill 确实生效，数据来自 tgt_tx_buf[0x10..0x1f]）。
- spin 实测核对：spin_ns 返回实际 cycles（2412 cycles = 4020ns @600MHz，
  与名义 3950ns 一致），TGT_DBG 打印。

## 实测结果（25M 源 5 轮 + 50M 源 3 轮）

| 时钟源 | I3C 内部计数时钟 | SDA_HD | 临界窗口（恰好不欠载的最大延时） |
|---|---|---|---|
| 25MHz | 50MHz | 0 | 6000 / 6150 / 6250 / 6300 / 7050 / 7100 ns（**6000-7100ns 漂移 ±9%**） |
| 50MHz | 100MHz | 1 | 12350 / 12500 / 12700 ns（≈12.5µs） |

- 欠载档 actual=0（非部分字节），首次欠载档在临界档附近 ±2 档内浮动。
- 窗口漂移（±9%）来源：master 间隙/等待期抖动 + CPU 实际频率波动
  （530-547MHz，见下），非从机侧退化——常态化门限按此范围设定。
- 临界档之后 master 下一笔 transfer 报 **Address NACK err=-5** 停止扫描
  （master_stop_step：122-144 档，波动同窗口）。**NACK 边界 ≈ 窗口 + 0.1µs**。

## NACK 边界（从机"多久没塞数据 → read 直接 NACK"）

- **窗口内（≤ 临界档）**：FIFO 空但 T-bit 采样前硬件能发起 → T-bit=0 提前
  终止（短读/0 字节），transfer 正常返回——**不是通信失败**。
- **超窗口（临界档+~2 档）**：从机 ISR 在 write_received_cb 死等 spin 期间
  不服务 I3C 硬件；读地址到来时（P1 后 ~4.5µs，波形）硬件等不到软件处理
  MATCHED → 地址无人 ACK → master 报 **Address NACK、err=-5，通信失败**。
- 名义 vs 实际：CPU 实测 530-547MHz（声明 600），spin 按 600MHz 标定循环 →
  名义 6.1µs 档的实际物理死等 ≈ 6.7µs（×600/547）。
- 50M 源：255 档（12.75µs 名义）内从未 NACK（master_stop=-1）。

## 窗口构成分析

- 协议固定部分：restart + 地址 9 周期 + T-bit 1 周期 ≈ 0.8µs @12.5MHz。
- 实测窗口（6.0-7.1µs / 12.5µs）远大于协议固定部分 → 主要成分是 **master
  （cpu2）写段结束到发出 restart 之间的内部间隙**（软件/硬件准备），
  随 I3C 内部计数时钟变化：内部时钟 100MHz（50M 主频）时间隙反而更大
  （波形参数 FREE/AVAL 等按 clock_frequency 计算，100M 内部时钟下寄存器
  档位不同所致，未深究）。
- 波形（25M 源）P1→read ACK ≈ 4.5µs（master 间隙 + restart + 地址段）；
  实测窗口 6.0-7.1µs 比它大 ≈ 1.5-2.6µs = read ACK 之后到数据时钟开始的
  **master 等待期**（master 在 ACK 后准备数据时钟的空档，从机同样可用）。
- 死等机制结论（WDR timeline，123 步 4 hit/119 miss）：spin 期间观测到的
  MATCHED 全部是**写段残留假匹配**（hit_sst=0x300213 = STREQWR 无 STREQRD），
  真读匹配（STREQRD）在 spin 期间从不出现——死等预算上限 = 读段数据时钟
  开始时刻，由 master 物理时序决定，非寄存器/新 match 寄存器限制。
- 从机侧结论：25M 时钟源下从机有 ≈6.0-7.1µs、50M 时钟源下 ≈12.5µs 可用
  准备时间（远超此前已知的"restart 后 ~3µs prefill 窗口"），软件在
  write_received_cb 内做完计算再 FLUSHTB+预填完全来得及。

## 常态化门限（2026-08-27 新增）

- 该测试项已纳入**常态化监控**：窗口值超出 case 配置区间 → FAIL
  （防驱动/配置改动导致从机准备时间退化或时序基线异常）。
- 配置：`cpu0_i3c_controller/Kconfig` 定义
  `I3C_TEST_WDR_MIN/MAX_CRITICAL_NS`（ns，0=禁用）+ `I3C_TEST_WDR_NACK_BOUNDARY_CHECK`
  （默认 y：stop_step 必须 > 临界档，NACK 不得早于窗口内）；
  case 差异在 `case_*/controller.conf`（25M 12m5 case：[5500, 8000]）。
- 经验：`west build -p auto` 不检测 case 目录 conf 文件变化（§10.9），
  改 controller.conf 后需删 build/controller 全量重建。
- 窗口值上报 INFO: critical_delay_ns（报告"实际"列取用）。

## WDR_LEN：写段内容决定读长度（FIFO 深度临界 17/32/64，2026-08-27）

对 WDR16 的扩展（协议 CMD_INDEXED_READ_DELAY_LEN 'Z'）：写段 4 字节
`{ 'Z', idx, delay_step, len }`，target 收完 len 字节（写段最后字节）后
`spin(delay_step × 50ns)` + FLUSHTB + prefill 16B，读段按 len 返回。
FIFO 深 16，len>16 由 TXSEND 中断 read_processed_cb 续发。len==0 约定 256。

### 判定

1. **critical 存在**：每 len 的 delay 扫描必须有成功档（pattern 前缀
   完整性 bnd≥len 判定，见下）且数据 pattern 对（validate_pattern len 字节）。
2. **len64↔len32 互比**（8 档容差）：续发量 3 倍差距下临界档差 ≤3 档
   （常态实测）；len64 需 3 次续发（TXSEND → read_processed_cb），单次
   续发 ISR 抖动在 len64 上统计性累积——临界档偶发提前 ≤7 档（=350ns，
   2026-08-27 连跑实测 6-7 档）→ 8 档吸收该抖动；续发路径真退化（跟
   不上 T-bit 间隔）时临界档缩小几十档，不被 8 档掩盖。len17（1 字节
   续发）波动 120-146 档（ISR 抖动放大）仅 INFO 上报不判定。

### actual=64 假象（已闭环，详见 I3C_WDRLEN_ACTUAL64_MASTER_DCNT_PADDING.md）

- master 读段 DCNT 固定 64（rd 缓冲大小），与写段 len 无关；读段终止只由
  target T-bit=0 驱动。
- **中途欠载**（供满 32 字节后 FIFO 空，第 33 字节 T-bit=0）→ master 硬件
  继续读满 DCNT 收 0xff（真实读回，对照 full 步 rd[32..]=0xaa 栈残留判别）
  → `actual=64` 假象。len=32 short 步偶发（6 次运行 1 次，11 步）。
- **起始欠载**（首字节 T-bit=0）→ master 收 0 字节（actual=0）。len17/64
  short 步均为起始欠载。
- 判定：`bnd ≥ len`（bnd=第一个 ≠0x10+i 的下标）⟺ rd[0..len-1] 全对；
  残留污染只会把 bnd 往大推，无把"真欠载"改判成功的风险。

### 实测（12m5/6m25 双 case）

| len | 临界档（12m5 / 6m25） | 续发量 | 备注 |
|---|---|---|---|
| 17 | 136-146 / ~124 | 1 字节 | 波动大（ISR 抖动放大），仅 INFO |
| 32 | 118-121 / 124 | 16 字节 | 判定②基准 |
| 64 | 115-120 / 123 | 48 字节 | 与 len32 互差 ≤3 档 |

- 偶发 first_short=30（len32，FIFO 空时刻抖动）被 bnd 判定正确归类不 FAIL。
- nack_recovery 在 len 扫描的 NACK 步后立即验证恢复（与 WDR16 同路径）。

## 顺带修复

- **SDA_HD 按 I3C 内部计数时钟配置**（驱动 i3c_linkedsemi.c 原硬编码 0）：
  `config->clock_frequency` = dts clock-frequency × 2 = 内部计数时钟
  （soc.c CLK_SEL_I3C 主频 × 2 采样）；≥100MHz → SDA_HD=1，否则 0。
  50M 主频（内部 100M）时 SDA_HD=1 后扫描结果不变（SDA_HD 影响 SDR
  读方向 hold，不改变准备窗口），但配置本身随频率正确化。

## 沉淀判定

- 测量方法与结果 → 本存档 + COVERAGE.md 一行（✅）。
- SDA_HD 频率依赖 → 驱动内注释（本层语义，合规 §4.6）。
- 欠载后 master -5 恢复问题 → 观察项，暂不深究（一次性问题，不沉淀
  进 §10/skill）。
