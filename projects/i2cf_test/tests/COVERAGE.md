# I2C filter 测试覆盖矩阵

- 驱动: `modules/lib/pfr_tpm/drivers/pfr/i2c/i2c_filter.c`（PFR/TMP I2C 白名单滤波器）
- 接口: `modules/lib/pfr_tpm/drivers/pfr/i2c/i2c_filter.h`
- 实例: `i2cf1..i2cf4` @ `0x40090000 / 0x40090800 / 0x40091000 / 0x40091800`（overlay 全 `okay`）
- 关键常量: `LINKEDSEMI_I2C_F_ADDR_NUM=16`、`LINKEDSEMI_I2C_F_REMAP_SIZE_BYTE=32`、
  `LINKEDSEMI_I2C_F_REMAP_SIZE_U32=8`（`i2c_filter.h:10-12`）；
  `WHITELIST_ADDRESS` 字段位宽 **7 bit**（`reg_i2c_filter.h:57`）

---

## 1. 拓扑推导（2026-09-17，证据 = 生效 DT + 驱动源码）

> **权威证据来源**：已编译的 `tests/case_default/build/target/zephyr/zephyr.dts`
> （即 `dtc` 处理完所有 overlay 后的**最终**设备树）。本工程 overlay 对 `i2cf4`
> **只改 `status`**，未改写 `i2c` 或 `pinctrl-0`（已逐行核对）。

```dts
i2cf4: i2cf@40091800 {
        compatible = "linkedsemi,i2c-filter";
        i2c = < &i2c4 >;                                                    /* ← DT 固定 */
        pinctrl-0 = < &i2cf4_sclo_pe02 &i2cf4_sdao_pb00
                      &i2cf4_scli_pe03 &i2cf4_sdai_pb02 >;                  /* ← DT 固定 */
        pinctrl-names = "default";
        status = "okay";
};
```

### 1.1 DT 决定的部分（无可协商）

| 事实 | 出处 |
|---|---|
| `i2cf4.i2c = <&i2c4>` → **配对控制器 = i2c4** | 生效 DT；模块 dtsi `:949-958` |
| `i2cf4.SCLO/SDAO = PE02/PB00`，而 `i2c4 SCL/SDA = PE02/PB00` → **O 侧与 i2c4 同引脚** | 模块 dtsi `:894-902` |
| `i2cf4.SCLI/SDAI = PE03/PB02` → **独立引脚**，滤波器输入/上游侧 | 模块 dtsi `:949-958` |

**⇒ 两条硬结论：**

1. `i2c4` 是滤波器**输出/下游**侧的配对控制器（旁路 master：`switch_to_master()`
   让它顶替滤波器直驱该侧，`i2c_filter.c:263-276`）。
2. **`i2c4` 不可能是"被滤波器门控的上游 master"** —— 引脚互斥：filter 模式下
   PE02/PB00 归滤波器（`switch_to_filter` 应用滤波器 pinctrl），master 模式下
   滤波器被旁路。两个角色在同一组引脚上不可共存。

### 1.2 DT **不**决定的部分（纯外部跳线，仓库内查不到）

- **谁被跳线接到 `SCLI/SDAI(PE03/PB02)` 当上游 master**；
- **`SCLO/SDAO(PE02/PB00)` 侧挂的是外接从设备，还是另一路被跳线过来的控制器**。

用户前后给过三种说法：`只连 I2C4↔I2CF4` → `I2C4 → I2CF4 → I2C3` →
`I2C2 > I2CF4 > I2C3`。**这些差异全部落在 1.2 的"外部跳线"范畴**，与 1.1 的
DT 事实**不冲突**；同时它们也**无法由仓库证实或证伪**。

### 1.3 对"引脚级事实不支持 I2C2"的更正（重要）

协调方指出"`i2c2` = PB04/PB01 与 `i2cf4` 不同引脚 ⇒ 不支持 I2C2"。该推理
**只对"配对控制器"这一角色成立**（i2c2 的配对滤波器确实是 i2cf2，不是 i2cf4），
但**不能排除 i2c2 作为"被跳线到 I 侧的上游 master"**：

- **配对控制器**（`i2c` phandle，O 侧同引脚）：DT 固定 = **i2c4**，不可换。
- **上游 master**（I 侧跳线）：DT 里**没有任何属性描述它**。i2c2(`0x40083400`)、
  i2c3(`0x40083800`) 或任何实例都可能，**只能靠接线确认**。

两个角色是**不同的东西**，不能用一个的引脚关系去否定另一个。

**⇒ 因此：**
- 写"旁路"用例 → 用 `i2c4`（DT 固定，已如此实现）。
- 写"被滤波器门控"的用例 → 上游 master 做成 **DT alias `i2cfupstream`**，
  当前取值见 `app.overlay`。**换实例只改 overlay 一行，用例代码零改动。**

### 1.4 实现约束：`pinctrl-noinit`（已在 overlay 处理）

`zephyr/drivers/i2c/i2c_ls.c:901`：
```c
#if defined(CONFIG_PINCTRL)
    if (!dev_config->pinctrl_noinit) { i2c_ls_pinctrl(dev, PINCTRL_STATE_DEFAULT); }
#endif
```
模块 dtsi 给 `i2c1..i2c4` **一律**标了 `pinctrl-noinit`（它们是各 `i2cfN` 的配对
控制器，引脚默认归滤波器）。被选作上游 master 的实例必须去掉该标记，否则其引脚
停在 GPIO/模拟态、**根本驱动不了总线**（症状会伪装成"接线错/器件不在位"）。
属性在 `zephyr/dts/bindings/i2c/linkedsemi,ls-i2c.yaml:48` 有绑定声明，删除合法。
已在生效 DT 中核实：`i2cfupstream` 所指实例无 `pinctrl-noinit`，`i2c4` 保留。

---

## 2. 用例分组（按"是否依赖下游从设备"）

> 用户提示"I2CF 那路测试有回环了，可能不需要挂 eeprom"；且明确"片外信息可能不对，
> 确认有问题就先跳过"。故按外部依赖度分组，**优先做零外部器件的那批**。

### A 组 —— 零外部器件（仅需 DT/寄存器，或仅需上游 master 跳线）

| 用例 | 依赖 |
|---|---|
| `device_ready` `cold_reset_state` `enable_channel_boundary` `fill_dump_roundtrip` `addr_7bit_field` `invalid_args` `duplicate_addr_rejected` `en_clears_table` `filter_set_bits` `scl_hold_time_roundtrip` `switch_master_filter` `callback_register` `concurrent_fill` | **纯寄存器/API 契约**，零接线 |
| `isr_addr_beyond_whitelist` | 仅需**上游 master 跳线**到 `SCLI/SDAI`；**不需要下游从设备**（判据来自滤波器自身锁存的违规信息 + ISR） |
| `slave_addr_scan` | 仅需 `i2c4`（DT 固定的旁路 master）；**全地址扫描本身即用例**，零应答也是合法结果 |

### B 组 —— **依赖外部从设备（用户已降级为可选）**

| 用例 | 依赖 |
|---|---|
| `master_eeprom_rw` | 下游从设备（走 `i2c4` 旁路通路） |
| `filter_blocks_nonwhitelist` | 下游从设备（"被拦"必须与"无器件"可区分 ⇒ 需要 ACK 源做对照） |
| `filter_passthrough_whitelist` | 下游从设备（需要可读回的已知数据） |

> **用户裁定（2026-09-17）**：*"I2CF 测试中，如果已经两个 I2C 回环，这个已经不重要了，
> 可以不测，或者你可以尝试 scan 一遍所有地址"*、*"如果还行不通就先搁置，收尾我再单独看"*。
>
> 据此的处置：
> 1. **不再纠缠 EEPROM 具体地址**（原先试 `0x51`/`0x52` 的做法作废），改为
>    `slave_addr_scan` 在标准 7-bit 段 `0x08..0x77` **全地址扫描**并上报应答清单。
> 2. **不因从设备缺失而阻塞任何用例**：B 组三项一律先扫描（`i2cf_scan_slaves_bypass()`，
>    走旁路通路、不经滤波器）；**扫不到就 `ztest_test_skip()`**（记为 SKIP，不是 FAIL）。
> 3. 若实测确认无外部从设备 → B 组三项保持 SKIP，**不作为阻塞项**；用户收尾时单独看。
> 4. 片内 software target 方案已评估可行（见 §6），**未落地**。

---

## 3. 外部路径（SPEC_REFERENCE §6.6）

- `外部: 上游 master（overlay alias i2cfupstream）跳线到 i2cf4.SCLI/SDAI(PE03/PB02)`
  —— **上游 master 的真实实例由实测判定**（DT 只能证明"不是 i2c4"，见 §1）
- `外部: i2cf4.SCLO/SDAO(PE02/PB00) 侧的下游从设备（如有）` —— **用户已降级为可选**，
  不再指定地址；由 `slave_addr_scan` 全段扫描发现
- ⚠️ **B 组三项依赖该从设备，扫不到即 SKIP（非 FAIL）**

接线变更后**必须重新回归**，不得复用旧报告。`switch_to_master` / `switch_to_filter`
是**器件内 pinctrl 状态切换**（i2c4 ↔ i2cf4 同引脚），不是外接线。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| device_ready | `I2C_FILTER_INIT` 宏（`i2c_filter.c:363-395`）`DEVICE_DT_INST_DEFINE` + `.i2c = DEVICE_DT_GET(DT_INST_PHANDLE(inst, i2c))`；`i2cf_i2c_dev()`（`:256-261`） | 功能通路 | 4 实例 `device_is_ready` 均真；绑定 i2c 设备非 NULL 且 ready；i2cf4 绑定 i2c4 | DT 绑定契约（`linkedsemi,i2c-filter.yaml` 要求 `i2c` phandle） | ⏳ |
| cold_reset_state | `linkedsemi_i2c_filter_cold_reset()`（`:292-349`）：clock off → `reset_line_toggle` → clock on → `pinctrl_apply_state` → `SMBF_REG_ENABLE=1`（`:342`）→ `INTR_MSK`（`:343`）→ `_en(false,false,true)` → `_config_scl_hold_time`（`:346`） | 复位/掉电 | `SMBF_REG_ENABLE==0x1`；`INTR_MSK==0x3`；`SCL_HOLD_TIME == DT scl-hold-time`（`0xfa`） | 驱动源码常量 + DT 属性 | ⏳ |
| enable_channel_boundary | `linkedsemi_i2c_filter_enable_channel()`（`:107-132`）：越界守卫 `idx > (16-1)` → `-EINVAL`（`:114`）；`\|= BIT(idx)` / `&= ~BIT(idx)`（`:122-126`） | 边界长度 | idx 15 → 0 且 `SMBF_ADDRESS_ENABLE[15]` 置位；idx **16** / **255** → `-EINVAL`；disable 后位清零 | 驱动源码（`LINKEDSEMI_I2C_F_ADDR_NUM-1` 为 ±1 边界） | ⏳ |
| fill_dump_roundtrip | `fill_bitmap()`（`:134-188`）分组偏移 `(idx>>2)<<2`（`:168`）、`SMBF_ADDRESS_INDEX`（`:174`）、8 字写入（`:175-179`）、自动置 enable（`:181-183`）；`dump_bitmap()`（`:190-222`） | 功能通路 | 16 通道逐个：dump addr == fill addr；32 B bitmap `memcmp == 0`；填满后 `SMBF_ADDRESS_ENABLE == 0xFFFF` | 驱动源码（读写同一寄存器组） | ⏳ |
| addr_7bit_field | `whitelist_address_t.WHITELIST_ADDRESS` 7 位字段（`reg_i2c_filter.h:53-59`）经 `fill_bitmap`（`:171`）/ `dump_bitmap`（`:211`）往返 | 边界长度 | addr `0x7f` 原样回读；addr `0x80` 回读被截断为 `0x00` | 寄存器字段位宽声明（`WHITELIST_ADDRESS : 7`） | ⏳ |
| invalid_args | `fill_bitmap` 越界（`:142-144`）/ NULL bitmap（`:145-148`）；`dump_bitmap` 越界（`:198-200`）/ NULL bitmap（`:201-204`） | 异常参数 | 四种非法入参均 `-EINVAL` | API 契约（驱动显式分支） | ⏳ |
| duplicate_addr_rejected | `fill_bitmap` 重址扫描（`:152-166`）：命中 `it_idx != idx` 且对方 enable 位为 1 → `-EINVAL`（`:157-164`） | 异常参数 | ch0 填 `0x50` 后 ch1 同址 → `-EINVAL`；关掉 ch0 后同址 → `0`；同址填回自身 → `0` | 驱动源码（判据 = 对方 `SMBF_ADDRESS_ENABLE` 位） | ⏳ |
| en_clears_table | `linkedsemi_i2c_filter_en()`（`:224-254`）`clr_tbl` 分支（`:242-249`）：16 通道 × 8 字清零 | 功能通路 | 16 通道填非零后 `_en(_,_,_,true)` → 全部 16×8 字回读为 0 | 驱动源码 | ⏳ |
| filter_set_bits | `_en()` 的 `smbf_set_t` 负逻辑映射（`:231-236`）：`BLOCK_DISABLE = filter_en?0:1`、`FILTER_DISABLE = wlist_en?0:1` | 功能通路 | 4 种入参组合的 `SMBUS_FILTER_SET` 位回读符合负逻辑表；`MASTER_WRITE_MODE` 恒 0 | 驱动源码三元表达式 | ⏳ |
| scl_hold_time_roundtrip | `config_scl_hold_time()`（`:52-67`）读改写 `SMBF_CONTROL0_REG.SCL_HOLD_TIME` | 功能通路 | `0x0000/0x0001/0x00ff/0xffff/0x1234` 均原样回读（16 位字段） | 寄存器字段位宽（`reg_i2c_filter.h:61-68`） | ⏳ |
| switch_master_filter | `switch_to_master()`（`:263-276`，`i2c_ls_pinctrl(master=i2c4, DEFAULT)`）与 `switch_to_filter()`（`:278-290`，`pinctrl_apply_state(i2cf4 pcfg)`）；判据读 **`SYSC_APP_AWO->IO_FUNC[f][p]` 复用位**（写路径 `ls_soc_gpio.c:831`） | 功能通路 | 两组切换及重复同向切换均返回 0；**且 PE02（`i2c4.SCL` 与 `i2cf4.SCLO` 的共同物理 pad）的复用档位互斥翻转**：cold_reset 后属 i2cf4(func2)；`switch_to_master` 后属 i2c4(func1) 且 func2 清；`switch_to_filter` 后反过来；重复同向切换幂等 | 驱动源码 + pinctrl 写路径（`pinctrl_lsqsh.c:13-105` → `ls_soc_gpio.c:951-981`/`:831`）+ DT `i2c4`/`i2cf4` 的 `pinctrl-0`。**2026-09-18 F1 波改判**：原实现只断两个恒返回 0 的 API 返回值（不可能 FAIL） | ⏳ |
| callback_register | `register_callback()`（`:69-80`）：纯指针落地，恒 `0` | 功能通路 | **读回 `dev->data` 的 `cb` / `user_data` 字段**（按驱动 `struct linkedsemi_i2c_filter_data`（`i2c_filter.c:44-48`）同布局复刻视图）：注册后 `cb == 注册的函数指针`、`user_data == 注册的指针`；覆盖注册 NULL 后两者归零；4 实例各自落地 | 驱动源码（字段赋值）+ 白盒布局视图。**2026-09-18 F1 波改判**：原实现只断恒 0 的返回值 | ⏳ |
| concurrent_fill | `dev_data->lock`（`k_mutex_lock(&dev_data->lock, K_FOREVER)`，`:150/:206/:119/:238` 等）对 `SMBF_ADDRESS_INDEX` 选择序列的保护 | 并发 | 双线程各管 8 通道 × 4 轮 fill+dump，全部 addr/bitmap 一致（无 INDEX 串扰） | 驱动 mutex 契约 + 读写序列 | ⏳ |
| isr_addr_beyond_whitelist | ISR 路径：`linkedsemi_i2c_filter_isr()`（`:82-105`）读 `INTR_STT`（`:89`）→ 写 `INTR_CLR`（`:90`）→ `ADDRESS_BEYOND_WHITELIST` 分支（`:93-95`）→ `dev_data->cb()`（`:102-104`）。触发源 = 空白名单（16 通道全关）仍 `_en(true,true,false)` | 中断/时序 | 上游 master 访问未授权地址 → ① 回调被调用（`isr_count>=1`）② `SMBF_NONWHITELIST` 锁存**非零**违规信息 ③ 主机侧返回负值。**零外部器件** | 驱动源码 ISR 分支 + `外部:` 上游跳线（见 §3） | ⏳ |
| filter_blocks_nonwhitelist | 白名单**非空但只授权另一地址**：`fill_bitmap(dev, 0, authorized, 全 1)`（`:134-188`）+ `_en(true,true,false)`；未授权地址的拦截语义 | 功能通路+异常参数 | 上游访问 `slave`（白名单只授权 `authorized`）→ 负值；**单变量对照**：**只把 `slave` 也授权进白名单、其余全不动**再读一次 → 0。**需下游从设备；扫不到则 SKIP**。★ 2026-09-23 改判：原判据"filter off 后同址 → 0"**不可满足、已废**（该期望只是寄存器位映射推出的③级推断、从未被独立验证）。⚠ **因果注意**：曾据一个判别实验断言"关滤波器 ≠ 直通"，**该判据已撤回**（实验两条读数无判别力）—— 真正原因是**该断言位置紧接一次被拒事务之后、本就多半不可测**（KNOWN_ISSUES #112 / #116） | 驱动源码（`SMBF_ADDRESS_ENABLE` 位 + 该地址命令位图共同决定放行） | ⏳ |
| filter_passthrough_whitelist | 放行方向：授权从设备地址 + 全命令码后，上游 master 的 `i2c_burst_read` | 功能通路 | 上游经滤波器读回 == 预先经旁路写入的 pattern（逐字节 `zassert_mem_equal`）；偏移 `0x00..0x07` 备份/恢复。**需下游从设备；扫不到则 SKIP** | 外部器件 + 透传通路。破坏性受控 | ⏳ |
| master_eeprom_rw | `i2cf_i2c_dev()`（`:256-261`）+ `switch_to_master()`（`:263-276`）→ Zephyr `i2c_burst_read/write` → `switch_to_filter()` | 功能通路 | 从设备写入 pattern 后回读逐字节相等；偏移 `0x00..0x0f` 备份/恢复。**需下游从设备（旁路通路）；扫不到则 SKIP** | 外部器件（读写契约） | ⏳ |
| slave_addr_scan | 旁路通路（`switch_to_master()` `:263-276` → `i2c4` 直驱 O 侧）+ 逐地址 `i2c_read` 探测 | 功能通路 | ⚠ **观测项，无判别力**（2026-09-18 F1 波如实标注）：唯一真判据 = 旁路 master 就绪；应答清单经 `ZT_INFO` 上报供人工判读。结果完全由**外部接线/器件是否在位**决定，平台内无对照量；**零应答又是用户裁定的合法结果** ⇒ 不存在"驱动正确必过 / 典型失效必不过"的判据。原两条断言（"范围 `0x08..0x77` 内" + "无重复"）**恒真**（`found[]` 由循环变量 `addr` 赋入且每地址只探一次），本轮已删除 | `外部:` 下游总线。**不因无器件失败**。⚠ 改名需同步 `tests/run_test.py:70` 与 `tests/scripts/dump_results.py:61` 的 `TEST_NAMES`（那两个文件本轮**不在写域**，另一 agent 在重写）⇒ 故**保留原名**，仅此处与源码注释标注无判别力 | ⏳ |
| enum_matrix | 白名单判定路径：`SMBF_ADDRESS_ENABLE` 位 + ISR `ADDRESS_BEYOND_WHITELIST` 分支（`i2c_filter.c:93-95`）+ `switch_to_master/filter`（`:263-290`）。9 格 = 白名单内/外 × 有/无器件 × 滤波开/关，每格跑读、写两方向 | 功能通路 + 异常参数 | 9 格 × 读/写 各自 ret / ISR 增量合计 / `SMBF_NONWHITELIST` 锁存：C1(内/有)0,0,0 · **C2(内/无)-5,0,无** · C3(外/有)-5,3,锁 0x3300 · C4(外/无)-5,3,锁 0x2a00 · C5/C6/C7(off)-5,0,无 · C8(旁路/有)0,0,0+真数据 · C9(旁路/无)-5,0,无。核心：**上报只由白名单内外决定**（C2↔C4 对照），与读写方向/器件应答无关 | `reports/2026-10-15-3/RESULT_enum_matrix.md` 实测（22 轮零方差）；地址 `0x33`=片内 target（有器件）、`0x2A`=实测无器件（`slave_addr_scan` 应答清单 `33 50`） | ✅ 2026-10-15 上板 19/19 PASS |
| scan_burst_drain | 白名单空 + filter on 下逐址扫描（`SMBF_ADDRESS_ENABLE` 全 0 ⇒ 每址越界）+ ISR 计数（`i2c_filter.c:93-95`）。三种事务类型：1 字节写 / 0 长度 quick command / 读 | 功能通路 + 中断时序 | 每类型 `isr == probed`（112 址各一次上报）；`elapsed_ms < 3000`（实测 ≈16 ms）。★ 真正的 `i2cdetect`/Zephyr `i2c scan` 用 **0 长度 quick command**（`i2c_shell.c` `msgs[0].len=0U`）；旧注释误称 1 字节写，已更正。⚠️ BEGIN→END 到达差（日志驱逐跨度）随后端变化 ⇒ 只作 `ZT_INFO` 观测、不写断言 | 驱动源码 ISR 分支 + 本工程实测（`reports/2026-10-14-3/case_quick_scan/metrics.json`：三类型均 ≈16 ms、isr==probed） | ✅ 2026-10-15 上板 19/19 PASS |

> 状态标记：`⏳` = **仅编译通过，待 P6 首次实测**；`✅` = 已上板实测通过。
>
> 注：`enum_matrix` / `scan_burst_drain` 两项 2026-10-15 首次上板实测通过（`reports/2026-10-15-6/`）。
>
> **P6 运维提示**：`isr_addr_beyond_whitelist` / `filter_blocks_nonwhitelist` 会**实触发**
> `linkedsemi_i2c_filter_isr()`，驱动自身在 ISR 内打 `DEV_ERR`（`:94`/`:97`）。
> 本工程 `CONFIG_LOG_MODE_DEFERRED=y` ⇒ 消息入 buffer 由 log 线程异步输出，ISR 内不阻塞，
> **预期日志出现 `address beyond whitelist: i2c@0x..` 一类行属正常**，不是失败信号。
> （ISR 内打日志违反 SPEC_REFERENCE §6.1.2，属上游问题，已记录不修改。）

---

## 4. 预期数据说明

- 错误码契约：`0` / `-EINVAL`（驱动源码分支，行号见上表）。
- 寄存器回读：与写入值一致；位宽受限字段（`WHITELIST_ADDRESS` 7 bit、
  `SCL_HOLD_TIME` 16 bit）以 `reg_i2c_filter.h` 的位域声明为准，**不用猜**。
- **未经实测的常量**（全部集中、可一次改正）：
  - 下游从设备地址 `0x51` / `0x52`（`i2cf_test.h` 的 `I2CF_SLAVE_ADDR_A/B`）；
  - 上游 master 实例（`app.overlay` 的 alias `i2cfupstream`）。
- 三项行为层用例的失败信息均带**实际读回字节 / 实际 ret / 目标地址**，用于区分
  "地址或器件不符"与"滤波器/接线问题"；`isr_addr_beyond_whitelist` 与
  `filter_blocks_nonwhitelist` 内置对照（前者用滤波器锁存信息，后者用"**只改白名单授权**（单变量）后同址应成功"；
  ★ 2026-09-23：原"关 filter 后同址应成功"**不可满足、已废**、勿再用 —— 注意因果：**不是**"关滤波器 ≠ 直通"（该判据已撤回），而是**断言落在被拒事务紧邻位置不可测**；见 KNOWN_ISSUES #112 / #116）。
- 无外部黄金向量文件（见 `expected/README.md`）。

---

## 5. 跳过项

| 跳过的 case | 判据类型 | 原因 |
|---|---|---|
| `COMMAND_BEYOND_WHITELIST` 分支的**独立**触发（`i2c_filter.c:96-100`） | **缺硬件条件** | 要单独触发"地址合法但命令码不在位图内"，需在上游精确构造一条只含未授权命令码的 SMBus 事务（含 PEC/块传输形态），且需先确认从设备的寄存器偏移语义。当前只覆盖**地址**维度（`ADDRESS_BEYOND_WHITELIST`） |
| `master_write_mode` 行为 | **驱动未实现** | `SMBUS_FILTER_SET.MASTER_WRITE_MODE` 在驱动内被**恒写 0**（`:234`），无对应 API 与分支，`filter_set_bits` 只断言其保持 0 |
| 多实例并发（`i2cf1..4` 同时被访问） | **缺硬件条件** | i2cf1/2/3 的 I 侧（PB09/PB05/PB13 等）未接线，无上游 master 可驱动；且不在用户确认的链路上 |
| `i2cf1/i2cf2/i2cf3` 的行为层用例 | **缺硬件条件** | 仅 `i2cf4` 在用户确认的链路上；其余三个只做到 `device_ready` 级验证 |

---

## 6. I2CF 待实测确认清单（给最终收尾用）

> 用户裁定："*我应该重新给你过 I2CF 记录的信息了，如果还行不通就先搁置，收尾我再单独看*"。
> 故本节只**登记待确认项**，不再反复推导。

### 6.1 已由 DT 证明的（无需再确认）

| # | 结论 | 证据 |
|---|---|---|
| D1 | `i2cf4.i2c = <&i2c4>` → **配对控制器 = i2c4** | 生效 `zephyr.dts` |
| D2 | `i2cf4.SCLO/SDAO = PE02/PB00`，与 `i2c4` **同引脚**（滤波器输出/下游侧） | 生效 `zephyr.dts` + 模块 dtsi `:894-902` |
| D3 | `i2cf4.SCLI/SDAI = PE03/PB02`（滤波器输入/上游侧，独立引脚） | 生效 `zephyr.dts` |
| D4 | **i2c4 不可能是被门控的上游 master**（引脚互斥，两角色不可共存） | 由 D1+D2 推出 |
| D5 | 被指定为上游 master 的实例须删 `pinctrl-noinit`，否则引脚不挂 I2C 功能、总线无波形 | `i2c_ls.c:901`；已在生效 DT 核实删除生效 |

### 6.2 待实测确认（按优先级）

| # | 待确认项 | 怎么确认 | 影响 |
|---|---|---|---|
| T1 | **上游 master 是哪个实例** | 看跳线落在哪一路；或在候选实例上跑 `isr_addr_beyond_whitelist`，**ISR 触发即证明该实例是上游 master** | 决定 overlay `i2cfupstream` 取值（改 2 行，用例零改动） |
| T2 | `i2cf4` 的 O 侧（PE02/PB00）是否真有从设备 | 跑 `slave_addr_scan`，看 `INFO slave_addr_scan: count=?,addrs=?` | 决定 B 组三项是跑还是 SKIP |
| T3 | `SMBF_NONWHITELIST` 字段语义（`ERROR_ADDRESS` / `ERROR_COMMAND`） | 跑 `isr_addr_beyond_whitelist`，看 `INFO` 上报的 `nw_addr` / `nw_cmd` 是否等于靶地址 | 目前只断言"非零"，字段语义确认后可加强断言 |
| T4 | 上游总线速率/上拉是否满足（i2c2/i2c3 的 pinctrl 未显式 `bias-pull-up`） | 实测是否出现随机 NACK | 影响 `filter_passthrough_whitelist` 的稳定性 |
| T5 | 用户口述「I2C2 › I2CF4 › I2C3」指**配对关系**还是**跳线链路** | 问用户 | 若指配对关系则与 DT 矛盾（i2c2 配对 i2cf2），需澄清 |

### 6.3 已评估、未落地（用户可选的下一步）

**片内 software target（免外接从设备）**：`zephyr,i2c-target-eeprom`
（`zephyr/drivers/i2c/target/eeprom_target.c`）可挂在被跳线到 O 侧的那路控制器上，
作为**片内从设备**，从而把 B 组三项转成零外部器件。要点：
- 驱动 init **不自注册**，需应用调 `i2c_target_driver_register(dev)`
  （与模块 `samples/i2c_filter_loopback/src/main.c:111,119` 同法）；
- 需 `CONFIG_I2C_TARGET=y` + `CONFIG_I2C_EEPROM_TARGET=y`；
- ⚠️ **不得把 target 挂在与 master 同一实例上** —— 会自应答、绕过整个滤波器，制造假 PASS。

**未落地的原因**：它同样受 T1（哪路在 O 侧）未决接线影响。T1 确认后可一键补上。

---

## 7. 上游缺陷观察（只读记录，未修改 `modules/lib/pfr_tpm/`）

| # | 位置 | 问题 | 后果 |
|---|---|---|---|
| 1 | `i2c_filter.c:271` / `:285` | `switch_to_master` 丢弃 `i2c_ls_pinctrl()` 返回值；`switch_to_filter` 丢弃 `pinctrl_apply_state()` 返回值 | 引脚切换失败仍报 `0`（"切换成功"被静默谎报）。**驱动仍未修**（本轮不改驱动）；但 `switch_master_filter` 已改为**直接读 `IO_FUNC` 复用位**（2026-09-18 F1 波），故"引脚没真的切过去"现在**能被发现**（返回值本身仍不可信） |
| 2 | `i2c_filter.c:94` / `:97` | ISR 内 `DEV_ERR`（打日志） | 违反 SPEC_REFERENCE §6.1.2；`CONFIG_LOG_MODE_IMMEDIATE` 下会阻塞。本工程用 deferred 模式规避 |
| 3 | `i2c_filter.c:69-80` | `register_callback` 忽略 `callback_idx` | 多回调注册会互相覆盖 |
| 4 | `i2c_filter.c:102-104` | 回调在**中断上下文**被调用，无任何约束说明 | 回调若调用阻塞/加锁 API 即构成 ISR 违规 |
| 5 | `i2c_filter.c:52` | `config_scl_hold_time()` 未在 `i2c_filter.h` 声明（非 static 却在头文件外） | API 面不一致；调用方须自行 `extern`（本工程如此） |

---

## 8. 生成说明（2026-09-17，只生成 + 编译通过，未烧录）

> **2026-10-15 增补**：新增两个正式回归 case —— `case_enum_matrix`（`i2cf::enum_matrix`，
> `CONFIG_I2CF_ENUM_MATRIX`）与 `case_scan_burst`（`i2cf::scan_burst_drain`，
> `CONFIG_I2CF_SCAN_BURST`）。两者各含 **18 项公共项 + 1 项本 case 专属项**（两开关默认 `n`，
> `case_default` 不编入相关源）。均登记进 `tests/cases.yaml` 与 `run_test.py`
> 的 `TEST_NAMES_BY_CASE`（判据不新增判定语义，仅按 case 取项表）。

- **共 18 项**，编译通过（`west build` rc=0，`lsqsh_evb@1os/lsqsh/cpu1`，0 error / 0 warning）。
- 演进：首轮 14 项（寄存器/API 契约 + 并发 + 旁路读写）→ 用户更正接线后 +3 项行为层
  → 按"零外部器件优先"重构（`isr_addr_beyond_whitelist` 去掉从设备依赖）
  → 按用户裁定把 EEPROM 降级：**+1 项 `slave_addr_scan`（全地址扫描）**，
  原"双地址候选探测"作废，B 组 3 项改走 **`ztest_test_skip()`**（扫不到从设备即 SKIP，非 FAIL）。
- **SKIP vs NOT_RUN**：`SKIP` 是 `ztest_test_skip()` 的主动跳过 —— 不计入 `ran`、不阻塞
  `case_ok`、不计 PASS 率分母，报告里以 `⏭️ SKIP` 单独标注。`NOT_RUN` 是"该项从未执行" =
  **无确定结果**（≠ 主动跳过），常规路径下**不得作为判 PASS 的依据**（本族有 NOT_RUN 即 case FAIL，见 `SPEC_REFERENCE` §6.2.4 F2/F5）。
- 每项自带前置 `i2cf_reset_all()`（`cold_reset` + 显式关闭全部 16 通道）——
  `cold_reset` 只清命令表、**不清** `WHITELIST_ADDRESS_*` 与 `SMBF_ADDRESS_ENABLE`，
  故必须额外关通道才能保证用例间零残留。行为层三项收尾统一回
  `_en(false,false,false)` + `switch_to_filter`。
- ISR 回调 `i2cf_isr_cb()` 在**中断上下文**只置 `volatile` 标志，不打日志/不拿锁
  （SPEC_REFERENCE §6.1.2）。事务前清 `INTR_CLR` 并把标志归零，保证归因唯一。
- 并发用例用栈上 32 B bitmap（**不走堆**），线程优先级 8/9 制造抢占。
- 上手前必读 §1（拓扑）与 §1.4（`pinctrl-noinit`），以及 §6 未决项。

---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
>
> **登记规则**：只能落到**单个标量**上的确定预期才登记断言（取该量的自然 INFO 键名，
> 即使当前未上报 —— 那时判定为 `NO_ACTUAL`）；断言是"多子返回值合取 / 结构性质 /
> 观测项"的一律写 `!none`，不硬凑。
>
> ★ 本工程 18 项里只有 4 项有 INFO 通道（`cold_reset_state` / `addr_7bit_field` /
> `concurrent_fill` / `slave_addr_scan`），且 `slave_addr_scan` 原表自述**无判别力**。
> 其余 14 项的预期全在**寄存器读回**层面，当前无上报 ⇒ 独立预期覆盖率天然很低，
> 这是覆盖缺口（不是"这些项没必要验"）。

```expect
# 测试项 | 断言 | 来源标签:说明
cold_reset_state | reg_enable == 0x1; intr_msk == 0x3; scl_hold == 0xfa | spec:SMBF_REG_ENABLE 复位值 0x1、INTR_MSK 复位值 0x3（reg_i2c_filter.h）+ DT scl-hold-time=0xfa（lsqsh_pinctrl_pfr_tpm.dtsi:19）
addr_7bit_field | max_ok == 0x7f; ovf_readback == 0 | handcalc:WHITELIST_ADDRESS 字段宽 7 位 ⇒ 0x7f 原样回读、0x80 的高位被截断为 0
concurrent_fill | threads == 2; rounds == 4; channels == 16 | handcalc:测试设计常量（双线程各 8 通道 × 4 轮）
slave_addr_scan | !none | !none:原表自述"观测项，无判别力"——应答清单由外部接线决定，不设独立预期（这正是它自述的语义）
enum_matrix | !none | !none:9 格×读/写 的 ret/isr/nw 三元组的合取，无单一标量（判据表见上方覆盖矩阵行）
scan_burst_drain | !none | !none:三种事务类型 isr==probed + elapsed 上界的合取，非单一标量
device_ready | !none | !none:四个实例的 device_is_ready/绑定关系，多子结果合取，无单一标量
enable_channel_boundary | !none | !none:idx 15 成功与 idx 16/255 各自 -EINVAL 的合取，无单一标量
fill_dump_roundtrip | !none | !none:16 通道 addr/bitmap 逐项比对 + 寄存器全 1 的合取
invalid_args | !none | !none:四种非法入参各自 -EINVAL 的合取
duplicate_addr_rejected | !none | !none:三条路径（同址拒绝/关通道后放行/同址填自身）合取
en_clears_table | !none | !none:16×8 字回读全 0，非单一标量
filter_set_bits | !none | !none:4 种入参组合的位回读表，非单一标量
scl_hold_time_roundtrip | !none | !none:5 个值各自原样回读的合取
switch_master_filter | !none | !none:切换返回码 + PE02 复用档位互斥翻转，跨面合取
callback_register | !none | !none:读回 dev->data 的 cb/user_data 字段（白盒布局视图），非单个 INFO 键
isr_addr_beyond_whitelist | !none | !none:回调计数 + NONWHITELIST 锁存 + 主机侧负值的合取；且**需外部上游跳线**
filter_blocks_nonwhitelist | !none | !none:需下游从设备；扫不到即 SKIP
filter_passthrough_whitelist | !none | !none:需下游从设备做逐字节比对
master_eeprom_rw | !none | !none:需下游从设备做写后读回比对
```
