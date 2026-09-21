# TPM SPI slave (PFR/TPM) 驱动测试报告

- 工程: spid_test
- case: case_default
- 日期: 2026-09-21 13:14:12
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=30s
- 执行标记: SUCCESSFUL
- 内存兜底: 有效
- 结果: PASS (10/10)
- 接线: tpm_spis1(PI04-07) <-> spi1(PA00-PA02+CS) 外部 4 线跳线（master 发帧用例）；其余零接线

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| callback_register | ✅ PASS | token | **读回 `dev->data` 的 `cb`/`user_data` 字段**（按驱动 `struct spid_li… | - |  |
| cold_reset_idempotent | ✅ PASS | token | 连续 3 轮 `cold_reset` 均返回 0 且 `INTR_ENABLE` 保持 `0xff` | - |  |
| cold_reset_state | ✅ PASS | token | 两实例复位后 `SPID_INTR_ENABLE == 0xff` | intr_enable=0xff |  |
| device_ready | ✅ PASS | token | 两实例 `device_is_ready` 均真；DT reg 地址非 0 且**两实例地址不同** | spis1=0x4000c000, spis2=0x4000e000 |  |
| init_register_modes | ✅ PASS | token | FIFO/CRB 写入后按掩码 `0x13` 回读分别为 `0x11`/`0x13`；两者差异位 = bit1；非法模式… | fifo_cfg=0x11, crb_cfg=0x13 |  |
| instance_isolation | ✅ PASS | token | spis1 写 FIFO、spis2 写 CRB 后各自保持；再 `cold_reset(spis1)` 不影响 spi… | spis1=0, spis2=0x13 |  |
| interrupt_register | ✅ PASS | token | ⚠ **无判别力**（2026-09-18 F1 波如实标注）：驱动恒返回 `0`，故"两实例重复安装均返回 `0`"是… | inst=2, irq=58, en_before=0, en_after=1, en_repeat=1 |  |
| intr_state_clear_idiom | ✅ PASS | token | 写 `0xff` 到 `SPID_INTR_STATE` 后读回 `0`；空闲初值经 `ZT_INFO` 上报 | idle_state=0, after_w1c=0 |  |
| master_send_tpm_frame | ✅ PASS | token | master 侧 `spi_transceive` 返回 0（**真断言**：跳线/引脚复用/CS/时钟任一坏掉都会 F… | status=0, cmd_addr=0, frame_len=5 |  |
| register_stability | ✅ PASS | token | `SPID_TPM_CAP` 既非全 0 也非全 1（IP 已时钟/响应）；连续两次读一致；**空闲时 `SPID_TP… | cap=0x660100, didvid=0, rid=0, status=0, access0=0 |  |

## 汇总

OVERALL: PASS
