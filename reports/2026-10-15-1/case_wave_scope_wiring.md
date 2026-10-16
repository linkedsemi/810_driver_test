# case_wave — 循环 I2C 波形固件（一页）

**状态：已烧录并运行中**（gdb 常驻 hold，固件持续发波）。波形存在性**未自验**（无示波器），
由用户侧示波器判读。串口已见事务循环（见文末旁证）⇒ 固件**确实在跑**（非挂死/panic）。

## 一句话
周期 **~420 ms**：先发一笔**写**(**地址 0x2A**)、隔 ~50 ms 发一笔**读**(0x2A)、再隔 ~350 ms 进下一帧。
相位逐轮交替：**偶数轮 A = filter on**（空白名单，被拦）/ **奇数轮 B = filter off**（对照）。

**所用地址 = `0x2A`**，依据：`i2cf::slave_addr_scan` 全段(0x08..0x77)实测应答清单 = **`33 50`**
（0x33=片内 target、**0x50=板载器件**）⇒ **0x2A 不在其中 = 实测无器件应答**。
（不用 0x50：板上有实体器件、历史同址撞过 —— KNOWN_ISSUES #110/#111 / :1598。）

## 探测引脚（复核自 DT，与任务书一致）
| 信号 | 引脚 | 说明 |
|---|---|---|
| SCL / SDA | **PE03 / PB02** | **i2cf4 输入侧** —— 上游 i2c2 实际发出的帧。**回答"read 到底是不是 read"就看这里**（推荐探这侧） |
| SCL / SDA | PE02 / PB00 | i2cf4 **输出侧**（滤波后转发给 i2c3），可作对照 |
| **TRIG** | **PA06** | 每笔事务一个脉冲（事务前 10µs 建立、事务后 10µs 撤销）→ **示波器触发源** |
| **DIR** | **PA04** | **写=低 / 读=高** → 一眼分辨方向 |
| PHASE | PA08 | **A=低**(filter on) / **B=高**(filter off) |

> 标记脚在 PORT A，避开框架已占的 PA05/PA07（gpio_toggle 500ms）——本工程 pwm/cap/spi1/uart4
> 全 disabled，PA04/PA06/PA08 无外设 pinmux 占用，已实测可驱动（固件启动即配置）。
> 若探测不到某标记脚，改 `src/i2cf_wave.c` 顶部 3 个 `#define WAVE_*_PIN` 即可。

## 烧录 / 运行
```bash
cd d:/zephyr4_0/810_driver_test/projects/i2cf_test/tests/case_wave
./test.sh          # 仅编译（已编译）
./test.sh -r       # 加载并运行（不重编；gdb 常驻，固件持续跑）   ← 当前状态
./test.sh -g       # 重编 + 加载运行
```
重新点火前必须先清场：`taskkill //F //IM riscv64-unknown-elf-gdb.exe` → 重启 DebugServer
（`C:/uaer_app/debug_server/bin/DebugServerConsole.exe`）→ 再 `./test.sh -r`。

## 示波器设置建议
- 触发：**TRIG(PA06) 上升沿**，电平 ~1.65 V。
- 时基：先 **100 µs/div** 看单笔事务（SCL/SDA/DIR 一位一位）；再 **100 ms/div** 看整帧节奏。
- 通道：SCL(PE03) / SDA(PB02) / DIR(PA04) / TRIG(PA06)。

## 预期看到
- **DIR 高段＝读、低段＝写**；8 位地址字节的 LSB（写=0 / 读=1）应与 DIR 一致
  —— 这就是"read 在线上是不是读"的直接判据。
- 输入侧两相位都应有帧（master 照样发）；A（filter on）每笔被滤器标记、B（filter off）无标记。

## 串口旁证（cpu1 uart1 = COM16，CH342）
```
[WAVE] cyc=62 ph=A | W ret=-5 dt=0ms isr=1 | R ret=-5 dt=0ms isr=1 | period=420ms
[WAVE] cyc=61 ph=B | W ret=-5 dt=0ms isr=0 | R ret=-5 dt=0ms isr=0 | period=421ms
```
`ret=-5`(=-EIO) 两相位一致（0x2A 无人应答）；A 相位每笔 `isr=1` + 串口
`<err> ... address beyond whitelist: i2c@0x2a`（滤波器确实上报了越界），B 相位 `isr=0`。

## 归档
- 构建命令与日志：`build_both.log`（case_default 复验 + case_wave 构建，均 0 warning）
  · `rebuild_case_wave.log`（改址后重建）
- 首轮烧录+采集（dbg_run）：`run_case_wave.log`
- 常驻运行日志：`run_case_wave_continuous.log`
