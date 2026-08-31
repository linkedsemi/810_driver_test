# I3C target 静态地址支持修复（SCONFIG.SADDR 移位 + DACHG 未清）

> 2026-08-26，新增 setaasa/setdasa 测试项（SETAASA 广播 / SETDASA 直通分配）
> 时发现并修复的两个驱动缺陷。硬件依据：NXP I3C slave programmers model
> （tests/scripts/i3c_peripheral_programmers_model.pdf）。

## 背景：SETAASA/SETDASA 测试设计

- target 静态地址 `0x5A`（`cpu1_i3c_target/app.overlay` 的 `static-address`，
  写进 SCONFIG.SADDR，硬件据此自动响应 SETDASA 直通和 SETAASA 广播）。
- controller 流程：RSTDAA 清 0x32 → SETAASA 广播 / SETDASA@0x5A 分配 0x45
  → GETPID 验身份 + 探针写验数据通路 → SETNEWDA 恢复 0x32。
- target 证据链：探针写能被新地址寻址 + ISR 内快照 SDYNADDR
  （DAVALID=1 / DADDR=期望地址 / DCAUSE=2）+ magic 字节校验。
- 两个测试项放套件末尾：中途 RSTDAA 清了 0x32，靠 SETNEWDA 恢复；
  恢复失败只影响本项 TEST_END，不波及其他测试。

## Bug 1：`ls_i3c_target_config` 的 SCONFIG.SADDR 未移位（i3c_linkedsemi.c）

- **发现方式**：写测试时代码走查发现（未实际踩炸），推断失效模式为：
  target 完全不响应任何传输（连 ENTDAA 后的正常通信都挂）。
- **根因**：`base->SCONFIG |= config_target->static_addr;` 把 7 位地址值
  直接 OR 进寄存器低 7 位，而 SADDR 域在 bit[31:25]。0x5A=0b1011010 落在
  bit[6:0]，把 SLVNACK(bit1)/S0IGNORE(bit3) 等使能位污染——SLVNACK=1 时
  target NACK 除广播 CCC 外的一切请求。
- **为何以前没炸**：此前所有工程 `static-address` 都不配（=0），OR 0 无感。
  `ls_i3c_dev_init` 里另有一段正确的移位写入（带 mask 清 SADDR），但
  不清低 7 位，污染残留。
- **修复**：`static_addr << I3C_SCONFIG_SADDR_POS`（与 dev_init 段对齐）。
- **验证**：setaasa/setdasa 双核 PASS，且原有 27 项全 PASS（静态地址不影响
  正常 ENTDAA/数据通路）。

## Bug 2：target ISR 从不清 DACHG（SSTATUS bit13）

- **现象**：RSTDAA/SETAASA/SETDASA 会触发 DACHG（PDF：SETDASA/ENTDAA 置位、
  RSTDAA 丢失；SETNEWDA 不触发），DACHG 在使能掩码 I3C_TGT_INTSET_MASK 里，
  但 ISR 没有任何分支清它 → 电平触发下一旦挂起就是中断风暴。
- **修复**：ISR 里按 CHANDLED 同款模式补 W1C 清除：
  `if (SINTMASKED.DACHG) SSTATUS = I3C_SSTATUS_DACHG_MASK;`
- **验证**：setdasa 测试含两次 DACHG 触发（RSTDAA + SETDASA），
  测试通过且后续 TEST_END 通信正常。

## 通用教训

- 寄存器"域写入"必须走 `<< FIELD_POS` 或 REG_FIELD_WR，裸 OR 地址值是
  隐形炸弹——默认配置（=0）下永远不暴露，第一次配非零值就炸。
- 使能了的中断位必须在 ISR 有对应清除分支，逐个对照 INTSET 掩码与
  ISR 清除点审计。

## 后续重构（2026-08-26）：SETAASA 记账修复移到正确层级

- 对照上游 Zephyr main 发现：4.0 基线 `i3c_common.c` 的 SETAASA 记账条件
  写反（`if (ret != 0)` 才记账 = 失败路径记账、成功不记账），上游
  `373771c062`（2025-02-27, "fix the setaasa flow"）已修为 `else`。
  **已 backport 这两行到本地 i3c_common.c**（根因修复）。
- 驱动层两个对应补丁随之删除：
  1. `ls_i3c_init` 里 i3c_bus_init 后的补记账循环（与上游修复语义一致但
     层级错误，且幂等冗余）；
  2. `ls_i3c_transfer` 的 `dynamic_addr==0 → 回退 static_addr + 回写 desc`
     （上游无此设计；修好后是死代码，且会把配置错误静默变成发去静态地址）。
- 测试侧：setaasa 原 3b 步（验证 fallback）反转为负路径契约：
  `dynamic_addr=0/static_addr≠0` 的 desc 传输必须 `-EINVAL` 且 desc 不被改写。
- 验证：全量回归 6 case × 29 项 = 174/174 PASS。
